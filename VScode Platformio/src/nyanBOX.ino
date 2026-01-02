/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <stdint.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <esp_wifi.h>
#include "esp_bt_main.h"

#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif
#include <RF24.h>

#include "../include/icon.h"
#include "../include/neopixel.h"
#include "../include/setting.h"

#include "../include/scanner.h"
#include "../include/analyzer.h"
#include "../include/sourapple.h"
#include "../include/sourdroid.h"
#include "../include/blescan.h"
#include "../include/ble_spammer.h"
#include "../include/ble_spoofer.h"
#include "../include/swiftpair.h"
#include "../include/flipperzero_detector.h"
#include "../include/meshtastic_detector.h"
#include "../include/meshcore_detector.h"
#include "../include/airtag_detector.h"
#include "../include/airtag_spoofer.h"
#include "../include/tile_detector.h"
#include "../include/wifiscan.h"
#include "../include/deauth.h"
#include "../include/deauth_scanner.h"
#include "../include/beacon_spam.h"
#include "../include/pwnagotchi_detector.h"
#include "../include/pindefs.h"
#include "../include/sigkill.h"
#include "../include/about.h"
#include "../include/channel_analyzer.h"
#include "../include/pwnagotchi_spam.h"
#include "../include/level_system.h"
#include "../include/nyanbox_detector.h"
#include "../include/nyanbox_advertiser.h"
#include "../include/evil_portal.h"
#include "../include/legal_disclaimer.h"
#include "../include/cardskimmer_detector.h"
#include "../include/axon_detector.h"
#include "../include/drone_detector.h"
#include "../include/flock_detector.h"
#include "../include/pineapple_detector.h"
#include "../include/display_mirror.h"

