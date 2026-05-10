# Part 9/11: Dispatcher and Runtime Topology

← Prev: [07_led_model.md](07_led_model.md) | → Next: [09_migration_stages.md](09_migration_stages.md)

> **Button ISR bridge and click decoder** are documented in [11_button_adapter.md](11_button_adapter.md).
> This file covers the dispatcher task, watchdog, event queue, timers, and observability.

---

## Dispatcher Task

```
Name:     "app"
Core:     1
Priority: 1
Stack:    8192 bytes minimum (validate via HWM logs)
Queue:    sized from measured worst-case bursts (see Event Queue Policy)
```

### Dispatcher Responsibilities

1. Receive events from the central event queue.
2. Generate `CmdId` for effects that need correlation.
3. Call `reduce(current_state, event, millis())` → `ReduceResult`.
4. Update `current_state` to `result.next_state`.
5. Call `deriveLedScene(current_state)` → update LED executor if scene changed.
6. For each effect in `result.effects[0..effect_count]`:
   a. Dispatch to the appropriate adapter/executor.
   b. Record in `pending_effects[]` if the effect expects async feedback.
7. Check `pending_effects[]` for expired timeouts → post synthetic failure events.
8. Update timers from `AppState` (start/stop based on deadline fields).
9. Log state transition: `from_state + event → to_state + effects`.
10. Reset watchdog.

---

## Watchdog Contract

[Current code uses 15s WDT: verified in `esp32/src/main.cpp`]

- Dispatcher task calls `esp_task_wdt_reset()` at the start of each event loop iteration.
- WDT timeout: 15 seconds (preserved from current implementation).
- If the dispatcher blocks (e.g., waiting for a synchronous executor), the WDT fires.
- Therefore: no executor called by dispatcher may block longer than 14 seconds.
  Each synchronous executor must have its own internal timeout.
- `EnterDeepSleep` executor calls `esp_task_wdt_delete(NULL)` before `esp_deep_sleep_start()`
  to deregister the dispatcher task from WDT (sleep sequence may take > 15s total).

```cpp
void dispatcherLoop() {
    while (true) {
        esp_task_wdt_reset();  // reset WDT at top of every iteration

        Event ev;
        if (xQueueReceive(dispatcherQueue, &ev, pdMS_TO_TICKS(10)) == pdTRUE) {
            ReduceResult result = reduce(currentState, ev, millis());
            // ... dispatch effects, update LED, log transition ...
            currentState = result.next_state;
        }

        checkPendingEffectTimeouts();
        checkDeadlines();
    }
}
```

---

## Event Queue Policy

Single central queue. Events classified by drop behavior:

| Class | Behavior on queue full | Examples |
|-------|----------------------|---------|
| `Critical` | MUST NOT drop — log CRIT + restart | SleepRequested, BtShutdownCompleted |
| `Coalescible` | Replace equivalent pending event | IdleTimeoutFired, VolumeOverlayExpired |
| `EdgeTriggered` | Drop if same event already in queue | NfcTagDetected, BtConnected |
| `Telemetry` | May drop silently | DiagnosticModeEntered |

Queue overflow on `Critical` class requires ISR-safe handling. `LOGC` and `esp_restart()`
are NOT safe to call from ISR context (UART write, stack usage). The mechanism is:

```cpp
// ISR-safe path (called from postEventFromIsr):
if (xQueueSendFromISR(dispatcherQueue, &ev, &xHigherPriorityTaskWoken) != pdTRUE
        && isCritical(ev)) {
    // Set RTC flag only — no UART, no restart from ISR
    rtcSetCriticalQueueOverflowFlag(ev.type);  // writes single RTC word, ISR-safe
    // Watchdog will fire within 15s and produce a restart with the RTC flag set
}

// Task-safe path (called from postEventFromTask):
if (xQueueSend(dispatcherQueue, &ev, 0) != pdTRUE && isCritical(ev)) {
    LOGC("[QUEUE] Critical event dropped: %d\n", (int)ev.type);
    rtcSaveCriticalQueueOverflow(ev.type);
    esp_restart();
}
```

On next boot, `setup()` reads the RTC flag and logs a CRIT warning with the dropped event type.

### Queue Depth

Minimum queue depth: [Must validate in implementation — measure worst-case burst]

Recommended starting point: 16 events. Rationale:
- Boot sequence may produce ~5 events in rapid succession (BootStarted, WakeCause,
  BootInitCompleted, MappingsLoaded, BtInitStarted).
- NFC + BT + button events can arrive simultaneously.
- LED coalescing reduces downstream work but not queue depth.

---

## Timers and Deadlines

The dispatcher manages timers as FreeRTOS software timers or by polling `AppState` deadlines.

### Deadline Polling Strategy

After each state transition, dispatcher checks all deadline fields in `AppState`:

