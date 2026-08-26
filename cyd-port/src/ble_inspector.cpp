/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2026 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/ble_inspector.h"
#include "../include/radio_manager.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "../include/setting.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#include <stdarg.h>
#include <vector>
#include <algorithm>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

#define BTN_UP    BUTTON_PIN_UP
#define BTN_DOWN  BUTTON_PIN_DOWN
#define BTN_RIGHT BUTTON_PIN_RIGHT
#define BTN_BACK  BUTTON_PIN_LEFT

std::vector<BLEDevice> bleInspectorDevices;

static const int MAX_DEVICES = 100;
static const int MAX_ROWS = 60;
static const int VISIBLE = 5;

static int currentIndex = 0;
static int listStartIndex = 0;
enum BLEInspView { BLEINSP_LIST, BLEINSP_DETAIL, BLEINSP_LOCATE };
static BLEInspView inspView = BLEINSP_LIST;
static char locateTargetAddress[18] = {0};

static char detailRows[MAX_ROWS][26];
static int numRows = 0;
static int detailScrollOffset = 0;

static unsigned long lastButtonPress = 0;
static const unsigned long debounceTime = 200;

static bool needsRedraw = true;
static int lastDeviceCount = 0;
static unsigned long lastLocateUpdate = 0;
static unsigned long lastCountdownUpd = 0;
static const unsigned long locateUpdateInterval = 1000;
static bool wasScanning = false;

static bool isScanning = false;
static unsigned long lastScanTime = 0;
static const unsigned long scanInterval = 120000;
static const uint32_t scanDuration = 8;

static bool bleInitialized = false;
static bool scanCompleted = false;

static esp_ble_scan_params_t ble_inspector_scan_params = {
    .scan_type          = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval      = 0x100,
    .scan_window        = 0xA0,
    .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE
};

static const char* addrTypeName(uint8_t t) {
    switch (t) {
        case 0x00: return "Public";
        case 0x01: return "Random Static";
        case 0x02: return "RPA (Public)";
        case 0x03: return "RPA (Random)";
        default:   return "Unknown";
    }
}

static const char* advTypeName(uint8_t t) {
    switch (t) {
        case 0x00: return "ADV_IND (Conn.)";
        case 0x01: return "ADV_DIRECT_IND";
        case 0x02: return "ADV_SCAN_IND";
        case 0x03: return "ADV_NONCONN_IND";
        case 0x04: return "SCAN_RSP";
        default:   return "Unknown";
    }
}

static const char* companyName(uint16_t id) {
    switch (id) {
        case 0x004C: return "Apple";
        case 0x0006: return "Microsoft";
        case 0x0075: return "Samsung";
        case 0x00E0: return "Google";
        case 0x0499: return "Ruuvi";
        case 0x0087: return "Garmin";
        case 0x0171: return "Amazon";
        case 0x067C: return "Tile";
        case 0x022B: return "Tesla";
        case 0x08AA: return "DJI";
        case 0xFC81: return "Axon";
        case 0x01AB: return "Meta";
        case 0x058E: return "Meta XR";
        case 0x0059: return "Nordic Semi";
        case 0x000F: return "Broadcom";
        case 0x0002: return "Intel";
        case 0x001D: return "Qualcomm";
        case 0x000D: return "Texas Instr";
        case 0x038F: return "Xiaomi";
        case 0x02E5: return "Espressif";
        case 0x0030: return "ST Micro";
        case 0x006B: return "Polar";
        case 0x012D: return "Sony";
        default:     return nullptr;
    }
}

static const char* uuid16Name(uint16_t u) {
    switch (u) {
        case 0x1800: return "GenericAccess";
        case 0x1801: return "GenericAttrib";
        case 0x1802: return "ImmAlert";
        case 0x1803: return "LinkLoss";
        case 0x1804: return "TxPower";
        case 0x1805: return "CurrentTime";
        case 0x1809: return "HealthThermo";
        case 0x180A: return "DeviceInfo";
        case 0x180D: return "HeartRate";
        case 0x180F: return "Battery";
        case 0x1810: return "BloodPress";
        case 0x1811: return "AlertNotif";
        case 0x1812: return "HID";
        case 0x1814: return "RunSpeed";
        case 0x1816: return "CycleSpeed";
        case 0x1818: return "CyclePower";
        case 0x1819: return "LocationNav";
        case 0x181C: return "UserData";
        case 0x1826: return "FitnessMachine";
        case 0x183B: return "BinarySensor";
        case 0x3081: return "Flipper Zero";
        case 0x3082: return "Flipper Zero";
        case 0x3083: return "Flipper Zero";
        case 0xFD5A: return "DUNS";
        case 0xFD5F: return "RayBan/Meta";
        case 0xFD6F: return "ExposureNotif";
        case 0xFE95: return "Xiaomi";
        case 0xFE9F: return "Google";
        case 0xFEAA: return "Eddystone";
        case 0xFECB: return "Tile";
        case 0xFEEC: return "Tile";
        case 0xFEED: return "Tile";
        case 0xFFFA: return "RemoteID";
        default:     return nullptr;
    }
}

