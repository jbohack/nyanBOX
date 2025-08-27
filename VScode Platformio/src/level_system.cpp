/* ____________________________
   This software is licensed under the MIT License:
   https://github.com/jbohack/nyanBOX
   ________________________________________ */

#include <EEPROM.h>
#include <U8g2lib.h>
#include <level_calculations.h>
#include "../include/level_system.h" 
#include "../include/sleep_manager.h"
#include "../include/pindefs.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

#define EEPROM_ADDRESS_LEVEL_MAGIC 100
#define EEPROM_ADDRESS_XP_LOW 101
#define EEPROM_ADDRESS_XP_HIGH 102
#define LEVEL_MAGIC_NUMBER 0xAB

static int currentXP = 0;
static bool buttonCenterPressed = false;

void saveLevelData();

void loadLevelData() {
  uint8_t magic = EEPROM.read(EEPROM_ADDRESS_LEVEL_MAGIC);
  if (magic != LEVEL_MAGIC_NUMBER) {
    currentXP = 0;
    saveLevelData();
    return;
  }
  
  uint8_t xpLow = EEPROM.read(EEPROM_ADDRESS_XP_LOW);
  uint8_t xpHigh = EEPROM.read(EEPROM_ADDRESS_XP_HIGH);
  currentXP = (xpHigh << 8) | xpLow;
  
  if (currentXP < 0) currentXP = 0;
}

void saveLevelData() {
  EEPROM.write(EEPROM_ADDRESS_LEVEL_MAGIC, LEVEL_MAGIC_NUMBER);
  
  EEPROM.write(EEPROM_ADDRESS_XP_LOW, currentXP & 0xFF);
  EEPROM.write(EEPROM_ADDRESS_XP_HIGH, (currentXP >> 8) & 0xFF);
  
  EEPROM.commit();
}

void levelSystemSetup() {
  EEPROM.begin(512);
  loadLevelData();
  
  pinMode(BUTTON_PIN_CENTER, INPUT_PULLUP);
}

void addXP(int amount) {
  currentXP = calculateNewXP(currentXP, amount);
  saveLevelData();
}

int getCurrentLevel() {
  return calculateCurrentLevelFromXP(currentXP);
}

int getCurrentXP() {
  return currentXP;
}

int getXPForNextLevel() {
  int currentLevel = calculateCurrentLevelFromXP(currentXP);
  if (currentLevel >= 99) return 0;
  return calculateXPRequiredForLevel(currentLevel + 1);
}

void displayLevelScreen() {
  u8g2.clearBuffer();
  
  int currentLevel = calculateCurrentLevelFromXP(currentXP);
  
  u8g2.setFont(u8g2_font_helvB14_tr);
  char levelStr[8];
  sprintf(levelStr, "Level %d", currentLevel);
  int levelWidth = u8g2.getUTF8Width(levelStr);
  u8g2.drawStr((128 - levelWidth) / 2, 18, levelStr);
  
  const char* rankName = calculateRankName(currentLevel);
  u8g2.setFont(u8g2_font_helvR08_tr);
  int rankWidth = u8g2.getUTF8Width(rankName);
  u8g2.drawStr((128 - rankWidth) / 2, 32, rankName);
  
  u8g2.setFont(u8g2_font_6x10_tr);
  char xpStr[24];
  if (currentLevel >= 99) {
    sprintf(xpStr, "XP: %d (MAX)", currentXP);
  } else {
    int nextLevelXP = getXPForNextLevel();
    sprintf(xpStr, "XP: %d/%d", currentXP, nextLevelXP);
  }
  int xpWidth = u8g2.getUTF8Width(xpStr);
  u8g2.drawStr((128 - xpWidth) / 2, 44, xpStr);
  
  int barWidth = 100;
  int barX = (128 - barWidth) / 2;
  u8g2.drawFrame(barX, 50, barWidth, 4);
  
  int fillWidth = 0;
  if (currentLevel < 99) {
    int nextLevelXP = getXPForNextLevel();
    int currentLevelXP = calculateXPRequiredForLevel(currentLevel);
    if (nextLevelXP > currentLevelXP) {
      int progress = map(currentXP - currentLevelXP, 0, nextLevelXP - currentLevelXP, 0, 100);
      fillWidth = map(progress, 0, 100, 0, barWidth - 2);
    }
  } else {
    fillWidth = barWidth - 2;
  }
  
  if (fillWidth > 0) {
    u8g2.drawBox(barX + 1, 51, fillWidth, 2);
  }
  
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 64, "<- Back");
  
  u8g2.sendBuffer();
}

void resetXPData() {
  currentXP = 0;
  saveLevelData();
}

void levelSystemLoop() {
  displayLevelScreen();
}