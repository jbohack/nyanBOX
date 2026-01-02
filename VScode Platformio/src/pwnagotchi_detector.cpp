/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/pwnagotchi_detector.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "../include/setting.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include <ArduinoJson.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

#define BTN_UP BUTTON_PIN_UP
#define BTN_DOWN BUTTON_PIN_DOWN
#define BTN_SELECT BUTTON_PIN_RIGHT
#define BTN_BACK BUTTON_PIN_LEFT

struct PwnagotchiData {
  String name;
  String version;
  int pwnd;
  bool deauth;
  int uptime;
  int channel;
  int rssi;
};
static std::vector<PwnagotchiData> pwnagotchi;

int currentIndex = 0;
int listStartIndex = 0;
bool isDetailView = false;
bool isLocateMode = false;
String locateTargetName = "";
unsigned long lastButtonPress = 0;
const unsigned long debounceTime = 200;

static const uint8_t channels[] = {1, 6, 11};
static const int numChannels = sizeof(channels) / sizeof(channels[0]);
static int currentChannelIndex = 0;
static uint32_t lastHop = 0;
static bool wifiWasInitialized = false;

static bool needsRedraw = true;
static int lastPwnagotchiSize = 0;
static int lastCurrentIndex = -1;
static int lastListStartIndex = -1;
static bool lastIsDetailView = false;
static bool lastIsLocateMode = false;
static unsigned long lastPeriodicUpdate = 0;
const unsigned long periodicUpdateInterval = 1000;

void IRAM_ATTR pwnagotchiSnifferCallback(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT)
    return;

  auto *pkt = reinterpret_cast<wifi_promiscuous_pkt_t *>(buf);
  const uint8_t *pl = pkt->payload;
  int len = pkt->rx_ctrl.sig_len;
  if (len <= 4)
    return; // too short
  len -= 4; // strip FCS
  if (len < 38 || pl[0] != 0x80)
    return; // not a beacon

  // filter on Pwnagotchi's MAC (Addr #2 @ offset 10)
  char addr[18];
  snprintf(addr, sizeof(addr), "%02x:%02x:%02x:%02x:%02x:%02x", pl[10], pl[11],
           pl[12], pl[13], pl[14], pl[15]);
  if (String(addr) != "de:ad:be:ef:de:ad")
    return;

  // extract the SSID IE (offset 38, length = len-37)
  int ssidLen = len - 37;
  if (ssidLen <= 0)
    return;

  String essid;
  for (int i = 0; i < ssidLen; ++i) {
    char c = (char)pl[38 + i];
    if (c == '\0')
      break;
    essid.concat(c);
  }

  JsonDocument doc; // adjusts automatically on ArduinoJson v7 (if changed to v6, use 1024)
  if (deserializeJson(doc, essid))
    return;

  const char *jsName = doc["name"];
  const char *jsVer = doc["version"];
  
  if (!jsName || !jsVer || strlen(jsName) == 0 || strlen(jsVer) == 0)
    return;
  
  int jsPwnd = doc["pwnd_tot"];
  bool jsDeauth = doc["policy"]["deauth"];
  int jsUptime = doc["uptime"];
  int ch = pkt->rx_ctrl.channel;
  int rssi = pkt->rx_ctrl.rssi;

  if (isLocateMode && locateTargetName.length() > 0) {
    if (String(jsName) != locateTargetName) {
      return;
    }
  }

  for (auto &e : pwnagotchi) {
    if (e.name == jsName) {
      e.version = jsVer;
      e.pwnd = jsPwnd;
      e.deauth = jsDeauth;
      e.uptime = jsUptime;
      e.channel = ch;
      e.rssi = rssi;
      needsRedraw = true;
      return;
    }
  }

  if (!isLocateMode) {
    pwnagotchi.push_back(
        {String(jsName), String(jsVer), jsPwnd, jsDeauth, jsUptime, ch, rssi});
    needsRedraw = true;
  }
}