static const char* appearanceName(uint16_t app) {
    switch (app >> 6) {
        case 0:  return "Unknown";
        case 1:  return "Phone";
        case 2:  return "Computer";
        case 3:  return "Watch";
        case 4:  return "Clock";
        case 5:  return "Display";
        case 6:  return "Remote Ctrl";
        case 7:  return "Eye Glasses";
        case 8:  return "Tag";
        case 9:  return "Keyring";
        case 10: return "Media Player";
        case 11: return "Barcode";
        case 12: return "Thermometer";
        case 13: return "Heart Rate";
        case 14: return "Blood Pressure";
        case 15: return "HID";
        case 16: return "Glucose Meter";
        case 17: return "Running";
        case 18: return "Cycling";
        case 49: return "Pulse Oximeter";
        case 50: return "Weight Scale";
        default: return nullptr;
    }
}

static void addRow(const char* fmt, ...) {
    if (numRows >= MAX_ROWS) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(detailRows[numRows], 26, fmt, args);
    va_end(args);
    numRows++;
}

static void addHexRows(const uint8_t* data, int len, int bytesPerRow = 6) {
    for (int i = 0; i < len; i += bytesPerRow) {
        char hex[26] = "  ";
        for (int j = i; j < i + bytesPerRow && j < len; j++) {
            char b[4];
            snprintf(b, sizeof(b), "%02X ", data[j]);
            strncat(hex, b, sizeof(hex) - strlen(hex) - 1);
        }
        addRow("%s", hex);
    }
}

