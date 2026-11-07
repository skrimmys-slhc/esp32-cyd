#include "ble_scan.h"

namespace blescan {

static Handler s_handler;
static bool s_running = false;

class Cb : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* d) override {
    if (s_handler) s_handler(d);
  }
};
static Cb s_cb;

static void restart(NimBLEScanResults) {
  if (s_running) NimBLEDevice::getScan()->start(0, restart, false);
}

void start(Handler h, bool active) {
  s_handler = h;
  NimBLEDevice::init("");
  NimBLEScan* sc = NimBLEDevice::getScan();
  sc->setAdvertisedDeviceCallbacks(&s_cb, true);
  sc->setActiveScan(active);
  sc->setInterval(97);
  sc->setWindow(67);  // leave air time for Wi-Fi coexistence
  sc->setMaxResults(0);  // we keep our own table
  sc->setDuplicateFilter(false);
  s_running = true;
  sc->start(0, restart, false);
}

void stop() {
  s_running = false;
  if (NimBLEDevice::getInitialized()) {
    NimBLEDevice::getScan()->stop();
    delay(50);
    NimBLEDevice::deinit(true);
  }
  s_handler = nullptr;
}

const char* companyName(uint16_t id) {
  switch (id) {
    case 0x0002: return "Intel";
    case 0x0006: return "Microsoft";
    case 0x000A: return "Qualcomm";
    case 0x000F: return "Broadcom";
    case 0x0046: return "MediaTek";
    case 0x004C: return "Apple";
    case 0x0059: return "Nordic";
    case 0x0075: return "Samsung";
    case 0x0087: return "Garmin";
    case 0x00E0: return "Google";
    case 0x0157: return "Huami";
    case 0x0171: return "Amazon";
    case 0x02E5: return "Espressif";
    case 0x038F: return "Xiaomi";
    case 0x0499: return "Ruuvi";
    case 0x09C8: return "XUNTONG";
    default: return nullptr;
  }
}

String hex(const std::string& s, size_t max) {
  String r;
  char b[4];
  for (size_t i = 0; i < s.size() && i < max; i++) {
    snprintf(b, sizeof(b), "%02X", (uint8_t)s[i]);
    r += b;
  }
  if (s.size() > max) r += "..";
  return r;
}

}  // namespace blescan
