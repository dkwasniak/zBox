# LED/NFC stutter debug notes

Date: 2026-05-30

Symptom: LED animation is smooth for the first 5-8 seconds after boot, then starts stuttering roughly every 2-3 seconds.

Observed tests:
- With NFC fully disabled via `ENABLE_NFC false`, LED animation did not stutter.
- Re-enabling NFC and removing `ledSuspendTask()` from the regular `readNfcTag()` scan was not sufficient; the animation still stuttered.

Current test change:
- `ENABLE_NFC` is set back to `true` in `esp32/src/zbox_config.h`.
- Experiments that did not fix the stutter:
  - regular NFC reads without `ledSuspendTask()`
  - NFC task priority lowered to `0`
  - periodic NFC task pinned to core 0
  - `NFC_PASSIVE_ACTIVATION_RETRIES = 0x00` and `NFC_READ_TIMEOUT_MS = 1`
- Current direction: restore NFC behavior close to `for_github` and test a pinned FastLED version instead of allowing PlatformIO to resolve `^3.6.0` to `3.10.3`.
- FastLED `3.6.0` was tried but does not build with the current ESP-IDF 5.x platform; the current pinned test version is `3.9.16`.
- NFC regular reads, init, prescan, and powerdown use `nfcCriticalBegin()` / `nfcCriticalEnd()`.
- NFC task is back on core 1, priority 1. LED task priority is `1`.

If this breaks the build or behaves worse:
1. In `esp32/platformio.ini`, change the pinned `fastled/FastLED@...` line back to `fastled/FastLED@^3.6.0`.
2. Rebuild with `pio run -e lolin_d32_pro`.

Useful logs to check:
- `[LED] ... frame gap=...`
- `[LED] FastLED.show mode=... dt=...`
- `[NFC] read critical dt=... found=...`
