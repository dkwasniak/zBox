#include "button_adapter.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "zbox_config.h"
#include "dispatcher.h"
#include "event_queue.h"
#include "events.h"
#include "logging.h"
#include "battery.h"
#include "helpers.h"

// ── Constants ──────────────────────────────────────────────────────────────
static constexpr uint32_t BTN_DEBOUNCE_MS     = DEBOUNCE_MS;             // 50 (from zbox_config.h)
static constexpr uint32_t DOUBLE_CLICK_MS     = DOUBLE_CLICK_WINDOW_MS;  // 350
static constexpr uint32_t LONG_PRESS_SLEEP_MS = NIGHT_LIGHT_SLEEP_HOLD_MS; // 1000
static constexpr uint32_t COMBO_AB_MS         = LONG_PRESS_MS;           // 2000 (match current)
static constexpr uint32_t BT_HEADPHONES_LONG_MS = LONG_PRESS_MS;         // 2000
static constexpr uint32_t BATTERY_CHECK_LONG_MS = LONG_PRESS_MS;         // 2000

static const uint8_t BUTTON_PINS[BTN_COUNT] = { BTN_A, BTN_B, BTN_C, BTN_D };

static uint8_t buttonPinMode(uint8_t pin)
{
    switch (pin) {
    case 34:
    case 35:
    case 36:
    case 39:
        return INPUT;
    default:
        return INPUT_PULLUP;
    }
}

// ── Raw queue ──────────────────────────────────────────────────────────────
static QueueHandle_t s_rawQueue;

void IRAM_ATTR buttonAdapterISR(void *arg)
{
    uint8_t id = (uint8_t)(uintptr_t)arg;
    uint32_t now = (uint32_t)millis();
    bool pressed = (digitalRead(BUTTON_PINS[id]) == LOW);
    RawButtonEvent ev = { id, pressed, now };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_rawQueue, &ev, &woken);
    portYIELD_FROM_ISR(woken);
}

// ── Per-button decoder state ───────────────────────────────────────────────
struct BtnState {
    bool     down;
    uint32_t press_ms;
    uint32_t last_release_ms;
    uint8_t  click_count;
    bool     long_handled;
    bool     sleep_warn_fired;  // true after SleepHoldWarning sent for this press
    uint32_t last_event_ms;
};

static BtnState s_btn[4] = {};

void buttonDecoderReset()
{
    for (auto &b : s_btn) b = {};
}

// ── Combo detection state ─────────────────────────────────────────────────
static struct {
    bool     active;
    uint32_t start_ms;
    bool     fired;
} s_combo_ab = {};

static void resetComboAB() { s_combo_ab = {}; }

// ── Resolve single / double click ─────────────────────────────────────────
static void resolveClick(uint8_t id, uint8_t clicks)
{
    switch (id) {
    case 0: // A
        if (clicks >= 2) {
            LOGC("[BTN_ADAPTER] BTN_A double -> PrevTrackPressed\n");
            postEventFromTask(makeEvent(EventType::PrevTrackPressed));
        } else {
            LOGC("[BTN_ADAPTER] BTN_A single -> PlayPausePressed\n");
            postEventFromTask(makeEvent(EventType::PlayPausePressed));
        }
        break;
    case 1: // B
        if (clicks >= 2) {
            LOGC("[BTN_ADAPTER] BTN_B double -> NextTrackPressed\n");
            postEventFromTask(makeEvent(EventType::NextTrackPressed));
        } else {
            LOGC("[BTN_ADAPTER] BTN_B single -> PlayPausePressed\n");
            postEventFromTask(makeEvent(EventType::PlayPausePressed));
        }
        break;
    default:
        break;
    }
}

