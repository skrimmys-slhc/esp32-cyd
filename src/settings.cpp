#include "settings.h"
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include "pins.h"

Config cfg;
bool sdOk = false;
static Preferences prefs;
SPIClass sdSpi(VSPI);

static void loadList(const char* key, std::vector<String>& out) {
  out.clear();
  String s = prefs.getString(key, "");
  int start = 0;
  while (start < (int)s.length()) {
    int nl = s.indexOf('\n', start);
    if (nl < 0) nl = s.length();
    if (nl > start) out.push_back(s.substring(start, nl));
    start = nl + 1;
  }
}

static void saveList(const char* key, const std::vector<String>& v) {
  String s;
  for (auto& e : v) { s += e; s += '\n'; }
  prefs.putString(key, s);
}

static void jsonList(JsonVariantConst arr, std::vector<String>& out) {
  if (!arr.is<JsonArrayConst>()) return;
  out.clear();
  for (JsonVariantConst v : arr.as<JsonArrayConst>()) out.push_back(v.as<String>());
}

namespace settings {

bool loadSdConfig() {
  if (!sdOk || !SD.exists("/config.json")) return false;
  File f = SD.open("/config.json");
  if (!f) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf("config.json parse error: %s\n", err.c_str());
    return false;
  }
  auto str = [&](const char* k, String& dst) {
    if (doc[k].is<const char*>()) dst = doc[k].as<String>();
  };
  // Wi-Fi from the card only seeds NVS if nothing was set on-device yet
  if (cfg.wifiSsid.isEmpty()) { str("wifi_ssid", cfg.wifiSsid); str("wifi_pass", cfg.wifiPass); }
  str("timezone", cfg.tz);
  str("ha_url", cfg.haUrl);
  str("ha_token", cfg.haToken);
  jsonList(doc["ha_entities"], cfg.haEntities);
  str("nest_project_id", cfg.nestProject);
  str("nest_client_id", cfg.nestClientId);
  str("nest_client_secret", cfg.nestClientSecret);
  str("nest_refresh_token", cfg.nestRefreshToken);
  jsonList(doc["flock_extra_ssids"], cfg.flockSsids);
  jsonList(doc["flock_extra_ouis"], cfg.flockOuis);
  while (cfg.haUrl.endsWith("/")) cfg.haUrl.remove(cfg.haUrl.length() - 1);

  prefs.putString("tz", cfg.tz);
  prefs.putString("haUrl", cfg.haUrl);
  prefs.putString("haTok", cfg.haToken);
  saveList("haEnt", cfg.haEntities);
  prefs.putString("nProj", cfg.nestProject);
  prefs.putString("nCid", cfg.nestClientId);
  prefs.putString("nSec", cfg.nestClientSecret);
  prefs.putString("nRef", cfg.nestRefreshToken);
  saveList("fSsid", cfg.flockSsids);
  saveList("fOui", cfg.flockOuis);
  save();
  return true;
}

void begin() {
  prefs.begin("cyd", false);
  cfg.wifiSsid = prefs.getString("ssid", "");
  cfg.wifiPass = prefs.getString("pass", "");
  cfg.tz = prefs.getString("tz", cfg.tz);
  cfg.haUrl = prefs.getString("haUrl", "");
  cfg.haToken = prefs.getString("haTok", "");
  loadList("haEnt", cfg.haEntities);
  cfg.nestProject = prefs.getString("nProj", "");
  cfg.nestClientId = prefs.getString("nCid", "");
  cfg.nestClientSecret = prefs.getString("nSec", "");
  cfg.nestRefreshToken = prefs.getString("nRef", "");
  loadList("fSsid", cfg.flockSsids);
  loadList("fOui", cfg.flockOuis);
  cfg.brightness = prefs.getUChar("bright", cfg.brightness);
  cfg.volume = prefs.getUChar("vol", cfg.volume);
  prefs.getBytes("eq", cfg.eq, sizeof(cfg.eq));
  cfg.invert = prefs.getBool("inv", LCD_INVERT_DEFAULT);
  cfg.bgr = prefs.getBool("bgr", LCD_BGR_DEFAULT);
  cfg.flockBeep = prefs.getBool("fBeep", true);
  cfg.calValid = prefs.getBytes("cal", cfg.cal, sizeof(cfg.cal)) == sizeof(cfg.cal);

  sdSpi.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  sdOk = SD.begin(PIN_SD_CS, sdSpi, 20000000);
  Serial.printf("SD card: %s\n", sdOk ? "mounted" : "not found");
  if (sdOk) loadSdConfig();
}

void save() {
  prefs.putString("ssid", cfg.wifiSsid);
  prefs.putString("pass", cfg.wifiPass);
  prefs.putUChar("bright", cfg.brightness);
  prefs.putUChar("vol", cfg.volume);
  prefs.putBytes("eq", cfg.eq, sizeof(cfg.eq));
  prefs.putBool("inv", cfg.invert);
  prefs.putBool("bgr", cfg.bgr);
  prefs.putBool("fBeep", cfg.flockBeep);
}

void saveCalibration() {
  prefs.putBytes("cal", cfg.cal, sizeof(cfg.cal));
  cfg.calValid = true;
}

}  // namespace settings
