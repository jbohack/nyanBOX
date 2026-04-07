/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/beacon_spam.h"
#include "../include/radio_manager.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "../include/setting.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include <string.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

// Disable frame sanity checks
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    return 0;
}

namespace {

#define BTN_UP BUTTON_PIN_UP
#define BTN_DOWN BUTTON_PIN_DOWN
#define BTN_RIGHT BUTTON_PIN_RIGHT
#define BTN_BACK BUTTON_PIN_LEFT

const uint8_t channels[] = {1, 6, 11};

const char ssids[] PROGMEM = {
  "Mom Use This One\n"
  "Abraham Linksys\n"
  "Benjamin FrankLAN\n"
  "Martin Router King\n"
  "John Wilkes Bluetooth\n"
  "Pretty Fly for a Wi-Fi\n"
  "Bill Wi the Science Fi\n"
  "I Believe Wi Can Fi\n"
  "Tell My Wi-Fi Love Her\n"
  "No More Mister Wi-Fi\n"
  "Subscribe to TalkingSasquach\n"
  "jbohack was here\n"
  "zr_crackiin was here\n"
  "nyandevices.com\n"
  "LAN Solo\n"
  "The LAN Before Time\n"
  "Silence of the LANs\n"
  "House LANister\n"
  "Winternet Is Coming\n"
  "Ping's Landing\n"
  "The Ping in the North\n"
  "This LAN Is My LAN\n"
  "Get Off My LAN\n"
  "The Promised LAN\n"
  "The LAN Down Under\n"
  "FBI Surveillance Van 4\n"
  "Area 51 Test Site\n"
  "Drive-By Wi-Fi\n"
  "Planet Express\n"
  "Wu Tang LAN\n"
  "Darude LANstorm\n"
  "Never Gonna Give You Up\n"
  "Hide Yo Kids, Hide Yo Wi-Fi\n"
  "Loading…\n"
  "Searching…\n"
  "VIRUS.EXE\n"
  "Virus-Infected Wi-Fi\n"
  "Starbucks Wi-Fi\n"
  "Text ###-#### for Password\n"
  "Yell ____ for Password\n"
  "The Password Is 1234\n"
  "Free Public Wi-Fi\n"
  "No Free Wi-Fi Here\n"
  "Get Your Own Damn Wi-Fi\n"
  "It Hurts When IP\n"
  "Dora the Internet Explorer\n"
  "404 Wi-Fi Unavailable\n"
  "Porque-Fi\n"
  "Titanic Syncing\n"
  "Test Wi-Fi Please Ignore\n"
  "Drop It Like It's Hotspot\n"
  "Life in the Fast LAN\n"
  "The Creep Next Door\n"
  "Ye Olde Internet\n"
  "Lan Before Time\n"
  "Lan Of The Lost\n"
};

uint8_t macAddr[6];
uint8_t wifi_channel = 1;
uint32_t currentTime = 0;

void randomMac() {
  for (int i = 0; i < 6; i++) macAddr[i] = random(256);
  macAddr[0] |= 0x02;
  macAddr[0] &= 0xFE;
}

// Build beacon frame dynamically so IE tags are at the correct offsets
// regardless of SSID length. The old static templates had tags at fixed
// positions which broke when the SSID was shorter than 32 bytes.
void sendBeaconFrame(const char *ssid, uint8_t ssidLen, uint8_t channel, bool wpa2) {
  uint8_t frame[128];
  memset(frame, 0, sizeof(frame));
  int p = 0;

  frame[p++] = 0x80; frame[p++] = 0x00;
  frame[p++] = 0x00; frame[p++] = 0x00;
  memset(&frame[p], 0xFF, 6); p += 6;
  randomMac();
  memcpy(&frame[p], macAddr, 6); p += 6;
  memcpy(&frame[p], macAddr, 6); p += 6;
  uint16_t seq = random(4096) << 4;
  frame[p++] = seq & 0xFF;
  frame[p++] = (seq >> 8) & 0xFF;

  uint64_t ts = (uint64_t)esp_timer_get_time();
  memcpy(&frame[p], &ts, 8); p += 8;
  frame[p++] = 0x64; frame[p++] = 0x00;
  frame[p++] = wpa2 ? 0x11 : 0x01;
  frame[p++] = 0x04;

  // SSID
  frame[p++] = 0x00;
  frame[p++] = ssidLen;
  memcpy(&frame[p], ssid, ssidLen); p += ssidLen;

  // Supported Rates
  frame[p++] = 0x01; frame[p++] = 0x08;
  frame[p++] = 0x82; frame[p++] = 0x84;
  frame[p++] = 0x8b; frame[p++] = 0x96;
  frame[p++] = 0x0c; frame[p++] = 0x18;
  frame[p++] = 0x30; frame[p++] = 0x48;

  // DS Parameter Set
  frame[p++] = 0x03; frame[p++] = 0x01; frame[p++] = channel;

  // RSN (WPA2)
  if (wpa2) {
    const uint8_t rsn[] = {
      0x30, 0x18, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02,
      0x02, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x00, 0x0f,
      0xac, 0x02, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02,
      0x00, 0x00
    };
    memcpy(&frame[p], rsn, sizeof(rsn)); p += sizeof(rsn);
  }

  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_80211_tx(WIFI_IF_AP, frame, p, false);
}

void nextChannel() {
  static uint8_t channelIndex = 0;
  wifi_channel = channels[channelIndex];
  channelIndex = (channelIndex + 1) % (sizeof(channels) / sizeof(channels[0]));
  esp_wifi_set_channel(wifi_channel, WIFI_SECOND_CHAN_NONE);
}

enum BeaconSpamMode { BEACON_SPAM_MENU, BEACON_SPAM_CLONE_ALL, BEACON_SPAM_CLONE_SELECTED, BEACON_SPAM_CUSTOM, BEACON_SPAM_RANDOM, BEACON_SPAM_SCANNING };
BeaconSpamMode beaconSpamMode = BEACON_SPAM_MENU;
int menuSelection = 0;
int ssidIndex = 0;

struct ClonedSSID {
    char ssid[33];
    uint8_t channel;
    bool selected;
};
std::vector<ClonedSSID> scannedSSIDs;
std::vector<ClonedSSID> oldSSIDList;
const int MAX_CLONE_SSIDS = 50;
const unsigned long SCAN_INTERVAL = 30000;
const unsigned long SCAN_DURATION = 8000;
const unsigned long DISPLAY_UPDATE_INTERVAL = 100;
unsigned long beacon_lastScanTime = 0;
unsigned long beacon_scanStartTime = 0;
unsigned long beacon_lastDisplayUpdate = 0;
uint16_t beacon_lastApCount = 0;
bool beacon_isScanning = false;
BeaconSpamMode returnToMode = BEACON_SPAM_MENU;

static bool needsRedraw = true;
static int lastMenuSelection = -1;
static int lastSSIDIndex = -1;
static int lastScannedSSIDsSize = 0;
static bool lastSSIDSelectedState = false;
static BeaconSpamMode lastBeaconSpamMode = BEACON_SPAM_MENU;

void drawBeaconSpamMenu() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, "Beacon Spam Mode:");
  u8g2.drawStr(0, 22, menuSelection == 0 ? "> Clone All" : "  Clone All");
  u8g2.drawStr(0, 32, menuSelection == 1 ? "> Clone Selected" : "  Clone Selected");
  u8g2.drawStr(0, 42, menuSelection == 2 ? "> Custom" : "  Custom");
  u8g2.drawStr(0, 52, menuSelection == 3 ? "> Random" : "  Random");
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.drawStr(0, 64, "U/D=Move R=OK SEL=Exit");
  u8g2.sendBuffer();
  displayMirrorSend(u8g2);
}

