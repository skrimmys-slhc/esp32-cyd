#include "ui.h"
#include "pins.h"

namespace ui {

static bool s_wasDown = false;
static bool s_btnWas = false;
static Touch s_last;

#define F_SMALL (&fonts::DejaVu12)
#define F_MED   (&fonts::DejaVu18)
#define F_BIG   (&fonts::DejaVu24)

void begin() { pinMode(PIN_BOOT_BTN, INPUT_PULLUP); }

Touch poll() {
  Touch t;
  lgfx::touch_point_t tp;
  int n = tft.getTouch(&tp, 1);
  t.down = n > 0;
  if (t.down) {
    t.x = tp.x; t.y = tp.y;
  } else {
    t.x = s_last.x; t.y = s_last.y;
  }
  t.pressed = t.down && !s_wasDown;
  t.released = !t.down && s_wasDown;
  if (t.pressed) { t.sx = t.x; t.sy = t.y; } else { t.sx = s_last.sx; t.sy = s_last.sy; }
  s_wasDown = t.down;

  bool btn = digitalRead(PIN_BOOT_BTN) == LOW;
  t.back = btn && !s_btnWas;
  s_btnWas = btn;
  s_last = t;
  return t;
}

void waitRelease() {
  while (true) {
    Touch t = poll();
    if (!t.down) break;
    delay(10);
  }
}

void clear(uint16_t c) { tft.fillScreen(c); }

void text(int x, int y, const String& s, uint16_t color, const lgfx::IFont* font, textdatum_t datum) {
  tft.setFont(font ? font : F_MED);
  tft.setTextColor(color);
  tft.setTextDatum(datum);
  tft.drawString(s, x, y);
}

String ellipsize(const String& s, int maxW, const lgfx::IFont* font) {
  tft.setFont(font ? font : F_MED);
  if (tft.textWidth(s) <= maxW) return s;
  String r = s;
  while (r.length() > 1 && tft.textWidth(r + "..") > maxW) r.remove(r.length() - 1);
  return r + "..";
}

void header(const char* title, bool back, const char* right) {
  tft.fillRect(0, 0, SCR_W, HEADER_H, HEADER);
  tft.drawFastHLine(0, HEADER_H - 1, SCR_W, ACCENT);
  int tx = 12;
  if (back) {
    // back arrow
    tft.fillTriangle(14, 20, 28, 9, 28, 31, ACCENT);
    tft.fillRect(26, 16, 18, 8, ACCENT);
    tx = 58;
  }
  text(tx, 20, title, TEXT, F_MED, middle_left);
  if (right) {
    tft.setFont(F_MED);
    int w = tft.textWidth(right) + 20;
    tft.fillRoundRect(SCR_W - w - 6, 5, w, 30, 6, PANEL2);
    text(SCR_W - 6 - w / 2, 20, right, TEXT, F_MED, middle_center);
  }
}

bool backHit(const Touch& t) { return t.back || (t.pressed && t.x < 70 && t.y < HEADER_H); }
bool rightHit(const Touch& t) { return t.pressed && t.x > SCR_W - 140 && t.y < HEADER_H; }

void drawBtn(const Btn& b, bool active, const lgfx::IFont* font) {
  uint16_t c = active ? ACCENT : b.color;
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, c);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 8, active ? TEXT : PANEL);
  text(b.x + b.w / 2, b.y + b.h / 2 + 1, b.label, active ? BG : TEXT, font ? font : F_MED, middle_center);
}

void toast(const String& msg, uint16_t color, int ms) {
  tft.setFont(F_MED);
  int w = min(SCR_W - 20, (int)tft.textWidth(msg) + 40);
  int x = (SCR_W - w) / 2, y = SCR_H / 2 - 30;
  tft.fillRoundRect(x, y, w, 60, 10, PANEL);
  tft.drawRoundRect(x, y, w, 60, 10, color);
  text(SCR_W / 2, y + 31, ellipsize(msg, w - 20, F_MED), color, F_MED, middle_center);
  if (ms > 0) delay(ms);
}

void busy(const String& msg) { toast(msg, ACCENT, 0); }

