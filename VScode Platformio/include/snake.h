/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#ifndef SNAKE_H
#define SNAKE_H

#include <Arduino.h>
#include <U8g2lib.h>
#include "pindefs.h"

#define SNAKE_CELL   4
#define SNAKE_COLS  (128 / SNAKE_CELL)
#define SNAKE_ROWS  ( 64 / SNAKE_CELL)
#define SNAKE_MAX   (SNAKE_COLS * SNAKE_ROWS)

void snakeSetup();
void snakeLoop();
void snakeCleanup();

#endif
