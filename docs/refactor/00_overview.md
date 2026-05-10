# Part 1/11: Overview, Goals, and Safety Constraints

→ Next: [01_appstate.md](01_appstate.md)

---

## Summary

This refactor introduces a single-source-of-truth `AppState`, a pure
`reduce(state, event) -> next_state + effects` function, driver executors, and mandatory
feedback events for every async operation.

**Primary goal:** deterministic behavior under fault, no hidden state ownership, safe staged
migration with zero tolerance for regressions on a production device.

**What this document set is NOT:** a theoretical architecture exercise. Every contract here
must map to concrete hardware behavior or be explicitly tagged as needing validation.

---

## Locked Decisions

- `reducer + dispatcher` is the target architecture. No alternatives considered.
- Do not change proven low-level hardware sequences unless the new path is hardware-validated.
- Sleep/wake, BT shutdown, NFC power-down/wake, and boot sequencing are hardware-critical
  protocols. They are executor responsibilities, not reducer logic.
- Reducer does not know SD paths, hardware handles, or FreeRTOS APIs.
- LED scene selection derives from `AppState` via `deriveLedScene()`, with one explicit
  exception: beat-energy modulation reads `g_audioEnergy` and `g_beatDetected` globals
  (see [07_led_model.md §Beat Globals Contract](07_led_model.md)).
- Migration must preserve all current user-visible behaviors unless explicitly listed in the
  Behavior Changes section of the relevant stage document.
- No stage may leave a behavior with two active policy owners.

---

## Safety Constraints

The following behaviors are safety-critical and must be treated as protocol contracts:

- Deep sleep entry and wake handling (ordering matters for hardware stability)
- BT controller shutdown ordering relative to audio and JBL power
  (`esp_bt_controller_disable()` MUST precede `jblPowerOff()` — verified in sleep.cpp)
- NFC task stop and PN532 power-down ordering
- Boot-time wake-hold confirmation path (pre-scheduler, pre-FreeRTOS)
- Audio start/stop/system-sound feedback and timeout handling
- Queue overflow and command rejection behavior

Rules:
- No driver contract may be assumed reliable without either code proof or hardware validation.
- No async effect may be modeled as "eventually succeeds" without: success signal, failure
  signal, timeout owner, and recovery path — all defined before implementation.
- No stage may replace a working behavior with a weaker abstraction.
- Any behavior known to have crash-sensitive ordering must carry that ordering into the
  executor contract until proven unnecessary on hardware.

Assumption tags used across these documents:
- `[Verified in current code]` — checked against esp32/src/ source
- `[Must validate in implementation]` — design decision, not yet code-proven
- `[Must validate on hardware]` — requires physical device test

---

## Current Behavior Preservation Matrix

Every behavior below must work identically after migration unless explicitly listed as changed.

**Boot:**
- Boot from power-on into normal mode
- Wake from deep sleep with hold-to-confirm semantics:
  - release < 800ms → return to sleep
  - 800–1600ms hold → normal boot
  - ≥ 1600ms hold → night-light mode

**Night-light mode:**
- Brightness restore from NVS on boot
- Brightness adjustment on BTN_C (−) / BTN_D (+)
- Deferred brightness save (not on every button press)
- Timeout to sleep after `NIGHT_LIGHT_TIMEOUT_MS` (15 minutes)
- BTN_C or BTN_D press resets the 15-minute inactivity timer

**Normal mode:**
- NFC prescan at boot in NFC mode only
- Deferred NFC playback when JBL/BT not ready
- Music mode persistence across reboot
- Music mode autostart behavior on BT connect
- next/prev/play-pause button semantics
- Idle timeout sleep (10 minutes)
- Normal sleep: BTN_C 2s hold → sleep-ready LED (red blink) → releasing BTN_C triggers shutdown animation → deep sleep
- Emergency sleep: BTN_C held continuously to 10s → immediate deep sleep (skips animation)
- Battery-check action on BTN_A hold
- Diagnostic entry via A+B hold

**System sounds:**
- Mode-change sounds on mode toggle
- Power-off sound on normal sleep when BT path allows it

**LED behaviors** (all must be preserved):
- Boot progress bar
- Wake progress bar (pre-boot, pre-FreeRTOS)
- Wait BT breathing animation
- Idle scene
- Playing scene (beat-reactive)
- Volume overlay (auto-hide after 1s)
- Night-light warm breathing
- Sleep-ready slow pulse
- Sync WiFi animation
- Sync progress bar
- Diagnostic scene
- Warning/error flash
- Battery display (bar count)
- Mode-change flash

---

## Behavior Changes (Intentional)

None for Stages 0–5. Stage 6 cleanup may remove deprecated globals but must not change
any user-visible behavior. Any deviation from this must be documented here before
implementation begins.

---

## Document Index

| File | Contents |
|------|----------|
| [00_overview.md](00_overview.md) | Goals, locked decisions, safety constraints, behavior matrix |
| [01_appstate.md](01_appstate.md) | AppState design, sizeof analysis, deadline convention, enum invariants |
| [02_events_effects.md](02_events_effects.md) | Event hierarchy, drop policy, Effect types, budget analysis |
| [03_reducer.md](03_reducer.md) | Reducer contract, ReduceResult, EffectBuilder, assert safety |
| [04_audio_adapter.md](04_audio_adapter.md) | Audio adapter contract, cmd_id mechanism, all feedback events |
| [05_bt_nfc_adapters.md](05_bt_nfc_adapters.md) | BT, NFC, and Persistence adapter contracts |
| [06_sleep_wake.md](06_sleep_wake.md) | Normal, emergency, and night-light sleep; wake protocol |
| [07_led_model.md](07_led_model.md) | LED scene derivation, beat globals contract, coalescing |
| [08_dispatcher_runtime.md](08_dispatcher_runtime.md) | Dispatcher task, watchdog, timers, event queue policy |
| [09_migration_stages.md](09_migration_stages.md) | 6 stages with concrete acceptance criteria and checklists |
| [10_test_plan.md](10_test_plan.md) | Moved stub pointing to split native and hardware test plans |
| [10a_test_native.md](10a_test_native.md) | Native tests with invariants and reducer transition coverage |
| [10b_test_hardware.md](10b_test_hardware.md) | Hardware tests, fault injection, soak criteria, release gates |
| [review_definition_of_ready.md](review_definition_of_ready.md) | Preconditions for running a closure review |
| [review_closure_checklist.md](review_closure_checklist.md) | Binary checklist for determining plan completeness |
| [review_output_contract.md](review_output_contract.md) | Required output schema and evidence rules for reviews |
| [review_prompt.md](review_prompt.md) | Copy-paste prompt for convergent plan reviews |
