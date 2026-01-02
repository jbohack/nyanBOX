/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/nyanbox_advertiser.h"
#include "../include/nyanbox_common.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"

static bool advertiserActive = false;
static bool advertiserEnabled = false;
static char deviceName[32];
static uint8_t advData[31];
static uint8_t scanRespData[31];
static uint8_t advDataLen = 0;
static uint8_t scanRespDataLen = 0;

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

void generateAdvertiserDeviceName(char *name, size_t size) {
    uint64_t chipid = ESP.getEfuseMac();
    uint16_t chip = (uint16_t)(chipid >> 32);
    snprintf(name, size, "nyanBOX-%04X", chip);
}

uint32_t parseAdvertiserVersionToNumber(const char *versionStr) {
    if (!versionStr || versionStr[0] != 'v')
        return 0;

    int major = 0, minor = 0, patch = 0;
    sscanf(versionStr + 1, "%d.%d.%d", &major, &minor, &patch);

    return (major * 10000) + (minor * 100) + patch;
}

void createManufacturerData(uint8_t *manufData) {
    manufData[0] = 0xFF;
    manufData[1] = 0xFF;
    uint16_t level = getCurrentLevel();
    uint32_t version = parseAdvertiserVersionToNumber(NYANBOX_VERSION);
    manufData[2] = (level >> 8) & 0xFF;
    manufData[3] = level & 0xFF;
    manufData[4] = (version >> 24) & 0xFF;
    manufData[5] = (version >> 16) & 0xFF;
    manufData[6] = (version >> 8) & 0xFF;
    manufData[7] = version & 0xFF;
}

void buildAdvertisementData() {
    advDataLen = 0;
    
    advData[advDataLen++] = 0x11;
    advData[advDataLen++] = ESP_BLE_AD_TYPE_128SRV_CMPL;
    
    const char serviceUUID[] = "nyanBOX-service!";
    for (int i = 15; i >= 0; i--) {
        advData[advDataLen++] = serviceUUID[i];
    }
    
    scanRespDataLen = 0;
    
    uint8_t nameLen = strlen(deviceName);
    scanRespData[scanRespDataLen++] = nameLen + 1;
    scanRespData[scanRespDataLen++] = ESP_BLE_AD_TYPE_NAME_CMPL;
    memcpy(&scanRespData[scanRespDataLen], deviceName, nameLen);
    scanRespDataLen += nameLen;
    
    uint8_t manufData[8];
    createManufacturerData(manufData);
    scanRespData[scanRespDataLen++] = 9;
    scanRespData[scanRespDataLen++] = ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE;
    memcpy(&scanRespData[scanRespDataLen], manufData, 8);
    scanRespDataLen += 8;
}

void initNyanboxAdvertiser() {
    advertiserActive = false;
    advertiserEnabled = false;
    generateAdvertiserDeviceName(deviceName, sizeof(deviceName));
}

void startNyanboxAdvertiser() {
    if (advertiserEnabled) return;
    
    advertiserEnabled = true;
    
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
    
    buildAdvertisementData();
    
    esp_ble_gap_config_adv_data_raw(advData, advDataLen);
    esp_ble_gap_config_scan_rsp_data_raw(scanRespData, scanRespDataLen);
    esp_ble_gap_start_advertising(&adv_params);
    
    advertiserActive = true;
}

void stopNyanboxAdvertiser() {
    advertiserEnabled = false;
    
    if (advertiserActive) {
        esp_ble_gap_stop_scanning();
        delay(50);
        esp_ble_gap_stop_advertising();
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
        advertiserActive = false;
    }
}

void updateNyanboxAdvertiser() {
    if (!advertiserEnabled || advertiserActive) return;
    
    if (!advertiserActive) {
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
        
        buildAdvertisementData();
        
        esp_ble_gap_config_adv_data_raw(advData, advDataLen);
        esp_ble_gap_config_scan_rsp_data_raw(scanRespData, scanRespDataLen);
        esp_ble_gap_start_advertising(&adv_params);
        
        advertiserActive = true;
    }
}

bool isNyanboxAdvertising() {
    return advertiserActive;
}