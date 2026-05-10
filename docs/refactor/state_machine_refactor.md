# ESP32 Firmware Refactor: Reducer + Dispatcher + Driver Feedback Architecture

> **This document has been superseded by the split documentation below.**
> The original single-file version (758 lines) has been replaced with focused plan files
> plus a review toolkit that address the gaps identified in review. See the index below.

---

## Document Index

| File | Contents | Key fixes |
|------|----------|-----------|
| [00_overview.md](00_overview.md) | Goals, locked decisions, safety constraints, behavior preservation matrix | Behavior matrix expanded; wake timing corrected (800ms/1600ms) |
| [01_appstate.md](01_appstate.md) | AppState design, sizeof analysis, deadline convention, NFC UID sizing, enum invariants | L1 sizeof, L4 deadlines, L8 None/Unknown, L12 uid[24], L13 bt_volume_applied |
| [02_events_effects.md](02_events_effects.md) | Event hierarchy with drop policy, Effect types, budget analysis, CmdId mechanism | L2 budget, L5 cmd_id |
| [03_reducer.md](03_reducer.md) | Reducer contract, ReduceResult, EffectBuilder, assert safety; CmdId filling approach committed | L14 assert in sleep path |
| [04_audio_adapter.md](04_audio_adapter.md) | Audio adapter contract, supersession, timeout ownership, queue-full rule | L5 cmd_id full spec |
| [05_bt_nfc_adapters.md](05_bt_nfc_adapters.md) | BT, NFC, and persistence adapter contracts | BT callback context, shutdown ordering |
| [06_sleep_wake.md](06_sleep_wake.md) | Normal, emergency, night-light sleep sequences; wake protocol | L6 emergency sleep ordering |
| [07_led_model.md](07_led_model.md) | LED scene derivation, beat globals contract, coalescing | L3 beat globals sync, L15 coalescing |
| [08_dispatcher_runtime.md](08_dispatcher_runtime.md) | Dispatcher task, watchdog, timers, event queue policy, observability | L7 watchdog |
| [09_migration_stages.md](09_migration_stages.md) | 6 stages with acceptance criteria; sync_mode deferred decision; diagnostic_mode integration | L10 verifiable gates |
| [10a_test_native.md](10a_test_native.md) | Native reducer tests, button decoder tests, clock control pattern, mock fidelity contract | L9 invariants |
| [10b_test_hardware.md](10b_test_hardware.md) | Hardware integration tests (22 scenarios), fault injection, soak criteria, release gates | L11 soak metrics |
| [11_button_adapter.md](11_button_adapter.md) | Button ISR bridge, debounce, click decoder, combo detection, Stage 5 integration | ISR→event bridge specification |
| [review_definition_of_ready.md](review_definition_of_ready.md) | Preconditions for plan closure review | Prevents open-ended critique loops |
| [review_closure_checklist.md](review_closure_checklist.md) | Binary closure checklist for plan review | Forces PASS/FAIL review boundary |
| [review_output_contract.md](review_output_contract.md) | Required evidence and severity contract for findings | Prevents low-signal review output |
| [review_prompt.md](review_prompt.md) | Copy-paste closure review prompt + targeted follow-ups | Makes the review loop converge |

---

## Agent Instructions: Progress Tracking

When an agent implements this refactor, it **must** maintain a progress file at
`docs/refactor/PROGRESS.md`. This allows work to be interrupted and resumed without
losing context.

### PROGRESS.md format

```markdown
# Refactor Progress

Last updated: <ISO date + time>
Current stage: Stage N — <stage name>
Status: in_progress | blocked | completed

## Stages

| Stage | Status | Notes |
|-------|--------|-------|
| Stage 0: Types, contracts, pure tests | ✅ done / 🔄 in_progress / ⬜ todo | ... |
| Stage 1: Observability and adapters   | ... | ... |
| Stage 2: BT and NFC source migration  | ... | ... |
| Stage 3: Audio command/ack migration  | ... | ... |
| Stage 4: Sleep/wake migration         | ... | ... |
| Stage 5: Buttons, deadlines, persist  | ... | ... |
| Stage 6: Cleanup                      | ... | ... |

## Current Stage Detail

### What is done
- <bullet per completed item>

### What is in progress
- <bullet for the current in-flight task>

### What is next
- <bullet for the next item after resuming>

### Blockers
- <anything that prevents forward progress — empty if none>

## Files Created / Modified

| File | Status |
|------|--------|
| esp32/src/app_state.h | created |
| esp32/src/reducer.cpp | in_progress |
| ... | ... |

## Test Results (latest run)

`pio test -e native`: X/Y passing
`pio run -e lolin_d32_pro`: OK / FAILED
Last hardware checklist stage: Stage N (date)
```

### Rules for the implementing agent

1. **Update PROGRESS.md before stopping**, even for an unexpected interruption.
   The "What is in progress" field must describe the exact state of the current task,
   not just the stage name.

2. **Read PROGRESS.md at the start of every session.** Before writing any code, check
   the current stage, what is done, and what is in progress. Do not re-do completed work.

3. **Mark checklist items in `09_migration_stages.md` as the work proceeds.**
   Change `[ ]` to `[x]` in the stage acceptance checklists as items are verified.
   This makes PROGRESS.md and `09_migration_stages.md` the dual source of truth.

4. **Never leave a stage half-migrated.** If interrupted mid-stage, the "What is in
   progress" field must describe exactly which acceptance criterion is being worked on,
   so the next agent session can resume at the right point without re-reading all code.

5. **Run `pio test -e native` after every file creation.** Record the result in
   PROGRESS.md. If tests regress, do not proceed to the next file.

6. **One stage at a time.** Do not start Stage N+1 until all acceptance criteria for
   Stage N are checked and PROGRESS.md reflects `✅ done` for that stage.
