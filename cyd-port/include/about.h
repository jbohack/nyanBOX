/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2026 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#ifndef ABOUT_H
#define ABOUT_H

#include <U8g2lib.h>
#include "pindefs.h"

#define NYANBOX_VERSION "v4.27.11"
extern const char* nyanboxVersion;

void aboutSetup();
void aboutLoop();
void aboutCleanup();

#endif