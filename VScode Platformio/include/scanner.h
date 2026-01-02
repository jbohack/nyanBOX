/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#ifndef scanner_H
#define scanner_H

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <U8g2lib.h>
#include "esp_bt.h"
#include "esp_wifi.h"
#include "neopixel.h"

void scannerSetup();
void scannerLoop();

#endif
