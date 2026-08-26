/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2026 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include <string.h>

#include "../include/beacon_spam.h"
#include "../include/display_mirror.h"
#include "../include/radio_manager.h"
#include "../include/setting.h"
#include "../include/sleep_manager.h"
#include "esp_event.h"
#include "esp_wifi.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

// Disable frame sanity checks
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2,
                                                int32_t arg3) {
  return 0;
}

namespace {

#define BTN_UP BUTTON_PIN_UP
#define BTN_DOWN BUTTON_PIN_DOWN
#define BTN_RIGHT BUTTON_PIN_RIGHT
#define BTN_BACK BUTTON_PIN_LEFT

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
    "Lan Of The Lost\n"};

static int ssidOffsets[64];
static int ssidLengths[64];
static int totalCustomSSIDs = 0;
static uint8_t macAddr[6];
static uint8_t wifi_channel = 1;
static uint32_t currentTime = 0;
static uint8_t lastTxChannel = 0;
static const int POOL_SIZE = 10;
static const int POOL_LIFETIME = 5;

struct PoolEntry {
  char ssid[33];
  uint8_t mac[6];
  bool wpa2;
  uint8_t age;
};
static PoolEntry pool[POOL_SIZE];
static bool poolReady = false;

static void poolFillRandom(PoolEntry& e) {
  static const char chars[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_ ";
  int len = random(5, 33);
  for (int i = 0; i < len; i++) e.ssid[i] = chars[random(sizeof(chars) - 1)];
  e.ssid[len] = '\0';
  for (int i = 0; i < 6; i++) e.mac[i] = (uint8_t)random(256);
  e.mac[0] = (e.mac[0] | 0x02) & 0xFE;
  e.wpa2 = random(10) < 4;
  e.age = 0;
}

static void poolFillCustom(PoolEntry& e) {
  int idx = (totalCustomSSIDs > 0) ? random(totalCustomSSIDs) : 0;
  int len = (totalCustomSSIDs > 0) ? ssidLengths[idx] : 0;
  if (len > 32) len = 32;
  for (int k = 0; k < len; k++)
    e.ssid[k] = (char)pgm_read_byte(ssids + ssidOffsets[idx] + k);
  e.ssid[len] = '\0';
  for (int i = 0; i < 6; i++) e.mac[i] = (uint8_t)random(256);
  e.mac[0] = (e.mac[0] | 0x02) & 0xFE;
  e.wpa2 = random(10) < 4;
  e.age = 0;
}

static void randomMac() {
  for (int i = 0; i < 6; i++) macAddr[i] = (uint8_t)random(256);
  macAddr[0] = (macAddr[0] | 0x02) & 0xFE;
}

static void sendBeaconFrame(const char* ssid, uint8_t ssidLen, uint8_t channel,
                            bool wpa2) {
  static const uint8_t rsn[] = {0x30, 0x18, 0x01, 0x00, 0x00, 0x0F, 0xAC,
                                0x02, 0x02, 0x00, 0x00, 0x0F, 0xAC, 0x04,
                                0x00, 0x0F, 0xAC, 0x02, 0x01, 0x00, 0x00,
                                0x0F, 0xAC, 0x02, 0x00, 0x00};

  uint8_t frame[109];
  uint8_t* p = frame;
  *p++ = 0x80;
  *p++ = 0x00;
  *p++ = 0x00;
  *p++ = 0x00;
  memset(p, 0xFF, 6);
  p += 6;
  memcpy(p, macAddr, 6);
  p += 6;
  memcpy(p, macAddr, 6);
  p += 6;
  uint16_t seq = (uint16_t)(random(4096) << 4);
  *p++ = seq & 0xFF;
  *p++ = seq >> 8;
  uint64_t ts = (uint64_t)esp_timer_get_time();
  memcpy(p, &ts, 8);
  p += 8;
  *p++ = 0x64;
  *p++ = 0x00;
  *p++ = wpa2 ? 0x11 : 0x01;
  *p++ = 0x04;
  *p++ = 0x00;
  *p++ = ssidLen;
  memcpy(p, ssid, ssidLen);
  p += ssidLen;
  *p++ = 0x01;
  *p++ = 0x08;
  *p++ = 0x82;
  *p++ = 0x84;
  *p++ = 0x8B;
  *p++ = 0x96;
  *p++ = 0x0C;
  *p++ = 0x18;
  *p++ = 0x30;
  *p++ = 0x48;

  *p++ = 0x03;
  *p++ = 0x01;
  *p++ = channel;

  if (wpa2) {
    memcpy(p, rsn, sizeof(rsn));
    p += sizeof(rsn);
  }

  if (channel != lastTxChannel) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    lastTxChannel = channel;
  }

  esp_wifi_80211_tx(WIFI_IF_AP, frame, (int)(p - frame), false);
}

