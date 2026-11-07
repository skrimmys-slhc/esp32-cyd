#pragma once
// Thin wrapper around NimBLE scanning shared by the BLE scanner and the Flock detector.
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <functional>

namespace blescan {
using Handler = std::function<void(NimBLEAdvertisedDevice*)>;
void start(Handler h, bool active = true);  // init BLE + continuous scan
void stop();                                // stop scan + deinit (frees ~50KB RAM)
const char* companyName(uint16_t id);
String hex(const std::string& s, size_t max = 32);
}
