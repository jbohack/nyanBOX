/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Open Drone ID (ODID) Remote ID spoofing
    per the ASTM F3411 / ASD-STAN prEN 4709-002 spec

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/drone_spoofer.h"
#include "../include/display_mirror.h"
#include "../include/pindefs.h"
#include <U8g2lib.h>
#include <Arduino.h>
#include "esp_wifi.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#include "../include/radio_manager.h"
#include <cstring>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

#define ODID_MESSAGE_SIZE 25
#define ODID_ID_SIZE 20
#define MSG_TYPE_COUNT 4
#define CYCLES_PER_PHASE 50
#define WIFI_CHANNEL 6

static uint8_t msgCounter = 0;
static unsigned long lastAdvTime = 0;
static unsigned long lastDisplayUpdate = 0;
static unsigned long blePktCount = 0;
static unsigned long wifiPktCount = 0;
static unsigned long uniqueDrones = 0;
static bool bleInitialized = false;
static bool wifiInitialized = false;
static bool isAdvertising = false;
static bool needsRedraw = true;
static int currentMsgType = 0;
static int cyclesInPhase = 0;

enum DroneSpooferMode { DS_IDLE, DS_BLE, DS_WIFI };
static DroneSpooferMode spooferMode = DS_IDLE;

static const unsigned long ADV_INTERVAL_MS = 20;
static const unsigned long DISPLAY_UPDATE_MS = 1000;

static const char SERIAL_CHARS[] = "0123456789ABCDEFGHJKLMNPRSTUVWXYZ";
static const int SERIAL_CHARS_LEN = sizeof(SERIAL_CHARS) - 1;

static uint8_t advData[31];
static uint8_t bleMac[6];
static uint8_t wifiMac[6];

static char curSerial[ODID_ID_SIZE + 1];
static double curLat, curLon;
static float curAltGeo, curAltBaro, curSpeed, curDir, curHeight;
static uint8_t curUAType, curStatus, curIDType, curHeightType;
static char curOperatorId[ODID_ID_SIZE + 1];
static double curOpLat, curOpLon;
static float curOpAlt;
static int8_t curVSpeed;

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x0020,
    .adv_int_max = 0x0040,
    .adv_type = ADV_TYPE_NONCONN_IND,
    .own_addr_type = BLE_ADDR_TYPE_RANDOM,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint8_t wifiBeaconFrame[84] = {
    0x80, 0x00,
    0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x64, 0x00,
    0x01, 0x04,
    0x00, 0x00,
    0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x18, 0x30, 0x48,
    0x03, 0x01, WIFI_CHANNEL,
    0xDD,
    0x1E,
    0xFA, 0x0B, 0xBC,
    0x0D,
    0x00,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0x00
};

static uint8_t wifiNanFrame[52] = {
    0xD0, 0x00,
    0x00, 0x00,
    0x51, 0x6F, 0x9A, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x51, 0x6F, 0x9A, 0x01, 0x00, 0x00,
    0x00, 0x00,
    0x04,
    0x09,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0x00
};

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            isAdvertising = (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS);
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            isAdvertising = false;
            break;
        default:
            break;
    }
}

static double randomDouble(double min, double max) {
    return min + (double)random(1000000) / 1000000.0 * (max - min);
}

static float randomFloat(float min, float max) {
    return min + (float)random(10000) / 10000.0f * (max - min);
}

static void randomizeBLEMac(uint8_t *mac) {
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)random(256);
    mac[0] |= 0xC0;
    mac[0] &= 0xFE;
}

static void randomizeWiFiMac(uint8_t *mac) {
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)random(256);
    mac[0] |= 0x02;
    mac[0] &= 0xFE;
}

static void randomizeSerial(char *serial) {
    for (int i = 0; i < ODID_ID_SIZE; i++) {
        serial[i] = SERIAL_CHARS[random(SERIAL_CHARS_LEN)];
    }
    serial[ODID_ID_SIZE] = '\0';
}

static void randomizeOperatorId(char *opId) {
    int fmt = random(3);
    memset(opId, 0, ODID_ID_SIZE + 1);
    if (fmt == 0) {
        strcpy(opId, "OP-");
        for (int i = 3; i < 11; i++) opId[i] = SERIAL_CHARS[random(SERIAL_CHARS_LEN)];
    } else if (fmt == 1) {
        strcpy(opId, "FAA-");
        for (int i = 4; i < 14; i++) opId[i] = SERIAL_CHARS[random(SERIAL_CHARS_LEN)];
    } else {
        int len = random(8, ODID_ID_SIZE);
        for (int i = 0; i < len; i++) opId[i] = SERIAL_CHARS[random(SERIAL_CHARS_LEN)];
    }
}

