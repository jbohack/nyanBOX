/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/flipperzero_detector.h"
#include "../include/radio_manager.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "../include/setting.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#include <vector>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

#define BTN_UP BUTTON_PIN_UP
#define BTN_DOWN BUTTON_PIN_DOWN
#define BTN_RIGHT BUTTON_PIN_RIGHT
#define BTN_BACK BUTTON_PIN_LEFT
#define BTN_CENTER BUTTON_PIN_CENTER

struct FlipperZeroDeviceData {
  char name[32];
  char address[18];
  char color[16];
  int8_t rssi;
  bool hasName;
  unsigned long lastSeen;
  bool isFlipperZero;
};
static std::vector<FlipperZeroDeviceData> flipperZeroDevices;

const int MAX_DEVICES = 100;

int currentIndex = 0;
int listStartIndex = 0;
bool isDetailView = false;
bool isLocateMode = false;
char locateTargetAddress[18] = {0};
unsigned long lastButtonPress = 0;
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
const unsigned long scanDuration = 8;

static bool bleInitialized = false;
static bool scanCompleted = false;

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x100,
    .scan_window            = 0xA0,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
};

static void bda_to_string(uint8_t *bda, char *str, size_t size) {
    if (bda == NULL || str == NULL || size < 18) {
        return;
    }
    snprintf(str, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

const char* getFlipperColorFromUUID(uint8_t *adv_data, uint8_t adv_data_len) {
    // Flipper Zero uses 16-bit service UUIDs:
    // Black: 0x3081, white: 0x3082, transparent: 0x3083
    
    uint8_t uuid_len = 0;
    uint8_t *uuid_data = esp_ble_resolve_adv_data(adv_data, ESP_BLE_AD_TYPE_16SRV_CMPL, &uuid_len);
    
    if (uuid_data != NULL && uuid_len >= 2) {
        for (int i = 0; i + 2 <= uuid_len; i += 2) {
            uint16_t service_uuid = uuid_data[i] | (uuid_data[i+1] << 8);
            
            if (service_uuid == 0x3081) return "Black";
            if (service_uuid == 0x3082) return "White";
            if (service_uuid == 0x3083) return "Transparent";
            if ((service_uuid & 0xFFF0) == 0x3080) return "Generic";
        }
    }
    
    uuid_len = 0;
    uuid_data = esp_ble_resolve_adv_data(adv_data, ESP_BLE_AD_TYPE_16SRV_PART, &uuid_len);
    
    if (uuid_data != NULL && uuid_len >= 2) {
        for (int i = 0; i + 2 <= uuid_len; i += 2) {
            uint16_t service_uuid = uuid_data[i] | (uuid_data[i+1] << 8);
            
            if (service_uuid == 0x3081) return "Black";
            if (service_uuid == 0x3082) return "White";
            if (service_uuid == 0x3083) return "Transparent";
            if ((service_uuid & 0xFFF0) == 0x3080) return "Generic";
        }
    }
    
    return "Unknown";
}

static void process_scan_result(esp_ble_gap_cb_param_t *scan_result) {
    uint8_t *bda = scan_result->scan_rst.bda;
    char addrStr[18];
    bda_to_string(bda, addrStr, sizeof(addrStr));

    if (strlen(addrStr) < 12) return;

    bool isFlipperByMAC = (strncasecmp(addrStr, "80:e1:26", 8) == 0) ||
                          (strncasecmp(addrStr, "80:e1:27", 8) == 0) ||
                          (strncasecmp(addrStr, "0c:fa:22", 8) == 0);

    if (!isFlipperByMAC) {
        return;
    }

    if (isLocateMode && strlen(locateTargetAddress) > 0) {
        if (strcmp(addrStr, locateTargetAddress) != 0) {
            return;
        }
    } else if (flipperZeroDevices.size() >= MAX_DEVICES) {
        return;
    }

    const char* detectedColor = getFlipperColorFromUUID(scan_result->scan_rst.ble_adv, 
                                                         scan_result->scan_rst.adv_data_len);

    for (size_t i = 0; i < flipperZeroDevices.size(); i++) {
        if (strcmp(flipperZeroDevices[i].address, addrStr) == 0) {
            flipperZeroDevices[i].rssi = scan_result->scan_rst.rssi;
            flipperZeroDevices[i].lastSeen = millis();
            
            if (strcmp(detectedColor, "Unknown") != 0 && strcmp(detectedColor, "Generic") != 0) {
                strncpy(flipperZeroDevices[i].color, detectedColor, 15);
                flipperZeroDevices[i].color[15] = '\0';
            }
            
            if (!flipperZeroDevices[i].hasName) {
                uint8_t adv_name_len = 0;
                uint8_t *adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                                    ESP_BLE_AD_TYPE_NAME_CMPL,
                                                    &adv_name_len);
                
                if (adv_name == NULL) {
                    adv_name_len = 0;
                    adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                                        ESP_BLE_AD_TYPE_NAME_SHORT,
                                                        &adv_name_len);
                }

                if (adv_name != NULL && adv_name_len > 0 && adv_name_len < 32) {
                    memcpy(flipperZeroDevices[i].name, adv_name, adv_name_len);
                    flipperZeroDevices[i].name[adv_name_len] = '\0';
                    flipperZeroDevices[i].hasName = true;
                }
            }

            if (!isLocateMode) {
                std::sort(flipperZeroDevices.begin(), flipperZeroDevices.end(),
                          [](const FlipperZeroDeviceData &a, const FlipperZeroDeviceData &b) {
                            return a.rssi > b.rssi;
                          });
            }
            return;
        }
    }

    FlipperZeroDeviceData newDev = {};
    strncpy(newDev.address, addrStr, 17);
    newDev.address[17] = '\0';
    newDev.rssi = scan_result->scan_rst.rssi;
    newDev.lastSeen = millis();
    newDev.isFlipperZero = true;
    
    strncpy(newDev.color, detectedColor, 15);
    newDev.color[15] = '\0';
    
    strcpy(newDev.name, "Flipper Zero");
    newDev.hasName = false;

    uint8_t adv_name_len = 0;
    uint8_t *adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                        ESP_BLE_AD_TYPE_NAME_CMPL,
                                        &adv_name_len);
    
    if (adv_name == NULL) {
        adv_name_len = 0;
        adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                            ESP_BLE_AD_TYPE_NAME_SHORT,
                                            &adv_name_len);
    }

    if (adv_name != NULL && adv_name_len > 0 && adv_name_len < 32) {
        memcpy(newDev.name, adv_name, adv_name_len);
        newDev.name[adv_name_len] = '\0';
        newDev.hasName = true;
    }

    flipperZeroDevices.push_back(newDev);

    if (!isLocateMode) {
        std::sort(flipperZeroDevices.begin(), flipperZeroDevices.end(),
                  [](const FlipperZeroDeviceData &a, const FlipperZeroDeviceData &b) {
                    return a.rssi > b.rssi;
                  });
    }

    needsRedraw = true;
}

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            isScanning = true;
            esp_ble_gap_start_scanning(scanDuration);
            lastScanTime = millis();
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
            process_scan_result(param);
            break;
        case ESP_GAP_SEARCH_INQ_CMPL_EVT:
            lastScanTime = millis();
            scanCompleted = true;
            needsRedraw = true;
            if (isLocateMode) {
                isScanning = true;
                esp_ble_gap_start_scanning(scanDuration);
            } else {
                isScanning = false;
            }
            break;
        default:
            break;
        }
        break;
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        isScanning = false;
        scanCompleted = true;
        break;
    default:
        break;
    }
}

