# ESP32 Firmware

Firmware for the zBox NFC audio device, built with PlatformIO on the ESP32 Lolin D32 Pro using the mixed `arduino, espidf` framework.

## Build environments

| Environment | Purpose |
|-------------|---------|
| `lolin_d32_pro` | Main firmware for the device |
| `native` | Unit tests (reducer, state logic) — runs on the host |
| `native_btndec` | Unit tests for the button decoder module |

```bash
pio run                    # build firmware
pio run -t upload          # flash to device
pio device monitor         # serial monitor at 115200 baud
pio test -e native         # run native unit tests
```

## Compile-time configuration

All device-specific constants live in [`esp32/src/zbox_config.h`](../esp32/src/zbox_config.h). The ones you are most likely to change before flashing:

| Constant | Default | Purpose |
|----------|---------|---------|
| `SERVER_HOST` | `"zbox.local"` | Hostname or IP of the server used for Wi-Fi sync |
| `BT_SPEAKER_NAME` | `"JBL GO 2"` | Bluetooth device name to connect to |
| `ENABLE_LEDS` | `true` | Enable or disable the WS2812B LED panel |

GPIO assignments and tuning constants (timeouts, ADC factors, volume steps) are also defined there.

## Architecture

The firmware uses a **reducer + dispatcher** architecture. All application logic lives in a pure function:

```
reduce(AppState, Event) → (next AppState, Effects[])
```

The dispatcher executes effects (start audio, power BT, sleep, sync) and feeds feedback events back into the queue. No module owns hidden state — all state is in `AppState`.

### Modules

| Module | Responsibility |
|--------|---------------|
| `state` | `AppState` definition and zero-init |
| `reducer` | Pure `reduce()` function, no I/O |
| `dispatcher` | FreeRTOS task, event queue, effect execution |
| `audio` | SD card audio playback adapter |
| `nfc_module` | PN532 NFC reader, tag detection loop |
| `playback` | Track list management and position tracking |
| `buttons` | Button press detection, debounce, long-press |
| `buttons_isr` | ISR-level button sampling |
| `sleep` | Deep sleep entry, wake protocol, night-light mode |
| `sync_mode` | Wi-Fi sync with the server over HTTP |
| `leds` | WS2812B LED scenes derived from `AppState` |
| `jbl` | JBL power control and status detection |
| `volume` | BT AVRCP volume commands |
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
