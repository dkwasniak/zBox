# ESP32 Firmware

Firmware for the zBox NFC audio device, built with PlatformIO on the ESP32 Lolin D32 Pro using the mixed `arduino, espidf` framework.

## Build environments

| Environment | Purpose |
|-------------|---------|
| `lolin_d32_pro` | Main firmware for the device — **logging compiled out** (default) |
| `lolin_d32_pro_debug` | Main firmware **with logging enabled** (`LOG_ENABLED`) |
| `lolin_d32_pro_ota` | `lolin_d32_pro` flashed over the air (espota) |
| `lolin_d32_pro_debug_ota` | `lolin_d32_pro_debug` flashed over the air (espota) |
| `native` | Unit tests (reducer, state logic) — runs on the host |
| `native_btndec` | Unit tests for the button decoder module |

```bash
pio run                              # build release firmware (no logs)
pio run -t upload                    # flash to device
pio device monitor                   # serial monitor at 115200 baud

pio run -e lolin_d32_pro_debug       # build with logging enabled
pio device monitor -b 921600         # serial monitor for the debug build

pio test -e native                   # run native unit tests
```

## Compile-time configuration

All device-specific constants live in [`esp32/src/zbox_config.h`](../esp32/src/zbox_config.h). The ones you are most likely to change before flashing:

| Constant | Default | Purpose |
|----------|---------|---------|
| `AUDIO_I2S_BCLK` | `32` | NS4168 bit clock pin |
| `AUDIO_I2S_LRCK` | `33` | NS4168 left/right clock pin |
| `AUDIO_I2S_DOUT` | `13` | NS4168 data output pin |
| `ENABLE_LEDS` | `true` | Enable or disable the WS2812B LED panel |

GPIO assignments and tuning constants (timeouts, ADC factors, volume steps) are also defined there.

## Logging

Logging is controlled by the compile-time flag `LOG_ENABLED`, defined only in the `_debug` environments. The macros and persistent-log backend live in [`esp32/src/util/logging.h`](../esp32/src/util/logging.h) and [`esp32/src/util/persistent_log.cpp`](../esp32/src/util/persistent_log.cpp); the flag is wired up in [`esp32/platformio.ini`](../esp32/platformio.ini).

**Release** (`lolin_d32_pro`, the default env) — logging is compiled out entirely. The `LOGI/LOGW/LOGE/LOGC` macros expand to `((void)0)` (arguments are never evaluated) and `plog*`/`logWritef` early-return. No `Serial`/`vsnprintf` cost, no RTC/SD persistence, and about 12 KB less flash. The trade-off is losing the post-crash "RECOVERED FROM RTC" dump.

**Debug** (`lolin_d32_pro_debug`) — logging is on. Serial runs at 921600 baud with an enlarged TX buffer, and writes are **non-blocking**: a line is dropped rather than stalling a task when the TX buffer is full, so bursts of logs can't add jitter to the audio/BT path. Persistent lines are still kept in the RTC ring even when the Serial print is skipped.

Sync mode is separate: the `/diag/logs` HTTP ring buffer stays populated in every build, and only its `Serial` output is gated behind `LOG_ENABLED`.

## Architecture

The firmware uses a **reducer + dispatcher** architecture. All application logic lives in a pure function:

```
reduce(AppState, Event) → (next AppState, Effects[])
```

The dispatcher executes effects (start audio, enter/leave BT headphones mode, sleep, sync-mode restart) and feeds feedback events back into the queue. No module owns hidden state — all state is in `AppState`.

### Modules

| Module | Responsibility |
|--------|---------------|
| `state` | `AppState` definition and zero-init |
| `reducer` | Pure `reduce()` function, no I/O |
| `dispatcher` | FreeRTOS task, event queue, effect execution |
| `audio` | SD card audio playback over local I2S with optional BT headphones routing and zBox-side PCM volume attenuation |
| `nfc_module` | PN532 NFC reader, tag detection loop |
| `playback` | Track list management and position tracking |
| `button_adapter` | Button press detection, debounce, combo and long-press decoding |
| `sleep` | Deep sleep entry, wake protocol, night-light mode |
| `sync_mode` | Wi-Fi service mode used by the admin portal for push sync, logs, and maintenance |
| `leds` | WS2812B LED scenes derived from `AppState` |
| `bt_adapter` | Temporary BT headphones-mode lifecycle and connection edges |
| `volume` | Output volume persistence and apply path |
| `battery` | ADC battery level reading |
| `sd_storage` | SD card init, file listing, path helpers |
| `persistent_log` | Diagnostic log written to SD |
| `logging` | Serial log helpers |
| `helpers` | Miscellaneous utilities |
| `musicbox_config` | Runtime configuration loaded at boot |

### Event flow

```
ISR / NFC task / timer
        │  Event
        ▼
   Event queue (FreeRTOS)
        │
        ▼
   Dispatcher task
        │  reduce(state, event) → (next_state, effects)
        ├─ updates AppState
        └─ executes Effects
               │  async result
               └─ feedback Event → queue
```

## Sleep and wake

The device enters deep sleep on:
- idle timeout (10 minutes without playback)
- BTN_C 2-second hold (normal sleep with shutdown animation)
- BTN_C 10-second hold (emergency sleep, skips animation)

Local audio is the default output on every boot. A long hold on `BTN_A` enables a temporary Bluetooth headphones mode; when the mode exits or the device restarts, output returns to local I2S.

Output volume is owned by zBox in both modes. For BT headphones the firmware scales PCM before it reaches the transport; it does not rely on remote headphone volume support. The legacy NVS key remains `bt_volume` only for backward compatibility with devices already in the field.

Wake from deep sleep requires a hold of BTN_D:
- release < 800 ms → return to sleep
- 800–1600 ms hold → normal boot
- ≥ 1600 ms hold → night-light mode

## Tests

Native tests cover reducer transitions and state invariants without requiring hardware. Run with:

```bash
pio test -e native
```

Hardware tests (sleep/wake cycles, BT reconnect, fault injection) are described in [`docs/refactor/10b_test_hardware.md`](refactor/10b_test_hardware.md).

## Further reading

Detailed design documents for the reducer/dispatcher architecture live in [`docs/refactor/`](refactor/):

- [00_overview.md](refactor/00_overview.md) — goals, locked decisions, safety constraints
- [01_appstate.md](refactor/01_appstate.md) — AppState layout
- [02_events_effects.md](refactor/02_events_effects.md) — event hierarchy and effects
- [03_reducer.md](refactor/03_reducer.md) — reducer contract
- [09_migration_stages.md](refactor/09_migration_stages.md) — staged migration plan with acceptance criteria
