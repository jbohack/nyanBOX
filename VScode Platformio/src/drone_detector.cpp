/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    This file includes code derived from opendroneid-core-c
    https://github.com/opendroneid/opendroneid-core-c

    Licensed under the Apache License, Version 2.0
    http://www.apache.org/licenses/LICENSE-2.0

    Modifications have been made to the original code.

    SPDX-License-Identifier: MIT AND Apache-2.0
*/

#include "../include/drone_detector.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include "../include/setting.h"
#include "../include/pindefs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#include <vector>
#include <cstring>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

#define BTN_UP BUTTON_PIN_UP
#define BTN_DOWN BUTTON_PIN_DOWN
#define BTN_RIGHT BUTTON_PIN_RIGHT
#define BTN_BACK BUTTON_PIN_LEFT
#define BTN_CENTER BUTTON_PIN_CENTER

#define ODID_MESSAGE_SIZE 25
#define ODID_ID_SIZE 20
#define ODID_STR_SIZE 23
#define ODID_BASIC_ID_MAX_MESSAGES 2
#define ODID_AUTH_MAX_PAGES 1

enum ODID_messagetype {
    ODID_MESSAGETYPE_BASIC_ID = 0,
    ODID_MESSAGETYPE_LOCATION = 1,
    ODID_MESSAGETYPE_AUTH = 2,
    ODID_MESSAGETYPE_SELF_ID = 3,
    ODID_MESSAGETYPE_SYSTEM = 4,
    ODID_MESSAGETYPE_OPERATOR_ID = 5,
    ODID_MESSAGETYPE_PACKED = 0xF,
};

enum ODID_idtype {
    ODID_IDTYPE_NONE = 0,
    ODID_IDTYPE_SERIAL_NUMBER = 1,
    ODID_IDTYPE_CAA_REGISTRATION_ID = 2,
    ODID_IDTYPE_UTM_ASSIGNED_UUID = 3,
    ODID_IDTYPE_SPECIFIC_SESSION_ID = 4,
};

enum ODID_uatype {
    ODID_UATYPE_NONE = 0,
    ODID_UATYPE_AEROPLANE = 1,
    ODID_UATYPE_HELICOPTER_OR_MULTIROTOR = 2,
    ODID_UATYPE_GYROPLANE = 3,
    ODID_UATYPE_HYBRID_LIFT = 4,
    ODID_UATYPE_ORNITHOPTER = 5,
    ODID_UATYPE_GLIDER = 6,
    ODID_UATYPE_KITE = 7,
    ODID_UATYPE_FREE_BALLOON = 8,
    ODID_UATYPE_CAPTIVE_BALLOON = 9,
    ODID_UATYPE_AIRSHIP = 10,
    ODID_UATYPE_ROCKET = 12,
    ODID_UATYPE_OTHER = 15,
};

enum ODID_status {
    ODID_STATUS_UNDECLARED = 0,
    ODID_STATUS_GROUND = 1,
    ODID_STATUS_AIRBORNE = 2,
    ODID_STATUS_EMERGENCY = 3,
    ODID_STATUS_REMOTE_ID_SYSTEM_FAILURE = 4,
};

struct ODID_BasicID_data {
    uint8_t UAType;
    uint8_t IDType;
    char UASID[ODID_ID_SIZE + 1];
};

struct ODID_Location_data {
    uint8_t Status;
    float Direction;
    float SpeedHorizontal;
    float SpeedVertical;
    double Latitude;
    double Longitude;
    float AltitudeBaro;
    float AltitudeGeo;
    uint8_t HeightType;
    float Height;
    uint8_t HorizAccuracy;
    uint8_t VertAccuracy;
    uint8_t BaroAccuracy;
    uint8_t SpeedAccuracy;
    uint8_t TSAccuracy;
    float TimeStamp;
};

struct ODID_SelfID_data {
    uint8_t DescType;
    char Desc[ODID_STR_SIZE + 1];
};

struct ODID_System_data {
    uint8_t OperatorLocationType;
    uint8_t ClassificationType;
    double OperatorLatitude;
    double OperatorLongitude;
    uint16_t AreaCount;
    uint16_t AreaRadius;
    float AreaCeiling;
    float AreaFloor;
    uint8_t CategoryEU;
    uint8_t ClassEU;
    float OperatorAltitudeGeo;
    uint32_t Timestamp;
};

struct ODID_OperatorID_data {
    uint8_t OperatorIdType;
    char OperatorId[ODID_ID_SIZE + 1];
};

struct ODID_UAS_Data {
    ODID_BasicID_data BasicID[ODID_BASIC_ID_MAX_MESSAGES];
    ODID_Location_data Location;
    ODID_SelfID_data SelfID;
    ODID_System_data System;
    ODID_OperatorID_data OperatorID;
    uint8_t BasicIDValid[ODID_BASIC_ID_MAX_MESSAGES];
    uint8_t LocationValid;
    uint8_t SelfIDValid;
    uint8_t SystemValid;
    uint8_t OperatorIDValid;
};

const float SPEED_DIV[2] = {0.25f, 0.75f};
const float VSPEED_DIV = 0.5f;
const int32_t LATLON_MULT = 10000000;
const float ALT_DIV = 0.5f;
const int ALT_ADDER = 1000;
const float INV_DIR = 361.0f;
const float INV_SPEED_H = 255.0f;
const float INV_SPEED_V = 63.0f;
const float INV_ALT = -1000.0f;
const uint16_t INV_TIMESTAMP = 0xFFFF;

static void odid_initUasData(ODID_UAS_Data *data) {
    if (! data) return;
    memset(data, 0, sizeof(ODID_UAS_Data));
    
    for (int i = 0; i < ODID_BASIC_ID_MAX_MESSAGES; i++) {
        data->BasicIDValid[i] = 0;
    }
    data->LocationValid = 0;
    data->SelfIDValid = 0;
    data->SystemValid = 0;
    data->OperatorIDValid = 0;
    
    data->Location.Direction = INV_DIR;
    data->Location.SpeedHorizontal = INV_SPEED_H;
    data->Location.SpeedVertical = INV_SPEED_V;
    data->Location.AltitudeBaro = INV_ALT;
    data->Location.AltitudeGeo = INV_ALT;
    data->Location.Height = INV_ALT;
    data->Location.TimeStamp = INV_TIMESTAMP;
}

static float decodeDirection(uint8_t Direction_enc, uint8_t EWDirection) {
    if (EWDirection)
        return (float)Direction_enc + 180.0f;
    else
        return (float)Direction_enc;
}

static float decodeSpeedHorizontal(uint8_t Speed_enc, uint8_t mult) {
    if (Speed_enc == 255) return INV_SPEED_H;
    if (mult)
        return ((float)Speed_enc * SPEED_DIV[1]) + (255.0f * SPEED_DIV[0]);
    else
        return (float)Speed_enc * SPEED_DIV[0];
}

