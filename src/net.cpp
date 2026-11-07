#include "net.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "settings.h"

namespace net {

static bool s_timeStarted = false;

static void startTime() {
  if (s_timeStarted) return;
  configTzTime(cfg.tz.c_str(), "pool.ntp.org", "time.google.com");
  s_timeStarted = true;
}

void begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname("cyd-multitool");
  if (cfg.wifiSsid.length()) WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) { startTime(); }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
}

bool connected() { return WiFi.status() == WL_CONNECTED; }

bool connect(const String& ssid, const String& pass, uint32_t timeoutMs) {
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      cfg.wifiSsid = ssid;
      cfg.wifiPass = pass;
      settings::save();
      return true;
    }
    delay(100);
  }
  return false;
}

bool ensure(uint32_t timeoutMs) {
  if (connected()) return true;
  if (cfg.wifiSsid.isEmpty()) return false;
  if (WiFi.getMode() != WIFI_STA) WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (connected()) return true;
    delay(100);
  }
  return false;
}

void suspend() {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  delay(50);
}

void resume() {
  WiFi.setAutoReconnect(true);
  if (cfg.wifiSsid.length()) WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
}

String timeStr() {
  struct tm tm;
  if (!getLocalTime(&tm, 0) || tm.tm_year < 120) return "";
  char b[8];
  strftime(b, sizeof(b), "%H:%M", &tm);
  return b;
}

int request(const char* method, const String& url, const String& body, String& resp,
            const String& bearer, const char* contentType, size_t maxLen) {
  resp = "";
  if (!ensure()) return -100;
  HTTPClient http;
  WiFiClientSecure tls;
  WiFiClient plain;
  bool https = url.startsWith("https://");
  if (https) {
    tls.setInsecure();
    if (!http.begin(tls, url)) return -101;
  } else {
    if (!http.begin(plain, url)) return -101;
  }
  http.setTimeout(10000);
  http.useHTTP10(true);  // no chunked encoding -> simple stream reading
  if (bearer.length()) http.addHeader("Authorization", "Bearer " + bearer);
  if (body.length() || strcmp(method, "POST") == 0) http.addHeader("Content-Type", contentType);
  int code = http.sendRequest(method, body);
  if (code > 0) {
    WiFiClient* s = http.getStreamPtr();
    uint32_t t0 = millis();
    resp.reserve(min((size_t)2048, maxLen));
    while ((http.connected() || s->available()) && millis() - t0 < 10000) {
      while (s->available()) {
        int c = s->read();
        if (c < 0) break;
        if (resp.length() < maxLen) resp += (char)c;
      }
      if (!http.connected() && !s->available()) break;
      delay(1);
    }
  }
  http.end();
  return code;
}

}  // namespace net
