// Bluetooth LE scanner: live list of nearby devices with vendor / tracker
// identification (AirTag / Find My, SmartTag, Tile...) and a details page.
#include <vector>
#include <algorithm>
#include "apps.h"
#include "ui.h"
#include "ble_scan.h"

using namespace ui;

namespace {

struct Dev {
  String addr;
  uint8_t addrType = 0;
  String name;
  int rssi = -127;
  int rssiMax = -127;
  uint16_t company = 0xFFFF;
  String mfg;
  String services;
  String tag;
  int8_t tx = 0;
  bool hasTx = false;
  bool connectable = false;
  uint16_t appearance = 0;
  uint32_t firstSeen = 0, lastSeen = 0, count = 0;
};

constexpr size_t MAX_DEVS = 120;
std::vector<Dev> devs;
SemaphoreHandle_t mtx;
volatile bool paused = false;

String classify(NimBLEAdvertisedDevice* d, uint16_t company, const std::string& md) {
  if (company == 0x004C && md.size() >= 3) {
    switch ((uint8_t)md[2]) {
      case 0x12: return "Find My / AirTag";
      case 0x07: return "AirPods/Beats";
      case 0x02: return "iBeacon";
      case 0x09: return "AirPlay";
      case 0x0C: return "Apple Handoff";
      case 0x0F: return "Apple Nearby Action";
      case 0x10: return "Apple device";
    }
  }
  if (company == 0x0006) return "Microsoft (Swift Pair/CDP)";
  for (int i = 0; i < d->getServiceUUIDCount(); i++) {
    NimBLEUUID u = d->getServiceUUID(i);
    if (u == NimBLEUUID((uint16_t)0xFD5A)) return "Samsung SmartTag";
    if (u == NimBLEUUID((uint16_t)0xFEED) || u == NimBLEUUID((uint16_t)0xFEEC)) return "Tile tracker";
    if (u == NimBLEUUID((uint16_t)0xFE2C)) return "Google Fast Pair";
    if (u == NimBLEUUID((uint16_t)0xFEAA)) return "Eddystone beacon";
    if (u == NimBLEUUID((uint16_t)0xFE9F)) return "Google";
    if (u == NimBLEUUID((uint16_t)0x180D)) return "Heart-rate sensor";
    if (u == NimBLEUUID((uint16_t)0x1812)) return "HID (keyboard/mouse)";
    if (u == NimBLEUUID((uint16_t)0xFCF1)) return "Google (Nest/Home)";
  }
  const char* c = blescan::companyName(company);
  return c ? String(c) : String();
}

void onAdv(NimBLEAdvertisedDevice* d) {
  if (paused) return;
  String addr = d->getAddress().toString().c_str();
  std::string md = d->haveManufacturerData() ? d->getManufacturerData() : std::string();
  uint16_t company = md.size() >= 2 ? ((uint8_t)md[0] | ((uint8_t)md[1] << 8)) : 0xFFFF;
  xSemaphoreTake(mtx, portMAX_DELAY);
  Dev* e = nullptr;
  for (auto& x : devs) if (x.addr == addr) { e = &x; break; }
  if (!e) {
    if (devs.size() >= MAX_DEVS) {
      auto oldest = std::min_element(devs.begin(), devs.end(),
                                     [](const Dev& a, const Dev& b) { return a.lastSeen < b.lastSeen; });
      devs.erase(oldest);
    }
    devs.push_back(Dev());
    e = &devs.back();
    e->addr = addr;
    e->firstSeen = millis();
  }
  e->addrType = d->getAddressType();
  if (d->haveName() && d->getName().size()) e->name = d->getName().c_str();
  e->rssi = d->getRSSI();
  e->rssiMax = max(e->rssiMax, e->rssi);
  if (md.size()) { e->company = company; e->mfg = blescan::hex(md, 40); }
  if (d->getServiceUUIDCount()) {
    String s;
    for (int i = 0; i < d->getServiceUUIDCount() && i < 6; i++) {
      if (i) s += ", ";
      s += d->getServiceUUID(i).toString().c_str();
    }
    e->services = s;
  }
  String tag = classify(d, company, md);
  if (tag.length()) e->tag = tag;
  if (d->haveTXPower()) { e->tx = d->getTXPower(); e->hasTx = true; }
  if (d->haveAppearance()) e->appearance = d->getAppearance();
  e->connectable = d->isConnectable();
  e->lastSeen = millis();
  e->count++;
  xSemaphoreGive(mtx);
}

std::vector<Dev> snapshot() {
  xSemaphoreTake(mtx, portMAX_DELAY);
  std::vector<Dev> v = devs;
  xSemaphoreGive(mtx);
  std::sort(v.begin(), v.end(), [](const Dev& a, const Dev& b) { return a.rssi > b.rssi; });
  return v;
}

void showDetail(const String& addr) {
  uint32_t lastDraw = 0;
  waitRelease();
  while (true) {
    Touch t = poll();
    if (backHit(t)) return;
    if (millis() - lastDraw > 1000) {
      lastDraw = millis();
      Dev d;
      bool found = false;
      xSemaphoreTake(mtx, portMAX_DELAY);
      for (auto& x : devs) if (x.addr == addr) { d = x; found = true; break; }
      xSemaphoreGive(mtx);
      if (!found) return;
      clear();
      header(d.name.length() ? d.name.c_str() : d.addr.c_str(), true);
      int y = 50;
      auto row = [&](const char* k, const String& v) {
        text(12, y, k, DIM, &fonts::DejaVu12);
        text(130, y, ellipsize(v, SCR_W - 140, &fonts::DejaVu12), TEXT, &fonts::DejaVu12);
        y += 22;
      };
      const char* types[] = {"public", "random", "public-id", "random-id"};
      row("Address", d.addr + "  (" + (d.addrType < 4 ? types[d.addrType] : "?") + ")");
      row("RSSI", String(d.rssi) + " dBm   (best " + String(d.rssiMax) + ")");
      if (d.hasTx) {
        // rough distance from path loss: d = 10^((tx - rssi) / (10 * n)), n=2.5
        float m = pow(10.0f, (d.tx - 41 - d.rssi) / 25.0f);  // adv TX power is at 0m; ~-41dBm @1m
        row("TX power", String(d.tx) + " dBm   (~" + String(m, 1) + " m)");
      }
      const char* cn = blescan::companyName(d.company);
      if (d.company != 0xFFFF) {
        char b[8];
        snprintf(b, sizeof(b), "0x%04X", d.company);
        row("Company", String(b) + (cn ? String(" ") + cn : ""));
      }
      if (d.tag.length()) row("Type", d.tag);
      if (d.appearance) row("Appearance", String(d.appearance));
      row("Connectable", d.connectable ? "yes" : "no");
      row("Seen", String(d.count) + " adverts, " + String((millis() - d.firstSeen) / 1000) + " s");
      if (d.services.length()) {
        row("Services", "");
        y -= 22;
        String s = d.services;
        while (s.length() && y < SCR_H - 40) {
          String part = s.substring(0, 52);
          s.remove(0, 52);
          text(130, y, part, TEXT, &fonts::DejaVu12);
          y += 18;
        }
        y += 4;
      }
      if (d.mfg.length()) {
        row("Mfg data", "");
        y -= 22;
        String s = d.mfg;
        while (s.length() && y < SCR_H - 16) {
          text(130, y, s.substring(0, 40), ACCENT, &fonts::DejaVu12);
          s.remove(0, 40);
          y += 18;
        }
      }
      // live RSSI meter (use it to physically locate a device)
      int w = constrain((d.rssi + 100) * 4, 0, 280);
      tft.fillRect(190, SCR_H - 14, 280, 10, PANEL2);
      tft.fillRect(190, SCR_H - 14, w, 10, rssiColor(d.rssi));
    }
    delay(20);
  }
}

}  // namespace