static float decodeSpeedVertical(int8_t SpeedVertical_enc) {
    if (SpeedVertical_enc == 63) return INV_SPEED_V;
    return (float)SpeedVertical_enc * VSPEED_DIV;
}

static double decodeLatLon(int32_t LatLon_enc) {
    return (double)LatLon_enc / (double)LATLON_MULT;
}

static float decodeAltitude(uint16_t Alt_enc) {
    if (Alt_enc == 0xFFFF) return INV_ALT;
    return (float)Alt_enc * ALT_DIV - (float)ALT_ADDER;
}

static float decodeTimeStamp(uint16_t Seconds_enc) {
    if (Seconds_enc == INV_TIMESTAMP)
        return (float)INV_TIMESTAMP;
    else
        return (float)Seconds_enc / 10.0f;
}

static uint16_t decodeAreaRadius(uint8_t Radius_enc) {
    return (uint16_t)((int)Radius_enc * 10);
}

static int decodeBasicIDMessage(ODID_BasicID_data *outData, const uint8_t *inEncoded) {
    if (!outData || !inEncoded) return -1;
    
    uint8_t msgType = (inEncoded[0] >> 4) & 0x0F;
    if (msgType != ODID_MESSAGETYPE_BASIC_ID) return -1;
    
    outData->IDType = (inEncoded[1] >> 4) & 0x0F;
    outData->UAType = inEncoded[1] & 0x0F;
    
    memcpy(outData->UASID, &inEncoded[2], ODID_ID_SIZE);
    outData->UASID[ODID_ID_SIZE] = '\0';
    
    return 0;
}

static int decodeLocationMessage(ODID_Location_data *outData, const uint8_t *inEncoded) {
    if (!outData || !inEncoded) return -1;
    
    uint8_t msgType = (inEncoded[0] >> 4) & 0x0F;
    if (msgType != ODID_MESSAGETYPE_LOCATION) return -1;

    outData->Status = (inEncoded[1] >> 4) & 0x0F;
    uint8_t SpeedMult = (inEncoded[1] >> 0) & 0x01;
    uint8_t EWDirection = (inEncoded[1] >> 1) & 0x01;
    uint8_t HeightType = (inEncoded[1] >> 2) & 0x01;

    outData->Direction = decodeDirection(inEncoded[2], EWDirection);

    outData->SpeedHorizontal = decodeSpeedHorizontal(inEncoded[3], SpeedMult);

    outData->SpeedVertical = decodeSpeedVertical((int8_t)inEncoded[4]);

    int32_t lat = (int32_t)((uint32_t)inEncoded[5] | 
                            ((uint32_t)inEncoded[6] << 8) | 
                            ((uint32_t)inEncoded[7] << 16) | 
                            ((uint32_t)inEncoded[8] << 24));
    outData->Latitude = decodeLatLon(lat);

    int32_t lon = (int32_t)((uint32_t)inEncoded[9] | 
                            ((uint32_t)inEncoded[10] << 8) | 
                            ((uint32_t)inEncoded[11] << 16) | 
                            ((uint32_t)inEncoded[12] << 24));
    outData->Longitude = decodeLatLon(lon);

    uint16_t altBaro = (uint16_t)inEncoded[13] | ((uint16_t)inEncoded[14] << 8);
    outData->AltitudeBaro = decodeAltitude(altBaro);

    uint16_t altGeo = (uint16_t)inEncoded[15] | ((uint16_t)inEncoded[16] << 8);
    outData->AltitudeGeo = decodeAltitude(altGeo);

    uint16_t height = (uint16_t)inEncoded[17] | ((uint16_t)inEncoded[18] << 8);
    outData->Height = decodeAltitude(height);
    outData->HeightType = HeightType;

    outData->HorizAccuracy = inEncoded[19] & 0x0F;
    outData->VertAccuracy = (inEncoded[19] >> 4) & 0x0F;

    outData->SpeedAccuracy = inEncoded[20] & 0x0F;
    outData->BaroAccuracy = (inEncoded[20] >> 4) & 0x0F;

    uint16_t timestamp = (uint16_t)inEncoded[21] | ((uint16_t)inEncoded[22] << 8);
    outData->TimeStamp = decodeTimeStamp(timestamp);

    outData->TSAccuracy = inEncoded[23] & 0x0F;
    
    return 0;
}

static int decodeSelfIDMessage(ODID_SelfID_data *outData, const uint8_t *inEncoded) {
    if (!outData || !inEncoded) return -1;
    
    uint8_t msgType = (inEncoded[0] >> 4) & 0x0F;
    if (msgType != ODID_MESSAGETYPE_SELF_ID) return -1;
    
    outData->DescType = inEncoded[1];
    memcpy(outData->Desc, &inEncoded[2], ODID_STR_SIZE);
    outData->Desc[ODID_STR_SIZE] = '\0';
    
    return 0;
}

static int decodeSystemMessage(ODID_System_data *outData, const uint8_t *inEncoded) {
    if (!outData || !inEncoded) return -1;
    
    uint8_t msgType = (inEncoded[0] >> 4) & 0x0F;
    if (msgType != ODID_MESSAGETYPE_SYSTEM) return -1;

    outData->OperatorLocationType = inEncoded[1] & 0x03;
    outData->ClassificationType = (inEncoded[1] >> 2) & 0x07;

    int32_t opLat = (int32_t)((uint32_t)inEncoded[2] | 
                              ((uint32_t)inEncoded[3] << 8) | 
                              ((uint32_t)inEncoded[4] << 16) | 
                              ((uint32_t)inEncoded[5] << 24));
    outData->OperatorLatitude = decodeLatLon(opLat);

    int32_t opLon = (int32_t)((uint32_t)inEncoded[6] | 
                              ((uint32_t)inEncoded[7] << 8) | 
                              ((uint32_t)inEncoded[8] << 16) | 
                              ((uint32_t)inEncoded[9] << 24));
    outData->OperatorLongitude = decodeLatLon(opLon);

    outData->AreaCount = (uint16_t)inEncoded[10] | ((uint16_t)inEncoded[11] << 8);

    outData->AreaRadius = decodeAreaRadius(inEncoded[12]);

    uint16_t ceiling = (uint16_t)inEncoded[13] | ((uint16_t)inEncoded[14] << 8);
    outData->AreaCeiling = decodeAltitude(ceiling);

    uint16_t floor = (uint16_t)inEncoded[15] | ((uint16_t)inEncoded[16] << 8);
    outData->AreaFloor = decodeAltitude(floor);

    outData->ClassEU = inEncoded[17] & 0x0F;
    outData->CategoryEU = (inEncoded[17] >> 4) & 0x0F;

    uint16_t opAlt = (uint16_t)inEncoded[18] | ((uint16_t)inEncoded[19] << 8);
    outData->OperatorAltitudeGeo = decodeAltitude(opAlt);

    outData->Timestamp = (uint32_t)inEncoded[20] | 
                         ((uint32_t)inEncoded[21] << 8) |
                         ((uint32_t)inEncoded[22] << 16) |
                         ((uint32_t)inEncoded[23] << 24);
    
    return 0;
}

