/*
    touch_input.cpp — nyanBOX CYD port

    XPT2046 resistive touch, read over SOFTWARE (bit-bang) SPI so it does not
    contend for either ESP32 hardware SPI host (HSPI=TFT_eSPI, VSPI=nRF24 radios).

    Touch is detected by Z-PRESSURE, NOT PENIRQ (IRQ is unreliable on the CYD).
    Slowed bit-bang + sample-and-hold settle per hexeguitar's CYD28_Touchscreen.

    SELF-CALIBRATING: on first boot (or when a finger is held at power-up) it runs
    a 4-corner tap calibration, auto-detects axis swap + direction + range, and
    stores it to EEPROM — so orientation is never hand-tuned again.

    Emulates the five nyanBOX control buttons (active-LOW) as touch zones in a
    bottom control strip + draws the on-screen arrow D-pad. nyanDigitalRead() is
    the shim the pindefs.h macro routes every project-side digitalRead() through.

    IMPORTANT: this file MUST NOT include pindefs.h — that would pull in the
    `#define digitalRead nyanDigitalRead` macro and turn the bit-bang MISO
    sampling below into infinite recursion. Here digitalRead/Write are real.
*/
#include <Arduino.h>
#include "touch_input.h"
#include <TFT_eSPI.h>
#include <EEPROM.h>
extern TFT_eSPI tft;   // defined in cyd_u8g2_bridge.cpp

// --- Bit-bang XPT2046 bus pins (CYD dedicated touch header) ---
static const uint8_t XPT_CLK  = 25;
static const uint8_t XPT_MOSI = 32;
static const uint8_t XPT_MISO = 39;   // input-only
static const uint8_t XPT_CS   = 33;
static const uint8_t XPT_IRQ  = 36;   // input-only (unused as a gate)

// --- XPT2046 12-bit differential command bytes (SER/DFR=0, PD=00) ---
static const uint8_t CMD_X  = 0xD0;
static const uint8_t CMD_Y  = 0x90;
static const uint8_t CMD_Z1 = 0xB0;
static const uint8_t CMD_Z2 = 0xC0;
static const int     Z_THRESHOLD = 400;

// --- nyanBOX logical button pin numbers (mirror of pindefs.h, NOT included) ---
static const uint8_t B_UP     = 26;
static const uint8_t B_DOWN   = 33;
static const uint8_t B_CENTER = 32;   // Exit
static const uint8_t B_LEFT   = 25;   // Back
static const uint8_t B_RIGHT  = 27;   // Select

static const int SCREEN_W = 240;
static const int SCREEN_H = 320;
static const int STRIP_Y  = 258;      // touch bar zone = bottom (UI band is y 100..219)

// --- Persistent calibration (measured by the 4-corner routine) ---
struct TouchCal {
  uint16_t magic;
  uint8_t  swap;              // 1 = screen X is driven by the raw Y channel
  uint16_t xr0, xr1;          // raw at screenX=0 and screenX=SCREEN_W (may be inverted)
  uint16_t yr0, yr1;          // raw at screenY=0 and screenY=SCREEN_H
};
#define CAL_ADDR  400          // clear of nyanBOX's low-address EEPROM settings
#define CAL_MAGIC 0xCA11
// Sane fallback (from the ESPHome config) if EEPROM is empty and cal is skipped.
static TouchCal g_cal = { 0, 0, 3756, 220, 394, 3749 };

// Cached poll state
static uint8_t  g_pressedButton = 0xFF;
static uint32_t g_lastPollMs    = 0;

// --- Touch mode + menu-band gesture state ---
static uint8_t g_touchMode = TOUCH_MENU;
static const int      Z_DOWN = Z_THRESHOLD, Z_UP = 200;
static const int      DRAG_SLOP = 12, SLIDER_W = 34;   // left strip (screen px) = scroll slider (never selects)
static const uint32_t SAMPLE_MS = 15, SETTLE_MS = 30, TAP_MAX_MS = 600, LOCKOUT_MS = 100;
static bool     g_gDown = false, g_gSettled = false, g_gMoved = false, g_inSlider = false;
static bool     g_sliderActive = false;
static int      g_gAnchorY = 0, g_menuTapY = -1, g_sliderY = 0;
static uint32_t g_gDownMs = 0, g_gLockout = 0;

