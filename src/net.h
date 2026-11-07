#pragma once
#include <Arduino.h>

namespace net {
void begin();                       // start STA + auto-connect with saved creds
bool connect(const String& ssid, const String& pass, uint32_t timeoutMs = 15000);
bool connected();
bool ensure(uint32_t timeoutMs = 8000);  // reconnect if needed (blocking)
void suspend();                     // drop the AP (used by the Flock scanner)
void resume();                      // reconnect after suspend()
String timeStr();                   // "HH:MM" or "" if not synced

// Minimal HTTP(S) helpers. HTTPS uses no certificate pinning (see README).
// Return HTTP status (negative on transport error); body is truncated to maxLen.
int request(const char* method, const String& url, const String& body, String& resp,
            const String& authBearer = "", const char* contentType = "application/json",
            size_t maxLen = 16384);
}
