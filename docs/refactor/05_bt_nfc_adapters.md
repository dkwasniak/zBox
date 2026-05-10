# Part 6/11: BT, NFC, and Persistence Adapter Contracts

← Prev: [04_audio_adapter.md](04_audio_adapter.md) | → Next: [06_sleep_wake.md](06_sleep_wake.md)

---

## BT Adapter Contract

### Current Architecture (Reference)

[Verified in current code: `esp32/src/audio.cpp` lines 161–174, 418–449]

- BT state tracked as `volatile bool g_btConnected`.
- Connection state detected by polling `a2dp.source().is_connected()` in main loop.
- No callbacks. No `postEventFromTask` / `postEventFromIsr` distinction.
- Recovery: `audioRestartDiscovery()` restarts A2DP for re-discovery after timeout.
- BT shutdown: `esp_bt_controller_disable()` in `enterDeepSleep()` — crash-sensitive ordering
  (must precede `jblPowerOff()`).

### New BT Adapter Interface

The BT adapter owns connection polling and posts events to the central dispatcher queue.

```cpp
// Called during setup to initialize A2DP (no callbacks registered — see design note).
void btAdapterInit();

// Called from dispatcher to issue BT effects:
void btAdapterTriggerRecoveryPulse(CmdId cmd_id);
    // Posts BtRecoveryPulseCompleted or BtRecoveryPulseFailed.

void btAdapterTriggerDiscoveryRestart(CmdId cmd_id);
    // Posts BtDiscoveryRestarted or BtDiscoveryRestartFailed.

void btAdapterShutdown(CmdId cmd_id);
    // Initiates BT controller disable sequence.
    // Posts BtShutdownCompleted or BtShutdownFailed.
    // Must preserve crash-safe ordering (see §Shutdown Ordering).

// Called periodically by dispatcher each loop iteration to poll connection state:
void btAdapterPoll();
    // Polls a2dp.source().is_connected(). Posts BtConnected or BtDisconnected
    // on state change (false→true or true→false).
```

**Design note — polling vs callbacks:** The BT adapter uses **polling only**, not A2DP callbacks.
Reason: the ESP32 Arduino A2DP library callback context is underdocumented and the current
implementation (polled) is verified stable. Registering callbacks for connection state introduces
untested code paths. If callbacks are added in the future, `postEventFromTask()` must be used
(NOT `postEventFromISR` — A2DP callbacks are task-context), and the polling path removed to
avoid duplicate event posting. [Must validate on hardware before switching to callbacks]

### BT Feedback Events

| Event | Trigger | Carries |
|-------|---------|---------|
| `BtConnected` | `is_connected()` transitions false→true | — |
| `BtDisconnected` | `is_connected()` transitions true→false | — |
| `BtRecoveryPulseCompleted` | JBL power pulse completed | — |
| `BtRecoveryPulseFailed` | Pulse timeout or ADC check failed | BtFailReason |
| `BtDiscoveryRestarted` | A2DP restart completed | — |
| `BtDiscoveryRestartFailed` | Restart timeout | BtFailReason |
| `BtShutdownCompleted` | `esp_bt_controller_disable()` returned | — |
| `BtShutdownFailed` | Shutdown timeout (see timeout table) | BtFailReason |

### Callback Context and Queue API

A2DP state callbacks run in the BT stack task context — NOT in an ISR.

Rule: use `postEventFromTask()` (which calls `xQueueSend` without `FromISR` suffix) for
all BT-originated events. Using `xQueueSendFromISR` in task context causes undefined
behavior. [Verified: A2DP library callbacks are task-context, not ISR-context]

```cpp
// Correct:
void onA2dpConnectionState(esp_a2d_connection_state_t state, ...) {
    // This is a task-context callback:
    Event ev = makeEvent(state == ESP_A2D_CONNECTION_STATE_CONNECTED
                         ? EventType::BtConnected : EventType::BtDisconnected);
    postEventFromTask(ev);  // xQueueSend, NOT xQueueSendFromISR
}
```

### JBL Recovery & BT Reconnect Deadline Lifecycle

The two BT-related deadline fields map to the existing two-phase recovery in `main.cpp`:

| Phase | Current code (`main.cpp`) | New model |
|-------|--------------------------|-----------|
| Deadline set | `btWaitStart = millis()` after `audioInit()` | `BootInitCompleted` → reducer sets `jbl_recovery_deadline_ms = now + 5000` |
| Phase 1 (5s) | JBL power pulse (500ms HIGH pulse) | `JblRecoveryTimeoutFired` → effect `TriggerBtRecoveryPulse(cmd_id)` |
| Phase 1 done | `jblRecoveryDone = true` | `BtRecoveryPulseCompleted` → reducer sets `bt_reconnect_deadline_ms = now + 15000` |
| Phase 2 (15s) | `audioRestartDiscovery()` | `BtReconnectTimeoutFired` → effect `TriggerBtDiscoveryRestart(cmd_id)` |
| BT connects | Clears `jblRecoveryDone`, `btDiscoveryFallbackDone` | `BtConnected` → reducer zeroes both deadline fields |

**Sequencing rule:** The two deadlines are **sequential, not parallel**.
`bt_reconnect_deadline_ms` is set only after `BtRecoveryPulseCompleted` — not at boot.
This preserves the original behavior: first try JBL pulse, then (if still no BT) try discovery.

```
BootInitCompleted
  → jbl_recovery_deadline_ms = now + 5000   (bt_reconnect_deadline_ms stays 0)

JblRecoveryTimeoutFired
  → TriggerBtRecoveryPulse(cmd_id)

BtRecoveryPulseCompleted
  → jbl_recovery_deadline_ms = 0
  → bt_reconnect_deadline_ms = now + 15000

BtReconnectTimeoutFired
  → TriggerBtDiscoveryRestart(cmd_id)
  → bt_reconnect_deadline_ms = 0  (one-shot)

BtConnected (any time)
  → jbl_recovery_deadline_ms = 0
  → bt_reconnect_deadline_ms = 0
```

