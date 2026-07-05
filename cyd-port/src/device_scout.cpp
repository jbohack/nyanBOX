/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/device_scout.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "../include/setting.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#include "../include/radio_manager.h"
#include <vector>
#include <algorithm>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

namespace {

#define BTN_UP BUTTON_PIN_UP
#define BTN_DOWN BUTTON_PIN_DOWN
#define BTN_RIGHT BUTTON_PIN_RIGHT
#define BTN_BACK BUTTON_PIN_LEFT

enum ScanPhase {
  PHASE_WIFI_INIT,
  PHASE_BLE_INIT,
  PHASE_COMPLETED
};

struct ScoutDeviceData {
  char name[32];
  char address[18];
  int8_t rssi;
  bool isWiFi;
  unsigned long lastSeen;
  uint8_t channel;
  uint16_t scanCount;
  bool seenThisCycle;
};

static std::vector<ScoutDeviceData> scoutDevices;

const int MAX_DEVICES = 100;

int currentIndex = 0;
int listStartIndex = 0;
bool isDetailView = false;
bool isLocateMode = false;
char locateTargetAddress[18] = {0};
bool locateTargetIsWiFi = false;
uint8_t locateTargetChannel = 0;
unsigned long lastButtonPress = 0;
const unsigned long debounceTime = 200;

static bool needsRedraw = true;
static int lastDeviceCount = 0;
static unsigned long lastLocateUpdate = 0;
const unsigned long locateUpdateInterval = 1000;
static unsigned long lastCountdownUpdate = 0;
const unsigned long countdownUpdateInterval = 1000;
static bool wasScanning = false;

static ScanPhase currentPhase = PHASE_WIFI_INIT;
static bool isScanning = false;
static unsigned long lastScanTime = 0;
const unsigned long scanInterval = 30000;
const unsigned long wifiScanDuration = 8000;
const unsigned long bleScanDuration = 8000;
static unsigned long phaseStartTime = 0;

static bool bleInitialized = false;
static bool wifiInitialized = false;
static bool scanCompleted = false;

static uint8_t current_channel = 1;
static unsigned long last_channel_hop = 0;
const unsigned long CHANNEL_HOP_INTERVAL = 500;
const uint8_t MAX_CHANNEL = 13;

static void bda_to_string(uint8_t *bda, char *str, size_t size) {
    if (bda == NULL || str == NULL || size < 18) {
        return;
    }
    snprintf(str, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

int getWiFiDeviceCount() {
    int count = 0;
    for (const auto &dev : scoutDevices) {
        if (dev.isWiFi) count++;
    }
    return count;
}

int getBLEDeviceCount() {
    int count = 0;
    for (const auto &dev : scoutDevices) {
        if (!dev.isWiFi) count++;
    }
    return count;
}

void hop_channel() {
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

void addOrUpdateScoutDevice(const char* name, const char* address, int8_t rssi, bool isWiFi, uint8_t channel) {
    if (isLocateMode && strlen(locateTargetAddress) > 0) {
        if (strcmp(address, locateTargetAddress) != 0) {
            return;
        }
    } else if (scoutDevices.size() >= MAX_DEVICES) {
        return;
    }

    unsigned long now = millis();

    for (size_t i = 0; i < scoutDevices.size(); i++) {
        if (strcmp(scoutDevices[i].address, address) == 0) {
            scoutDevices[i].rssi = rssi;
            scoutDevices[i].lastSeen = now;
            scoutDevices[i].channel = channel;

            if (!isLocateMode && !scoutDevices[i].seenThisCycle) {
                scoutDevices[i].scanCount++;
                scoutDevices[i].seenThisCycle = true;
            }

            if (strlen(name) > 0 && strcmp(name, "Unknown") != 0) {
                strncpy(scoutDevices[i].name, name, 31);
                scoutDevices[i].name[31] = '\0';
            }

            if (!isLocateMode) {
                std::sort(scoutDevices.begin(), scoutDevices.end(),
                          [](const ScoutDeviceData &a, const ScoutDeviceData &b) {
                            if (a.scanCount != b.scanCount) {
                                return a.scanCount > b.scanCount;
                            }
                            return a.rssi > b.rssi;
                          });
            }

            return;
        }
    }

    if (isLocateMode) {
        return;
    }

    ScoutDeviceData newDev;
    strncpy(newDev.name, name[0] ? name : "Unknown", 31);
    newDev.name[31] = '\0';
    strncpy(newDev.address, address, 17);
    newDev.address[17] = '\0';
    newDev.rssi = rssi;
    newDev.isWiFi = isWiFi;
    newDev.lastSeen = now;
    newDev.channel = channel;
    newDev.scanCount = 1;
    newDev.seenThisCycle = true;

    scoutDevices.push_back(newDev);

    std::sort(scoutDevices.begin(), scoutDevices.end(),
              [](const ScoutDeviceData &a, const ScoutDeviceData &b) {
                if (a.scanCount != b.scanCount) {
                    return a.scanCount > b.scanCount;
                }
                return a.rssi > b.rssi;
              });

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

    char addrStr[18];
    snprintf(addrStr, sizeof(addrStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             frame[10], frame[11], frame[12], frame[13], frame[14], frame[15]);

    if (frame[10] & 0x01) {
        return;
    }

    char ssid[33] = {0};
    uint8_t channel = 0;
    int ssidOffset = 0;

    if (frameSubtype == 0x80 || frameSubtype == 0x50) {
        ssidOffset = 36;
    } else if (frameSubtype == 0x40) {
        ssidOffset = 24;
    }

    if (ssidOffset > 0 && len > ssidOffset) {
        int offset = ssidOffset;

        while (offset + 2 < len) {
            uint8_t tag_number = frame[offset];
            uint8_t tag_length = frame[offset + 1];

            if (offset + 2 + tag_length > len) {
                break;
            }

            if (tag_number == 0 && tag_length > 0 && tag_length <= 32) {
                memcpy(ssid, &frame[offset + 2], tag_length);
                ssid[tag_length] = '\0';
            }

            if (tag_number == 3 && tag_length == 1) {
                channel = frame[offset + 2];
            }

            offset += 2 + tag_length;
        }
    }

    if (strlen(ssid) == 0) {
        return;
    }

    if (channel == 0) {
        channel = current_channel;
    }

    addOrUpdateScoutDevice(ssid, addrStr, ppkt->rx_ctrl.rssi, true, channel);
}

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x100,
    .scan_window            = 0xA0,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
};

static void process_ble_scan_result(esp_ble_gap_cb_param_t *scan_result) {
    uint8_t *bda = scan_result->scan_rst.bda;
    char addrStr[18];
    bda_to_string(bda, addrStr, sizeof(addrStr));

    if (strlen(addrStr) < 12) return;

    char name[32] = {0};
    uint8_t *adv_name = NULL;
    uint8_t adv_name_len = 0;

    adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                       ESP_BLE_AD_TYPE_NAME_CMPL,
                                       &adv_name_len);

    if (!adv_name) {
        adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                           ESP_BLE_AD_TYPE_NAME_SHORT,
                                           &adv_name_len);
    }

    if (adv_name) {
        memcpy(name, adv_name, std::min((int)adv_name_len, 31));
        name[std::min((int)adv_name_len, 31)] = '\0';
    }

    addOrUpdateScoutDevice(name, addrStr, scan_result->scan_rst.rssi, false, 0);
}

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            isScanning = true;
            esp_ble_gap_start_scanning(8);
        }
        break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            isScanning = false;
        }
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        switch (param->scan_rst.search_evt) {
        case ESP_GAP_SEARCH_INQ_RES_EVT:
            process_ble_scan_result(param);
            break;
        case ESP_GAP_SEARCH_INQ_CMPL_EVT:
            isScanning = false;
            if (isLocateMode && !locateTargetIsWiFi) {
                isScanning = true;
                esp_ble_gap_start_scanning(8);
            }
            break;
        default:
            break;
        }
        break;
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        isScanning = false;
        break;
    default:
        break;
    }
}