// Bit-bang clock: 1us per half-cycle keeps DCLK ~500kHz (under the 2MHz limit).
static inline void clkPulseHigh() { digitalWrite(XPT_CLK, HIGH); delayMicroseconds(1); }
static inline void clkPulseLow()  { digitalWrite(XPT_CLK, LOW);  delayMicroseconds(1); }

static uint16_t xptRead(uint8_t cmd) {
  digitalWrite(XPT_CLK, LOW);
  digitalWrite(XPT_CS, LOW);
  delayMicroseconds(1);
  for (int i = 7; i >= 0; i--) {
    digitalWrite(XPT_MOSI, (cmd >> i) & 0x01);
    clkPulseHigh();
    clkPulseLow();
  }
  delayMicroseconds(3);   // sample-and-hold settle
  clkPulseHigh();         // busy/dummy clock
  clkPulseLow();
  uint16_t v = 0;
  for (int i = 11; i >= 0; i--) {
    clkPulseHigh();
    v |= (uint16_t)(digitalRead(XPT_MISO) ? 1 : 0) << i;
    clkPulseLow();
  }
  delayMicroseconds(1);
  digitalWrite(XPT_CS, HIGH);
  return v & 0x0FFF;
}

static uint16_t xptSample(uint8_t cmd) {
  uint16_t a = xptRead(cmd), b = xptRead(cmd), c = xptRead(cmd);
  if (a > b) { uint16_t t = a; a = b; b = t; }
  if (b > c) { uint16_t t = b; b = c; c = t; }
  if (a > b) { uint16_t t = a; a = b; b = t; }
  return b;
}

static int xptPressure() {
  uint16_t z1 = xptSample(CMD_Z1);
  uint16_t z2 = xptSample(CMD_Z2);
  return (int)z1 + 4095 - (int)z2;
}

// Reclaim the bus pins as OUTPUT (menus flip them to INPUT_PULLUP). Self-healing.
static void touchBusClaim() {
  pinMode(XPT_CLK,  OUTPUT);
  pinMode(XPT_MOSI, OUTPUT);
  pinMode(XPT_CS,   OUTPUT);
  pinMode(XPT_MISO, INPUT);
  pinMode(XPT_IRQ,  INPUT);
  digitalWrite(XPT_CS,  HIGH);
  digitalWrite(XPT_CLK, LOW);
}

// Apply the stored calibration: raw -> screen (handles swap + direction + scale).
static void xptToScreen(uint16_t rawX, uint16_t rawY, long &sx, long &sy) {
  int rfx = g_cal.swap ? (int)rawY : (int)rawX;
  int rfy = g_cal.swap ? (int)rawX : (int)rawY;
  int dx = (int)g_cal.xr1 - (int)g_cal.xr0;
  int dy = (int)g_cal.yr1 - (int)g_cal.yr0;
  if (dx == 0) dx = 1;
  if (dy == 0) dy = 1;
  sx = (long)(rfx - (int)g_cal.xr0) * SCREEN_W / dx;
  sy = (long)(rfy - (int)g_cal.yr0) * SCREEN_H / dy;
  if (sx < 0) sx = 0; if (sx >= SCREEN_W) sx = SCREEN_W - 1;
  if (sy < 0) sy = 0; if (sy >= SCREEN_H) sy = SCREEN_H - 1;
}

static uint8_t buttonForColumn(int sx) {
  int col = sx / (SCREEN_W / 5);          // 48 px per column
  if (col < 0) col = 0;
  if (col > 4) col = 4;
  switch (col) {                          // BACK | UP | DN | SEL | EXIT
    case 0: return B_LEFT;
    case 1: return B_UP;
    case 2: return B_DOWN;
    case 3: return B_RIGHT;
    default: return B_CENTER;
  }
}

