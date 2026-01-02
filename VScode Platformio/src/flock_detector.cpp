/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/flock_detector.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "../include/setting.h"
#include "esp_wifi.h"
#include "esp_event.h"
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

enum ScanPhase {
  PHASE_WIFI_INIT,
  PHASE_BLE_INIT,
  PHASE_COMPLETED
};

struct FlockDeviceData {
  char name[32];
  char address[18];
  int8_t rssi;
  bool isWiFi;
  unsigned long lastSeen;
  char detectionMethod[32];
};

static std::vector<FlockDeviceData> flockDevices;

const int MAX_DEVICES = 100;

// WiFi SSID patterns
const char* wifi_ssid_patterns[] = {
    "flock", "Flock", "FLOCK",
    "FS Ext Battery",
    "Penguin",
    "Pigvision"
};
const int wifi_ssid_patterns_count = sizeof(wifi_ssid_patterns) / sizeof(wifi_ssid_patterns[0]);

// MAC address prefixes
const char* mac_prefixes[] = {
    // FS Ext Battery devices
    "58:8e:81", "cc:cc:cc", "ec:1b:bd", "90:35:ea", "04:0d:84",
    "f0:82:c0", "1c:34:f1", "38:5b:44", "94:34:69", "b4:e3:f9",
    // Flock WiFi devices
    "70:c9:4e", "3c:91:80", "d8:f3:bc", "80:30:49", "14:5a:fc",
    "74:4c:a1", "08:3a:88", "9c:2f:9d", "94:08:53", "e4:aa:ea"
};
const int mac_prefixes_count = sizeof(mac_prefixes) / sizeof(mac_prefixes[0]);

// Device name patterns for BLE
const char* device_name_patterns[] = {
    "FS Ext Battery",
    "Penguin",
    "Flock",
    "Pigvision"
};
const int device_name_patterns_count = sizeof(device_name_patterns) / sizeof(device_name_patterns[0]);

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

typedef struct {
    unsigned frame_ctrl:16;
    unsigned duration_id:16;
    uint8_t addr1[6];
    uint8_t addr2[6];
    uint8_t addr3[6];
    unsigned sequence_ctrl:16;
    uint8_t addr4[6];
} wifi_ieee80211_mac_hdr_t;

typedef struct {
    wifi_ieee80211_mac_hdr_t hdr;
    uint8_t payload[0];
} wifi_ieee80211_packet_t;