// ── buttonDecoderFeed: called for each raw event ──────────────────────────
void buttonDecoderFeed(RawButtonEvent raw)
{
    uint8_t id = raw.button_id;
    if (id >= 4) return;
    BtnState &b = s_btn[id];

    // Debounce
    if ((raw.timestamp_ms - b.last_event_ms) < BTN_DEBOUNCE_MS) return;
    b.last_event_ms = raw.timestamp_ms;

    if (raw.pressed) {
        // ── PRESS ──
        if (b.down) return;  // spurious
        b.down = true;
        b.press_ms = raw.timestamp_ms;
        b.long_handled = false;
        b.sleep_warn_fired = false;
        LOGI("[BTN_ADAPTER] BTN_%c press\n", 'A' + id);

        // Combo tracking
        if (id == 0 || id == 1) {
            uint8_t other = (id == 0) ? 1 : 0;
            if (s_btn[other].down && !s_combo_ab.active && !s_combo_ab.fired) {
                s_combo_ab = { true, raw.timestamp_ms, false };
            }
        }
    } else {
        // ── RELEASE ──
        if (!b.down) return;  // spurious
        uint32_t dur = raw.timestamp_ms - b.press_ms;
        b.down = false;

        LOGI("[BTN_ADAPTER] BTN_%c release dur=%lums\n", 'A' + id, (unsigned long)dur);

        // Cancel combos if either participant released
        if (id == 0 || id == 1) resetComboAB();

        if (b.long_handled) {
            b.long_handled = false;
            return;
        }

        if (dur >= LONG_PRESS_SLEEP_MS) {
            // Long press on release
            if (id == 3) {
                // BTN_D long release → normal sleep
                LOGC("[BTN_ADAPTER] BTN_D long release -> SleepRequested(Normal)\n");
                postEventFromTask(makeSleepRequestedEvent(RequestedSleepKind::Normal));
            }
        } else if (dur < LONG_PRESS_SLEEP_MS) {
            // Short press
            if (id == 0 || id == 1) {
                // A/B: accumulate clicks
                b.click_count = (b.click_count >= 2) ? 2 : (uint8_t)(b.click_count + 1);
                b.last_release_ms = raw.timestamp_ms;
            } else if (id == 2) {
                LOGC("[BTN_ADAPTER] BTN_C short -> VolumeDownPressed\n");
                postEventFromTask(makeEvent(EventType::VolumeDownPressed));
            } else if (id == 3) {
                LOGC("[BTN_ADAPTER] BTN_D short -> VolumeUpPressed\n");
                postEventFromTask(makeEvent(EventType::VolumeUpPressed));
            }
        }
    }
}

