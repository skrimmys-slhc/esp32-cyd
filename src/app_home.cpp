// Smart-home remote.
//  * Home Assistant (REST API + long-lived token): lights (on/off, brightness,
//    colour), switches, fans, climate (thermostats), locks, covers, scenes,
//    scripts and camera / doorbell snapshots. Anything you've linked into HA
//    (Google Home / Nest, Hue, TP-Link, etc.) shows up here.
//  * Google Nest direct via the Smart Device Management API: thermostats
//    (setpoint + mode) and status of doorbells / cameras / displays.
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm>
#include "apps.h"
#include "ui.h"
#include "net.h"
#include "settings.h"

using namespace ui;

namespace {

// ===================================================================== HA
struct Entity {
  String id, name, domain, state;
  int brightness = -1;  // 0..255
  float target = NAN, current = NAN;
  String unit;
  String hvacAction;
};

std::vector<Entity> ents;
const char* DOMAINS[] = {"light", "switch", "fan", "climate", "lock", "cover", "camera",
                         "scene", "script", "input_boolean", "media_player"};

bool wantDomain(const String& d) {
  for (auto x : DOMAINS) if (d == x) return true;
  return false;
}

String domainOf(const String& id) { return id.substring(0, id.indexOf('.')); }

void fillEntity(Entity& e, JsonObjectConst o) {
  e.id = o["entity_id"].as<String>();
  e.domain = domainOf(e.id);
  e.state = o["state"].as<String>();
  JsonObjectConst a = o["attributes"];
  e.name = a["friendly_name"] | e.id;
  e.brightness = a["brightness"].is<int>() ? a["brightness"].as<int>() : -1;
  e.target = a["temperature"].is<float>() ? a["temperature"].as<float>() : NAN;
  e.current = a["current_temperature"].is<float>() ? a["current_temperature"].as<float>() : NAN;
  e.hvacAction = a["hvac_action"] | "";
  e.unit = a["unit_of_measurement"] | "";
}

JsonDocument stateFilter() {
  JsonDocument f;
  JsonObject o = f[0].to<JsonObject>();
  o["entity_id"] = true;
  o["state"] = true;
  JsonObject a = o["attributes"].to<JsonObject>();
  for (auto k : {"friendly_name", "brightness", "temperature", "current_temperature", "hvac_action",
                 "unit_of_measurement"})
    a[k] = true;
  return f;
}

// Open a GET to HA and hand back the stream; caller must http.end()
bool haOpen(HTTPClient& http, WiFiClient& plain, WiFiClientSecure& tls, const String& path) {
  if (!net::ensure()) return false;
  String url = cfg.haUrl + path;
  bool ok;
  if (url.startsWith("https://")) { tls.setInsecure(); ok = http.begin(tls, url); }
  else ok = http.begin(plain, url);
  if (!ok) return false;
  http.useHTTP10(true);
  http.setTimeout(15000);
  http.addHeader("Authorization", "Bearer " + cfg.haToken);
  int code = http.GET();
  if (code != 200) { Serial.printf("HA GET %s -> %d\n", path.c_str(), code); http.end(); return false; }
  return true;
}

bool haLoad() {
  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure tls;
  if (!haOpen(http, plain, tls, "/api/states")) return false;
  JsonDocument filter = stateFilter();
  JsonDocument doc;
  http.getStream().setTimeout(5000);
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) { Serial.printf("HA json: %s\n", err.c_str()); return false; }
  ents.clear();
  for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
    String id = o["entity_id"].as<String>();
    bool keep;
    if (!cfg.haEntities.empty()) keep = std::find(cfg.haEntities.begin(), cfg.haEntities.end(), id) != cfg.haEntities.end();
    else keep = wantDomain(domainOf(id));
    if (!keep) continue;
    Entity e;
    fillEntity(e, o);
    ents.push_back(e);
    if (ents.size() >= 200) break;
  }
  std::sort(ents.begin(), ents.end(), [](const Entity& a, const Entity& b) {
    if (a.domain != b.domain) return a.domain < b.domain;
    return a.name.compareTo(b.name) < 0;
  });
  return true;
}

bool haRefresh(Entity& e) {
  String resp;
  int code = net::request("GET", cfg.haUrl + "/api/states/" + e.id, "", resp, cfg.haToken);
  if (code != 200) return false;
  JsonDocument doc;
  if (deserializeJson(doc, resp)) return false;
  fillEntity(e, doc.as<JsonObjectConst>());
  return true;
}

