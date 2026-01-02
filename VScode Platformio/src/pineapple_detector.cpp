/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/pineapple_detector.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "../include/setting.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include <vector>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

#define BTN_UP BUTTON_PIN_UP
#define BTN_DOWN BUTTON_PIN_DOWN
#define BTN_RIGHT BUTTON_PIN_RIGHT
#define BTN_BACK BUTTON_PIN_LEFT

struct PineappleDeviceData {
  char ssid[33];
  char bssid[18];
  int8_t rssi;
  uint8_t channel;
  unsigned long lastSeen;
  char authMode[20];
};

static std::vector<PineappleDeviceData> pineappleDevices;

const int MAX_DEVICES = 100;

static int currentIndex = 0;
static int listStartIndex = 0;
static bool isDetailView = false;
static bool isLocateMode = false;
static char locateTargetBSSID[18] = {0};
static uint8_t locateTargetChannel = 0;
static unsigned long lastButtonPress = 0;
const unsigned long debounceTime = 200;

static bool needsRedraw = true;
static int lastDeviceCount = 0;
static unsigned long lastLocateUpdate = 0;
const unsigned long locateUpdateInterval = 1000;
static unsigned long lastCountdownUpdate = 0;
const unsigned long countdownUpdateInterval = 1000;
static bool wasScanning = false;

static bool isScanning = false;
static unsigned long lastScanTime = 0;
const unsigned long scanInterval = 30000;
const unsigned long scanDuration = 8000;
static unsigned long scanStartTime = 0;

static bool wifiInitialized = false;
static bool scanCompleted = false;

static uint8_t current_channel = 1;
static unsigned long last_channel_hop = 0;
const unsigned long CHANNEL_HOP_INTERVAL = 500;
const uint8_t MAX_CHANNEL = 13;

static bool check_pineapple_oui(const char* bssid_str) {
    if (strlen(bssid_str) < 8) return false;
    return (strncasecmp(&bssid_str[3], "13", 2) == 0) &&
           (strncasecmp(&bssid_str[6], "37", 2) == 0);
}

static void hop_channel() {
    unsigned long now = millis();
    if (now - last_channel_hop > CHANNEL_HOP_INTERVAL) {
        current_channel++;
        if (current_channel > MAX_CHANNEL) {
            current_channel = 1;
        }
        esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);
        last_channel_hop = now;
    }
}

static const char* getSecurityFromBeacon(const uint8_t *frame, int len) {
    if (len < 36) return "Open";

    uint16_t capabilities = (frame[35] << 8) | frame[34];
    bool privacyEnabled = (capabilities & 0x0010) != 0;

    if (!privacyEnabled) return "Open";

    int offset = 36;
    bool hasRSN = false;
    bool hasWPA = false;

    while (offset + 2 <= len) {
        uint8_t tag = frame[offset];
        uint8_t tag_len = frame[offset + 1];

        if (offset + 2 + tag_len > len) break;

        if (tag == 48) {
            hasRSN = true;
            if (tag_len >= 8) {
                int akm_offset = offset + 2 + 6;
                if (akm_offset + 4 <= offset + 2 + tag_len) {
                    if (frame[akm_offset + 3] == 0x08) {
                        return "WPA3-PSK";
                    }
                }
            }
        }

        if (tag == 221 && tag_len >= 4) {
            if (frame[offset + 2] == 0x00 &&
                frame[offset + 3] == 0x50 &&
                frame[offset + 4] == 0xF2 &&
                frame[offset + 5] == 0x01) {
                hasWPA = true;
            }
        }

        offset += 2 + tag_len;
    }

    if (hasRSN && hasWPA) return "WPA/WPA2";
    if (hasRSN) return "WPA2-PSK";
    if (hasWPA) return "WPA-PSK";
    if (privacyEnabled) return "WEP";

    return "Open";
}

