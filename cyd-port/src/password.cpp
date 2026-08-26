/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2026 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include <EEPROM.h>
#include <U8g2lib.h>
#include <Arduino.h>

#include "../include/pindefs.h"
#include "../include/password.h"
#include "../include/sleep_manager.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

#define EEPROM_ADDR_PW_SEQ  6
#define PW_MAX_LEN          8

static uint8_t readStoredSequence(uint8_t* out) {
    uint8_t len = 0;
    for (int i = 0; i < PW_MAX_LEN; i++) {
        uint8_t b = EEPROM.read(EEPROM_ADDR_PW_SEQ + i);
        if (b < 1 || b > 4) break;
        out[len++] = b;
    }
    return len;
}

bool passwordEnabled() {
    uint8_t b = EEPROM.read(EEPROM_ADDR_PW_SEQ);
    return b >= 1 && b <= 4;
}

void clearPassword() {
    EEPROM.write(EEPROM_ADDR_PW_SEQ, 0x00);
    EEPROM.commit();
}

static void drawPasswordScreen(const char* title, uint8_t entered, bool showError) {
    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_helvB14_tr);
    u8g2.setCursor((128 - u8g2.getUTF8Width(title)) / 2, 16);
    u8g2.print(title);

    u8g2.setFont(u8g2_font_helvR08_tr);
    const char* lbl = "Enter Password";
    u8g2.setCursor((128 - u8g2.getUTF8Width(lbl)) / 2, 30);
    u8g2.print(lbl);

    {
        const int AW = 9, AG = 4;
        int totalW = PW_MAX_LEN * AW + (PW_MAX_LEN - 1) * AG;
        int sx = (128 - totalW) / 2;
        int sy = 37;
        for (int i = 0; i < PW_MAX_LEN; i++) {
            int ax = sx + i * (AW + AG);
            if (i < (int)entered) {
                u8g2.drawBox(ax + 2, sy + 1, 5, 5);
            } else {
                u8g2.drawHLine(ax + 2, sy + 3, 5);
            }
        }
    }

    u8g2.setFont(u8g2_font_helvR08_tr);
    if (showError) {
        const char* err = "Incorrect, try again";
        u8g2.setCursor((128 - u8g2.getUTF8Width(err)) / 2, 60);
        u8g2.print(err);
    } else {
        const char* hint = "SELECT to confirm";
        u8g2.setCursor((128 - u8g2.getUTF8Width(hint)) / 2, 60);
        u8g2.print(hint);
    }

    u8g2.sendBuffer();
}

void checkPasswordOnBoot() {
    uint8_t stored[PW_MAX_LEN];
    uint8_t storedLen = readStoredSequence(stored);
    if (storedLen == 0) return;

    bool upPrev = false, downPrev = false, leftPrev = false, rightPrev = false, selPrev = false;
    unsigned long upT = 0, downT = 0, leftT = 0, rightT = 0, selT = 0;
    const unsigned long db = 200;

    uint8_t entered[PW_MAX_LEN];
    uint8_t enteredLen = 0;

    drawPasswordScreen("nyanBOX", enteredLen, false);

    while (true) {
        checkIdle();
        if (anyButtonPressed()) updateLastActivity();

        unsigned long now = millis();
        bool upNow    = digitalRead(BUTTON_PIN_UP)     == LOW;
        bool downNow  = digitalRead(BUTTON_PIN_DOWN)   == LOW;
        bool leftNow  = digitalRead(BUTTON_PIN_LEFT)   == LOW;
        bool rightNow = digitalRead(BUTTON_PIN_RIGHT)  == LOW;
        bool selNow   = digitalRead(BUTTON_PIN_CENTER) == LOW;

        uint8_t pressed = 0;
        bool selPressed = false;

        if (upNow != upPrev && now - upT > db) {
            upT = now; upPrev = upNow;
            if (upNow) pressed = 1;
        }
        if (downNow != downPrev && now - downT > db) {
            downT = now; downPrev = downNow;
            if (downNow) pressed = 2;
        }
        if (leftNow != leftPrev && now - leftT > db) {
            leftT = now; leftPrev = leftNow;
            if (leftNow) pressed = 3;
        }
        if (rightNow != rightPrev && now - rightT > db) {
            rightT = now; rightPrev = rightNow;
            if (rightNow) pressed = 4;
        }
        if (selNow != selPrev && now - selT > db) {
            selT = now; selPrev = selNow;
            if (selNow) selPressed = true;
        }

        if (pressed && enteredLen < PW_MAX_LEN) {
            entered[enteredLen++] = pressed;
            drawPasswordScreen("nyanBOX", enteredLen, false);
        }

        if (selPressed) {
            bool correct = (enteredLen == storedLen);
            for (int i = 0; i < (int)storedLen && correct; i++) {
                if (entered[i] != stored[i]) correct = false;
            }
            if (correct) {
                while (anyButtonPressed()) { delay(10); }
                delay(50);
                return;
            }
            drawPasswordScreen("nyanBOX", enteredLen, true);
            delay(1500);
            enteredLen = 0;
            drawPasswordScreen("nyanBOX", enteredLen, false);
        }

        delay(10);
    }
}

