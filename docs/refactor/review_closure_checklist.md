# Review Closure Checklist

This checklist is the review boundary.
The reviewer may only raise a finding if it fails one of the checks below or proves a
contradiction against the current code or another plan document.

Output format is defined in `review_output_contract.md`.

---

## Reviewer Instruction

Evaluate the plan against this checklist only.
Do not suggest optimizations, style improvements, or alternative architectures unless they
close a failing checklist item.

For each section, return one of:

- `PASS`
- `PASS WITH DEFERRED DECISION`
- `FAIL`

Each `FAIL` must include a concrete document reference and a concrete missing or
contradictory fact.

---

## 1. Scope and Intent

- The plan states the target architecture clearly.
- The plan states non-goals or excluded concerns.
- Intentional behavior changes are explicitly listed.
- Deferred decisions are named and scoped.
- No stage silently broadens scope beyond the overview.

## 2. Behavior Preservation

- All current user-visible behaviors are enumerated.
- Wake/sleep timing is concrete and consistent across documents.
- Mode semantics are stable and unambiguous.
- LED behaviors are fully listed.
- No behavior appears in stage acceptance criteria without appearing in the behavior matrix.

## 3. State Model and Invariants

- Main enums and sentinel values have defined meaning.
- Deadlines have a documented time-base convention.
- State ownership is singular for each policy domain per stage.
- Memory and size constraints are documented where relevant.
- State invariants are testable and not merely descriptive.

## 4. Event and Effect Contracts

- Events have clear source ownership.
- Effects have clear executor ownership.
- Async commands define success, failure, timeout, and rejection paths.
- `cmd_id` correlation is defined where async feedback is required.
- Queue drop policy is explicit for non-critical and critical paths.

## 5. Safety-Critical Protocols

- Sleep entry ordering is concrete.
- Wake confirmation path is concrete.
- BT shutdown ordering is concrete.
- NFC shutdown or reinit ordering is concrete.
- Crash-sensitive paths define what happens on failure and timeout.

## 6. Staged Migration

- Each stage defines what changes and what does not.
- Each stage has explicit policy ownership.
- Dual-path or observe-only phases are temporary and bounded.
- Acceptance criteria are stage-local and measurable.
- No stage requires hidden prerequisites from a later stage.

## 7. Verification Mapping

- Every major behavior maps to a test or checklist.
- Native tests cover reducer invariants and core transitions.
- Hardware tests cover integration and timing-sensitive behavior.
- Fault injection covers the main failure modes.
- Release gates are binary, not narrative.

## 8. Operability and Execution

- `PROGRESS.md` usage rules are explicit.
- A stopped implementation can resume from plan artifacts alone.
- Stage completion can be audited from docs and tests.
- There is a clear rule for not starting Stage N+1 early.
- The plan states what evidence is required before cleanup removes old code.

## 9. Internal Consistency

- Cross-document links point to the current files.
- No moved or split document is referenced as if still canonical.
- Terms are used consistently across the document set.
- Acceptance criteria do not contradict locked decisions.
- Stage checklists do not contradict the test plan.

---

## Exit Decision

Recommend exactly one:

- `READY FOR IMPLEMENTATION`
- `READY WITH DEFERRED DECISIONS`
- `NOT READY`

`READY WITH DEFERRED DECISIONS` is allowed only if:

- there are no `blocker` findings,
- deferred items have owners,
- deferred items do not invalidate Stage 0 or Stage 1 work.