static int decodeOperatorIDMessage(ODID_OperatorID_data *outData, const uint8_t *inEncoded) {
    if (!outData || !inEncoded) return -1;

    uint8_t msgType = (inEncoded[0] >> 4) & 0x0F;
    if (msgType != ODID_MESSAGETYPE_OPERATOR_ID) return -1;

    outData->OperatorIdType = inEncoded[1];

    memset(outData->OperatorId, 0, ODID_ID_SIZE + 1);
    strncpy(outData->OperatorId, (const char *)&inEncoded[2], ODID_ID_SIZE);

    return 0;
}

static uint8_t decodeMessageType(uint8_t byte) {
    return (byte >> 4) & 0x0F;
}

static int decodeOpenDroneID(ODID_UAS_Data *uasData, const uint8_t *msgData) {
    if (!uasData || !msgData) return -1;
    
    uint8_t msgType = decodeMessageType(msgData[0]);
    
    switch (msgType) {
        case ODID_MESSAGETYPE_BASIC_ID:
            for (int i = 0; i < ODID_BASIC_ID_MAX_MESSAGES; i++) {
                if (! uasData->BasicIDValid[i] || uasData->BasicID[i]. IDType == ODID_IDTYPE_NONE) {
                    if (decodeBasicIDMessage(&uasData->BasicID[i], msgData) == 0) {
                        uasData->BasicIDValid[i] = 1;
                        return 0;
                    }
                    break;
                }
            }
            break;
            
        case ODID_MESSAGETYPE_LOCATION:
            if (decodeLocationMessage(&uasData->Location, msgData) == 0) {
                uasData->LocationValid = 1;
                return 0;
            }
            break;
            
        case ODID_MESSAGETYPE_SELF_ID:  
            if (decodeSelfIDMessage(&uasData->SelfID, msgData) == 0) {
                uasData->SelfIDValid = 1;
                return 0;
            }
            break;
            
        case ODID_MESSAGETYPE_SYSTEM:  
            if (decodeSystemMessage(&uasData->System, msgData) == 0) {
                uasData->SystemValid = 1;
                return 0;
            }
            break;
            
        case ODID_MESSAGETYPE_OPERATOR_ID:  
            if (decodeOperatorIDMessage(&uasData->OperatorID, msgData) == 0) {
                uasData->OperatorIDValid = 1;
                return 0;
            }
            break;
            
        case ODID_MESSAGETYPE_PACKED:   
            if (msgData[1] == ODID_MESSAGE_SIZE && msgData[2] > 0) {
                int msgCount = msgData[2];
                for (int i = 0; i < msgCount && i < 9; i++) {
                    decodeOpenDroneID(uasData, &msgData[3 + i * ODID_MESSAGE_SIZE]);
                }
                return 0;
            }
            break;
    }
    
    return -1;
}

enum ScanPhase {
    PHASE_WIFI_INIT,
    PHASE_BLE_INIT,
    PHASE_COMPLETED
};

struct DroneData {
    char mac[18];
    char id[ODID_ID_SIZE + 1];
    int8_t rssi;
    uint8_t uaType;
    uint8_t idType;
    uint8_t status;
    double latitude;
    double longitude;
    float altitude;
    float speed;
    float direction;
    float height;
    char operatorId[ODID_ID_SIZE + 1];
    char description[ODID_STR_SIZE + 1];
    double operatorLatitude;
    double operatorLongitude;
    float operatorAltitude;
    unsigned long lastSeen;
    uint8_t messagesSeen;
    char detectionMethod[16];
    bool isWiFi;
};

static std::vector<DroneData> drones;

const int MAX_DRONES = 50;
const unsigned long debounceTime = 200;
const unsigned long locateUpdateInterval = 1000;
const unsigned long countdownUpdateInterval = 1000;
const unsigned long WIFI_SCAN_DURATION = 8000;
const unsigned long BLE_SCAN_DURATION = 8000;
const unsigned long SCAN_INTERVAL = 30000;
static const uint8_t FIXED_CHANNEL = 6;
static const uint8_t nan_dest[6] = {0x51, 0x6f, 0x9a, 0x01, 0x00, 0x00};

static int currentIndex = 0;
static int listStartIndex = 0;
static bool isDetailView = false;
static bool isLocateMode = false;
static int detailPage = 0;
static const int detailPageCount = 4;
static char locateTargetMac[18] = {0};
static unsigned long lastButtonPress = 0;

static bool needsRedraw = true;
static int lastDroneCount = 0;
static unsigned long lastLocateUpdate = 0;
static unsigned long lastCountdownUpdate = 0;

static ScanPhase currentPhase = PHASE_WIFI_INIT;
static bool isScanning = false;
static unsigned long lastScanTime = 0;
static unsigned long phaseStartTime = 0;
static bool wifiInitialized = false;
static bool bleInitialized = false;
static bool scanCompleted = false;

