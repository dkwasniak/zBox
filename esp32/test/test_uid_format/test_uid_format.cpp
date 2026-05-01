#include <unity.h>
#include <Arduino.h>

// Inline copy of uidToString() from main.cpp:271
static String uidToString(uint8_t *uid, uint8_t uidLength)
{
    String r;
    for (uint8_t i = 0; i < uidLength; i++)
    {
        if (i)
            r += ":";
        if (uid[i] < 0x10)
            r += "0";
        r += String(uid[i], HEX);
    }
    r.toUpperCase();
    return r;
}

void test_uid_4_bytes()
{
    uint8_t uid[] = {0x04, 0xA3, 0xB2, 0xC1};
    TEST_ASSERT_EQUAL_STRING("04:A3:B2:C1", uidToString(uid, 4).c_str());
}

void test_uid_7_bytes()
{
    uint8_t uid[] = {0x04, 0xA3, 0xB2, 0xC1, 0x11, 0x22, 0x80};
    TEST_ASSERT_EQUAL_STRING("04:A3:B2:C1:11:22:80", uidToString(uid, 7).c_str());
}

void test_uid_lowercase_uppercased()
{
    uint8_t uid[] = {0xab, 0xcd};
    TEST_ASSERT_EQUAL_STRING("AB:CD", uidToString(uid, 2).c_str());
}

void test_uid_zero_byte_padded()
{
    // Regression: String(0x05, HEX) → "5", padding jest w uidToString
    uint8_t uid[] = {0x00, 0x05};
    TEST_ASSERT_EQUAL_STRING("00:05", uidToString(uid, 2).c_str());
}

void test_uid_ff_byte()
{
    uint8_t uid[] = {0xFF, 0xFF};
    TEST_ASSERT_EQUAL_STRING("FF:FF", uidToString(uid, 2).c_str());
}

void test_uid_single_byte()
{
    uint8_t uid[] = {0x05};
    TEST_ASSERT_EQUAL_STRING("05", uidToString(uid, 1).c_str());
}

void test_uid_all_zeros()
{
    uint8_t uid[] = {0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_STRING("00:00:00:00", uidToString(uid, 4).c_str());
}

void setUp() {}
void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_uid_4_bytes);
    RUN_TEST(test_uid_7_bytes);
    RUN_TEST(test_uid_lowercase_uppercased);
    RUN_TEST(test_uid_zero_byte_padded);
    RUN_TEST(test_uid_ff_byte);
    RUN_TEST(test_uid_single_byte);
    RUN_TEST(test_uid_all_zeros);
    return UNITY_END();
}