### BT Shutdown Ordering

[Verified in current code: `esp32/src/sleep.cpp`]

The following ordering is crash-critical and must be preserved in the executor:

1. `esp_bt_controller_disable()` — MUST come before JBL power-off.
   Calling A2DP disconnect functions after `jblPowerOff()` causes a state machine crash
   in the ESP-IDF BT stack. This ordering is empirically proven.
2. `jblPowerOff()` — only after BT controller is disabled.

Any new shutdown path must be hardware-validated before replacing this sequence.
[Must validate on hardware]

### Shutdown Timeout

| Command | Timeout | On expiry |
|---------|---------|-----------|
| `btAdapterShutdown` | 3000ms | Post `BtShutdownFailed(DisableTimeout)` |

If `BtShutdownFailed`, the sleep executor must still attempt `jblPowerOff()` and enter
deep sleep — a failed BT shutdown is not a reason to hang forever.

---

## NFC Adapter Contract

### Current Architecture (Reference)

[Verified in current code: `esp32/src/nfc_module.cpp`]

- Separate FreeRTOS task polling PN532 via software SPI at 1Hz.
- `QueueHandle_t nfcQueue` (capacity 5) holds `NfcEvent {bool tagPresent, char uid[30]}`.
- Debounce: requires N consecutive "no tag" reads before emitting tag-removed event.
- Bus mutex: `nfcMutex` + LED task suspension during SPI critical sections.
- Power: `nfcPowerDown()` reduces PN532 current from ~100mA to ~1mA.

### NFC Adapter Events

In the new model, the NFC adapter posts directly to the central dispatcher event queue
instead of to a separate `nfcQueue`.

| Event | Trigger | Carries |
|-------|---------|---------|
| `NfcTagDetected` | New tag UID detected (after debounce) | char uid[24] |
| `NfcTagRemoved` | Tag removed (after consecutive empty reads) | — |
| `NfcPreScanCompleted` | Boot prescan finished | bool uid_found, char uid[24] |

### NFC Adapter Interface

```cpp
// Synchronous prescan before NFC task starts (called during boot).
bool nfcAdapterPrescan(char* uid_buf, size_t len);
    // Returns true if tag found. Fills uid_buf with UID string.
    // Fires NfcPreScanCompleted event to dispatcher queue.

void nfcAdapterStartTask();
    // Creates NFC polling task. Task posts NfcTagDetected / NfcTagRemoved events.

void nfcAdapterStopTaskForSleep();
    // Sets stop flag, waits NFC_READ_INTERVAL + 250ms for task exit.
    // Calls nfcPowerDown() after task exits.
```

### NFC Rules

- NFC task owns tag polling and debouncing — reducer never debounces.
- Reducer owns playback policy based on NFC events.
- NFC task stop and PN532 power-down ordering are executor responsibilities, not reducer.
- NFC queue/send failures are not silent: log CRIT if `xQueueSend` to dispatcher queue fails.
- `NfcTagDetected` is `EdgeTriggered` drop policy — if same UID event is already pending
  in the dispatcher queue, drop the duplicate.

---

## Persistence Adapter Contract

### Current Architecture (Reference)

[Verified in current code: `esp32/src/night_light.cpp`, `esp32/src/playback.cpp`]

- NVS/Preferences writes: synchronous, called inline during event handling.
- No feedback events — writes are fire-and-forget.
- Brightness saved with a short debounce (not on every brightness step).

### New Persistence Adapter Interface

Persistence writes are async in the new model. The reducer optimistically updates state
(desired brightness is immediately reflected in `AppState`) and an executor writes to NVS.

```cpp
void persistenceAdapterSaveBrightness(uint8_t percent);
    // Posts BrightnessPersisted or BrightnessPersistFailed to dispatcher.

void persistenceAdapterSavePlaybackMode(PlaybackMode mode);
    // Posts PlaybackModePersisted or PlaybackModePersistFailed to dispatcher.
```

### Persistence Feedback Events

| Event | Trigger | Carries |
|-------|---------|---------|
| `BrightnessPersisted` | NVS write succeeded | — |
| `BrightnessPersistFailed` | NVS write failed | PersistenceFailReason |
| `PlaybackModePersisted` | NVS write succeeded | — |
| `PlaybackModePersistFailed` | NVS write failed | PersistenceFailReason |

### Persistence Rules

- Reducer may optimistically update desired state before persistence succeeds.
- Persistence failure must not corrupt session consistency — the in-memory state remains
  valid even if the NVS write fails.
- A failure is logged and reported to the reducer; reducer decides whether to retry.
- Persistence timing (debounce deadline) belongs to dispatcher-managed deadlines, not to
  hidden per-module state. The `BrightnessSaveDeadlineFired` timer event triggers the
  actual `PersistBrightness` effect.
- Debounce interval: use the existing constant from `esp32/src/night_light.cpp`
  (currently `BRIGHTNESS_SAVE_DEBOUNCE_MS`). [Must validate: confirm exact value before Stage 5]
- Sleep interaction: if the device enters the sleep sequence while `brightness_save_deadline_ms`
  is still active (brightness changed but not yet flushed), the Normal sleep executor Step 1
  must flush synchronously and cancel the pending deadline. See [06_sleep_wake.md §Normal Sleep](06_sleep_wake.md).
  Consequence: on sleep entry, brightness is always persisted — no data loss on user-initiated sleep.
- Reads (loading saved brightness, playback mode) happen during boot, before the dispatcher
  starts, and are passed as initial AppState fields — not as events.