void drawSSIDList() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 12, "Select SSID to clone");
    if (!scannedSSIDs.empty()) {
        int idx = ssidIndex;
        char maskedSSID[33];
        maskName(scannedSSIDs[idx].ssid, maskedSSID, sizeof(maskedSSID) - 1);
        char line1[32];
        snprintf(line1, sizeof(line1), "%s  Ch:%d", maskedSSID, scannedSSIDs[idx].channel);
        u8g2.drawStr(0, 28, line1);
        u8g2.drawStr(0, 44, scannedSSIDs[idx].selected ? "[*] Selected" : "[ ] Not selected");
    } else {
        u8g2.drawStr(0, 30, "No SSIDs found");
    }
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(0, 62, "U/D=Move R=Toggle L=Back");
    u8g2.sendBuffer();
  displayMirrorSend(u8g2);
}

void processScanResults(unsigned long now) {
    uint16_t number = 0;
    esp_wifi_scan_get_ap_num(&number);

    if (number == 0) return;

    wifi_ap_record_t *ap_info = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * number);
    if (ap_info == NULL) return;

    memset(ap_info, 0, sizeof(wifi_ap_record_t) * number);

    uint16_t actual_number = number;
    esp_err_t err = esp_wifi_scan_get_ap_records(&actual_number, ap_info);

    if (err == ESP_OK) {
        for (int i = 0; i < actual_number && (int)scannedSSIDs.size() < MAX_CLONE_SSIDS; i++) {
            if (ap_info[i].ssid[0] != '\0') {
                bool found = false;
                for (const auto& existing : scannedSSIDs) {
                    if (strcmp(existing.ssid, (char*)ap_info[i].ssid) == 0 &&
                        existing.channel == ap_info[i].primary) {
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    ClonedSSID entry;
                    strncpy(entry.ssid, (char*)ap_info[i].ssid, sizeof(entry.ssid) - 1);
                    entry.ssid[sizeof(entry.ssid) - 1] = '\0';
                    entry.channel = ap_info[i].primary;
                    entry.selected = false;

                    for (const auto& old : oldSSIDList) {
                        if (strcmp(old.ssid, entry.ssid) == 0 && old.channel == entry.channel) {
                            entry.selected = old.selected;
                            break;
                        }
                    }
                    scannedSSIDs.push_back(entry);
                }
            }
        }
    }

    free(ap_info);
}