static void bda_to_string(uint8_t *bda, char *str, size_t size) {
    if (bda == NULL || str == NULL || size < 18) {
        return;
    }
    snprintf(str, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
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

bool check_mac_prefix(const char* mac_str) {
    for (int i = 0; i < mac_prefixes_count; i++) {
        if (strncasecmp(mac_str, mac_prefixes[i], 8) == 0) {
            return true;
        }
    }
    return false;
}

bool check_ssid_pattern(const char* ssid) {
    if (!ssid || strlen(ssid) == 0) return false;

    for (int i = 0; i < wifi_ssid_patterns_count; i++) {
        if (strcasestr(ssid, wifi_ssid_patterns[i])) {
            return true;
        }
    }
    return false;
}

bool check_device_name_pattern(const char* name) {
    if (!name || strlen(name) == 0) return false;

    for (int i = 0; i < device_name_patterns_count; i++) {
        if (strcasestr(name, device_name_patterns[i])) {
            return true;
        }
    }
    return false;
}

void addOrUpdateFlockDevice(const char* name, const char* address, int8_t rssi, bool isWiFi, const char* detectionMethod) {
    if (isLocateMode && strlen(locateTargetAddress) > 0) {
        if (strcmp(address, locateTargetAddress) != 0) {
            return;
        }
    } else if (flockDevices.size() >= MAX_DEVICES) {
        return;
    }

    for (size_t i = 0; i < flockDevices.size(); i++) {
        if (strcmp(flockDevices[i].address, address) == 0) {
            flockDevices[i].rssi = rssi;
            flockDevices[i].lastSeen = millis();

            if (strlen(name) > 0 && strcmp(name, "Flock Device") != 0) {
                strncpy(flockDevices[i].name, name, 31);
                flockDevices[i].name[31] = '\0';
            }

            if (!isLocateMode) {
                std::sort(flockDevices.begin(), flockDevices.end(),
                          [](const FlockDeviceData &a, const FlockDeviceData &b) {
                            return a.rssi > b.rssi;
                          });
            }
            return;
        }
    }

    FlockDeviceData newDev = {};
    strncpy(newDev.name, name[0] ? name : "Flock Device", 31);
    newDev.name[31] = '\0';
    strncpy(newDev.address, address, 17);
    newDev.address[17] = '\0';
    newDev.rssi = rssi;
    newDev.isWiFi = isWiFi;
    newDev.lastSeen = millis();
    strncpy(newDev.detectionMethod, detectionMethod, 31);
    newDev.detectionMethod[31] = '\0';

    flockDevices.push_back(newDev);

    if (!isLocateMode) {
        std::sort(flockDevices.begin(), flockDevices.end(),
                  [](const FlockDeviceData &a, const FlockDeviceData &b) {
                    return a.rssi > b.rssi;
                  });
    }

    needsRedraw = true;
}

void IRAM_ATTR wifi_sniffer_packet_handler(void* buff, wifi_promiscuous_pkt_type_t type) {
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

    char ssid[33] = {0};
    int ssid_len = 0;

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
                ssid_len = tag_len;
            }
            break;
        }

        offset += 2 + tag_len;
    }

    if (ssid_len > 0 && check_ssid_pattern(ssid)) {
        addOrUpdateFlockDevice(ssid, addrStr, ppkt->rx_ctrl.rssi, true, "WiFi SSID");
        return;
    }

    if (check_mac_prefix(addrStr)) {
        addOrUpdateFlockDevice(ssid_len > 0 ? ssid : "hidden", addrStr, ppkt->rx_ctrl.rssi, true, "WiFi MAC");
        return;
    }
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

    bool macMatch = check_mac_prefix(addrStr);

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

    char name[32] = {0};
    bool nameMatch = false;

    if (adv_name != NULL && adv_name_len > 0 && adv_name_len < 32) {
        memcpy(name, adv_name, adv_name_len);
        name[adv_name_len] = '\0';
        nameMatch = check_device_name_pattern(name);
    }

    if (macMatch || nameMatch) {
        const char* detectionMethod = nameMatch ? "BLE Name" : "BLE MAC";
        addOrUpdateFlockDevice(name[0] ? name : "Flock Device", addrStr, scan_result->scan_rst.rssi, false, detectionMethod);
    }
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
            if (isLocateMode) {
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

void flockDetectorSetup() {
    flockDevices.clear();
    flockDevices.reserve(MAX_DEVICES);
    currentIndex = listStartIndex = 0;
    isDetailView = false;
    isLocateMode = false;
    memset(locateTargetAddress, 0, sizeof(locateTargetAddress));
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
    pinMode(BTN_CENTER, INPUT_PULLUP);

    phaseStartTime = millis();
    lastScanTime = millis();
    current_channel = 1;
    last_channel_hop = millis();
}

void flockDetectorLoop() {
    unsigned long now = millis();

    unsigned long effectiveWifiScanDuration = wifiScanDuration;
    unsigned long effectiveBleScanDuration = bleScanDuration;
    unsigned long effectiveScanInterval = scanInterval;

    if (flockDevices.empty() && isContinuousScanEnabled() && scanCompleted) {
        effectiveWifiScanDuration = 3000;
        effectiveBleScanDuration = 3000;
        effectiveScanInterval = 500;
    }

    if ((currentPhase == PHASE_WIFI_INIT) ||
        (isLocateMode && !bleInitialized)) {
        hop_channel();
    }

    bool shouldShowPhaseScreen = !scanCompleted || (flockDevices.empty() && isContinuousScanEnabled());

    if (shouldShowPhaseScreen && !isDetailView && !isLocateMode && !scanCompleted) {
        if (currentPhase == PHASE_WIFI_INIT) {
            unsigned long elapsed = now - phaseStartTime;

            if (elapsed >= effectiveWifiScanDuration) {
                esp_wifi_set_promiscuous(false);
                esp_wifi_stop();
                delay(100);

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

                esp_ble_gap_register_callback(esp_gap_cb);
                esp_ble_gap_set_scan_params(&ble_scan_params);
                bleInitialized = true;

                currentPhase = PHASE_BLE_INIT;
                phaseStartTime = now;
                needsRedraw = true;
            } else {
                if ((lastDeviceCount != (int)flockDevices.size() || wasScanning != isScanning) || (now - lastLocateUpdate >= 100)) {
                    lastDeviceCount = (int)flockDevices.size();
                    wasScanning = isScanning;
                    lastLocateUpdate = now;

                    u8g2.clearBuffer();
                    u8g2.setFont(u8g2_font_6x10_tr);
                    u8g2.drawStr(0, 10, "Flock Detector");

                    char scanStr[32];
                    snprintf(scanStr, sizeof(scanStr), "WiFi CH:%d", current_channel);
                    u8g2.drawStr(0, 22, scanStr);

                    char countStr[32];
                    snprintf(countStr, sizeof(countStr), "Found: %d", (int)flockDevices.size());
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
                    esp_ble_gap_stop_scanning();
                    delay(50);

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
                    bleInitialized = false;
                }

                esp_wifi_start();
                delay(50);
                esp_wifi_set_ps(WIFI_PS_NONE);
                esp_wifi_set_promiscuous(false);

                currentPhase = PHASE_COMPLETED;
                scanCompleted = true;
                lastScanTime = now;
                needsRedraw = true;
            } else {
                bool shouldRedraw = (lastDeviceCount != (int)flockDevices.size()) ||
                                   (wasScanning != isScanning && !(flockDevices.empty() && isContinuousScanEnabled()));

                if (shouldRedraw || (now - lastLocateUpdate >= 100)) {
                    lastDeviceCount = (int)flockDevices.size();
                    wasScanning = isScanning;
                    lastLocateUpdate = now;

                    u8g2.clearBuffer();
                    u8g2.setFont(u8g2_font_6x10_tr);
                    u8g2.drawStr(0, 10, "Flock Detector");
                    u8g2.drawStr(0, 22, "Scanning BLE...");

                    char countStr[32];
                    snprintf(countStr, sizeof(countStr), "Found: %d", (int)flockDevices.size());
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
        if (flockDevices.size() >= MAX_DEVICES) {
            std::sort(flockDevices.begin(), flockDevices.end(),
                    [](const FlockDeviceData &a, const FlockDeviceData &b) {
                        if (a.lastSeen != b.lastSeen) {
                            return a.lastSeen < b.lastSeen;
                        }
                        return a.rssi < b.rssi;
                    });

            int devicesToRemove = MAX_DEVICES / 4;
            if (devicesToRemove > 0) {
                flockDevices.erase(flockDevices.begin(),
                                        flockDevices.begin() + devicesToRemove);
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
                   currentIndex < (int)flockDevices.size() - 1) {
            ++currentIndex;
            if (currentIndex >= listStartIndex + 5)
                ++listStartIndex;
            lastButtonPress = now;
            needsRedraw = true;
        } else if (!isDetailView && !isLocateMode && digitalRead(BTN_RIGHT) == LOW &&
                   !flockDevices.empty()) {
            isDetailView = true;
            esp_wifi_set_promiscuous(false);
            lastButtonPress = now;
            needsRedraw = true;
        } else if (isDetailView && !isLocateMode && digitalRead(BTN_RIGHT) == LOW &&
                   !flockDevices.empty()) {
            isLocateMode = true;
            strncpy(locateTargetAddress, flockDevices[currentIndex].address, sizeof(locateTargetAddress) - 1);
            locateTargetAddress[sizeof(locateTargetAddress) - 1] = '\0';

            if (flockDevices[currentIndex].isWiFi) {
                esp_wifi_set_promiscuous(true);
                esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
                wifi_promiscuous_filter_t flt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
                esp_wifi_set_promiscuous_filter(&flt);
                esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
                current_channel = 1;
                last_channel_hop = millis();
            } else {
                esp_wifi_set_promiscuous(false);
                esp_wifi_stop();
                delay(100);

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

                esp_ble_gap_register_callback(esp_gap_cb);
                esp_ble_gap_set_scan_params(&ble_scan_params);
                bleInitialized = true;
            }

            lastButtonPress = now;
            lastLocateUpdate = now;
            needsRedraw = true;
        } else if (isLocateMode && digitalRead(BTN_BACK) == LOW) {
            isLocateMode = false;
            memset(locateTargetAddress, 0, sizeof(locateTargetAddress));

            if (bleInitialized) {
                esp_ble_gap_stop_scanning();
                delay(50);

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

    if (flockDevices.empty()) {
        if (currentIndex != 0 || isDetailView || isLocateMode) {
            needsRedraw = true;
        }
        currentIndex = listStartIndex = 0;
        isDetailView = false;
        isLocateMode = false;
        memset(locateTargetAddress, 0, sizeof(locateTargetAddress));
    } else {
        currentIndex = constrain(currentIndex, 0, (int)flockDevices.size() - 1);
        listStartIndex =
            constrain(listStartIndex, 0, max(0, (int)flockDevices.size() - 5));
    }

    if (isDetailView && now - lastLocateUpdate >= locateUpdateInterval) {
        lastLocateUpdate = now;
        needsRedraw = true;
    }

    if (isLocateMode && now - lastLocateUpdate >= locateUpdateInterval) {
        lastLocateUpdate = now;
        needsRedraw = true;
    }

    if (flockDevices.empty() && scanCompleted && now - lastCountdownUpdate >= countdownUpdateInterval) {
        lastCountdownUpdate = now;
        needsRedraw = true;
    }

    if (!needsRedraw) {
        return;
    }

    needsRedraw = false;
    u8g2.clearBuffer();

    if (flockDevices.empty()) {
        if (isContinuousScanEnabled()) {

            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(0, 10, "Flock Detector");
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
            u8g2.drawStr(0, 10, "No Flock devices");
            u8g2.setFont(u8g2_font_5x8_tr);
            char timeStr[32];
            unsigned long timeLeft = (scanInterval - (now - lastScanTime)) / 1000;
            snprintf(timeStr, sizeof(timeStr), "Scanning in %lus", timeLeft);
            u8g2.drawStr(0, 30, timeStr);
            u8g2.drawStr(0, 45, "Press SEL to exit");
        }
    } else if (isLocateMode) {
        auto &dev = flockDevices[currentIndex];
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
        auto &dev = flockDevices[currentIndex];
        char buf[32];
        char maskedName[33];
        maskName(dev.name, maskedName, sizeof(maskedName) - 1);
        snprintf(buf, sizeof(buf), "Name: %s", maskedName);
        u8g2.drawStr(0, 10, buf);
        char maskedAddress[18];
        maskMAC(dev.address, maskedAddress);
        snprintf(buf, sizeof(buf), "MAC: %s", maskedAddress);
        u8g2.drawStr(0, 20, buf);
        snprintf(buf, sizeof(buf), "Method: %s", dev.detectionMethod);
        u8g2.drawStr(0, 30, buf);
        snprintf(buf, sizeof(buf), "RSSI: %d dBm", dev.rssi);
        u8g2.drawStr(0, 40, buf);
        snprintf(buf, sizeof(buf), "Age: %lus", (millis() - dev.lastSeen) / 1000);
        u8g2.drawStr(0, 50, buf);
        u8g2.drawStr(0, 60, "L=Back SEL=Exit R=Locate");
    } else {
        u8g2.setFont(u8g2_font_6x10_tr);
        char header[32];
        snprintf(header, sizeof(header), "Flock: %d/%d",
                 (int)flockDevices.size(), MAX_DEVICES);
        u8g2.drawStr(0, 10, header);

        for (int i = 0; i < 5; ++i) {
            int idx = listStartIndex + i;
            if (idx >= (int)flockDevices.size())
                break;
            auto &d = flockDevices[idx];
            if (idx == currentIndex)
                u8g2.drawStr(0, 20 + i * 10, ">");
            char line[32];
            char maskedName[33];
            maskName(d.name, maskedName, sizeof(maskedName) - 1);
            snprintf(line, sizeof(line), "%.9s %s %d",
                     maskedName, d.isWiFi ? "W" : "B", d.rssi);
            u8g2.drawStr(10, 20 + i * 10, line);
        }
    }
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

void cleanupFlockDetector() {
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        esp_wifi_set_promiscuous(false);
        esp_wifi_stop();
        delay(50);
        esp_wifi_deinit();
        delay(100);
    }

    esp_bluedroid_status_t bt_state = esp_bluedroid_get_status();
    if (bt_state == ESP_BLUEDROID_STATUS_ENABLED) {
        esp_ble_gap_stop_scanning();
        delay(50);
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
}