static void sendBeacon(const char* ssid, uint8_t channel) {
  randomMac();
  uint8_t len = (uint8_t)strnlen(ssid, 32);
  bool wpa2 = random(10) < 4;
  sendBeaconFrame(ssid, len, channel, wpa2);
  delay(1);
  sendBeaconFrame(ssid, len, channel, wpa2);
}

static void spamPool(void (*fill)(PoolEntry&), uint8_t ch) {
  for (int i = 0; i < POOL_SIZE; i++) {
    memcpy(macAddr, pool[i].mac, 6);
    uint8_t len = (uint8_t)strnlen(pool[i].ssid, 32);
    sendBeaconFrame(pool[i].ssid, len, ch, pool[i].wpa2);
    delay(1);
    if (++pool[i].age >= POOL_LIFETIME) fill(pool[i]);
  }
}

static void nextChannel() {
  static uint8_t idx = 0;
  static const uint8_t chs[] = {1, 6, 11};
  wifi_channel = chs[idx];
  idx = (idx + 1) % 3;
  esp_wifi_set_channel(wifi_channel, WIFI_SECOND_CHAN_NONE);
  lastTxChannel = 0;
}

enum BeaconSpamMode {
  BEACON_SPAM_MENU,
  BEACON_SPAM_CLONE_ALL,
  BEACON_SPAM_CLONE_SELECTED,
  BEACON_SPAM_CUSTOM,
  BEACON_SPAM_RANDOM,
  BEACON_SPAM_SCANNING
};

struct ClonedSSID {
  char ssid[33];
  uint8_t channel;
  bool selected;
};

static BeaconSpamMode beaconSpamMode = BEACON_SPAM_MENU;
static BeaconSpamMode lastBeaconSpamMode = BEACON_SPAM_MENU;
static BeaconSpamMode returnToMode = BEACON_SPAM_MENU;
static int menuSelection = 0;
static int ssidIndex = 0;
static bool needsRedraw = true;
static int lastMenuSelection = -1;
static int lastSSIDIndex = -1;
static int lastScannedSSIDsSize = 0;
static bool lastSSIDSelectedState = false;

static const unsigned long SCAN_INTERVAL = 30000;
static const unsigned long SCAN_DURATION = 8000;
static const unsigned long DISPLAY_UPDATE_INTERVAL = 100;
static unsigned long beacon_lastScanTime = 0;
static unsigned long beacon_scanStartTime = 0;
static unsigned long beacon_lastDisplayUpdate = 0;
static uint16_t beacon_lastApCount = 0;
static bool beacon_isScanning = false;

std::vector<ClonedSSID> scannedSSIDs;
std::vector<ClonedSSID> oldSSIDList;
static const int MAX_CLONE_SSIDS = 50;

static void drawBeaconSpamMenu() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, "Beacon Spam Mode:");
  u8g2.drawStr(0, 22, menuSelection == 0 ? "> Clone All" : "  Clone All");
  u8g2.drawStr(0, 32,
               menuSelection == 1 ? "> Clone Selected" : "  Clone Selected");
  u8g2.drawStr(0, 42, menuSelection == 2 ? "> Custom" : "  Custom");
  u8g2.drawStr(0, 52, menuSelection == 3 ? "> Random" : "  Random");
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.drawStr(0, 64, "U/D=Move R=OK SEL=Exit");
  u8g2.sendBuffer();
  displayMirrorSend(u8g2);
}

static void drawSSIDList() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 12, "Select SSID to clone");
  if (!scannedSSIDs.empty()) {
    char maskedSSID[33];
    maskName(scannedSSIDs[ssidIndex].ssid, maskedSSID, sizeof(maskedSSID) - 1);
    char line1[32];
    snprintf(line1, sizeof(line1), "%s Ch:%d", maskedSSID,
             scannedSSIDs[ssidIndex].channel);
    u8g2.drawStr(0, 28, line1);
    u8g2.drawStr(
        0, 44,
        scannedSSIDs[ssidIndex].selected ? "[*] Selected" : "[ ] Not selected");
  } else {
    u8g2.drawStr(0, 30, "No SSIDs found");
  }
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.drawStr(0, 62, "U/D=Move R=Toggle L=Back");
  u8g2.sendBuffer();
  displayMirrorSend(u8g2);
}

