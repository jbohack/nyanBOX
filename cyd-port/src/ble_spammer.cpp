/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/pindefs.h"
#include "ble_spammer.h"
#include "../include/radio_manager.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include <U8g2lib.h>
#include <Arduino.h>
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#include <string.h>
#include <esp_system.h>
    
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

enum BleSpamMode { BLE_SPAM_MENU, BLE_SPAM_RANDOM, BLE_SPAM_EMOJI, BLE_SPAM_CUSTOM, BLE_SPAM_ALL };
static BleSpamMode bleSpamMode = BLE_SPAM_MENU;
static int menuSelection = 0;
static unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 200;
static bool bleInitialized = false;
static bool isCurrentlyAdvertising = false;

static bool needsRedraw = true;
static unsigned long lastActiveUpdate = 0;
const unsigned long activeUpdateInterval = 1000;

// BLE advertising parameters (connectable, but connections are rejected in esp_ble_gap_register_callback)
static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_RANDOM,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY
};
    
// NyanBOX custom names
static const char* customNames[] = {
    "zr_crackin was here",
    "jbohack was here",
    "nyandevices.com",
    "Sub2TalkingSasquach",
    "nyanBOX",
    "Crypto Wallet",
    "Toaster",
    "ATM Machine",
    "OnlyFans Portal",
    "FeetFinder Portal",
    "Garbage Can",
    "FBI Surveillance Van",
    "Toilet",
    "Listening Device",
    "Bathroom Camera",
    "Rickroll",
    "Ejection Seat",
    "Dark Web Access Point",
    "Time Machine",
    "👉👌",
    "hi ;)"
};
static const uint8_t customNamesCount = sizeof(customNames) / sizeof(customNames[0]);
    
static const uint32_t emojiRanges[][2] = {
    { 0x1F600, 0x1F64F },
    { 0x1F300, 0x1F5FF },
    { 0x1F680, 0x1F6FF },
    { 0x2600, 0x26FF },
    { 0x2700, 0x27BF },
    { 0x1F1E6, 0x1F1FF }
};
static const uint8_t emojiRangeCount = sizeof(emojiRanges) / sizeof(emojiRanges[0]);
    
static const uint8_t minNameLen = 3;
static const uint8_t maxNameLen = 10;
static const uint16_t nameBufSize = maxNameLen * 4 + 1;
    
static uint8_t utf8_encode(uint32_t cp, char *out) {
    if (cp <= 0x7F) {
        out[0] = cp; return 1;
    } else if (cp <= 0x7FF) {
        out[0] = 0xC0 | ((cp >> 6) & 0x1F);
        out[1] = 0x80 | (cp & 0x3F);
        return 2;
    } else if (cp <= 0xFFFF) {
        out[0] = 0xE0 | ((cp >> 12) & 0x0F);
        out[1] = 0x80 | ((cp >> 6) & 0x3F);
        out[2] = 0x80 | (cp & 0x3F);
        return 3;
    } else if (cp <= 0x10FFFF) {
        out[0] = 0xF0 | ((cp >> 18) & 0x07);
        out[1] = 0x80 | ((cp >> 12) & 0x3F);
        out[2] = 0x80 | ((cp >> 6) & 0x3F);
        out[3] = 0x80 | (cp & 0x3F);
        return 4;
    }
    return 0;
}
    
static void generateRandomAlphaName(char* buf, uint8_t length) {
    for (uint8_t i = 0; i < length; i++) {
        buf[i] = 'A' + random(26);
    }
    buf[length] = '\0';
}

static void generateRandomEmojiName(char* buf) {
    uint8_t count = random(minNameLen, maxNameLen + 1);
    uint16_t pos = 0;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t ri = random(emojiRangeCount);
        uint32_t start = emojiRanges[ri][0];
        uint32_t end = emojiRanges[ri][1];
        uint32_t cp = random(start, end + 1);
        char utf8[4];
        uint8_t len = utf8_encode(cp, utf8);
        if (pos + len < nameBufSize) {
            memcpy(&buf[pos], utf8, len);
            pos += len;
        }
    }
    buf[pos] = '\0';
}

static void generateRandomMixedName(char* buf) {
    uint8_t glyphs = random(minNameLen, maxNameLen + 1);
    uint16_t pos = 0;
    for (uint8_t i = 0; i < glyphs; i++) {
        if (random(2) == 0) {
            if (pos + 1 < nameBufSize) {
                buf[pos++] = 'A' + random(26);
            }
        } else {
            uint8_t ri = random(emojiRangeCount);
            uint32_t start = emojiRanges[ri][0];
            uint32_t end = emojiRanges[ri][1];
            uint32_t cp = random(start, end + 1);
            char utf8[4];
            uint8_t len = utf8_encode(cp, utf8);
            if (pos + len < nameBufSize) {
                memcpy(&buf[pos], utf8, len);
                pos += len;
            }
        }
    }
    buf[pos] = '\0';
}
    
