/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include <Arduino.h>
#include "../include/analyzer.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "../include/setting.h"
#include <esp_bt_main.h>
#include "../include/pindefs.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern Adafruit_NeoPixel pixels;

#define NRF24_CONFIG      0x00
#define NRF24_EN_AA       0x01
#define NRF24_RF_CH       0x05
#define NRF24_RF_SETUP    0x06
#define NRF24_RPD         0x09

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define CHANNELS 128

uint8_t spectrum[CHANNELS];
uint8_t peakSignal = 0;
uint8_t peakChannel = 0;
uint8_t avgSignal = 0;

uint8_t viewMode = 0;

enum ChannelFilter {
    FILTER_ALL = 0,
    FILTER_WIFI = 1,
    FILTER_BLUETOOTH = 2,
    FILTER_LOW = 3,
    FILTER_MID_LOW = 4,
    FILTER_MID_HIGH = 5,
    FILTER_HIGH = 6,
    FILTER_COUNT = 7
};

struct FilterRange {
    uint8_t start;
    uint8_t end;
    const char* name;
};

const FilterRange filterRanges[FILTER_COUNT] = {
    {0, 127, "All"},
    {12, 72, "WiFi"},
    {0, 83, "Bluetooth"},
    {0, 31, "Low"},
    {32, 63, "Mid-Low"},
    {64, 95, "Mid-High"},
    {96, 127, "High"}
};

uint8_t currentFilter = FILTER_ALL;

#define CE1  RADIO_CE_PIN_1
#define CSN1 RADIO_CSN_PIN_1
#define CE2  RADIO_CE_PIN_2
#define CSN2 RADIO_CSN_PIN_2
#define CE3  RADIO_CE_PIN_3
#define CSN3 RADIO_CSN_PIN_3


void writeRegister(uint8_t csn, uint8_t reg, uint8_t value) {
    digitalWrite(csn, LOW);
    SPI.transfer(reg | 0x20);
    SPI.transfer(value);
    digitalWrite(csn, HIGH);
}

uint8_t readRegister(uint8_t csn, uint8_t reg) {
    digitalWrite(csn, LOW);
    SPI.transfer(reg & 0x1F);
    uint8_t result = SPI.transfer(0x00);
    digitalWrite(csn, HIGH);
    return result;
}

void setChannel(uint8_t csn, uint8_t channel) {
    writeRegister(csn, NRF24_RF_CH, channel);
}

void powerUP(uint8_t csn) {
    uint8_t config = readRegister(csn, NRF24_CONFIG);
    writeRegister(csn, NRF24_CONFIG, config | 0x02);
    delayMicroseconds(130);
}

void powerDOWN(uint8_t csn) {
    uint8_t config = readRegister(csn, NRF24_CONFIG);
    writeRegister(csn, NRF24_CONFIG, config & ~0x02);
}

void startListening(uint8_t ce, uint8_t csn) {
    uint8_t config = readRegister(csn, NRF24_CONFIG);
    writeRegister(csn, NRF24_CONFIG, config | 0x01);
    digitalWrite(ce, HIGH);
}

void stopListening(uint8_t ce) {
    digitalWrite(ce, LOW);
}

bool carrierDetected(uint8_t csn) {
    return readRegister(csn, NRF24_RPD) & 0x01;
}

void renderSpectrum();

