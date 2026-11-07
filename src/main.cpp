// CYD 4.0" multi-tool: Wi-Fi scanner, BLE scanner, smart-home remote
// (Home Assistant + Google Nest), MP3 player with EQ/visualiser, and a
// Flock Safety ALPR camera detector.
#include <Arduino.h>
#include <WiFi.h>
#include "display.h"
#include "ui.h"
#include "led.h"
#include "settings.h"
#include "net.h"
#include "audio_engine.h"
#include "apps.h"

LGFX tft;
using namespace ui;

// ------------------------------------------------------------------ icons
static void iconWifi(int cx, int cy, uint16_t c) {
  for (int r = 34; r >= 10; r -= 12) {
    tft.fillArc(cx, cy + 18, r, r - 6, 225, 315, c);
  }
  tft.fillCircle(cx, cy + 16, 4, c);
}
static void iconBt(int cx, int cy, uint16_t c) {
  int x = cx - 4, t = cy - 26, b = cy + 26;
  for (int d = -1; d <= 1; d++) {
    tft.drawLine(x + d, t, x + d, b, c);
    tft.drawLine(x + d, t, x + 16 + d, cy - 13, c);
    tft.drawLine(x + 16 + d, cy - 13, x - 14 + d, cy + 13, c);
    tft.drawLine(x + d, b, x + 16 + d, cy + 13, c);
    tft.drawLine(x + 16 + d, cy + 13, x - 14 + d, cy - 13, c);
  }
}
static void iconHome(int cx, int cy, uint16_t c) {
  tft.fillTriangle(cx, cy - 28, cx - 32, cy, cx + 32, cy, c);
  tft.fillRect(cx - 22, cy, 44, 26, c);
  tft.fillRect(cx - 7, cy + 8, 14, 18, BG);
  // light bulb glow
  tft.fillCircle(cx + 18, cy - 16, 4, WARN);
}
static void iconMusic(int cx, int cy, uint16_t c) {
  tft.fillEllipse(cx - 14, cy + 18, 10, 8, c);
  tft.fillEllipse(cx + 16, cy + 12, 10, 8, c);
  tft.fillRect(cx - 6, cy - 24, 5, 42, c);
  tft.fillRect(cx + 24, cy - 30, 5, 42, c);
  for (int i = 0; i < 8; i++) tft.drawLine(cx - 6, cy - 24 + i, cx + 28, cy - 30 + i, c);
}
static void iconFlock(int cx, int cy, uint16_t c) {
  // pole-mounted camera
  tft.fillRect(cx - 3, cy - 6, 6, 36, c);
  tft.fillRoundRect(cx - 30, cy - 26, 50, 22, 5, c);
  tft.fillRect(cx + 20, cy - 22, 10, 14, c);
  tft.fillCircle(cx - 14, cy - 15, 6, BG);
  tft.fillCircle(cx - 14, cy - 15, 3, BAD);
}
static void iconGear(int cx, int cy, uint16_t c) {
  for (int a = 0; a < 360; a += 45) {
    float r = a * DEG_TO_RAD;
    tft.fillCircle(cx + cosf(r) * 24, cy + sinf(r) * 24, 7, c);
  }
  tft.fillCircle(cx, cy, 24, c);
  tft.fillCircle(cx, cy, 10, BG);
}

struct Tile {
  const char* name;
  void (*icon)(int, int, uint16_t);
  uint16_t color;
  void (*run)();
};

static const Tile TILES[] = {
    {"Wi-Fi", iconWifi, ACCENT, appWifi},
    {"Bluetooth", iconBt, 0x041F, appBle},
    {"Smart Home", iconHome, GOOD, appHome},
    {"Music", iconMusic, 0xF81F, appMusic},
    {"Flock Detect", iconFlock, BAD, appFlock},
    {"Settings", iconGear, DIM, appSettings},
};
static constexpr int NTILES = sizeof(TILES) / sizeof(TILES[0]);
static constexpr int TW = 150, TH = 132, TX0 = 9, TY0 = 46, TGAP = 6;

static void tileRect(int i, int& x, int& y) {
  x = TX0 + (i % 3) * (TW + TGAP);
  y = TY0 + (i / 3) * (TH + TGAP);
}

