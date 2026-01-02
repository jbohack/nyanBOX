/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include <Arduino.h>
#include <U8g2lib.h>
#include "../include/legal_disclaimer.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "../include/pindefs.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

bool showLegalDisclaimer() {
  int page = 0;
  bool buttonUpPressed = false;
  bool buttonDownPressed = false;
  bool buttonSelectPressed = false;
  bool buttonLeftPressed = false;
  bool buttonRightPressed = false;
  bool hasSeenAllPages = false;

  bool needsRedraw = true;
  int lastPage = -1;
  bool lastHasSeenAllPages = false;

  while (true) {
    checkIdle();

    bool up = !digitalRead(BUTTON_PIN_UP);
    bool down = !digitalRead(BUTTON_PIN_DOWN);
    bool right = !digitalRead(BUTTON_PIN_RIGHT);
    bool left = !digitalRead(BUTTON_PIN_LEFT);

    if (up) {
      if (!buttonUpPressed) {
        buttonUpPressed = true;
        if (page > 0) {
          page--;
          needsRedraw = true;
        }
      }
    } else {
      buttonUpPressed = false;
    }

    if (down) {
      if (!buttonDownPressed) {
        buttonDownPressed = true;
        if (page < 3) {
          page++;
          if (page == 3) {
            hasSeenAllPages = true;
          }
          needsRedraw = true;
        }
      }
    } else {
      buttonDownPressed = false;
    }

    if (left) {
      if (!buttonLeftPressed) {
        buttonLeftPressed = true;
        while (!digitalRead(BUTTON_PIN_LEFT)) {
          delay(10);
        }
        delay(100);
        return false;
      }
    } else {
      buttonLeftPressed = false;
    }

    if (right) {
      if (!buttonRightPressed) {
        buttonRightPressed = true;
        if (page == 3 && hasSeenAllPages) {
          while (!digitalRead(BUTTON_PIN_RIGHT)) {
            delay(10);
          }
          delay(100);
          return true;
        }
      }
    } else {
      buttonRightPressed = false;
    }

    if (lastPage != page) {
      lastPage = page;
      needsRedraw = true;
    }
    if (lastHasSeenAllPages != hasSeenAllPages) {
      lastHasSeenAllPages = hasSeenAllPages;
      needsRedraw = true;
    }

    if (!needsRedraw) {
      delay(50);
      continue;
    }

    needsRedraw = false;
    u8g2.clearBuffer();

    if (page == 0) {
      u8g2.setFont(u8g2_font_helvB08_tr);
      int titleWidth = u8g2.getUTF8Width("JAMMING WARNING");
      u8g2.drawStr((128 - titleWidth) / 2, 14, "JAMMING WARNING");
      u8g2.drawLine((128 - titleWidth) / 2 - 5, 18, (128 + titleWidth) / 2 + 5, 18);

      u8g2.setFont(u8g2_font_6x10_tr);
      int line1Width = u8g2.getUTF8Width("Jamming tools are");
      u8g2.drawStr((128 - line1Width) / 2, 30, "Jamming tools are");

      int line2Width = u8g2.getUTF8Width("for educational &");
      u8g2.drawStr((128 - line2Width) / 2, 42, "for educational &");

      int line3Width = u8g2.getUTF8Width("authorized use only");
      u8g2.drawStr((128 - line3Width) / 2, 54, "authorized use only");
    }
    else if (page == 1) {
      u8g2.setFont(u8g2_font_helvB08_tr);
      int titleWidth = u8g2.getUTF8Width("LEGAL WARNING");
      u8g2.drawStr((128 - titleWidth) / 2, 14, "LEGAL WARNING");
      u8g2.drawLine((128 - titleWidth) / 2 - 5, 18, (128 + titleWidth) / 2 + 5, 18);

      u8g2.setFont(u8g2_font_6x10_tr);
      int line1Width = u8g2.getUTF8Width("Unauthorized jamming");
      u8g2.drawStr((128 - line1Width) / 2, 30, "Unauthorized jamming");

      int line2Width = u8g2.getUTF8Width("may be illegal in");
      u8g2.drawStr((128 - line2Width) / 2, 42, "may be illegal in");

      int line3Width = u8g2.getUTF8Width("your jurisdiction");
      u8g2.drawStr((128 - line3Width) / 2, 54, "your jurisdiction");
    }
    else if (page == 2) {
      u8g2.setFont(u8g2_font_helvB08_tr);
      int titleWidth = u8g2.getUTF8Width("RESPONSIBILITY");
      u8g2.drawStr((128 - titleWidth) / 2, 14, "RESPONSIBILITY");
      u8g2.drawLine((128 - titleWidth) / 2 - 5, 18, (128 + titleWidth) / 2 + 5, 18);

      u8g2.setFont(u8g2_font_6x10_tr);
      int line1Width = u8g2.getUTF8Width("You are responsible");
      u8g2.drawStr((128 - line1Width) / 2, 30, "You are responsible");

      int line2Width = u8g2.getUTF8Width("for compliance with");
      u8g2.drawStr((128 - line2Width) / 2, 42, "for compliance with");

      int line3Width = u8g2.getUTF8Width("all applicable laws");
      u8g2.drawStr((128 - line3Width) / 2, 54, "all applicable laws");
    }
    else if (page == 3) {
      u8g2.setFont(u8g2_font_helvB08_tr);
      int titleWidth = u8g2.getUTF8Width("AGREEMENT");
      u8g2.drawStr((128 - titleWidth) / 2, 14, "AGREEMENT");
      u8g2.drawLine((128 - titleWidth) / 2 - 5, 18, (128 + titleWidth) / 2 + 5, 18);

      u8g2.setFont(u8g2_font_6x10_tr);
      int line1Width = u8g2.getUTF8Width("I agree to use");
      u8g2.drawStr((128 - line1Width) / 2, 30, "I agree to use");

      int line2Width = u8g2.getUTF8Width("jamming tools");
      u8g2.drawStr((128 - line2Width) / 2, 42, "jamming tools");

      int line3Width = u8g2.getUTF8Width("lawfully & ethically");
      u8g2.drawStr((128 - line3Width) / 2, 54, "lawfully & ethically");
    }

    u8g2.setFont(u8g2_font_4x6_tr);
    if (page < 3) {
      int instrWidth = u8g2.getUTF8Width("DOWN=Next  LEFT=Exit");
      u8g2.drawStr((128 - instrWidth) / 2, 64, "DOWN=Next  LEFT=Exit");
    } else if (page == 3 && hasSeenAllPages) {
      int instrWidth = u8g2.getUTF8Width("UP=Back  RIGHT=Accept");
      u8g2.drawStr((128 - instrWidth) / 2, 64, "UP=Back  RIGHT=Accept");
    }

    u8g2.sendBuffer();
    displayMirrorSend(u8g2);

    delay(50);
  }
}