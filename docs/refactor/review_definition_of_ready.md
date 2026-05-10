# Review Definition of Ready

Use this document before asking an agent for another review of the refactor plan.
If these preconditions are not met, the review will drift into open-ended critique and
keep producing low-signal findings.

---

## Purpose

The goal of review is not "find anything you can improve."
The goal is "determine whether the plan is complete enough to start implementation with
known risk, explicit tradeoffs, and measurable acceptance gates."

This DoR is the entry gate for a **closure review**.
If any required item below is missing, fix the plan first and only then run the review.

---

## Required Inputs

All of the following must exist and be current:

- The plan entrypoint: `state_machine_refactor.md`
- The split stage docs referenced by the entrypoint
- `00_overview.md` with a behavior preservation matrix
- `09_migration_stages.md` with stage-level acceptance criteria
- `10a_test_native.md` and `10b_test_hardware.md` with explicit test mapping
- This review toolkit:
  - `review_definition_of_ready.md`
  - `review_closure_checklist.md`
  - `review_output_contract.md`
  - `review_prompt.md`

---

## Preconditions

All items below must be true before review starts.

### Scope

- The target architecture is locked.
- The plan states explicit non-goals.
- Intentional behavior changes are listed explicitly.
- Open design alternatives are either resolved or marked as deferred decisions.

### Coverage

- Every user-visible behavior appears in the behavior preservation matrix.
- Every safety-critical protocol has an owning document section.
- Every migration stage has explicit ownership boundaries.
- Every stage has acceptance criteria that can be checked without interpretation.

### Testability

- Each major behavior has at least one verification path:
  native test, hardware test, or manual stage checklist.
- Known fault paths have explicit validation:
  queue full, timeout, rejection, missing asset, disconnected BT, sleep/wake edge cases.
- Soak criteria and release gates are written as pass/fail thresholds.

### Operability

- The implementing agent has a progress-tracking protocol (`PROGRESS.md`).
- Stage completion is observable from plan artifacts, not only from code.
- Rollback or safe-stop conditions are defined at stage boundaries.

---

## Stop Conditions

Do not ask for another broad review if any of the following is true:

- The previous review produced only `editorial` findings.
- The previous two reviews produced no new `blocker` findings.
- The same issue reappears because the output contract was not enforced.
- The reviewer is asked to "look for anything else" without a checklist boundary.

At that point, switch to one of these targeted modes:

- `coverage review` — find current-code behaviors not represented in the plan
- `verification review` — find plan claims with no measurable test or acceptance gate
- `implementation-readiness review` — find stage blockers that prevent coding from starting

---

## Exit Rule

The plan is ready for implementation review only when:

- all checklist sections in `review_closure_checklist.md` are `PASS` or `PASS WITH DEFERRED DECISION`,
- there are no unresolved `blocker` findings,
- every `important_before_implementation` finding has either:
  a plan patch,
  an explicit deferred-decision owner,
  or a written risk acceptance note.

Perfection is not required.
Explicit control of remaining risk is required.
