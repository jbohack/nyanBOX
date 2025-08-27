/* ____________________________
   This software is licensed under the MIT License:
   https://github.com/jbohack/nyanBOX
   ________________________________________ */

#include <level_calculations.h>

int calculateXPRequiredForLevel(int level) {
    if (level <= 1) return 0;
    return (level * level) + 10;
}

int calculateCurrentLevelFromXP(int currentXP) {
    if (currentXP < 0) return 1;

    int level = 1;
    while (level < 99 && currentXP >= calculateXPRequiredForLevel(level + 1)) {
        level++;
    }
    return level;
}

const char* calculateRankName(int level) {
    if (level <= 5) return "N00b";
    else if (level <= 15) return "Skid";
    else if (level <= 25) return "Wannabe";
    else if (level <= 40) return "L33t";
    else if (level <= 55) return "Hacker";
    else if (level <= 70) return "Uber Hacker";
    else if (level <= 85) return "Elite";
    else if (level <= 95) return "Godlike";
    else return "Legend";
}

int calculateNewXP(int currentXP, int amount) {
    if (currentXP + amount > 65535) {
        return 65535;
    } else {
        return currentXP + amount;
    }
}