static void addOrUpdatePineappleDevice(const char* ssid, const char* bssid, int8_t rssi, uint8_t channel, const char* authMode) {
    if (isLocateMode && strlen(locateTargetBSSID) > 0) {
        if (strcmp(bssid, locateTargetBSSID) != 0) {
            return;
        }
    } else if (pineappleDevices.size() >= MAX_DEVICES) {
        return;
    }

    for (size_t i = 0; i < pineappleDevices.size(); i++) {
        if (strcmp(pineappleDevices[i].bssid, bssid) == 0) {
            pineappleDevices[i].rssi = rssi;
            pineappleDevices[i].lastSeen = millis();
            pineappleDevices[i].channel = channel;

            if (ssid && ssid[0] != '\0') {
                strncpy(pineappleDevices[i].ssid, ssid, 32);
                pineappleDevices[i].ssid[32] = '\0';
            }

            if (authMode && strlen(authMode) > 0) {
                strncpy(pineappleDevices[i].authMode, authMode, sizeof(pineappleDevices[i].authMode) - 1);
                pineappleDevices[i].authMode[sizeof(pineappleDevices[i].authMode) - 1] = '\0';
            }

            if (!isLocateMode) {
                std::sort(pineappleDevices.begin(), pineappleDevices.end(),
                          [](const PineappleDeviceData &a, const PineappleDeviceData &b) {
                            return a.rssi > b.rssi;
                          });
            }
            return;
        }
    }

    PineappleDeviceData newDev = {};
    if (ssid && ssid[0] != '\0') {
        strncpy(newDev.ssid, ssid, 32);
        newDev.ssid[32] = '\0';
    } else {
        newDev.ssid[0] = '\0';
    }
    strncpy(newDev.bssid, bssid, 17);
    newDev.bssid[17] = '\0';
    newDev.rssi = rssi;
    newDev.channel = channel;
    newDev.lastSeen = millis();

    if (authMode && strlen(authMode) > 0) {
        strncpy(newDev.authMode, authMode, sizeof(newDev.authMode) - 1);
        newDev.authMode[sizeof(newDev.authMode) - 1] = '\0';
    } else {
        strncpy(newDev.authMode, "Unknown", sizeof(newDev.authMode) - 1);
        newDev.authMode[sizeof(newDev.authMode) - 1] = '\0';
    }

    pineappleDevices.push_back(newDev);

    if (!isLocateMode) {
        std::sort(pineappleDevices.begin(), pineappleDevices.end(),
                  [](const PineappleDeviceData &a, const PineappleDeviceData &b) {
                    return a.rssi > b.rssi;
                  });
    }

    needsRedraw = true;
}

static void IRAM_ATTR wifi_sniffer_packet_handler(void* buff, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT)
        return;

    const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buff;
    const uint8_t *frame = ppkt->payload;
    int len = ppkt->rx_ctrl.sig_len;

    if (len <= 4)
        return;
    len -= 4;

    uint8_t frameType = frame[0];
    uint8_t frameSubtype = (frameType & 0xF0);

    if (frameSubtype != 0x80 && frameSubtype != 0x40 && frameSubtype != 0x50) {
        return;
    }

    char bssidStr[18];
    snprintf(bssidStr, sizeof(bssidStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             frame[10], frame[11], frame[12], frame[13], frame[14], frame[15]);

    if (!check_pineapple_oui(bssidStr)) {
        return;
    }

    char ssid[33] = {0};
    uint8_t channel = ppkt->rx_ctrl.channel;

    int offset = 24;
    if (frameSubtype == 0x80) {
        offset += 12;
    }

    while (offset + 2 <= len) {
        uint8_t tag = frame[offset];
        uint8_t tag_len = frame[offset + 1];

        if (offset + 2 + tag_len > len)
            break;

        if (tag == 0) {
            if (tag_len > 0 && tag_len <= 32) {
                memcpy(ssid, &frame[offset + 2], tag_len);
                ssid[tag_len] = '\0';
            }
        }

        if (tag == 3 && tag_len == 1) {
            channel = frame[offset + 2];
        }

        offset += 2 + tag_len;
    }

    const char* authMode = (frameSubtype == 0x80) ? getSecurityFromBeacon(frame, len) : "Unknown";

    addOrUpdatePineappleDevice(ssid, bssidStr, ppkt->rx_ctrl.rssi, channel, authMode);
}

void pineappleDetectorSetup() {
    pineappleDevices.clear();
    pineappleDevices.reserve(MAX_DEVICES);
    currentIndex = listStartIndex = 0;
    isDetailView = false;
    isLocateMode = false;
    memset(locateTargetBSSID, 0, sizeof(locateTargetBSSID));
    locateTargetChannel = 0;
    lastButtonPress = 0;
    isScanning = true;
    needsRedraw = true;
    lastDeviceCount = 0;
    lastLocateUpdate = 0;
    lastCountdownUpdate = 0;
    wasScanning = false;
    scanStartTime = 0;
    scanCompleted = false;
    wifiInitialized = false;

    u8g2.begin();
    u8g2.setFont(u8g2_font_6x10_tr);

    wifi_mode_t currentMode;
    if (esp_wifi_get_mode(&currentMode) == ESP_OK) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        wifiInitialized = true;
    } else {
        wifiInitialized = false;
    }

    if (!wifiInitialized) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
    }

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
    wifi_promiscuous_filter_t flt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
    esp_wifi_set_promiscuous_filter(&flt);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

    wifiInitialized = true;

    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(BTN_BACK, INPUT_PULLUP);

    scanStartTime = millis();
    lastScanTime = millis();
    current_channel = 1;
    last_channel_hop = millis();
}

