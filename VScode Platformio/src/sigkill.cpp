/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include <Arduino.h>
#include "../include/sigkill.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "../include/icon.h"
#include "../include/pindefs.h"
#include <esp_bt_main.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

RF24 radio_1(RADIO_CE_PIN_1, RADIO_CSN_PIN_1, 16000000);
RF24 radio_2(RADIO_CE_PIN_2, RADIO_CSN_PIN_2, 16000000);
RF24 radio_3(RADIO_CE_PIN_3, RADIO_CSN_PIN_3, 16000000);

enum SigKillMode { SIG_MENU, SIG_JAMMING };
enum ProtocolType { ALL, WIFI, BLUETOOTH, BLE, VIDEO_TX, RC, USB_WIRELESS, ZIGBEE, NRF24 };

static SigKillMode currentMode = SIG_MENU;
static ProtocolType selectedProtocol = ALL;
static int menuSelection = 0;
static unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 200;

static bool needsRedraw = true;
static int lastMenuSelection = -1;
static SigKillMode lastMode = SIG_MENU;
static ProtocolType lastProtocol = ALL;

// Protocol channel definitions
const byte bluetooth_channels[]        = {32,34,46,48,50,52,0,1,2,4,6,8,22,24,26,28,30,74,76,78,80};
const byte ble_channels[]              = {2,26,80};
const byte wifi_channels[]             = {1,2,3,4,5,6,7,8,9,10,11,12};
const byte usbWireless_channels[]      = {40,50,60};
const byte videoTransmitter_channels[] = {70,75,80};
const byte rc_channels[]               = {1,3,5,7};
const byte zigbee_channels[]           = {11,15,20,25};
const byte nrf24_channels[]            = {76,78,79};

const char* protocolNames[] = {
  "All", "WiFi", "Bluetooth", "BLE", "Video TX", "RC",
  "USB Wireless", "Zigbee", "NRF24"
};

void configureRadio(RF24 &radio, byte initialChannel) {
  radio.setAutoAck(false);
  radio.stopListening();
  radio.setRetries(0, 0);
  radio.setPALevel(RF24_PA_MAX, true);
  radio.setDataRate(RF24_2MBPS);
  radio.setCRCLength(RF24_CRC_DISABLED);
  radio.printPrettyDetails();
  
  radio.startConstCarrier(RF24_PA_MAX, initialChannel);
}

void initializeRadios() {
  SPI.begin();
  delay(100);
  
  if (radio_1.begin()) configureRadio(radio_1, 2);
  if (radio_2.begin()) configureRadio(radio_2, 26);
  if (radio_3.begin()) configureRadio(radio_3, 80);
}

void powerDownRadios() {
  radio_1.powerDown();
  radio_2.powerDown();
  radio_3.powerDown();
  delay(100);
}

static void drawSigMenu() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, "SigKill Mode:");

  int start = (menuSelection / 4) * 4;

  for (int i = 0; i < 4 && (start + i) < 9; i++) {
    int idx = start + i;
    char line[24];
    snprintf(line, sizeof(line), "%s %s",
             (idx == menuSelection) ? ">" : " ",
             protocolNames[idx]);
    u8g2.drawStr(0, 22 + (i * 10), line);
  }

  int scrollbarX = 122;
  int scrollbarWidth = 4;
  int scrollbarY = 14;
  int scrollbarHeight = 34;

  u8g2.drawFrame(scrollbarX, scrollbarY, scrollbarWidth, scrollbarHeight);

  int thumbHeight = (scrollbarHeight * 4) / 9;
  int maxThumbTravel = scrollbarHeight - thumbHeight - 2;
  int thumbY = scrollbarY + 1 + ((menuSelection * maxThumbTravel) / 8);

  u8g2.drawBox(scrollbarX + 1, thumbY, scrollbarWidth - 2, thumbHeight);

  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.drawStr(0, 62, "U/D=Move R=Start SEL=Exit");
  u8g2.sendBuffer();
  displayMirrorSend(u8g2);
}

static void drawActiveJamming(const char* protocolName) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  char title[32];
  snprintf(title, sizeof(title), "%s Jamming", protocolName);
  u8g2.drawStr(0, 12, title);
  u8g2.drawStr(0, 28, "Status: Active");
  u8g2.setFont(u8g2_font_5x8_tr);
  char radioStatus[32];
  snprintf(radioStatus, sizeof(radioStatus), "R1:%s R2:%s R3:%s",
           radio_1.isChipConnected() ? "ON" : "OFF",
           radio_2.isChipConnected() ? "ON" : "OFF",
           radio_3.isChipConnected() ? "ON" : "OFF");
  u8g2.drawStr(0, 42, radioStatus);
  u8g2.drawStr(0, 62, "L=Back SEL=Exit");
  u8g2.sendBuffer();
  displayMirrorSend(u8g2);
}