static const char* pickName(char* buf, uint8_t nameMode) {
    if (nameMode == 0 && customNamesCount > 0) {
        return customNames[random(customNamesCount)];
    }
    if (nameMode == 1) {
        uint8_t len = random(minNameLen, maxNameLen + 1);
        generateRandomAlphaName(buf, len);
    } else {
        generateRandomEmojiName(buf);
    }
    return buf;
}

static void drawBleSpamMenu() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "BLE Spam Mode:");
    u8g2.drawStr(0, 22, menuSelection == 0 ? "> Random" : "  Random");
    u8g2.drawStr(0, 32, menuSelection == 1 ? "> Emoji" : "  Emoji");
    u8g2.drawStr(0, 42, menuSelection == 2 ? "> Custom" : "  Custom");
    u8g2.drawStr(0, 52, menuSelection == 3 ? "> All" : "  All");
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(0, 62, "U/D=Move R=Start SEL=Exit");
    u8g2.sendBuffer();
  displayMirrorSend(u8g2);
}

static void drawActiveSpam(const char* modeName, const char* extraInfo = nullptr) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 12, modeName);
    if (extraInfo) {
        u8g2.drawStr(0, 28, extraInfo);
        u8g2.drawStr(0, 44, "Status: Active");
    } else {
        u8g2.drawStr(0, 28, "Status: Active");
    }
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(0, 62, "L=Back SEL=Exit");
    u8g2.sendBuffer();
  displayMirrorSend(u8g2);
}
    
// Packet structure for each advertisement
typedef uint8_t* PacketPtr;
static void make_packet(const char* name, uint8_t* size, PacketPtr* packet) {
    uint8_t name_len = strlen(name);
    uint8_t total = 12 + name_len;
    *packet = (uint8_t*)malloc(total);
    uint8_t i = 0;
    // Flags
    (*packet)[i++] = 2;
    (*packet)[i++] = 0x01;
    (*packet)[i++] = 0x06;
    // Complete local name
    (*packet)[i++] = name_len + 1;
    (*packet)[i++] = 0x09;
    memcpy(&(*packet)[i], name, name_len);
    i += name_len;
    // Service UUID list (HID)
    (*packet)[i++] = 3;
    (*packet)[i++] = 0x02;
    (*packet)[i++] = 0x12;
    (*packet)[i++] = 0x18;
    // TX power level
    (*packet)[i++] = 2;
    (*packet)[i++] = 0x0A;
    (*packet)[i++] = 0x00;
    *size = total;
}
    
static void advertiseDevice(const char* chosenName) {
    static unsigned long lastAdv = 0;
    unsigned long now = millis();

    if (now - lastAdv < 15) {
        delay(15 - (now - lastAdv));
    }

    if (isCurrentlyAdvertising) {
        esp_ble_gap_stop_advertising();
        delay(5);
        isCurrentlyAdvertising = false;
    }

    esp_bd_addr_t randAddr;
    for (int i = 0; i < 6; i++) randAddr[i] = random(0,256);
    randAddr[0] = (randAddr[0] & 0x3F) | 0xC0;
    esp_ble_gap_set_rand_addr(randAddr);

    uint8_t size;
    PacketPtr packet;
    make_packet(chosenName, &size, &packet);
    if (packet != NULL) {
        esp_ble_gap_config_adv_data_raw(packet, size);
        free(packet);
    }
    
    delay(5);
    esp_ble_gap_start_advertising(&adv_params);
    isCurrentlyAdvertising = true;
    lastAdv = millis();
}
    
void bleSpamSetup() {
    randomSeed((uint32_t)esp_random());
    pinMode(BUTTON_PIN_UP, INPUT_PULLUP);
    pinMode(BUTTON_PIN_DOWN, INPUT_PULLUP);
    pinMode(BUTTON_PIN_RIGHT, INPUT_PULLUP);
    pinMode(BUTTON_PIN_LEFT, INPUT_PULLUP);

    initBLE();

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);

    // Callback registration to handle incoming connections (they are ignored)
    esp_ble_gap_register_callback([](esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param){});

    bleInitialized = true;
    isCurrentlyAdvertising = false;
    delay(100);

    bleSpamMode = BLE_SPAM_MENU;
    menuSelection = 0;
    needsRedraw = true;
    lastActiveUpdate = 0;
    drawBleSpamMenu();
}
    