void pwnagotchiDetectorSetup() {
  pwnagotchi.clear();
  currentIndex = 0;
  listStartIndex = 0;
  isDetailView = false;
  isLocateMode = false;
  locateTargetName = "";
  lastButtonPress = 0;

  needsRedraw = true;
  lastPwnagotchiSize = 0;
  lastCurrentIndex = -1;
  lastListStartIndex = -1;
  lastIsDetailView = false;
  lastIsLocateMode = false;
  lastPeriodicUpdate = 0;

  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tr);

  wifi_mode_t currentMode;
  if (esp_wifi_get_mode(&currentMode) == ESP_OK) {
    esp_wifi_disconnect();
    esp_wifi_stop();
    wifiWasInitialized = true;
  } else {
    wifiWasInitialized = false;
  }

  if (!wifiWasInitialized) {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
  }

  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();

  esp_wifi_set_ps(WIFI_PS_NONE);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);

  esp_wifi_set_promiscuous_rx_cb(&pwnagotchiSnifferCallback);
  wifi_promiscuous_filter_t flt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
  esp_wifi_set_promiscuous_filter(&flt);
  esp_wifi_set_promiscuous(true);

  esp_wifi_set_channel(channels[currentChannelIndex], WIFI_SECOND_CHAN_NONE);
  lastHop = millis();
}

