/* ____________________________
   This software is licensed under the MIT License:
   https://github.com/jbohack/nyanBOX
   ________________________________________ */

#ifndef LEVEL_CALCULATIONS_H
#define LEVEL_CALCULATIONS_H

// Pure functions for level system calculations
// These functions have no side effects and are easily testable

/**
 * Calculate XP required to reach a specific level
 * @param level The target level (1-99)
 * @return XP required for that level
 */
int calculateXPRequiredForLevel(int level);

/**
 * Calculate current level based on total XP
 * @param currentXP Total experience points
 * @return Current level (1-99)
 */
int calculateCurrentLevelFromXP(int currentXP);

/**
 * Get rank name for a given level
 * @param level Player level
 * @return Rank name string
 */
const char* calculateRankName(int level);

/**
 * Add XP to the current XP
 * @param currentXP Current experience points
 * @param amount Amount of XP to add
 * @return New total XP
 */
int calculateNewXP(int currentXP, int amount);

#endif
