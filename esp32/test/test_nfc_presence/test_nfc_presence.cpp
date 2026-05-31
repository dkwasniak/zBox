#include <unity.h>

#include "nfc_presence.cpp"

static NfcPresenceState s_state;

void setUp()
{
    nfcPresenceReset(s_state);
}

void tearDown() {}

void test_new_card_emits_detected_once()
{
    auto first = nfcPresenceUpdate(s_state, "04:AA:BB", 1000, 3000);
    TEST_ASSERT_EQUAL_INT((int)NfcPresenceAction::Detected, (int)first.action);
    TEST_ASSERT_EQUAL_STRING("04:AA:BB", first.uid);

    auto second = nfcPresenceUpdate(s_state, "04:AA:BB", 2000, 3000);
    TEST_ASSERT_EQUAL_INT((int)NfcPresenceAction::None, (int)second.action);
}

void test_short_missing_read_does_not_emit_removed()
{
    nfcPresenceUpdate(s_state, "04:AA:BB", 1000, 3000);

    auto missing = nfcPresenceUpdate(s_state, "", 2000, 3000);
    TEST_ASSERT_EQUAL_INT((int)NfcPresenceAction::None, (int)missing.action);
    TEST_ASSERT_TRUE(s_state.lost_pending);

    auto still_missing = nfcPresenceUpdate(s_state, "", 3999, 3000);
    TEST_ASSERT_EQUAL_INT((int)NfcPresenceAction::None, (int)still_missing.action);
    TEST_ASSERT_TRUE(s_state.lost_pending);
}

void test_read_recovery_clears_lost_pending()
{
    nfcPresenceUpdate(s_state, "04:AA:BB", 1000, 3000);
    nfcPresenceUpdate(s_state, "", 2000, 3000);

    auto recovered = nfcPresenceUpdate(s_state, "04:AA:BB", 2500, 3000);
    TEST_ASSERT_EQUAL_INT((int)NfcPresenceAction::None, (int)recovered.action);
    TEST_ASSERT_FALSE(s_state.lost_pending);
    TEST_ASSERT_EQUAL_STRING("04:AA:BB", s_state.current_uid);
}

void test_removed_after_full_lost_window()
{
    nfcPresenceUpdate(s_state, "04:AA:BB", 1000, 3000);
    nfcPresenceUpdate(s_state, "", 2000, 3000);

    auto removed = nfcPresenceUpdate(s_state, "", 5000, 3000);
    TEST_ASSERT_EQUAL_INT((int)NfcPresenceAction::Removed, (int)removed.action);
    TEST_ASSERT_EQUAL_STRING("04:AA:BB", removed.uid);
    TEST_ASSERT_FALSE(s_state.lost_pending);
    TEST_ASSERT_EQUAL_STRING("", s_state.current_uid);
}

void test_reinsert_same_card_after_removed_emits_detected()
{
    nfcPresenceUpdate(s_state, "04:AA:BB", 1000, 3000);
    nfcPresenceUpdate(s_state, "", 2000, 3000);
    nfcPresenceUpdate(s_state, "", 5000, 3000);

    auto reinserted = nfcPresenceUpdate(s_state, "04:AA:BB", 6000, 3000);
    TEST_ASSERT_EQUAL_INT((int)NfcPresenceAction::Detected, (int)reinserted.action);
    TEST_ASSERT_EQUAL_STRING("04:AA:BB", reinserted.uid);
}

void test_different_card_emits_detected()
{
    nfcPresenceUpdate(s_state, "04:AA:BB", 1000, 3000);

    auto different = nfcPresenceUpdate(s_state, "04:CC:DD", 2000, 3000);
    TEST_ASSERT_EQUAL_INT((int)NfcPresenceAction::Detected, (int)different.action);
    TEST_ASSERT_EQUAL_STRING("04:CC:DD", different.uid);
    TEST_ASSERT_EQUAL_STRING("04:CC:DD", s_state.current_uid);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_new_card_emits_detected_once);
    RUN_TEST(test_short_missing_read_does_not_emit_removed);
    RUN_TEST(test_read_recovery_clears_lost_pending);
    RUN_TEST(test_removed_after_full_lost_window);
    RUN_TEST(test_reinsert_same_card_after_removed_emits_detected);
    RUN_TEST(test_different_card_emits_detected);
    return UNITY_END();
}