RF24 radios[] = {
  RF24(RADIO_CE_PIN_1, RADIO_CSN_PIN_1),
  RF24(RADIO_CE_PIN_2, RADIO_CSN_PIN_2),
  RF24(RADIO_CE_PIN_3, RADIO_CSN_PIN_3)
};

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
Adafruit_NeoPixel pixels(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
extern uint8_t oledBrightness;

struct MenuItem {
  const char* name;
  const unsigned char* icon;
  void (*setup)();
  void (*loop)();
  void (*cleanup)();
};

bool dangerousActionsEnabled = false;

const char* nyanboxVersion = NYANBOX_VERSION;
unsigned long idleTimeout = 120000;
static unsigned long lastActivity = 0;
static bool displayOff = false;
const unsigned long MAX_XP_IDLE_TIME = 120000;

unsigned long upLastMillis    = 0;
unsigned long upNextRepeat    = 0;
bool         upPressed        = false;
unsigned long upDebounceTime  = 0;

unsigned long downLastMillis  = 0;
unsigned long downNextRepeat  = 0;
bool         downPressed      = false;
unsigned long downDebounceTime = 0;

bool selPrev = false;
bool rightPrev = false;
bool leftPrev = false;
unsigned long selDebounceTime = 0;
unsigned long rightDebounceTime = 0;
unsigned long leftDebounceTime = 0;

const unsigned long initialDelay   = 500;
const unsigned long repeatInterval = 250;
const unsigned long debounceDelay  = 200;

static bool needsRedraw = true;

void updateLastActivity() {
  lastActivity = millis();
}

void updateSleepTimeout(unsigned long newTimeout) {
  idleTimeout = newTimeout;
}

bool anyButtonPressed() {
  return digitalRead(BUTTON_PIN_UP)    == LOW ||
        digitalRead(BUTTON_PIN_DOWN)  == LOW ||
        digitalRead(BUTTON_PIN_CENTER)== LOW ||
        digitalRead(BUTTON_PIN_RIGHT) == LOW ||
        digitalRead(BUTTON_PIN_LEFT)  == LOW;
}


void loadSleepTimeoutFromEEPROM() {
  uint8_t sleepTimeoutValue = EEPROM.read(3);
  const unsigned long sleepTimeouts[] = {15, 30, 60, 120, 300, 900, 1800, 0};
  if (sleepTimeoutValue < 8) {
    updateSleepTimeout(sleepTimeouts[sleepTimeoutValue] * 1000);
  } else {
    updateSleepTimeout(120000);
  }
}


void wakeDisplay() {
  u8g2.setPowerSave(0);
  displayOff = false;
  while (anyButtonPressed()) {}

  upDebounceTime = 0;
  downDebounceTime = 0;
  selDebounceTime = 0;
  leftDebounceTime = 0;
  rightDebounceTime = 0;

  upPressed = false;
  downPressed = false;
  selPrev = false;
  leftPrev = false;
  rightPrev = false;

  updateLastActivity();
  needsRedraw = true;
}

void checkIdle() {
  if (idleTimeout == 0) {
    return;
  }

  if (!displayOff && millis() - lastActivity >= idleTimeout) {
    u8g2.setPowerSave(1);
    displayOff = true;
    return;
  }
  if (displayOff && anyButtonPressed()) {
    delay(10);
    if (anyButtonPressed()) {
      wakeDisplay();
    }
  }
}

const int ITEM_HEIGHT = 16;
const int ITEM_SPACING = 2;
const int TEXT_X = 28;
const int SELECTION_X = 4;
const int SELECTION_WIDTH = 120;
const int ICON_X = 8;

void drawSelection(int x, int y, int width, int height, bool selected) {
  if (selected) {
    u8g2.drawBox(x, y+2, 2, height-4);
  }
}

enum AppMenuState { APP_MAIN, APP_BLE, APP_WIFI, APP_OTHER, APP_LEVEL };

int getXPAmount(const char* appName) {
  if (isReconApp(appName)) {
    return 3;
  } else if (isOffensiveApp(appName)) {
    return 4;
  } else if (isUtilityApp(appName)) {
    return 2;
  } else {
    return 0;
  }
}

bool isReconApp(const char* appName) {
  return strstr(appName, "Scan") != nullptr || 
         strstr(appName, "Detector") != nullptr ||
         strstr(appName, "Analyzer") != nullptr;
}

bool isDangerousApp(const char* appName) {
  return strstr(appName, "SigKill") != nullptr;
}

bool isOffensiveApp(const char* appName) {
  if (isDangerousApp(appName)) {
    return true;
  }

  return strstr(appName, "Deauth") != nullptr ||
         strstr(appName, "Spam") != nullptr ||
         strstr(appName, "Swift Pair") != nullptr ||
         strstr(appName, "Sour Apple") != nullptr ||
         strstr(appName, "Sour Droid") != nullptr ||
         strstr(appName, "Spoofer") != nullptr ||
         strstr(appName, "Evil Portal") != nullptr;
}

bool isUtilityApp(const char* appName) {
  return strstr(appName, "Setting") != nullptr ||
         strstr(appName, "About") != nullptr;
}

AppMenuState currentState = APP_MAIN;
MenuItem*    currentMenuItems = nullptr;
int          currentMenuSize  = 0;
int          item_selected    = 0;

constexpr uint8_t BUTTON_UP    = BUTTON_PIN_UP;
constexpr uint8_t BUTTON_SEL   = BUTTON_PIN_CENTER;
constexpr uint8_t BUTTON_DOWN  = BUTTON_PIN_DOWN;
constexpr uint8_t BUTTON_RIGHT = BUTTON_PIN_RIGHT;
constexpr uint8_t BUTTON_LEFT  = BUTTON_PIN_LEFT;

static unsigned long appStartTime = 0;
static unsigned long lastXPReward = 0;
static const char* currentAppName = "";
static int currentXPAmount = 0;
static bool inApplication = false;
static int pendingXP = 0;
static unsigned long totalActiveMinutes = 0;
const unsigned long XP_REWARD_INTERVAL = 60000;

bool justPressed(uint8_t pin, bool &prev, unsigned long &debounceTime) {
  bool now = digitalRead(pin) == LOW;
  unsigned long currentTime = millis();

  if (now != prev && (currentTime - debounceTime) > debounceDelay) {
    debounceTime = currentTime;
    prev = now;
    return now;
  }

  return false;
}

bool shouldShowApp(const char* appName) {
  return !isDangerousApp(appName) || isDangerousActionsEnabled();
}

int getVisibleMenuSize() {
  int count = 0;
  for (int i = 0; i < currentMenuSize; i++) {
    if (shouldShowApp(currentMenuItems[i].name)) {
      count++;
    }
  }
  return count;
}

MenuItem* getVisibleMenuItem(int visibleIndex) {
  int visibleCount = 0;
  for (int i = 0; i < currentMenuSize; i++) {
    if (shouldShowApp(currentMenuItems[i].name)) {
      if (visibleCount == visibleIndex) {
        return &currentMenuItems[i];
      }
      visibleCount++;
    }
  }

  return &currentMenuItems[0];
}


void startAppTracking(const char* appName) {
  currentAppName = appName;
  currentXPAmount = getXPAmount(appName);
  appStartTime = millis();
  lastXPReward = appStartTime;
  totalActiveMinutes = 0;
  inApplication = true;
}

void stopAppTracking() {
  if (inApplication) {
    if (totalActiveMinutes >= 10) {
      pendingXP += 12;
    } else if (totalActiveMinutes >= 5) {
      pendingXP += 4;
    }

    if (pendingXP > 0) {
      addXP(pendingXP);
      pendingXP = 0;
    }

    inApplication = false;
    currentAppName = "";
    currentXPAmount = 0;

    updateLastActivity();
  }
}

void updateAppXP() {
  if (!inApplication) return;

  bool currentlyActive = (millis() - lastActivity < MAX_XP_IDLE_TIME);

  if (currentlyActive && millis() - lastXPReward >= XP_REWARD_INTERVAL) {
    totalActiveMinutes++;

    if (currentXPAmount > 0) {
      pendingXP += currentXPAmount;
    }
    lastXPReward = millis();
  }
}

void enterMenu(AppMenuState st);
void runApp(MenuItem &mi);

void cleanupWiFi() {
  wifi_mode_t mode;
  if (esp_wifi_get_mode(&mode) == ESP_OK) {
    esp_wifi_stop();
    delay(50);
    esp_wifi_deinit();
    delay(100);
  }

  esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (sta_netif != NULL) {
    esp_netif_destroy(sta_netif);
  }

  esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (ap_netif != NULL) {
    esp_netif_destroy(ap_netif);
  }

  delay(100);
}

void cleanupRadio() {
  for (auto &r : radios) r.powerDown();

  wifi_mode_t mode;
  if (esp_wifi_get_mode(&mode) == ESP_OK) {
    esp_wifi_stop();
    delay(50);
    esp_wifi_deinit();
    delay(100);
  }

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
}

void cleanupBLE() {
  delay(100);

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
}

void noCleanup() {
}

MenuItem mainMenu[] = {
  { "WiFi",  bitmap_icon_wifi,    nullptr, nullptr, noCleanup },
  { "BLE",   bitmap_icon_ble,     nullptr, nullptr, noCleanup },
  { "Other", bitmap_icon_analyzer, nullptr, nullptr, noCleanup }
};
constexpr int MAIN_MENU_SIZE = sizeof(mainMenu) / sizeof(mainMenu[0]);

MenuItem wifiMenu[] = {
  { "WiFi Scan",       nullptr, wifiscanSetup,           wifiscanLoop,           wifiscanCleanup },
  { "Channel Analyzer", nullptr, channelAnalyzerSetup,   channelAnalyzerLoop,    cleanupWiFi },
  { "WiFi Deauther",   nullptr, deauthSetup,             deauthLoop,             cleanupWiFi },
  { "Deauth Scanner",  nullptr, deauthScannerSetup,      deauthScannerLoop,      cleanupWiFi },
  { "Beacon Spam",     nullptr, beaconSpamSetup,         beaconSpamLoop,         cleanupWiFi },
  { "Evil Portal",     nullptr, evilPortalSetup,         evilPortalLoop,         cleanupEvilPortal },
  { "Pineapple Detector", nullptr, pineappleDetectorSetup, pineappleDetectorLoop, cleanupWiFi },
  { "Pwnagotchi Detector", nullptr, pwnagotchiDetectorSetup, pwnagotchiDetectorLoop, cleanupWiFi },
  { "Pwnagotchi Spam", nullptr, pwnagotchiSpamSetup,     pwnagotchiSpamLoop,     cleanupWiFi },
  { "Back",            nullptr, nullptr,                 nullptr,                noCleanup }
};
constexpr int WIFI_MENU_SIZE = sizeof(wifiMenu) / sizeof(wifiMenu[0]);

MenuItem bleMenu[] = {
  { "BLE Scan",     nullptr, blescanSetup,             blescanLoop,             cleanupBLE },
  { "nyanBOX Detector", nullptr, nyanboxDetectorSetup,         nyanboxDetectorLoop,         cleanupBLE },
  { "Flipper Zero Detector", nullptr, flipperZeroDetectorSetup, flipperZeroDetectorLoop, cleanupBLE },
  { "Axon Detector", nullptr, axonDetectorSetup, axonDetectorLoop, cleanupBLE },
  { "Meshtastic Detector", nullptr, meshtasticDetectorSetup, meshtasticDetectorLoop, cleanupBLE },
  { "MeshCore Detector", nullptr, meshcoreDetectorSetup, meshcoreDetectorLoop, cleanupBLE },
  { "Skimmer Detector", nullptr, cardskimmerDetectorSetup, cardskimmerDetectorLoop, cleanupBLE },
  { "AirTag Detector", nullptr, airtagDetectorSetup,   airtagDetectorLoop,      cleanupBLE },
  { "AirTag Spoofer", nullptr, airtagSpooferSetup,     airtagSpooferLoop,       cleanupBLE },
  { "Tile Detector", nullptr, tileDetectorSetup,     tileDetectorLoop,       cleanupBLE },
  { "BLE Spammer",  nullptr, bleSpamSetup,             bleSpamLoop,             cleanupBLE },
  { "Swift Pair",   nullptr, swiftpairSpamSetup,       swiftpairSpamLoop,       cleanupBLE },
  { "Sour Apple",   nullptr, sourappleSetup,           sourappleLoop,           cleanupBLE },
  { "Sour Droid",    nullptr, sourDroidSetup,          sourDroidLoop,          cleanupBLE },
  { "BLE Spoofer",  nullptr, bleSpooferSetup,          bleSpooferLoop,          cleanupBLE },
  { "Back",         nullptr, nullptr,                  nullptr,                 noCleanup }
};
constexpr int BLE_MENU_SIZE = sizeof(bleMenu) / sizeof(bleMenu[0]);

MenuItem otherMenu[] = {
  { "SigKill",   nullptr, sigkillSetup,   sigkillLoop,   cleanupRadio },
  { "Drone Detector", nullptr, droneDetectorSetup, droneDetectorLoop, cleanupDroneDetector },
  { "Flock Detector", nullptr, flockDetectorSetup, flockDetectorLoop, cleanupFlockDetector },
  { "Scanner",      nullptr, scannerSetup,    scannerLoop,    cleanupRadio },
  { "Analyzer",     nullptr, analyzerSetup,   analyzerLoop,   cleanupRadio },
  { "Setting",      nullptr, settingSetup,    settingLoop,    noCleanup },
  { "About",        nullptr, aboutSetup,      aboutLoop,      aboutCleanup },
  { "Back",         nullptr, nullptr,         nullptr,        noCleanup }
};
constexpr int OTHER_MENU_SIZE = sizeof(otherMenu) / sizeof(otherMenu[0]);

void enterMenu(AppMenuState st) {
  currentState = st;

  int previousSelection = item_selected;
  const char* previousAppName = nullptr;

  if (item_selected < getVisibleMenuSize()) {
    previousAppName = getVisibleMenuItem(item_selected)->name;
  }

  if (st == APP_MAIN) {
    startNyanboxAdvertiser();
  } else {
    stopNyanboxAdvertiser();
  }

  switch (st) {
    case APP_MAIN:
      currentMenuItems = mainMenu;
      currentMenuSize  = MAIN_MENU_SIZE;
      break;
    case APP_WIFI:
      currentMenuItems = wifiMenu;
      currentMenuSize  = WIFI_MENU_SIZE;
      break;
    case APP_BLE:
      currentMenuItems = bleMenu;
      currentMenuSize  = BLE_MENU_SIZE;
      break;
    case APP_OTHER:
      currentMenuItems = otherMenu;
      currentMenuSize  = OTHER_MENU_SIZE;
      break;
  }

  item_selected = 0;
  if (previousAppName && st != APP_MAIN) {
    for (int i = 0; i < getVisibleMenuSize(); i++) {
      if (strcmp(getVisibleMenuItem(i)->name, previousAppName) == 0) {
        item_selected = i;
        break;
      }
    }
  }

  needsRedraw = true;
}

void runApp(MenuItem &mi) {
  if (!mi.setup) return;

  startAppTracking(mi.name);

  if (isReconApp(mi.name)) {
    blinkColor(0, 0, 255);  // Blue
  } else if (isOffensiveApp(mi.name)) {
    blinkColor(255, 0, 0); // Red
  }

  mi.setup();
  updateLastActivity();
  displayOff = false;
  u8g2.setPowerSave(0);

  if (!mi.loop) return;
  while (digitalRead(BUTTON_SEL) == LOW);

  while (true) {
    checkIdle();
    updateAppXP();
    neopixelLoop();

    if (anyButtonPressed()) {
      updateLastActivity();
    }

    mi.loop();
    if (digitalRead(BUTTON_SEL) == LOW) {
      while (digitalRead(BUTTON_SEL) == LOW);

      if (mi.cleanup) {
        mi.cleanup();
      }

      break;
    }
  }

  stopBlinking();
  stopAppTracking();
  u8g2.clearBuffer();
}

void setup() {
  Serial.begin(115200);

  neopixelSetup();
  SPI.begin();

  int cePins[] = {RADIO_CE_PIN_1, RADIO_CE_PIN_2, RADIO_CE_PIN_3};
  int csnPins[] = {RADIO_CSN_PIN_1, RADIO_CSN_PIN_2, RADIO_CSN_PIN_3};

  for (int i = 0; i < 3; i++) {
    pinMode(cePins[i], OUTPUT);
    pinMode(csnPins[i], OUTPUT);
    digitalWrite(csnPins[i], HIGH);
    digitalWrite(cePins[i], LOW);
  }
  delay(100);

  for (int i = 0; i < 3; i++) {
    if (!radios[i].begin() || !radios[i].isChipConnected()) {
      while(true) { delay(1000); }
    }
    radios[i].setAutoAck(false);
    radios[i].stopListening();
    radios[i].setRetries(0,0);
    radios[i].setPALevel(RF24_PA_MAX, true);
    radios[i].setDataRate(RF24_2MBPS);
    radios[i].setCRCLength(RF24_CRC_DISABLED);
  }

  EEPROM.begin(512);
  oledBrightness = EEPROM.read(1);

  dangerousActionsEnabled = false;

  loadSleepTimeoutFromEEPROM();

  uint8_t continuousScanValue = EEPROM.read(4);
  if (continuousScanValue == 0xFF) {
    continuousScanEnabled = true;
  } else {
    continuousScanEnabled = (continuousScanValue == 1);
  }

  uint8_t privacyModeValue = EEPROM.read(5);
  if (privacyModeValue == 0xFF) {
    privacyModeEnabled = false;
  } else {
    privacyModeEnabled = (privacyModeValue == 1);
  }

  u8g2.begin();
  u8g2.setContrast(oledBrightness);
  u8g2.setBitmapMode(1);

  updateLastActivity();

  u8g2.clearBuffer();
  
  u8g2.setFont(u8g2_font_helvB14_tr);
  const char* title = "nyanBOX";
  int16_t titleW = u8g2.getUTF8Width(title);
  u8g2.setCursor((128 - titleW) / 2, 16);
  u8g2.print(title);

  u8g2.setFont(u8g2_font_helvR08_tr);
  const char* url = "nyandevices.com";
  int16_t urlW = u8g2.getUTF8Width(url);
  u8g2.setCursor((128 - urlW) / 2, 32);
  u8g2.print(url);

  u8g2.setFont(u8g2_font_helvR08_tr);
  int16_t creditWidth = u8g2.getUTF8Width("by jbohack & zr_crackiin");
  int16_t creditX = (128 - creditWidth) / 2;
  u8g2.setCursor(creditX, 50);
  u8g2.print("by jbohack & zr_crackiin");

  u8g2.setFont(u8g2_font_helvR08_tr);
  int16_t verW = u8g2.getUTF8Width(nyanboxVersion);
  u8g2.setCursor((128 - verW) / 2, 62);
  u8g2.print(nyanboxVersion);

  u8g2.sendBuffer();
  delay(2000);

  u8g2.clearBuffer();
  u8g2.drawXBMP(0, 0, 128, 64, logo_nyanbox);
  u8g2.sendBuffer();
  delay(1500);

  pinMode(BUTTON_PIN_UP, INPUT_PULLUP);
  pinMode(BUTTON_PIN_CENTER, INPUT_PULLUP);
  pinMode(BUTTON_PIN_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_PIN_RIGHT, INPUT_PULLUP);
  pinMode(BUTTON_PIN_LEFT, INPUT_PULLUP);

  levelSystemSetup();
  enterMenu(APP_MAIN);

  initNyanboxAdvertiser();
  startNyanboxAdvertiser();

  displayMirrorSetup();
}

void loop() {
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "MIRROR_ON") {
      displayMirrorEnable(true);
      needsRedraw = true;
    } else if (cmd == "MIRROR_OFF") {
      displayMirrorEnable(false);
    }
  }

  checkIdle();

  if (displayOff) {
    return;
  }

  updateAppXP();
  neopixelLoop();
  updateNyanboxAdvertiser();

  bool upNow = (digitalRead(BUTTON_PIN_UP) == LOW);
  bool downNow = (digitalRead(BUTTON_PIN_DOWN) == LOW);
  unsigned long currentTime = millis();

  if (upNow) {
    updateLastActivity();
    if (!upPressed && (currentTime - upDebounceTime) > debounceDelay) {
      upDebounceTime = currentTime;
      if (item_selected > 0) {
        item_selected--;
      } else {
        item_selected = getVisibleMenuSize() - 1;
      }
      upLastMillis = currentTime;
      upNextRepeat = upLastMillis + initialDelay;
      needsRedraw = true;
    } else if (upPressed && currentTime >= upNextRepeat) {
      if (item_selected > 0) {
        item_selected--;
      } else {
        item_selected = getVisibleMenuSize() - 1;
      }
      upNextRepeat += repeatInterval;
      needsRedraw = true;
    }
  }
  upPressed = upNow;

  if (downNow) {
    updateLastActivity();
    if (!downPressed && (currentTime - downDebounceTime) > debounceDelay) {
      downDebounceTime = currentTime;
      if (item_selected < getVisibleMenuSize() - 1) {
        item_selected++;
      } else {
        item_selected = 0;
      }
      downLastMillis = currentTime;
      downNextRepeat = downLastMillis + initialDelay;
      needsRedraw = true;
    } else if (downPressed && currentTime >= downNextRepeat) {
      if (item_selected < getVisibleMenuSize() - 1) {
        item_selected++;
      } else {
        item_selected = 0;
      }
      downNextRepeat += repeatInterval;
      needsRedraw = true;
    }
  }
  downPressed = downNow;

  if (justPressed(BUTTON_SEL, selPrev, selDebounceTime)) {
    updateLastActivity();
    if (currentState != APP_LEVEL) {
      MenuItem *sel = getVisibleMenuItem(item_selected);
      if (currentState == APP_MAIN) {
        if (strcmp(sel->name, "WiFi") == 0) enterMenu(APP_WIFI);
        else if (strcmp(sel->name, "BLE") == 0) enterMenu(APP_BLE);
        else if (strcmp(sel->name, "Other") == 0) enterMenu(APP_OTHER);
      } else {
        if (strcmp(sel->name, "Back") == 0) {
          enterMenu(APP_MAIN);
        } else {
          runApp(*sel);
          needsRedraw = true;
        }
      }
    }
  }

  if (justPressed(BUTTON_LEFT, leftPrev, leftDebounceTime)) {
    updateLastActivity();
    if (currentState == APP_LEVEL) {
      enterMenu(APP_MAIN);
    }
  }

  if (justPressed(BUTTON_RIGHT, rightPrev, rightDebounceTime)) {
    updateLastActivity();
    if (currentState == APP_MAIN) {
      currentState = APP_LEVEL;
      levelSystemSetup();
      needsRedraw = true;
    }
  }

  if (currentState == APP_LEVEL) {
    levelSystemLoop();
  } else {
    if (currentState == APP_MAIN) {
      updateNyanboxAdvertiser();
    }

    if (needsRedraw) {
      u8g2.clearBuffer();

      int start;
      if (item_selected == 0) start = 0;
      else if (item_selected == getVisibleMenuSize() - 1) start = max(0, getVisibleMenuSize() - 3);
      else start = item_selected - 1;

      int highlight = item_selected - start;

      int selectionY = 6 + (highlight * (ITEM_HEIGHT + ITEM_SPACING));
      drawSelection(SELECTION_X, selectionY, SELECTION_WIDTH, ITEM_HEIGHT, true);

      for (int i = 0; i < 3; i++) {
        int idx = start + i;
        if (idx < getVisibleMenuSize()) {
          MenuItem *item = getVisibleMenuItem(idx);
          int itemY = 6 + (i * (ITEM_HEIGHT + ITEM_SPACING));
          int textY = itemY + 11;

          u8g2.setFont(u8g2_font_helvR08_tr);
          u8g2.drawStr(TEXT_X, textY, item->name);

          if (item->icon) {
            int iconY = itemY;
            u8g2.drawXBMP(ICON_X, iconY, 16, 16, item->icon);
          }
        }
      }

      if (currentState == APP_MAIN) {
        u8g2.setFont(u8g2_font_5x8_tr);
        char levelStr[16];
        sprintf(levelStr, "Level %d", getCurrentLevel());
        int levelWidth = u8g2.getUTF8Width(levelStr);
        u8g2.drawStr(128 - levelWidth, 8, levelStr);

        const char* rightHint = "Level Menu ->";
        int rightHintWidth = u8g2.getUTF8Width(rightHint);
        u8g2.drawStr(128 - rightHintWidth, 64, rightHint);
      }

      u8g2.sendBuffer();
      displayMirrorSend(u8g2);
      needsRedraw = false;
    }
  }
}