void pwnagotchiDetectorLoop() {
  unsigned long now = millis();

  if (now - lastHop > 1000) {
    currentChannelIndex = (currentChannelIndex + 1) % numChannels;
    esp_wifi_set_channel(channels[currentChannelIndex], WIFI_SECOND_CHAN_NONE);
    lastHop = now;
  }

  if (now - lastButtonPress > debounceTime) {
    if (!isDetailView && !isLocateMode && digitalRead(BTN_UP) == LOW && currentIndex > 0) {
      --currentIndex;
      if (currentIndex < listStartIndex)
        --listStartIndex;
      lastButtonPress = now;
      needsRedraw = true;
    } else if (!isDetailView && !isLocateMode && digitalRead(BTN_DOWN) == LOW &&
               currentIndex < (int)pwnagotchi.size() - 1) {
      ++currentIndex;
      if (currentIndex >= listStartIndex + 5)
        ++listStartIndex;
      lastButtonPress = now;
      needsRedraw = true;
    } else if (!isDetailView && !isLocateMode && digitalRead(BTN_SELECT) == LOW &&
               !pwnagotchi.empty()) {
      isDetailView = true;
      lastButtonPress = now;
      needsRedraw = true;
    } else if (isDetailView && !isLocateMode && digitalRead(BTN_SELECT) == LOW &&
               !pwnagotchi.empty()) {
      isLocateMode = true;
      locateTargetName = pwnagotchi[currentIndex].name;
      lastButtonPress = now;
      needsRedraw = true;
    } else if (isLocateMode && digitalRead(BTN_BACK) == LOW) {
      isLocateMode = false;
      locateTargetName = "";
      lastButtonPress = now;
      needsRedraw = true;
    } else if (isDetailView && !isLocateMode && digitalRead(BTN_BACK) == LOW) {
      isDetailView = false;
      lastButtonPress = now;
      needsRedraw = true;
    }
  }

  if (pwnagotchi.empty()) {
    if (currentIndex != 0 || listStartIndex != 0 || isDetailView || isLocateMode) {
      needsRedraw = true;
    }
    currentIndex = 0;
    listStartIndex = 0;
    isDetailView = false;
    isLocateMode = false;
    locateTargetName = "";
  }

  if (lastPwnagotchiSize != (int)pwnagotchi.size()) {
    lastPwnagotchiSize = (int)pwnagotchi.size();
    needsRedraw = true;
  }
  if (lastCurrentIndex != currentIndex) {
    lastCurrentIndex = currentIndex;
    needsRedraw = true;
  }
  if (lastListStartIndex != listStartIndex) {
    lastListStartIndex = listStartIndex;
    needsRedraw = true;
  }
  if (lastIsDetailView != isDetailView) {
    lastIsDetailView = isDetailView;
    needsRedraw = true;
  }
  if (lastIsLocateMode != isLocateMode) {
    lastIsLocateMode = isLocateMode;
    needsRedraw = true;
  }

  if (isLocateMode && now - lastPeriodicUpdate >= periodicUpdateInterval) {
    lastPeriodicUpdate = now;
    needsRedraw = true;
  }

  if (!needsRedraw) {
    return;
  }

  needsRedraw = false;
  u8g2.clearBuffer();

  if (pwnagotchi.empty()) {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "Scanning for");
    u8g2.drawStr(0, 20, "Pwnagotchis...");
    u8g2.drawStr(0, 45, "Press SEL to stop");
  } else if (isLocateMode) {
    if (currentIndex >= 0 && currentIndex < (int)pwnagotchi.size()) {
      auto &e = pwnagotchi[currentIndex];
      u8g2.setFont(u8g2_font_5x8_tr);
      char buf[32];

      String displayName = e.name.length() > 0 ? e.name : "Unknown";
      char maskedName[33];
      maskName(displayName.c_str(), maskedName, sizeof(maskedName) - 1);
      snprintf(buf, sizeof(buf), "%.21s", maskedName);
      u8g2.drawStr(0, 8, buf);

      String verLine = "Ver: " + (e.version.length() > 0 ? e.version : "?");
      u8g2.drawStr(0, 16, verLine.c_str());

      u8g2.setFont(u8g2_font_7x13B_tr);
      snprintf(buf, sizeof(buf), "RSSI: %d dBm", e.rssi);
      u8g2.drawStr(0, 28, buf);

      u8g2.setFont(u8g2_font_5x8_tr);
      int rssiClamped = constrain(e.rssi, -100, -40);
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
    } else {
      isLocateMode = false;
    }
  } else if (isDetailView) {
    if (currentIndex >= 0 && currentIndex < (int)pwnagotchi.size()) {
      auto &e = pwnagotchi[currentIndex];
      u8g2.setFont(u8g2_font_5x8_tr);

      String displayName = e.name.length() > 0 ? e.name : "Unknown";
      char maskedName[33];
      maskName(displayName.c_str(), maskedName, sizeof(maskedName) - 1);
      String nameLine = "Name: " + String(maskedName);
      u8g2.drawStr(0, 10, nameLine.c_str());

      String verLine = "Ver:  " + (e.version.length() > 0 ? e.version : "Unknown");
      u8g2.drawStr(0, 20, verLine.c_str());

      String pwndLine = "Pwnd: " + String(e.pwnd);
      u8g2.drawStr(0, 30, pwndLine.c_str());

      String deauthLine = "Deauth: " + String(e.deauth ? "Yes" : "No");
      u8g2.drawStr(0, 40, deauthLine.c_str());

      String uptimeLine = "Uptime: " + String(e.uptime / 60) + "min";
      u8g2.drawStr(0, 50, uptimeLine.c_str());

      u8g2.drawStr(0, 60, "L=Back SEL=Exit R=Locate");
    } else {
      isDetailView = false;
    }
  } else {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "Pwnagotchi list:");
    for (int i = 0; i < 5; ++i) {
      int idx = listStartIndex + i;
      if (idx >= (int)pwnagotchi.size())
        break;
      auto &e = pwnagotchi[idx];
      if (idx == currentIndex)
        u8g2.drawStr(0, 20 + i * 10, ">");

      String displayName = e.name.length() > 0 ? e.name : "Unknown";
      char maskedName[33];
      maskName(displayName.c_str(), maskedName, sizeof(maskedName) - 1);
      String line = String(maskedName).substring(0, 7) + " | RSSI " + String(e.rssi);
      u8g2.drawStr(10, 20 + i * 10, line.c_str());
    }
  }

  u8g2.sendBuffer();
  displayMirrorSend(u8g2);
}