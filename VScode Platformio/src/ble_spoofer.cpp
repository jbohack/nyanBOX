/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/ble_spoofer.h"
#include "../include/blescan.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "../include/setting.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern std::vector<BLEDeviceData> bleDevices;

#define BTN_UP BUTTON_PIN_UP
#define BTN_DOWN BUTTON_PIN_DOWN
#define BTN_RIGHT BUTTON_PIN_RIGHT
#define BTN_BACK BUTTON_PIN_LEFT
#define BTN_CENTER BUTTON_PIN_CENTER

enum BLESpooferState {
  SPOOFER_MENU,
  SPOOFER_CLONE_SELECT,
  SPOOFER_CLONE_RUNNING,
  SPOOFER_CLONE_ALL_RUNNING
};

static BLESpooferState currentState = SPOOFER_MENU;
static bool bleInitialized = false;

static int menuSelection = 0;
static int cloneTargetIndex = 0;

static bool isAdvertising = false;
static unsigned long lastAdvertiseTime = 0;
static int currentCloneAllIndex = 0;
static unsigned long lastButtonPress = 0;
const unsigned long debounceTime = 200;
const unsigned long advertiseInterval = 1000;

static bool needsRedraw = true;
static BLESpooferState lastState = SPOOFER_MENU;
static unsigned long lastRunningUpdate = 0;
const unsigned long runningUpdateInterval = 1000;

static void drawMainMenu() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 12, "BLE Spoofer");

  if (bleDevices.empty()) {
    u8g2.drawStr(0, 28, "No BLE devices found!");
    u8g2.drawStr(0, 44, "Run BLE Scan first");
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(0, 62, "SEL=Exit");
  } else {
    const char* menuItems[] = {
      "Clone Target",
      "Clone All (Spam)"
    };

    for (int i = 0; i < 2; i++) {
      char itemStr[32];
      bool selected = (menuSelection == i);
      snprintf(itemStr, sizeof(itemStr), "%s %s",
              selected ? ">" : " ", menuItems[i]);
      u8g2.drawStr(0, 28 + i * 16, itemStr);
    }

    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(0, 62, "U/D=NAV R=OK SEL=Exit");
  }

  u8g2.sendBuffer();
  displayMirrorSend(u8g2);
}

static void drawCloneSelect() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);

  char headerStr[32];
  snprintf(headerStr, sizeof(headerStr), "Select Target %d/%d",
           cloneTargetIndex + 1, (int)bleDevices.size());
  u8g2.drawStr(0, 12, headerStr);

  if (cloneTargetIndex >= (int)bleDevices.size()) {
    cloneTargetIndex = 0;
  }
  auto &target = bleDevices[cloneTargetIndex];
  char targetInfo[32];
  char maskedName[33];
  maskName(target.name, maskedName, sizeof(maskedName) - 1);
  snprintf(targetInfo, sizeof(targetInfo), "%.14s", maskedName);
  u8g2.drawStr(0, 24, targetInfo);

  char addrInfo[32];
  char maskedAddress[18];
  maskMAC(target.address, maskedAddress);
  snprintf(addrInfo, sizeof(addrInfo), "%.17s", maskedAddress);
  u8g2.drawStr(0, 36, addrInfo);

  u8g2.setFont(u8g2_font_5x8_tr);
  char dataInfo[32];
  snprintf(dataInfo, sizeof(dataInfo), "Adv:%d Rsp:%d",
           target.payloadLength, target.scanResponseLength);
  u8g2.drawStr(0, 48, dataInfo);

  u8g2.drawStr(0, 62, "L=Back U/D=Scroll R=Start");

  u8g2.sendBuffer();
  displayMirrorSend(u8g2);
}

