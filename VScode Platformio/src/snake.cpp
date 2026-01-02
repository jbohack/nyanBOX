/*
    nyanBOX by Nyan Devices
    https://github.com/jbohack/nyanBOX
    Copyright (c) 2025 jbohack

    Licensed under the MIT License
    https://opensource.org/licenses/MIT

    SPDX-License-Identifier: MIT
*/

#include <Arduino.h>
#include <U8g2lib.h>
#include <EEPROM.h>
#include "snake.h"
#include "../include/sleep_manager.h"
#include "../include/display_mirror.h"
#include <esp_system.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

#define EEPROM_ADDRESS_HIGHSCORE_LOW 103
#define EEPROM_ADDRESS_HIGHSCORE_HIGH 104

static uint8_t snakeX[SNAKE_MAX];
static uint8_t snakeY[SNAKE_MAX];
static uint16_t snakeLen;
static uint8_t dir;
static uint8_t appleX, appleY;
static unsigned long lastMove;
static const unsigned long INTERVAL = 200;
static uint16_t score = 0;
static uint16_t highScore = 0;
static uint16_t savedHighScore = 0;

static bool needsRedraw = true;
static uint8_t lastDir = 0;

void loadHighScore() {
  uint8_t scoreLow = EEPROM.read(EEPROM_ADDRESS_HIGHSCORE_LOW);
  uint8_t scoreHigh = EEPROM.read(EEPROM_ADDRESS_HIGHSCORE_HIGH);
  uint16_t loadedScore = (scoreHigh << 8) | scoreLow;

  if (loadedScore == 0xFFFF || loadedScore > 512) {
    highScore = 0;
    savedHighScore = 0;
  } else {
    highScore = loadedScore;
    savedHighScore = loadedScore;
  }
}

void saveHighScore() {
  if (highScore > savedHighScore && highScore <= 512) {
    EEPROM.write(EEPROM_ADDRESS_HIGHSCORE_LOW, highScore & 0xFF);
    EEPROM.write(EEPROM_ADDRESS_HIGHSCORE_HIGH, (highScore >> 8) & 0xFF);
    EEPROM.commit();
    savedHighScore = highScore;
  }
}

void resetSnake(){
  snakeLen = 3;
  score = 0;
  snakeX[0] = SNAKE_COLS/2; snakeY[0] = SNAKE_ROWS/2;
  snakeX[1] = snakeX[0] - 1; snakeY[1] = snakeY[0];
  snakeX[2] = snakeX[1] - 1; snakeY[2] = snakeY[0];
  dir = 1;
  appleX = random(SNAKE_COLS);
  appleY = random(SNAKE_ROWS);
  lastMove = millis();
  needsRedraw = true;
}

void snakeSetup(){
  randomSeed((uint32_t)esp_random());
  loadHighScore();
  resetSnake();
  needsRedraw = true;
  lastDir = dir;
}

void snakeLoop(){
  if(digitalRead(BUTTON_PIN_UP)==LOW && dir!=2) {
    dir=0;
  }
  else if(digitalRead(BUTTON_PIN_RIGHT)==LOW && dir!=3) {
    dir=1;
  }
  else if(digitalRead(BUTTON_PIN_DOWN)==LOW && dir!=0) {
    dir=2;
  }
  else if(digitalRead(BUTTON_PIN_LEFT)==LOW && dir!=1) {
    dir=3;
  }

  if(lastDir != dir) {
    lastDir = dir;
    needsRedraw = true;
  }

  if(millis() - lastMove >= INTERVAL){
    lastMove = millis();
    needsRedraw = true;
    for(int i=snakeLen; i>0; i--){
      snakeX[i] = snakeX[i-1];
      snakeY[i] = snakeY[i-1];
    }
    switch(dir){
      case 0: snakeY[0] = snakeY[0] ? snakeY[0]-1 : SNAKE_ROWS-1; break;
      case 1: snakeX[0] = snakeX[0] < SNAKE_COLS-1 ? snakeX[0]+1 : 0; break;
      case 2: snakeY[0] = snakeY[0] < SNAKE_ROWS-1 ? snakeY[0]+1 : 0; break;
      case 3: snakeX[0] = snakeX[0] ? snakeX[0]-1 : SNAKE_COLS-1; break;
    }
    for(int i=1; i<snakeLen; i++){
      if(snakeX[i]==snakeX[0] && snakeY[i]==snakeY[0]){
        if(score > highScore) {
          highScore = score;
        }
        resetSnake();
        return;
      }
    }
    if(snakeX[0]==appleX && snakeY[0]==appleY){
      score++;
      if(score>highScore) {
        highScore = score;
      }
      if(snakeLen < SNAKE_MAX-1) snakeLen++;
      appleX = random(SNAKE_COLS);
      appleY = random(SNAKE_ROWS);
    }
  }

  if(!needsRedraw) {
    return;
  }

  needsRedraw = false;
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.setCursor(0,10); u8g2.print("Score:"); u8g2.print(score);
  u8g2.setCursor(64,10); u8g2.print("Hi:"); u8g2.print(highScore);
  u8g2.drawBox(appleX*SNAKE_CELL, appleY*SNAKE_CELL, SNAKE_CELL, SNAKE_CELL);
  for(int i=0; i<snakeLen; i++){
    u8g2.drawFrame(snakeX[i]*SNAKE_CELL, snakeY[i]*SNAKE_CELL, SNAKE_CELL, SNAKE_CELL);
  }
  u8g2.sendBuffer();
  displayMirrorSend(u8g2);
}

void snakeCleanup() {
  saveHighScore();
}

void setup(){
  snakeSetup();
}

void loop(){
  snakeLoop();
}