void startSSIDScan(BeaconSpamMode returnMode) {
    oldSSIDList = scannedSSIDs;
    scannedSSIDs.clear();
    beacon_isScanning = true;
    beacon_lastApCount = 0;
    beacon_scanStartTime = millis();
    beacon_lastDisplayUpdate = millis();
    returnToMode = returnMode;

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_mode(WIFI_MODE_STA);
    delay(100);

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 120,
                .max = 200
            }
        }
    };

    esp_wifi_scan_start(&scan_config, false);
    beaconSpamMode = BEACON_SPAM_SCANNING;
    needsRedraw = true;
}

void updateSSIDScan() {
    unsigned long now = millis();
    bool displayNeedsUpdate = false;

    uint16_t currentApCount = 0;
    esp_wifi_scan_get_ap_num(&currentApCount);

    if (currentApCount > beacon_lastApCount) {
        processScanResults(now);
        beacon_lastApCount = currentApCount;
        displayNeedsUpdate = true;
    }

    if (displayNeedsUpdate || (now - beacon_lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL)) {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(0, 10, "Scanning WiFi...");

        char countStr[32];
        snprintf(countStr, sizeof(countStr), "Found: %d networks", (int)scannedSSIDs.size());
        u8g2.drawStr(0, 25, countStr);

        int barWidth = 120;
        int barHeight = 10;
        int barX = 4;
        int barY = 35;
        u8g2.drawFrame(barX, barY, barWidth, barHeight);

        unsigned long elapsed = now - beacon_scanStartTime;
        int fillWidth = ((elapsed * (barWidth - 4)) / SCAN_DURATION);
        if (fillWidth > (barWidth - 4)) fillWidth = barWidth - 4;
        if (fillWidth > 0) {
            u8g2.drawBox(barX + 2, barY + 2, fillWidth, barHeight - 4);
        }

        u8g2.setFont(u8g2_font_5x8_tr);
        u8g2.drawStr(0, 60, "Press SEL to exit");
        u8g2.sendBuffer();
        displayMirrorSend(u8g2);

        beacon_lastDisplayUpdate = now;
    }

    if (now - beacon_scanStartTime > SCAN_DURATION) {
        processScanResults(now);
        esp_wifi_scan_stop();
        esp_wifi_set_mode(WIFI_MODE_AP);

        beacon_isScanning = false;
        beacon_lastScanTime = now;
        beaconSpamMode = returnToMode;
        needsRedraw = true;

        if (ssidIndex >= (int)scannedSSIDs.size() && !scannedSSIDs.empty()) {
            ssidIndex = 0;
        }
    }
}

}

