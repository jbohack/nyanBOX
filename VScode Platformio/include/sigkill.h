/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#ifndef sigkill_H
#define sigkill_H

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "neopixel.h"
#include "esp_bt.h"
#include "esp_wifi.h"
#include "neopixel.h"

void sigkillSetup();
void sigkillLoop();

#endif
