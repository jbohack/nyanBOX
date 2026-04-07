/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2026 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/packet_logger.h"
#include "../include/radio_manager.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "../include/setting.h"
#include "esp_wifi.h"
#include <string.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

namespace {

struct PacketEntry {
    unsigned long timestamp;
    uint8_t channel;
    uint8_t frameType;
    uint8_t frameSubtype;
    uint8_t srcMAC[6];
    uint8_t dstMAC[6];
    uint8_t bssid[6];
    int8_t  rssi;
    uint16_t length;
};

const int RING_SIZE = 20;
PacketEntry ring[RING_SIZE];
volatile int ringHead = 0;
volatile uint32_t totalPackets = 0;
volatile uint32_t mgmtCount = 0;
volatile uint32_t dataCount = 0;

uint8_t currentChannel = 1;
bool autoHop = true;
bool paused = false;
unsigned long lastHop = 0;
unsigned long lastDraw = 0;

const unsigned long HOP_INTERVAL = 500;
const unsigned long DRAW_INTERVAL = 200;

static bool needsRedraw = true;
static uint32_t lastDisplayedTotal = 0;
static uint8_t lastDisplayedChannel = 0;
static bool lastPaused = false;

const char* subtypeShort(uint8_t type, uint8_t subtype) {
    if (type == 0) {
        switch (subtype) {
            case 0:  return "AQ";
            case 1:  return "AR";
            case 4:  return "PQ";
            case 5:  return "PR";
            case 8:  return "Bc";
            case 10: return "Di";
            case 11: return "Au";
            case 12: return "DA";
            case 13: return "Ac";
            default: return "M?";
        }
    } else if (type == 1) {
        switch (subtype) {
            case 9:  return "Bk";
            case 11: return "RT";
            case 12: return "CT";
            case 13: return "Ak";
            default: return "C?";
        }
    } else if (type == 2) {
        switch (subtype) {
            case 0:  return "Dt";
            case 4:  return "Nl";
            case 8:  return "QD";
            case 12: return "QN";
            default: return "D?";
        }
    }
    return "??";
}

const char* subtypeName(uint8_t type, uint8_t subtype) {
    if (type == 0) {
        switch (subtype) {
            case 0:  return "AssocReq";
            case 1:  return "AssocResp";
            case 4:  return "ProbeReq";
            case 5:  return "ProbeResp";
            case 8:  return "Beacon";
            case 10: return "Disassoc";
            case 11: return "Auth";
            case 12: return "Deauth";
            case 13: return "Action";
            default: return "MgmtOther";
        }
    } else if (type == 1) {
        switch (subtype) {
            case 9:  return "BlockAck";
            case 11: return "RTS";
            case 12: return "CTS";
            case 13: return "ACK";
            default: return "CtrlOther";
        }
    } else if (type == 2) {
        switch (subtype) {
            case 0:  return "Data";
            case 4:  return "Null";
            case 8:  return "QoSData";
            case 12: return "QoSNull";
            default: return "DataOther";
        }
    }
    return "Unknown";
}

const char* typeName(uint8_t type) {
    switch (type) {
        case 0: return "MGMT";
        case 1: return "CTRL";
        case 2: return "DATA";
        default: return "UNK";
    }
}

void macStr(const uint8_t *mac, char *buf) {
    snprintf(buf, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void macShort(const uint8_t *mac, char *buf) {
    snprintf(buf, 9, "%02X%02X%02X", mac[3], mac[4], mac[5]);
}

void IRAM_ATTR packetSniffer(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (paused) return;

    const wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    const uint8_t *frame = pkt->payload;
    uint16_t len = pkt->rx_ctrl.sig_len;
    if (len < 24) return;

    uint8_t fc0 = frame[0];
    uint8_t ftype = (fc0 >> 2) & 0x03;
    uint8_t fsubtype = (fc0 >> 4) & 0x0F;

    PacketEntry entry;
    entry.timestamp = millis();
    entry.channel = currentChannel;
    entry.frameType = ftype;
    entry.frameSubtype = fsubtype;
    entry.rssi = pkt->rx_ctrl.rssi;
    entry.length = len;
    memcpy(entry.dstMAC, &frame[4], 6);
    memcpy(entry.srcMAC, &frame[10], 6);
    memcpy(entry.bssid, &frame[16], 6);

    ring[ringHead] = entry;
    ringHead = (ringHead + 1) % RING_SIZE;
    totalPackets++;

    if (ftype == 0) mgmtCount++;
    else if (ftype == 2) dataCount++;

    char src[18], dst[18], bss[18];
    macStr(entry.srcMAC, src);
    macStr(entry.dstMAC, dst);
    macStr(entry.bssid, bss);

    Serial.printf("%lu,%u,%s,%s,%s,%s,%s,%d,%u\n",
        entry.timestamp, entry.channel,
        typeName(ftype), subtypeName(ftype, fsubtype),
        src, dst, bss, entry.rssi, len);
}

void drawScreen() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x8_tr);

    char header[32];
    if (paused) {
        snprintf(header, sizeof(header), "PAUSED  Ch:%02d #%lu", currentChannel, (unsigned long)totalPackets);
    } else if (autoHop) {
        snprintf(header, sizeof(header), "Pkt Log Ch:%02d #%lu", currentChannel, (unsigned long)totalPackets);
    } else {
        snprintf(header, sizeof(header), "LOCKED  Ch:%02d #%lu", currentChannel, (unsigned long)totalPackets);
    }
    u8g2.drawStr(0, 8, header);
    u8g2.drawHLine(0, 10, 128);

    for (int i = 0; i < 5; i++) {
        int idx = (ringHead - 1 - i + RING_SIZE) % RING_SIZE;
        if (ring[idx].timestamp == 0) continue;

        const PacketEntry &e = ring[idx];
        char srcShort[9];
        if (privacyModeEnabled) {
            snprintf(srcShort, sizeof(srcShort), "XX:XX:XX");
        } else {
            macShort(e.srcMAC, srcShort);
        }

        char line[28];
        snprintf(line, sizeof(line), "%-2s %s %3d %4u",
                 subtypeShort(e.frameType, e.frameSubtype),
                 srcShort, e.rssi, e.length);
        u8g2.drawStr(0, 20 + (i * 8), line);
    }

    if (paused) {
        char stats[32];
        snprintf(stats, sizeof(stats), "M:%lu D:%lu",
                 (unsigned long)mgmtCount, (unsigned long)dataCount);
        u8g2.drawStr(0, 62, stats);
    } else {
        u8g2.drawStr(0, 62, "U/D=Ch R=Pause L=Back");
    }

    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

}

void packetLoggerSetup() {
    memset(ring, 0, sizeof(ring));
    ringHead = 0;
    totalPackets = 0;
    mgmtCount = 0;
    dataCount = 0;
    currentChannel = 1;
    autoHop = true;
    paused = false;
    lastHop = 0;
    lastDraw = 0;
    needsRedraw = true;
    lastDisplayedTotal = 0;
    lastDisplayedChannel = 0;
    lastPaused = false;

    initWiFi(WIFI_MODE_STA);

    wifi_promiscuous_filter_t filter;
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(packetSniffer);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);

