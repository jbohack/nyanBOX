/*
    touch_input.h — nyanBOX CYD port

    Declares the touch-input glue that emulates the five nyanBOX control buttons
    on the CYD (ESP32-2432S028) resistive touchscreen (XPT2046).

    pindefs.h installs:  #define digitalRead(pin) nyanDigitalRead(pin)
    so every project-side digitalRead(BUTTON_PIN_*) is routed to the touch zones.
    Non-button pins fall through to the real digitalRead.
*/
#ifndef TOUCH_INPUT_H
#define TOUCH_INPUT_H

#include <stdint.h>

// Bring up the bit-bang XPT2046 touch bus. Call once from setup().
void touchInputSetup();

// Button emulation: returns LOW (0) when the touch zone mapped to `pin` is
// pressed, HIGH (1) otherwise. Any non-button pin is forwarded to the real
// Arduino digitalRead so this remains a transparent shim.
int nyanDigitalRead(uint8_t pin);

// Load stored calibration, or run the 4-corner tap calibration (first boot / finger
// held at power-up), then paint the on-screen arrow D-pad. Call once from setup().
void touchBegin();

// Force the on-screen 4-corner calibration and store the result to EEPROM.
void touchCalibrate();

// --- Touch mode: MENU screens are tap-to-select + drag-to-scroll with a 2-zone
//     BACK/LEVEL bar; apps (and the level screen) use the 5-zone arrow D-pad. ---
enum { TOUCH_MENU = 0, TOUCH_APP = 1 };
void setTouchMode(uint8_t m);        // switch mode + repaint the matching bottom bar
bool touchMenuTap(int &sy);          // true once per completed tap in the MAIN area; gives screen y
bool touchMenuSlider(int &sy);       // true while dragging the LEFT scroll slider; gives screen y (never selects)
int  touchScreenYToItem(int sy, int menuStart);  // screen y -> absolute item index (caller bounds-checks)

#endif // TOUCH_INPUT_H
