# Stage 0 Implementation Prompt

Copy the prompt below to the implementing agent when starting the refactor.

---

## Prompt

```text
Implement Stage 0 of the refactor plan in this repository.

Before writing code, read these files:
- `docs/refactor/PROGRESS.md`
- `docs/refactor/state_machine_refactor.md`
- `docs/refactor/09_migration_stages.md`
- `docs/refactor/01_appstate.md`
- `docs/refactor/02_events_effects.md`
- `docs/refactor/03_reducer.md`
- `docs/refactor/07_led_model.md`
- `docs/refactor/10a_test_native.md`

You must follow the refactor plan exactly. The plan review is closed. Do not reopen architecture review unless you hit a concrete contradiction in the plan or code.

Implementation scope:
- Stage 0 only
- No Stage 1 work
- No runtime ownership changes
- No hardware behavior changes

Stage 0 deliverables:
- `esp32/src/app_state.h`
- `esp32/src/events.h`
- `esp32/src/effects.h`
- `esp32/src/reducer.h`
- `esp32/src/reducer.cpp`
- `esp32/src/led_scene.h`
- `esp32/src/led_scene.cpp`
- `esp32/src/musicbox_assert.h`
- `esp32/test/test_reducer/` native test suite scaffold

Execution rules:
- Read `docs/refactor/PROGRESS.md` first and update it before stopping.
- Mark completed checklist items in `docs/refactor/09_migration_stages.md`.
- Stay within Stage 0 acceptance criteria only.
- Do not start Stage 1 even if Stage 0 feels easy.
- If you find a contradiction, document the exact file/section and stop after updating `PROGRESS.md`.

Verification rules:
- Run `pio test -e native` after each file creation or meaningful checkpoint.
- Keep reducer code pure: no Arduino includes, no FreeRTOS includes, no hardware handles.
- Preserve `static_assert(sizeof(AppState) <= 128)`.

Output expectations:
- Make the code changes directly.
- Keep `docs/refactor/PROGRESS.md` current.
- At the end, report:
  1. Stage 0 checklist items completed
  2. Files created/modified
  3. Latest native test result
  4. Any blocker preventing Stage 0 completion
```