    Serial.println();
    Serial.println("# nyanBOX Packet Logger v1.0");
    Serial.println("# timestamp_ms,channel,type,subtype,src_mac,dst_mac,bssid,rssi_dbm,length");

    pinMode(BUTTON_PIN_UP, INPUT_PULLUP);
    pinMode(BUTTON_PIN_DOWN, INPUT_PULLUP);
    pinMode(BUTTON_PIN_LEFT, INPUT_PULLUP);
    pinMode(BUTTON_PIN_RIGHT, INPUT_PULLUP);

    drawScreen();
}

void packetLoggerLoop() {
    unsigned long now = millis();

    static bool upPrev = false, downPrev = false, rightPrev = false;
    bool upNow = digitalRead(BUTTON_PIN_UP) == LOW;
    bool downNow = digitalRead(BUTTON_PIN_DOWN) == LOW;
    bool rightNow = digitalRead(BUTTON_PIN_RIGHT) == LOW;

    if (rightNow && !rightPrev) {
        paused = !paused;
        needsRedraw = true;
        delay(200);
    }

    if (upNow && !upPrev && !paused) {
        if (autoHop) {
            autoHop = false;
        } else {
            currentChannel++;
            if (currentChannel > 13) { currentChannel = 1; autoHop = true; }
            esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
        }
        needsRedraw = true;
        delay(200);
    }

    if (downNow && !downPrev && !paused) {
        if (autoHop) {
            autoHop = false;
        } else {
            currentChannel--;
            if (currentChannel < 1) { currentChannel = 13; autoHop = true; }
            esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
        }
        needsRedraw = true;
        delay(200);
    }

    upPrev = upNow;
    downPrev = downNow;
    rightPrev = rightNow;

    if (autoHop && !paused && (now - lastHop >= HOP_INTERVAL)) {
        lastHop = now;
        currentChannel++;
        if (currentChannel > 13) currentChannel = 1;
        esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    }

    if (totalPackets != lastDisplayedTotal ||
        currentChannel != lastDisplayedChannel ||
        paused != lastPaused) {
        needsRedraw = true;
        lastDisplayedTotal = totalPackets;
        lastDisplayedChannel = currentChannel;
        lastPaused = paused;
    }

    if (needsRedraw && (now - lastDraw >= DRAW_INTERVAL)) {
        needsRedraw = false;
        lastDraw = now;
        drawScreen();
    }
}
