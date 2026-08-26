/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/channel_analyzer.h"
#include "../include/radio_manager.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include <map>
#include <string>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

namespace {

#define MAX_CHANNELS 14
#define MAX_NETWORKS 100
#define NETWORK_TIMEOUT 60000

struct ChannelInfo {
    std::map<std::string, unsigned long> uniqueNetworks;
};

static ChannelInfo channels[MAX_CHANNELS];
static unsigned long lastScanStart = 0;
static unsigned long lastDisplayUpdate = 0;
static unsigned long lastCleanup = 0;
const unsigned long displayUpdateInterval = 1000;
const unsigned long scanInterval = 100;
const unsigned long scanDuration = 4000;
const unsigned long cleanupInterval = 60000;

static bool scanInProgress = false;
static bool hasData = false;

void initChannelData() {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        channels[i].uniqueNetworks.clear();
    }
}

void cleanupOldNetworks() {
    unsigned long now = millis();

    for (int i = 0; i < MAX_CHANNELS; i++) {
        auto it = channels[i].uniqueNetworks.begin();
        while (it != channels[i].uniqueNetworks.end()) {
            if (now - it->second > NETWORK_TIMEOUT) {
                it = channels[i].uniqueNetworks.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void updateChannelData() {
    uint16_t number = 0;
    esp_wifi_scan_get_ap_num(&number);

    if (number == 0) return;

    wifi_ap_record_t *ap_info = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * number);
    if (ap_info == NULL) return;

    memset(ap_info, 0, sizeof(wifi_ap_record_t) * number);

    uint16_t actual_number = number;
    esp_err_t err = esp_wifi_scan_get_ap_records(&actual_number, ap_info);

    if (err == ESP_OK) {
        for (int i = 0; i < actual_number; i++) {
            int channel = ap_info[i].primary;

            if (channel >= 1 && channel <= 14) {
                int idx = channel - 1;

                char macStr[18];
                snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
                    ap_info[i].bssid[0], ap_info[i].bssid[1], ap_info[i].bssid[2],
                    ap_info[i].bssid[3], ap_info[i].bssid[4], ap_info[i].bssid[5]);

                int totalNetworks = 0;
                for (int j = 0; j < MAX_CHANNELS; j++) {
                    totalNetworks += channels[j].uniqueNetworks.size();
                }

                std::string macString(macStr);
                if (totalNetworks < MAX_NETWORKS || channels[idx].uniqueNetworks.count(macString) > 0) {
                    channels[idx].uniqueNetworks[macString] = millis();
                }
            }
        }
        hasData = true;
    }

    free(ap_info);
}

void performChannelScan() {
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
    scanInProgress = true;
    lastScanStart = millis();
}

void renderScanningScreen() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);

    const char* titleText = "Channel Analyzer";
    int titleWidth = u8g2.getUTF8Width(titleText);
    u8g2.drawStr((128 - titleWidth) / 2, 20, titleText);

    const char* scanText = "Scanning...";
    int scanWidth = u8g2.getUTF8Width(scanText);
    u8g2.drawStr((128 - scanWidth) / 2, 32, scanText);

    int barWidth = 120;
    int barHeight = 10;
    int barX = (128 - barWidth) / 2;
    int barY = 38;

    u8g2.drawFrame(barX, barY, barWidth, barHeight);

    unsigned long now = millis();
    unsigned long elapsed = now - lastScanStart;
    if (elapsed < scanDuration) {
        int fillWidth = (elapsed * (barWidth - 4)) / scanDuration;
        if (fillWidth > 0 && fillWidth < barWidth - 4) {
            u8g2.drawBox(barX + 2, barY + 2, fillWidth, barHeight - 4);
        }
    }

    u8g2.setFont(u8g2_font_5x8_tr);
    const char* exitText = "Press SEL to exit";
    int exitWidth = u8g2.getUTF8Width(exitText);
    u8g2.drawStr((128 - exitWidth) / 2, 62, exitText);

    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

void renderChannelChart() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);

    int totalNetworks = 0;
    int bestChannel = 1;
    int worstChannel = 1;
    int minCount = 999;
    int maxCount = 0;

    for (int i = 0; i < MAX_CHANNELS; i++) {
        int count = channels[i].uniqueNetworks.size();
        totalNetworks += count;

        if (count > maxCount) {
            maxCount = count;
            worstChannel = i + 1;
        }
    }

    for (int ch : {1, 6, 11}) {
        int idx = ch - 1;
        int count = channels[idx].uniqueNetworks.size();
        if (count < minCount) {
            minCount = count;
            bestChannel = ch;
        }
    }

    int bestCount = channels[bestChannel - 1].uniqueNetworks.size();
    int worstCount = channels[worstChannel - 1].uniqueNetworks.size();

    char headerStr[32];
    snprintf(headerStr, sizeof(headerStr), "B:%d(%d) W:%d(%d) T:%d",
             bestChannel, bestCount, worstChannel, worstCount, totalNetworks);
    int headerWidth = u8g2.getUTF8Width(headerStr);
    u8g2.drawStr((128 - headerWidth) / 2, 7, headerStr);

    u8g2.drawHLine(0, 8, 128);

    const int CHART_TOP = 10;
    const int CHART_BOTTOM = 56;
    const int CHART_HEIGHT = CHART_BOTTOM - CHART_TOP;
    const int barWidth = 7;
    const int barSpacing = 2;
    const int scaleMax = (maxCount < 3) ? 3 : maxCount;

    for (int ch = 0; ch < MAX_CHANNELS; ch++) {
        int count = channels[ch].uniqueNetworks.size();
        int xPos = 4 + (ch * (barWidth + barSpacing));

        if (count > 0) {
            int barHeight = (count * CHART_HEIGHT) / scaleMax;
            if (barHeight > CHART_HEIGHT) barHeight = CHART_HEIGHT;
            if (barHeight < 2) barHeight = 2;

            u8g2.drawBox(xPos, CHART_BOTTOM - barHeight, barWidth, barHeight);
        }
    }

    u8g2.drawHLine(0, CHART_BOTTOM, 128);

    u8g2.setFont(u8g2_font_4x6_tr);
    for (int ch = 0; ch < MAX_CHANNELS; ch += 2) {
        int xPos = 4 + (ch * (barWidth + barSpacing));
        char chLabel[3];
        snprintf(chLabel, sizeof(chLabel), "%d", ch + 1);
        u8g2.drawStr(xPos, 63, chLabel);
    }

    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

}

void channelAnalyzerSetup() {
    initWiFi(WIFI_MODE_STA);

    scanInProgress = false;
    hasData = false;
    initChannelData();

    u8g2.begin();
    renderScanningScreen();

    performChannelScan();
    lastDisplayUpdate = millis();
    lastCleanup = millis();
}

void channelAnalyzerLoop() {
    unsigned long now = millis();

    if (scanInProgress && (now - lastScanStart > scanDuration)) {
        esp_wifi_scan_stop();
        updateChannelData();
        scanInProgress = false;
    }

    if (!scanInProgress && (now - lastScanStart >= scanInterval)) {
        performChannelScan();
    }

    if (now - lastCleanup >= cleanupInterval) {
        cleanupOldNetworks();
        lastCleanup = now;
    }

    if (hasData) {
        if (now - lastDisplayUpdate >= displayUpdateInterval) {
            renderChannelChart();
            lastDisplayUpdate = now;
        }
    } else {
        renderScanningScreen();
    }
}