static void randomizeDrone() {
    randomizeBLEMac(bleMac);
    randomizeWiFiMac(wifiMac);
    randomizeSerial(curSerial);

    curLat = randomDouble(-90.0, 90.0);
    curLon = randomDouble(-180.0, 180.0);
    curAltGeo = randomFloat(5.0f, 500.0f);
    curAltBaro = curAltGeo + randomFloat(-10.0f, 10.0f);
    curSpeed = randomFloat(0.0f, 50.0f);
    curDir = randomFloat(0.0f, 359.0f);
    curHeight = randomFloat(1.0f, 400.0f);
    curVSpeed = (int8_t)random(-20, 21);
    curUAType = (uint8_t)random(16);
    curStatus = (uint8_t)random(5);
    curIDType = (uint8_t)random(5);
    curHeightType = (uint8_t)random(2);

    randomizeOperatorId(curOperatorId);
    curOpLat = curLat + randomDouble(-0.05, 0.05);
    curOpLon = curLon + randomDouble(-0.05, 0.05);
    curOpAlt = randomFloat(0.0f, 100.0f);

    uniqueDrones++;
}

static int32_t encodeLatLon(double val) {
    return (int32_t)(val * 10000000.0);
}

static uint16_t encodeAltitude(float alt) {
    return (uint16_t)((alt + 1000.0f) / 0.5f);
}

static void encodeBasicIDMessage(uint8_t *out) {
    memset(out, 0, ODID_MESSAGE_SIZE);
    out[1] = (curIDType << 4) | (curUAType & 0x0F);
    memcpy(&out[2], curSerial, ODID_ID_SIZE);
}

static void encodeLocationMessage(uint8_t *out) {
    memset(out, 0, ODID_MESSAGE_SIZE);
    out[0] = (0x1 << 4);

    uint8_t ewDir = 0;
    uint8_t dirEnc;
    if (curDir >= 180.0f) {
        ewDir = 1;
        dirEnc = (uint8_t)(curDir - 180.0f);
    } else {
        dirEnc = (uint8_t)curDir;
    }

    uint8_t speedMult = 0;
    uint8_t speedEnc;
    if (curSpeed <= 63.75f) {
        speedMult = 0;
        speedEnc = (uint8_t)(curSpeed / 0.25f);
    } else {
        speedMult = 1;
        speedEnc = (uint8_t)((curSpeed - 63.75f) / 0.75f);
    }

    out[1] = (curStatus << 4) | (curHeightType << 2) | (ewDir << 1) | speedMult;
    out[2] = dirEnc;
    out[3] = speedEnc;
    out[4] = (uint8_t)curVSpeed;

    int32_t lat = encodeLatLon(curLat);
    out[5] = lat & 0xFF;  out[6] = (lat >> 8) & 0xFF;
    out[7] = (lat >> 16) & 0xFF;  out[8] = (lat >> 24) & 0xFF;

    int32_t lon = encodeLatLon(curLon);
    out[9] = lon & 0xFF;  out[10] = (lon >> 8) & 0xFF;
    out[11] = (lon >> 16) & 0xFF;  out[12] = (lon >> 24) & 0xFF;

    uint16_t altBaro = encodeAltitude(curAltBaro);
    out[13] = altBaro & 0xFF;  out[14] = (altBaro >> 8) & 0xFF;

    uint16_t altGeo = encodeAltitude(curAltGeo);
    out[15] = altGeo & 0xFF;  out[16] = (altGeo >> 8) & 0xFF;

    uint16_t height = encodeAltitude(curHeight);
    out[17] = height & 0xFF;  out[18] = (height >> 8) & 0xFF;

    out[19] = ((uint8_t)random(16) << 4) | (uint8_t)random(16);
    out[20] = ((uint8_t)random(16) << 4) | (uint8_t)random(16);

    float ts = fmod((float)(millis() / 1000), 3600.0f);
    uint16_t tsEnc = (uint16_t)(ts * 10.0f);
    out[21] = tsEnc & 0xFF;  out[22] = (tsEnc >> 8) & 0xFF;
    out[23] = (uint8_t)random(16);
}

