/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2026 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#ifndef ble_inspector_H
#define ble_inspector_H

#include <vector>
#include <U8g2lib.h>
#include "neopixel.h"
#include "pindefs.h"

struct BLEDevice {
  char name[32];
  char address[18];
  uint8_t bdAddr[6];
  int8_t rssi;
  bool hasName;
  unsigned long lastSeen;
  uint8_t payload[62];
  size_t payloadLength;
  uint8_t scanResponse[62];
  size_t scanResponseLength;
  uint8_t advType;
  uint8_t addrType;
};

extern std::vector<BLEDevice> bleInspectorDevices;

void bleInspectorSetup();
void bleInspectorLoop();

#endif