bool haCall(const String& domain, const String& service, Entity& e, JsonDocument* extra = nullptr) {
  JsonDocument body;
  if (extra) body = *extra;
  body["entity_id"] = e.id;
  String b, resp;
  serializeJson(body, b);
  int code = net::request("POST", cfg.haUrl + "/api/services/" + domain + "/" + service, b, resp, cfg.haToken);
  if (code != 200) { toast("HA error " + String(code), BAD); return false; }
  delay(250);
  haRefresh(e);
  return true;
}

bool toggleable(const String& d) {
  return d == "light" || d == "switch" || d == "fan" || d == "input_boolean" || d == "media_player";
}

void haToggle(Entity& e) {
  if (e.domain == "scene" || e.domain == "script") haCall(e.domain, "turn_on", e);
  else if (e.domain == "lock") haCall("lock", e.state == "locked" ? "unlock" : "lock", e);
  else if (e.domain == "media_player") haCall("media_player", "media_play_pause", e);
  else haCall("homeassistant", "toggle", e);
}

uint16_t stateColor(const Entity& e) {
  const String& s = e.state;
  if (s == "on" || s == "open" || s == "unlocked" || s == "playing" || s == "heat" || s == "cool") return WARN;
  if (s == "unavailable" || s == "unknown") return BAD;
  return DIM;
}

String stateText(const Entity& e) {
  if (e.domain == "climate") {
    String s = e.state;
    if (!isnan(e.current)) s += "  " + String(e.current, 1) + "";
    if (!isnan(e.target)) s += " > " + String(e.target, 1) + "";
    return s;
  }
  if (e.domain == "light" && e.state == "on" && e.brightness >= 0) return "on " + String(e.brightness * 100 / 255) + "%";
  if (e.domain == "scene" || e.domain == "script") return "run";
  return e.state;
}

// --------------------------------------------------- camera / doorbell view
void cameraView(Entity& e) {
  clear();
  header(e.name.c_str(), true, "Refresh");
  uint32_t last = 0;
  bool autoRefresh = true;
  waitRelease();
  while (true) {
    Touch t = poll();
    if (backHit(t)) return;
    if (rightHit(t)) { last = 0; autoRefresh = true; }
    if (t.pressed && t.y > HEADER_H) autoRefresh = !autoRefresh;
    if (autoRefresh && (last == 0 || millis() - last > 5000)) {
      last = millis();
      HTTPClient http;
      WiFiClient plain;
      WiFiClientSecure tls;
      tft.fillCircle(SCR_W - 160, 20, 5, ACCENT);
      if (haOpen(http, plain, tls, "/api/camera_proxy/" + e.id + "?width=464&height=270")) {
        int len = http.getSize();
        const int MAXJ = 90 * 1024;
        if (len <= 0 || len > MAXJ) len = MAXJ;
        uint8_t* buf = (uint8_t*)malloc(len);
        int got = 0;
        if (buf) {
          WiFiClient* s = http.getStreamPtr();
          uint32_t t0 = millis();
          while (got < len && millis() - t0 < 8000) {
            int n = s->available();
            if (n > 0) got += s->readBytes(buf + got, min(n, len - got));
            else if (!http.connected()) break;
            else delay(2);
          }
          bool ok = tft.drawJpg(buf, got, 8, HEADER_H + 4, SCR_W - 16, SCR_H - HEADER_H - 8, 0, 0, 0.0f);
          if (!ok) toast("Can't decode image (" + String(got / 1024) + " KB)", BAD, 800);
          free(buf);
        } else {
          toast("Out of memory", BAD, 800);
        }
        http.end();
      } else {
        toast("Snapshot failed", BAD, 800);
      }
      tft.fillCircle(SCR_W - 160, 20, 5, autoRefresh ? GOOD : PANEL2);
    }
    delay(20);
  }
}