static void drawStatus() {
  tft.fillRect(0, 0, SCR_W, 38, HEADER);
  text(12, 19, "CYD Multitool", ACCENT, &fonts::DejaVu18, middle_left);
  String t = net::timeStr();
  if (t.length()) text(SCR_W / 2 + 30, 19, t, TEXT, &fonts::DejaVu18, middle_center);
  if (audio::isPlaying()) text(SCR_W / 2 - 40, 19, audio::isPaused() ? "||" : ">", 0xF81F, &fonts::DejaVu18, middle_center);
  if (net::connected()) {
    int rssi = WiFi.RSSI();
    text(SCR_W - 44, 19, ellipsize(WiFi.SSID(), 110, &fonts::DejaVu12), DIM, &fonts::DejaVu12, middle_right);
    signalBars(SCR_W - 36, 10, rssi);
  } else {
    text(SCR_W - 12, 19, cfg.wifiSsid.length() ? "connecting..." : "no Wi-Fi", DIM, &fonts::DejaVu12,
         middle_right);
  }
}

static void drawHome() {
  clear();
  drawStatus();
  for (int i = 0; i < NTILES; i++) {
    int x, y;
    tileRect(i, x, y);
    tft.fillRoundRect(x, y, TW, TH, 14, PANEL);
    tft.drawRoundRect(x, y, TW, TH, 14, PANEL2);
    TILES[i].icon(x + TW / 2, y + 52, TILES[i].color);
    text(x + TW / 2, y + TH - 18, TILES[i].name, TEXT, &fonts::DejaVu18, middle_center);
  }
}

void calibrateTouch() {
  clear();
  text(SCR_W / 2, SCR_H / 2 - 20, "Touch calibration", ACCENT, &fonts::DejaVu24, middle_center);
  text(SCR_W / 2, SCR_H / 2 + 14, "Tap each arrow tip precisely", TEXT, &fonts::DejaVu18, middle_center);
  tft.calibrateTouch(cfg.cal, TEXT, BG, 24);
  settings::saveCalibration();
  tft.setTouchCalibrate(cfg.cal);
}

void setup() {
  Serial.begin(115200);
  delay(50);
  led::begin();
  settings::begin();

  tft.setPanelOptions(cfg.invert, cfg.bgr);
  tft.init();
  tft.setRotation(1);  // landscape, USB on the right
  tft.setBrightness(cfg.brightness);
  ui::begin();

  clear();
  text(SCR_W / 2, SCR_H / 2 - 16, "CYD Multitool", ACCENT, &fonts::DejaVu40, middle_center);
  text(SCR_W / 2, SCR_H / 2 + 26, sdOk ? "SD card OK" : "No SD card (music/config disabled)", DIM,
       &fonts::DejaVu18, middle_center);
  text(SCR_W / 2, SCR_H - 20, "touch & hold to recalibrate", DIM, &fonts::DejaVu12, middle_center);

  if (cfg.calValid) tft.setTouchCalibrate(cfg.cal);
  // Hold the screen during the splash (or press BOOT) to recalibrate
  bool recal = !cfg.calValid;
  for (int i = 0; i < 120 && !recal; i++) {
    int32_t x, y;
    if (tft.getTouchRaw(&x, &y) || digitalRead(PIN_BOOT_BTN) == LOW) recal = true;
    delay(10);
  }
  if (recal) {
    ui::waitRelease();
    calibrateTouch();
  }

  audio::begin();
  audio::setVolume(cfg.volume);
  audio::setEq(cfg.eq);
  net::begin();
  drawHome();
}

void loop() {
  static uint32_t lastStatus = 0;
  Touch t = poll();
  if (t.pressed) {
    for (int i = 0; i < NTILES; i++) {
      int x, y;
      tileRect(i, x, y);
      if (t.x >= x && t.x < x + TW && t.y >= y && t.y < y + TH) {
        tft.drawRoundRect(x, y, TW, TH, 14, ACCENT);
        tft.drawRoundRect(x + 1, y + 1, TW - 2, TH - 2, 14, ACCENT);
        waitRelease();
        TILES[i].run();
        waitRelease();
        drawHome();
        break;
      }
    }
  }
  musicBackgroundTick();
  if (millis() - lastStatus > 5000) {
    lastStatus = millis();
    drawStatus();
  }
  delay(15);
}