// The on-screen arrow D-pad in the bottom strip (persists; UI band is y100..219).
void drawDpadBar() {
  const uint16_t fill   = tft.color565(16, 22, 18);
  const uint16_t border = tft.color565(46, 150, 74);
  const uint16_t glyph  = tft.color565(74, 214, 112);
  const int top = 264, h = 42, cy = top + h / 2, a = 7;
  tft.fillRect(0, 258, 240, 62, TFT_BLACK);
  tft.drawFastHLine(0, 258, 240, border);
  for (int c = 0; c < 5; c++) {
    int x0 = c * 48, cx = x0 + 24;
    tft.fillRoundRect(x0 + 3, top + 2, 42, h - 4, 6, fill);
    tft.drawRoundRect(x0 + 3, top + 2, 42, h - 4, 6, border);
    switch (c) {
      case 0: tft.fillTriangle(cx - a, cy, cx + a, cy - a, cx + a, cy + a, glyph); break;      // LEFT  <
      case 1: tft.fillTriangle(cx, cy - a, cx - a, cy + a, cx + a, cy + a, glyph); break;      // UP    ^
      case 2: tft.fillTriangle(cx, cy + a, cx - a, cy - a, cx + a, cy - a, glyph); break;      // DOWN  v
      case 3: tft.fillTriangle(cx + a, cy, cx - a, cy - a, cx - a, cy + a, glyph); break;      // RIGHT >
      default: tft.fillCircle(cx, cy, a - 1, glyph); tft.fillCircle(cx, cy, a - 5, fill);      // OK (center)
    }
  }
}

// Menu-band gesture machine. LEFT strip (sx<SLIDER_W) = live scroll slider (never
// selects). MAIN area = tap-to-select (tap latched on release; a drag past the slop
// cancels it, so scrolling never triggers a select).
static void menuGesture(bool bandDown, int sx, int sy) {
  uint32_t now = millis();
  if (bandDown) {
    if (!g_gDown) {
      if (now < g_gLockout) return;                       // dead time after a gesture
      g_gDown = true; g_gDownMs = now; g_gSettled = false; g_gMoved = false;
      g_inSlider = (sx < SLIDER_W);                       // which zone this press started in
    }
    if (!g_gSettled && now - g_gDownMs >= SETTLE_MS) { g_gAnchorY = sy; g_gSettled = true; }
    if (g_gSettled) {
      if (g_inSlider) { g_sliderActive = true; g_sliderY = sy; }        // slider: live scroll, no select
      else if (abs(sy - g_gAnchorY) > DRAG_SLOP) g_gMoved = true;       // main-area drag cancels the tap
    }
  } else if (g_gDown) {
    g_gDown = false; g_sliderActive = false;
    if (!g_inSlider && g_gSettled && !g_gMoved && (now - g_gDownMs) <= TAP_MAX_MS) g_menuTapY = g_gAnchorY;
    g_gLockout = now + LOCKOUT_MS;
  }
}

// One XPT sample -> drives g_pressedButton (bottom bar); in TOUCH_MENU also the band gestures.
static void touchSampleNow() {
  touchBusClaim();
  int z = xptPressure();
  bool down = g_gDown ? (z >= Z_UP) : (z >= Z_DOWN);      // release hysteresis
  long sx = 0, sy = 0;
  if (down) { uint16_t rawX = xptSample(CMD_X), rawY = xptSample(CMD_Y); xptToScreen(rawX, rawY, sx, sy); }

  if (g_touchMode == TOUCH_MENU) {
    g_pressedButton = (down && sy >= STRIP_Y) ? ((sx < 192) ? B_LEFT : B_RIGHT) : 0xFF;  // 2-zone BACK/LEVEL bar
    menuGesture(down && sy < STRIP_Y, (int)sx, (int)sy);
  } else {
    g_pressedButton = (down && sy >= STRIP_Y) ? buttonForColumn((int)sx) : 0xFF;          // 5-zone D-pad
  }
}

