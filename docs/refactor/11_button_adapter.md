# Part 12/12: Button Adapter Contract

← Prev: [10b_test_hardware.md](10b_test_hardware.md)

---

## Current Architecture (Reference)

[Verified in current code: `esp32/src/buttons_isr.cpp`, `esp32/src/buttons.cpp`]

- Four buttons (A, B, C, D) with FALLING edge GPIO interrupts.
- ISR sets `Button::pressed` flag + `Button::pressedAt` debounce timestamp.
- `handleButtons()` runs in `loop()` task context every ~5ms (implicit via `vTaskDelay`).
- Click decoding: single, double-click (< 350ms), long-press — all in `handleButtons()`.
- Combos (A+B, C+D): detected by checking both button pressed flags simultaneously.
- Calls playback/volume/sleep handlers directly — tightly coupled to old architecture.

---

## New Model: Button Adapter Task

Button adapter is a FreeRTOS task that converts raw GPIO events into semantic events
posted to the central dispatcher queue.

### ISR Context

```cpp
struct RawButtonEvent {
    uint8_t  button_id;     // 0=A, 1=B, 2=C, 3=D
    bool     pressed;       // true=FALLING, false=RISING
    uint32_t timestamp_ms;  // millis() at ISR time
};

static QueueHandle_t rawButtonQueue;  // depth 8, never drops from ISR
```

```cpp
void IRAM_ATTR buttonISR(void* arg) {
    uint8_t id = (uint8_t)(uintptr_t)arg;
    RawButtonEvent ev = { id, digitalRead(BUTTON_PINS[id]) == LOW, (uint32_t)millis() };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(rawButtonQueue, &ev, &woken);
    portYIELD_FROM_ISR(woken);
}
```

Queue depth 8 ensures no drops during rapid multi-button combos.
`millis()` is safe to call from ISR on ESP32 Arduino framework.

---

## Button Adapter Task

```cpp
void buttonAdapterTask(void*) {
    RawButtonEvent raw;
    while (true) {
        if (xQueueReceive(rawButtonQueue, &raw, pdMS_TO_TICKS(50)) == pdTRUE) {
            buttonDecoderFeed(raw);
        } else {
            buttonDecoderTick(millis());  // check long-press and combo timeouts
        }
    }
}
```

Task parameters: core 1, priority 2 (same as audio task), stack 2048 bytes.
Runs independently of the dispatcher — posts to dispatcher queue via `postEventFromTask()`.

---

## Click Decoder State Machine

Each button has independent state:

```
IDLE → [press] → PRESSED → [release < LONG_PRESS_MS] → RELEASED
                                                       → wait DOUBLE_CLICK_MS
                                                       → [second press] → DOUBLE_CLICK
                                                       → [timeout] → SINGLE_CLICK fired
                          → [hold ≥ LONG_PRESS_MS] → LONG_PRESS fired (once)
```

Timing constants (match current `buttons.cpp`):

| Constant | Value | Notes |
|----------|-------|-------|
| `DEBOUNCE_MS` | 30 | Discard same-button events within 30ms |
| `DOUBLE_CLICK_MS` | 350 | Window for second press |
| `LONG_PRESS_SLEEP_MS` | 1500 | BTN_C long → emergency sleep |
| `LONG_PRESS_BATTERY_MS` | 800 | BTN_A long → battery check |

[Must validate: confirm exact values against current `buttons.cpp` before Stage 5]

---

## Semantic Events Emitted

| Gesture | Button(s) | Event posted |
|---------|-----------|-------------|
| Single click | A | `PlayPausePressed` |
| Double click | A | `PrevTrackPressed` |
| Single click | B | `PlayPausePressed` (music mode only, handled by reducer) |
| Double click | B | `NextTrackPressed` |
| Single click | C | `VolumeDownPressed` |
| Long press | C (1500ms) | `SleepRequested(Emergency)` |
| Single click | D | `VolumeUpPressed` |
| Combo | A+B (within 200ms, held 3s) | `DiagnosticEntryRequested` |
| Combo | C+D (within 200ms, held 5s) | `SleepRequested(Emergency)` |
| Single click | A (night-light) | `ModeToggleRequested` (reducer ignores in night-light) |
| Long press | A (800ms) | `BatteryCheckRequested` |

Reducer owns all policy — the button adapter only translates gestures to events.
It never inspects session mode or playback mode.

---

## Combo Detection

Combos are detected by the button adapter, not the reducer.

```cpp
// Combo state per button pair:
struct ComboState {
    uint32_t first_press_ms;  // 0 = no pending combo
    uint8_t  first_button;
    uint8_t  held_duration_ms;
};
```

A+B combo rule:
1. Button A pressed → record `first_press_ms`, `first_button=A`
2. Button B pressed within 200ms of A → combo candidate active
3. Both held for ≥ 3000ms → post `DiagnosticEntryRequested`
4. Either released before 3000ms → cancel combo, fall through to single-click logic

C+D combo rule: same pattern, 5000ms hold → `SleepRequested(Emergency)`.

---

## Interface

```cpp
void buttonAdapterInit();
    // Creates rawButtonQueue. Attaches GPIO ISRs for all four buttons.
    // Must be called before buttonAdapterStartTask().

void buttonAdapterStartTask();
    // Starts the button adapter FreeRTOS task.
    // Called during Stage 1 dispatcher initialization.

// Internal (exposed for native testing only):
void buttonDecoderFeed(RawButtonEvent raw);
void buttonDecoderTick(uint32_t now_ms);
void buttonDecoderReset();  // clears all state — call between test cases
```

---

## Native Testing

The decoder functions are pure logic (no FreeRTOS, no hardware) — testable natively.

```cpp
// Pattern for native tests:
buttonDecoderReset();
buttonDecoderFeed({.button_id=0, .pressed=true,  .timestamp_ms=0});    // A press
buttonDecoderFeed({.button_id=0, .pressed=false, .timestamp_ms=100});  // A release
buttonDecoderTick(450);  // past DOUBLE_CLICK_MS window → single click fires
// assert: postEventFromTask was called with PlayPausePressed
```

Mock `postEventFromTask` in native build to capture posted events without FreeRTOS queue.

Add to `10a_test_native.md` §Button Decoder Tests:
- Single click A → PlayPausePressed
- Double click A → PrevTrackPressed (second press within 350ms)
- Long press C (1500ms) → SleepRequested(Emergency)
- Long press C (500ms then release) → VolumeDownPressed (not long-press)
- A+B combo 3s → DiagnosticEntryRequested
- Debounce: second event within 30ms same button → ignored
- C+D combo 5s → SleepRequested(Emergency)

---

## Integration with Migration Stages

Stage 1: `buttonAdapterInit()` called during setup, `buttonAdapterStartTask()` starts
the task. Old `handleButtons()` still runs (dual-call). Button adapter posts events
to dispatcher but `DISPATCHER_OWNS_BUTTONS = 0` so results are discarded.

Stage 5: `DISPATCHER_OWNS_BUTTONS = 1`. Old `handleButtons()` removed.
Button adapter task is sole owner of button policy.