// --------------------------------------------------- per-entity control page
void entityPage(Entity& e) {
  if (e.domain == "camera") { cameraView(e); return; }
  const uint16_t SW[] = {0xF800, 0xFD20, 0xFFE0, 0x07E0, 0x07FF, 0x001F, 0xF81F, 0xFFFF};
  const uint8_t SWRGB[][3] = {{255, 0, 0}, {255, 140, 0}, {255, 230, 0}, {0, 255, 0},
                              {0, 255, 255}, {0, 0, 255}, {255, 0, 255}, {255, 255, 255}};
  auto draw = [&]() {
    clear();
    header(e.name.c_str(), true, "Refresh");
    text(20, 60, e.id, DIM, &fonts::DejaVu12);
    text(20, 84, "State: " + stateText(e), stateColor(e), &fonts::DejaVu24);
    if (e.domain == "light") {
      drawBtn(Btn(20, 120, 140, 50, e.state == "on" ? "Turn off" : "Turn on", e.state == "on" ? PANEL2 : GOOD));
      // brightness slider
      text(20, 190, "Brightness", DIM, &fonts::DejaVu12);
      tft.fillRoundRect(20, 206, 440, 30, 8, PANEL2);
      int b = e.state == "on" && e.brightness >= 0 ? e.brightness : 0;
      tft.fillRoundRect(20, 206, 440 * b / 255, 30, 8, WARN);
      for (int i = 0; i < 8; i++) tft.fillRoundRect(20 + i * 56, 256, 48, 48, 8, SW[i]);
    } else if (e.domain == "climate") {
      text(20, 128, "Current " + (isnan(e.current) ? String("-") : String(e.current, 1)) + "   Target " +
                        (isnan(e.target) ? String("-") : String(e.target, 1)) + "",
           TEXT, &fonts::DejaVu18);
      if (e.hvacAction.length()) text(20, 154, "Action: " + e.hvacAction, DIM, &fonts::DejaVu12);
      drawBtn(Btn(20, 176, 100, 60, "-", 0x001F), false, &fonts::DejaVu40);
      drawBtn(Btn(140, 176, 100, 60, "+", 0xB800), false, &fonts::DejaVu40);
      const char* modes[] = {"off", "heat", "cool", "heat_cool", "auto"};
      for (int i = 0; i < 5; i++)
        drawBtn(Btn(10 + i * 94, 256, 88, 48, modes[i]), e.state == modes[i], &fonts::DejaVu12);
    } else if (e.domain == "cover") {
      drawBtn(Btn(20, 140, 130, 60, "Open", GOOD));
      drawBtn(Btn(170, 140, 130, 60, "Stop"));
      drawBtn(Btn(320, 140, 130, 60, "Close", BAD));
    } else if (e.domain == "lock") {
      drawBtn(Btn(20, 140, 200, 60, "Lock", GOOD));
      drawBtn(Btn(250, 140, 200, 60, "Unlock", BAD));
    } else if (e.domain == "media_player") {
      drawBtn(Btn(20, 140, 100, 60, "|<<"));
      drawBtn(Btn(130, 140, 100, 60, ">||", GOOD));
      drawBtn(Btn(240, 140, 100, 60, ">>|"));
      drawBtn(Btn(350, 140, 50, 60, "-"));
      drawBtn(Btn(410, 140, 50, 60, "+"));
    } else {
      drawBtn(Btn(20, 140, 200, 60, e.domain == "scene" || e.domain == "script" ? "Activate" : "Toggle", GOOD));
    }
  };
  draw();
  waitRelease();
  while (true) {
    Touch t = poll();
    if (backHit(t)) return;
    if (rightHit(t)) { haRefresh(e); draw(); waitRelease(); continue; }
    if (!t.pressed) { delay(15); continue; }
    int x = t.x, y = t.y;
    bool acted = true;
    JsonDocument p;
    if (e.domain == "light") {
      if (Btn(20, 120, 140, 50, "").hit(x, y)) haToggle(e);
      else if (y >= 200 && y < 242) {
        int pct = constrain((x - 20) * 100 / 440, 1, 100);
        p["brightness_pct"] = pct;
        haCall("light", "turn_on", e, &p);
      } else if (y >= 256 && y < 304 && x >= 20 && x < 20 + 8 * 56) {
        int i = (x - 20) / 56;
        if (i == 7) p["color_temp_kelvin"] = 4000;
        else { JsonArray c = p["rgb_color"].to<JsonArray>(); for (int k = 0; k < 3; k++) c.add(SWRGB[i][k]); }
        haCall("light", "turn_on", e, &p);
      } else acted = false;
    } else if (e.domain == "climate") {
      float step = e.unit.indexOf('F') >= 0 ? 1.0f : 0.5f;
      if (y >= 176 && y < 236 && x < 240 && !isnan(e.target)) {
        p["temperature"] = e.target + (x < 130 ? -step : step);
        haCall("climate", "set_temperature", e, &p);
      } else if (y >= 256 && y < 304) {
        const char* modes[] = {"off", "heat", "cool", "heat_cool", "auto"};
        int i = constrain((x - 10) / 94, 0, 4);
        p["hvac_mode"] = modes[i];
        haCall("climate", "set_hvac_mode", e, &p);
      } else acted = false;
    } else if (e.domain == "cover" && y >= 140 && y < 200) {
      haCall("cover", x < 160 ? "open_cover" : x < 310 ? "stop_cover" : "close_cover", e);
    } else if (e.domain == "lock" && y >= 140 && y < 200) {
      if (x > 240 && !confirm("Unlock " + e.name + "?")) { draw(); continue; }
      haCall("lock", x < 240 ? "lock" : "unlock", e);
    } else if (e.domain == "media_player" && y >= 140 && y < 200) {
      const char* sv = x < 120 ? "media_previous_track" : x < 230 ? "media_play_pause" : x < 340 ? "media_next_track"
                     : x < 400 ? "volume_down" : "volume_up";
      haCall("media_player", sv, e);
    } else if (y >= 140 && y < 200 && x < 220) {
      haToggle(e);
    } else acted = false;
    if (acted) { draw(); waitRelease(); }
    delay(15);
  }
}