void drawDetailView() {
    if (currentIndex >= scoutDevices.size()) {
        isDetailView = false;
        needsRedraw = true;
        return;
    }

    const ScoutDeviceData& dev = scoutDevices[currentIndex];

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x8_tr);

    char maskedName[33];
    maskName(dev.name, maskedName, sizeof(maskedName) - 1);

    char truncatedName[22];
    if (strlen(maskedName) > 20) {
        strncpy(truncatedName, maskedName, 18);
        truncatedName[18] = '\0';
        strcat(truncatedName, "..");
    } else {
        strcpy(truncatedName, maskedName);
    }
    u8g2.drawStr(0, 8, truncatedName);

    char maskedMAC[18];
    maskMAC(dev.address, maskedMAC);
    char line[32];
    snprintf(line, sizeof(line), "MAC: %s", maskedMAC);
    u8g2.drawStr(0, 18, line);

    snprintf(line, sizeof(line), "RSSI: %ddBm", dev.rssi);
    u8g2.drawStr(0, 28, line);

    if (dev.isWiFi && dev.channel > 0) {
        snprintf(line, sizeof(line), "Type: WiFi CH:%d", dev.channel);
    } else {
        snprintf(line, sizeof(line), "Type: %s", dev.isWiFi ? "WiFi" : "BLE");
    }
    u8g2.drawStr(0, 38, line);

    unsigned long ageSeconds = (millis() - dev.lastSeen) / 1000;

    char ageLine[16];
    if (ageSeconds >= 60) {
        unsigned long ageMinutes = ageSeconds / 60;
        snprintf(ageLine, sizeof(ageLine), "%lum", ageMinutes);
    } else {
        snprintf(ageLine, sizeof(ageLine), "%lus", ageSeconds);
    }

    snprintf(line, sizeof(line), "Scans:%-4d Age:%s", dev.scanCount, ageLine);
    u8g2.drawStr(0, 48, line);

    u8g2.drawStr(0, 64, "L=Back R=Locate");

    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