void bleSpamLoop() {
    unsigned long now = millis();
    static uint8_t nextIdx = 0;
    static BleSpamMode previousMode = BLE_SPAM_MENU;
    const uint8_t batchSize = 5;
    char nameBuf[nameBufSize];

    bool up = digitalRead(BUTTON_PIN_UP) == LOW;
    bool down = digitalRead(BUTTON_PIN_DOWN) == LOW;
    bool left = digitalRead(BUTTON_PIN_LEFT) == LOW;
    bool right = digitalRead(BUTTON_PIN_RIGHT) == LOW;

    if (bleSpamMode != previousMode) {
        needsRedraw = true;
        previousMode = bleSpamMode;
        lastActiveUpdate = now;
    }

    if (bleSpamMode != BLE_SPAM_MENU && now - lastActiveUpdate >= activeUpdateInterval) {
        lastActiveUpdate = now;
        needsRedraw = true;
    }

    switch (bleSpamMode) {
        case BLE_SPAM_MENU:
            if (now - lastButtonPress > debounceDelay) {
                if (up) {
                    menuSelection = (menuSelection - 1 + 4) % 4;
                    needsRedraw = true;
                    lastButtonPress = now;
                } else if (down) {
                    menuSelection = (menuSelection + 1) % 4;
                    needsRedraw = true;
                    lastButtonPress = now;
                } else if (right) {
                    if (menuSelection == 0) {
                        bleSpamMode = BLE_SPAM_RANDOM;
                    } else if (menuSelection == 1) {
                        bleSpamMode = BLE_SPAM_EMOJI;
                    } else if (menuSelection == 2) {
                        bleSpamMode = BLE_SPAM_CUSTOM;
                    } else {
                        bleSpamMode = BLE_SPAM_ALL;
                    }
                    nextIdx = 0;
                    needsRedraw = true;
                    lastButtonPress = now;
                }
            }

            if (needsRedraw) {
                drawBleSpamMenu();
                needsRedraw = false;
            }
            break;

        case BLE_SPAM_RANDOM:
            if (needsRedraw) {
                drawActiveSpam("Random Spam");
                needsRedraw = false;
            }

            for (uint8_t i = 0; i < batchSize; i++) {
                const char* name = pickName(nameBuf, 1);
                advertiseDevice(name);
            }

            if (left && now - lastButtonPress > debounceDelay) {
                if (isCurrentlyAdvertising) {
                    esp_ble_gap_stop_advertising();
                    isCurrentlyAdvertising = false;
                    delay(50);
                }
                bleSpamMode = BLE_SPAM_MENU;
                needsRedraw = true;
                lastButtonPress = now;
            }
            break;

        case BLE_SPAM_EMOJI:
            if (needsRedraw) {
                drawActiveSpam("Emoji Spam");
                needsRedraw = false;
            }

            for (uint8_t i = 0; i < batchSize; i++) {
                const char* name = pickName(nameBuf, 2);
                advertiseDevice(name);
            }

            if (left && now - lastButtonPress > debounceDelay) {
                if (isCurrentlyAdvertising) {
                    esp_ble_gap_stop_advertising();
                    isCurrentlyAdvertising = false;
                    delay(50);
                }
                bleSpamMode = BLE_SPAM_MENU;
                needsRedraw = true;
                lastButtonPress = now;
            }
            break;

        case BLE_SPAM_CUSTOM:
            if (needsRedraw) {
                char buf[32];
                snprintf(buf, sizeof(buf), "Index Count: %d", customNamesCount);
                drawActiveSpam("Custom Spam", buf);
                needsRedraw = false;
            }

            if (customNamesCount > 0) {
                for (uint8_t i = 0; i < batchSize; i++) {
                    const char* name = customNames[nextIdx];
                    nextIdx = (nextIdx + 1) % customNamesCount;
                    advertiseDevice(name);
                }
            }

            if (left && now - lastButtonPress > debounceDelay) {
                if (isCurrentlyAdvertising) {
                    esp_ble_gap_stop_advertising();
                    isCurrentlyAdvertising = false;
                    delay(50);
                }
                bleSpamMode = BLE_SPAM_MENU;
                nextIdx = 0;
                needsRedraw = true;
                lastButtonPress = now;
            }
            break;

        case BLE_SPAM_ALL:
            if (needsRedraw) {
                drawActiveSpam("All Spam");
                needsRedraw = false;
            }

            for (uint8_t i = 0; i < batchSize; i++) {
                uint8_t useMode = i % 3;
                const char* name;
                if (useMode == 0 && customNamesCount > 0) {
                    name = customNames[nextIdx];
                    nextIdx = (nextIdx + 1) % customNamesCount;
                } else {
                    name = pickName(nameBuf, useMode);
                }
                advertiseDevice(name);
            }

            if (left && now - lastButtonPress > debounceDelay) {
                if (isCurrentlyAdvertising) {
                    esp_ble_gap_stop_advertising();
                    isCurrentlyAdvertising = false;
                    delay(50);
                }
                bleSpamMode = BLE_SPAM_MENU;
                nextIdx = 0;
                needsRedraw = true;
                lastButtonPress = now;
            }
            break;
    }
}