static void touchGatedSample() {
  uint32_t now = millis();
  if (now - g_lastPollMs >= SAMPLE_MS) { touchSampleNow(); g_lastPollMs = now; }
}

// ---- Calibration ----
static bool touchLoadCal() {
  TouchCal c;
  EEPROM.get(CAL_ADDR, c);
  if (c.magic != CAL_MAGIC) return false;
  if (abs((int)c.xr1 - (int)c.xr0) < 50 || abs((int)c.yr1 - (int)c.yr0) < 50) return false;
  g_cal = c;
  return true;
}

static void touchSaveCal() {
  g_cal.magic = CAL_MAGIC;
  EEPROM.put(CAL_ADDR, g_cal);
  EEPROM.commit();
}

// Block until a firm tap, return averaged raw at press, then wait for release.
static void touchWaitTap(uint16_t &rx, uint16_t &ry) {
  touchBusClaim();
  while (xptPressure() > Z_THRESHOLD / 2) delay(10);   // ensure released first
  while (xptPressure() < Z_THRESHOLD)     delay(10);   // wait for press
  delay(40);
  long ax = 0, ay = 0; int n = 0;
  for (int i = 0; i < 8 && xptPressure() > Z_THRESHOLD; i++) {
    ax += xptSample(CMD_X); ay += xptSample(CMD_Y); n++; delay(8);
  }
  if (n == 0) { rx = xptSample(CMD_X); ry = xptSample(CMD_Y); }
  else        { rx = ax / n; ry = ay / n; }
  while (xptPressure() > Z_THRESHOLD / 2) delay(10);   // wait release
}

static void drawTarget(int x, int y, const char *label) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextFont(2);
  tft.drawString("TOUCH CALIBRATION", 120, 150);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(label, 120, 176);
  tft.drawLine(x - 12, y, x + 12, y, TFT_YELLOW);
  tft.drawLine(x, y - 12, x, y + 12, TFT_YELLOW);
  tft.drawCircle(x, y, 10, TFT_YELLOW);
}

void touchCalibrate() {
  touchBusClaim();
  const int M = 18;
  const int px[4] = { M, SCREEN_W - M, M, SCREEN_W - M };          // TL, TR, BL, BR
  const int py[4] = { M, M, SCREEN_H - M, SCREEN_H - M };
  const char *lbl[4] = { "Tap TOP-LEFT", "Tap TOP-RIGHT", "Tap BOTTOM-LEFT", "Tap BOTTOM-RIGHT" };
  uint16_t rx[4], ry[4];
  for (int i = 0; i < 4; i++) {
    drawTarget(px[i], py[i], lbl[i]);
    touchWaitTap(rx[i], ry[i]);
    tft.fillCircle(px[i], py[i], 10, TFT_GREEN);
    delay(200);
  }
  // TL=0 TR=1 BL=2 BR=3
  long leftX = (rx[0] + rx[2]) / 2, rightX = (rx[1] + rx[3]) / 2;
  long leftY = (ry[0] + ry[2]) / 2, rightY = (ry[1] + ry[3]) / 2;
  long topX  = (rx[0] + rx[1]) / 2, botX   = (rx[2] + rx[3]) / 2;
  long topY  = (ry[0] + ry[1]) / 2, botY   = (ry[2] + ry[3]) / 2;

  bool swap = labs(rightY - leftY) > labs(rightX - leftX);  // which raw axis tracks horizontal?
  TouchCal c;
  c.swap = swap ? 1 : 0;
  if (!swap) { c.xr0 = leftX; c.xr1 = rightX; c.yr0 = topY; c.yr1 = botY; }
  else       { c.xr0 = leftY; c.xr1 = rightY; c.yr0 = topX; c.yr1 = botX; }

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  if (abs((int)c.xr1 - (int)c.xr0) < 50 || abs((int)c.yr1 - (int)c.yr0) < 50) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("CAL FAILED", 120, 150);
    delay(1200);
    touchCalibrate();   // retry
    return;
  }
  g_cal = c;
  touchSaveCal();
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("CALIBRATED", 120, 150);
  delay(800);
}

