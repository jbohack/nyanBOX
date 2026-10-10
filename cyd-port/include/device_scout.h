/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#ifndef DEVICE_SCOUT_H
#define DEVICE_SCOUT_H

#include <U8g2lib.h>
#include <Arduino.h>
#include "../include/pindefs.h"

void deviceScoutSetup();
void deviceScoutLoop();
void cleanupDeviceScout();

#endif