static void mac_to_string(const uint8_t *mac, char *str, size_t size) {
    snprintf(str, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static const char* get_ua_type_string(uint8_t type) {
    switch (type) {
        case ODID_UATYPE_AEROPLANE:  return "Plane";
        case ODID_UATYPE_HELICOPTER_OR_MULTIROTOR:  return "Multi/Heli";
        case ODID_UATYPE_GYROPLANE:  return "Gyroplane";
        case ODID_UATYPE_HYBRID_LIFT: return "Hybrid";
        case ODID_UATYPE_GLIDER: return "Glider";
        case ODID_UATYPE_KITE:  return "Kite";
        case ODID_UATYPE_FREE_BALLOON: return "Balloon";
        case ODID_UATYPE_AIRSHIP: return "Airship";
        case ODID_UATYPE_ROCKET: return "Rocket";
        default: return "Unknown";
    }
}

static const char* get_status_string(uint8_t status) {
    switch (status) {
        case ODID_STATUS_UNDECLARED: return "Undeclared";
        case ODID_STATUS_GROUND: return "Ground";
        case ODID_STATUS_AIRBORNE: return "Airborne";
        case ODID_STATUS_EMERGENCY: return "EMERGENCY";
        case ODID_STATUS_REMOTE_ID_SYSTEM_FAILURE: return "RID Failure";
        default: return "Unknown";
    }
}

static DroneData* findOrCreateDrone(const uint8_t *mac, int8_t rssi, const char *method, bool isWiFi) {
    char mac_str[18];
    mac_to_string(mac, mac_str, sizeof(mac_str));

    if (isLocateMode && strlen(locateTargetMac) > 0) {
        if (strcmp(mac_str, locateTargetMac) != 0) {
            return nullptr;
        }
    }

    for (auto &d : drones) {
        if (strcmp(d.mac, mac_str) == 0) {
            d.rssi = rssi;
            d.lastSeen = millis();
            if (strstr(d.detectionMethod, method) == nullptr) {
                if (strlen(d.detectionMethod) > 0 && strcmp(d.detectionMethod, method) != 0) {
                    strcpy(d.detectionMethod, "WiFi+BLE");
                } else {
                    strcpy(d.detectionMethod, method);
                }
            }
            return &d;
        }
    }

    if (drones.size() >= MAX_DRONES) return nullptr;

    DroneData newDrone = {};
    strcpy(newDrone.mac, mac_str);
    newDrone.rssi = rssi;
    newDrone.lastSeen = millis();
    strcpy(newDrone.detectionMethod, method);

    snprintf(newDrone.id, sizeof(newDrone.id), "Drone-%02X%02X", mac[4], mac[5]);
    strcpy(newDrone.operatorId, "N/A");
    strcpy(newDrone.description, "N/A");
    newDrone.latitude = 0;
    newDrone.longitude = 0;
    newDrone.operatorLatitude = 0;
    newDrone.operatorLongitude = 0;
    newDrone.operatorAltitude = INV_ALT;
    newDrone.altitude = INV_ALT;
    newDrone.speed = INV_SPEED_H;
    newDrone.direction = INV_DIR;
    newDrone.height = INV_ALT;
    newDrone.status = ODID_STATUS_UNDECLARED;
    newDrone.uaType = ODID_UATYPE_NONE;
    newDrone.idType = ODID_IDTYPE_NONE;
    newDrone.messagesSeen = 0;
    newDrone.isWiFi = isWiFi;

    drones.push_back(newDrone);
    if (! isLocateMode) needsRedraw = true;
    return &drones.back();
}

static void updateDroneFromUASData(const uint8_t *mac, int8_t rssi, const char *method, ODID_UAS_Data *UAS_Data, bool isWiFi) {
    DroneData *drone = findOrCreateDrone(mac, rssi, method, isWiFi);
    if (!drone) return;

    for (int i = 0; i < ODID_BASIC_ID_MAX_MESSAGES; i++) {
        if (UAS_Data->BasicIDValid[i]) {
            drone->messagesSeen |= (1 << 0);
            drone->idType = UAS_Data->BasicID[i].IDType;
            drone->uaType = UAS_Data->BasicID[i].UAType;
            
            strncpy(drone->id, UAS_Data->BasicID[i]. UASID, ODID_ID_SIZE);
            drone->id[ODID_ID_SIZE] = '\0';

            for (int j = 0; j < ODID_ID_SIZE; j++) {
                if (drone->id[j] < 32 || drone->id[j] > 126) {
                    drone->id[j] = '\0';
                    break;
                }
            }

            if (!isLocateMode) needsRedraw = true;
            break;
        }
    }

    if (UAS_Data->LocationValid) {
        drone->messagesSeen |= (1 << 1);
        drone->status = UAS_Data->Location. Status;
        drone->latitude = UAS_Data->Location. Latitude;
        drone->longitude = UAS_Data->Location. Longitude;
        drone->altitude = UAS_Data->Location.AltitudeGeo;
        drone->height = UAS_Data->Location. Height;
        drone->speed = UAS_Data->Location.SpeedHorizontal;
        drone->direction = UAS_Data->Location. Direction;
        if (!isLocateMode) needsRedraw = true;
    }

    if (UAS_Data->SelfIDValid) {
        drone->messagesSeen |= (1 << 3);
        strncpy(drone->description, UAS_Data->SelfID. Desc, ODID_STR_SIZE);
        drone->description[ODID_STR_SIZE] = '\0';
        
        for (int j = 0; j < ODID_STR_SIZE; j++) {
            if (drone->description[j] < 32 || drone->description[j] > 126) {
                drone->description[j] = '\0';
                break;
            }
        }
        if (!isLocateMode) needsRedraw = true;
    }

    if (UAS_Data->SystemValid) {
        drone->messagesSeen |= (1 << 4);
        drone->operatorLatitude = UAS_Data->System.OperatorLatitude;
        drone->operatorLongitude = UAS_Data->System.OperatorLongitude;
        drone->operatorAltitude = UAS_Data->System.OperatorAltitudeGeo;
        if (!isLocateMode) needsRedraw = true;
    }

    if (UAS_Data->OperatorIDValid) {
        drone->messagesSeen |= (1 << 5);
        strncpy(drone->operatorId, UAS_Data->OperatorID. OperatorId, ODID_ID_SIZE);
        drone->operatorId[ODID_ID_SIZE] = '\0';
        
        for (int j = 0; j < ODID_ID_SIZE; j++) {
            if (drone->operatorId[j] < 32 || drone->operatorId[j] > 126) {
                drone->operatorId[j] = '\0';
                break;
            }
        }
        if (!isLocateMode) needsRedraw = true;
    }
}

static void wifi_sniffer_packet_handler(void *buff, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;

    wifi_promiscuous_pkt_t *packet = (wifi_promiscuous_pkt_t *)buff;
    uint8_t *payload = packet->payload;
    int length = packet->rx_ctrl.sig_len;

    ODID_UAS_Data UAS_Data;
    odid_initUasData(&UAS_Data);

    if (length > 40 && memcmp(nan_dest, &payload[4], 6) == 0) {
        for (int offset = 26; offset < length - ODID_MESSAGE_SIZE; offset++) {
            if (decodeOpenDroneID(&UAS_Data, &payload[offset]) == 0) {
                updateDroneFromUASData(&payload[10], packet->rx_ctrl.rssi, "WiFi", &UAS_Data, true);
            }
        }
    }
    else if (length > 40 && payload[0] == 0x80) {
        int offset = 36;
        while (offset < length - 6) {
            uint8_t typ = payload[offset];
            uint8_t len = payload[offset + 1];

            if ((typ == 0xdd) && len >= 4 &&
                (((payload[offset + 2] == 0x90 && payload[offset + 3] == 0x3a && payload[offset + 4] == 0xe6)) ||
                 ((payload[offset + 2] == 0xfa && payload[offset + 3] == 0x0b && payload[offset + 4] == 0xbc)))) {
                
                int j = offset + 7;
                if (j < length - ODID_MESSAGE_SIZE) {
                    decodeOpenDroneID(&UAS_Data, &payload[j]);
                    updateDroneFromUASData(&payload[10], packet->rx_ctrl.rssi, "WiFi", &UAS_Data, true);
                }
            }
            offset += len + 2;
            if (offset >= length) break;
        }
    }
}

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x100,
    .scan_window = 0xA0,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE
};

static void process_ble_remoteid(const uint8_t *payload, size_t len, const uint8_t *mac, int8_t rssi) {
    if (len > 6 && payload[1] == 0x16 && payload[2] == 0xFA &&
        payload[3] == 0xFF && payload[4] == 0x0D) {
        
        ODID_UAS_Data UAS_Data;
        odid_initUasData(&UAS_Data);
        
        if (decodeOpenDroneID(&UAS_Data, &payload[6]) == 0) {
            updateDroneFromUASData(mac, rssi, "BLE", &UAS_Data, false);
        }
    }
}

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                isScanning = true;
                esp_ble_gap_start_scanning(8);
            }
            break;

        case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:  
            if (param->scan_start_cmpl. status != ESP_BT_STATUS_SUCCESS) {
                isScanning = false;
            }
            break;

        case ESP_GAP_BLE_SCAN_RESULT_EVT:
            if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
                uint8_t *adv_data = param->scan_rst.ble_adv;
                size_t adv_len = param->scan_rst.adv_data_len;
                uint8_t *mac = param->scan_rst.bda;
                int8_t rssi = param->scan_rst. rssi;

                process_ble_remoteid(adv_data, adv_len, mac, rssi);

                if (param->scan_rst.scan_rsp_len > 0) {
                    process_ble_remoteid(param->scan_rst. ble_adv + adv_len,
                                        param->scan_rst.scan_rsp_len, mac, rssi);
                }
            } else if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
                isScanning = false;
                if (isLocateMode) {
                    isScanning = true;
                    esp_ble_gap_start_scanning(8);
                }
            }
            break;

        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:  
            isScanning = false;
            break;

        default:
            break;
    }
}