// ── buttonDecoderTick: called every ~50ms when no raw event arrives ────────
void buttonDecoderTick(uint32_t now_ms)
{
    // Resolve pending double-click windows for A and B
    for (uint8_t id = 0; id <= 1; id++) {
        BtnState &b = s_btn[id];
        if (b.click_count > 0 && !b.down &&
            (now_ms - b.last_release_ms) >= DOUBLE_CLICK_MS) {
            uint8_t clicks = b.click_count;
            b.click_count = 0;
            resolveClick(id, clicks);
        }
    }

    // Long-press while held
    for (uint8_t id = 0; id < 4; id++) {
        BtnState &b = s_btn[id];
        if (!b.down || b.long_handled) continue;
        // Sanity-check: ISR noise can register a press whose release gets
        // swallowed by the debounce, leaving b.down stuck forever. Verify
        // the GPIO is actually still LOW before acting on held time.
        if (digitalRead(BUTTON_PINS[id]) != LOW) {
            // Release was swallowed by debounce — synthesize action based on hold duration.
            uint32_t dur = now_ms - b.press_ms;
            b.down = false;
            if (!b.long_handled && dur < LONG_PRESS_SLEEP_MS) {
                if (id == 2) {
                    LOGC("[BTN_ADAPTER] BTN_C short (synth) -> VolumeDownPressed\n");
                    postEventFromTask(makeEvent(EventType::VolumeDownPressed));
                } else if (id == 3) {
                    LOGC("[BTN_ADAPTER] BTN_D short (synth) -> VolumeUpPressed\n");
                    postEventFromTask(makeEvent(EventType::VolumeUpPressed));
                } else if (id == 0 || id == 1) {
                    b.click_count = (b.click_count >= 2) ? 2 : (uint8_t)(b.click_count + 1);
                    b.last_release_ms = now_ms;
                }
            } else if (!b.long_handled && dur >= LONG_PRESS_SLEEP_MS && b.sleep_warn_fired) {
                // sleep_warn_fired guarantees the user actually held ≥ LONG_PRESS_MS
                // (the warning only fires while GPIO is confirmed LOW)
                if (id == 3) {
                    LOGC("[BTN_ADAPTER] BTN_D long release (synth) -> SleepRequested(Normal)\n");
                    postEventFromTask(makeSleepRequestedEvent(RequestedSleepKind::Normal));
                }
            }
            b.long_handled = false;
            b.sleep_warn_fired = false;
            continue;
        }
        uint32_t held = now_ms - b.press_ms;

        if (id == 0 && held >= BT_HEADPHONES_LONG_MS && !s_btn[1].down) {
            // BTN_A long hold (alone) -> temporary BT headphones mode
            b.long_handled = true;
            b.click_count = 0;
            LOGC("[BTN_ADAPTER] BTN_A long -> BtHeadphonesModeRequested\n");
            postEventFromTask(makeEvent(EventType::BtHeadphonesModeRequested));
        }
        if (id == 1 && held >= LONG_PRESS_SLEEP_MS && !s_btn[0].down) {
            // BTN_B long hold (alone) → mode toggle
            b.long_handled = true;
            b.click_count = 0;
            LOGC("[BTN_ADAPTER] BTN_B long -> ModeToggleRequested\n");
            postEventFromTask(makeEvent(EventType::ModeToggleRequested));
        }
        if (id == 3 && held >= LONG_PRESS_SLEEP_MS && !b.sleep_warn_fired && !b.long_handled) {
            b.sleep_warn_fired = true;
            LOGI("[BTN_ADAPTER] BTN_D sleep threshold -> SleepHoldWarning\n");
            postEventFromTask(makeEvent(EventType::SleepHoldWarning));
        }
        if (id == 2 && held >= BATTERY_CHECK_LONG_MS && !b.long_handled) {
            b.long_handled = true;
            const float v = readBatteryVoltage();
            const uint8_t bars = (uint8_t)batteryBars(v);
            LOGC("[BTN_ADAPTER] BTN_C long -> BatteryCheck %.2fV %d bars\n", v, (int)bars);
            postEventFromTask(makeBatteryCheckEvent(bars));
        }
    }

    // A+B combo
    if (s_combo_ab.active && !s_combo_ab.fired) {
        if (s_btn[0].down && s_btn[1].down) {
            uint32_t held = now_ms - s_combo_ab.start_ms;
            if (held >= COMBO_AB_MS) {
                s_combo_ab.fired = true;
                s_btn[0].long_handled = true;
                s_btn[1].long_handled = true;
                s_btn[0].click_count = 0;
                s_btn[1].click_count = 0;
                LOGC("[BTN_ADAPTER] A+B combo -> SyncModeRequested\n");
                postEventFromTask(makeEvent(EventType::SyncModeRequested));
            }
        }
    }
}

// ── FreeRTOS task ──────────────────────────────────────────────────────────
static void buttonAdapterTask(void *)
{
    RawButtonEvent raw;
    while (true) {
        if (xQueueReceive(s_rawQueue, &raw, pdMS_TO_TICKS(50)) == pdTRUE) {
            buttonDecoderFeed(raw);
        } else {
            buttonDecoderTick((uint32_t)millis());
        }
    }
}

// ── Public API ─────────────────────────────────────────────────────────────
void buttonAdapterInit()
{
    s_rawQueue = xQueueCreate(8, sizeof(RawButtonEvent));
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        pinMode(BUTTON_PINS[i], buttonPinMode(BUTTON_PINS[i]));
        attachInterruptArg(digitalPinToInterrupt(BUTTON_PINS[i]),
                           buttonAdapterISR, (void *)(uintptr_t)i, CHANGE);
    }
    // BTN_D may still be LOW after wake auto-boot. Pre-mark it as long_handled
    // so buttonDecoderTick skips SleepHoldWarning for this initial press.
    if (digitalRead(BTN_D) == LOW) {
        s_btn[3].down         = true;
        s_btn[3].press_ms     = (uint32_t)millis();
        s_btn[3].long_handled = true;
    }
}

void buttonAdapterStartTask()
{
    xTaskCreatePinnedToCore(
        buttonAdapterTask, "btnadapt",
        4096, nullptr,
        2, nullptr,
        1   // core 1
    );
}
