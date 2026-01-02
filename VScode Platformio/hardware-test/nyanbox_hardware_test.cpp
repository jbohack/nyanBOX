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
#include <SPI.h>
#include <RF24.h>
#include <Adafruit_NeoPixel.h>
#include "../include/pindefs.h"

const char* VERSION = "v1.0";

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
Adafruit_NeoPixel pixels(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
RF24 radios[] = {
    RF24(RADIO_CE_PIN_1, RADIO_CSN_PIN_1, 16000000),
    RF24(RADIO_CE_PIN_2, RADIO_CSN_PIN_2, 16000000),
    RF24(RADIO_CE_PIN_3, RADIO_CSN_PIN_3, 16000000)
};

bool radioResults[3] = {false, false, false};
bool testingComplete = false;
int testPhase = 0;

void displayTest() {
    u8g2.clearBuffer();
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.drawFrame(2, 2, 124, 60);
    u8g2.setFont(u8g2_font_helvB12_tr);
    u8g2.drawStr(20, 22, "nyanBOX");
    u8g2.setFont(u8g2_font_helvR08_tr);
    u8g2.drawStr(20, 38, "HW TEST ");
    u8g2.drawStr(70, 38, VERSION);
    u8g2.sendBuffer();
    delay(2000);

    // Full white screen
    u8g2.clearBuffer();
    u8g2.drawBox(0, 0, 128, 64);
    u8g2.sendBuffer();
    delay(2000);

    // Simple alternating pattern
    u8g2.clearBuffer();
    for (int x = 0; x < 128; x += 16) {
        for (int y = 0; y < 64; y += 16) {
            u8g2.drawBox(x, y, 8, 8);
            u8g2.drawBox(x + 8, y + 8, 8, 8);
        }
    }
    u8g2.sendBuffer();
    delay(2000);

    // Lines test
    u8g2.clearBuffer();
    u8g2.drawHLine(0, 16, 128);
    u8g2.drawHLine(0, 32, 128);
    u8g2.drawHLine(0, 48, 128);
    u8g2.drawVLine(32, 0, 64);
    u8g2.drawVLine(64, 0, 64);
    u8g2.drawVLine(96, 0, 64);
    u8g2.sendBuffer();
    delay(2000);

    // Border and corners
    u8g2.clearBuffer();
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.drawFrame(2, 2, 124, 60);
    u8g2.drawBox(0, 0, 10, 10);
    u8g2.drawBox(118, 0, 10, 10);
    u8g2.drawBox(0, 54, 10, 10);
    u8g2.drawBox(118, 54, 10, 10);
    u8g2.sendBuffer();
    delay(2000);
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("");
    Serial.println("========================================");
    Serial.print("nyanBOX Hardware QC Test ");
    Serial.println(VERSION);
    Serial.println("Manufacturing Quality Control");
    Serial.println("https://github.com/jbohack/nyanBOX");
    Serial.println("========================================");

    u8g2.begin();
    pixels.begin();
    pixels.clear();
    pixels.show();

    int buttonPins[] = {BUTTON_PIN_UP, BUTTON_PIN_DOWN, BUTTON_PIN_LEFT, BUTTON_PIN_RIGHT, BUTTON_PIN_CENTER};
    for (int pin : buttonPins) pinMode(pin, INPUT_PULLUP);

    Serial.println("Starting display test...");
    displayTest();

    SPI.begin();
    int cePins[] = {RADIO_CE_PIN_1, RADIO_CE_PIN_2, RADIO_CE_PIN_3};
    int csnPins[] = {RADIO_CSN_PIN_1, RADIO_CSN_PIN_2, RADIO_CSN_PIN_3};

    for (int i = 0; i < 3; i++) {
        pinMode(cePins[i], OUTPUT);
        pinMode(csnPins[i], OUTPUT);
        digitalWrite(csnPins[i], HIGH);
        digitalWrite(cePins[i], LOW);
    }
    delay(100);

    Serial.println("");
    Serial.println("Enabling NeoPixel RGB cycle...");
    Serial.println("");
    testPhase = 1;
}


void loop() {
    static unsigned long lastPhaseTime = 0;
    static unsigned long lastButtonCheck = 0;
    static unsigned long lastNeo = 0;
    static int colorStep = 0;
    if (millis() - lastNeo > 5) {
        lastNeo = millis();
        int r = 0, g = 0, b = 0;
        int phase = (colorStep / 85) % 6;
        int fade = colorStep % 85;
        int intensity = (fade * 255) / 84;

        switch(phase) {
            case 0: r = 255; g = intensity; break;
            case 1: r = 255 - intensity; g = 255; break;
            case 2: g = 255; b = intensity; break;
            case 3: g = 255 - intensity; b = 255; break;
            case 4: b = 255; r = intensity; break;
            case 5: b = 255 - intensity; r = 255; break;
        }

        pixels.setPixelColor(0, pixels.Color(r, g, b));
        pixels.show();
        colorStep = (colorStep + 1) % 510;
    }

    if (!testingComplete && millis() - lastPhaseTime > 3000) {
        lastPhaseTime = millis();

        int radioIndex = testPhase - 1;
        if (radioIndex < 3) {
            Serial.print("Starting Radio ");
            Serial.print(radioIndex + 1);
            Serial.println(" test...");

            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_helvB12_tr);
            u8g2.drawStr(10, 16, (String("RADIO ") + (radioIndex + 1) + " TEST").c_str());

            const char* pinInfo[] = {"CE:5, CSN:17", "CE:16, CSN:4", "CE:15, CSN:2"};
            u8g2.setFont(u8g2_font_helvR08_tr);
            u8g2.drawStr(10, 32, pinInfo[radioIndex]);

            radioResults[radioIndex] = radios[radioIndex].begin();
            if (radioResults[radioIndex] && radios[radioIndex].isChipConnected()) {
                radios[radioIndex].setChannel(radioIndex + 1);
                radios[radioIndex].setPALevel(RF24_PA_LOW);
                Serial.print("Radio");
                Serial.print(radioIndex + 1);
                Serial.println(": OK");
                u8g2.drawStr(10, 48, "Status: OK");
            } else {
                radioResults[radioIndex] = false;
                Serial.print("Radio");
                Serial.print(radioIndex + 1);
                Serial.println(": FAIL");
                u8g2.drawStr(10, 48, "Status: FAIL");
            }
            u8g2.sendBuffer();
            testPhase++;

        } else {
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_helvB12_tr);
            u8g2.drawStr(10, 15, "RESULTS");
            u8g2.setFont(u8g2_font_6x10_tr);

            for (int i = 0; i < 3; i++) {
                String result = "Radio" + String(i + 1) + ": " + (radioResults[i] ? "OK" : "FAIL");
                u8g2.drawStr(10, 28 + i * 12, result.c_str());
            }

            u8g2.setFont(u8g2_font_5x8_tr);
            u8g2.drawStr(10, 62, "Press any button");
            u8g2.sendBuffer();
            Serial.println("");

            Serial.println("==== SUMMARY ====");
            const char* pins[] = {"(CE:5, CSN:17)", "(CE:16, CSN:4)", "(CE:15, CSN:2)"};
            for (int i = 0; i < 3; i++) {
                Serial.print("Radio");
                Serial.print(i + 1);
                Serial.print(" ");
                Serial.print(pins[i]);
                Serial.println(radioResults[i] ? ": OK" : ": FAIL");
            }
            Serial.println("Button test ready...");
            testingComplete = true;
        }
    }

    if (millis() - lastButtonCheck > 100) {
        lastButtonCheck = millis();
        static bool lastButtons[5] = {false, false, false, false, false};
        const char* buttonNames[] = {"UP", "DOWN", "LEFT", "RIGHT", "CENTER"};
        const int buttonPins[] = {BUTTON_PIN_UP, BUTTON_PIN_DOWN, BUTTON_PIN_LEFT, BUTTON_PIN_RIGHT, BUTTON_PIN_CENTER};

        for (int i = 0; i < 5; i++) {
            bool pressed = !digitalRead(buttonPins[i]);
            if (pressed && !lastButtons[i]) {
                Serial.print("BUTTON: ");
                Serial.println(buttonNames[i]);

                if (testingComplete) {
                    u8g2.clearBuffer();

                    switch(i) {
                        case 0: // UP
                            u8g2.drawTriangle(64, 20, 54, 35, 74, 35);
                            u8g2.drawBox(59, 35, 10, 15);
                            break;
                        case 1: // DOWN
                            u8g2.drawBox(59, 15, 10, 15);
                            u8g2.drawTriangle(64, 45, 54, 30, 74, 30);
                            break;
                        case 2: // LEFT
                            u8g2.drawTriangle(35, 32, 50, 22, 50, 42);
                            u8g2.drawBox(50, 27, 15, 10);
                            break;
                        case 3: // RIGHT
                            u8g2.drawBox(63, 27, 15, 10);
                            u8g2.drawTriangle(93, 32, 78, 22, 78, 42);
                            break;
                        case 4: // CENTER
                            u8g2.drawCircle(64, 32, 12);
                            u8g2.drawDisc(64, 32, 6);
                            break;
                    }
                    u8g2.sendBuffer();
                    delay(500);

                    u8g2.clearBuffer();
                    u8g2.setFont(u8g2_font_helvB12_tr);
                    u8g2.drawStr(10, 15, "RESULTS");
                    u8g2.setFont(u8g2_font_6x10_tr);
                    for (int j = 0; j < 3; j++) {
                        String result = "Radio" + String(j + 1) + ": " + (radioResults[j] ? "OK" : "FAIL");
                        u8g2.drawStr(10, 28 + j * 12, result.c_str());
                    }
                    u8g2.setFont(u8g2_font_5x8_tr);
                    u8g2.drawStr(10, 62, "Press any button");
                    u8g2.sendBuffer();
                }
            }
            lastButtons[i] = pressed;
        }
    }
}