static void encodeSystemMessage(uint8_t *out) {
    memset(out, 0, ODID_MESSAGE_SIZE);
    out[0] = (0x4 << 4);
    out[1] = ((uint8_t)random(8) << 2) | (uint8_t)random(4);

    int32_t opLat = encodeLatLon(curOpLat);
    out[2] = opLat & 0xFF;  out[3] = (opLat >> 8) & 0xFF;
    out[4] = (opLat >> 16) & 0xFF;  out[5] = (opLat >> 24) & 0xFF;

    int32_t opLon = encodeLatLon(curOpLon);
    out[6] = opLon & 0xFF;  out[7] = (opLon >> 8) & 0xFF;
    out[8] = (opLon >> 16) & 0xFF;  out[9] = (opLon >> 24) & 0xFF;

    uint16_t areaCount = (uint16_t)random(1, 100);
    out[10] = areaCount & 0xFF;  out[11] = (areaCount >> 8) & 0xFF;
    out[12] = (uint8_t)random(256);

    uint16_t ceiling = encodeAltitude(curAltGeo + randomFloat(10.0f, 200.0f));
    out[13] = ceiling & 0xFF;  out[14] = (ceiling >> 8) & 0xFF;

    uint16_t floor = encodeAltitude(randomFloat(-10.0f, 50.0f));
    out[15] = floor & 0xFF;  out[16] = (floor >> 8) & 0xFF;

    out[17] = ((uint8_t)random(16) << 4) | (uint8_t)random(16);

    uint16_t opAlt = encodeAltitude(curOpAlt);
    out[18] = opAlt & 0xFF;  out[19] = (opAlt >> 8) & 0xFF;

    uint32_t ts = (uint32_t)(millis() / 1000);
    out[20] = ts & 0xFF;  out[21] = (ts >> 8) & 0xFF;
    out[22] = (ts >> 16) & 0xFF;  out[23] = (ts >> 24) & 0xFF;
}

static void encodeOperatorIDMessage(uint8_t *out) {
    memset(out, 0, ODID_MESSAGE_SIZE);
    out[0] = (0x5 << 4);
    out[1] = (uint8_t)random(2);
    size_t len = strlen(curOperatorId);
    if (len > ODID_ID_SIZE) len = ODID_ID_SIZE;
    memcpy(&out[2], curOperatorId, len);
}

static void encodeODIDMessage(uint8_t *out, int msgType) {
    switch (msgType) {
        case 0: encodeBasicIDMessage(out); break;
        case 1: encodeLocationMessage(out); break;
        case 2: encodeSystemMessage(out); break;
        case 3: encodeOperatorIDMessage(out); break;
    }
}

static void buildBLEAdvData(const uint8_t *odidMsg) {
    advData[0] = 30;
    advData[1] = 0x16;
    advData[2] = 0xFA;
    advData[3] = 0xFF;
    advData[4] = 0x0D;
    advData[5] = msgCounter++;
    memcpy(&advData[6], odidMsg, ODID_MESSAGE_SIZE);
}

static void sendBLE(const uint8_t *odidMsg) {
    if (isAdvertising) {
        esp_ble_gap_stop_advertising();
        unsigned long t = millis();
        while (isAdvertising && millis() - t < 50) delay(1);
    }

    esp_ble_gap_set_rand_addr(bleMac);
    delay(2);

    buildBLEAdvData(odidMsg);
    esp_ble_gap_config_adv_data_raw(advData, 31);
    delay(2);
    esp_ble_gap_start_advertising(&adv_params);

    blePktCount++;
}

static void sendWiFiBeacon(const uint8_t *odidMsg) {
    if (!wifiInitialized) return;

    memcpy(&wifiBeaconFrame[10], wifiMac, 6);
    memcpy(&wifiBeaconFrame[16], wifiMac, 6);

    uint16_t seqNum = random(4096) << 4;
    wifiBeaconFrame[22] = seqNum & 0xFF;
    wifiBeaconFrame[23] = (seqNum >> 8) & 0xFF;

    uint64_t timestamp = (uint64_t)esp_timer_get_time();
    memcpy(&wifiBeaconFrame[24], &timestamp, 8);

    wifiBeaconFrame[57] = msgCounter;

    memcpy(&wifiBeaconFrame[58], odidMsg, ODID_MESSAGE_SIZE);

    esp_wifi_80211_tx(WIFI_IF_STA, wifiBeaconFrame, sizeof(wifiBeaconFrame), false);
    wifiPktCount++;
}

static void sendWiFiNAN(const uint8_t *odidMsg) {
    if (!wifiInitialized) return;

    memcpy(&wifiNanFrame[10], wifiMac, 6);

    uint16_t seqNum = random(4096) << 4;
    wifiNanFrame[22] = seqNum & 0xFF;
    wifiNanFrame[23] = (seqNum >> 8) & 0xFF;

    memcpy(&wifiNanFrame[26], odidMsg, ODID_MESSAGE_SIZE);

    esp_wifi_80211_tx(WIFI_IF_STA, wifiNanFrame, sizeof(wifiNanFrame), false);
}

static void startBLE() {
    if (bleInitialized) return;
    if (!initBLE()) return;
    esp_ble_gap_register_callback(gap_event_handler);
    bleInitialized = true;
}

