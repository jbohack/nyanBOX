/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2026 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#pragma once

#include "esp_wifi.h"
#include "esp_bt_main.h"

bool initBLE();
void cleanupBLE();
bool initWiFi(wifi_mode_t mode);
void cleanupWiFi();
void cleanupRadio();