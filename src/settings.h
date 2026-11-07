#pragma once
#include <Arduino.h>
#include <vector>

// Persistent settings live in NVS (Preferences). Secrets / long strings
// (Home Assistant token, Nest OAuth) are read from /config.json on the SD card
// at boot and cached in NVS, so the card can be removed afterwards.
struct Config {
  // Wi-Fi
  String wifiSsid, wifiPass;
  String tz = "UTC0";  // POSIX TZ string, e.g. "EST5EDT,M3.2.0,M11.1.0"

  // Home Assistant
  String haUrl;    // e.g. http://homeassistant.local:8123
  String haToken;  // long-lived access token
  std::vector<String> haEntities;  // optional allow-list; empty = auto

  // Google Nest (Smart Device Management API)
  String nestProject, nestClientId, nestClientSecret, nestRefreshToken;

  // Extra Flock signatures (appended to the built-in list)
  std::vector<String> flockSsids, flockOuis;

  // UI / audio
  uint8_t brightness = 200;
  uint8_t volume = 12;  // 0..21
  int8_t eq[5] = {0, 0, 0, 0, 0};  // dB per band
  bool invert = false, bgr = false;
  bool flockBeep = true;
  uint16_t cal[8] = {0};
  bool calValid = false;
};

extern Config cfg;
extern bool sdOk;

namespace settings {
void begin();              // mount SD, load NVS, merge /config.json
void save();               // persist UI/audio + wifi to NVS
void saveCalibration();
bool loadSdConfig();       // re-read /config.json (returns true if found)
}