void beaconSpamSetup() {
  randomSeed((uint32_t)esp_random());

  beacon_isScanning = false;
  beacon_lastApCount = 0;

  initWiFi(WIFI_MODE_AP);

  esp_wifi_set_channel(channels[0], WIFI_SECOND_CHAN_NONE);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);

  beaconSpamMode = BEACON_SPAM_MENU;
  menuSelection = 0;
  ssidIndex = 0;
  beacon_lastScanTime = millis();
  scannedSSIDs.clear();

  needsRedraw = true;
  lastMenuSelection = -1;
  lastSSIDIndex = -1;
  lastScannedSSIDsSize = 0;
  lastSSIDSelectedState = false;
  lastBeaconSpamMode = BEACON_SPAM_MENU;

  drawBeaconSpamMenu();
}

void beaconSpamLoop() {
  currentTime = millis();

  bool up = digitalRead(BTN_UP) == LOW;
  bool down = digitalRead(BTN_DOWN) == LOW;
  bool left = digitalRead(BTN_BACK) == LOW;
  bool right = digitalRead(BTN_RIGHT) == LOW;

  bool anySelected = false;
  for (const auto& entry : scannedSSIDs) if (entry.selected) { anySelected = true; break; }

  if ((beaconSpamMode == BEACON_SPAM_CLONE_ALL || beaconSpamMode == BEACON_SPAM_CLONE_SELECTED) && !anySelected && currentTime - beacon_lastScanTime >= SCAN_INTERVAL) {
    startSSIDScan(beaconSpamMode);
    return;
  }

  if (beaconSpamMode == BEACON_SPAM_SCANNING) {
    updateSSIDScan();
    return;
  }

  if (lastBeaconSpamMode != beaconSpamMode) {
    lastBeaconSpamMode = beaconSpamMode;
    needsRedraw = true;
  }

  switch (beaconSpamMode) {
    case BEACON_SPAM_MENU:
      if (lastMenuSelection != menuSelection) {
        lastMenuSelection = menuSelection;
        needsRedraw = true;
      }

      if (up) {
        menuSelection = (menuSelection - 1 + 4) % 4;
        needsRedraw = true;
        delay(200);
      }
      if (down) {
        menuSelection = (menuSelection + 1) % 4;
        needsRedraw = true;
        delay(200);
      }
      if (right) {
        if (menuSelection == 0) {
          startSSIDScan(BEACON_SPAM_CLONE_ALL);
        } else if (menuSelection == 1) {
          startSSIDScan(BEACON_SPAM_CLONE_SELECTED);
        } else if (menuSelection == 2) {
          beaconSpamMode = BEACON_SPAM_CUSTOM;
        } else {
          beaconSpamMode = BEACON_SPAM_RANDOM;
        }
        needsRedraw = true;
        delay(200);
      }
      if (left) {
        beaconSpamMode = BEACON_SPAM_MENU;
        needsRedraw = true;
        delay(200);
      }

      if (needsRedraw) {
        needsRedraw = false;
        drawBeaconSpamMenu();
      }
      break;
      
    case BEACON_SPAM_CLONE_ALL:
      if (lastScannedSSIDsSize != (int)scannedSSIDs.size()) {
        lastScannedSSIDsSize = (int)scannedSSIDs.size();
        needsRedraw = true;
      }

      if (needsRedraw) {
        needsRedraw = false;
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(0, 10, "Clone All SSIDs");
        char buf[32];
        snprintf(buf, sizeof(buf), "Count: %d", (int)scannedSSIDs.size());
        u8g2.drawStr(0, 25, buf);
        u8g2.setFont(u8g2_font_5x8_tr);
        u8g2.drawStr(0, 40, "Spamming on channels");
        u8g2.drawStr(0, 50, "1, 6, 11");
        u8g2.drawStr(0, 62, "L=Back SEL=Exit");
        u8g2.sendBuffer();
        displayMirrorSend(u8g2);
      }

      for (const auto& entry : scannedSSIDs) {
        sendBeaconFrame(entry.ssid, strlen(entry.ssid), entry.channel, random(10) < 4);
        delayMicroseconds(100);
      }
      if (left) {
        beaconSpamMode = BEACON_SPAM_MENU;
        needsRedraw = true;
        delay(200);
      }
      break;

    case BEACON_SPAM_CLONE_SELECTED:
      {
        bool currentSSIDSelectedState = !scannedSSIDs.empty() ? scannedSSIDs[ssidIndex].selected : false;
        
        if (lastSSIDIndex != ssidIndex || 
            lastScannedSSIDsSize != (int)scannedSSIDs.size() ||
            lastSSIDSelectedState != currentSSIDSelectedState) {
          lastSSIDIndex = ssidIndex;
          lastScannedSSIDsSize = (int)scannedSSIDs.size();
          lastSSIDSelectedState = currentSSIDSelectedState;
          needsRedraw = true;
        }
        
        if (up && !scannedSSIDs.empty()) {
          ssidIndex = (ssidIndex - 1 + scannedSSIDs.size()) % scannedSSIDs.size();
          needsRedraw = true;
          delay(200);
        }
        if (down && !scannedSSIDs.empty()) {
          ssidIndex = (ssidIndex + 1) % scannedSSIDs.size();
          needsRedraw = true;
          delay(200);
        }
        if (right && !scannedSSIDs.empty()) {
          scannedSSIDs[ssidIndex].selected = !scannedSSIDs[ssidIndex].selected;
          needsRedraw = true;
          delay(200);
        }
        if (left) {
          beaconSpamMode = BEACON_SPAM_MENU;
          needsRedraw = true;
          delay(200);
        }

        if (needsRedraw) {
          needsRedraw = false;
          drawSSIDList();
        }
        
        if (anySelected) {
          static unsigned long lastBeacon = 0;
          const unsigned long beaconInterval = 20;
          if (currentTime - lastBeacon > beaconInterval) {
            for (const auto& entry : scannedSSIDs) {
              if (entry.selected) {
                sendBeaconFrame(entry.ssid, strlen(entry.ssid), entry.channel, random(10) < 4);
                delayMicroseconds(100);
              }
            }
            lastBeacon = currentTime;
          }
        }
      }
      break;
      
    case BEACON_SPAM_CUSTOM:
      if (needsRedraw) {
        needsRedraw = false;
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(0, 10, "Beacon Spam: Custom");
        u8g2.setFont(u8g2_font_5x8_tr);
        u8g2.drawStr(0, 25, "Spamming on channels");
        u8g2.drawStr(0, 35, "1, 6, 11");
        u8g2.drawStr(0, 62, "L=Back SEL=Exit");
        u8g2.sendBuffer();
        displayMirrorSend(u8g2);
      }
      {
        static const int batchSize = 10;
        static int batchIndices[batchSize];
        static int batchCycle = 0;
        static int batchBeacon = 0;
        static uint32_t lastBatch = 0;
        static uint8_t randomBatchesSinceSwitch = 0;
        static int totalSSIDs = -1;
        
        if (totalSSIDs == -1) {
          totalSSIDs = 0;
          for (int i = 0; i < strlen_P(ssids); i++) {
            if (pgm_read_byte(ssids + i) == '\n') totalSSIDs++;
          }
        }
        
        if (batchCycle == 0 || batchBeacon >= 10) {
          for (int i = 0; i < batchSize; i++) {
            batchIndices[i] = random(totalSSIDs);
          }
          batchBeacon = 0;
        }
        
        if (currentTime - lastBatch > 50) {
          lastBatch = currentTime;
          for (int b = 0; b < batchSize; b++) {
            int randomSSID = batchIndices[b];
            int ssidStart = 0;
            int found = 0;
            
            for (int i = 0; i < strlen_P(ssids); i++) {
              if (pgm_read_byte(ssids + i) == '\n') {
                if (found == randomSSID) {
                  ssidStart = i + 1;
                  break;
                }
                found++;
              }
            }
            
            int j = 0;
            char tmp;
            do {
              tmp = pgm_read_byte(ssids + ssidStart + j);
              j++;
            } while (tmp != '\n' && j <= 32 && (ssidStart + j) < (int)strlen_P(ssids));
            
            uint8_t ssidLen = j - 1;

            char ssidBuf[33];
            for (int k = 0; k < ssidLen && k < 32; k++) {
              ssidBuf[k] = pgm_read_byte(ssids + ssidStart + k);
            }
            ssidBuf[ssidLen] = '\0';

            sendBeaconFrame(ssidBuf, ssidLen, wifi_channel, random(10) < 4);
          }
          batchBeacon++;
        }
        
        batchCycle++;
        if (batchCycle >= 10) batchCycle = 0;
        if (++randomBatchesSinceSwitch >= 4) {
          randomBatchesSinceSwitch = 0;
          nextChannel();
        }
      }
      if (left) {
        beaconSpamMode = BEACON_SPAM_MENU;
        needsRedraw = true;
        delay(200);
      }
      break;

    case BEACON_SPAM_RANDOM:
      if (needsRedraw) {
        needsRedraw = false;
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(0, 10, "Beacon Spam: Random");
        u8g2.setFont(u8g2_font_5x8_tr);
        u8g2.drawStr(0, 25, "Spamming on channels");
        u8g2.drawStr(0, 35, "1, 6, 11");
        u8g2.drawStr(0, 62, "L=Back SEL=Exit");
        u8g2.sendBuffer();
        displayMirrorSend(u8g2);
      }
      {
        static unsigned long lastRandom = 0;
        static int randomChannelIndex = 0;
        const int randomChannels[] = {1, 6, 11};
        static uint8_t randomBatchesSinceSwitch = 0;
        
        if (currentTime - lastRandom >= 50) {
          for (int b = 0; b < 10; b++) {
            char randomSSID[33];
            int length = random(5, 33);
            const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
            for (int i = 0; i < length; i++) {
              randomSSID[i] = chars[random(strlen(chars))];
            }
            randomSSID[length] = '\0';

            sendBeaconFrame(randomSSID, length, wifi_channel, random(10) < 4);
          }
          
          if (++randomBatchesSinceSwitch >= 4) {
            randomBatchesSinceSwitch = 0;
            wifi_channel = randomChannels[randomChannelIndex];
            randomChannelIndex = (randomChannelIndex + 1) % 3;
            esp_wifi_set_channel(wifi_channel, WIFI_SECOND_CHAN_NONE);
          }
          
          lastRandom = currentTime;
        }
      }
      if (left) {
        beaconSpamMode = BEACON_SPAM_MENU;
        menuSelection = 3;
        needsRedraw = true;
        delay(200);
      }
      break;
  }
}