void appBle() {
  mtx = xSemaphoreCreateMutex();
  devs.clear();
  devs.reserve(MAX_DEVS);
  paused = false;
  clear();
  header("Bluetooth LE", true, "Pause");
  busy("Starting BLE scan...");
  blescan::start(onAdv, true);

  ListView lv;
  lv.rowH = 40;
  std::vector<Dev> view;
  uint32_t lastDraw = 0;
  auto draw = [&]() {
    view = snapshot();
    header(("BLE: " + String(view.size()) + " devices" + (paused ? " (paused)" : "")).c_str(), true,
           paused ? "Resume" : "Pause");
    lv.count = view.size();
    lv.draw([&](int i, int x, int y, int w, int h) {
      const Dev& d = view[i];
      bool stale = millis() - d.lastSeen > 15000;
      String title = d.name.length() ? d.name : d.addr;
      text(x + 8, y + 3, ellipsize(title, 250, &fonts::DejaVu18), stale ? DIM : TEXT, &fonts::DejaVu18);
      String sub = d.tag.length() ? d.tag : (d.name.length() ? d.addr : String("unknown"));
      uint16_t sc = d.tag.indexOf("AirTag") >= 0 || d.tag.indexOf("Tile") >= 0 || d.tag.indexOf("SmartTag") >= 0
                        ? WARN : DIM;
      text(x + 8, y + h - 4, ellipsize(sub, 280, &fonts::DejaVu12), sc, &fonts::DejaVu12, bottom_left);
      text(x + w - 40, y + h / 2, String(d.rssi), rssiColor(d.rssi), &fonts::DejaVu12, middle_right);
      signalBars(x + w - 32, y + h / 2 - 9, d.rssi);
    });
  };
  draw();
  waitRelease();
  while (true) {
    Touch t = poll();
    if (backHit(t)) break;
    if (rightHit(t)) { paused = !paused; draw(); waitRelease(); continue; }
    int r = lv.handle(t);
    if (r == -2) { draw(); lastDraw = millis(); }
    if (t.released && abs(t.y - t.sy) < 15 && t.sy > lv.y0 && t.sx < lv.width) {
      int idx = lv.first + (t.sy - lv.y0) / lv.rowH;
      if (idx < (int)view.size()) {
        showDetail(view[idx].addr);
        clear();
        draw();
      }
    }
    if (!t.down && !paused && millis() - lastDraw > 2000) { draw(); lastDraw = millis(); }
    delay(15);
  }
  busy("Stopping BLE...");
  blescan::stop();
  devs.clear();
  devs.shrink_to_fit();
  vSemaphoreDelete(mtx);
}
