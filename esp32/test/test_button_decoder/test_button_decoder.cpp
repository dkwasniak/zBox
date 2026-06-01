#include <unity.h>
#include <cstdint>
#include <cstring>

// ── GPIO mock ─────────────────────────────────────────────────────────────────

#define LOW  0
#define HIGH 1
static constexpr uint32_t EMERG_SLEEP_MS = 10000;

static int s_gpio[40];
inline int digitalRead(int pin) { return s_gpio[pin]; }

static void gpioSet(int pin, bool pressed) { s_gpio[pin] = pressed ? LOW : HIGH; }

// ── postEventFromTask capture ─────────────────────────────────────────────────

#include "events.h"

static Event   s_posted[32];
static uint8_t s_posted_count;

bool postEventFromTask(const Event& ev) {
    if (s_posted_count < 32) s_posted[s_posted_count++] = ev;
    return true;
}
bool postEventFromIsr(const Event&, int*) { return true; }

// ── Implementations for symbols declared in src/ but hardware-only natively ───
// (button_adapter.cpp uses local-dir search and finds src/ headers, so these
//  are plain function definitions that satisfy the linker.)

#include <cstdarg>

void  logWritef(const char*, bool, const char*, ...) {}
void  plogInit(bool) {}
void  plogWrite(const char*) {}
void  plogFlushToSd() {}
void  plogMark(const char*, const char*) {}
void  formatUptime(char*, size_t, unsigned long) {}
float readBatteryVoltage() { return 4.0f; }

// ── button_adapter decoder under test ────────────────────────────────────────

#include "button_adapter.cpp"

// ── Test helpers ──────────────────────────────────────────────────────────────

static void reset() {
    memset(s_gpio, HIGH, sizeof(s_gpio));
    s_posted_count = 0;
    memset(s_posted, 0, sizeof(s_posted));
    buttonDecoderReset();
}

static bool hasEvent(EventType t) {
    for (uint8_t i = 0; i < s_posted_count; i++)
        if (s_posted[i].type == t) return true;
    return false;
}

static uint8_t countEvents(EventType t) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < s_posted_count; i++)
        if (s_posted[i].type == t) n++;
    return n;
}

static const uint8_t PIN[4] = { BTN_A, BTN_B, BTN_C, BTN_D };

// Press and release button id with given hold duration. Returns release time.
static uint32_t shortPress(uint8_t id, uint32_t t, uint32_t hold = 120) {
    gpioSet(PIN[id], true);
    buttonDecoderFeed({id, true,  t});
    uint32_t rel = t + hold;
    gpioSet(PIN[id], false);
    buttonDecoderFeed({id, false, rel});
    return rel;
}

void setUp()    { reset(); }
void tearDown() {}

// ── Normal short press: C → VolumeDown, D → VolumeUp ─────────────────────────

void test_c_short_press_posts_volume_down() {
    shortPress(2, 1000);
    TEST_ASSERT_TRUE(hasEvent(EventType::VolumeDownPressed));
    TEST_ASSERT_FALSE(hasEvent(EventType::VolumeUpPressed));
}

void test_d_short_press_posts_volume_up() {
    shortPress(3, 1000);
    TEST_ASSERT_TRUE(hasEvent(EventType::VolumeUpPressed));
    TEST_ASSERT_FALSE(hasEvent(EventType::VolumeDownPressed));
}

// ── Bug-fix: debounced release → tick synthesizes action ─────────────────────

void test_d_debounced_release_tick_synthesizes_volume_up() {
    // press D, release comes 10ms later (<20ms debounce) → release is eaten
    gpioSet(PIN[3], true);
    buttonDecoderFeed({3, true,  1000});
    gpioSet(PIN[3], false);
    buttonDecoderFeed({3, false, 1010}); // 10ms < DEBOUNCE_MS → debounced, b.down stays true

    TEST_ASSERT_FALSE(hasEvent(EventType::VolumeUpPressed)); // not yet

    buttonDecoderTick(1200); // GPIO is HIGH → synthesize VolumeUp
    TEST_ASSERT_TRUE(hasEvent(EventType::VolumeUpPressed));
}