static void buildDetailRows(const BLEDevice& dev) {
    numRows = 0;

    char maskedName[33];
    maskName(dev.hasName ? dev.name : "Unknown", maskedName, 24);
    addRow("Name: %.19s", maskedName);

    char maskedMAC[18];
    maskMAC(dev.address, maskedMAC);
    addRow("MAC: %s", maskedMAC);

    addRow("Addr: %s", addrTypeName(dev.addrType));
    addRow("ADV:  %s", advTypeName(dev.advType));
    addRow("RSSI: %d dBm", dev.rssi);
    addRow("Age:  %lus ago", (millis() - dev.lastSeen) / 1000);

    if (dev.payloadLength == 0) {
        addRow("(no adv data)");
        return;
    }

    const uint8_t* raw = dev.payload;
    size_t total = dev.payloadLength;
    size_t i = 0;

    while (i + 1 < total) {
        uint8_t recLen = raw[i];
        if (recLen == 0) break;
        if (i + recLen >= total) break;

        uint8_t adType = raw[i + 1];
        const uint8_t* d = &raw[i + 2];
        uint8_t dLen = recLen - 1;

        switch (adType) {

        case 0x01:
            if (dLen >= 1) {
                addRow("Flags: 0x%02X", d[0]);
                if (d[0] & 0x01) addRow("  Ltd Discoverable");
                if (d[0] & 0x02) addRow("  Gen Discoverable");
                if (d[0] & 0x04) addRow("  BR/EDR Not Supp.");
                if (d[0] & 0x08) addRow("  LE+BR/EDR (ctrl)");
                if (d[0] & 0x10) addRow("  LE+BR/EDR (host)");
            }
            break;

        case 0x02:
        case 0x03:
            addRow(adType == 0x03 ? "Svc UUIDs:" : "Svc UUIDs (inc.):");
            for (int j = 0; j + 1 < dLen; j += 2) {
                uint16_t uuid = d[j] | ((uint16_t)d[j + 1] << 8);
                const char* n = uuid16Name(uuid);
                if (n) addRow("  0x%04X %s", uuid, n);
                else   addRow("  0x%04X", uuid);
            }
            break;

        case 0x04:
        case 0x05:
            addRow(adType == 0x05 ? "32-bit UUIDs:" : "32-UUIDs (inc.):");
            for (int j = 0; j + 3 < dLen; j += 4) {
                uint32_t uuid = (uint32_t)d[j]         | ((uint32_t)d[j+1] << 8)
                              | ((uint32_t)d[j+2] << 16) | ((uint32_t)d[j+3] << 24);
                addRow("  0x%08X", uuid);
            }
            break;

        case 0x06:
        case 0x07:
            addRow(adType == 0x07 ? "128-bit UUID:" : "128-UUID (inc.):");
            for (int j = 0; j + 15 < dLen; j += 16) {
                addRow("%02x%02x%02x%02x-%02x%02x-%02x%02x-",
                       d[j+15], d[j+14], d[j+13], d[j+12],
                       d[j+11], d[j+10], d[j+9],  d[j+8]);
                addRow("%02x%02x-%02x%02x%02x%02x%02x%02x",
                       d[j+7], d[j+6],
                       d[j+5], d[j+4], d[j+3], d[j+2], d[j+1], d[j+0]);
            }
            break;

        case 0x08:
        case 0x09: {
            char nameBuf[32] = {};
            uint8_t nl = (dLen < 31) ? dLen : 31;
            memcpy(nameBuf, d, nl);
            char masked[33];
            maskName(nameBuf, masked, sizeof(masked) - 1);
            addRow(adType == 0x09 ? "Local Name: %.13s"
                                  : "Short Name: %.13s", masked);
            break;
        }

        case 0x0A:
            if (dLen >= 1)
                addRow("TX Power: %+d dBm", (int8_t)d[0]);
            break;

        case 0x12:
            if (dLen >= 4) {
                uint16_t lo = d[0] | ((uint16_t)d[1] << 8);
                uint16_t hi = d[2] | ((uint16_t)d[3] << 8);
                addRow("Conn Interval:");
                addRow("  %d-%d ms", (int)(lo * 1.25f), (int)(hi * 1.25f));
            }
            break;

        case 0x14:
            addRow("Solicitation:");
            for (int j = 0; j + 1 < dLen; j += 2) {
                uint16_t uuid = d[j] | ((uint16_t)d[j + 1] << 8);
                const char* n = uuid16Name(uuid);
                if (n) addRow("  0x%04X %s", uuid, n);
                else   addRow("  0x%04X", uuid);
            }
            break;

        case 0x15:
            addRow("128 Solicitation:");
            if (dLen >= 16) {
                addRow("%02x%02x%02x%02x-%02x%02x-%02x%02x-",
                       d[15], d[14], d[13], d[12],
                       d[11], d[10], d[9],  d[8]);
                addRow("%02x%02x-%02x%02x%02x%02x%02x%02x",
                       d[7], d[6], d[5], d[4], d[3], d[2], d[1], d[0]);
            }
            break;

        case 0x16:
            if (dLen >= 2) {
                uint16_t uuid = d[0] | ((uint16_t)d[1] << 8);
                const char* n = uuid16Name(uuid);
                if (n) addRow("Svc Data 0x%04X:", uuid);
                else   addRow("Svc Data: 0x%04X", uuid);
                if (n) addRow("  (%s)", n);
                if (dLen > 2) addHexRows(d + 2, dLen - 2);
            }
            break;

        case 0x19:
            if (dLen >= 2) {
                uint16_t app = d[0] | ((uint16_t)d[1] << 8);
                const char* n = appearanceName(app);
                if (n) addRow("Appearance: %s", n);
                else   addRow("Appearance:0x%04X", app);
            }
            break;

        case 0x1A:
            if (dLen >= 2) {
                uint16_t iv = d[0] | ((uint16_t)d[1] << 8);
                addRow("Adv Interval:%dms", (int)(iv * 0.625f));
            }
            break;

        case 0x1B:
            if (dLen >= 7) {
                addRow("LE BT Addr:");
                char leAddr[18];
                snprintf(leAddr, sizeof(leAddr), "%02x:%02x:%02x:%02x:%02x:%02x",
                         d[6], d[5], d[4], d[3], d[2], d[1]);
                char maskedLeAddr[18];
                maskMAC(leAddr, maskedLeAddr);
                addRow("  %s", maskedLeAddr);
            }
            break;

        case 0x20:
            if (dLen >= 4) {
                uint32_t uuid = (uint32_t)d[0] | ((uint32_t)d[1] << 8)
                              | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
                addRow("Svc Data32:");
                addRow("  0x%08X", uuid);
                if (dLen > 4) addHexRows(d + 4, dLen - 4);
            }
            break;

        case 0x21:
            if (dLen >= 16) {
                addRow("Svc Data128:");
                addRow("%02x%02x%02x%02x-%02x%02x-%02x%02x-",
                       d[15], d[14], d[13], d[12],
                       d[11], d[10], d[9],  d[8]);
                addRow("%02x%02x-%02x%02x%02x%02x%02x%02x",
                       d[7], d[6], d[5], d[4], d[3], d[2], d[1], d[0]);
                if (dLen > 16) addHexRows(d + 16, dLen - 16);
            }
            break;

        case 0xFF:
            if (dLen >= 2) {
                uint16_t cid = d[0] | ((uint16_t)d[1] << 8);
                const char* cn = companyName(cid);
                addRow("Mfr Data:");
                if (cn) addRow("  ID:0x%04X(%s)", cid, cn);
                else    addRow("  ID: 0x%04X", cid);
                if (dLen > 2) addHexRows(d + 2, dLen - 2);
            }
            break;

        default:
            addRow("AD[0x%02X]:", adType);
            if (dLen > 0) addHexRows(d, dLen);
            break;
        }

        i += recLen + 1;
    }
}

