#include <unity.h>
#include <level_calculations.h>

void test_xp_required_for_level_calculation() {
    TEST_ASSERT_EQUAL(0, calculateXPRequiredForLevel(-1));
    TEST_ASSERT_EQUAL(0, calculateXPRequiredForLevel(0));
    TEST_ASSERT_EQUAL(0, calculateXPRequiredForLevel(1));
    TEST_ASSERT_EQUAL(14, calculateXPRequiredForLevel(2));
    TEST_ASSERT_EQUAL(19, calculateXPRequiredForLevel(3));
    TEST_ASSERT_EQUAL(26, calculateXPRequiredForLevel(4));
    TEST_ASSERT_EQUAL(99*99 + 10, calculateXPRequiredForLevel(99));
}

void test_current_level_calculation() {
    TEST_ASSERT_EQUAL(1, calculateCurrentLevelFromXP(-1));
    TEST_ASSERT_EQUAL(1, calculateCurrentLevelFromXP(0));
    TEST_ASSERT_EQUAL(1, calculateCurrentLevelFromXP(1));
    TEST_ASSERT_EQUAL(1, calculateCurrentLevelFromXP(13));
    TEST_ASSERT_EQUAL(2, calculateCurrentLevelFromXP(14));
    TEST_ASSERT_EQUAL(2, calculateCurrentLevelFromXP(18));
    TEST_ASSERT_EQUAL(3, calculateCurrentLevelFromXP(19));
}

void test_rank_name_calculation() {
    TEST_ASSERT_EQUAL_STRING("N00b", calculateRankName(1));
    TEST_ASSERT_EQUAL_STRING("N00b", calculateRankName(5));
    TEST_ASSERT_EQUAL_STRING("Skid", calculateRankName(6));
    TEST_ASSERT_EQUAL_STRING("Skid", calculateRankName(15));
    TEST_ASSERT_EQUAL_STRING("Wannabe", calculateRankName(16));
    TEST_ASSERT_EQUAL_STRING("Wannabe", calculateRankName(25));
    TEST_ASSERT_EQUAL_STRING("L33t", calculateRankName(40));
    TEST_ASSERT_EQUAL_STRING("Hacker", calculateRankName(55));
    TEST_ASSERT_EQUAL_STRING("Uber Hacker", calculateRankName(70));
    TEST_ASSERT_EQUAL_STRING("Elite", calculateRankName(85));
    TEST_ASSERT_EQUAL_STRING("Godlike", calculateRankName(95));
    TEST_ASSERT_EQUAL_STRING("Legend", calculateRankName(99));
}

void test_increase_xp() {
    TEST_ASSERT_EQUAL(110, calculateNewXP(100, 10));
    TEST_ASSERT_EQUAL(100, calculateNewXP(0, 100));
    TEST_ASSERT_EQUAL(65535, calculateNewXP(65534, 1));
    TEST_ASSERT_EQUAL(65535, calculateNewXP(65535, 10));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_xp_required_for_level_calculation);
    RUN_TEST(test_current_level_calculation);
    RUN_TEST(test_rank_name_calculation);
    RUN_TEST(test_increase_xp);

    return UNITY_END();
}