void haScreen() {
  if (cfg.haUrl.isEmpty() || cfg.haToken.isEmpty()) {
    clear();
    header("Home Assistant", true);
    int y = 60;
    for (auto l : {"Not configured. Put this in /config.json on the SD card:", "",
                   "  \"ha_url\": \"http://homeassistant.local:8123\",",
                   "  \"ha_token\": \"<long-lived access token>\"", "",
                   "Create the token in HA: Profile > Security >", "Long-lived access tokens. Then reboot.",
                   "", "Tip: link Google Home/Nest, Hue, Kasa... into HA", "and they all show up here."}) {
      text(16, y, l, TEXT, &fonts::DejaVu12);
      y += 22;
    }
    waitRelease();
    while (!backHit(poll())) delay(20);
    return;
  }
  clear();
  header("Home Assistant", true);
  busy("Loading entities...");
  if (!haLoad()) {
    toast(net::connected() ? "Can't reach Home Assistant" : "Wi-Fi not connected", BAD, 1800);
    return;
  }

  const char* chips[] = {"All", "Lights", "Switch", "Climate", "Camera", "Lock", "Scene"};
  const char* chipDom[][3] = {{nullptr}, {"light"}, {"switch", "fan", "input_boolean"}, {"climate"},
                              {"camera"}, {"lock", "cover"}, {"scene", "script", "media_player"}};
  int chip = 0;
  std::vector<int> view;
  ListView lv;
  lv.y0 = HEADER_H + 40;
  lv.rowH = 40;

  auto rebuild = [&]() {
    view.clear();
    for (int i = 0; i < (int)ents.size(); i++) {
      if (chip == 0) { view.push_back(i); continue; }
      for (auto d : chipDom[chip]) if (d && ents[i].domain == d) { view.push_back(i); break; }
    }
    lv.count = view.size();
  };
  auto draw = [&]() {
    clear();
    header(("Home Assistant (" + String(ents.size()) + ")").c_str(), true, "Refresh");
    for (int i = 0; i < 7; i++) drawBtn(Btn(4 + i * 68, HEADER_H + 4, 64, 32, chips[i]), i == chip, &fonts::DejaVu12);
    lv.draw([&](int vi, int x, int y, int w, int h) {
      Entity& e = ents[view[vi]];
      text(x + 8, y + 3, ellipsize(e.name, w - 110, &fonts::DejaVu18), TEXT, &fonts::DejaVu18);
      text(x + 8, y + h - 3, ellipsize(stateText(e), w - 110, &fonts::DejaVu12), stateColor(e), &fonts::DejaVu12,
           bottom_left);
      if (toggleable(e.domain) || e.domain == "scene" || e.domain == "script") {
        bool on = e.state == "on" || e.state == "playing";
        int bx = x + w - 74, by = y + h / 2 - 12;
        tft.fillRoundRect(bx, by, 64, 24, 12, on ? GOOD : BG);
        tft.fillCircle(on ? bx + 52 : bx + 12, by + 12, 9, TEXT);
      }
    });
  };
  rebuild();
  draw();
  waitRelease();
  while (true) {
    Touch t = poll();
    if (backHit(t)) return;
    if (rightHit(t)) { busy("Refreshing..."); haLoad(); rebuild(); draw(); waitRelease(); continue; }
    if (t.pressed && t.y >= HEADER_H && t.y < lv.y0) {
      int c = (t.x - 4) / 68;
      if (c >= 0 && c < 7) { chip = c; lv.first = 0; rebuild(); draw(); }
      continue;
    }
    if (lv.handle(t) == -2) draw();
    if (t.released && abs(t.y - t.sy) < 15 && t.sy > lv.y0 && t.sx < lv.width) {
      int vi = lv.first + (t.sy - lv.y0) / lv.rowH;
      if (vi < (int)view.size()) {
        Entity& e = ents[view[vi]];
        bool quick = t.sx > lv.width - 90 && (toggleable(e.domain) || e.domain == "scene" || e.domain == "script");
        if (quick) haToggle(e);
        else entityPage(e);
        draw();
      }
    }
    delay(15);
  }
}