uint16_t rssiColor(int rssi) { return rssi > -60 ? GOOD : rssi > -75 ? WARN : BAD; }

void signalBars(int x, int y, int rssi) {
  int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : rssi > -85 ? 1 : 0;
  uint16_t c = rssiColor(rssi);
  for (int i = 0; i < 4; i++) {
    int h = 4 + i * 4;
    tft.fillRect(x + i * 6, y + 16 - h, 4, h, i < bars ? c : PANEL2);
  }
}

// ---------------------------------------------------------------- list view
void ListView::draw(const std::function<void(int, int, int, int, int)>& row) {
  int vis = visible();
  if (first > max(0, count - vis)) first = max(0, count - vis);
  if (first < 0) first = 0;
  tft.fillRect(0, y0, SCR_W, SCR_H - y0, BG);
  for (int i = 0; i < vis && first + i < count; i++) {
    int y = y0 + i * rowH;
    tft.fillRoundRect(4, y + 2, width - 4, rowH - 4, 6, (i & 1) ? PANEL : PANEL2);
    row(first + i, 4, y + 2, width - 4, rowH - 4);
  }
  // scroll rail
  int rx = SCR_W - 46, rh = SCR_H - y0;
  tft.fillRoundRect(rx, y0 + 2, 42, rh / 2 - 4, 6, PANEL2);
  tft.fillRoundRect(rx, y0 + rh / 2 + 2, 42, rh / 2 - 4, 6, PANEL2);
  uint16_t cu = first > 0 ? TEXT : PANEL;
  uint16_t cd = first + vis < count ? TEXT : PANEL;
  int cx = rx + 21, uy = y0 + rh / 4, dy = y0 + 3 * rh / 4;
  tft.fillTriangle(cx, uy - 10, cx - 12, uy + 8, cx + 12, uy + 8, cu);
  tft.fillTriangle(cx, dy + 10, cx - 12, dy - 8, cx + 12, dy - 8, cd);
  if (count == 0) text(width / 2, y0 + 60, "(empty)", DIM, F_MED, middle_center);
  else {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d/%d", min(first + vis, count), count);
    text(cx, y0 + rh / 2, buf, DIM, F_SMALL, middle_center);
  }
}

int ListView::handle(const Touch& t) {
  int vis = visible();
  // swipe to scroll
  if (t.released && abs(t.y - t.sy) > 40 && t.sx < width) {
    int rows = (t.sy - t.y) / rowH;
    if (rows == 0) rows = t.sy > t.y ? 1 : -1;
    int nf = constrain(first + rows, 0, max(0, count - vis));
    if (nf != first) { first = nf; return -2; }
    return -1;
  }
  if (!t.pressed || t.y < y0) return -1;
  if (t.x >= SCR_W - 50) {
    int rh = SCR_H - y0;
    int nf = first;
    if (t.y < y0 + rh / 2) nf = max(0, first - vis);
    else nf = min(max(0, count - vis), first + vis);
    if (nf != first) { first = nf; return -2; }
    return -1;
  }
  return -1;
}

