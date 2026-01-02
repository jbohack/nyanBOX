/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#ifndef NYANBOX_DETECTOR_H
#define NYANBOX_DETECTOR_H

#include <esp_bt.h>
#include <U8g2lib.h>
#include "neopixel.h"
#include "pindefs.h"

void nyanboxDetectorSetup();
void nyanboxDetectorLoop();

#endif