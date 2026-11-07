// Wi-Fi scanner: list of APs (RSSI, channel, security), channel occupancy
// graph, details page and connect-with-on-screen-keyboard.
#include <WiFi.h>
#include <vector>
#include <algorithm>
#include "apps.h"
#include "ui.h"
#include "net.h"
#include "settings.h"

using namespace ui;

struct Ap {
  String ssid;
  uint8_t bssid[6];
  int32_t rssi;
  int32_t ch;
  wifi_auth_mode_t enc;
};

static const char* encName(wifi_auth_mode_t e) {
  switch (e) {
    case WIFI_AUTH_OPEN: return "Open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
    default: return "?";
  }
}

static String macStr(const uint8_t* m) {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return b;
}

static std::vector<Ap> doScan() {
  busy("Scanning Wi-Fi...");
  std::vector<Ap> v;
  int n = WiFi.scanNetworks(false, true);
  for (int i = 0; i < n; i++) {
    Ap a;
    a.ssid = WiFi.SSID(i);
    memcpy(a.bssid, WiFi.BSSID(i), 6);
    a.rssi = WiFi.RSSI(i);
    a.ch = WiFi.channel(i);
    a.enc = WiFi.encryptionType(i);
    v.push_back(a);
  }
  WiFi.scanDelete();
  std::sort(v.begin(), v.end(), [](const Ap& a, const Ap& b) { return a.rssi > b.rssi; });
  return v;
}

static void drawChannels(const std::vector<Ap>& aps) {
  clear();
  header("Channel usage (2.4 GHz)", true, "List");
  int x0 = 30, y0 = 290, w = 430, h = 220;
  tft.drawFastHLine(x0, y0, w, DIM);
  float px = w / 14.0f;
  for (int c = 1; c <= 13; c++)
    text(x0 + c * px, y0 + 4, String(c), DIM, &fonts::DejaVu12, top_center);
  for (int r = -90; r <= -30; r += 20) {
    int y = y0 - (r + 100) * h / 70;
    tft.drawFastHLine(x0, y, w, PANEL2);
    text(x0 - 2, y, String(r), DIM, &fonts::DejaVu9, middle_right);
  }
  const uint16_t pal[] = {ACCENT, ACCENT2, GOOD, WARN, 0xF81F, 0x7BFF, BAD, 0xAFE5};
  int i = 0;
  for (auto& a : aps) {
    uint16_t c = pal[i++ % 8];
    int peak = y0 - constrain(a.rssi + 100, 0, 70) * h / 70;
    int cx = x0 + a.ch * px;
    int half = px * 2;  // 20 MHz channel ~ +/-2 channels
    // parabola-ish hump
    int prevX = cx - half, prevY = y0;
    for (int dx = -half; dx <= half; dx += 3) {
      float f = 1.0f - (float)(dx * dx) / (half * half);
      int y = y0 - (y0 - peak) * f;
      tft.drawLine(prevX, prevY, cx + dx, y, c);
      prevX = cx + dx; prevY = y;
    }
    if (a.ssid.length())
      text(cx, peak - 2, ellipsize(a.ssid, 90, &fonts::DejaVu9), c, &fonts::DejaVu9, bottom_center);
  }
}