static void processScanResults() {
  uint16_t number = 0;
  esp_wifi_scan_get_ap_num(&number);
  if (number == 0) return;

  wifi_ap_record_t* ap =
      (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * number);
  if (!ap) return;
  memset(ap, 0, sizeof(wifi_ap_record_t) * number);

  uint16_t actual = number;
  if (esp_wifi_scan_get_ap_records(&actual, ap) == ESP_OK) {
    for (int i = 0; i < actual && (int)scannedSSIDs.size() < MAX_CLONE_SSIDS;
         i++) {
      if (ap[i].ssid[0] == '\0') continue;
      bool found = false;
      for (const auto& e : scannedSSIDs)
        if (strcmp(e.ssid, (char*)ap[i].ssid) == 0 &&
            e.channel == ap[i].primary) {
          found = true;
          break;
        }
      if (!found) {
        ClonedSSID entry;
        strncpy(entry.ssid, (char*)ap[i].ssid, sizeof(entry.ssid) - 1);
        entry.ssid[sizeof(entry.ssid) - 1] = '\0';
        entry.channel = ap[i].primary;
        entry.selected = false;
        for (const auto& old : oldSSIDList)
          if (strcmp(old.ssid, entry.ssid) == 0 &&
              old.channel == entry.channel) {
            entry.selected = old.selected;
            break;
          }
        scannedSSIDs.push_back(entry);
      }
    }
  }
  free(ap);
}

static void startSSIDScan(BeaconSpamMode returnMode) {
  oldSSIDList = scannedSSIDs;
  scannedSSIDs.clear();
  beacon_isScanning = true;
  beacon_lastApCount = 0;
  beacon_scanStartTime = millis();
  beacon_lastDisplayUpdate = millis();
  returnToMode = returnMode;

  esp_wifi_stop();
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();
  delay(100);

  wifi_scan_config_t cfg = {.ssid = NULL,
                            .bssid = NULL,
                            .channel = 0,
                            .show_hidden = false,
                            .scan_type = WIFI_SCAN_TYPE_ACTIVE,
                            .scan_time = {.active = {.min = 120, .max = 200}}};
  esp_wifi_scan_start(&cfg, false);
  beaconSpamMode = BEACON_SPAM_SCANNING;
  needsRedraw = true;
}

static void updateSSIDScan() {
  unsigned long now = millis();
  uint16_t apCount = 0;
  esp_wifi_scan_get_ap_num(&apCount);
  bool refresh = (apCount > beacon_lastApCount);
  if (refresh) {
    processScanResults();
    beacon_lastApCount = apCount;
  }

  if (refresh || now - beacon_lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "Scanning WiFi...");
    char buf[32];
    snprintf(buf, sizeof(buf), "Found: %d networks", (int)scannedSSIDs.size());
    u8g2.drawStr(0, 25, buf);
    u8g2.drawFrame(4, 35, 120, 10);
    int fill = (int)(((now - beacon_scanStartTime) * 116UL) / SCAN_DURATION);
    if (fill > 116) fill = 116;
    if (fill > 0) u8g2.drawBox(6, 37, fill, 6);
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(0, 60, "Press SEL to exit");
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
    beacon_lastDisplayUpdate = now;
  }

  if (now - beacon_scanStartTime > SCAN_DURATION) {
    processScanResults();
    esp_wifi_scan_stop();
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_start();
    delay(50);
    lastTxChannel = 0;

    beacon_isScanning = false;
    beacon_lastScanTime = now;
    beaconSpamMode = returnToMode;
    needsRedraw = true;
    if (ssidIndex >= (int)scannedSSIDs.size() && !scannedSSIDs.empty())
      ssidIndex = 0;
  }
}
}