static void drawArrowGlyph(int x, int y, uint8_t dir) {
    switch (dir) {
        case 1: u8g2.drawTriangle(x+4, y,   x,   y+6, x+8, y+6); break;
        case 2: u8g2.drawTriangle(x+4, y+6, x,   y,   x+8, y  ); break;
        case 3: u8g2.drawTriangle(x,   y+3, x+8, y,   x+8, y+6); break;
        case 4: u8g2.drawTriangle(x+8, y+3, x,   y,   x,   y+6); break;
    }
}

static void drawSetPasswordScreen(uint8_t* seq, uint8_t len) {
    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_helvB14_tr);
    const char* title = "nyanBOX";
    u8g2.setCursor((128 - u8g2.getUTF8Width(title)) / 2, 16);
    u8g2.print(title);

    u8g2.setFont(u8g2_font_helvR08_tr);

    const char* sub = "Set Password";
    u8g2.setCursor((128 - u8g2.getUTF8Width(sub)) / 2, 30);
    u8g2.print(sub);

    {
        const int AW = 9, AG = 4;
        int totalW = PW_MAX_LEN * AW + (PW_MAX_LEN - 1) * AG;
        int sx = (128 - totalW) / 2;
        int sy = 37;
        for (int i = 0; i < PW_MAX_LEN; i++) {
            int ax = sx + i * (AW + AG);
            if (i < (int)len) {
                drawArrowGlyph(ax, sy, seq[i]);
            } else {
                u8g2.drawHLine(ax + 2, sy + 3, 5);
            }
        }
    }

    const char* hint = len == 0 ? "SELECT to cancel" : "SELECT to save";
    u8g2.setCursor((128 - u8g2.getUTF8Width(hint)) / 2, 60);
    u8g2.print(hint);

    u8g2.sendBuffer();
}

void setPasswordInSettings() {
    bool upPrev = false, downPrev = false, leftPrev = false, rightPrev = false, selPrev = false;
    unsigned long upT = 0, downT = 0, leftT = 0, rightT = 0, selT = 0;
    const unsigned long db = 200;

    uint8_t seq[PW_MAX_LEN];
    uint8_t seqLen = 0;

    while (digitalRead(BUTTON_PIN_UP)    == LOW ||
           digitalRead(BUTTON_PIN_DOWN)  == LOW ||
           digitalRead(BUTTON_PIN_LEFT)  == LOW ||
           digitalRead(BUTTON_PIN_RIGHT) == LOW ||
           digitalRead(BUTTON_PIN_CENTER) == LOW) { delay(10); }
    delay(50);

    drawSetPasswordScreen(seq, seqLen);

    while (true) {
        checkIdle();
        if (anyButtonPressed()) updateLastActivity();

        unsigned long now = millis();
        bool upNow    = digitalRead(BUTTON_PIN_UP)    == LOW;
        bool downNow  = digitalRead(BUTTON_PIN_DOWN)  == LOW;
        bool leftNow  = digitalRead(BUTTON_PIN_LEFT)  == LOW;
        bool rightNow = digitalRead(BUTTON_PIN_RIGHT) == LOW;
        bool selNow   = digitalRead(BUTTON_PIN_CENTER) == LOW;

        uint8_t pressed = 0;
        bool selPressed = false;

        if (upNow != upPrev && now - upT > db) {
            upT = now; upPrev = upNow;
            if (upNow) pressed = 1;
        }
        if (downNow != downPrev && now - downT > db) {
            downT = now; downPrev = downNow;
            if (downNow) pressed = 2;
        }
        if (leftNow != leftPrev && now - leftT > db) {
            leftT = now; leftPrev = leftNow;
            if (leftNow) pressed = 3;
        }
        if (rightNow != rightPrev && now - rightT > db) {
            rightT = now; rightPrev = rightNow;
            if (rightNow) pressed = 4;
        }
        if (selNow != selPrev && now - selT > db) {
            selT = now; selPrev = selNow;
            if (selNow) selPressed = true;
        }

        if (pressed && seqLen < PW_MAX_LEN) {
            seq[seqLen++] = pressed;
            drawSetPasswordScreen(seq, seqLen);
        }

        if (selPressed) {
            if (seqLen > 0) {
                for (int i = 0; i < (int)seqLen; i++) {
                    EEPROM.write(EEPROM_ADDR_PW_SEQ + i, seq[i]);
                }
                if (seqLen < PW_MAX_LEN) {
                    EEPROM.write(EEPROM_ADDR_PW_SEQ + seqLen, 0x00);
                }
                EEPROM.commit();

                u8g2.clearBuffer();
                u8g2.setFont(u8g2_font_helvB14_tr);
                const char* t = "nyanBOX";
                u8g2.setCursor((128 - u8g2.getUTF8Width(t)) / 2, 16);
                u8g2.print(t);
                u8g2.setFont(u8g2_font_helvR08_tr);
                const char* msg = "Password saved";
                u8g2.setCursor((128 - u8g2.getUTF8Width(msg)) / 2, 38);
                u8g2.print(msg);
                u8g2.sendBuffer();
                delay(1000);
            }
            while (digitalRead(BUTTON_PIN_CENTER) == LOW) { delay(10); }
            delay(50);
            return;
        }

        delay(10);
    }
}
