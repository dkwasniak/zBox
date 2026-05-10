# Resume Implementation Prompt

Copy the prompt below when resuming interrupted refactor work in a new session.

---

## Prompt

```text
Resume implementation of the refactor plan from `docs/refactor/PROGRESS.md`.

Before doing anything else:
1. Read `docs/refactor/PROGRESS.md`
2. Read the current stage checklist in `docs/refactor/09_migration_stages.md`
3. Read only the stage-relevant design docs referenced by `PROGRESS.md`

Rules:
- Treat `docs/refactor/PROGRESS.md` and `docs/refactor/09_migration_stages.md` as the source of truth.
- Do not re-do completed work.
- Do not broaden scope beyond the active stage.
- Do not reopen broad review of the plan. The plan is already implementation-ready.
- If you discover a contradiction, update `PROGRESS.md` with the blocker before stopping.

At the start of your response:
- state the current stage
- summarize what is already done
- name the exact next task from `PROGRESS.md`

During execution:
- update `PROGRESS.md` as work progresses
- update checklist items in `09_migration_stages.md` when verified
- run the stage-relevant tests before stopping

At the end of your response:
- summarize completed work
- report latest test results
- report the next exact resume point
```