void beaconSpamSetup() {
  randomSeed((uint32_t)esp_random());
  beacon_isScanning = false;
  beacon_lastApCount = 0;

  initWiFi(WIFI_MODE_AP);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);

  totalCustomSSIDs = 0;
  int pLen = (int)strlen_P(ssids);
  int lineStart = 0;
  for (int i = 0; i <= pLen && totalCustomSSIDs < 64; i++) {
    char c = (i < pLen) ? (char)pgm_read_byte(ssids + i) : '\n';
    if (c == '\n') {
      if (i > lineStart) {
        ssidOffsets[totalCustomSSIDs] = lineStart;
        ssidLengths[totalCustomSSIDs] = i - lineStart;
        totalCustomSSIDs++;
      }
      lineStart = i + 1;
    }
  }

  beaconSpamMode = BEACON_SPAM_MENU;
  lastBeaconSpamMode = BEACON_SPAM_MENU;
  menuSelection = 0;
  ssidIndex = 0;
  poolReady = false;
  beacon_lastScanTime = millis();
  scannedSSIDs.clear();

  needsRedraw = true;
  lastMenuSelection = -1;
  lastSSIDIndex = -1;
  lastScannedSSIDsSize = 0;
  lastSSIDSelectedState = false;

  drawBeaconSpamMenu();
}

void beaconSpamLoop() {
  currentTime = millis();

  bool up = digitalRead(BTN_UP) == LOW;
  bool down = digitalRead(BTN_DOWN) == LOW;
  bool left = digitalRead(BTN_BACK) == LOW;
  bool right = digitalRead(BTN_RIGHT) == LOW;

  bool anySelected = false;
  for (const auto& e : scannedSSIDs)
    if (e.selected) {
      anySelected = true;
      break;
    }

  if ((beaconSpamMode == BEACON_SPAM_CLONE_ALL ||
       beaconSpamMode == BEACON_SPAM_CLONE_SELECTED) &&
      !anySelected && currentTime - beacon_lastScanTime >= SCAN_INTERVAL) {
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
    poolReady = false;
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
        u8g2.drawStr(0, 40, "Spamming on CH: 1,6,11");
        u8g2.drawStr(0, 62, "L=Back SEL=Exit");
        u8g2.sendBuffer();
        displayMirrorSend(u8g2);
      }
      {
        static unsigned long lastBatch = 0;
        if (currentTime - lastBatch >= 20) {
          lastBatch = currentTime;
          for (const auto& e : scannedSSIDs) {
            sendBeacon(e.ssid, e.channel);
          }
        }
      }

      if (left) {
        beaconSpamMode = BEACON_SPAM_MENU;
        needsRedraw = true;
        delay(200);
      }
      break;

    case BEACON_SPAM_CLONE_SELECTED: {
      bool selState =
          !scannedSSIDs.empty() ? scannedSSIDs[ssidIndex].selected : false;

      if (lastSSIDIndex != ssidIndex ||
          lastScannedSSIDsSize != (int)scannedSSIDs.size() ||
          lastSSIDSelectedState != selState) {
        lastSSIDIndex = ssidIndex;
        lastScannedSSIDsSize = (int)scannedSSIDs.size();
        lastSSIDSelectedState = selState;
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
        if (currentTime - lastBeacon >= 20) {
          lastBeacon = currentTime;
          for (const auto& e : scannedSSIDs) {
            if (e.selected) sendBeacon(e.ssid, e.channel);
          }
        }
      }
    } break;

    case BEACON_SPAM_CUSTOM:
      if (needsRedraw) {
        needsRedraw = false;
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(0, 10, "Beacon Spam: Custom");
        u8g2.setFont(u8g2_font_5x8_tr);
        u8g2.drawStr(0, 25, "Spamming on CH: 1,6,11");
        u8g2.drawStr(0, 62, "L=Back SEL=Exit");
        u8g2.sendBuffer();
        displayMirrorSend(u8g2);
      }
      {
        static uint8_t sweepCount = 0;

        if (!poolReady) {
          for (int i = 0; i < POOL_SIZE; i++) poolFillCustom(pool[i]);
          poolReady = true;
          sweepCount = 0;
        }

        spamPool(poolFillCustom, wifi_channel);

        if (++sweepCount >= 20) {
          sweepCount = 0;
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
        u8g2.drawStr(0, 25, "Spamming on CH: 1,6,11");
        u8g2.drawStr(0, 62, "L=Back SEL=Exit");
        u8g2.sendBuffer();
        displayMirrorSend(u8g2);
      }
      {
        static uint8_t sweepCount = 0;

        if (!poolReady) {
          for (int i = 0; i < POOL_SIZE; i++) poolFillRandom(pool[i]);
          poolReady = true;
          sweepCount = 0;
        }

        spamPool(poolFillRandom, wifi_channel);

        if (++sweepCount >= 20) {
          sweepCount = 0;
          nextChannel();
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