/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/channel_analyzer.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "esp_wifi.h"
#include "esp_event.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

#define BTN_UP BUTTON_PIN_UP
#define BTN_DOWN BUTTON_PIN_DOWN
#define BTN_RIGHT BUTTON_PIN_RIGHT
#define BTN_BACK BUTTON_PIN_LEFT

#define MAX_CHANNELS 14
#define SCAN_INTERVAL 60000
#define NON_OVERLAP_CHANNELS 3

const int nonOverlapChannels[] = {0, 5, 10};

struct ChannelInfo {
    int networkCount;
    int maxRSSI;
};

static ChannelInfo channels[MAX_CHANNELS];
static unsigned long lastScanTime = 0;
static unsigned long scanStartTime = 0;
static unsigned long lastDisplayUpdate = 0;
static unsigned long lastButtonPress = 0;
const unsigned long debounceTime = 200;
const unsigned long displayUpdateInterval = 100;
const unsigned long scanDuration = 8000;

static int currentView = 0;
static bool scanInProgress = false;
static uint16_t lastApCount = 0;
static int scannedNetworks = 0;

static bool needsRedraw = true;
static int lastView = -1;
static int lastScannedNetworks = 0;

const char* getSignalStrengthLabel(int rssi) {
    if (rssi > -50) return "Strong";
    if (rssi > -70) return "Medium";
    return "Weak";
}

void initChannelData() {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        channels[i].networkCount = 0;
        channels[i].maxRSSI = -100;
    }
    scannedNetworks = 0;
}

void processChannelScanResults(unsigned long now) {
    uint16_t number = 0;
    esp_wifi_scan_get_ap_num(&number);
    
    if (number == 0) return;
    
    wifi_ap_record_t *ap_info = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * number);
    
    if (ap_info == NULL) return;
    
    memset(ap_info, 0, sizeof(wifi_ap_record_t) * number);
    
    uint16_t actual_number = number;
    esp_err_t err = esp_wifi_scan_get_ap_records(&actual_number, ap_info);
    
    if (err == ESP_OK) {
        scannedNetworks = actual_number;
        
        for (int i = 0; i < actual_number; i++) {
            int channel = ap_info[i].primary;
            int rssi = ap_info[i].rssi;
            
            if (channel >= 1 && channel <= 14) {
                int idx = channel - 1;
                channels[idx].networkCount++;
                
                if (rssi > channels[idx].maxRSSI) {
                    channels[idx].maxRSSI = rssi;
                }
            }
        }
    }
    
    free(ap_info);
}

void performChannelScan() {
    if (scanInProgress) return;

    scanInProgress = true;
    initChannelData();

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
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
    lastApCount = 0;
}

void drawNetworkCountView() {
    u8g2.setFont(u8g2_font_helvB08_tr);
    const char* title = "Channel Activity";
    int titleWidth = u8g2.getUTF8Width(title);
    u8g2.drawStr((128 - titleWidth) / 2, 12, title);
    
    u8g2.drawHLine(16, 16, 96);
    
    int totalNetworks = 0;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        totalNetworks += channels[i].networkCount;
    }
    
    u8g2.setFont(u8g2_font_helvR08_tr);
    int y = 26;
    int shown = 0;
    
    for (int i = 0; i < NON_OVERLAP_CHANNELS; i++) {
        int chIdx = nonOverlapChannels[i];
        char line[16];
        snprintf(line, sizeof(line), "Ch %d: %d", chIdx + 1, channels[chIdx].networkCount);
        int lineWidth = u8g2.getUTF8Width(line);
        u8g2.drawStr((128 - lineWidth) / 2, y, line);
        y += 12;
        shown++;
    }
    
    if (shown == 0) {
        const char* noData = "No activity";
        int noDataWidth = u8g2.getUTF8Width(noData);
        u8g2.drawStr((128 - noDataWidth) / 2, 32, noData);
    }
    
    u8g2.setFont(u8g2_font_4x6_tr);
    const char* instructions = "U/D=Move R=SCN SEL=EXIT";
    int instWidth = u8g2.getUTF8Width(instructions);
    u8g2.drawStr((128 - instWidth) / 2, 62, instructions);
}

