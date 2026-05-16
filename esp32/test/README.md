# zBox — test documentation

## Overview

The firmware test strategy is now host-side only.

All maintained tests live under `esp32/test/test_*` and run in PlatformIO `native` environments. There are no serial integration tests, tester-firmware flows, or manual hardware test scripts in this repository anymore.

## Running

```bash
cd esp32
pio platform install native   # one-time
pio test -e native -v
pio test -e native_btndec -v
```

## Suites

### `test_uid_format` (7 tests)

Tests `uidToString()` formatting rules for NFC UIDs:

- 4-byte and 7-byte UIDs
- uppercase hex output
- zero padding regression cases
- all-zero and single-byte inputs

### `test_volume` (21 tests)

Tests pure helpers:

- output-volume clamp/step logic
- battery-to-bars thresholds

### `test_mapping` (8 tests)

Tests JSON mapping parsing behavior:

- valid and invalid `mappings.json`
- empty and missing `figurines`
- lookup for known and unknown UIDs

### `test_reducer` (21 tests)

Tests the pure `reduce(AppState, Event, now_ms)` state machine:

- boot readiness and startup autoplay
- `PlaybackModeLoaded`, `MappingsLoaded`, `VolumeLoaded`
- NFC playback and music playback transitions
- play/pause/next/prev behavior
- BT headphones mode lifecycle
- sleep flow, power-off sound, BT stop-before-sleep
- battery preview and volume overlay deadlines

### `test_bt_adapter` (3 tests)

Tests the BT adapter contract:

- start while headphones are already connected
- connection-edge event emission
- stop completion without false disconnect events

### `test_audio_adapter` (7 tests)

Tests critical audio-adapter failure paths:

- missing NFC mapping
- missing music index
- missing or unknown system sound
- queue rejection for playback/system sound
- best-effort `AudioStopped` on failed stop enqueue

### `test_dispatcher` (4 tests)

Tests dispatcher timeout handling:

- NFC playback start timeout
- system-sound timeout
- stop timeout
- pending registry cleanup after feedback

### `test_button_decoder` (27 tests, `native_btndec`)

Tests pure button decoding:

- single and double click behavior
- short `BTN_C` / `BTN_D` volume actions
- long `BTN_A` -> BT headphones mode request
- long `BTN_D` -> battery preview request
- long `BTN_B` -> mode toggle
- long `BTN_C` and emergency hold sleep paths
- combo `A+B` and `C+D`
- debounce and swallowed-release regressions

## Scope

These tests cover the maintained architectural contracts:

- pure reducer logic
- dispatcher timeout behavior
- adapter-level failure handling
- button decoding
- target firmware compilation via `pio run -e lolin_d32_pro`

They do not simulate real ESP32 peripherals or radio stacks. NFC silicon behavior, SD timing, deep sleep wake causes, and actual Bluetooth stack behavior still require manual hardware validation outside this repo.
