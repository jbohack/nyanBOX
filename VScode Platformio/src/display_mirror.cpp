/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/display_mirror.h"
#include <Arduino.h>

static bool mirrorEnabled = false;

void displayMirrorSetup() {
    mirrorEnabled = false;
}

void displayMirrorEnable(bool enable) {
    mirrorEnabled = enable;
}

bool displayMirrorEnabled() {
    return mirrorEnabled;
}

void displayMirrorSend(U8G2_SSD1306_128X64_NONAME_F_HW_I2C &display) {
    if (!mirrorEnabled) return;
    if (!Serial) return;

    uint8_t *buffer = display.getBufferPtr();
    size_t bufferSize = display.getBufferTileWidth() * display.getBufferTileHeight() * 8;

    Serial.write("<FB>");

    uint16_t size = bufferSize;
    Serial.write((uint8_t)(size & 0xFF));
    Serial.write((uint8_t)((size >> 8) & 0xFF));

    Serial.write(buffer, bufferSize);

    Serial.write("</FB>");
}