void drawSignalStrengthView() {
    u8g2.setFont(u8g2_font_helvB08_tr);
    const char* title = "Signal Strength";
    int titleWidth = u8g2.getUTF8Width(title);
    u8g2.drawStr((128 - titleWidth) / 2, 12, title);
    
    u8g2.drawHLine(16, 16, 96);
    
    u8g2.setFont(u8g2_font_helvR08_tr);
    int y = 28;
    int shown = 0;
    
    for (int i = 0; i < NON_OVERLAP_CHANNELS; i++) {
        int chIdx = nonOverlapChannels[i];
        char line[16];
        
        if (channels[chIdx].networkCount > 0) {
            const char* strength = getSignalStrengthLabel(channels[chIdx].maxRSSI);
            snprintf(line, sizeof(line), "Ch %d: %s", chIdx + 1, strength);
        } else {
            snprintf(line, sizeof(line), "Ch %d: Clear", chIdx + 1);
        }
        
        int lineWidth = u8g2.getUTF8Width(line);
        u8g2.drawStr((128 - lineWidth) / 2, y, line);
        y += 10;
        shown++;
    }
    
    if (shown == 0) {
        const char* noData = "No signals";
        int noDataWidth = u8g2.getUTF8Width(noData);
        u8g2.drawStr((128 - noDataWidth) / 2, 33, noData);
    }
    
    u8g2.setFont(u8g2_font_4x6_tr);
    const char* instructions = "U/D=Move R=SCN SEL=EXIT";
    int instWidth = u8g2.getUTF8Width(instructions);
    u8g2.drawStr((128 - instWidth) / 2, 62, instructions);
}

void drawSummaryView() {
    u8g2.setFont(u8g2_font_helvB08_tr);
    const char* title = "Overview";
    int titleWidth = u8g2.getUTF8Width(title);
    u8g2.drawStr((128 - titleWidth) / 2, 12, title);
    
    u8g2.drawHLine(16, 16, 96);
    
    int busiestChannel = 1;
    int maxNetworks = 0;
    int totalNetworks = 0;
    int quietestChannel = 1;
    int minNetworks = 999;
    
    for (int i = 0; i < MAX_CHANNELS; i++) {
        totalNetworks += channels[i].networkCount;
    }
    
    for (int i = 0; i < NON_OVERLAP_CHANNELS; i++) {
        int chIdx = nonOverlapChannels[i];
        if (channels[chIdx].networkCount > maxNetworks) {
            maxNetworks = channels[chIdx].networkCount;
            busiestChannel = chIdx + 1;
        }
        if (channels[chIdx].networkCount < minNetworks) {
            minNetworks = channels[chIdx].networkCount;
            quietestChannel = chIdx + 1;
        }
    }
    
    u8g2.setFont(u8g2_font_helvR08_tr);
    
    char totalStr[18];
    snprintf(totalStr, sizeof(totalStr), "%d networks", totalNetworks);
    int totalWidth = u8g2.getUTF8Width(totalStr);
    u8g2.drawStr((128 - totalWidth) / 2, 26, totalStr);
    
    if (maxNetworks > 0) {
        char busiestStr[18];
        snprintf(busiestStr, sizeof(busiestStr), "Busiest: Ch %d", busiestChannel);
        int busiestWidth = u8g2.getUTF8Width(busiestStr);
        u8g2.drawStr((128 - busiestWidth) / 2, 38, busiestStr);
        
        char quietestStr[18];
        snprintf(quietestStr, sizeof(quietestStr), "Quietest: Ch %d", quietestChannel);
        int quietestWidth = u8g2.getUTF8Width(quietestStr);
        u8g2.drawStr((128 - quietestWidth) / 2, 50, quietestStr);
    }
    
    u8g2.setFont(u8g2_font_4x6_tr);
    const char* instructions = "U/D=Move R=SCN SEL=EXIT";
    int instWidth = u8g2.getUTF8Width(instructions);
    u8g2.drawStr((128 - instWidth) / 2, 62, instructions);
}

