/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#ifndef LEVEL_SYSTEM_H
#define LEVEL_SYSTEM_H

#include <Arduino.h>

void levelSystemSetup();
void levelSystemLoop();
void addXP(int amount);
int getCurrentLevel();
int getCurrentXP();
int getXPForNextLevel();
void displayLevelScreen();
void resetXPData();

#endif