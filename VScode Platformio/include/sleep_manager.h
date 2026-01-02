/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#ifndef SLEEP_MANAGER_H
#define SLEEP_MANAGER_H

extern void updateLastActivity();
extern void checkIdle();
extern void wakeDisplay();
extern bool anyButtonPressed();
extern void updateSleepTimeout(unsigned long newTimeout);

#endif