// ===================================================================== Nest
struct NestDev {
  String name;     // enterprises/.../devices/...
  String type;     // THERMOSTAT, DOORBELL, CAMERA, DISPLAY
  String label;
  String room;
  float ambient = NAN, humidity = NAN, heat = NAN, cool = NAN;
  String mode, hvac, connectivity;
  bool fahrenheit = false;
};
std::vector<NestDev> nest;
String nestToken;
uint32_t nestTokenExp = 0;

bool nestAuth() {
  if (nestToken.length() && millis() < nestTokenExp) return true;
  String body = "client_id=" + cfg.nestClientId + "&client_secret=" + cfg.nestClientSecret +
                "&refresh_token=" + cfg.nestRefreshToken + "&grant_type=refresh_token";
  String resp;
  int code = net::request("POST", "https://www.googleapis.com/oauth2/v4/token", body, resp, "",
                          "application/x-www-form-urlencoded");
  if (code != 200) { Serial.printf("Nest token %d: %s\n", code, resp.c_str()); return false; }
  JsonDocument doc;
  if (deserializeJson(doc, resp)) return false;
  nestToken = doc["access_token"].as<String>();
  nestTokenExp = millis() + ((doc["expires_in"] | 3600) - 120) * 1000UL;
  return nestToken.length() > 0;
}

void parseNest(JsonObjectConst d, NestDev& n) {
  n.name = d["name"].as<String>();
  String type = d["type"].as<String>();
  n.type = type.substring(type.lastIndexOf('.') + 1);
  JsonObjectConst tr = d["traits"];
  n.label = tr["sdm.devices.traits.Info"]["customName"] | "";
  JsonArrayConst pr = d["parentRelations"];
  if (pr.size()) n.room = pr[0]["displayName"] | "";
  if (n.label.isEmpty()) n.label = n.room.length() ? n.room + " " + n.type : n.type;
  n.ambient = tr["sdm.devices.traits.Temperature"]["ambientTemperatureCelsius"] | NAN;
  n.humidity = tr["sdm.devices.traits.Humidity"]["ambientHumidityPercent"] | NAN;
  n.heat = tr["sdm.devices.traits.ThermostatTemperatureSetpoint"]["heatCelsius"] | NAN;
  n.cool = tr["sdm.devices.traits.ThermostatTemperatureSetpoint"]["coolCelsius"] | NAN;
  n.mode = tr["sdm.devices.traits.ThermostatMode"]["mode"] | "";
  n.hvac = tr["sdm.devices.traits.ThermostatHvac"]["status"] | "";
  n.connectivity = tr["sdm.devices.traits.Connectivity"]["status"] | "";
  n.fahrenheit = String(tr["sdm.devices.traits.Settings"]["temperatureScale"] | "") == "FAHRENHEIT";
}

bool nestLoad() {
  if (!nestAuth()) return false;
  String resp;
  int code = net::request("GET", "https://smartdevicemanagement.googleapis.com/v1/enterprises/" + cfg.nestProject +
                                     "/devices", "", resp, nestToken, "application/json", 48000);
  if (code != 200) { Serial.printf("Nest list %d: %s\n", code, resp.c_str()); return false; }
  JsonDocument doc;
  if (deserializeJson(doc, resp)) return false;
  nest.clear();
  for (JsonObjectConst d : doc["devices"].as<JsonArrayConst>()) {
    NestDev n;
    parseNest(d, n);
    nest.push_back(n);
  }
  return true;
}