static void store_raw(BLEDevice& dev, esp_ble_gap_cb_param_t* sr) {
    uint8_t adv_len = sr->scan_rst.adv_data_len;
    uint8_t rsp_len = sr->scan_rst.scan_rsp_len;
    uint8_t total = adv_len + rsp_len;
    
    if (total > 0 && total <= 62) {
        if (total >= dev.payloadLength) {
            memcpy(dev.payload, sr->scan_rst.ble_adv, total);
            dev.payloadLength = total;
        }
    } else if (adv_len > 0 && adv_len <= 62 && adv_len > dev.payloadLength) {
        memcpy(dev.payload, sr->scan_rst.ble_adv, adv_len);
        dev.payloadLength = adv_len;
    }
}

static void bda_to_str(uint8_t* bda, char* str, size_t size) {
    snprintf(str, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

static void extract_name(esp_ble_gap_cb_param_t* sr, BLEDevice& dev) {
    if (dev.hasName) return;
    uint8_t nlen = 0;
    uint8_t* nd = esp_ble_resolve_adv_data(sr->scan_rst.ble_adv,
                                            ESP_BLE_AD_TYPE_NAME_CMPL, &nlen);
    if (!nd) nd = esp_ble_resolve_adv_data(sr->scan_rst.ble_adv,
                                           ESP_BLE_AD_TYPE_NAME_SHORT, &nlen);
    if (nd && nlen > 0 && nlen < 32) {
        memcpy(dev.name, nd, nlen);
        dev.name[nlen] = '\0';
        dev.hasName = true;
    }
}

static void process_scan_result(esp_ble_gap_cb_param_t* sr) {
    uint8_t* bda = sr->scan_rst.bda;

    char addrStr[18];
    bda_to_str(bda, addrStr, sizeof(addrStr));
    if (strlen(addrStr) < 12) return;

    if (inspView == BLEINSP_LOCATE && strlen(locateTargetAddress) > 0) {
        if (strcmp(addrStr, locateTargetAddress) != 0) return;
    } else if ((int)bleInspectorDevices.size() >= MAX_DEVICES) {
        return;
    }

    for (auto& dev : bleInspectorDevices) {
        if (strcmp(dev.address, addrStr) != 0) continue;
        dev.rssi = sr->scan_rst.rssi;
        dev.lastSeen = millis();
        dev.advType = sr->scan_rst.ble_evt_type;
        dev.addrType = sr->scan_rst.ble_addr_type;
        memcpy(dev.bdAddr, bda, 6);
        store_raw(dev, sr);
        extract_name(sr, dev);
        if (inspView != BLEINSP_LOCATE)
            std::sort(bleInspectorDevices.begin(), bleInspectorDevices.end(),
                      [](const BLEDevice& a, const BLEDevice& b)
                      { return a.rssi > b.rssi; });
        return;
    }

    BLEDevice dev = {};
    strncpy(dev.address, addrStr, 17);
    dev.address[17] = '\0';
    memcpy(dev.bdAddr, bda, 6);
    dev.rssi = sr->scan_rst.rssi;
    dev.lastSeen = millis();
    dev.advType = sr->scan_rst.ble_evt_type;
    dev.addrType = sr->scan_rst.ble_addr_type;
    store_raw(dev, sr);
    strcpy(dev.name, "Unknown");
    dev.hasName = false;
    extract_name(sr, dev);

    bleInspectorDevices.push_back(dev);
    if (inspView != BLEINSP_LOCATE)
        std::sort(bleInspectorDevices.begin(), bleInspectorDevices.end(),
                  [](const BLEDevice& a, const BLEDevice& b)
                  { return a.rssi > b.rssi; });
    needsRedraw = true;
}

static void ble_inspector_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            isScanning = true;
            esp_ble_gap_start_scanning(scanDuration);
            lastScanTime = millis();
        }
        break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
            isScanning = false;
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
            isScanning = (inspView == BLEINSP_LOCATE);
            if (inspView == BLEINSP_LOCATE) esp_ble_gap_start_scanning(scanDuration);
            break;
        default: break;
        }
        break;
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        isScanning = false;
        scanCompleted = true;
        break;
    default: break;
    }
}

