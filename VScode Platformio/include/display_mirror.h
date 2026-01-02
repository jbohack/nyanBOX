/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#ifndef DISPLAY_MIRROR_H
#define DISPLAY_MIRROR_H

#include <U8g2lib.h>

void displayMirrorSetup();

void displayMirrorSend(U8G2_SSD1306_128X64_NONAME_F_HW_I2C &display);

void displayMirrorEnable(bool enable);

bool displayMirrorEnabled();

#endif
