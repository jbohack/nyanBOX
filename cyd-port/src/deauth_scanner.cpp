/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2026 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/deauth_scanner.h"
#include "../include/radio_manager.h"
#include "../include/sleep_manager.h"
#include "../include/pindefs.h"
#include "../include/display_mirror.h"
#include "../include/setting.h"
#include <U8g2lib.h>
#include "esp_wifi.h"
#include "esp_event.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

#define BTN_UP BUTTON_PIN_UP
#define BTN_DOWN BUTTON_PIN_DOWN
#define BTN_RIGHT BUTTON_PIN_RIGHT
#define BTN_BACK BUTTON_PIN_LEFT

#define CHANNEL_MIN 1
#define CHANNEL_MAX 13
#define CHANNEL_HOP_INTERVAL 1500

static bool useMainChannels = true;
static const uint8_t mainChannels[] = {1, 6, 11};
static const int numMainChannels = sizeof(mainChannels) / sizeof(mainChannels[0]);
static int currentChannelIndex = 0;
static uint8_t currentChannel = 1;
static uint16_t deauthCount = 0;
static uint16_t totalDeauths = 0;

static uint8_t lastDeauthMAC[6] = {0};
static uint8_t lastDeauthChannel = 0;
static int8_t displayedRSSI = 0;
static int32_t rssiAccum = 0;
static uint16_t rssiCount = 0;
static bool macSeen = false;

static unsigned long lastChannelHop = 0;
static unsigned long lastButtonPress = 0;
const unsigned long debounceTime = 200;

static bool needsRedraw = true;
static uint16_t lastDisplayedDeauthCount = 0;
static uint16_t lastDisplayedTotalDeauths = 0;
static uint8_t lastDisplayedChannel = 0;
static bool lastDisplayedMode = true;
static bool lastMacSeen = false;
static unsigned long lastPeriodicUpdate = 0;
const unsigned long periodicUpdateInterval = 1000;
static unsigned long lastRSSIUpdate = 0;

// Bypass sanity checks for raw 802.11 frames
extern "C" int ieee80211_raw_frame_sanity_check(int32_t, int32_t, int32_t) {
    return 0;
}

typedef struct {
    uint16_t frame_ctrl;
    uint16_t duration_id;
    uint8_t addr1[6];
    uint8_t addr2[6];
    uint8_t addr3[6];
    uint16_t sequence_ctrl;
} __attribute__((packed)) wifi_ieee80211_mac_hdr_t;

static void fmtCount(char *out, size_t sz, unsigned long val) {
    if (val < 1000) {
        snprintf(out, sz, "%lu", val);
    } else {
        snprintf(out, sz, "%.2fk", val / 1000.0);
    }
}