static void drawDetail() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x8_tr);

    for (int i = 0; i < VISIBLE; i++) {
        int rowIdx = detailScrollOffset + i;
        if (rowIdx >= numRows) break;
        u8g2.drawStr(0, 10 + i * 10, detailRows[rowIdx]);
    }

    if (detailScrollOffset > 0)
        u8g2.drawTriangle(124, 2, 120, 8, 128, 8);
    if (detailScrollOffset + VISIBLE < numRows)
        u8g2.drawTriangle(124, 58, 120, 52, 128, 52);

    u8g2.drawStr(0, 60, "L=Back SEL=Exit R=Locate");
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

static void drawLocate() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x8_tr);

    if (currentIndex >= (int)bleInspectorDevices.size()) {
        u8g2.drawStr(0, 30, "Device lost");
        u8g2.sendBuffer();
        displayMirrorSend(u8g2);
        return;
    }

    const BLEDevice& dev = bleInspectorDevices[currentIndex];
    char buf[32];

    char maskedName[33];
    maskName(dev.hasName ? dev.name : "Unknown", maskedName, sizeof(maskedName) - 1);
    snprintf(buf, sizeof(buf), "%.22s", maskedName);
    u8g2.drawStr(0, 8, buf);

    char maskedMAC[18];
    maskMAC(dev.address, maskedMAC);
    u8g2.drawStr(0, 17, maskedMAC);

    u8g2.setFont(u8g2_font_7x13B_tr);
    snprintf(buf, sizeof(buf), "RSSI: %d dBm", dev.rssi);
    u8g2.drawStr(0, 30, buf);

    u8g2.setFont(u8g2_font_5x8_tr);
    int rssiClamped = constrain(dev.rssi, -100, -40);
    int signalLevel = map(rssiClamped, -100, -40, 0, 5);

    const char* qual;
    if      (signalLevel >= 5) qual = "EXCELLENT";
    else if (signalLevel >= 4) qual = "VERY GOOD";
    else if (signalLevel >= 3) qual = "GOOD";
    else if (signalLevel >= 2) qual = "FAIR";
    else if (signalLevel >= 1) qual = "WEAK";
    else                       qual = "VERY WEAK";

    snprintf(buf, sizeof(buf), "Signal: %s", qual);
    u8g2.drawStr(0, 40, buf);

    const int bw = 12, bsp = 5;
    int startX = (128 - (bw * 5 + bsp * 4)) / 2;
    for (int i = 0; i < 5; i++) {
        int bh = 8 + i * 2;
        int x = startX + i * (bw + bsp);
        int y = 54 - bh;
        if (i < signalLevel) u8g2.drawBox(x, y, bw, bh);
        else                  u8g2.drawFrame(x, y, bw, bh);
    }

    u8g2.drawStr(0, 60, "L=Back SEL=Exit");
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