static void drawCloneRunning() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 12, "Advertising");

  if (cloneTargetIndex >= (int)bleDevices.size()) {
    cloneTargetIndex = 0;
  }
  auto &target = bleDevices[cloneTargetIndex];

  char nameStr[20];
  char maskedName[33];
  maskName(target.name, maskedName, sizeof(maskedName) - 1);
  snprintf(nameStr, sizeof(nameStr), "%.14s", maskedName);
  u8g2.drawStr(0, 24, nameStr);

  char addrStr[20];
  char maskedAddress[18];
  maskMAC(target.address, maskedAddress);
  snprintf(addrStr, sizeof(addrStr), "%.17s", maskedAddress);
  u8g2.drawStr(0, 36, addrStr);

  u8g2.setFont(u8g2_font_5x8_tr);
  char statusStr[32];
  snprintf(statusStr, sizeof(statusStr), "Adv:%d Rsp:%d %s",
           target.payloadLength, target.scanResponseLength,
           isAdvertising ? "ON" : "OFF");
  u8g2.drawStr(0, 48, statusStr);

  u8g2.drawStr(0, 62, "L=Back SEL=Exit");

  u8g2.sendBuffer();
  displayMirrorSend(u8g2);
}

static void drawCloneAllRunning() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 12, "Clone All Spam");

  char countStr[32];
  snprintf(countStr, sizeof(countStr), "Targets: %d", (int)bleDevices.size());
  u8g2.drawStr(0, 28, countStr);

  u8g2.drawStr(0, 44, "Advertising...");

  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.drawStr(0, 62, "L=Back SEL=Exit");

  u8g2.sendBuffer();
  displayMirrorSend(u8g2);
}

static void initializeAdvertising() {
  if (!bleInitialized) {
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

    bleInitialized = true;
  }
}

static void startSingleClone() {
  if (bleDevices.empty() || isAdvertising) return;

  if (cloneTargetIndex >= (int)bleDevices.size()) {
    cloneTargetIndex = 0;
  }

  initializeAdvertising();

  auto &device = bleDevices[cloneTargetIndex];

  esp_ble_gap_stop_advertising();
  delay(50);

  esp_ble_gap_set_device_name(device.name);
  delay(50);

  esp_ble_gap_set_rand_addr(device.bdAddr);
  delay(50);

  if (device.payloadLength > 0) {
    esp_ble_gap_config_adv_data_raw(device.payload, device.payloadLength);
    delay(20);
  }

  if (device.scanResponseLength > 0) {
    esp_ble_gap_config_scan_rsp_data_raw(device.scanResponse, device.scanResponseLength);
    delay(20);
  }

  delay(50);

  esp_ble_adv_params_t device_adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = (esp_ble_adv_type_t)device.advType,
    .own_addr_type      = BLE_ADDR_TYPE_RANDOM,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
  };

  esp_err_t ret = esp_ble_gap_start_advertising(&device_adv_params);

  if (ret == ESP_OK) {
    isAdvertising = true;
    lastAdvertiseTime = millis();
  }
}

static void startCloneAllSpam() {
  if (bleDevices.empty()) return;

  currentCloneAllIndex = 0;
  cloneTargetIndex = 0;
  startSingleClone();
}

static void stopAdvertising() {
  if (!isAdvertising) return;
  esp_ble_gap_stop_advertising();
  delay(5);
  isAdvertising = false;
}

void bleSpooferSetup() {
  menuSelection = 0;
  cloneTargetIndex = 0;
  currentCloneAllIndex = 0;
  isAdvertising = false;
  lastButtonPress = 0;
  currentState = SPOOFER_MENU;
  bleInitialized = false;
  needsRedraw = true;
  lastState = SPOOFER_MENU;
  lastRunningUpdate = 0;

  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tr);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(BTN_CENTER, INPUT_PULLUP);

  randomSeed((uint32_t)esp_random());
}

