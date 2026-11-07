// Flock Safety ALPR camera detector.
// Passive only: sniffs Wi-Fi management frames (channel hopping 1-13) and BLE
// advertisements, matching them against flock_signatures.h. Hits are shown
// live, flagged with the RGB LED + speaker, and logged to /flock_log.csv.
#include <WiFi.h>
#include <esp_wifi.h>
#include <SD.h>
#include <vector>
#include <algorithm>
#include "apps.h"
#include "ui.h"
#include "led.h"
#include "net.h"
#include "settings.h"
#include "ble_scan.h"
#include "audio_engine.h"
#include "flock_signatures.h"

using namespace ui;

namespace {

enum Source : uint8_t { SRC_BEACON, SRC_PROBE, SRC_PROBE_RESP, SRC_BLE };
const char* SRC_NAMES[] = {"Wi-Fi beacon", "Wi-Fi probe", "Wi-Fi probe resp", "BLE"};

struct Hit {
  uint8_t mac[6];
  int8_t rssi;
  uint8_t ch;
  Source src;
  char label[33];
  char reason[24];
};

struct Det {
  uint8_t mac[6];
  String label, reason;
  Source src;
  int rssi, best;
  uint8_t ch;
  uint32_t count, first, last, lastLog;
};

QueueHandle_t hitQ;
std::vector<uint32_t> ouis;            // 24-bit prefixes
std::vector<String> names;             // lower-case patterns
volatile uint32_t wifiFrames = 0, bleAdverts = 0;
volatile uint8_t curCh = 1;

uint32_t parseOui(const String& s) {
  unsigned a, b, c;
  if (sscanf(s.c_str(), "%x:%x:%x", &a, &b, &c) == 3 || sscanf(s.c_str(), "%x-%x-%x", &a, &b, &c) == 3)
    return (a << 16) | (b << 8) | c;
  return 0xFFFFFFFF;
}

void loadSignatures() {
  ouis.clear();
  names.clear();
  for (auto o : FLOCK_OUIS) ouis.push_back(parseOui(o));
  for (auto& o : cfg.flockOuis) { uint32_t v = parseOui(o); if (v != 0xFFFFFFFF) ouis.push_back(v); }
  for (auto n : FLOCK_NAME_PATTERNS) names.push_back(n);
  for (auto n : cfg.flockSsids) { String l = n; l.toLowerCase(); if (l.length()) names.push_back(l); }
}

bool matchOui(const uint8_t* mac) {
  uint32_t p = ((uint32_t)mac[0] << 16) | (mac[1] << 8) | mac[2];
  for (auto o : ouis) if (o == p) return true;
  return false;
}

// case-insensitive substring search, no allocation (safe in Wi-Fi callback)
const char* matchName(const char* s, int len) {
  for (auto& pat : names) {
    int pl = pat.length();
    for (int i = 0; i + pl <= len; i++) {
      int k = 0;
      while (k < pl && tolower((unsigned char)s[i + k]) == pat[k]) k++;
      if (k == pl) return pat.c_str();
    }
  }
  return nullptr;
}

void wifiCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  wifiFrames++;
  auto* p = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* f = p->payload;
  int len = p->rx_ctrl.sig_len - 4;  // strip FCS
  if (len < 24) return;
  uint8_t subtype = (f[0] >> 4) & 0x0F;
  Source src;
  int ie;
  if (subtype == 8) { src = SRC_BEACON; ie = 36; }
  else if (subtype == 5) { src = SRC_PROBE_RESP; ie = 36; }
  else if (subtype == 4) { src = SRC_PROBE; ie = 24; }
  else return;
  const uint8_t* mac = f + 10;  // transmitter

  char ssid[33] = {0};
  int sl = 0;
  if (ie + 2 <= len && f[ie] == 0) {
    sl = min<int>(f[ie + 1], 32);
    if (ie + 2 + sl <= len) memcpy(ssid, f + ie + 2, sl); else sl = 0;
  }
  const char* why = nullptr;
  char reason[24];
  if (sl && (why = matchName(ssid, sl))) snprintf(reason, sizeof(reason), "SSID~\"%s\"", why);
  else if (matchOui(mac)) snprintf(reason, sizeof(reason), "MAC OUI");
  else return;

