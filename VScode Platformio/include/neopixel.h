/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/
#ifndef NEOPIXEL_H
#define NEOPIXEL_H

#include <Adafruit_NeoPixel.h>

extern Adafruit_NeoPixel pixels;

void neopixelSetup();
void neopixelLoop();

void blinkColor(uint8_t r, uint8_t g, uint8_t b);
void stopBlinking();

#endif // NEOPIXEL_H
