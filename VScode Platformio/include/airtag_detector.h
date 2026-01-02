/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#ifndef airtag_H
#define airtag_H

#include <U8g2lib.h>
#include <vector>
#include "neopixel.h"
#include "pindefs.h"

struct AirTagDeviceData {
  char name[32];
  char address[18];
  int8_t rssi;
  unsigned long lastSeen;
  uint8_t payload[64];
  size_t payloadLength;
  bool isAirTag;
};

extern std::vector<AirTagDeviceData> airtagDevices;

void airtagDetectorSetup();
void airtagDetectorLoop();

#endif