static void init_wifi_sniffer() {
    if (wifiInitialized) return;

    wifi_mode_t currentMode;
    if (esp_wifi_get_mode(&currentMode) == ESP_OK) {
        esp_wifi_set_promiscuous(false);
        esp_wifi_stop();
        delay(50);
    } else {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
    }

    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    delay(50);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
    wifi_promiscuous_filter_t flt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
    esp_wifi_set_promiscuous_filter(&flt);
    esp_wifi_set_channel(FIXED_CHANNEL, WIFI_SECOND_CHAN_NONE);

    wifiInitialized = true;
}

static void stop_wifi_sniffer() {
    if (!wifiInitialized) return;
    esp_wifi_set_promiscuous(false);
    esp_wifi_stop();
}

static void init_ble_scanner() {
    if (bleInitialized) return;

    if (!btStarted()) {
        btStart();
        delay(50);
    }

    esp_bluedroid_status_t bt_state = esp_bluedroid_get_status();
    if (bt_state == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        esp_bluedroid_init();
        delay(50);
    }
    if (bt_state != ESP_BLUEDROID_STATUS_ENABLED) {
        esp_bluedroid_enable();
        delay(50);
    }

    esp_ble_gap_register_callback(esp_gap_cb);
    esp_ble_gap_set_scan_params(&ble_scan_params);
    bleInitialized = true;
}

static void stop_ble_scanner() {
    if (!bleInitialized) return;

    esp_ble_gap_stop_scanning();
    delay(50);

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
    bleInitialized = false;
}

static void drawList() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);

    if (! scanCompleted && ! isDetailView && !isLocateMode) {
        unsigned long now = millis();
        unsigned long elapsed = now - phaseStartTime;

        u8g2.drawStr(0, 10, "Drone Detector");

        char scanStr[32];
        if (currentPhase == PHASE_WIFI_INIT) {
            snprintf(scanStr, sizeof(scanStr), "WiFi CH:%d", FIXED_CHANNEL);
        } else if (currentPhase == PHASE_BLE_INIT) {
            snprintf(scanStr, sizeof(scanStr), "Scanning BLE.. .");
        }
        u8g2.drawStr(0, 22, scanStr);

        char countStr[32];
        snprintf(countStr, sizeof(countStr), "Found: %d", (int)drones.size());
        u8g2.drawStr(0, 34, countStr);

        int barWidth = 120;
        int barHeight = 10;
        int barX = (128 - barWidth) / 2;
        int barY = 38;

        u8g2.drawFrame(barX, barY, barWidth, barHeight);

        unsigned long phaseDuration = (currentPhase == PHASE_WIFI_INIT) ? WIFI_SCAN_DURATION : BLE_SCAN_DURATION;
        if (elapsed < phaseDuration) {
            int fillWidth = (elapsed * (barWidth - 4)) / phaseDuration;
            if (fillWidth > 0 && fillWidth < barWidth - 4) {
                u8g2.drawBox(barX + 2, barY + 2, fillWidth, barHeight - 4);
            }
        }

        u8g2.setFont(u8g2_font_5x8_tr);
        u8g2.drawStr(0, 62, "Press SEL to exit");

        u8g2.sendBuffer();
        displayMirrorSend(u8g2);
        return;
    }

    if (drones.empty()) {
        if (isContinuousScanEnabled()) {
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(0, 10, "Drone Detector");
            u8g2.drawStr(0, 22, "Scanning...");

            char countStr[32];
            snprintf(countStr, sizeof(countStr), "Found: %d", 0);
            u8g2.drawStr(0, 34, countStr);

            int barWidth = 120;
            int barHeight = 10;
            int barX = (128 - barWidth) / 2;
            int barY = 38;
            u8g2.drawFrame(barX, barY, barWidth, barHeight);

            u8g2.setFont(u8g2_font_5x8_tr);
            u8g2.drawStr(0, 62, "Press SEL to exit");
        } else {
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(0, 10, "No drones detected");
            u8g2.setFont(u8g2_font_5x8_tr);
            unsigned long now = millis();
            char timeStr[32];
            unsigned long timeLeft = (SCAN_INTERVAL - (now - lastScanTime)) / 1000;
            snprintf(timeStr, sizeof(timeStr), "Scanning in %lus", timeLeft);
            u8g2.drawStr(0, 30, timeStr);
            u8g2.drawStr(0, 45, "Press SEL to exit");
        }
        u8g2.sendBuffer();
        displayMirrorSend(u8g2);
        return;
    }

    char header[32];
    snprintf(header, sizeof(header), "Drones:  %d/%d",
             (int)drones.size(), MAX_DRONES);
    u8g2.drawStr(0, 10, header);

    for (int i = 0; i < 5; ++i) {
        int idx = listStartIndex + i;
        if (idx >= (int)drones.size()) break;

        auto &drone = drones[idx];
        if (idx == currentIndex) {
            u8g2.drawStr(0, 20 + i * 10, ">");
        }

        char line[32];
        const char *display_id = (drone.id[0] != '\0') ? drone.id : drone.mac;
        char maskedID[33];
        if (drone.id[0] != '\0') {
            maskName(drone.id, maskedID, sizeof(maskedID) - 1);
        } else {
            maskMAC(drone.mac, maskedID);
        }
        snprintf(line, sizeof(line), "%.12s %d", maskedID, drone.rssi);
        u8g2.drawStr(10, 20 + i * 10, line);
    }

    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