void bleSpooferLoop() {
  unsigned long now = millis();

  if (currentState != lastState) {
    lastState = currentState;
    needsRedraw = true;
  }

  static bool upPressed = false, downPressed = false;
  static bool rightPressed = false, leftPressed = false;
  bool upNow = digitalRead(BTN_UP) == LOW;
  bool downNow = digitalRead(BTN_DOWN) == LOW;
  bool rightNow = digitalRead(BTN_RIGHT) == LOW;
  bool leftNow = digitalRead(BTN_BACK) == LOW;
  bool centerNow = digitalRead(BTN_CENTER) == LOW;


  switch (currentState) {
    case SPOOFER_MENU:
      if (!bleDevices.empty()) {
        if (upNow && !upPressed) {
          menuSelection = (menuSelection - 1 + 2) % 2;
          needsRedraw = true;
          delay(200);
        }
        if (downNow && !downPressed) {
          menuSelection = (menuSelection + 1) % 2;
          needsRedraw = true;
          delay(200);
        }
        if (rightNow && !rightPressed) {
          switch (menuSelection) {
            case 0:
              currentState = SPOOFER_CLONE_SELECT;
              needsRedraw = true;
              break;
            case 1:
              startCloneAllSpam();
              currentState = SPOOFER_CLONE_ALL_RUNNING;
              needsRedraw = true;
              break;
          }
          delay(200);
        }
      }
      if (centerNow) {
        delay(200);
        return;
      }

      if (needsRedraw) {
        drawMainMenu();
        needsRedraw = false;
      }
      break;

    case SPOOFER_CLONE_SELECT:
      if (upNow && !upPressed) {
        cloneTargetIndex = (cloneTargetIndex - 1 + bleDevices.size()) % bleDevices.size();
        needsRedraw = true;
        delay(200);
      }
      if (downNow && !downPressed) {
        cloneTargetIndex = (cloneTargetIndex + 1) % bleDevices.size();
        needsRedraw = true;
        delay(200);
      }
      if (rightNow && !rightPressed) {
        startSingleClone();
        currentState = SPOOFER_CLONE_RUNNING;
        needsRedraw = true;
        delay(200);
      }
      if (leftNow && !leftPressed) {
        currentState = SPOOFER_MENU;
        needsRedraw = true;
        delay(200);
      }
      if (centerNow) {
        delay(200);
        return;
      }

      if (needsRedraw) {
        drawCloneSelect();
        needsRedraw = false;
      }
      break;

    case SPOOFER_CLONE_RUNNING:
      if (leftNow && !leftPressed) {
        stopAdvertising();
        currentState = SPOOFER_CLONE_SELECT;
        needsRedraw = true;
        delay(200);
      }
      if (centerNow) {
        stopAdvertising();
        delay(200);
        return;
      }

      if (now - lastRunningUpdate >= runningUpdateInterval) {
        lastRunningUpdate = now;
        needsRedraw = true;
      }

      if (needsRedraw) {
        drawCloneRunning();
        needsRedraw = false;
      }
      break;

    case SPOOFER_CLONE_ALL_RUNNING:
      if (leftNow && !leftPressed) {
        stopAdvertising();
        currentState = SPOOFER_MENU;
        needsRedraw = true;
        delay(200);
      }
      if (centerNow) {
        stopAdvertising();
        delay(200);
        return;
      }

      if (isAdvertising && now - lastAdvertiseTime > advertiseInterval) {
        stopAdvertising();
        delay(100);

        currentCloneAllIndex = (currentCloneAllIndex + 1) % bleDevices.size();
        cloneTargetIndex = currentCloneAllIndex;

        delay(50);
        startSingleClone();
      }

      if (now - lastRunningUpdate >= runningUpdateInterval) {
        lastRunningUpdate = now;
        needsRedraw = true;
      }

      if (needsRedraw) {
        drawCloneAllRunning();
        needsRedraw = false;
      }
      break;
  }

  upPressed = upNow;
  downPressed = downNow;
  rightPressed = rightNow;
  leftPressed = leftNow;

  delay(5);
}