#include <unity.h>

#include "events.h"

static bool s_mode_running = false;
static bool s_connected = false;
static bool s_start_ok = true;
static uint8_t s_stop_calls = 0;
static Event s_posted[16];
static uint8_t s_posted_count = 0;

volatile bool g_beatDetected = false;
volatile uint8_t g_audioEnergy = 0;

bool postEventFromTask(const Event& ev) {
    if (s_posted_count < 16) s_posted[s_posted_count++] = ev;
    return true;
}

bool audioStartBtHeadphonesMode() {
    if (!s_start_ok) return false;
    s_mode_running = true;
    return true;
}

void audioStopBtHeadphonesMode() {
    s_stop_calls++;
    s_mode_running = false;
    s_connected = false;
}

bool audioBtHeadphonesAreConnected() {
    return s_connected;
}

bool audioBtHeadphonesModeIsRunning() {
    return s_mode_running;
}

void logWritef(const char*, bool, const char*, ...) {}
void plogInit(bool) {}
void plogWrite(const char*) {}
void plogFlushToSd() {}
void plogMark(const char*, const char*) {}
void formatUptime(char*, size_t, unsigned long) {}

#include "bt_adapter.cpp"

static void resetState() {
    s_mode_running = false;
    s_connected = false;
    s_start_ok = true;
    s_stop_calls = 0;
    s_posted_count = 0;
    btAdapterInit();
}

static bool hasEvent(EventType type) {
    for (uint8_t i = 0; i < s_posted_count; ++i) {
        if (s_posted[i].type == type) return true;
    }
    return false;
}

void setUp() {
    resetState();
}

void tearDown() {}

void test_start_posts_connected_when_headphones_already_connected() {
    s_connected = true;

    btAdapterStartHeadphonesMode(0);

    TEST_ASSERT_TRUE(hasEvent(EventType::BtConnected));
}

void test_poll_posts_connected_edge_after_mode_start() {
    btAdapterStartHeadphonesMode(0);
    TEST_ASSERT_FALSE(hasEvent(EventType::BtConnected));

    s_connected = true;
    btAdapterPoll();

    TEST_ASSERT_TRUE(hasEvent(EventType::BtConnected));
}

void test_stop_posts_mode_stopped_without_disconnect_edge() {
    s_mode_running = true;
    s_connected = true;
    btAdapterInit();
    s_posted_count = 0;

    btAdapterStopHeadphonesMode(0);

    TEST_ASSERT_EQUAL_UINT8(1, s_stop_calls);
    TEST_ASSERT_TRUE(hasEvent(EventType::BtHeadphonesModeStopped));
    TEST_ASSERT_FALSE(hasEvent(EventType::BtDisconnected));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_start_posts_connected_when_headphones_already_connected);
    RUN_TEST(test_poll_posts_connected_edge_after_mode_start);
    RUN_TEST(test_stop_posts_mode_stopped_without_disconnect_edge);
    return UNITY_END();
}