static void stopBLE() {
    if (!bleInitialized) return;
    cleanupBLE();
    bleInitialized = false;
    isAdvertising = false;
}

static void startWiFi() {
    if (wifiInitialized) return;
    initWiFi(WIFI_MODE_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    wifiInitialized = true;
}

static void stopWiFi() {
    if (!wifiInitialized) return;
    esp_wifi_set_promiscuous(false);
    esp_wifi_stop();
    delay(50);
    wifiInitialized = false;
}

static void switchPhase() {
    if (spooferMode == DS_BLE) {
        stopBLE();
        startWiFi();
        spooferMode = DS_WIFI;
    } else {
        stopWiFi();
        startBLE();
        spooferMode = DS_BLE;
    }
}

static void sendNextMessage() {
    if (currentMsgType == 0) {
        randomizeDrone();
    }

    uint8_t odidMsg[ODID_MESSAGE_SIZE];
    encodeODIDMessage(odidMsg, currentMsgType);

    if (spooferMode == DS_BLE) {
        sendBLE(odidMsg);
    } else {
        sendWiFiBeacon(odidMsg);
        sendWiFiNAN(odidMsg);
    }

    currentMsgType++;
    if (currentMsgType >= MSG_TYPE_COUNT) {
        currentMsgType = 0;
        cyclesInPhase++;
        if (cyclesInPhase >= CYCLES_PER_PHASE) {
            cyclesInPhase = 0;
            switchPhase();
        }
    }
}

static void fmtCount(char *out, size_t sz, unsigned long val) {
    if (val < 1000) {
        snprintf(out, sz, "%lu", val);
    } else {
        snprintf(out, sz, "%.2fk", val / 1000.0);
    }
}

static void drawDisplay() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_profont11_tf);

    u8g2.drawStr(0, 10, "Drone Spoofer");
    u8g2.drawLine(0, 12, u8g2.getUTF8Width("Drone Spoofer"), 12);

    u8g2.drawStr(0, 26, "Status:");
    u8g2.setCursor(50, 26);
    u8g2.print(spooferMode != DS_IDLE ? "Active" : "Stopped");

    char buf[32], v1[16], v2[16], v3[16];
    fmtCount(v1, sizeof(v1), uniqueDrones);
    snprintf(buf, sizeof(buf), "Drones: %s", v1);
    u8g2.drawStr(0, 40, buf);

    fmtCount(v2, sizeof(v2), blePktCount);
    fmtCount(v3, sizeof(v3), wifiPktCount);
    snprintf(buf, sizeof(buf), "BLE:%s WiFi:%s", v2, v3);
    u8g2.drawStr(0, 52, buf);

    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(0, 62, "UP: Start/Stop");

    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

void droneSpooferSetup() {
    currentMsgType = 0;
    cyclesInPhase = 0;
    msgCounter = 0;
    lastAdvTime = 0;
    lastDisplayUpdate = 0;
    blePktCount = 0;
    wifiPktCount = 0;
    uniqueDrones = 0;
    bleInitialized = false;
    wifiInitialized = false;
    isAdvertising = false;
    spooferMode = DS_IDLE;
    needsRedraw = true;
    memset(bleMac, 0, sizeof(bleMac));
    memset(wifiMac, 0, sizeof(wifiMac));

    randomSeed(esp_random());

    drawDisplay();
    delay(500);
}

void droneSpooferLoop() {
    static unsigned long lastButtonCheck = 0;
    unsigned long now = millis();

    if (now - lastButtonCheck > 300) {
        if (digitalRead(BUTTON_PIN_UP) == LOW) {
            if (spooferMode == DS_IDLE) {
                spooferMode = DS_BLE;
                drawDisplay();
                startBLE();
            } else {
                if (spooferMode == DS_BLE) stopBLE();
                else stopWiFi();
                spooferMode = DS_IDLE;
                drawDisplay();
            }
            delay(500);
            lastButtonCheck = millis();
        }
    }

    now = millis();

    switch (spooferMode) {
        case DS_IDLE:
            break;
        case DS_BLE:
        case DS_WIFI:
            if (now - lastAdvTime >= ADV_INTERVAL_MS) {
                sendNextMessage();
                lastAdvTime = now;
            }
            break;
    }

    if (now - lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
        lastDisplayUpdate = now;
        needsRedraw = true;
    }

    if (needsRedraw) {
        drawDisplay();
        needsRedraw = false;
    }
}

void cleanupDroneSpoofer() {
    if (spooferMode == DS_BLE) stopBLE();
    spooferMode = DS_IDLE;
    cleanupWiFi();
}