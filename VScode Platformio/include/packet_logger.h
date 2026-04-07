/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2026 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#ifndef PACKET_LOGGER_H
#define PACKET_LOGGER_H

#include "esp_wifi.h"
#include "pindefs.h"
#include <U8g2lib.h>

void packetLoggerSetup();
void packetLoggerLoop();

#endif