void pineappleDetectorLoop() {
    unsigned long now = millis();

    unsigned long effectiveScanDuration = scanDuration;
    unsigned long effectiveScanInterval = scanInterval;

    if (pineappleDevices.empty() && isContinuousScanEnabled() && scanCompleted) {
        effectiveScanDuration = 3000;
        effectiveScanInterval = 500;
    }

    if (!scanCompleted || isLocateMode) {
        hop_channel();
    }

    bool shouldShowScanScreen = !scanCompleted || (pineappleDevices.empty() && isContinuousScanEnabled());

    if (shouldShowScanScreen && !isDetailView && !isLocateMode && !scanCompleted) {
        unsigned long elapsed = now - scanStartTime;

        if (elapsed >= effectiveScanDuration) {
            scanCompleted = true;
            lastScanTime = now;
            esp_wifi_set_promiscuous(false);
            needsRedraw = true;
        } else {
            if ((lastDeviceCount != (int)pineappleDevices.size() || wasScanning != isScanning) || (now - lastLocateUpdate >= 100)) {
                lastDeviceCount = (int)pineappleDevices.size();
                wasScanning = isScanning;
                lastLocateUpdate = now;

                u8g2.clearBuffer();
                u8g2.setFont(u8g2_font_6x10_tr);
                u8g2.drawStr(0, 10, "Pineapple Detector");

                char scanStr[32];
                snprintf(scanStr, sizeof(scanStr), "WiFi CH:%d", current_channel);
                u8g2.drawStr(0, 22, scanStr);

                char countStr[32];
                snprintf(countStr, sizeof(countStr), "Found: %d", (int)pineappleDevices.size());
                u8g2.drawStr(0, 34, countStr);

                int barWidth = 120;
                int barHeight = 10;
                int barX = (128 - barWidth) / 2;
                int barY = 38;

                u8g2.drawFrame(barX, barY, barWidth, barHeight);

                int fillWidth = (elapsed * (barWidth - 4)) / effectiveScanDuration;
                if (fillWidth > 0 && fillWidth < barWidth - 4) {
                    u8g2.drawBox(barX + 2, barY + 2, fillWidth, barHeight - 4);
                }

                u8g2.setFont(u8g2_font_5x8_tr);
                u8g2.drawStr(0, 62, "Press SEL to exit");

                u8g2.sendBuffer();
                displayMirrorSend(u8g2);
            }
        }
        return;
    }

    if (wasScanning != isScanning) {
        wasScanning = isScanning;
        needsRedraw = true;
    }

    if (scanCompleted && now - lastScanTime > effectiveScanInterval && !isDetailView && !isLocateMode) {
        if (pineappleDevices.size() >= MAX_DEVICES) {
            std::sort(pineappleDevices.begin(), pineappleDevices.end(),
                    [](const PineappleDeviceData &a, const PineappleDeviceData &b) {
                        if (a.lastSeen != b.lastSeen) {
                            return a.lastSeen < b.lastSeen;
                        }
                        return a.rssi < b.rssi;
                    });

            int devicesToRemove = MAX_DEVICES / 4;
            if (devicesToRemove > 0) {
                pineappleDevices.erase(pineappleDevices.begin(),
                                        pineappleDevices.begin() + devicesToRemove);
            }

            currentIndex = listStartIndex = 0;
        }

        esp_wifi_set_promiscuous(true);
        esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
        wifi_promiscuous_filter_t flt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
        esp_wifi_set_promiscuous_filter(&flt);
        esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
        current_channel = 1;

        scanCompleted = false;
        scanStartTime = now;
        lastScanTime = now;
        return;
    }

    if (scanCompleted && now - lastButtonPress > debounceTime) {
        if (!isDetailView && !isLocateMode && digitalRead(BTN_UP) == LOW && currentIndex > 0) {
            --currentIndex;
            if (currentIndex < listStartIndex)
                --listStartIndex;
            lastButtonPress = now;
            needsRedraw = true;
        } else if (!isDetailView && !isLocateMode && digitalRead(BTN_DOWN) == LOW &&
                   currentIndex < (int)pineappleDevices.size() - 1) {
            ++currentIndex;
            if (currentIndex >= listStartIndex + 5)
                ++listStartIndex;
            lastButtonPress = now;
            needsRedraw = true;
        } else if (!isDetailView && !isLocateMode && digitalRead(BTN_RIGHT) == LOW &&
                   !pineappleDevices.empty()) {
            isDetailView = true;
            esp_wifi_set_promiscuous(false);
            lastButtonPress = now;
            needsRedraw = true;
        } else if (isDetailView && !isLocateMode && digitalRead(BTN_RIGHT) == LOW &&
                   !pineappleDevices.empty()) {
            isLocateMode = true;
            strncpy(locateTargetBSSID, pineappleDevices[currentIndex].bssid, sizeof(locateTargetBSSID) - 1);
            locateTargetBSSID[sizeof(locateTargetBSSID) - 1] = '\0';
            locateTargetChannel = pineappleDevices[currentIndex].channel;

            esp_wifi_set_promiscuous(true);
            esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
            wifi_promiscuous_filter_t flt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
            esp_wifi_set_promiscuous_filter(&flt);
            esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
            current_channel = 1;
            last_channel_hop = millis();

            lastButtonPress = now;
            lastLocateUpdate = now;
            needsRedraw = true;
        } else if (isLocateMode && digitalRead(BTN_BACK) == LOW) {
            isLocateMode = false;
            memset(locateTargetBSSID, 0, sizeof(locateTargetBSSID));
            locateTargetChannel = 0;
            esp_wifi_set_promiscuous(false);

            lastButtonPress = now;
            lastScanTime = now;
            needsRedraw = true;
        } else if (isDetailView && !isLocateMode && digitalRead(BTN_BACK) == LOW) {
            isDetailView = false;
            lastButtonPress = now;
            needsRedraw = true;
        }
    }

    if (pineappleDevices.empty()) {
        if (currentIndex != 0 || isDetailView || isLocateMode) {
            needsRedraw = true;
        }
        currentIndex = listStartIndex = 0;
        isDetailView = false;
        isLocateMode = false;
        memset(locateTargetBSSID, 0, sizeof(locateTargetBSSID));
        locateTargetChannel = 0;
    } else {
        currentIndex = constrain(currentIndex, 0, (int)pineappleDevices.size() - 1);
        listStartIndex =
            constrain(listStartIndex, 0, max(0, (int)pineappleDevices.size() - 5));
    }

    if (isDetailView && now - lastLocateUpdate >= locateUpdateInterval) {
        lastLocateUpdate = now;
        needsRedraw = true;
    }

    if (isLocateMode && now - lastLocateUpdate >= locateUpdateInterval) {
        lastLocateUpdate = now;
        needsRedraw = true;
    }

    if (pineappleDevices.empty() && scanCompleted && now - lastCountdownUpdate >= countdownUpdateInterval) {
        lastCountdownUpdate = now;
        needsRedraw = true;
    }

    if (!needsRedraw) {
        return;
    }

    needsRedraw = false;
    u8g2.clearBuffer();

    if (pineappleDevices.empty()) {
        if (isContinuousScanEnabled()) {
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(0, 10, "Pineapple Detector");
            u8g2.drawStr(0, 22, "Scanning...");

            char countStr[32];
            snprintf(countStr, sizeof(countStr), "Found: %d", 0);
            u8g2.drawStr(0, 34, countStr);

            int barWidth = 120;
            int barHeight = 10;
            int barX = (128 - barWidth) / 2;
            int barY = 38;
            u8g2.drawFrame(barX, barY, barWidth, barHeight);

            u8g2.setFont(u8g2_font_5x8_tr);
            u8g2.drawStr(0, 62, "Press SEL to exit");
        } else {
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(0, 10, "No Pineapples");
            u8g2.setFont(u8g2_font_5x8_tr);
            char timeStr[32];
            unsigned long timeLeft = (scanInterval - (now - lastScanTime)) / 1000;
            snprintf(timeStr, sizeof(timeStr), "Scanning in %lus", timeLeft);
            u8g2.drawStr(0, 30, timeStr);
            u8g2.drawStr(0, 45, "Press SEL to exit");
        }
    } else if (isLocateMode) {
        auto &dev = pineappleDevices[currentIndex];
        u8g2.setFont(u8g2_font_5x8_tr);
        char buf[32];

        const char* displaySSID = (dev.ssid[0] == '\0') ? "Hidden" : dev.ssid;
        char maskedSSID[33];
        if (dev.ssid[0] != '\0') {
            maskName(dev.ssid, maskedSSID, sizeof(maskedSSID) - 1);
            displaySSID = maskedSSID;
        }
        snprintf(buf, sizeof(buf), "%.13s Ch:%d", displaySSID, locateTargetChannel);
        u8g2.drawStr(0, 8, buf);

        char maskedBSSID[18];
        maskMAC(dev.bssid, maskedBSSID);
        snprintf(buf, sizeof(buf), "%s", maskedBSSID);
        u8g2.drawStr(0, 16, buf);

        u8g2.setFont(u8g2_font_7x13B_tr);
        snprintf(buf, sizeof(buf), "RSSI: %d dBm", dev.rssi);
        u8g2.drawStr(0, 28, buf);

        u8g2.setFont(u8g2_font_5x8_tr);
        int rssiClamped = constrain(dev.rssi, -100, -40);
        int signalLevel = map(rssiClamped, -100, -40, 0, 5);

        const char* quality;
        if (signalLevel >= 5) quality = "EXCELLENT";
        else if (signalLevel >= 4) quality = "VERY GOOD";
        else if (signalLevel >= 3) quality = "GOOD";
        else if (signalLevel >= 2) quality = "FAIR";
        else if (signalLevel >= 1) quality = "WEAK";
        else quality = "VERY WEAK";

        snprintf(buf, sizeof(buf), "Signal: %s", quality);
        u8g2.drawStr(0, 38, buf);

        int barWidth = 12;
        int barSpacing = 5;
        int totalWidth = (barWidth * 5) + (barSpacing * 4);
        int startX = (128 - totalWidth) / 2;
        int baseY = 54;

        for (int i = 0; i < 5; i++) {
            int barHeight = 8 + (i * 2);
            int x = startX + (i * (barWidth + barSpacing));
            int y = baseY - barHeight;

            if (i < signalLevel) {
                u8g2.drawBox(x, y, barWidth, barHeight);
            } else {
                u8g2.drawFrame(x, y, barWidth, barHeight);
            }
        }

        u8g2.drawStr(0, 62, "L=Back SEL=Exit");
    } else if (isDetailView) {
        u8g2.setFont(u8g2_font_5x8_tr);
        auto &dev = pineappleDevices[currentIndex];
        char buf[40];

        const char* displaySSID = (dev.ssid[0] == '\0') ? "Hidden" : dev.ssid;
        char maskedSSID[33];
        if (dev.ssid[0] != '\0') {
            maskName(dev.ssid, maskedSSID, sizeof(maskedSSID) - 1);
            displaySSID = maskedSSID;
        }
        snprintf(buf, sizeof(buf), "SSID: %s", displaySSID);
        u8g2.drawStr(0, 10, buf);

        char maskedBSSID[18];
        maskMAC(dev.bssid, maskedBSSID);
        snprintf(buf, sizeof(buf), "BSSID: %s", maskedBSSID);
        u8g2.drawStr(0, 20, buf);

        snprintf(buf, sizeof(buf), "RSSI: %d dBm", dev.rssi);
        u8g2.drawStr(0, 30, buf);

        snprintf(buf, sizeof(buf), "Ch: %d  Auth: %s", dev.channel, dev.authMode);
        u8g2.drawStr(0, 40, buf);

        snprintf(buf, sizeof(buf), "Age: %lus", (millis() - dev.lastSeen) / 1000);
        u8g2.drawStr(0, 50, buf);

        u8g2.drawStr(0, 60, "L=Back SEL=Exit R=Locate");
    } else {
        u8g2.setFont(u8g2_font_6x10_tr);
        char header[32];
        snprintf(header, sizeof(header), "Pineapple: %d/%d",
                 (int)pineappleDevices.size(), MAX_DEVICES);
        u8g2.drawStr(0, 10, header);

        for (int i = 0; i < 5; ++i) {
            int idx = listStartIndex + i;
            if (idx >= (int)pineappleDevices.size())
                break;
            auto &d = pineappleDevices[idx];
            if (idx == currentIndex)
                u8g2.drawStr(0, 20 + i * 10, ">");
            char line[32];
            const char* displaySSID = (d.ssid[0] == '\0') ? "Hidden" : d.ssid;
            char maskedSSID[33];
            if (d.ssid[0] != '\0') {
                maskName(d.ssid, maskedSSID, sizeof(maskedSSID) - 1);
                displaySSID = maskedSSID;
            }
            snprintf(line, sizeof(line), "%.8s | RSSI %d",
                     displaySSID, d.rssi);
            u8g2.drawStr(10, 20 + i * 10, line);
        }
    }
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}