void analyzerSetup(){

    Serial.begin(115200);

    esp_bluedroid_status_t bt_state = esp_bluedroid_get_status();
    if (bt_state == ESP_BLUEDROID_STATUS_ENABLED) {
        esp_bluedroid_disable();
        delay(50);
    }
    if (bt_state != ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        esp_bluedroid_deinit();
        delay(50);
    }
    
    if (btStarted()) {
        btStop();
        delay(50);
    }

    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        esp_wifi_stop();
        delay(50);
        esp_wifi_deinit();
        delay(100);
    }

    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif != NULL) {
        esp_netif_destroy(sta_netif);
    }

    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif != NULL) {
        esp_netif_destroy(ap_netif);
    }

    pinMode(CE1, OUTPUT);
    pinMode(CSN1, OUTPUT);
    pinMode(CE2, OUTPUT);
    pinMode(CSN2, OUTPUT);
    pinMode(CE3, OUTPUT);
    pinMode(CSN3, OUTPUT);

    SPI.begin(18, 19, 23, 17);
    delay(100);
    SPI.setDataMode(SPI_MODE0);
    SPI.setFrequency(10000000);
    SPI.setBitOrder(MSBFIRST);

    digitalWrite(CSN1, HIGH);
    digitalWrite(CE1, LOW);
    digitalWrite(CSN2, HIGH);
    digitalWrite(CE2, LOW);
    digitalWrite(CSN3, HIGH);
    digitalWrite(CE3, LOW);

    powerUP(CSN1);
    writeRegister(CSN1, NRF24_EN_AA, 0x00);
    writeRegister(CSN1, NRF24_RF_SETUP, 0x0F);

    powerUP(CSN2);
    writeRegister(CSN2, NRF24_EN_AA, 0x00);
    writeRegister(CSN2, NRF24_RF_SETUP, 0x0F);

    powerUP(CSN3);
    writeRegister(CSN3, NRF24_EN_AA, 0x00);
    writeRegister(CSN3, NRF24_RF_SETUP, 0x0F);

}

void analyzerLoop(){

    static bool leftPressed = false;
    static bool rightPressed = false;
    static unsigned long lastButtonCheck = 0;
    static unsigned long lastDisplayUpdate = 0;
    static bool forceRedraw = false;

    unsigned long now = millis();
    if (now - lastButtonCheck >= 50) {
        bool leftNow = digitalRead(BUTTON_PIN_LEFT) == LOW;
        bool rightNow = digitalRead(BUTTON_PIN_RIGHT) == LOW;

        if (leftNow && !leftPressed) {
            currentFilter = (currentFilter == 0) ? (FILTER_COUNT - 1) : (currentFilter - 1);
            forceRedraw = true;
            delay(200);
        }

        if (rightNow && !rightPressed) {
            currentFilter = (currentFilter + 1) % FILTER_COUNT;
            forceRedraw = true;
            delay(200);
        }

        leftPressed = leftNow;
        rightPressed = rightNow;
        lastButtonCheck = now;
    }

    memset(spectrum, 0, sizeof(spectrum));

    const int sweeps = 30;
    const int channelStep = 3;
    const FilterRange &filter = filterRanges[currentFilter];

    for (int sweep = 0; sweep < sweeps; sweep++) {
        for (int ch = filter.start; ch <= filter.end; ch += channelStep) {
            if (ch > filter.end) break;

            setChannel(CSN1, ch);
            if (ch + 1 <= filter.end) setChannel(CSN2, ch + 1);
            if (ch + 2 <= filter.end) setChannel(CSN3, ch + 2);

            startListening(CE1, CSN1);
            if (ch + 1 <= filter.end) startListening(CE2, CSN2);
            if (ch + 2 <= filter.end) startListening(CE3, CSN3);

            delayMicroseconds(100);

            if (carrierDetected(CSN1)) spectrum[ch]++;
            if (ch + 1 <= filter.end && carrierDetected(CSN2)) spectrum[ch + 1]++;
            if (ch + 2 <= filter.end && carrierDetected(CSN3)) spectrum[ch + 2]++;

            stopListening(CE1);
            if (ch + 1 <= filter.end) stopListening(CE2);
            if (ch + 2 <= filter.end) stopListening(CE3);
        }

        if (sweep % 5 == 0) {
            now = millis();
            if (now - lastButtonCheck >= 50) {
                bool leftNow = digitalRead(BUTTON_PIN_LEFT) == LOW;
                bool rightNow = digitalRead(BUTTON_PIN_RIGHT) == LOW;

                if (leftNow && !leftPressed) {
                    currentFilter = (currentFilter == 0) ? (FILTER_COUNT - 1) : (currentFilter - 1);
                    forceRedraw = true;
                    delay(200);
                    break;
                }

                if (rightNow && !rightPressed) {
                    currentFilter = (currentFilter + 1) % FILTER_COUNT;
                    forceRedraw = true;
                    delay(200);
                    break;
                }

                leftPressed = leftNow;
                rightPressed = rightNow;
                lastButtonCheck = now;
            }
        }
    }

    peakSignal = 0;
    peakChannel = 0;
    uint16_t signalSum = 0;
    for (int i = 0; i < CHANNELS; i++) {
        uint8_t val = spectrum[i];
        signalSum += val;
        if (val > peakSignal) {
            peakSignal = val;
            peakChannel = i;
        }
    }
    avgSignal = signalSum / CHANNELS;

    now = millis();
    if (forceRedraw || (now - lastDisplayUpdate >= 500)) {
        renderSpectrum();
        lastDisplayUpdate = now;
        forceRedraw = false;
    }
}

