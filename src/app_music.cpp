// Music player: SD library browser, transport controls, 5-band EQ with presets
// and three visualisers (oscilloscope, spectrum analyser, spectrogram).
#include <SD.h>
#include <vector>
#include <algorithm>
#include "apps.h"
#include "ui.h"
#include "settings.h"
#include "audio_engine.h"
#include "dsp.h"

using namespace ui;

namespace {

std::vector<String> tracks;
int current = -1;
bool shuffle = false;
bool libraryLoaded = false;
int vizMode = 0;  // 0 scope, 1 spectrum, 2 spectrogram, 3 off
const char* VIZ_NAMES[] = {"Scope", "Bars", "Spectro", "Off"};

bool isAudio(const String& n) {
  String l = n;
  l.toLowerCase();
  return l.endsWith(".mp3") || l.endsWith(".wav") || l.endsWith(".aac");
}

void scanDir(const String& path, int depth) {
  if (tracks.size() >= 600) return;
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return;
  while (File f = dir.openNextFile()) {
    String name = f.name();
    String full = path == "/" ? "/" + name : path + "/" + name;
    if (name.startsWith(".") || name == "System Volume Information") { f.close(); continue; }
    if (f.isDirectory()) {
      if (depth < 3) scanDir(full, depth + 1);
    } else if (isAudio(name)) {
      tracks.push_back(full);
    }
    f.close();
    if (tracks.size() >= 600) break;
  }
  dir.close();
}

void loadLibrary() {
  if (libraryLoaded) return;
  busy("Scanning SD card...");
  tracks.clear();
  if (sdOk) scanDir("/", 0);
  std::sort(tracks.begin(), tracks.end(), [](const String& a, const String& b) {
    return a.compareTo(b) < 0;
  });
  libraryLoaded = true;
}

String baseName(const String& p) {
  int s = p.lastIndexOf('/');
  int d = p.lastIndexOf('.');
  return p.substring(s + 1, d > s ? d : p.length());
}

String folderName(const String& p) {
  int s = p.lastIndexOf('/');
  if (s <= 0) return "/";
  int s2 = p.lastIndexOf('/', s - 1);
  return p.substring(s2 + 1, s);
}

String fmtTime(uint32_t ms) {
  char b[12];
  snprintf(b, sizeof(b), "%u:%02u", (unsigned)(ms / 60000), (unsigned)((ms / 1000) % 60));
  return b;
}

bool playIndex(int i) {
  if (tracks.empty()) return false;
  i = (i % (int)tracks.size() + tracks.size()) % tracks.size();
  current = i;
  return audio::play(tracks[i]);
}

void nextTrack(int dir = 1) {
  if (tracks.empty()) return;
  if (shuffle && tracks.size() > 1) {
    int n;
    do { n = random(tracks.size()); } while (n == current);
    playIndex(n);
  } else {
    playIndex(current + dir);
  }
}

// ------------------------------------------------------------- visualiser
constexpr int VX = 10, VY = 46, VW = 460, VH = 140;
int16_t samples[audio::SCOPE_LEN];
int16_t prevTop[VW], prevBot[VW];
constexpr int NBARS = 32;
float barLevel[NBARS], barPeak[NBARS];
int16_t barDrawn[NBARS], peakDrawn[NBARS];
int specCol = 0;
float re[512], im[512];

void vizReset() {
  tft.fillRect(VX, VY, VW, VH, BG);
  tft.drawRect(VX - 1, VY - 1, VW + 2, VH + 2, PANEL2);
  for (int i = 0; i < VW; i++) prevTop[i] = prevBot[i] = VY + VH / 2;
  for (int i = 0; i < NBARS; i++) { barLevel[i] = 0; barPeak[i] = 0; barDrawn[i] = 0; peakDrawn[i] = 0; }
  specCol = 0;
  if (vizMode == 0) tft.drawFastHLine(VX, VY + VH / 2, VW, PANEL);
  if (vizMode == 3) text(VX + VW / 2, VY + VH / 2, "visualiser off", DIM, &fonts::DejaVu12, middle_center);
}

uint16_t heat(float v) {  // 0..1 -> black/blue/purple/orange/yellow/white
  v = constrain(v, 0.0f, 1.0f);
  uint8_t r, g, b;
  if (v < 0.25f) { r = 0; g = 0; b = v * 4 * 160; }
  else if (v < 0.5f) { float t = (v - 0.25f) * 4; r = t * 200; g = 0; b = 160 - t * 60; }
  else if (v < 0.75f) { float t = (v - 0.5f) * 4; r = 200 + t * 55; g = t * 160; b = 100 - t * 100; }
  else { float t = (v - 0.75f) * 4; r = 255; g = 160 + t * 95; b = t * 255; }
  return tft.color565(r, g, b);
}

void computeSpectrum(float* out, int nOut) {
  audio::scope(samples, 512);
  for (int i = 0; i < 512; i++) {
    float w = 0.5f - 0.5f * cosf(2 * (float)M_PI * i / 511);
    re[i] = samples[i] * w / 32768.0f;
    im[i] = 0;
  }
  fft(re, im, 512);
  // log-spaced bands from ~60 Hz to ~16 kHz
  float lo = 1.0f, hi = 200.0f;
  for (int b = 0; b < nOut; b++) {
    int k0 = (int)(lo * powf(hi / lo, (float)b / nOut));
    int k1 = (int)(lo * powf(hi / lo, (float)(b + 1) / nOut));
    if (k1 <= k0) k1 = k0 + 1;
    float m = 0;
    for (int k = k0; k < k1 && k < 256; k++) m = max(m, sqrtf(re[k] * re[k] + im[k] * im[k]));
    float db = 20 * log10f(m / 64.0f + 1e-6f);  // 0 dB ~ full-scale sine
    out[b] = constrain((db + 60.0f) / 60.0f, 0.0f, 1.0f);
  }
}

void vizDraw() {
  if (vizMode == 0) {
    audio::scope(samples, audio::SCOPE_LEN);
    int trig = 0;
    for (int i = 1; i < audio::SCOPE_LEN - VW - 1; i++)
      if (samples[i - 1] < 0 && samples[i] >= 0) { trig = i; break; }
    int mid = VY + VH / 2;
    int lastY = mid - samples[trig] * (VH / 2 - 2) / 32768;
    for (int x = 0; x < VW; x++) {
      int y = mid - samples[trig + x] * (VH / 2 - 2) / 32768;
      int top = min(lastY, y), bot = max(lastY, y);
      // erase what this column showed last frame, then draw the new segment
      tft.drawFastVLine(VX + x, prevTop[x], prevBot[x] - prevTop[x] + 1, BG);
      if (prevTop[x] <= mid && prevBot[x] >= mid) tft.drawPixel(VX + x, mid, PANEL);
      tft.drawFastVLine(VX + x, top, bot - top + 1, GOOD);
      prevTop[x] = top;
      prevBot[x] = bot;
      lastY = y;
    }
  } else if (vizMode == 1) {
    float lv[NBARS];
    computeSpectrum(lv, NBARS);
    int bw = VW / NBARS;
    for (int b = 0; b < NBARS; b++) {
      barLevel[b] = lv[b] > barLevel[b] ? lv[b] : barLevel[b] * 0.85f;
      barPeak[b] = barLevel[b] > barPeak[b] ? barLevel[b] : barPeak[b] - 0.01f;
      int h = barLevel[b] * (VH - 4);
      int ph = barPeak[b] * (VH - 4);
      int x = VX + b * bw + 1;
      int base = VY + VH - 1;
      if (h > barDrawn[b]) {
        for (int yy = barDrawn[b]; yy < h; yy += 4) {
          int seg = min(4, h - yy);
          tft.fillRect(x, base - yy - seg + 1, bw - 3, seg, heat(0.35f + 0.65f * yy / (float)VH));
        }
      } else if (h < barDrawn[b]) {
        tft.fillRect(x, base - barDrawn[b] + 1, bw - 3, barDrawn[b] - h, BG);
      }
      barDrawn[b] = h;
      if (ph != peakDrawn[b]) {
        if (peakDrawn[b] > h) tft.drawFastHLine(x, base - peakDrawn[b], bw - 3, BG);
        if (ph > h + 1) tft.drawFastHLine(x, base - ph, bw - 3, TEXT);
        peakDrawn[b] = ph;
      }
    }
  } else if (vizMode == 2) {
    constexpr int BINS = 70;
    float lv[BINS];
    computeSpectrum(lv, BINS);
    int x = VX + specCol;
    for (int b = 0; b < BINS; b++) tft.drawFastVLine(x, VY + VH - (b + 1) * 2, 2, heat(lv[b]));
    specCol = (specCol + 1) % VW;
    tft.drawFastVLine(VX + specCol, VY, VH, ACCENT);
  }
}

// ------------------------------------------------------------- EQ screen
struct Preset { const char* name; int8_t g[audio::EQ_BANDS]; };
const Preset PRESETS[] = {
    {"Flat", {0, 0, 0, 0, 0}},     {"Bass", {8, 4, 0, 0, 0}},     {"Vocal", {-2, 0, 4, 3, 0}},
    {"Treble", {0, 0, 0, 4, 8}},   {"Rock", {5, 2, -2, 3, 5}},    {"Loud", {6, 2, 0, 2, 5}},
};
constexpr int NPRESET = sizeof(PRESETS) / sizeof(PRESETS[0]);
constexpr int EQ_TOP = 70, EQ_BOT = 240;

int eqX(int i) { return 50 + i * 90; }
int dbToY(int db) { return EQ_BOT - (db + 12) * (EQ_BOT - EQ_TOP) / 24; }

void drawSlider(int i) {
  int x = eqX(i);
  tft.fillRect(x - 32, EQ_TOP - 30, 64, EQ_BOT - EQ_TOP + 64, BG);
  text(x, EQ_TOP - 18, (cfg.eq[i] > 0 ? "+" : "") + String(cfg.eq[i]) + "dB", cfg.eq[i] ? ACCENT : DIM,
       &fonts::DejaVu12, middle_center);
  tft.fillRoundRect(x - 3, EQ_TOP, 6, EQ_BOT - EQ_TOP, 3, PANEL2);
  int y0 = dbToY(0), y = dbToY(cfg.eq[i]);
  tft.fillRect(x - 3, min(y, y0), 6, abs(y - y0) + 1, ACCENT);
  tft.drawFastHLine(x - 12, y0, 24, DIM);
  tft.fillRoundRect(x - 24, y - 9, 48, 18, 6, TEXT);
  text(x, EQ_BOT + 20, String(audio::EQ_LABELS[i]) + "Hz", TEXT, &fonts::DejaVu12, middle_center);
}

void eqScreen() {
  clear();
  header("Equalizer", true);
  for (int i = 0; i < audio::EQ_BANDS; i++) drawSlider(i);
  int pw = (SCR_W - 10) / NPRESET;
  for (int p = 0; p < NPRESET; p++) drawBtn(Btn(5 + p * pw, 278, pw - 6, 38, PRESETS[p].name), false, &fonts::DejaVu12);
  waitRelease();
  int drag = -1;
  while (true) {
    Touch t = poll();
    if (backHit(t)) break;
    if (t.pressed) {
      drag = -1;
      for (int i = 0; i < audio::EQ_BANDS; i++)
        if (abs(t.x - eqX(i)) < 40 && t.y > EQ_TOP - 20 && t.y < EQ_BOT + 10) drag = i;
      if (t.y > 278) {
        int p = (t.x - 5) / pw;
        if (p >= 0 && p < NPRESET) {
          memcpy(cfg.eq, PRESETS[p].g, sizeof(cfg.eq));
          for (int i = 0; i < audio::EQ_BANDS; i++) drawSlider(i);
          audio::setEq(cfg.eq);
        }
      }
    }
    if (t.down && drag >= 0) {
      int db = constrain((EQ_BOT - t.y) * 24 / (EQ_BOT - EQ_TOP) - 12, -12, 12);
      if (db != cfg.eq[drag]) {
        cfg.eq[drag] = db;
        drawSlider(drag);
        audio::setEq(cfg.eq);
      }
    }
    if (!t.down) drag = -1;
    delay(15);
  }
  settings::save();
}

// ------------------------------------------------------------- player
constexpr int CB_Y = 266, CB_H = 48, CB_W = 62;
enum { B_SHUF, B_PREV, B_PLAY, B_NEXT, B_VOLD, B_VOLU, B_VIZ, NB };
Btn ctl(int i) { return Btn(8 + i * (CB_W + 5), CB_Y, CB_W, CB_H, ""); }

void drawControls() {
  const char* labels[NB] = {"Shuf", "|<<", audio::isPlaying() && !audio::isPaused() ? "||" : ">", ">>|", "Vol-", "Vol+",
                            VIZ_NAMES[vizMode]};
  for (int i = 0; i < NB; i++) {
    Btn b = ctl(i);
    b.label = labels[i];
    if (i == B_PLAY) b.color = 0x8010;
    drawBtn(b, i == B_SHUF && shuffle, &fonts::DejaVu12);
  }
}

void drawTrackInfo() {
  tft.fillRect(0, VY + VH + 4, SCR_W, CB_Y - VY - VH - 6, BG);
  String title = audio::title();
  String sub = audio::artist();
  if (current >= 0 && current < (int)tracks.size()) {
    if (title.isEmpty()) title = baseName(tracks[current]);
    if (sub.isEmpty()) sub = folderName(tracks[current]);
  } else {
    title = tracks.empty() ? "No audio files on SD card" : "Pick a track";
    sub = "";
  }
  text(SCR_W / 2, VY + VH + 18, ellipsize(title, SCR_W - 20, &fonts::DejaVu18), TEXT, &fonts::DejaVu18,
       middle_center);
  text(SCR_W / 2, VY + VH + 40, ellipsize(sub, SCR_W - 20, &fonts::DejaVu12), DIM, &fonts::DejaVu12, middle_center);
}

void drawProgress() {
  int y = VY + VH + 54;
  float p = audio::isPlaying() ? audio::progress() : 0;
  tft.fillRoundRect(70, y, SCR_W - 140, 8, 4, PANEL2);
  tft.fillRoundRect(70, y, (SCR_W - 140) * p, 8, 4, ACCENT);
  tft.fillRect(0, y - 4, 66, 16, BG);
  tft.fillRect(SCR_W - 66, y - 4, 66, 16, BG);
  text(62, y + 4, fmtTime(audio::isPlaying() ? audio::elapsedMs() : 0), DIM, &fonts::DejaVu12, middle_right);
  text(SCR_W - 62, y + 4, "vol " + String(cfg.volume), DIM, &fonts::DejaVu12, middle_left);
}

void playerScreen() {
  clear();
  header("Now playing", true, "EQ");
  vizReset();
  drawTrackInfo();
  drawProgress();
  drawControls();
  waitRelease();
  uint32_t lastViz = 0, lastProg = 0;
  int shownTrack = current;
  String shownTitle = audio::title();
  while (true) {
    Touch t = poll();
    if (backHit(t)) break;
    if (rightHit(t)) {
      eqScreen();
      clear();
      header("Now playing", true, "EQ");
      vizReset();
      drawTrackInfo();
      drawProgress();
      drawControls();
      waitRelease();
      continue;
    }
    if (t.pressed) {
      for (int i = 0; i < NB; i++) {
        if (!ctl(i).hit(t.x, t.y)) continue;
        switch (i) {
          case B_SHUF: shuffle = !shuffle; break;
          case B_PREV:
            if (audio::elapsedMs() > 4000 && current >= 0) playIndex(current);
            else nextTrack(-1);
            break;
          case B_PLAY:
            if (audio::isPlaying()) audio::togglePause();
            else if (!tracks.empty()) playIndex(current < 0 ? 0 : current);
            break;
          case B_NEXT: nextTrack(1); break;
          case B_VOLD: if (cfg.volume > 0) audio::setVolume(--cfg.volume); break;
          case B_VOLU: if (cfg.volume < 21) audio::setVolume(++cfg.volume); break;
          case B_VIZ: vizMode = (vizMode + 1) % 4; vizReset(); break;
        }
        drawControls();
        drawProgress();
        break;
      }
    }
    // tap the visualiser to cycle modes too
    if (t.pressed && t.y > VY && t.y < VY + VH) { vizMode = (vizMode + 1) % 4; vizReset(); drawControls(); }

    if (audio::finished()) nextTrack(1);
    if (current != shownTrack || audio::title() != shownTitle) {
      shownTrack = current;
      shownTitle = audio::title();
      drawTrackInfo();
      drawControls();
    }
    if (vizMode != 3 && audio::isPlaying() && !audio::isPaused() && millis() - lastViz > 33) {
      lastViz = millis();
      vizDraw();
    }
    if (millis() - lastProg > 500) {
      lastProg = millis();
      drawProgress();
    }
    delay(2);
  }
  settings::save();
}

}  // namespace