// Load stored cal, or run the 4-corner routine (first boot OR finger held at boot),
// then paint the D-pad bar.
void touchBegin() {
  bool have = touchLoadCal();
  touchBusClaim();
  bool held = xptPressure() > Z_THRESHOLD;   // hold the screen at power-up to re-calibrate
  if (!have || held) touchCalibrate();
  tft.fillScreen(TFT_BLACK);
  setTouchMode(TOUCH_MENU);   // paint the initial (menu) control bar
}

// ---- Menu tap/drag/scroll API (consumed by the menu loop) ----
bool touchMenuTap(int &sy) {
  if (g_menuTapY < 0) return false;
  sy = g_menuTapY; g_menuTapY = -1; return true;
}
bool touchMenuSlider(int &sy) {
  if (!g_sliderActive) return false;
  sy = g_sliderY;
  return true;
}

// Screen y -> absolute menu item index (mirrors the render + letterbox). Caller bounds-checks.
int touchScreenYToItem(int sy, int menuStart) {
  if (sy < 100 || sy >= 220) return -1;          // outside the u8g2 list band
  int by  = ((sy - 100) * 64) / 120;             // screen -> 64px buffer y
  int rel = by - 1; if (rel < 0) rel = 0;        // first row at buffer-y 1
  int row = rel / 21; if (row > 2) row = 2;      // pitch 21, 3 rows shown
  return menuStart + row;
}

// 2-zone menu control bar: wide BACK (left) + narrow LEVEL (right).
void drawMenuBar() {
  const uint16_t fill = tft.color565(16, 22, 18), border = tft.color565(46, 150, 74), glyph = tft.color565(74, 214, 112);
  const int top = 264, h = 42, cy = top + h / 2, a = 7;
  tft.fillRect(0, 258, 240, 62, TFT_BLACK);
  tft.drawFastHLine(0, 258, 240, border);
  // BACK zone (x 0..191)
  tft.fillRoundRect(3, top + 2, 183, h - 4, 6, fill);
  tft.drawRoundRect(3, top + 2, 183, h - 4, 6, border);
  int bx = 34;
  tft.fillTriangle(bx - a, cy, bx + a, cy - a, bx + a, cy + a, glyph);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(glyph, fill); tft.setTextFont(4);
  tft.drawString("BACK", bx + 20, cy);
  // LEVEL zone (x 192..239): three ascending bars
  tft.fillRoundRect(195, top + 2, 42, h - 4, 6, fill);
  tft.drawRoundRect(195, top + 2, 42, h - 4, 6, border);
  int lx = 210;
  tft.fillRect(lx,      cy + 2, 5, 8,  glyph);
  tft.fillRect(lx + 8,  cy - 3, 5, 13, glyph);
  tft.fillRect(lx + 16, cy - 8, 5, 18, glyph);
}

void setTouchMode(uint8_t m) {
  g_touchMode = m;
  g_gDown = false; g_sliderActive = false; g_menuTapY = -1;
  if (m == TOUCH_APP) drawDpadBar(); else drawMenuBar();
}

void touchInputSetup() {
  touchBusClaim();
  digitalWrite(XPT_MOSI, LOW);
  g_pressedButton = 0xFF;
  g_lastPollMs = 0;
}

int nyanDigitalRead(uint8_t pin) {
  switch (pin) {
    case B_UP:
    case B_DOWN:
    case B_CENTER:
    case B_LEFT:
    case B_RIGHT: {
      touchGatedSample();
      return (g_pressedButton == pin) ? LOW : HIGH;   // active-LOW
    }
    default:
      return digitalRead(pin);
  }
}