bool nestCmd(NestDev& n, const char* command, JsonDocument& params) {
  if (!nestAuth()) return false;
  JsonDocument body;
  body["command"] = command;
  body["params"] = params;
  String b, resp;
  serializeJson(body, b);
  int code = net::request("POST", "https://smartdevicemanagement.googleapis.com/v1/" + n.name + ":executeCommand",
                          b, resp, nestToken);
  if (code != 200) {
    JsonDocument err;
    String msg = "Nest error " + String(code);
    if (!deserializeJson(err, resp) && err["error"]["message"].is<const char*>()) msg = err["error"]["message"].as<String>();
    toast(msg, BAD, 2000);
    return false;
  }
  // re-read device
  delay(400);
  code = net::request("GET", "https://smartdevicemanagement.googleapis.com/v1/" + n.name, "", resp, nestToken);
  if (code == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, resp)) parseNest(doc.as<JsonObjectConst>(), n);
  }
  return true;
}

String fmtT(const NestDev& n, float c) {
  if (isnan(c)) return "-";
  return n.fahrenheit ? String(c * 9 / 5 + 32, 0) + "F" : String(c, 1) + "C";
}

void thermostatPage(NestDev& n) {
  auto draw = [&]() {
    clear();
    header(n.label.c_str(), true, "Refresh");
    text(SCR_W / 2, 90, fmtT(n, n.ambient), TEXT, &fonts::DejaVu56, middle_center);
    String sub = "Humidity " + (isnan(n.humidity) ? String("-") : String((int)n.humidity) + "%") + "   HVAC " + n.hvac;
    text(SCR_W / 2, 132, sub, DIM, &fonts::DejaVu12, middle_center);
    bool heat = n.mode == "HEAT" || n.mode == "HEATCOOL", cool = n.mode == "COOL" || n.mode == "HEATCOOL";
    if (heat) {
      text(110, 160, "Heat to " + fmtT(n, n.heat), 0xFC00, &fonts::DejaVu18, middle_center);
      drawBtn(Btn(30, 178, 70, 54, "-", 0x8000), false, &fonts::DejaVu40);
      drawBtn(Btn(120, 178, 70, 54, "+", 0x8000), false, &fonts::DejaVu40);
    }
    if (cool) {
      text(370, 160, "Cool to " + fmtT(n, n.cool), 0x05FF, &fonts::DejaVu18, middle_center);
      drawBtn(Btn(290, 178, 70, 54, "-", 0x0010), false, &fonts::DejaVu40);
      drawBtn(Btn(380, 178, 70, 54, "+", 0x0010), false, &fonts::DejaVu40);
    }
    const char* modes[] = {"OFF", "HEAT", "COOL", "HEATCOOL"};
    for (int i = 0; i < 4; i++) drawBtn(Btn(10 + i * 117, 256, 109, 50, modes[i]), n.mode == modes[i], &fonts::DejaVu12);
  };
  draw();
  waitRelease();
  while (true) {
    Touch t = poll();
    if (backHit(t)) return;
    if (rightHit(t)) { nestLoad(); for (auto& d : nest) if (d.name == n.name) n = d; draw(); waitRelease(); continue; }
    if (!t.pressed) { delay(15); continue; }
    float step = n.fahrenheit ? 5.0f / 9.0f : 0.5f;
    JsonDocument p;
    bool heat = n.mode == "HEAT" || n.mode == "HEATCOOL", cool = n.mode == "COOL" || n.mode == "HEATCOOL";
    bool sent = false;
    if (t.y >= 178 && t.y < 232) {
      float h = n.heat, c = n.cool;
      if (heat && t.x < 200) h += t.x < 110 ? -step : step;
      else if (cool && t.x > 280) c += t.x < 370 ? -step : step;
      else { delay(15); continue; }
      busy("Sending...");
      if (n.mode == "HEATCOOL") { p["heatCelsius"] = h; p["coolCelsius"] = c; sent = nestCmd(n, "sdm.devices.commands.ThermostatTemperatureSetpoint.SetRange", p); }
      else if (heat) { p["heatCelsius"] = h; sent = nestCmd(n, "sdm.devices.commands.ThermostatTemperatureSetpoint.SetHeat", p); }
      else { p["coolCelsius"] = c; sent = nestCmd(n, "sdm.devices.commands.ThermostatTemperatureSetpoint.SetCool", p); }
    } else if (t.y >= 256) {
      const char* modes[] = {"OFF", "HEAT", "COOL", "HEATCOOL"};
      int i = constrain((t.x - 10) / 117, 0, 3);
      p["mode"] = modes[i];
      busy("Sending...");
      sent = nestCmd(n, "sdm.devices.commands.ThermostatMode.SetMode", p);
    }
    (void)sent;
    draw();
    waitRelease();
  }
}