void flipperZeroDetectorSetup() {
    flipperZeroDevices.clear();
    flipperZeroDevices.reserve(MAX_DEVICES);
    currentIndex = listStartIndex = 0;
    isDetailView = false;
    isLocateMode = false;
    memset(locateTargetAddress, 0, sizeof(locateTargetAddress));
    lastButtonPress = 0;
    isScanning = true;
    scanCompleted = false;
    needsRedraw = true;
    lastDeviceCount = 0;
    lastLocateUpdate = 0;
    lastCountdownUpdate = 0;
    wasScanning = false;

    u8g2.begin();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.clearBuffer();
    u8g2.drawStr(0, 10, "Scanning for");
    u8g2.drawStr(0, 20, "Flippers...");
    char countStr[32];
    snprintf(countStr, sizeof(countStr), "%d/%d devices", 0, MAX_DEVICES);
    u8g2.drawStr(0, 35, countStr);
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(0, 60, "Press SEL to exit");
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);

    initBLE();

    esp_ble_gap_register_callback(esp_gap_cb);
    esp_ble_gap_set_scan_params(&ble_scan_params);

    bleInitialized = true;

    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(BTN_BACK, INPUT_PULLUP);
    pinMode(BTN_CENTER, INPUT_PULLUP);
}

void flipperZeroDetectorLoop() {
    if (!bleInitialized) return;

    unsigned long now = millis();

    if (isScanning && !isLocateMode) {
        if (lastDeviceCount != (int)flipperZeroDevices.size() || wasScanning != isScanning) {
            lastDeviceCount = (int)flipperZeroDevices.size();
            wasScanning = isScanning;

            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(0, 10, "Scanning for");
            u8g2.drawStr(0, 20, "Flippers...");

            char countStr[32];
            snprintf(countStr, sizeof(countStr), "%d/%d devices", (int)flipperZeroDevices.size(), MAX_DEVICES);
            u8g2.drawStr(0, 35, countStr);

            int barWidth = 120;
            int barHeight = 10;
            int barX = (128 - barWidth) / 2;
            int barY = 42;

            u8g2.drawFrame(barX, barY, barWidth, barHeight);

            int fillWidth = (flipperZeroDevices.size() * (barWidth - 4)) / MAX_DEVICES;
            if (fillWidth > 0) {
                u8g2.drawBox(barX + 2, barY + 2, fillWidth, barHeight - 4);
            }

            u8g2.setFont(u8g2_font_5x8_tr);
            u8g2.drawStr(0, 62, "Press SEL to exit");

            u8g2.sendBuffer();
            displayMirrorSend(u8g2);
        }
        return;
    }

    if (wasScanning != isScanning) {
        wasScanning = isScanning;
        needsRedraw = true;
    }


    unsigned long effectiveScanInterval = scanInterval;
    uint32_t effectiveScanDuration = scanDuration;

    if (flipperZeroDevices.empty() && isContinuousScanEnabled()) {
        effectiveScanInterval = 500;
        effectiveScanDuration = 3;
    }

    if (!isScanning && scanCompleted && now - lastScanTime > effectiveScanInterval &&
        !isDetailView && !isLocateMode) {
        if (flipperZeroDevices.size() >= MAX_DEVICES) {
            std::sort(flipperZeroDevices.begin(), flipperZeroDevices.end(),
                    [](const FlipperZeroDeviceData &a, const FlipperZeroDeviceData &b) {
                        if (a.lastSeen != b.lastSeen) {
                            return a.lastSeen < b.lastSeen;
                        }
                        return a.rssi < b.rssi;
                    });
            
            int devicesToRemove = MAX_DEVICES / 4;
            if (devicesToRemove > 0) {
                flipperZeroDevices.erase(flipperZeroDevices.begin(), 
                                        flipperZeroDevices.begin() + devicesToRemove);
            }
            
            currentIndex = listStartIndex = 0;
        }
        
        scanCompleted = false;
        isScanning = true;
        esp_ble_gap_start_scanning(effectiveScanDuration);
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
                   currentIndex < (int)flipperZeroDevices.size() - 1) {
            ++currentIndex;
            if (currentIndex >= listStartIndex + 5)
                ++listStartIndex;
            lastButtonPress = now;
            needsRedraw = true;
        } else if (!isDetailView && !isLocateMode && digitalRead(BTN_RIGHT) == LOW &&
                   !flipperZeroDevices.empty()) {
            isDetailView = true;
            if (isScanning) {
                esp_ble_gap_stop_scanning();
                isScanning = false;
            }
            lastButtonPress = now;
            needsRedraw = true;
        } else if (isDetailView && !isLocateMode && digitalRead(BTN_RIGHT) == LOW &&
                   !flipperZeroDevices.empty()) {
            isLocateMode = true;
            strncpy(locateTargetAddress, flipperZeroDevices[currentIndex].address, sizeof(locateTargetAddress) - 1);
            locateTargetAddress[sizeof(locateTargetAddress) - 1] = '\0';
            if (!isScanning) {
                isScanning = true;
                esp_ble_gap_start_scanning(scanDuration);
            }
            lastButtonPress = now;
            lastLocateUpdate = now;
            needsRedraw = true;
        } else if (isLocateMode && digitalRead(BTN_BACK) == LOW) {
            isLocateMode = false;
            memset(locateTargetAddress, 0, sizeof(locateTargetAddress));
            if (isScanning) {
                esp_ble_gap_stop_scanning();
                isScanning = false;
            }
            lastButtonPress = now;
            needsRedraw = true;
        } else if (isDetailView && !isLocateMode && digitalRead(BTN_BACK) == LOW) {
            isDetailView = false;
            if (isScanning) {
                esp_ble_gap_stop_scanning();
                isScanning = false;
            }
            lastButtonPress = now;
            needsRedraw = true;
        }
    }

    if (flipperZeroDevices.empty()) {
        if (currentIndex != 0 || isDetailView || isLocateMode) {
            needsRedraw = true;
        }
        currentIndex = listStartIndex = 0;
        isDetailView = false;
        isLocateMode = false;
        memset(locateTargetAddress, 0, sizeof(locateTargetAddress));
    } else {
        currentIndex = constrain(currentIndex, 0, (int)flipperZeroDevices.size() - 1);
        listStartIndex =
            constrain(listStartIndex, 0, max(0, (int)flipperZeroDevices.size() - 5));
    }

    if (isDetailView && now - lastLocateUpdate >= locateUpdateInterval) {
        lastLocateUpdate = now;
        needsRedraw = true;
    }

    if (isLocateMode && now - lastLocateUpdate >= locateUpdateInterval) {
        lastLocateUpdate = now;
        needsRedraw = true;
    }

    if (flipperZeroDevices.empty() && scanCompleted && !isScanning &&
        now - lastCountdownUpdate >= countdownUpdateInterval) {
        lastCountdownUpdate = now;
        needsRedraw = true;
    }

    if (!needsRedraw) {
        return;
    }

    needsRedraw = false;
    u8g2.clearBuffer();

    if (flipperZeroDevices.empty()) {
        if (isContinuousScanEnabled()) {

            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(0, 10, "Scanning for");
            u8g2.drawStr(0, 20, "Flippers...");

            char countStr[32];
            snprintf(countStr, sizeof(countStr), "%d/%d devices", 0, MAX_DEVICES);
            u8g2.drawStr(0, 35, countStr);

            int barWidth = 120;
            int barHeight = 10;
            int barX = (128 - barWidth) / 2;
            int barY = 42;
            u8g2.drawFrame(barX, barY, barWidth, barHeight);

            u8g2.setFont(u8g2_font_5x8_tr);
            u8g2.drawStr(0, 62, "Press SEL to exit");
        } else {

            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(0, 10, "No Flippers found");
            u8g2.setFont(u8g2_font_5x8_tr);
            char timeStr[32];
            unsigned long timeLeft = (scanInterval - (now - lastScanTime)) / 1000;
            snprintf(timeStr, sizeof(timeStr), "Scanning in %lus", timeLeft);
            u8g2.drawStr(0, 30, timeStr);
            u8g2.drawStr(0, 45, "Press SEL to exit");
        }
    } else if (isLocateMode) {
        auto &dev = flipperZeroDevices[currentIndex];
        u8g2.setFont(u8g2_font_5x8_tr);
        char buf[32];

        char maskedName[33];
        maskName(dev.name, maskedName, sizeof(maskedName) - 1);
        snprintf(buf, sizeof(buf), "%.16s", maskedName);
        u8g2.drawStr(0, 8, buf);

        char maskedAddress[18];
        maskMAC(dev.address, maskedAddress);
        snprintf(buf, sizeof(buf), "%s", maskedAddress);
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
        auto &dev = flipperZeroDevices[currentIndex];
        u8g2.setFont(u8g2_font_5x8_tr);
        char buf[32];

        char maskedName[33];
        maskName(dev.name, maskedName, sizeof(maskedName) - 1);
        snprintf(buf, sizeof(buf), "Name: %s", maskedName);
        u8g2.drawStr(0, 10, buf);

        char maskedAddress[18];
        maskMAC(dev.address, maskedAddress);
        snprintf(buf, sizeof(buf), "MAC: %s", maskedAddress);
        u8g2.drawStr(0, 20, buf);
        snprintf(buf, sizeof(buf), "Color: %s", dev.color);
        u8g2.drawStr(0, 30, buf);
        snprintf(buf, sizeof(buf), "RSSI: %d dBm", dev.rssi);
        u8g2.drawStr(0, 40, buf);
        snprintf(buf, sizeof(buf), "Age: %lus", (millis() - dev.lastSeen) / 1000);
        u8g2.drawStr(0, 50, buf);
        u8g2.drawStr(0, 60, "L=Back SEL=Exit R=Locate");
    } else {
        u8g2.setFont(u8g2_font_6x10_tr);
        char header[32];
        snprintf(header, sizeof(header), "Flippers: %d/%d",
                 (int)flipperZeroDevices.size(), MAX_DEVICES);
        u8g2.drawStr(0, 10, header);

        for (int i = 0; i < 5; ++i) {
            int idx = listStartIndex + i;
            if (idx >= (int)flipperZeroDevices.size())
                break;
            auto &d = flipperZeroDevices[idx];
            if (idx == currentIndex)
                u8g2.drawStr(0, 20 + i * 10, ">");
            char line[32];
            const char* displayName = d.name[0] ? d.name : "Flipper";
            char maskedName[33];
            maskName(displayName, maskedName, sizeof(maskedName) - 1);
            snprintf(line, sizeof(line), "%.8s | RSSI %d",
                     maskedName, d.rssi);
            u8g2.drawStr(10, 20 + i * 10, line);
        }
    }
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}