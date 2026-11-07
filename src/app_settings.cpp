// Settings: brightness, panel colour fixes, touch calibration, SD config
// reload, Wi-Fi forget, system info, reboot.
#include <WiFi.h>
#include <SD.h>
#include "apps.h"
#include "ui.h"
#include "net.h"
#include "settings.h"
#include "audio_engine.h"

using namespace ui;

static void drawSlider(int y, const char* label, int val, int maxv) {
  tft.fillRect(0, y - 4, SCR_W, 50, BG);
  text(16, y + 2, label, DIM, &fonts::DejaVu12);
  text(SCR_W - 16, y + 2, String(val), TEXT, &fonts::DejaVu12, top_right);
  tft.fillRoundRect(16, y + 20, SCR_W - 32, 22, 8, PANEL2);
  tft.fillRoundRect(16, y + 20, (SCR_W - 32) * val / maxv, 22, 8, ACCENT);
}

static void infoPage() {
  clear();
  header("System info", true);
  int y = 56;
  auto row = [&](const char* k, const String& v) {
    text(16, y, k, DIM, &fonts::DejaVu18);
    text(180, y, v, TEXT, &fonts::DejaVu18);
    y += 28;
  };
  row("Wi-Fi", net::connected() ? WiFi.SSID() + " (" + WiFi.RSSI() + " dBm)" : String("disconnected"));
  row("IP", net::connected() ? WiFi.localIP().toString() : String("-"));
  row("MAC", WiFi.macAddress());
  row("Free heap", String(ESP.getFreeHeap() / 1024) + " KB (min " + String(ESP.getMinFreeHeap() / 1024) + ")");
  row("SD card", sdOk ? String((uint32_t)(SD.cardSize() / (1024 * 1024))) + " MB" : String("none"));
  row("Uptime", String(millis() / 60000) + " min");
  row("Chip", String(ESP.getChipModel()) + " rev" + ESP.getChipRevision() + " " + ESP.getCpuFreqMHz() + "MHz");
  row("Firmware", String(__DATE__) + " " + __TIME__);
  waitRelease();
  while (!backHit(poll())) delay(20);
}

void appSettings() {
  const int BY = 186;
  Btn bInv(10, BY, 150, 40, "Invert colors"), bBgr(165, BY, 150, 40, "Swap R/B"), bCal(320, BY, 150, 40, "Calibrate");
  Btn bCfg(10, BY + 46, 150, 40, "Reload SD cfg"), bForget(165, BY + 46, 150, 40, "Forget Wi-Fi"),
      bInfo(320, BY + 46, 150, 40, "System info");
  Btn bBeep(10, BY + 92, 150, 40, ""), bTest(165, BY + 92, 150, 40, "Test speaker"),
      bReboot(320, BY + 92, 150, 40, "Reboot", 0x8000);
  auto draw = [&]() {
    clear();
    header("Settings", true);
    drawSlider(50, "Screen brightness", cfg.brightness, 255);
    drawSlider(110, "Volume", cfg.volume, 21);
    bBeep.label = cfg.flockBeep ? "Flock beep: on" : "Flock beep: off";
    for (Btn* b : {&bInv, &bBgr, &bCal, &bCfg, &bForget, &bInfo, &bBeep, &bTest, &bReboot})
      drawBtn(*b, (b == &bInv && cfg.invert) || (b == &bBgr && cfg.bgr), &fonts::DejaVu12);
  };
  draw();
  waitRelease();
  int drag = -1;
  while (true) {
    Touch t = poll();
    if (backHit(t)) break;
    if (t.pressed) {
      drag = (t.y > 64 && t.y < 100) ? 0 : (t.y > 124 && t.y < 160) ? 1 : -1;
      if (bInv.hit(t.x, t.y) || bBgr.hit(t.x, t.y)) {
        if (bInv.hit(t.x, t.y)) cfg.invert = !cfg.invert; else cfg.bgr = !cfg.bgr;
        settings::save();
        tft.setPanelOptions(cfg.invert, cfg.bgr);
        tft.init();
        tft.setRotation(1);
        tft.setBrightness(cfg.brightness);
        if (cfg.calValid) tft.setTouchCalibrate(cfg.cal);
        draw();
      } else if (bCal.hit(t.x, t.y)) {
        waitRelease(); calibrateTouch(); draw();
      } else if (bCfg.hit(t.x, t.y)) {
        toast(settings::loadSdConfig() ? "config.json loaded" : "No /config.json found", ACCENT, 1200);
        draw();
      } else if (bForget.hit(t.x, t.y)) {
        if (confirm("Forget saved Wi-Fi?")) {
          WiFi.disconnect(false, true);
          cfg.wifiSsid = ""; cfg.wifiPass = "";
          settings::save();
        }
        draw();
      } else if (bInfo.hit(t.x, t.y)) {
        infoPage(); draw();
      } else if (bBeep.hit(t.x, t.y)) {
        cfg.flockBeep = !cfg.flockBeep; settings::save(); draw();
      } else if (bTest.hit(t.x, t.y)) {
        if (audio::isPlaying()) toast("Music is playing", WARN, 900);
        else { audio::beep(440, 200, 12); audio::beep(880, 200, 12); audio::beep(1320, 300, 12); }
      } else if (bReboot.hit(t.x, t.y)) {
        if (confirm("Reboot now?")) ESP.restart();
        draw();
      }
    }
    if (t.down && drag >= 0) {
      float f = constrain((t.x - 16) / (float)(SCR_W - 32), 0.0f, 1.0f);
      if (drag == 0) {
        uint8_t v = max(10, (int)(f * 255));
        if (v != cfg.brightness) { cfg.brightness = v; tft.setBrightness(v); drawSlider(50, "Screen brightness", v, 255); }
      } else {
        uint8_t v = f * 21 + 0.5f;
        if (v != cfg.volume) { cfg.volume = v; audio::setVolume(v); drawSlider(110, "Volume", v, 21); }
      }
    }
    if (!t.down && drag >= 0) { drag = -1; settings::save(); }
    delay(15);
  }
  settings::save();
}