void nestScreen() {
  if (cfg.nestProject.isEmpty() || cfg.nestRefreshToken.isEmpty()) {
    clear();
    header("Google Nest", true);
    int y = 56;
    for (auto l : {"Not configured. Google Nest needs a Device Access project", "(one-time US$5 fee) + OAuth client. Add to /config.json:", "",
                   "  \"nest_project_id\", \"nest_client_id\",", "  \"nest_client_secret\", \"nest_refresh_token\"", "",
                   "See README for the step-by-step. Alternatively link Nest", "into Home Assistant and use the HA screen (supports",
                   "doorbell snapshots)."}) {
      text(16, y, l, TEXT, &fonts::DejaVu12);
      y += 22;
    }
    waitRelease();
    while (!backHit(poll())) delay(20);
    return;
  }
  clear();
  header("Google Nest", true);
  busy("Contacting Google...");
  if (!nestLoad()) { toast(net::connected() ? "Nest API error (see serial)" : "Wi-Fi not connected", BAD, 1800); return; }
  ListView lv;
  lv.rowH = 50;
  auto draw = [&]() {
    clear();
    header(("Google Nest (" + String(nest.size()) + ")").c_str(), true, "Refresh");
    lv.count = nest.size();
    lv.draw([&](int i, int x, int y, int w, int h) {
      NestDev& n = nest[i];
      text(x + 8, y + 4, ellipsize(n.label, w - 140, &fonts::DejaVu18), TEXT, &fonts::DejaVu18);
      String sub = n.type;
      if (n.type == "THERMOSTAT") sub += "  " + fmtT(n, n.ambient) + "  " + n.mode + " " + n.hvac;
      if (n.connectivity.length()) sub += "  " + n.connectivity;
      text(x + 8, y + h - 4, ellipsize(sub, w - 20, &fonts::DejaVu12), DIM, &fonts::DejaVu12, bottom_left);
    });
  };
  draw();
  waitRelease();
  while (true) {
    Touch t = poll();
    if (backHit(t)) return;
    if (rightHit(t)) { busy("Refreshing..."); nestLoad(); draw(); waitRelease(); continue; }
    if (lv.handle(t) == -2) draw();
    if (t.released && abs(t.y - t.sy) < 15 && t.sy > lv.y0 && t.sx < lv.width) {
      int i = lv.first + (t.sy - lv.y0) / lv.rowH;
      if (i < (int)nest.size()) {
        if (nest[i].type == "THERMOSTAT") thermostatPage(nest[i]);
        else toast(nest[i].type + ": live view/snapshots need HA or Pub/Sub", WARN, 2200);
        draw();
      }
    }
    delay(15);
  }
}

}  // namespace

void appHome() {
  Btn ha(20, 70, 210, 200, "Home Assistant", 0x0249);
  Btn ne(250, 70, 210, 200, "Google Nest", 0x5800);
  auto draw = [&]() {
    clear();
    header("Smart Home", true);
    drawBtn(ha);
    drawBtn(ne);
    text(125, 205, cfg.haUrl.length() ? "configured" : "not set up", cfg.haUrl.length() ? GOOD : DIM, &fonts::DejaVu12, middle_center);
    text(355, 205, cfg.nestProject.length() ? "configured" : "not set up", cfg.nestProject.length() ? GOOD : DIM, &fonts::DejaVu12, middle_center);
    text(SCR_W / 2, 296, net::connected() ? "Wi-Fi: " + WiFi.SSID() : String("Wi-Fi not connected - use the Wi-Fi app"),
         net::connected() ? DIM : WARN, &fonts::DejaVu12, middle_center);
  };
  draw();
  waitRelease();
  while (true) {
    Touch t = poll();
    if (backHit(t)) break;
    if (t.pressed) {
      if (ha.hit(t.x, t.y)) { haScreen(); draw(); waitRelease(); }
      else if (ne.hit(t.x, t.y)) { nestScreen(); draw(); waitRelease(); }
    }
    delay(15);
  }
  ents.clear();
  ents.shrink_to_fit();
  nest.clear();
}