void drawLocateView() {
    if (currentIndex >= scoutDevices.size()) {
        isLocateMode = false;
        needsRedraw = true;
        return;
    }

    const ScoutDeviceData& dev = scoutDevices[currentIndex];

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x8_tr);
    char buf[32];

    char maskedName[33];
    maskName(dev.name, maskedName, sizeof(maskedName) - 1);
    snprintf(buf, sizeof(buf), "%.16s", maskedName);
    u8g2.drawStr(0, 8, buf);

    char maskedAddress[18];
    maskMAC(dev.address, maskedAddress);
    if (dev.isWiFi && dev.channel > 0) {
        snprintf(buf, sizeof(buf), "%s CH:%d", maskedAddress, dev.channel);
    } else {
        snprintf(buf, sizeof(buf), "%s", maskedAddress);
    }
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

    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

}

void deviceScoutSetup() {
    scoutDevices.clear();
    scoutDevices.reserve(MAX_DEVICES);
    currentIndex = listStartIndex = 0;
    isDetailView = false;
    isLocateMode = false;
    memset(locateTargetAddress, 0, sizeof(locateTargetAddress));
    locateTargetIsWiFi = false;
    locateTargetChannel = 0;
    lastButtonPress = 0;
    isScanning = true;
    needsRedraw = true;
    lastDeviceCount = 0;
    lastLocateUpdate = 0;
    lastCountdownUpdate = 0;
    wasScanning = false;
    currentPhase = PHASE_WIFI_INIT;
    phaseStartTime = 0;
    scanCompleted = false;
    bleInitialized = false;
    wifiInitialized = false;

    u8g2.begin();
    u8g2.setFont(u8g2_font_6x10_tr);

    initWiFi(WIFI_MODE_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
    wifi_promiscuous_filter_t flt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
    esp_wifi_set_promiscuous_filter(&flt);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    current_channel = 1;
    wifiInitialized = true;

    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(BTN_BACK, INPUT_PULLUP);

    phaseStartTime = millis();
    lastScanTime = millis();
    last_channel_hop = millis();
}

void deviceScoutLoop() {
    checkIdle();

    unsigned long now = millis();

    unsigned long effectiveWifiScanDuration = wifiScanDuration;
    unsigned long effectiveBleScanDuration = bleScanDuration;
    unsigned long effectiveScanInterval = scanInterval;

    if (scoutDevices.empty() && isContinuousScanEnabled() && scanCompleted) {
        effectiveWifiScanDuration = 3000;
        effectiveBleScanDuration = 3000;
        effectiveScanInterval = 500;
    }

    if ((currentPhase == PHASE_WIFI_INIT) && !isDetailView && !isLocateMode) {
        hop_channel();
    }

    bool shouldShowPhaseScreen = !scanCompleted || (scoutDevices.empty() && isContinuousScanEnabled());

    if (shouldShowPhaseScreen && !isDetailView && !isLocateMode && !scanCompleted) {
        if (currentPhase == PHASE_WIFI_INIT) {
            unsigned long elapsed = now - phaseStartTime;

            if (elapsed >= effectiveWifiScanDuration) {
                esp_wifi_set_promiscuous(false);
                esp_wifi_stop();
                delay(100);

                initBLE();
                esp_ble_gap_register_callback(esp_gap_cb);
                esp_ble_gap_set_scan_params(&ble_scan_params);
                bleInitialized = true;

                currentPhase = PHASE_BLE_INIT;
                phaseStartTime = now;
                needsRedraw = true;
            } else {
                if ((lastDeviceCount != (int)scoutDevices.size() || wasScanning != isScanning) || (now - lastLocateUpdate >= 100)) {
                    lastDeviceCount = (int)scoutDevices.size();
                    wasScanning = isScanning;
                    lastLocateUpdate = now;

                    u8g2.clearBuffer();
                    u8g2.setFont(u8g2_font_6x10_tr);
                    u8g2.drawStr(0, 10, "Device Scout");

                    char scanStr[32];
                    snprintf(scanStr, sizeof(scanStr), "Scanning WiFi...");
                    u8g2.drawStr(0, 22, scanStr);

                    char countStr[32];
                    int wifiCount = getWiFiDeviceCount();
                    int bleCount = getBLEDeviceCount();
                    snprintf(countStr, sizeof(countStr), "W:%d B:%d", wifiCount, bleCount);
                    u8g2.drawStr(0, 34, countStr);

                    int barWidth = 120;
                    int barHeight = 10;
                    int barX = (128 - barWidth) / 2;
                    int barY = 38;

                    u8g2.drawFrame(barX, barY, barWidth, barHeight);

                    int fillWidth = (elapsed * (barWidth - 4)) / effectiveWifiScanDuration;
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
        } else if (currentPhase == PHASE_BLE_INIT) {
            unsigned long elapsed = now - phaseStartTime;

            if (!isScanning && elapsed >= effectiveBleScanDuration) {
                if (bleInitialized) {
                    cleanupBLE();
                    bleInitialized = false;
                }

                esp_wifi_start();
                delay(50);

                currentPhase = PHASE_COMPLETED;
                scanCompleted = true;
                lastScanTime = now;
                needsRedraw = true;
            } else {
                bool shouldRedraw = (lastDeviceCount != (int)scoutDevices.size()) ||
                                   (wasScanning != isScanning && !(scoutDevices.empty() && isContinuousScanEnabled()));

                if (shouldRedraw || (now - lastLocateUpdate >= 100)) {
                    lastDeviceCount = (int)scoutDevices.size();
                    wasScanning = isScanning;
                    lastLocateUpdate = now;

                    u8g2.clearBuffer();
                    u8g2.setFont(u8g2_font_6x10_tr);
                    u8g2.drawStr(0, 10, "Device Scout");
                    u8g2.drawStr(0, 22, "Scanning BLE...");

                    char countStr[32];
                    int wifiCount = getWiFiDeviceCount();
                    int bleCount = getBLEDeviceCount();
                    snprintf(countStr, sizeof(countStr), "W:%d B:%d", wifiCount, bleCount);
                    u8g2.drawStr(0, 34, countStr);

                    int barWidth = 120;
                    int barHeight = 10;
                    int barX = (128 - barWidth) / 2;
                    int barY = 38;

                    u8g2.drawFrame(barX, barY, barWidth, barHeight);

                    int fillWidth = (elapsed * (barWidth - 4)) / effectiveBleScanDuration;
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
    }

    if (wasScanning != isScanning) {
        wasScanning = isScanning;
        needsRedraw = true;
    }

    if (scanCompleted && now - lastScanTime > effectiveScanInterval && !isDetailView && !isLocateMode) {
        for (auto &dev : scoutDevices) {
            dev.seenThisCycle = false;
        }

        if (scoutDevices.size() >= MAX_DEVICES) {
            std::sort(scoutDevices.begin(), scoutDevices.end(),
                    [](const ScoutDeviceData &a, const ScoutDeviceData &b) {
                        if (a.lastSeen != b.lastSeen) {
                            return a.lastSeen < b.lastSeen;
                        }
                        return a.rssi < b.rssi;
                    });

            int devicesToRemove = MAX_DEVICES / 4;
            if (devicesToRemove > 0) {
                scoutDevices.erase(scoutDevices.begin(),
                                        scoutDevices.begin() + devicesToRemove);
            }

            currentIndex = listStartIndex = 0;
        }

        esp_wifi_set_promiscuous(true);
        esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
        wifi_promiscuous_filter_t flt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
        esp_wifi_set_promiscuous_filter(&flt);
        esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
        current_channel = 1;

        currentPhase = PHASE_WIFI_INIT;
        scanCompleted = false;
        phaseStartTime = now;
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
                   currentIndex < (int)scoutDevices.size() - 1) {
            ++currentIndex;
            if (currentIndex >= listStartIndex + 4)
                ++listStartIndex;
            lastButtonPress = now;
            needsRedraw = true;
        } else if (!isDetailView && !isLocateMode && digitalRead(BTN_RIGHT) == LOW &&
                   !scoutDevices.empty()) {
            isDetailView = true;
            lastButtonPress = now;
            needsRedraw = true;
        } else if (isDetailView && !isLocateMode && digitalRead(BTN_RIGHT) == LOW &&
                   !scoutDevices.empty()) {
            isLocateMode = true;
            strncpy(locateTargetAddress, scoutDevices[currentIndex].address, sizeof(locateTargetAddress) - 1);
            locateTargetAddress[sizeof(locateTargetAddress) - 1] = '\0';
            locateTargetIsWiFi = scoutDevices[currentIndex].isWiFi;
            locateTargetChannel = scoutDevices[currentIndex].channel;

            if (locateTargetIsWiFi) {
                esp_wifi_set_promiscuous(true);
                esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
                wifi_promiscuous_filter_t flt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
                esp_wifi_set_promiscuous_filter(&flt);
                esp_wifi_set_channel(locateTargetChannel, WIFI_SECOND_CHAN_NONE);
            } else {
                esp_wifi_set_promiscuous(false);
                esp_wifi_stop();
                delay(100);

                initBLE();
                esp_ble_gap_register_callback(esp_gap_cb);
                esp_ble_gap_set_scan_params(&ble_scan_params);
                isScanning = true;
                bleInitialized = true;
            }

            lastButtonPress = now;
            lastLocateUpdate = now;
            needsRedraw = true;
        } else if (isLocateMode && digitalRead(BTN_BACK) == LOW) {
            isLocateMode = false;
            memset(locateTargetAddress, 0, sizeof(locateTargetAddress));
            locateTargetIsWiFi = false;
            locateTargetChannel = 0;

            if (bleInitialized) {
                cleanupBLE();
                bleInitialized = false;
                esp_wifi_start();
                delay(50);
                esp_wifi_set_ps(WIFI_PS_NONE);
            } else {
                esp_wifi_set_promiscuous(false);
            }

            lastButtonPress = now;
            lastScanTime = now;
            needsRedraw = true;
        } else if (isDetailView && !isLocateMode && digitalRead(BTN_BACK) == LOW) {
            isDetailView = false;
            lastButtonPress = now;
            needsRedraw = true;
        }
    }


    if (scoutDevices.empty()) {
        if (currentIndex != 0 || isDetailView || isLocateMode) {
            needsRedraw = true;
        }
        currentIndex = listStartIndex = 0;
        isDetailView = false;
        isLocateMode = false;
        memset(locateTargetAddress, 0, sizeof(locateTargetAddress));
        locateTargetIsWiFi = false;
        locateTargetChannel = 0;
    } else {
        currentIndex = constrain(currentIndex, 0, (int)scoutDevices.size() - 1);
        listStartIndex = constrain(listStartIndex, 0, max(0, (int)scoutDevices.size() - 4));
    }

    if (isDetailView && now - lastLocateUpdate >= locateUpdateInterval) {
        lastLocateUpdate = now;
        needsRedraw = true;
    }

    if (isLocateMode && now - lastLocateUpdate >= locateUpdateInterval) {
        lastLocateUpdate = now;
        needsRedraw = true;
    }

    if (scoutDevices.empty() && scanCompleted && now - lastCountdownUpdate >= countdownUpdateInterval) {
        lastCountdownUpdate = now;
        needsRedraw = true;
    }

    if (!needsRedraw) {
        return;
    }

    needsRedraw = false;
    u8g2.clearBuffer();

    if (scoutDevices.empty()) {
        if (isContinuousScanEnabled()) {
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(0, 10, "Device Scout");
            u8g2.drawStr(0, 22, "Scanning...");

            char countStr[32];
            snprintf(countStr, sizeof(countStr), "W:0 B:0");
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
            u8g2.drawStr(0, 10, "No devices found");
            u8g2.setFont(u8g2_font_5x8_tr);
            char timeStr[32];
            unsigned long timeLeft = (scanInterval - (now - lastScanTime)) / 1000;
            snprintf(timeStr, sizeof(timeStr), "Scanning in %lus", timeLeft);
            u8g2.drawStr(0, 30, timeStr);
            u8g2.drawStr(0, 45, "Press SEL to exit");
        }
    } else if (isLocateMode) {
        drawLocateView();
        return;
    } else if (isDetailView) {
        drawDetailView();
        return;
    } else {
        u8g2.setFont(u8g2_font_6x10_tr);
        char header[32];
        int wifiCount = getWiFiDeviceCount();
        int bleCount = getBLEDeviceCount();
        snprintf(header, sizeof(header), "W:%d B:%d (%d/%d)", wifiCount, bleCount, (int)scoutDevices.size(), MAX_DEVICES);
        u8g2.drawStr(0, 10, header);

        for (int i = 0; i < 4; ++i) {
            int idx = listStartIndex + i;
            if (idx >= (int)scoutDevices.size())
                break;
            auto &d = scoutDevices[idx];
            if (idx == currentIndex)
                u8g2.drawStr(0, 20 + i * 12, ">");

            char maskedName[33];
            maskName(d.name, maskedName, sizeof(maskedName) - 1);

            char line[32];
            snprintf(line, sizeof(line), "%.12s %s %d",
                     maskedName, d.isWiFi ? "W" : "B", d.rssi);
            u8g2.drawStr(10, 20 + i * 12, line);
        }
    }
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

void cleanupDeviceScout() {
    cleanupWiFi();
    cleanupBLE();
}