void test_c_debounced_release_tick_synthesizes_volume_down() {
    gpioSet(PIN[2], true);
    buttonDecoderFeed({2, true,  1000});
    gpioSet(PIN[2], false);
    buttonDecoderFeed({2, false, 1010}); // 10ms < DEBOUNCE_MS → debounced

    TEST_ASSERT_FALSE(hasEvent(EventType::VolumeDownPressed));
    buttonDecoderTick(1200);
    TEST_ASSERT_TRUE(hasEvent(EventType::VolumeDownPressed));
}

void test_d_synthesized_event_not_fired_twice() {
    gpioSet(PIN[3], true);
    buttonDecoderFeed({3, true,  1000});
    gpioSet(PIN[3], false);
    buttonDecoderFeed({3, false, 1030}); // debounced

    buttonDecoderTick(1200); // synthesizes VolumeUp (b.down → false)
    uint8_t cnt = s_posted_count;

    buttonDecoderTick(1300); // b.down already false → nothing
    TEST_ASSERT_EQUAL_UINT8(cnt, s_posted_count);
}

void test_tick_no_synthesize_when_gpio_still_low() {
    // button held → GPIO still LOW → tick must NOT fire VolumeUp
    gpioSet(PIN[3], true);
    buttonDecoderFeed({3, true, 1000}); // b.down = true, GPIO LOW

    buttonDecoderTick(1200); // held < LONG_PRESS_MS, GPIO LOW → no action
    TEST_ASSERT_FALSE(hasEvent(EventType::VolumeUpPressed));
}

// ── A/B: single click → PlayPausePressed after double-click window ────────────

void test_a_single_click_posts_playpause() {
    uint32_t rel = shortPress(0, 1000);
    buttonDecoderTick(rel + DOUBLE_CLICK_MS - 1); // window still open
    TEST_ASSERT_FALSE(hasEvent(EventType::PlayPausePressed));
    buttonDecoderTick(rel + DOUBLE_CLICK_MS + 10); // window expired
    TEST_ASSERT_TRUE(hasEvent(EventType::PlayPausePressed));
    TEST_ASSERT_FALSE(hasEvent(EventType::PrevTrackPressed));
}

void test_b_single_click_posts_playpause() {
    uint32_t rel = shortPress(1, 1000);
    buttonDecoderTick(rel + DOUBLE_CLICK_MS + 10);
    TEST_ASSERT_TRUE(hasEvent(EventType::PlayPausePressed));
    TEST_ASSERT_FALSE(hasEvent(EventType::NextTrackPressed));
}

// ── A/B: double click ─────────────────────────────────────────────────────────

void test_a_double_click_posts_prev_track() {
    uint32_t rel  = shortPress(0, 1000);
    uint32_t rel2 = shortPress(0, rel + 50); // second click within window
    buttonDecoderTick(rel2 + DOUBLE_CLICK_MS + 10); // window expires after 2nd release
    TEST_ASSERT_TRUE(hasEvent(EventType::PrevTrackPressed));
    TEST_ASSERT_FALSE(hasEvent(EventType::PlayPausePressed));
}

void test_b_double_click_posts_next_track() {
    uint32_t rel  = shortPress(1, 1000);
    uint32_t rel2 = shortPress(1, rel + 50);
    buttonDecoderTick(rel2 + DOUBLE_CLICK_MS + 10);
    TEST_ASSERT_TRUE(hasEvent(EventType::NextTrackPressed));
}

// ── Debounced A release → tick accumulates click, resolves as PlayPause ───────

void test_a_debounced_release_tick_accumulates_click() {
    gpioSet(PIN[0], true);
    buttonDecoderFeed({0, true,  1000});
    gpioSet(PIN[0], false);
    buttonDecoderFeed({0, false, 1030}); // debounced → no click accumulation yet

    buttonDecoderTick(1200); // synthesizes click (sets last_release_ms = 1200)
    buttonDecoderTick(1200 + DOUBLE_CLICK_MS + 10); // resolves → PlayPause
    TEST_ASSERT_TRUE(hasEvent(EventType::PlayPausePressed));
}

// ── B long hold → ModeToggleRequested, suppresses PlayPause on release ────────

void test_a_long_hold_posts_bt_headphones_request() {
    gpioSet(PIN[0], true);
    buttonDecoderFeed({0, true, 1000});
    buttonDecoderTick(1000 + LONG_PRESS_MS + 10);
    TEST_ASSERT_TRUE(hasEvent(EventType::BtHeadphonesModeRequested));
    TEST_ASSERT_FALSE(hasEvent(EventType::PlayPausePressed));
}

