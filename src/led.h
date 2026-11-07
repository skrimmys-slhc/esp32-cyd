#pragma once
#include <Arduino.h>
#include "pins.h"

namespace led {
inline void pinOut(int p, bool on) { digitalWrite(p, (on ^ LED_ACTIVE_LOW) ? HIGH : LOW); }
inline void begin() {
  pinMode(PIN_LED_R, OUTPUT); pinMode(PIN_LED_G, OUTPUT); pinMode(PIN_LED_B, OUTPUT);
  pinOut(PIN_LED_R, false); pinOut(PIN_LED_G, false); pinOut(PIN_LED_B, false);
}
inline void set(bool r, bool g, bool b) { pinOut(PIN_LED_R, r); pinOut(PIN_LED_G, g); pinOut(PIN_LED_B, b); }
inline void off() { set(false, false, false); }
}  // namespace led