  Hit h;
  memcpy(h.mac, mac, 6);
  h.rssi = p->rx_ctrl.rssi;
  h.ch = p->rx_ctrl.channel;
  h.src = src;
  memcpy(h.label, ssid, 33);
  memcpy(h.reason, reason, sizeof(reason));
  xQueueSend(hitQ, &h, 0);
}

void bleCb(NimBLEAdvertisedDevice* d) {
  bleAdverts++;
  Hit h = {};
  const uint8_t* a = d->getAddress().getNative();  // little-endian
  for (int i = 0; i < 6; i++) h.mac[i] = a[5 - i];
  std::string name = d->haveName() ? d->getName() : std::string();
  const char* why = nullptr;
  if (name.size() && (why = matchName(name.c_str(), name.size())))
    snprintf(h.reason, sizeof(h.reason), "name~\"%s\"", why);
  else if (d->getAddressType() == BLE_ADDR_PUBLIC && matchOui(h.mac))
    snprintf(h.reason, sizeof(h.reason), "MAC OUI");
  else if (d->haveManufacturerData()) {
    std::string md = d->getManufacturerData();
    if (md.size() >= 2) {
      uint16_t cid = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
      for (auto id : FLOCK_BLE_COMPANY_IDS)
        if (cid == id) snprintf(h.reason, sizeof(h.reason), "BLE company 0x%04X", cid);
    }
  }
  if (!h.reason[0]) return;
  h.rssi = d->getRSSI();
  h.src = SRC_BLE;
  strncpy(h.label, name.c_str(), 32);
  xQueueSend(hitQ, &h, 0);
}

String macStr(const uint8_t* m) {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return b;
}

void logHit(const Det& d) {
  if (!sdOk) return;
  bool fresh = !SD.exists("/flock_log.csv");
  File f = SD.open("/flock_log.csv", FILE_APPEND);
  if (!f) return;
  if (fresh) f.println("time,uptime_s,source,mac,rssi,channel,label,reason,count");
  struct tm tm;
  char ts[24] = "";
  if (getLocalTime(&tm, 0) && tm.tm_year > 120) strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
  String label = d.label;
  label.replace(",", " ");
  f.printf("%s,%lu,%s,%s,%d,%u,%s,%s,%lu\n", ts, (unsigned long)(millis() / 1000), SRC_NAMES[d.src],
           macStr(d.mac).c_str(), d.rssi, d.ch, label.c_str(), d.reason.c_str(), (unsigned long)d.count);
  f.close();
}

}  // namespace