void test_c_long_hold_posts_battery_check() {
    gpioSet(PIN[2], true);
    buttonDecoderFeed({2, true, 1000});
    buttonDecoderTick(1000 + LONG_PRESS_MS + 10);
    TEST_ASSERT_TRUE(hasEvent(EventType::BatteryCheckRequested));
    TEST_ASSERT_FALSE(hasEvent(EventType::VolumeDownPressed));
    TEST_ASSERT_EQUAL_UINT8(5, s_posted[0].payload.battery_check.bars);
}

void test_b_long_hold_posts_mode_toggle() {
    gpioSet(PIN[1], true);
    buttonDecoderFeed({1, true, 1000});
    buttonDecoderTick(1000 + LONG_PRESS_MS + 10);
    TEST_ASSERT_TRUE(hasEvent(EventType::ModeToggleRequested));
}

void test_b_long_hold_suppresses_playpause() {
    gpioSet(PIN[1], true);
    buttonDecoderFeed({1, true, 1000});
    buttonDecoderTick(1000 + LONG_PRESS_MS + 10); // fires ModeToggle, long_handled=true

    gpioSet(PIN[1], false);
    buttonDecoderFeed({1, false, 1000 + LONG_PRESS_MS + 50}); // long_handled → no click

    buttonDecoderTick(1000 + LONG_PRESS_MS + 50 + DOUBLE_CLICK_MS + 10);
    TEST_ASSERT_FALSE(hasEvent(EventType::PlayPausePressed));
}

// ── D long release → SleepRequested ──────────────────────────────────────────

void test_d_long_release_posts_sleep() {
    gpioSet(PIN[3], true);
    buttonDecoderFeed({3, true,  1000});
    gpioSet(PIN[3], false);
    buttonDecoderFeed({3, false, 1000 + LONG_PRESS_MS + 10});
    TEST_ASSERT_TRUE(hasEvent(EventType::SleepRequested));
    TEST_ASSERT_FALSE(hasEvent(EventType::VolumeUpPressed));
}

void test_c_short_does_not_post_sleep() {
    shortPress(2, 1000);
    TEST_ASSERT_FALSE(hasEvent(EventType::SleepRequested));
}

// ── Debounce: rapid second press rejected until debounce window expires ────────

void test_debounce_rejects_rapid_second_press_on_d() {
    gpioSet(PIN[3], true);
    buttonDecoderFeed({3, true,  1000});
    gpioSet(PIN[3], false);
    buttonDecoderFeed({3, false, 1120}); // ok (120ms)
    gpioSet(PIN[3], true);
    buttonDecoderFeed({3, true,  1140}); // 20ms after release → debounced

    TEST_ASSERT_EQUAL_UINT8(1, countEvents(EventType::VolumeUpPressed));
}

// ── Multiple rapid D presses all produce VolumeUp (via normal or synthesized) ─

void test_d_multiple_quick_presses_each_produce_volume_up() {
    uint32_t t = 1000;
    for (int i = 0; i < 3; i++) {
        t = shortPress(3, t) + 100; // 100ms gap between presses
    }
    TEST_ASSERT_EQUAL_UINT8(3, countEvents(EventType::VolumeUpPressed));
}

// ── Stuck b.down: GPIO HIGH but b.down=true, held past LONG_PRESS_MS ─────────
// These guard against the "phantom hold" regression: if a release ISR is eaten
// by debounce, b.down gets stuck. The tick must detect GPIO HIGH and synthesize
// a SHORT-press action — it must NOT fire the long-press action as if the user
// were physically holding the button.

void test_d_stuck_down_past_long_press_no_phantom_event() {
    // Stuck b.down on D for > LONG_PRESS_MS, GPIO HIGH (not physically held).
    // Short-press synthesis window has expired, so no VolumeUp.
    // D now owns battery preview on long hold, but that must not trigger from
    // a phantom long-press caused by a swallowed release.
    // Key check: state is cleaned up so the next real press works normally.
    gpioSet(PIN[3], true);
    buttonDecoderFeed({3, true, 1000});
    gpioSet(PIN[3], false); // button released physically; ISR debounced

    buttonDecoderTick(1000 + LONG_PRESS_MS + 100); // > 2000ms → synthesis window missed
    TEST_ASSERT_EQUAL_UINT8(0, s_posted_count);     // no phantom events

    uint32_t rel = shortPress(3, 3000); // next real press must work
    TEST_ASSERT_EQUAL_UINT8(1, countEvents(EventType::VolumeUpPressed));
}

