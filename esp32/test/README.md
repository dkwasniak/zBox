# zBox — test documentation

## Table of contents

1. [Test architecture](#architektura)
2. [Stage 1 — Native unit tests](#etap-1)
3. [Stage 2 — Boot sequence & timing](#etap-2)
4. [Stage 3 — Heartbeat & DIAG](#etap-3)
5. [Stage 4 — Tester firmware](#etap-4)
6. [Stage 5 — Button automation](#etap-5)
7. [Stage 6 — Manual NFC tests](#etap-6)
8. [Infrastructure](#infrastruktura)
9. [Test hardware](#sprzet)
10. [Known hardware behaviours](#zachowania)

---

## Test architecture {#architektura}

Three levels of tests:

```
┌─────────────────────────────────────────────────────────┐
│  Stage 1: Native unit tests                             │
│  PlatformIO + Unity, C++, WITHOUT hardware              │
│  esp32/test/test_*/                                     │
├─────────────────────────────────────────────────────────┤
│  Stages 2-3: Serial integration tests                   │
│  Python + pyserial, observing DUT logs over USB         │
│  esp32/test/integration/test_boot_sequence.py                │
│  esp32/test/integration/test_timing.py                       │
│  esp32/test/integration/test_heartbeat.py                    │
├─────────────────────────────────────────────────────────┤
│  Stages 4-6: Hardware automation + manual               │
│  Second ESP32-S3 simulates buttons, NFC figurine        │
│  esp32/test/integration/test_buttons.py                      │
│  esp32/test/integration/test_nfc_playback.py                 │
└─────────────────────────────────────────────────────────┘
```

---

## Stage 1 — Native unit tests {#etap-1}

### Requirements

```bash
cd esp32
pio platform install native   # one-time
```

### Running

```bash
cd esp32
pio test -e native -v
```

### What is tested

#### `test_uid_format` (7 tests)

Tests `uidToString()` from `main.cpp:271` — conversion of NFC UID byte array to string.

| Test | Input | Expected output |
|------|---------|-----------------|
| `test_uid_4_bytes` | `{0x04, 0xA3, 0xB2, 0xC1}` | `"04:A3:B2:C1"` |
| `test_uid_7_bytes` | 7 bytes | `"04:A3:...:80"` |
| `test_uid_lowercase_uppercased` | `{0xab, 0xcd}` | `"AB:CD"` |
| `test_uid_zero_byte_padded` | `{0x00, 0x05}` | `"00:05"` (not `"0:5"`) |
| `test_uid_ff_byte` | `{0xFF, 0xFF}` | `"FF:FF"` |
| `test_uid_single_byte` | `{0x05}` | `"05"` |
| `test_uid_all_zeros` | `{0,0,0,0}` | `"00:00:00:00"` |

**Note:** `test_uid_zero_byte_padded` is a regression guard. `String(0x05, HEX)` in Arduino returns `"5"` (without a leading zero); the `"0"` padding is added manually in `uidToString()`. If this logic is removed during refactoring, the test will catch it.

#### `test_volume` (16 tests)

Tests the volume logic (`main.cpp:684-698`) and `batteryBars()` (`main.cpp:1790`).

**Volume** (`BT_VOL_STEP=5, MIN=0, MAX=100`):

| Test | Operation | Input state | Expected output |
|------|----------|----------------|-----------------|
| `test_vol_up_normal` | `volumeUp` | 50 | 55 |
| `test_vol_up_clamps_at_100` | `volumeUp` | 100 | 100 (clamp) |
| `test_vol_near_max` | `volumeUp` | 98 | 100 (clamp) |
| `test_vol_down_normal` | `volumeDown` | 50 | 45 |
| `test_vol_down_clamps_at_0` | `volumeDown` | 0 | 0 (clamp) |
| `test_vol_near_min` | `volumeDown` | 2 | 0 (clamp) |

**Battery** (thresholds from Li-Po 1S discharge curve):

| Test | Voltage | Expected bars |
|------|----------|-----------------|
| `test_battery_5bars` | 4.20V | 5 |
| `test_battery_5bars_edge` | 4.05V | 5 (boundary) |
| `test_battery_4bars` | 3.95V | 4 |
| `test_battery_4bars_edge` | 3.90V | 4 (boundary) |
| `test_battery_3bars` | 3.85V | 3 |
| `test_battery_3bars_edge` | 3.80V | 3 (boundary) |
| `test_battery_2bars` | 3.75V | 2 |
| `test_battery_2bars_edge` | 3.70V | 2 (boundary) |
| `test_battery_1bar` | 3.50V | 1 |
| `test_battery_boundary_below_390` | 3.899V | 3 (not 4) |

#### `test_mapping` (8 tests)

Replicates the `loadMappings()` logic (`main.cpp:739`) with ArduinoJson v7, without SD (data from JSON string).

| Test | Scenario | Expected output |
|------|-----------|-----------------|
| `test_valid_single_mapping` | 1 figurine in JSON | `size=1, returns true` |
| `test_valid_multiple_mappings` | 2 figurines | `size=2, returns true` |
| `test_empty_figurines_object` | `{"figurines":{}}` | `size=0, returns true` |
| `test_invalid_json` | `{not valid` | `returns false` |
| `test_missing_figurines_key` | `{"other":{}}` | `size=0, returns true`* |
| `test_lookup_known_uid` | known UID | `find() != end()` |
| `test_lookup_unknown_uid` | unknown UID | `find() == end()` |
| `test_filename_value_correct` | known UID | `value == "abc.mp3"` |

*The firmware does not check for the presence of the `figurines` key — it returns `true` with an empty map. The test documents this behaviour.

---

## Stage 2 — Boot sequence & timing {#etap-2}

### Requirements

```bash
cd esp32/test
pip install -r requirements.txt
```

DUT (Lolin D32 Pro) connected via USB, firmware flashed.

### Running

```bash
cd esp32/test
pytest integration/test_boot_sequence.py integration/test_timing.py \
  -v --port /dev/cu.usbserial-10
```

### What is tested

#### `test_boot_sequence.py` (5 tests)

Verifies that all boot messages appear in the correct order:

```
=== zBox ===
[T+  0] Boot start
[T+  N] GPIO ready
[T+  N] SD OK|FAIL
[BOOT] sync_pending flag: 0
--- Normal mode
[T+  N] NFC OK|FAIL
[T+  N] Mappings loaded (N)
[T+  N] BT A2DP starting -> JBL GO 2
```

| Test | What it verifies |
|------|--------------|
| `test_boot_sequence_order` | Entire sequence in order (timeout 30s) |
| `test_sd_reports_status` | SD log contains `OK` or `FAIL` (not silent) |
| `test_nfc_reports_status` | NFC log contains `OK` or `FAIL` |
| `test_mappings_count_logged` | `Mappings loaded (N)` — N is a number |
| `test_bt_starting_with_name` | Log contains the specific name `JBL GO 2` |

#### `test_timing.py` (4 tests + 1 skipped)

Verifies that subsystem initialisation fits within timing thresholds. T+ values are measured from `bootStart` (after `handleWakeFromDeepSleep()`).

| Test | Threshold | Empirical value |
|------|------|-------------------|
| `test_gpio_ready_within_200ms` | ≤ 200ms | ~5ms |
| `test_sd_init_within_500ms` | ≤ 500ms | ~24ms |
| `test_nfc_init_within_2500ms` | ≤ 2500ms | ~1576ms |
| `test_bt_start_within_5000ms` | ≤ 5000ms | ~3638ms |
| `test_boot_to_play_under_10s` | — | requires `--run-hardware` |

---

## Stage 3 — Heartbeat & DIAG {#etap-3}

### Running

```bash
# Without soak (fast, ~35s)
pytest integration/test_heartbeat.py -v --port /dev/cu.usbserial-10 -m "not soak"

# With soak (60s without a freeze)
pytest integration/test_heartbeat.py -v --port /dev/cu.usbserial-10 -m soak
```

### Important: no reset in heartbeat tests

Heartbeat tests **do not reset the device** — they observe a running system after the boot tests. Reason: multiple rapid resets cause the JBL GO 2 to stop reconnecting quickly (BT connection time 6→40+s), causing `setup()` to block on `a2dp.begin()` for longer than the test timeouts.

Heartbeat tests depend on the previous tests having left the device in a state where `loop()` is running.

### What is tested

| Test | Pattern | Timeout | What it verifies |
|------|---------|---------|--------------|
| `test_loop_heartbeat_appears` | `[LOOP] alive` | 10s | `loop()` is not frozen |
| `test_loop_heartbeat_repeats_3x` | `[LOOP] alive` ×3 | 25s | 3 distinct lines (not the same one) |
| `test_nfc_task_heartbeat` | `[NFC] alive hwm=N err=N` | 10s | NFC task is alive |
| `test_diag_hwm_appears` | `[DIAG] HWM` | 35s | Stack monitor is running |
| `test_diag_all_tasks_above_512` | HWM loop/led/audio/nfc | 35s | No task is at risk of stack overflow |
| `test_diag_heap_appears` | `[DIAG] heap free=N` | 35s | Heap monitor is working |
| `test_no_freeze_60s` | `[LOOP] alive` every ≤15s | 60s | No freeze for one minute |

---

## Stage 4 — Tester firmware {#etap-4}

### Description

Separate firmware for a second ESP32-S3 (N16R8) that physically simulates DUT button presses via GPIO.

### Wiring diagram

```
Tester ESP32-S3          DUT Lolin D32 Pro
  GPIO4  (OUT_A) ──────  GPIO32 (BTN_A, long press = battery)
  GPIO5  (OUT_B) ──────  GPIO33 (BTN_B, free)
  GPIO6  (OUT_C) ──────  GPIO25 (BTN_C, VOL- / sleep)
  GPIO7  (OUT_D) ──────  GPIO26 (BTN_D, VOL+ / wake)
  GND            ──────  GND   (common ground — mandatory!)
```

### Operating principle

- **Press** = `pinMode(pin, OUTPUT)` + `digitalWrite(pin, LOW)` — DUT sees LOW
- **Release** = `pinMode(pin, INPUT)` — Hi-Z, DUT pullup pulls to HIGH
- **NEVER** `OUTPUT HIGH` — avoids conflict with DUT pullup

### Serial protocol (115200)

| Command | Effect |
|---------|-------|
| `PRESS A 2000\n` | Press BTN_A for 2000ms, then release |
| `PRESS_COMBO AB 2500\n` | Press BTN_A + BTN_B simultaneously for 2500ms |
| `RELEASE ALL\n` | Release all pins (Hi-Z) |
| `PING\n` | Responds with `PONG\n` |

Responses: `OK\n` (success) or `ERR reason\n` (error).
Safety timeout: 10s without a command → automatic `RELEASE ALL`.

### Flashing firmware

```bash
cd esp32/tester
pio run -t upload

# Verification
python3 -c "
import serial, time
s = serial.Serial('/dev/cu.usbmodem*', 115200, timeout=2)
s.write(b'PING\n')
print(s.readline())  # should print: b'PONG\n'
s.close()
"
```

---

## Stage 5 — Button automation tests {#etap-5}

### Requirements

- Tester ESP32-S3 connected and flashed (stage 4)
- Wires between tester and DUT as per the wiring diagram
- DUT connected via USB to `--port`
- Tester connected via USB to `--tester-port`

### Running

```bash
pytest integration/test_buttons.py -v \
  --port /dev/cu.usbserial-10 \
  --tester-port /dev/cu.usbmodem<N>
```

All tests have the `@requires_tester` marker — without `--tester-port` they are automatically skipped.

Each test waits for `BT A2DP starting` before performing any action (ensuring the device has booted).

### What is tested

| Test | Action | Verified log |
|------|-------|-----------------|
| `test_volume_up_logs` | Short BTN_D (100ms) | `[VOL]` |
| `test_volume_down_logs` | Short BTN_C (100ms) | `[VOL]` |
| `test_battery_long_press_a` | Long BTN_A (2500ms) | `[BAT] Voltage:` |
| `test_battery_voltage_range` | Long BTN_A (2500ms) | voltage 3.0V–4.5V |
| `test_deep_sleep_trigger` | Long BTN_C (2500ms) | `[SLEEP]` |
| `test_sync_mode_trigger` | Combo BTN_A+B (2500ms) | `sync_pending flag` / restart → `SYNC MODE` |

---

## Stage 6 — Manual NFC tests {#etap-6}

### Running

```bash
pytest integration/test_nfc_playback.py -v \
  --port /dev/cu.usbserial-10 \
  --run-manual
```

Without `--run-manual` tests are skipped automatically.

### What is tested

Tests prompt the user for physical interaction via `input()` before each verification.

| Test | User action | Verified log |
|------|------------------|-----------------|
| `test_nfc_place_triggers_playback` | Place figurine | `startPlayback uid=...` + `PLAYBACK START` |
| `test_uid_format_valid` | Place figurine | UID matches `[0-9A-F]{2}(:[0-9A-F]{2}){3,6}` |
| `test_nfc_remove_stops_playback` | Remove figurine | `stopPlayback` within 5s |
| `test_audio_telemetry` | Figurine plays music | `[AUDIO_TEL] SD=N B/s` |

---

## Infrastructure {#infrastruktura}

### `serial_harness.py`

`SerialHarness` class — asynchronous serial reading with pattern matching.

| Method | Description |
|--------|------|
| `wait_for_line(pattern, timeout_s)` | Waits for a line matching a regex. Scans from position 0 of the buffer. |
| `wait_for_nth_occurrence(pattern, n, timeout_s)` | Waits for N distinct occurrences of the pattern (tracks position — does not return the same line). |
| `wait_for_sequence(patterns, timeout_s)` | Waits for a list of patterns in order. |
| `reset_device()` | DTR pulse: `setDTR(False)` → 100ms → `setDTR(True)` → 100ms. Resets the ESP32 via the EN pin. |
| `flush()` | Clears the buffer and event. Use before `reset_device()` to avoid stale lines. |

Buffer: `deque(maxlen=2000)`. Reader thread runs in the background at all times.

### `button_tester.py`

`ButtonTester` class — controls the tester ESP32-S3 via serial.

| Method | Command sent |
|--------|-----------------|
| `press(btn, duration_ms)` | `PRESS A 2000\n` |
| `press_combo(btns, duration_ms)` | `PRESS_COMBO AB 2500\n` |
| `release_all()` | `RELEASE ALL\n` |

### `conftest.py` — fixtures and markers

**Fixtures:**

| Fixture | Scope | Description |
|---------|-------|------|
| `serial_harness` | session | One SerialHarness for the entire pytest session. Does not reset on startup. |
| `button_tester` | session | ButtonTester, skipped if `--tester-port` is not provided. |
| `reset_esp` | function | Flush + reset DUT before the test (for tests that require it). |

**Markers:**

| Marker | How to run | Description |
|--------|--------------|------|
| `soak` | `-m soak` | Long test (>60s) |
| `requires_tester` | `--tester-port /dev/cu.usbmodem*` | Requires tester ESP32-S3 |
| `requires_hardware` | `--run-hardware` | Requires JBL + figurine |
| `manual` | `--run-manual` | Requires physical interaction |

---

## Test hardware {#sprzet}

### Required for stage 1

Nothing — tests compile and run natively on PC.

### Required for stages 2-3

- DUT: Lolin D32 Pro with zBox firmware flashed
- USB cable → `/dev/cu.usbserial-10` (CH340)
- SD card with `data/mappings.json`
- JBL GO 2 powered on (tests wait for boot, BT connect happens in the background)

### Required for stages 4-5

As above, plus:
- Tester: ESP32-S3-DevKitC-1 (N16R8) with `esp32/tester/` flashed
- Tester USB cable → `/dev/cu.usbmodem*`
- 4 GPIO wires between tester and DUT (see wiring diagram in stage 4)
- Common GND ground — mandatory

### Required for stage 6

As stages 2-3, plus a figurine with an NFC sticker (any from `data/mappings.json`).

---

## Known hardware behaviours {#zachowania}

### BT reconnect time

JBL GO 2 reconnects within 6-9s from reset (variable). After several rapid consecutive resets the reconnect time grows above 10s. Therefore:
- Heartbeat tests **do not reset** the device
- Timing tests have a BT start threshold of 5000ms (not 3000ms)
- Do not run multiple resets in a row without a pause between them

### Figurine on the pad during tests

During tests, boot logs show `NFC pre-scan: 3C:26:D6:05` — the figurine is sitting on the pad and music starts automatically. This is normal and does not interfere with tests 1-3. Button tests (stage 5) do not require a figurine.

### IDLE_TIMEOUT

After 10 minutes without playback the device enters deep sleep. If tests run for > 10 min without a figurine, the device may fall asleep and heartbeat tests will stop receiving responses. Solution: keep a figurine on the pad, or run tests with shorter intervals.