static void drawDetail() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x8_tr);

    if (currentIndex >= (int)drones.size()) {
        u8g2.drawStr(0, 30, "No drone selected");
        u8g2.sendBuffer();
        displayMirrorSend(u8g2);
        return;
    }

    auto &drone = drones[currentIndex];
    char buf[64];

    if (detailPage == 0) {
        const char *display_id = (drone.id[0] != '\0') ? drone.id : drone.mac;
        char maskedID[33];
        if (drone.id[0] != '\0') {
            maskName(drone.id, maskedID, sizeof(maskedID) - 1);
        } else {
            maskMAC(drone.mac, maskedID);
        }
        snprintf(buf, sizeof(buf), "ID: %s", maskedID);
        u8g2.drawStr(0, 10, buf);

        snprintf(buf, sizeof(buf), "Type: %s", get_ua_type_string(drone.uaType));
        u8g2.drawStr(0, 20, buf);

        snprintf(buf, sizeof(buf), "Status: %s", get_status_string(drone.status));
        u8g2.drawStr(0, 30, buf);

        snprintf(buf, sizeof(buf), "RSSI: %d dBm", drone.rssi);
        u8g2.drawStr(0, 40, buf);

        unsigned long age = (millis() - drone.lastSeen) / 1000;
        snprintf(buf, sizeof(buf), "Age: %lus", age);
        u8g2.drawStr(0, 50, buf);

    } else if (detailPage == 1) {
        if (drone.latitude != 0 || drone.longitude != 0) {
            if (isPrivacyModeEnabled()) {
                snprintf(buf, sizeof(buf), "Lat: **.**");
            } else {
                snprintf(buf, sizeof(buf), "Lat: %.6f", drone.latitude);
            }
            u8g2.drawStr(0, 10, buf);

            if (isPrivacyModeEnabled()) {
                snprintf(buf, sizeof(buf), "Lng: **.**");
            } else {
                snprintf(buf, sizeof(buf), "Lng: %.6f", drone.longitude);
            }
            u8g2.drawStr(0, 20, buf);

            if (drone.altitude > INV_ALT) {
                if (isPrivacyModeEnabled()) {
                    snprintf(buf, sizeof(buf), "Alt: **m");
                } else {
                    snprintf(buf, sizeof(buf), "Alt: %.1fm", drone.altitude);
                }
            } else {
                snprintf(buf, sizeof(buf), "Alt: N/A");
            }
            u8g2.drawStr(0, 30, buf);

            if (drone.speed < INV_SPEED_H) {
                snprintf(buf, sizeof(buf), "Spd: %.1fm/s", drone.speed);
            } else {
                snprintf(buf, sizeof(buf), "Spd: N/A");
            }
            u8g2.drawStr(0, 40, buf);

            if (drone.direction <= 360.0f) {
                snprintf(buf, sizeof(buf), "Dir: %d deg", (int)drone.direction);
            } else {
                snprintf(buf, sizeof(buf), "Dir: N/A");
            }
            u8g2.drawStr(0, 50, buf);
        } else {
            u8g2.drawStr(0, 30, "No location data");
        }

    } else if (detailPage == 2) {
        char maskedMAC[18];
        maskMAC(drone.mac, maskedMAC);
        snprintf(buf, sizeof(buf), "MAC: %s", maskedMAC);
        u8g2.drawStr(0, 10, buf);

        snprintf(buf, sizeof(buf), "Via: %s", drone.detectionMethod);
        u8g2.drawStr(0, 20, buf);

        snprintf(buf, sizeof(buf), "Msgs:%c%c%c%c%c%c",
                 (drone.messagesSeen & (1<<0)) ? 'B' : '-', // BasicID
                 (drone.messagesSeen & (1<<1)) ? 'L' : '-', // Location
                 (drone.messagesSeen & (1<<2)) ? 'A' : '-', // Area
                 (drone.messagesSeen & (1<<3)) ? 'S' : '-', // SelfID
                 (drone.messagesSeen & (1<<4)) ? 'Y' : '-', // System
                 (drone.messagesSeen & (1<<5)) ? 'O' : '-'); // OperatorID
        u8g2.drawStr(0, 30, buf);

    } else if (detailPage == 3) {
        if (drone.operatorId[0] && strcmp(drone.operatorId, "N/A") != 0) {
            char maskedOperatorId[33];
            maskName(drone.operatorId, maskedOperatorId, sizeof(maskedOperatorId) - 1);
            snprintf(buf, sizeof(buf), "Op: %.16s", maskedOperatorId);
            u8g2.drawStr(0, 10, buf);
        } else {
            u8g2.drawStr(0, 10, "Op: N/A");
        }

        if (drone.operatorLatitude != 0 || drone.operatorLongitude != 0) {
            if (isPrivacyModeEnabled()) {
                snprintf(buf, sizeof(buf), "OpLat: **.**");
            } else {
                snprintf(buf, sizeof(buf), "OpLat: %.6f", drone.operatorLatitude);
            }
            u8g2.drawStr(0, 20, buf);

            if (isPrivacyModeEnabled()) {
                snprintf(buf, sizeof(buf), "OpLng: **.**");
            } else {
                snprintf(buf, sizeof(buf), "OpLng: %.6f", drone.operatorLongitude);
            }
            u8g2.drawStr(0, 30, buf);

            if (drone.operatorAltitude > INV_ALT) {
                if (isPrivacyModeEnabled()) {
                    snprintf(buf, sizeof(buf), "OpAlt: **m");
                } else {
                    snprintf(buf, sizeof(buf), "OpAlt: %.1fm", drone.operatorAltitude);
                }
                u8g2.drawStr(0, 40, buf);
            }
        } else {
            u8g2.drawStr(0, 20, "OpLoc: N/A");
        }

        if (drone.description[0] && strcmp(drone.description, "N/A") != 0) {
            char maskedDescription[33];
            maskName(drone.description, maskedDescription, sizeof(maskedDescription) - 1);
            snprintf(buf, sizeof(buf), "Dsc: %.16s", maskedDescription);
            u8g2.drawStr(0, 50, buf);
        } else {
            u8g2.drawStr(0, 50, "Dsc: N/A");
        }
    }

    snprintf(buf, sizeof(buf), "L=Back U/D=Pg%d/%d R=Loc", detailPage + 1, detailPageCount);
    u8g2.drawStr(0, 60, buf);

    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

