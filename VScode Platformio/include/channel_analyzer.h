/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#ifndef CHANNEL_MONITOR_H
#define CHANNEL_MONITOR_H

#include <U8g2lib.h>
#include <Arduino.h>
#include "pindefs.h"

void channelAnalyzerSetup();
void channelAnalyzerLoop();

void drawNetworkCountView();
void drawSignalStrengthView();
const char* getSignalStrengthLabel(int rssi);

#endif