void appFlock() {
  loadSignatures();
  std::vector<Det> dets;
  hitQ = xQueueCreate(32, sizeof(Hit));
  wifiFrames = bleAdverts = 0;

  clear();
  header("Flock detector", true);
  busy("Starting radios...");

  // Wi-Fi: leave the AP, sniff management frames
  net::suspend();
  WiFi.mode(WIFI_STA);
  wifi_promiscuous_filter_t flt = {};
  flt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&flt);
  esp_wifi_set_promiscuous_rx_cb(wifiCb);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(curCh, WIFI_SECOND_CHAN_NONE);
  // BLE: passive scan
  blescan::start(bleCb, false);

  ListView lv;
  lv.y0 = 112;
  lv.rowH = 40;
  uint32_t lastHop = 0, lastDraw = 0, lastAlert = 0, lastStats = 0, statFrames = 0, statT = millis();
  uint32_t fps = 0;
  bool dirty = true;

  auto drawTop = [&]() {
    header("Flock detector", true, cfg.flockBeep ? "Beep: on" : "Beep: off");
    tft.fillRect(0, HEADER_H, SCR_W, lv.y0 - HEADER_H, BG);
    bool alert = lastAlert && millis() - lastAlert < 30000;
    uint16_t c = alert ? BAD : GOOD;
    tft.fillRoundRect(6, HEADER_H + 4, SCR_W - 12, 40, 8, alert ? 0x5000 : 0x0200);
    text(SCR_W / 2, HEADER_H + 24,
         alert ? "FLOCK SIGNATURE NEARBY" : (dets.empty() ? "Scanning - nothing detected" : "Scanning - no recent hits"),
         c, &fonts::DejaVu18, middle_center);
    char b[96];
    snprintf(b, sizeof(b), "Wi-Fi ch %2u  %4lu mgmt/s   BLE adverts %lu   devices %u", curCh, (unsigned long)fps,
             (unsigned long)bleAdverts, (unsigned)dets.size());
    text(10, HEADER_H + 54, b, DIM, &fonts::DejaVu12);
    text(10, HEADER_H + 54 + 16, "Heuristic match on SSID / MAC OUI / BLE data - verify visually.", PANEL2,
         &fonts::DejaVu9);
  };
  auto drawList = [&]() {
    std::sort(dets.begin(), dets.end(), [](const Det& a, const Det& b) { return a.last > b.last; });
    lv.count = dets.size();
    lv.draw([&](int i, int x, int y, int w, int h) {
      const Det& d = dets[i];
      bool recent = millis() - d.last < 30000;
      String title = d.label.length() ? d.label : macStr(d.mac);
      text(x + 8, y + 3, ellipsize(title, 250, &fonts::DejaVu18), recent ? BAD : TEXT, &fonts::DejaVu18);
      String sub = String(SRC_NAMES[d.src]) + "  " + d.reason + "  x" + d.count + "  " +
                   String((millis() - d.last) / 1000) + "s ago";
      text(x + 8, y + h - 3, ellipsize(sub, w - 70, &fonts::DejaVu12), DIM, &fonts::DejaVu12, bottom_left);
      text(x + w - 40, y + h / 2, String(d.rssi), rssiColor(d.rssi), &fonts::DejaVu12, middle_right);
      signalBars(x + w - 32, y + h / 2 - 9, d.rssi);
    });
  };

  clear();
  drawTop();
  drawList();
  waitRelease();
  while (true) {
    Touch t = poll();
    if (backHit(t)) break;
    if (rightHit(t)) {
      cfg.flockBeep = !cfg.flockBeep;
      settings::save();
      drawTop();
      waitRelease();
    }
    if (lv.handle(t) == -2) drawList();

    // channel hopping
    if (millis() - lastHop > 250) {
      lastHop = millis();
      curCh = curCh % 13 + 1;
      esp_wifi_set_channel(curCh, WIFI_SECOND_CHAN_NONE);
    }

    // drain hits
    Hit h;
    while (xQueueReceive(hitQ, &h, 0) == pdTRUE) {
      Det* d = nullptr;
      for (auto& x : dets) if (!memcmp(x.mac, h.mac, 6)) { d = &x; break; }
      bool isNew = !d;
      if (isNew) {
        dets.push_back(Det());
        d = &dets.back();
        memcpy(d->mac, h.mac, 6);
        d->first = millis();
        d->count = 0;
        d->best = -127;
        d->lastLog = 0;
      }
      if (h.label[0]) d->label = h.label;
      d->reason = h.reason;
      d->src = h.src;
      d->rssi = h.rssi;
      d->best = max(d->best, (int)h.rssi);
      d->ch = h.ch;
      d->count++;
      bool wasQuiet = millis() - d->last > 60000;
      d->last = millis();
      if (isNew || millis() - d->lastLog > 60000) { logHit(*d); d->lastLog = millis(); }
      if (isNew || wasQuiet) {
        lastAlert = millis();
        Serial.printf("[FLOCK] %s %s %s rssi %d\n", SRC_NAMES[h.src], macStr(h.mac).c_str(), h.reason, h.rssi);
        if (cfg.flockBeep && !audio::isPlaying()) {
          audio::beep(2200, 120);
          audio::beep(2900, 160);
        }
      } else {
        lastAlert = millis();  // keep the alert banner alive while it stays in range
      }
      dirty = true;
    }

    // LED: red blink while a hit is fresh, slow green heartbeat otherwise
    bool alert = lastAlert && millis() - lastAlert < 30000;
    if (alert) led::set((millis() / 150) & 1, false, false);
    else led::set(false, (millis() % 2000) < 60, false);

    if (millis() - statT >= 1000) {
      fps = (wifiFrames - statFrames) * 1000 / (millis() - statT);
      statFrames = wifiFrames;
      statT = millis();
    }
    if (millis() - lastStats > 1000 && !t.down) {
      lastStats = millis();
      drawTop();
      if (dirty || millis() - lastDraw > 5000) { drawList(); dirty = false; lastDraw = millis(); }
    }
    delay(10);
  }

  busy("Stopping scanners...");
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  blescan::stop();
  vQueueDelete(hitQ);
  led::off();
  net::resume();
}
