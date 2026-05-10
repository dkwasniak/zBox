# Closure Review Prompt

Use this prompt for the next review pass.
It intentionally constrains the model so the review loop converges instead of generating
fresh open-ended critique forever.

---

## Prompt

```text
Review the refactor plan in `docs/refactor/state_machine_refactor.md`.

You must treat this as a closure review, not an open-ended brainstorming review.

Before reviewing, read and obey these files:
- `docs/refactor/review_definition_of_ready.md`
- `docs/refactor/review_closure_checklist.md`
- `docs/refactor/review_output_contract.md`

Review boundary:
- You may only report a finding if it fails a checklist item from `review_closure_checklist.md`.
- You may also report a finding if you can prove an internal contradiction between plan files.
- Do not suggest improvements, alternatives, or extra detail unless required to close a failed checklist item.
- Do not optimize for "find something new." Optimize for "determine whether implementation can safely begin."

Evidence rules:
- Every finding must include checklist item, severity, exact file/section location, evidence, why it matters, and a minimal fix.
- If you cannot support a finding with document-local evidence, do not report it.
- Do not repeat previously closed findings unless the plan regressed.

Severity classes:
- blocker
- important_before_implementation
- nice_to_have
- editorial

Required output format:

1. Decision
   One of:
   - READY FOR IMPLEMENTATION
   - READY WITH DEFERRED DECISIONS
   - NOT READY

2. Checklist Results
   Return PASS / PASS WITH DEFERRED DECISION / FAIL for every checklist section.

3. Findings
   Use this exact schema for each finding:
   - checklist_item:
   - severity:
   - location:
   - evidence:
   - why_it_matters:
   - minimal_fix:

   If there are no findings, write exactly:
   No findings.

4. Deferred Decisions
   List only items that are acceptable to defer without blocking Stage 0 or Stage 1.
   If none, write:
   None.

5. Stop or Continue
   Choose exactly one:
   - STOP REVIEW LOOP
   - RUN ONE MORE TARGETED REVIEW

Stop condition:
- If there are no blocker findings, no checklist section is FAIL, and remaining findings are only nice_to_have/editorial, choose STOP REVIEW LOOP.

Important:
- Do not perform a general critique.
- Do not invent new review dimensions beyond the checklist.
- Do not ask for another broad review pass.
```

---

## Suggested Follow-Up Prompts

Use these only if the closure review still returns specific failures.

### Coverage Review

```text
Compare the current firmware behavior in code against the refactor plan.
Find only behaviors that exist in code but are not represented in the plan documents.
Do not review style or architecture.
Return at most 10 findings, sorted by implementation risk.
```

### Verification Review

```text
Find only plan claims that do not have a measurable verification path in:
- `09_migration_stages.md`
- `10a_test_native.md`
- `10b_test_hardware.md`

For each finding, name the missing test or acceptance gate.
Do not report anything else.
```

### Consistency Review

```text
Check the `docs/refactor/` plan set only for internal consistency:
- broken cross-references
- old file names still referenced as canonical
- contradictory timing values
- contradictory ownership or stage boundaries

Do not report gaps unless they are caused by a contradiction.
```
