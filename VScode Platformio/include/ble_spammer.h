/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#ifndef BLE_SPAM_H
#define BLE_SPAM_H

#include <esp_bt.h>

extern bool isBleSpamming;

void bleSpamSetup();
void bleSpamLoop();

#endif