void test_b_stuck_down_past_long_press_no_mode_toggle() {
    // Stuck b.down on B for > LONG_PRESS_MS with GPIO HIGH (not physically held).
    // Without the GPIO sanity-check, the tick would fire ModeToggleRequested.
    // With the fix: GPIO HIGH is detected first → long-press handler is skipped.
    gpioSet(PIN[1], true);
    buttonDecoderFeed({1, true, 1000});
    gpioSet(PIN[1], false); // physically released, ISR swallowed

    buttonDecoderTick(1000 + LONG_PRESS_MS + 100);
    TEST_ASSERT_FALSE(hasEvent(EventType::ModeToggleRequested)); // key regression guard

    // Synthesis window also missed (dur > LONG_PRESS_MS) → no PlayPause either.
    // But state should be clean: next real press works.
    uint32_t rel = shortPress(1, 5000);
    buttonDecoderTick(rel + DOUBLE_CLICK_MS + 10);
    TEST_ASSERT_TRUE(hasEvent(EventType::PlayPausePressed));
}

void test_a_long_handled_swallowed_release_clears_state_for_next_press() {
    gpioSet(PIN[0], true);
    buttonDecoderFeed({0, true, 1000});
    buttonDecoderTick(1000 + LONG_PRESS_MS + 10);
    TEST_ASSERT_TRUE(hasEvent(EventType::BtHeadphonesModeRequested));

    gpioSet(PIN[0], false); // physical release, ISR event swallowed
    buttonDecoderTick(1000 + LONG_PRESS_MS + 100);

    s_posted_count = 0;
    uint32_t rel = shortPress(0, 5000);
    buttonDecoderTick(rel + DOUBLE_CLICK_MS + 10);
    TEST_ASSERT_TRUE(hasEvent(EventType::PlayPausePressed));
}

// ── Bug fix: hold D past warning threshold, release ISR dropped → tick synthesizes sleep ──
// This is the exact bug: user holds D, SleepHoldWarning fires, user releases, but the
// release ISR event is dropped. Tick must still deliver SleepRequested(Normal).

void test_d_long_hold_with_warning_synth_posts_sleep() {
    gpioSet(PIN[3], true);
    buttonDecoderFeed({3, true, 1000});

    // Tick fires after 2s: GPIO still LOW, sleep_warn_fired = true
    buttonDecoderTick(1000 + LONG_PRESS_MS + 10);
    TEST_ASSERT_TRUE(hasEvent(EventType::SleepHoldWarning));
    s_posted_count = 0; // clear for next assertion

    // User releases but ISR event dropped — simulate by setting GPIO HIGH
    // without calling buttonDecoderFeed for the release
    gpioSet(PIN[3], false);

    // Tick fires: GPIO HIGH, sleep_warn_fired=true → should synthesize SleepRequested
    buttonDecoderTick(1000 + LONG_PRESS_MS + 500);
    TEST_ASSERT_TRUE(hasEvent(EventType::SleepRequested));
    TEST_ASSERT_FALSE(hasEvent(EventType::VolumeUpPressed));
}

void test_c_stuck_down_past_emergency_sleep_no_sleep() {
    // Stuck b.down on C for > EMERGENCY_SLEEP_MS (10000ms), GPIO HIGH.
    // Without the GPIO sanity-check, the tick would fire SleepRequested(Emergency).
    // With the fix: GPIO HIGH → emergency-sleep handler is skipped.
    gpioSet(PIN[2], true);
    buttonDecoderFeed({2, true, 1000});
    gpioSet(PIN[2], false); // physically released, ISR swallowed

    buttonDecoderTick(1000 + EMERG_SLEEP_MS + 100); // > 10000ms
    TEST_ASSERT_FALSE(hasEvent(EventType::SleepRequested)); // key regression guard
    TEST_ASSERT_EQUAL_UINT8(0, s_posted_count);             // no phantom events at all
}

