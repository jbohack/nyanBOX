/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#ifndef setting_H
#define setting_H

#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>

extern bool neoPixelActive;
extern bool dangerousActionsEnabled;
extern bool continuousScanEnabled;
extern bool privacyModeEnabled;

void settingSetup();
void settingLoop();
bool isDangerousActionsEnabled();
bool isContinuousScanEnabled();
bool isPrivacyModeEnabled();

void maskMAC(const char* original, char* masked);
void maskName(const char* original, char* masked, int maxLen);

#endif