static void showDetail(const Ap& a) {
  auto draw = [&]() {
    clear();
    header(a.ssid.length() ? a.ssid.c_str() : "(hidden)", true);
    int y = 56;
    auto row = [&](const char* k, const String& v) {
      text(20, y, k, DIM, &fonts::DejaVu18);
      text(170, y, v, TEXT, &fonts::DejaVu18);
      y += 32;
    };
    row("BSSID", macStr(a.bssid));
    row("Channel", String(a.ch) + (a.ch ? String("  (") + (2407 + 5 * a.ch) + " MHz)" : ""));
    row("Signal", String(a.rssi) + " dBm");
    row("Security", encName(a.enc));
    char oui[9];
    snprintf(oui, sizeof(oui), "%02X:%02X:%02X", a.bssid[0], a.bssid[1], a.bssid[2]);
    row("OUI", String(oui) + ((a.bssid[0] & 0x02) ? " (local/random)" : ""));
    bool cur = net::connected() && WiFi.SSID() == a.ssid;
    row("Status", cur ? "connected  " + WiFi.localIP().toString() : "-");
    drawBtn({20, 250, 200, 56, cur ? "Disconnect" : "Connect", cur ? PANEL2 : GOOD});
  };
  draw();
  waitRelease();
  while (true) {
    Touch t = poll();
    if (backHit(t)) return;
    if (t.pressed && t.x > 20 && t.x < 220 && t.y > 250 && t.y < 306) {
      bool cur = net::connected() && WiFi.SSID() == a.ssid;
      if (cur) {
        WiFi.disconnect();
        cfg.wifiSsid = ""; cfg.wifiPass = "";
        settings::save();
      } else {
        String pass = (a.ssid == cfg.wifiSsid) ? cfg.wifiPass : "";
        bool ok = true;
        if (a.enc != WIFI_AUTH_OPEN) ok = textInput(("Password: " + a.ssid).c_str(), pass, true);
        if (ok) {
          draw();
          busy("Connecting...");
          if (net::connect(a.ssid, pass)) toast("Connected: " + WiFi.localIP().toString(), GOOD);
          else toast("Connection failed", BAD);
        }
      }
      draw();
      waitRelease();
    }
    delay(15);
  }
}

void appWifi() {
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  std::vector<Ap> aps = doScan();
  ListView lv;
  bool graph = false;
  auto drawList = [&]() {
    clear();
    header(("Wi-Fi: " + String(aps.size()) + "  [tap to rescan]").c_str(), true, "Graph");
    lv.count = aps.size();
    lv.draw([&](int i, int x, int y, int w, int h) {
      const Ap& a = aps[i];
      String name = a.ssid.length() ? a.ssid : "(hidden " + macStr(a.bssid).substring(9) + ")";
      text(x + 8, y + h / 2, ellipsize(name, 220, &fonts::DejaVu18), a.ssid.length() ? TEXT : DIM,
           &fonts::DejaVu18, middle_left);
      text(x + 240, y + h / 2, "ch" + String(a.ch), DIM, &fonts::DejaVu12, middle_left);
      text(x + 285, y + h / 2, encName(a.enc), a.enc == WIFI_AUTH_OPEN ? WARN : DIM, &fonts::DejaVu12,
           middle_left);
      text(x + w - 40, y + h / 2, String(a.rssi), rssiColor(a.rssi), &fonts::DejaVu12, middle_right);
      signalBars(x + w - 32, y + h / 2 - 9, a.rssi);
    });
    // rescan button in the scroll rail gap
  };
  auto redraw = [&]() { if (graph) drawChannels(aps); else drawList(); };
  redraw();
  waitRelease();
  uint32_t lastTap = 0;
  while (true) {
    Touch t = poll();
    if (backHit(t)) return;
    if (rightHit(t)) {
      graph = !graph;
      redraw();
      waitRelease();
      continue;
    }
    // tap the title area to rescan
    if (t.pressed && t.y < HEADER_H && t.x > 70 && t.x < 300) {
      aps = doScan();
      lv.first = 0;
      redraw();
      waitRelease();
      continue;
    }
    if (!graph) {
      int r = lv.handle(t);
      if (r == -2) drawList();
      if (t.released && abs(t.y - t.sy) < 15 && t.sy > lv.y0 && t.sx < lv.width && millis() - lastTap > 300) {
        int idx = lv.first + (t.sy - lv.y0) / lv.rowH;
        if (idx < (int)aps.size()) {
          showDetail(aps[idx]);
          drawList();
          lastTap = millis();
        }
      }
    }
    delay(15);
  }
}