void channelAnalyzerSetup() {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(BTN_BACK, INPUT_PULLUP);

    currentView = 0;
    lastScanTime = 0;
    lastButtonPress = 0;
    scanInProgress = false;
    initChannelData();
    needsRedraw = true;
    lastView = -1;
    lastScannedNetworks = 0;

    u8g2.begin();
    u8g2.setFont(u8g2_font_helvR08_tr);
    u8g2.clearBuffer();
    const char* scanText = "Scanning Networks...";
    int scanWidth = u8g2.getUTF8Width(scanText);
    u8g2.drawStr((128 - scanWidth) / 2, 32, scanText);
    u8g2.sendBuffer();
  displayMirrorSend(u8g2);

    performChannelScan();
    lastScanTime = millis();
}

void channelAnalyzerLoop() {
    unsigned long now = millis();

    if (scanInProgress) {
        uint16_t currentApCount = 0;
        esp_wifi_scan_get_ap_num(&currentApCount);
        scannedNetworks = currentApCount;

        if (scannedNetworks != lastScannedNetworks) {
            lastScannedNetworks = scannedNetworks;
            needsRedraw = true;
        }

        if (now - lastDisplayUpdate > displayUpdateInterval) {
            needsRedraw = true;
        }

        if (needsRedraw) {
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_helvR08_tr);
            const char* scanText = "Scanning Networks...";
            int scanWidth = u8g2.getUTF8Width(scanText);
            u8g2.drawStr((128 - scanWidth) / 2, 10, scanText);

            char countStr[32];
            snprintf(countStr, sizeof(countStr), "Found: %d networks", scannedNetworks);
            int countWidth = u8g2.getUTF8Width(countStr);
            u8g2.drawStr((128 - countWidth) / 2, 25, countStr);

            int barWidth = 120;
            int barHeight = 10;
            int barX = 4;
            int barY = 35;
            u8g2.drawFrame(barX, barY, barWidth, barHeight);

            unsigned long elapsed = now - scanStartTime;
            int fillWidth = ((elapsed * (barWidth - 4)) / scanDuration);
            if (fillWidth > (barWidth - 4)) fillWidth = barWidth - 4;
            if (fillWidth > 0) {
                u8g2.drawBox(barX + 2, barY + 2, fillWidth, barHeight - 4);
            }

            u8g2.setFont(u8g2_font_5x8_tr);
            const char* cancelText = "Press SEL to exit";
            int cancelWidth = u8g2.getUTF8Width(cancelText);
            u8g2.drawStr((128 - cancelWidth) / 2, 60, cancelText);
            u8g2.sendBuffer();
            displayMirrorSend(u8g2);

            lastDisplayUpdate = now;
            needsRedraw = false;
        }

        if (now - scanStartTime > scanDuration) {
            processChannelScanResults(now);
            scanInProgress = false;
            lastScanTime = now;
            lastApCount = 0;
            esp_wifi_scan_stop();
            needsRedraw = true;
        }

        return;
    }

    if (now - lastScanTime >= SCAN_INTERVAL) {
        performChannelScan();
        needsRedraw = true;
        return;
    }

    if (now - lastButtonPress > debounceTime) {
        if (digitalRead(BTN_UP) == LOW) {
            currentView = (currentView - 1 + 3) % 3;
            lastButtonPress = now;
            needsRedraw = true;
        } else if (digitalRead(BTN_DOWN) == LOW) {
            currentView = (currentView + 1) % 3;
            lastButtonPress = now;
            needsRedraw = true;
        } else if (digitalRead(BTN_RIGHT) == LOW) {
            performChannelScan();
            lastButtonPress = now;
            needsRedraw = true;
            return;
        }
    }

    if (currentView != lastView) {
        lastView = currentView;
        needsRedraw = true;
    }

    if (needsRedraw) {
        u8g2.clearBuffer();

        if (currentView == 0) {
            drawNetworkCountView();
        } else if (currentView == 1) {
            drawSignalStrengthView();
        } else {
            drawSummaryView();
        }

        u8g2.sendBuffer();
        displayMirrorSend(u8g2);
        needsRedraw = false;
    }
}