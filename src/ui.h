#pragma once
#include <Arduino.h>
#include <functional>
#include "display.h"

namespace ui {

// Theme (RGB565)
constexpr uint16_t BG      = 0x0861;  // near-black blue
constexpr uint16_t PANEL   = 0x18E5;
constexpr uint16_t PANEL2  = 0x2969;
constexpr uint16_t TEXT    = 0xFFFF;
constexpr uint16_t DIM     = 0x9CF3;
constexpr uint16_t ACCENT  = 0x05FF;  // cyan
constexpr uint16_t ACCENT2 = 0xFD20;  // orange
constexpr uint16_t GOOD    = 0x07E8;
constexpr uint16_t WARN    = 0xFFE0;
constexpr uint16_t BAD     = 0xF8A2;
constexpr uint16_t HEADER  = 0x10A4;

constexpr int HEADER_H = 40;

struct Touch {
  bool down = false;      // finger currently on screen
  bool pressed = false;   // went down this poll
  bool released = false;  // lifted this poll
  bool back = false;      // BOOT button pressed (acts as "back")
  int16_t x = 0, y = 0;   // current / last position
  int16_t sx = 0, sy = 0; // where the press started
};

struct Btn {
  int16_t x, y, w, h;
  String label;
  uint16_t color = PANEL2;
  Btn() : x(0), y(0), w(0), h(0) {}
  Btn(int x_, int y_, int w_, int h_, const String& l, uint16_t c = PANEL2)
      : x(x_), y(y_), w(w_), h(h_), label(l), color(c) {}
  bool hit(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

void begin();
Touch poll();
void waitRelease();

void clear(uint16_t c = BG);
void header(const char* title, bool back = true, const char* right = nullptr);
bool backHit(const Touch& t);          // back arrow tapped or BOOT pressed
bool rightHit(const Touch& t);         // header right-hand action tapped
void drawBtn(const Btn& b, bool active = false, const lgfx::IFont* font = nullptr);
void text(int x, int y, const String& s, uint16_t color = TEXT, const lgfx::IFont* font = nullptr,
          textdatum_t datum = top_left);
String ellipsize(const String& s, int maxW, const lgfx::IFont* font);
void toast(const String& msg, uint16_t color = ACCENT, int ms = 1200);
void busy(const String& msg);
void signalBars(int x, int y, int rssi);
uint16_t rssiColor(int rssi);

// Modal on-screen keyboard. Returns false if cancelled.
bool textInput(const char* title, String& value, bool masked = false);
// Modal yes/no
bool confirm(const String& question);

// Simple paged list with scroll buttons on the right edge
struct ListView {
  int y0 = HEADER_H + 2;
  int rowH = 38;
  int count = 0;
  int first = 0;
  int width = SCR_W - 52;
  int visible() const { return (SCR_H - y0) / rowH; }
  void draw(const std::function<void(int idx, int x, int y, int w, int h)>& row);
  // Returns row index tapped, -2 if the list scrolled (redraw), -1 otherwise
  int handle(const Touch& t);
};

}  // namespace ui
