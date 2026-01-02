/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#ifndef PWNAGOTCHI_SPAM_H
#define PWNAGOTCHI_SPAM_H

#include <Arduino.h>
#include <U8g2lib.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include "pindefs.h"
#include "neopixel.h"

void pwnagotchiSpamSetup();
void pwnagotchiSpamLoop();

#endif