void formatMAC(char *output, const uint8_t *mac) {
    if (!macSeen) {
        snprintf(output, 18, "N/A");
    } else {
        snprintf(output, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

void packetSniffer(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;

    wifi_promiscuous_pkt_t *packet = (wifi_promiscuous_pkt_t *)buf;
    if (packet->rx_ctrl.sig_len < sizeof(wifi_ieee80211_mac_hdr_t)) return;

    wifi_ieee80211_mac_hdr_t *hdr = (wifi_ieee80211_mac_hdr_t *)packet->payload;
    uint16_t fc = hdr->frame_ctrl;

    if ((fc & 0xFC) == 0xC0) {  // Deauthentication frame filter
        memcpy(lastDeauthMAC, hdr->addr2, 6);
        lastDeauthChannel = currentChannel;
        rssiAccum += packet->rx_ctrl.rssi;
        rssiCount++;
        macSeen = true;
        deauthCount++;
        totalDeauths++;
        needsRedraw = true;
    }
}

void deauthScannerSetup() {
    initWiFi(WIFI_MODE_STA);

    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT
    };

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(packetSniffer);
    esp_wifi_set_promiscuous(true);
    
    useMainChannels = true;
    currentChannelIndex = 0;
    currentChannel = mainChannels[currentChannelIndex];
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);

    deauthCount = 0;
    totalDeauths = 0;
    macSeen = false;
    memset(lastDeauthMAC, 0, sizeof(lastDeauthMAC));
    lastDeauthChannel = 0;
    displayedRSSI = 0;
    rssiAccum = 0;
    rssiCount = 0;
    lastChannelHop = millis();
    lastButtonPress = 0;

    needsRedraw = true;
    lastDisplayedDeauthCount = 0;
    lastDisplayedTotalDeauths = 0;
    lastDisplayedChannel = 0;
    lastDisplayedMode = true;
    lastMacSeen = false;
    lastPeriodicUpdate = millis();
    lastRSSIUpdate = millis();

    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(BTN_BACK, INPUT_PULLUP);

    u8g2.begin();
}

void renderDeauthStats() {
    char headerStr[32];
    char macStr[18];

    const char* modeText = useMainChannels ? "Main CH" : "All CH";
    snprintf(headerStr, sizeof(headerStr), "CH:%2d | %s", currentChannel, modeText);
    formatMAC(macStr, lastDeauthMAC);

    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_helvR08_tr);
    int headerWidth = u8g2.getUTF8Width(headerStr);
    u8g2.drawStr((128 - headerWidth) / 2, 10, headerStr);

    char curBuf[10], totBuf[10];
    fmtCount(curBuf, sizeof(curBuf), deauthCount);
    fmtCount(totBuf, sizeof(totBuf), totalDeauths);
    char countsStr[32];
    snprintf(countsStr, sizeof(countsStr), "Cur:%s  Tot:%s", curBuf, totBuf);
    int countsWidth = u8g2.getUTF8Width(countsStr);
    u8g2.drawStr((128 - countsWidth) / 2, 21, countsStr);

    u8g2.setFont(u8g2_font_5x8_tr);
    if (macSeen) {
        const char* macLabel = "Last MAC:";
        int macLabelWidth = u8g2.getUTF8Width(macLabel);
        u8g2.drawStr((128 - macLabelWidth) / 2, 31, macLabel);

        char maskedMAC[18];
        maskMAC(macStr, maskedMAC);
        char macChanStr[24];
        snprintf(macChanStr, sizeof(macChanStr), "%s CH%d", maskedMAC, lastDeauthChannel);
        int macChanWidth = u8g2.getUTF8Width(macChanStr);
        u8g2.drawStr((128 - macChanWidth) / 2, 41, macChanStr);

        char rssiStr[16];
        snprintf(rssiStr, sizeof(rssiStr), "RSSI: %d dBm", displayedRSSI);
        int rssiWidth = u8g2.getUTF8Width(rssiStr);
        u8g2.drawStr((128 - rssiWidth) / 2, 51, rssiStr);
    } else {
        const char* waitMsg = "Scanning for deauths...";
        int waitWidth = u8g2.getUTF8Width(waitMsg);
        u8g2.drawStr((128 - waitWidth) / 2, 41, waitMsg);
    }

    u8g2.setFont(u8g2_font_4x6_tr);
    const char* instruction = "LEFT=Mode  SEL=Exit";
    int instrWidth = u8g2.getUTF8Width(instruction);
    u8g2.drawStr((128 - instrWidth) / 2, 61, instruction);

    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

void hopChannel() {
    if (useMainChannels) {
        currentChannelIndex = (currentChannelIndex + 1) % numMainChannels;
        currentChannel = mainChannels[currentChannelIndex];
    } else {
        currentChannel++;
        if (currentChannel > CHANNEL_MAX) currentChannel = CHANNEL_MIN;
    }

    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    deauthCount = 0;
    needsRedraw = true;
}

void deauthScannerLoop() {
    unsigned long now = millis();

    if (now - lastButtonPress > debounceTime) {
        if (digitalRead(BTN_BACK) == LOW) {
            useMainChannels = !useMainChannels;

            if (useMainChannels) {
                currentChannelIndex = 0;
                currentChannel = mainChannels[currentChannelIndex];
            } else {
                currentChannel = CHANNEL_MIN;
            }

            esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
            deauthCount = 0;
            lastButtonPress = now;
            needsRedraw = true;
        }
    }

    if (now - lastChannelHop >= CHANNEL_HOP_INTERVAL) {
        hopChannel();
        lastChannelHop = now;
    }

    if (deauthCount != lastDisplayedDeauthCount) {
        lastDisplayedDeauthCount = deauthCount;
        needsRedraw = true;
    }

    if (totalDeauths != lastDisplayedTotalDeauths) {
        lastDisplayedTotalDeauths = totalDeauths;
        needsRedraw = true;
    }

    if (currentChannel != lastDisplayedChannel) {
        lastDisplayedChannel = currentChannel;
        needsRedraw = true;
    }

    if (useMainChannels != lastDisplayedMode) {
        lastDisplayedMode = useMainChannels;
        needsRedraw = true;
    }

    if (macSeen != lastMacSeen) {
        lastMacSeen = macSeen;
        needsRedraw = true;
    }

    if (now - lastPeriodicUpdate >= periodicUpdateInterval) {
        lastPeriodicUpdate = now;
        needsRedraw = true;
    }

    if (now - lastRSSIUpdate >= periodicUpdateInterval) {
        if (rssiCount > 0) {
            displayedRSSI = (int8_t)(rssiAccum / rssiCount);
            rssiAccum = 0;
            rssiCount = 0;
        }
        lastRSSIUpdate = now;
    }

    if (needsRedraw) {
        renderDeauthStats();
        needsRedraw = false;
    }
}