void renderSpectrum() {
    u8g2.clearBuffer();

    static const int SPECTRUM_TOP = 18;
    static const int SPECTRUM_BOTTOM = 55;
    static const int SPECTRUM_HEIGHT = SPECTRUM_BOTTOM - SPECTRUM_TOP;

    const uint8_t scaleMax = (peakSignal < 10) ? 10 : peakSignal;
    const FilterRange &filter = filterRanges[currentFilter];
    const int rangeWidth = filter.end - filter.start + 1;

    for (int ch = filter.start; ch <= filter.end; ch++) {
        uint8_t val = spectrum[ch];
        if (val > 0) {
            int barHeight = (val * SPECTRUM_HEIGHT) / scaleMax;
            if (barHeight > SPECTRUM_HEIGHT) barHeight = SPECTRUM_HEIGHT;
            if (barHeight < 1) barHeight = 1;

            int xPos = ((ch - filter.start) * 128) / rangeWidth;
            u8g2.drawVLine(xPos, SPECTRUM_BOTTOM - barHeight, barHeight);
        }
    }

    u8g2.setFont(u8g2_font_5x7_tr);

    char filterDisplay[20];
    snprintf(filterDisplay, sizeof(filterDisplay), "<%s>", filter.name);
    u8g2.drawStr(0, 6, filterDisplay);

    char rangeStr[16];
    snprintf(rangeStr, sizeof(rangeStr), "%d-%d", 2400 + filter.start, 2400 + filter.end);
    int rangeWidth_px = strlen(rangeStr) * 5;
    u8g2.drawStr(128 - rangeWidth_px, 6, rangeStr);

    u8g2.setCursor(0, 13);
    if (peakSignal > 0) {
        u8g2.print(2400 + peakChannel);
    } else {
        u8g2.print("----");
    }

    u8g2.setCursor(46, 13);
    u8g2.print("L:");
    u8g2.print(peakSignal);

    const char* strength = (peakSignal > 30) ? "HI" : (peakSignal > 10) ? "MD" : "LO";
    u8g2.drawStr(110, 13, strength);

    u8g2.drawHLine(0, 15, 128);

    u8g2.drawHLine(0, SPECTRUM_BOTTOM, 128);

    char startStr[6], centerStr[6], endStr[6];
    int centerFreq = 2400 + ((filter.start + filter.end) / 2);

    snprintf(startStr, sizeof(startStr), "%d", 2400 + filter.start);
    snprintf(centerStr, sizeof(centerStr), "%d", centerFreq);
    snprintf(endStr, sizeof(endStr), "%d", 2400 + filter.end);

    u8g2.drawStr(0, 63, startStr);

    int centerWidth = strlen(centerStr) * 5;
    u8g2.drawStr((128 - centerWidth) / 2, 63, centerStr);

    int endWidth = strlen(endStr) * 5;
    u8g2.drawStr(128 - endWidth, 63, endStr);

    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}