void bleInspectorSetup() {
    bleInspectorDevices.clear();
    bleInspectorDevices.reserve(MAX_DEVICES);
    currentIndex = listStartIndex = 0;
    inspView = BLEINSP_LIST;
    numRows = detailScrollOffset = 0;
    memset(locateTargetAddress, 0, sizeof(locateTargetAddress));
    lastButtonPress = 0;
    isScanning = true;
    scanCompleted = false;
    needsRedraw = true;
    lastDeviceCount = 0;
    lastLocateUpdate = lastCountdownUpd = 0;
    wasScanning = false;

    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "BLE Inspector");
    u8g2.drawStr(0, 22, "Scanning for BLE...");
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(0, 62, "Press SEL to exit");
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);

    initBLE();

    esp_ble_gap_register_callback(ble_inspector_gap_cb);
    esp_ble_gap_set_scan_params(&ble_inspector_scan_params);
    bleInitialized = true;

    pinMode(BTN_UP,    INPUT_PULLUP);
    pinMode(BTN_DOWN,  INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(BTN_BACK,  INPUT_PULLUP);
}

void bleInspectorLoop() {
    if (!bleInitialized) return;

    unsigned long now = millis();

    if (isScanning && inspView == BLEINSP_LIST) {
        if (lastDeviceCount != (int)bleInspectorDevices.size() || wasScanning != isScanning) {
            lastDeviceCount = (int)bleInspectorDevices.size();
            wasScanning = isScanning;
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(0, 10, "BLE Inspector");
            u8g2.drawStr(0, 22, "Scanning for BLE...");
            char cs[32];
            snprintf(cs, sizeof(cs), "%d/%d devices",
                     (int)bleInspectorDevices.size(), MAX_DEVICES);
            u8g2.drawStr(0, 34, cs);
            const int bw = 120, bx = (128 - bw) / 2;
            u8g2.drawFrame(bx, 42, bw, 10);
            int fill = ((int)bleInspectorDevices.size() * (bw - 4)) / MAX_DEVICES;
            if (fill > 0) u8g2.drawBox(bx + 2, 44, fill, 6);
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

    unsigned long effInterval = scanInterval;
    uint32_t effDuration = scanDuration;
    if (bleInspectorDevices.empty() && isContinuousScanEnabled()) {
        effInterval = 500;
        effDuration = 3;
    }

    if (!isScanning && scanCompleted && now - lastScanTime > effInterval &&
        inspView == BLEINSP_LIST) {
        if ((int)bleInspectorDevices.size() >= MAX_DEVICES) {
            std::sort(bleInspectorDevices.begin(), bleInspectorDevices.end(),
                      [](const BLEDevice& a, const BLEDevice& b) {
                          return a.lastSeen != b.lastSeen
                                 ? a.lastSeen < b.lastSeen : a.rssi < b.rssi;
                      });
            bleInspectorDevices.erase(bleInspectorDevices.begin(),
                             bleInspectorDevices.begin() + MAX_DEVICES / 4);
            currentIndex = listStartIndex = 0;
        }
        scanCompleted = false;
        isScanning = true;
        esp_ble_gap_start_scanning(effDuration);
        lastScanTime = now;
        return;
    }

    if (now - lastButtonPress > debounceTime) {
        if (inspView == BLEINSP_LOCATE) {
            if (digitalRead(BTN_BACK) == LOW) {
                inspView = BLEINSP_DETAIL;
                memset(locateTargetAddress, 0, sizeof(locateTargetAddress));
                if (isScanning) { esp_ble_gap_stop_scanning(); isScanning = false; }
                lastButtonPress = now;
                needsRedraw = true;
            }

        } else if (inspView == BLEINSP_DETAIL) {
            if (digitalRead(BTN_UP) == LOW && detailScrollOffset > 0) {
                detailScrollOffset--;
                lastButtonPress = now;
                needsRedraw = true;
            } else if (digitalRead(BTN_DOWN) == LOW &&
                       detailScrollOffset + VISIBLE < numRows) {
                detailScrollOffset++;
                lastButtonPress = now;
                needsRedraw = true;
            } else if (digitalRead(BTN_RIGHT) == LOW && !bleInspectorDevices.empty()) {
                inspView = BLEINSP_LOCATE;
                strncpy(locateTargetAddress, bleInspectorDevices[currentIndex].address,
                        sizeof(locateTargetAddress) - 1);
                if (!isScanning) { isScanning = true; esp_ble_gap_start_scanning(scanDuration); }
                lastButtonPress = now;
                lastLocateUpdate = now;
                needsRedraw = true;
            } else if (digitalRead(BTN_BACK) == LOW) {
                inspView = BLEINSP_LIST;
                detailScrollOffset = 0;
                if (isScanning) { esp_ble_gap_stop_scanning(); isScanning = false; }
                lastButtonPress = now;
                needsRedraw = true;
            }

        } else if (scanCompleted) {
            if (digitalRead(BTN_UP) == LOW && currentIndex > 0) {
                --currentIndex;
                if (currentIndex < listStartIndex) --listStartIndex;
                lastButtonPress = now;
                needsRedraw = true;
            } else if (digitalRead(BTN_DOWN) == LOW &&
                       currentIndex < (int)bleInspectorDevices.size() - 1) {
                ++currentIndex;
                if (currentIndex >= listStartIndex + 5) ++listStartIndex;
                lastButtonPress = now;
                needsRedraw = true;
            } else if (digitalRead(BTN_RIGHT) == LOW && !bleInspectorDevices.empty()) {
                inspView = BLEINSP_DETAIL;
                detailScrollOffset = 0;
                buildDetailRows(bleInspectorDevices[currentIndex]);
                if (isScanning) { esp_ble_gap_stop_scanning(); isScanning = false; }
                lastButtonPress = now;
                needsRedraw = true;
            }
        }
    }

    if (bleInspectorDevices.empty()) {
        currentIndex = listStartIndex = 0;
        inspView = BLEINSP_LIST;
    } else {
        currentIndex = constrain(currentIndex, 0, (int)bleInspectorDevices.size() - 1);
        listStartIndex = constrain(listStartIndex, 0,
                                   max(0, (int)bleInspectorDevices.size() - 5));
    }

    if (inspView != BLEINSP_LIST && now - lastLocateUpdate >= locateUpdateInterval) {
        lastLocateUpdate = now;
        if (inspView == BLEINSP_DETAIL) {
            int saved = detailScrollOffset;
            buildDetailRows(bleInspectorDevices[currentIndex]);
            detailScrollOffset = constrain(saved, 0, max(0, numRows - VISIBLE));
        }
        needsRedraw = true;
    }

    if (bleInspectorDevices.empty() && scanCompleted && !isScanning &&
        now - lastCountdownUpd >= 1000) {
        lastCountdownUpd = now;
        needsRedraw = true;
    }

    if (!needsRedraw) return;
    needsRedraw = false;

    if (inspView == BLEINSP_LOCATE) { drawLocate(); return; }
    if (inspView == BLEINSP_DETAIL) { drawDetail(); return; }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);

    if (bleInspectorDevices.empty()) {
        u8g2.drawStr(0, 10, "BLE Inspector");
        if (isContinuousScanEnabled()) {
            u8g2.drawStr(0, 25, "Scanning...");
        } else {
            u8g2.drawStr(0, 25, "No devices found");
            u8g2.setFont(u8g2_font_5x8_tr);
            unsigned long tl = (effInterval - (now - lastScanTime)) / 1000;
            char ts[32];
            snprintf(ts, sizeof(ts), "Rescan in %lus", tl);
            u8g2.drawStr(0, 38, ts);
        }
        u8g2.setFont(u8g2_font_5x8_tr);
        u8g2.drawStr(0, 60, "Press SEL to exit");
    } else {
        char header[32];
        snprintf(header, sizeof(header), "BLE Inspector: %d/%d",
                 (int)bleInspectorDevices.size(), MAX_DEVICES);
        u8g2.drawStr(0, 10, header);

        for (int i = 0; i < 5; i++) {
            int idx = listStartIndex + i;
            if (idx >= (int)bleInspectorDevices.size()) break;
            const BLEDevice& d = bleInspectorDevices[idx];
            if (idx == currentIndex) u8g2.drawStr(0, 20 + i * 10, ">");
            char maskedName[33];
            maskName(d.hasName ? d.name : "Unknown", maskedName, sizeof(maskedName) - 1);
            char line[32];
            snprintf(line, sizeof(line), "%.8s | RSSI %d", maskedName, d.rssi);
            u8g2.drawStr(10, 20 + i * 10, line);
        }
    }

    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}