static void drawLocate() {
    u8g2.clearBuffer();

    if (currentIndex >= (int)drones.size()) {
        u8g2.setFont(u8g2_font_5x8_tr);
        u8g2.drawStr(0, 30, "Drone lost");
        u8g2.sendBuffer();
        displayMirrorSend(u8g2);
        return;
    }

    auto &drone = drones[currentIndex];
    u8g2.setFont(u8g2_font_5x8_tr);
    char buf[32];

    const char *display_id = (drone.id[0] != '\0') ? drone.id : drone.mac;
    char maskedID[33];
    if (drone.id[0] != '\0') {
        maskName(drone.id, maskedID, sizeof(maskedID) - 1);
    } else {
        maskMAC(drone.mac, maskedID);
    }
    snprintf(buf, sizeof(buf), "%.16s", maskedID);
    u8g2.drawStr(0, 8, buf);

    char maskedMAC[18];
    maskMAC(drone.mac, maskedMAC);
    snprintf(buf, sizeof(buf), "%s", maskedMAC);
    u8g2.drawStr(0, 16, buf);

    u8g2.setFont(u8g2_font_7x13B_tr);
    snprintf(buf, sizeof(buf), "RSSI: %d dBm", drone.rssi);
    u8g2.drawStr(0, 28, buf);

    u8g2.setFont(u8g2_font_5x8_tr);
    int rssiClamped = constrain(drone.rssi, -100, -40);
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
    u8g2.sendBuffer();
    displayMirrorSend(u8g2);
}

void droneDetectorSetup() {
    drones.clear();
    drones.reserve(MAX_DRONES);

    currentIndex = listStartIndex = 0;
    isDetailView = false;
    isLocateMode = false;
    detailPage = 0;
    lastButtonPress = 0;
    isScanning = true;
    needsRedraw = true;
    lastDroneCount = 0;
    lastLocateUpdate = 0;
    lastCountdownUpdate = 0;
    scanCompleted = false;
    currentPhase = PHASE_WIFI_INIT;
    phaseStartTime = 0;
    bleInitialized = false;
    wifiInitialized = false;

    u8g2.begin();
    u8g2.setFont(u8g2_font_6x10_tr);

    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(BTN_BACK, INPUT_PULLUP);
    pinMode(BTN_CENTER, INPUT_PULLUP);

    init_wifi_sniffer();
    phaseStartTime = millis();
    lastScanTime = millis();
}