// Called from the launcher loop so playback continues across the playlist
void musicBackgroundTick() {
  if (audio::finished() && !tracks.empty()) nextTrack(1);
}

void appMusic() {
  if (!sdOk) {
    clear();
    header("Music", true);
    text(SCR_W / 2, 140, "No SD card detected", BAD, &fonts::DejaVu24, middle_center);
    text(SCR_W / 2, 180, "Insert a FAT32 card with .mp3 / .wav files", DIM, &fonts::DejaVu18, middle_center);
    waitRelease();
    while (!backHit(poll())) delay(20);
    return;
  }
  loadLibrary();
  ListView lv;
  lv.rowH = 40;
  auto draw = [&]() {
    clear();
    header(("Music: " + String(tracks.size()) + " tracks").c_str(), true, "Player");
    lv.count = tracks.size();
    lv.draw([&](int i, int x, int y, int w, int h) {
      bool cur = i == current && audio::isPlaying();
      text(x + 8, y + 3, ellipsize(baseName(tracks[i]), w - 20, &fonts::DejaVu18), cur ? ACCENT : TEXT,
           &fonts::DejaVu18);
      text(x + 8, y + h - 3, ellipsize(folderName(tracks[i]), w - 20, &fonts::DejaVu12), DIM, &fonts::DejaVu12,
           bottom_left);
    });
  };
  if (current >= 0) lv.first = max(0, current - 2);
  draw();
  waitRelease();
  while (true) {
    Touch t = poll();
    if (backHit(t)) break;
    if (rightHit(t)) { playerScreen(); draw(); waitRelease(); continue; }
    if (t.pressed && t.y < HEADER_H && t.x > 70 && t.x < 300) {  // tap title to rescan
      libraryLoaded = false;
      loadLibrary();
      draw();
      continue;
    }
    if (lv.handle(t) == -2) draw();
    if (t.released && abs(t.y - t.sy) < 15 && t.sy > lv.y0 && t.sx < lv.width) {
      int idx = lv.first + (t.sy - lv.y0) / lv.rowH;
      if (idx < (int)tracks.size()) {
        if (!playIndex(idx)) toast("Can't play this file", BAD);
        playerScreen();
        draw();
      }
    }
    musicBackgroundTick();
    delay(15);
  }
}