// ---------------------------------------------------------------- keyboard
static const char* KB_ROWS[3][4] = {
    {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"},
    {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"},
    {"!@#$%^&*()", "-_=+[]{}\\|", ";:'\",.<>/?", "`~"},
};
static constexpr int KW = 46, KH = 40, KY0 = 100, KGAP = 4;

static void kbDrawField(const String& v, bool masked, bool reveal) {
  tft.fillRoundRect(8, 48, SCR_W - 90, 44, 8, PANEL2);
  String shown = (masked && !reveal) ? String() : v;
  if (masked && !reveal) for (size_t i = 0; i < v.length(); i++) shown += '*';
  tft.setFont(F_MED);
  while (shown.length() && tft.textWidth(shown) > SCR_W - 120) shown.remove(0, 1);
  text(18, 70, shown + "_", TEXT, F_MED, middle_left);
  if (masked) {
    Btn eye{SCR_W - 76, 48, 68, 44, reveal ? "hide" : "show"};
    drawBtn(eye, false, F_SMALL);
  }
}

static void kbKeyRect(int row, int col, int n, int& x, int& y) {
  int rowW = n * (KW + KGAP) - KGAP;
  x = (SCR_W - rowW) / 2 + col * (KW + KGAP);
  y = KY0 + row * (KH + KGAP);
}

static void kbDraw(int page) {
  tft.fillRect(0, KY0 - 4, SCR_W, SCR_H - KY0 + 4, BG);
  for (int r = 0; r < 4; r++) {
    const char* s = KB_ROWS[page][r];
    int n = strlen(s);
    for (int c = 0; c < n; c++) {
      int x, y;
      kbKeyRect(r, c, n, x, y);
      tft.fillRoundRect(x, y, KW, KH, 6, PANEL2);
      char k[2] = {s[c], 0};
      text(x + KW / 2, y + KH / 2 + 1, k, TEXT, F_MED, middle_center);
    }
  }
  int y = KY0 + 4 * (KH + KGAP);
  drawBtn({8, y, 80, KH, page == 1 ? "abc" : "ABC"}, page == 1, F_SMALL);
  drawBtn({92, y, 70, KH, page == 2 ? "abc" : "#+="}, page == 2, F_SMALL);
  drawBtn({166, y, 160, KH, "space"}, false, F_SMALL);
  drawBtn({330, y, 66, KH, "del"}, false, F_SMALL);
  drawBtn({400, y, 72, KH, "OK", GOOD}, false, F_SMALL);
}

bool textInput(const char* title, String& value, bool masked) {
  String v = value;
  int page = 0;
  bool reveal = false;
  clear();
  header(title, true);
  kbDrawField(v, masked, reveal);
  kbDraw(page);
  waitRelease();
  while (true) {
    Touch t = poll();
    if (backHit(t)) return false;
    if (!t.pressed) { delay(10); continue; }
    bool changed = false;
    if (masked && t.x > SCR_W - 80 && t.y > 44 && t.y < 96) { reveal = !reveal; changed = true; }
    for (int r = 0; r < 4 && !changed; r++) {
      const char* s = KB_ROWS[page][r];
      int n = strlen(s);
      for (int c = 0; c < n; c++) {
        int x, y;
        kbKeyRect(r, c, n, x, y);
        if (t.x >= x - 2 && t.x < x + KW + 2 && t.y >= y - 2 && t.y < y + KH + 2) {
          v += s[c];
          tft.fillRoundRect(x, y, KW, KH, 6, ACCENT);
          delay(40);
          tft.fillRoundRect(x, y, KW, KH, 6, PANEL2);
          char k[2] = {s[c], 0};
          text(x + KW / 2, y + KH / 2 + 1, k, TEXT, F_MED, middle_center);
          if (page == 1) { page = 0; kbDraw(page); }
          changed = true;
          break;
        }
      }
    }
    int y = KY0 + 4 * (KH + KGAP);
    if (!changed && t.y >= y && t.y < y + KH) {
      if (t.x < 88) { page = page == 1 ? 0 : 1; kbDraw(page); }
      else if (t.x < 162) { page = page == 2 ? 0 : 2; kbDraw(page); }
      else if (t.x < 326) v += ' ';
      else if (t.x < 396) { if (v.length()) v.remove(v.length() - 1); }
      else { value = v; return true; }
      changed = true;
    }
    if (changed) kbDrawField(v, masked, reveal);
    delay(10);
  }
}

bool confirm(const String& q) {
  tft.fillRoundRect(40, 80, SCR_W - 80, 160, 12, PANEL);
  tft.drawRoundRect(40, 80, SCR_W - 80, 160, 12, ACCENT);
  text(SCR_W / 2, 125, ellipsize(q, SCR_W - 110, F_MED), TEXT, F_MED, middle_center);
  Btn no{70, 170, 150, 50, "Cancel"}, yes{SCR_W - 220, 170, 150, 50, "OK", GOOD};
  drawBtn(no); drawBtn(yes);
  waitRelease();
  while (true) {
    Touch t = poll();
    if (t.back) return false;
    if (t.pressed) {
      if (yes.hit(t.x, t.y)) { waitRelease(); return true; }
      if (no.hit(t.x, t.y)) { waitRelease(); return false; }
    }
    delay(10);
  }
}

}  // namespace ui