void droneDetectorLoop() {
    unsigned long now = millis();

    unsigned long effectiveWifiScanDuration = WIFI_SCAN_DURATION;
    unsigned long effectiveBleScanDuration = BLE_SCAN_DURATION;
    unsigned long effectiveScanInterval = SCAN_INTERVAL;

    if (drones.empty() && isContinuousScanEnabled() && scanCompleted) {
        effectiveWifiScanDuration = 3000;
        effectiveBleScanDuration = 3000;
        effectiveScanInterval = 500;
    }

    bool shouldShowPhaseScreen = ! scanCompleted || (drones.empty() && isContinuousScanEnabled());

    if (shouldShowPhaseScreen && ! isDetailView && !isLocateMode && !scanCompleted) {
        if (currentPhase == PHASE_WIFI_INIT) {
            unsigned long elapsed = now - phaseStartTime;

            if (elapsed >= effectiveWifiScanDuration) {
                esp_wifi_set_promiscuous(false);
                esp_wifi_stop();
                delay(100);

                init_ble_scanner();
                currentPhase = PHASE_BLE_INIT;
                phaseStartTime = now;
                needsRedraw = true;
            } else {
                if ((lastDroneCount != (int)drones.size()) || (now - lastLocateUpdate >= 100)) {
                    lastDroneCount = (int)drones.size();
                    lastLocateUpdate = now;

                    u8g2.clearBuffer();
                    u8g2.setFont(u8g2_font_6x10_tr);
                    u8g2.drawStr(0, 10, "Drone Detector");

                    char scanStr[32];
                    snprintf(scanStr, sizeof(scanStr), "WiFi CH:%d", FIXED_CHANNEL);
                    u8g2.drawStr(0, 22, scanStr);

                    char countStr[32];
                    snprintf(countStr, sizeof(countStr), "Found: %d", (int)drones.size());
                    u8g2.drawStr(0, 34, countStr);

                    int barWidth = 120;
                    int barHeight = 10;
                    int barX = (128 - barWidth) / 2;
                    int barY = 38;

                    u8g2.drawFrame(barX, barY, barWidth, barHeight);

                    int fillWidth = (elapsed * (barWidth - 4)) / effectiveWifiScanDuration;
                    if (fillWidth > 0 && fillWidth < barWidth - 4) {
                        u8g2.drawBox(barX + 2, barY + 2, fillWidth, barHeight - 4);
                    }

                    u8g2.setFont(u8g2_font_5x8_tr);
                    u8g2.drawStr(0, 62, "Press SEL to exit");

                    u8g2.sendBuffer();
                    displayMirrorSend(u8g2);
                }
            }
            return;

        } else if (currentPhase == PHASE_BLE_INIT) {
            unsigned long elapsed = now - phaseStartTime;

            if (! isScanning && elapsed >= effectiveBleScanDuration) {
                if (bleInitialized) {
                    esp_ble_gap_stop_scanning();
                    delay(50);

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
                    bleInitialized = false;
                }

                wifi_mode_t currentMode;
                if (esp_wifi_get_mode(&currentMode) != ESP_OK) {
                    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
                    esp_wifi_init(&cfg);
                    esp_wifi_set_storage(WIFI_STORAGE_RAM);
                }

                esp_wifi_set_mode(WIFI_MODE_STA);
                esp_wifi_start();
                delay(50);
                esp_wifi_set_ps(WIFI_PS_NONE);
                esp_wifi_set_promiscuous(false);
                wifiInitialized = true;

                currentPhase = PHASE_COMPLETED;
                scanCompleted = true;
                lastScanTime = now;
                needsRedraw = true;
            } else {
                if ((lastDroneCount != (int)drones.size()) || (now - lastLocateUpdate >= 100)) {
                    lastDroneCount = (int)drones.size();
                    lastLocateUpdate = now;

                    u8g2.clearBuffer();
                    u8g2.setFont(u8g2_font_6x10_tr);
                    u8g2.drawStr(0, 10, "Drone Detector");
                    u8g2.drawStr(0, 22, "Scanning BLE...");

                    char countStr[32];
                    snprintf(countStr, sizeof(countStr), "Found: %d", (int)drones.size());
                    u8g2.drawStr(0, 34, countStr);

                    int barWidth = 120;
                    int barHeight = 10;
                    int barX = (128 - barWidth) / 2;
                    int barY = 38;

                    u8g2.drawFrame(barX, barY, barWidth, barHeight);

                    int fillWidth = (elapsed * (barWidth - 4)) / effectiveBleScanDuration;
                    if (fillWidth > 0 && fillWidth < barWidth - 4) {
                        u8g2.drawBox(barX + 2, barY + 2, fillWidth, barHeight - 4);
                    }

                    u8g2.setFont(u8g2_font_5x8_tr);
                    u8g2.drawStr(0, 62, "Press SEL to exit");

                    u8g2.sendBuffer();
                    displayMirrorSend(u8g2);
                }
            }
            return;
        }
    }

    if (scanCompleted && now - lastScanTime > effectiveScanInterval && !isDetailView && !isLocateMode) {
        if (drones.size() >= MAX_DRONES) {
            std::sort(drones.begin(), drones.end(),
                    [](const DroneData &a, const DroneData &b) {
                        if (a.lastSeen != b.lastSeen) {
                            return a.lastSeen < b.lastSeen;
                        }
                        return a.rssi < b.rssi;
                    });

            int devicesToRemove = MAX_DRONES / 4;
            if (devicesToRemove > 0) {
                drones.erase(drones. begin(), drones.begin() + devicesToRemove);
            }

            currentIndex = listStartIndex = 0;
        }

        wifi_mode_t currentMode;
        if (esp_wifi_get_mode(&currentMode) != ESP_OK) {
            wifiInitialized = false;
            init_wifi_sniffer();
        } else {
            esp_wifi_set_promiscuous(true);
            esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
            wifi_promiscuous_filter_t flt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
            esp_wifi_set_promiscuous_filter(&flt);
            esp_wifi_set_channel(FIXED_CHANNEL, WIFI_SECOND_CHAN_NONE);
        }

        currentPhase = PHASE_WIFI_INIT;
        scanCompleted = false;
        phaseStartTime = now;
        lastScanTime = now;
        return;
    }

    if (scanCompleted && now - lastButtonPress > debounceTime) {
        if (!isDetailView && !isLocateMode && digitalRead(BTN_UP) == LOW && currentIndex > 0) {
            --currentIndex;
            if (currentIndex < listStartIndex)
                --listStartIndex;
            lastButtonPress = now;
            needsRedraw = true;
        } else if (!isDetailView && !isLocateMode && digitalRead(BTN_DOWN) == LOW &&
                   currentIndex < (int)drones.size() - 1) {
            ++currentIndex;
            if (currentIndex >= listStartIndex + 5)
                ++listStartIndex;
            lastButtonPress = now;
            needsRedraw = true;
        } else if (!isDetailView && !isLocateMode && digitalRead(BTN_RIGHT) == LOW &&
                   ! drones.empty()) {
            isDetailView = true;
            detailPage = 0;
            esp_wifi_set_promiscuous(false);
            lastButtonPress = now;
            needsRedraw = true;
        } else if (isDetailView && ! isLocateMode && digitalRead(BTN_UP) == LOW) {
            if (detailPage > 0) {
                --detailPage;
            } else {
                detailPage = detailPageCount - 1;
            }
            lastButtonPress = now;
            needsRedraw = true;
        } else if (isDetailView && !isLocateMode && digitalRead(BTN_DOWN) == LOW) {
            if (detailPage < detailPageCount - 1) {
                ++detailPage;
            } else {
                detailPage = 0;
            }
            lastButtonPress = now;
            needsRedraw = true;
        } else if (isDetailView && ! isLocateMode && digitalRead(BTN_RIGHT) == LOW &&
                   !drones.empty()) {
            isLocateMode = true;
            strncpy(locateTargetMac, drones[currentIndex]. mac, sizeof(locateTargetMac) - 1);
            locateTargetMac[sizeof(locateTargetMac) - 1] = '\0';

            if (drones[currentIndex].isWiFi) {
                wifi_mode_t currentMode;
                if (esp_wifi_get_mode(&currentMode) != ESP_OK) {
                    wifiInitialized = false;
                    init_wifi_sniffer();
                } else {
                    esp_wifi_set_promiscuous(true);
                    esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
                    wifi_promiscuous_filter_t flt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
                    esp_wifi_set_promiscuous_filter(&flt);
                    esp_wifi_set_channel(FIXED_CHANNEL, WIFI_SECOND_CHAN_NONE);
                }
            } else {
                esp_wifi_set_promiscuous(false);
                esp_wifi_stop();
                delay(100);

                init_ble_scanner();
            }

            lastButtonPress = now;
            lastLocateUpdate = now;
            needsRedraw = true;
        } else if (isLocateMode && digitalRead(BTN_BACK) == LOW) {
            isLocateMode = false;
            memset(locateTargetMac, 0, sizeof(locateTargetMac));

            if (bleInitialized) {
                esp_ble_gap_stop_scanning();
                delay(50);

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
                bleInitialized = false;

                wifi_mode_t currentMode;
                if (esp_wifi_get_mode(&currentMode) != ESP_OK) {
                    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
                    esp_wifi_init(&cfg);
                    esp_wifi_set_storage(WIFI_STORAGE_RAM);
                }

                esp_wifi_set_mode(WIFI_MODE_STA);
                esp_wifi_start();
                delay(50);
                esp_wifi_set_ps(WIFI_PS_NONE);
                wifiInitialized = true;
            } else {
                esp_wifi_set_promiscuous(false);
            }

            lastButtonPress = now;
            lastScanTime = now;
            needsRedraw = true;
        } else if (isDetailView && ! isLocateMode && digitalRead(BTN_BACK) == LOW) {
            isDetailView = false;
            detailPage = 0;
            lastButtonPress = now;
            needsRedraw = true;
        }
    }

    if (drones.empty()) {
        if (currentIndex != 0 || isDetailView || isLocateMode) {
            needsRedraw = true;
        }
        currentIndex = listStartIndex = 0;
        isDetailView = false;
        isLocateMode = false;
        detailPage = 0;
        memset(locateTargetMac, 0, sizeof(locateTargetMac));
    } else {
        currentIndex = constrain(currentIndex, 0, (int)drones.size() - 1);
        listStartIndex = constrain(listStartIndex, 0, max(0, (int)drones.size() - 5));
    }

    if (isDetailView && now - lastLocateUpdate >= locateUpdateInterval) {
        lastLocateUpdate = now;
        needsRedraw = true;
    }

    if (isLocateMode && now - lastLocateUpdate >= locateUpdateInterval) {
        lastLocateUpdate = now;
        needsRedraw = true;
    }

    if (drones.empty() && scanCompleted && now - lastCountdownUpdate >= countdownUpdateInterval) {
        lastCountdownUpdate = now;
        needsRedraw = true;
    }

    if (! needsRedraw) {
        return;
    }

    needsRedraw = false;

    if (isLocateMode) {
        drawLocate();
    } else if (isDetailView) {
        drawDetail();
    } else {
        drawList();
    }
}

void cleanupDroneDetector() {
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        esp_wifi_set_promiscuous(false);
        esp_wifi_stop();
        delay(50);
        esp_wifi_deinit();
        delay(100);
    }

    esp_bluedroid_status_t bt_state = esp_bluedroid_get_status();
    if (bt_state == ESP_BLUEDROID_STATUS_ENABLED) {
        esp_ble_gap_stop_scanning();
        delay(50);
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