void test_stuck_reset_clears_state_for_next_real_press() {
    // Stuck reset on D, then verify a real subsequent press+release works normally
    gpioSet(PIN[3], true);
    buttonDecoderFeed({3, true, 1000});
    gpioSet(PIN[3], false); // release swallowed by debounce

    buttonDecoderTick(1200); // tick synthesizes VolumeUp, resets b.down

    // Now do a real press+release
    uint32_t rel = shortPress(3, 2000);
    TEST_ASSERT_EQUAL_UINT8(2, countEvents(EventType::VolumeUpPressed));
}

// ── Sleep threshold: 1000 ms (NIGHT_LIGHT_SLEEP_HOLD_MS), not 2000 ms ────────

// SleepHoldWarning fires when D held for LONG_PRESS_SLEEP_MS (1000ms).
void test_d_sleep_warning_fires_at_1000ms() {
    gpioSet(PIN[3], true);
    buttonDecoderFeed({3, true, 1000});
    buttonDecoderTick(1000 + LONG_PRESS_SLEEP_MS + 10);
    TEST_ASSERT_TRUE(hasEvent(EventType::SleepHoldWarning));
}

// Held 1500 ms (past 1000ms threshold, still below old 2000ms threshold) and released
// → SleepRequested, not VolumeUpPressed.  Regression: proves threshold is 1000ms.
void test_d_held_1500ms_release_posts_sleep() {
    gpioSet(PIN[3], true);
    buttonDecoderFeed({3, true, 1000});
    buttonDecoderTick(1000 + LONG_PRESS_SLEEP_MS + 10); // fires SleepHoldWarning

    gpioSet(PIN[3], false);
    buttonDecoderFeed({3, false, 2500}); // held 1500ms >= 1000ms

    TEST_ASSERT_TRUE(hasEvent(EventType::SleepRequested));
    TEST_ASSERT_FALSE(hasEvent(EventType::VolumeUpPressed));
}

// Held 999 ms (just under threshold) and released → VolumeDownPressed, no sleep.
void test_c_held_999ms_release_posts_volume_down() {
    gpioSet(PIN[2], true);
    buttonDecoderFeed({2, true, 1000});
    gpioSet(PIN[2], false);
    buttonDecoderFeed({2, false, 1999}); // held 999ms < 1000ms

    TEST_ASSERT_TRUE(hasEvent(EventType::VolumeDownPressed));
    TEST_ASSERT_FALSE(hasEvent(EventType::SleepRequested));
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_c_short_press_posts_volume_down);
    RUN_TEST(test_d_short_press_posts_volume_up);

    RUN_TEST(test_d_debounced_release_tick_synthesizes_volume_up);
    RUN_TEST(test_c_debounced_release_tick_synthesizes_volume_down);
    RUN_TEST(test_d_synthesized_event_not_fired_twice);
    RUN_TEST(test_tick_no_synthesize_when_gpio_still_low);

    RUN_TEST(test_a_single_click_posts_playpause);
    RUN_TEST(test_b_single_click_posts_playpause);
    RUN_TEST(test_a_double_click_posts_prev_track);
    RUN_TEST(test_b_double_click_posts_next_track);
    RUN_TEST(test_a_debounced_release_tick_accumulates_click);
    RUN_TEST(test_a_long_hold_posts_bt_headphones_request);
    RUN_TEST(test_c_long_hold_posts_battery_check);

    RUN_TEST(test_b_long_hold_posts_mode_toggle);
    RUN_TEST(test_b_long_hold_suppresses_playpause);
    RUN_TEST(test_d_long_release_posts_sleep);
    RUN_TEST(test_c_short_does_not_post_sleep);

    RUN_TEST(test_debounce_rejects_rapid_second_press_on_d);
    RUN_TEST(test_d_multiple_quick_presses_each_produce_volume_up);

    RUN_TEST(test_d_long_hold_with_warning_synth_posts_sleep);

    RUN_TEST(test_d_stuck_down_past_long_press_no_phantom_event);
    RUN_TEST(test_b_stuck_down_past_long_press_no_mode_toggle);
    RUN_TEST(test_a_long_handled_swallowed_release_clears_state_for_next_press);
    RUN_TEST(test_c_stuck_down_past_emergency_sleep_no_sleep);
    RUN_TEST(test_stuck_reset_clears_state_for_next_real_press);

    RUN_TEST(test_d_sleep_warning_fires_at_1000ms);
    RUN_TEST(test_d_held_1500ms_release_posts_sleep);
    RUN_TEST(test_c_held_999ms_release_posts_volume_down);

    return UNITY_END();
}