void sigkillSetup() {
  Serial.begin(115200);

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

  pinMode(BUTTON_PIN_UP, INPUT_PULLUP);
  pinMode(BUTTON_PIN_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_PIN_RIGHT, INPUT_PULLUP);
  pinMode(BUTTON_PIN_LEFT, INPUT_PULLUP);

  currentMode = SIG_MENU;
  menuSelection = 0;
  selectedProtocol = ALL;

  needsRedraw = true;
  lastMenuSelection = -1;
  lastMode = SIG_MENU;
  lastProtocol = ALL;

  powerDownRadios();
  drawSigMenu();
}

void sigkillLoop() {
  unsigned long now = millis();

  bool up = digitalRead(BUTTON_PIN_UP) == LOW;
  bool down = digitalRead(BUTTON_PIN_DOWN) == LOW;
  bool left = digitalRead(BUTTON_PIN_LEFT) == LOW;
  bool right = digitalRead(BUTTON_PIN_RIGHT) == LOW;

  if (lastMode != currentMode) {
    lastMode = currentMode;
    needsRedraw = true;
  }
  if (lastMenuSelection != menuSelection) {
    lastMenuSelection = menuSelection;
    needsRedraw = true;
  }
  if (lastProtocol != selectedProtocol) {
    lastProtocol = selectedProtocol;
    needsRedraw = true;
  }

  switch (currentMode) {
    case SIG_MENU:
      if (now - lastButtonPress > debounceDelay) {
        if (up) {
          menuSelection = (menuSelection - 1 + 9) % 9;
          needsRedraw = true;
          lastButtonPress = now;
        } else if (down) {
          menuSelection = (menuSelection + 1) % 9;
          needsRedraw = true;
          lastButtonPress = now;
        } else if (right) {
          selectedProtocol = (ProtocolType)menuSelection;
          currentMode = SIG_JAMMING;
          needsRedraw = true;
          initializeRadios();
          lastButtonPress = now;
        }
      }

      if (needsRedraw) {
        needsRedraw = false;
        drawSigMenu();
      }
      break;

    case SIG_JAMMING:
      if (needsRedraw) {
        needsRedraw = false;
        drawActiveJamming(protocolNames[selectedProtocol]);
      }

      {
        int randomIndex1, randomIndex2, randomIndex3;
        int channel1, channel2, channel3;

        switch (selectedProtocol) {
          case ALL:
            {
              static int protocolIndex = 0;
              const byte* channelArray;
              int arraySize;

              switch (protocolIndex % 8) {
                case 0: channelArray = wifi_channels; arraySize = sizeof(wifi_channels); break;
                case 1: channelArray = bluetooth_channels; arraySize = sizeof(bluetooth_channels); break;
                case 2: channelArray = ble_channels; arraySize = sizeof(ble_channels); break;
                case 3: channelArray = videoTransmitter_channels; arraySize = sizeof(videoTransmitter_channels); break;
                case 4: channelArray = rc_channels; arraySize = sizeof(rc_channels); break;
                case 5: channelArray = usbWireless_channels; arraySize = sizeof(usbWireless_channels); break;
                case 6: channelArray = zigbee_channels; arraySize = sizeof(zigbee_channels); break;
                case 7: channelArray = nrf24_channels; arraySize = sizeof(nrf24_channels); break;
              }

              randomIndex1 = random(0, arraySize / sizeof(byte));
              randomIndex2 = random(0, arraySize / sizeof(byte));
              randomIndex3 = random(0, arraySize / sizeof(byte));
              
              channel1 = channelArray[randomIndex1];
              channel2 = channelArray[randomIndex2];
              channel3 = channelArray[randomIndex3];
              
              radio_1.setChannel(channel1);
              radio_2.setChannel(channel2);
              radio_3.setChannel(channel3);

              protocolIndex++;
            }
            break;

          case WIFI:
            randomIndex1 = random(0, sizeof(wifi_channels) / sizeof(wifi_channels[0]));
            randomIndex2 = random(0, sizeof(wifi_channels) / sizeof(wifi_channels[0]));
            randomIndex3 = random(0, sizeof(wifi_channels) / sizeof(wifi_channels[0]));
            
            channel1 = wifi_channels[randomIndex1];
            channel2 = wifi_channels[randomIndex2];
            channel3 = wifi_channels[randomIndex3];
            
            radio_1.setChannel(channel1);
            radio_2.setChannel(channel2);
            radio_3.setChannel(channel3);
            break;

          case BLUETOOTH:
            randomIndex1 = random(0, sizeof(bluetooth_channels) / sizeof(bluetooth_channels[0]));
            randomIndex2 = random(0, sizeof(bluetooth_channels) / sizeof(bluetooth_channels[0]));
            randomIndex3 = random(0, sizeof(bluetooth_channels) / sizeof(bluetooth_channels[0]));
            
            channel1 = bluetooth_channels[randomIndex1];
            channel2 = bluetooth_channels[randomIndex2];
            channel3 = bluetooth_channels[randomIndex3];
            
            radio_1.setChannel(channel1);
            radio_2.setChannel(channel2);
            radio_3.setChannel(channel3);
            break;

          case BLE:
            randomIndex1 = random(0, sizeof(ble_channels) / sizeof(ble_channels[0]));
            randomIndex2 = random(0, sizeof(ble_channels) / sizeof(ble_channels[0]));
            randomIndex3 = random(0, sizeof(ble_channels) / sizeof(ble_channels[0]));
            
            channel1 = ble_channels[randomIndex1];
            channel2 = ble_channels[randomIndex2];
            channel3 = ble_channels[randomIndex3];
            
            radio_1.setChannel(channel1);
            radio_2.setChannel(channel2);
            radio_3.setChannel(channel3);
            break;

          case VIDEO_TX:
            randomIndex1 = random(0, sizeof(videoTransmitter_channels) / sizeof(videoTransmitter_channels[0]));
            randomIndex2 = random(0, sizeof(videoTransmitter_channels) / sizeof(videoTransmitter_channels[0]));
            randomIndex3 = random(0, sizeof(videoTransmitter_channels) / sizeof(videoTransmitter_channels[0]));
            
            channel1 = videoTransmitter_channels[randomIndex1];
            channel2 = videoTransmitter_channels[randomIndex2];
            channel3 = videoTransmitter_channels[randomIndex3];
            
            radio_1.setChannel(channel1);
            radio_2.setChannel(channel2);
            radio_3.setChannel(channel3);
            break;

          case RC:
            randomIndex1 = random(0, sizeof(rc_channels) / sizeof(rc_channels[0]));
            randomIndex2 = random(0, sizeof(rc_channels) / sizeof(rc_channels[0]));
            randomIndex3 = random(0, sizeof(rc_channels) / sizeof(rc_channels[0]));
            
            channel1 = rc_channels[randomIndex1];
            channel2 = rc_channels[randomIndex2];
            channel3 = rc_channels[randomIndex3];
            
            radio_1.setChannel(channel1);
            radio_2.setChannel(channel2);
            radio_3.setChannel(channel3);
            break;

          case USB_WIRELESS:
            randomIndex1 = random(0, sizeof(usbWireless_channels) / sizeof(usbWireless_channels[0]));
            randomIndex2 = random(0, sizeof(usbWireless_channels) / sizeof(usbWireless_channels[0]));
            randomIndex3 = random(0, sizeof(usbWireless_channels) / sizeof(usbWireless_channels[0]));
            
            channel1 = usbWireless_channels[randomIndex1];
            channel2 = usbWireless_channels[randomIndex2];
            channel3 = usbWireless_channels[randomIndex3];
            
            radio_1.setChannel(channel1);
            radio_2.setChannel(channel2);
            radio_3.setChannel(channel3);
            break;

          case ZIGBEE:
            randomIndex1 = random(0, sizeof(zigbee_channels) / sizeof(zigbee_channels[0]));
            randomIndex2 = random(0, sizeof(zigbee_channels) / sizeof(zigbee_channels[0]));
            randomIndex3 = random(0, sizeof(zigbee_channels) / sizeof(zigbee_channels[0]));
            
            channel1 = zigbee_channels[randomIndex1];
            channel2 = zigbee_channels[randomIndex2];
            channel3 = zigbee_channels[randomIndex3];
            
            radio_1.setChannel(channel1);
            radio_2.setChannel(channel2);
            radio_3.setChannel(channel3);
            break;

          case NRF24:
            randomIndex1 = random(0, sizeof(nrf24_channels) / sizeof(nrf24_channels[0]));
            randomIndex2 = random(0, sizeof(nrf24_channels) / sizeof(nrf24_channels[0]));
            randomIndex3 = random(0, sizeof(nrf24_channels) / sizeof(nrf24_channels[0]));
            
            channel1 = nrf24_channels[randomIndex1];
            channel2 = nrf24_channels[randomIndex2];
            channel3 = nrf24_channels[randomIndex3];
            
            radio_1.setChannel(channel1);
            radio_2.setChannel(channel2);
            radio_3.setChannel(channel3);
            break;
        }
      }

      if (left && now - lastButtonPress > debounceDelay) {
        powerDownRadios();
        currentMode = SIG_MENU;
        needsRedraw = true;
        lastButtonPress = now;
      }
      break;
  }
}