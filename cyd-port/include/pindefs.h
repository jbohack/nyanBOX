/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

// pindefs.h
#ifndef PINDEFS_H
#define PINDEFS_H

// Button Pin Definitions
#define BUTTON_PIN_UP      26
#define BUTTON_PIN_DOWN    33
#define BUTTON_PIN_CENTER  32  // Exit
#define BUTTON_PIN_LEFT    25  // Back
#define BUTTON_PIN_RIGHT   27  // Select

// Radio Pins (CYD PIN-FIT remap — SPI bus stays default VSPI 18/19/23)
#define RADIO_CE_PIN_1     22
#define RADIO_CSN_PIN_1     5
#define RADIO_CE_PIN_2     27
#define RADIO_CSN_PIN_2    17
#define RADIO_CE_PIN_3      4
#define RADIO_CSN_PIN_3    16

// NeoPixel
#define NEOPIXEL_PIN        0    // was 14 = TFT_SCLK (would corrupt the display)

#include "touch_input.h"
#define digitalRead(pin) nyanDigitalRead(pin)   // route the 5 button pins to touch

#endif // PINDEFS_H