```cpp
void checkDeadlines() {
    uint32_t now = millis();
    if (s.idle_deadline_ms != 0 && now >= s.idle_deadline_ms)
        postEvent(makeEvent(EventType::IdleTimeoutFired));
    if (s.night_light_deadline_ms != 0 && now >= s.night_light_deadline_ms)
        postEvent(makeEvent(EventType::NightLightTimeoutFired));
    if (s.volume_overlay_deadline_ms != 0 && now >= s.volume_overlay_deadline_ms)
        postEvent(makeEvent(EventType::VolumeOverlayExpired));
    if (s.battery_preview_deadline_ms != 0 && now >= s.battery_preview_deadline_ms)
        postEvent(makeEvent(EventType::BatteryPreviewExpired));
    if (s.brightness_save_deadline_ms != 0 && now >= s.brightness_save_deadline_ms)
        postEvent(makeEvent(EventType::BrightnessSaveDeadlineFired));
    if (s.jbl_recovery_deadline_ms != 0 && now >= s.jbl_recovery_deadline_ms)
        postEvent(makeEvent(EventType::JblRecoveryTimeoutFired));
    if (s.bt_reconnect_deadline_ms != 0 && now >= s.bt_reconnect_deadline_ms)
        postEvent(makeEvent(EventType::BtReconnectTimeoutFired));
}
```

After posting a deadline event, the deadline field is NOT cleared here — it is cleared by
the reducer when it processes the event (reducer sets field to 0).

### Timer Rules

- Timers post events only — they never call hardware APIs directly.
- Reducer owns policy for timer events.
- Dispatcher owns timer start/stop based on `AppState` transitions.
- No hidden deadline state remains in feature modules after migration.

---

## Pending Effects Registry

The dispatcher maintains a fixed-size array of pending async effects:

```cpp
struct PendingEffect {
    CmdId    cmd_id;
    EffectType type;
    uint32_t timeout_deadline_ms;  // 0 = no timeout
    bool     active;
};

static PendingEffect pendingEffects[MAX_PENDING_EFFECTS];  // MAX = 4
```

`MAX_PENDING_EFFECTS = 4`: at most one audio command, one BT command, one system sound,
one persistence write outstanding simultaneously. [Must validate in implementation]

When `timeout_deadline_ms != 0 && millis() >= timeout_deadline_ms`:
- Dispatcher posts the synthetic timeout event (e.g., `NfcPlaybackStartFailed(AudioStartTimeout)`).
- Removes the pending effect record.

### Stale Feedback Policy

A feedback event carrying a `CmdId` may arrive after its pending effect has already been
removed (timeout fired, or a superseding command was issued). The dispatcher applies this
policy **before** calling `reduce()`:

1. Look up `cmd_id` in `pendingEffects[]`.
2. If **found**: clear the pending record, pass the event to `reduce()` normally.
3. If **not found** (stale): log `[DISP] WARN stale feedback cmd=%u type=%d — ignored` and
   discard the event. Do NOT call `reduce()`.

Rationale: the reducer has already processed the timeout/rejection event for that `cmd_id`.
Feeding it a late success would put the state machine in an inconsistent state (e.g.,
`audio_state` set to `PlayingFile` after the timeout already reset it to `Idle`).

---

## Observability

The dispatcher logs every state transition and effect:

```
[DISP] BtConnected → {bt=Connected, audio=StartingFile} +[StartNfc("04:AA:BB",cmd=12), SetVol(80)]
[DISP] effect sent: StartNfc cmd=12 deadline=T+5000
[DISP] NfcPlaybackStarted cmd=12 (pending cleared)
[DISP] TIMEOUT: cmd=12 NfcPlayback → posting NfcPlaybackStartFailed
```

Ring buffer (retained in RAM):
- Last 32 state transitions
- Last 32 effect dispatches
- Last 16 effect feedback events
- Last 8 contract failures (CRIT log entries)

Ring buffer is dumped on: CRIT assert, watchdog reset, diagnostic mode entry.

### Task HWM Logging

Every 30 seconds during normal operation, dispatcher logs all task stack HWMs:

```
[DISP] HWM: app=%u led=%u audio=%u nfc=%u
```

Release gate: all HWMs must remain > 512 bytes under soak. See [10b_test_hardware.md](10b_test_hardware.md).

---

## Boot and Initialization Sequence

Bootstrap responsibilities (outside reducer, before dispatcher starts):

1. Serial/log init
2. `handleWakeFromDeepSleep()` — hardware gate (may return to sleep)
3. Check RTC crash flag → log warning if set, clear flag
4. GPIO init
5. SD init → load mappings (`figurineMap`, `systemSoundMap`) and music library
6. LED config load from SD
7. Check diagnostic pending flag
8. Hardware modules init in proven-safe order (NFC, JBL, audio — NOT BT yet)
9. Load saved state from NVS: `PlaybackMode`, `night_light_brightness_percent`, volume
10. Create dispatcher task and event queue
11. Post initial events: `BootStarted`, then `WakeCauseResolved*`
12. Dispatcher task starts processing

Dispatcher posts `BootInitCompleted` after hardware adapters are ready.
NFC prescan (if applicable) happens before `BootInitCompleted`.
`BtInitStarted` is posted when BT adapter initialization begins (async).

Any boot failure that prevents safe operation must be documented as: fatal (halt/restart)
or degraded (continue without that feature). [Must validate in implementation for each module]
