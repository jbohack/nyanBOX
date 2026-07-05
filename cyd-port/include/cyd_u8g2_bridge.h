/*
    cyd_u8g2_bridge.h  — nyanBOX CYD port

    Shadows the SSD1306 128x64 U8g2 object with a subclass that renders the
    monochrome full-buffer onto the ESP32-2432S028 (CYD) ILI9341 TFT via
    TFT_eSPI, letterboxed into a 240x120 band. The U8g2 transport callbacks are
    no-ops, so begin()/setContrast()/setPowerSave() emit ZERO I2C traffic.

    Force-included into project sources only (build_src_flags: -include ...).
    MUST NOT include pindefs.h (that file installs the digitalRead touch macro;
    keeping it out preserves the real digitalRead/TFT SPI here).
*/
#ifndef CYD_U8G2_BRIDGE_H
#define CYD_U8G2_BRIDGE_H

#include <U8g2lib.h>
#include <TFT_eSPI.h>

// The single ODR definition lives in src/cyd_u8g2_bridge.cpp
extern TFT_eSPI tft;

// --- No-op U8x8 transport callbacks (return 1 = "handled", touch nothing) ---
static uint8_t cyd_u8x8_byte_noop(u8x8_t *, uint8_t, uint8_t, void *) { return 1; }
static uint8_t cyd_u8x8_gpio_noop(u8x8_t *, uint8_t, uint8_t, void *) { return 1; }

class CydU8g2 : public U8G2 {
public:
  CydU8g2(const u8g2_cb_t *rotation = U8G2_R0, uint8_t /*reset*/ = U8X8_PIN_NONE) {
    // Full-buffer 128x64 SSD1306 setup, but wired to empty transport callbacks.
    // This allocates the 1024-byte RAM tile buffer we render from.
    u8g2_Setup_ssd1306_128x64_noname_f(&u8g2, rotation,
                                       cyd_u8x8_byte_noop,
                                       cyd_u8x8_gpio_noop);
  }

  // Bring up the TFT once; the U8g2 RAM buffer is already allocated.
  void begin() {
    // CYD backlight is on GPIO21, active-HIGH. Drive it explicitly — TFT_eSPI's
    // init() does NOT turn it on in this build, so the panel stays dark ("not
    // powering on") even though the ILI9341 is initialized and being written to.
#ifdef TFT_BL
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
#else
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);
#endif
    tft.init();
    tft.setRotation(0);          // portrait 240x320
    tft.fillScreen(TFT_BLACK);
    clearBuffer();
    // The on-screen arrow D-pad is drawn by touch_input.cpp (touchBegin/drawDpadBar).
  }

  // Blit the 128x64 monochrome buffer to a 240x120 letterboxed band (y 100..219).
  void sendBuffer() {
    uint8_t *buf = getBufferPtr();
    if (!buf) return;
    static uint16_t line[240];
    const uint16_t fg = TFT_WHITE;
    const uint16_t bg = TFT_BLACK;
    const int Y0 = 100;          // (320 - 120) / 2
    for (int dy = 0; dy < 120; dy++) {
      int sy   = (dy * 64) / 120;
      int page = (sy >> 3) * 128; // tile row * width
      uint8_t mask = (uint8_t)(1 << (sy & 7));
      for (int dx = 0; dx < 240; dx++) {
        int sx = (dx * 128) / 240;
        line[dx] = (buf[page + sx] & mask) ? fg : bg;
      }
      tft.pushImage(0, Y0 + dy, 240, 1, line);
    }
  }
};

// Rewrite the panel type used at nyanBOX.ino:79 to our TFT-backed subclass.
#define U8G2_SSD1306_128X64_NONAME_F_HW_I2C CydU8g2

#endif // CYD_U8G2_BRIDGE_H
