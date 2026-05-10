# Review Output Contract

Every plan review must follow this output contract.
If the reviewer cannot support a finding within this contract, the finding should not be
reported.

---

## Allowed Finding Classes

- `blocker`
  The plan is not safe to implement until this is fixed.

- `important_before_implementation`
  The plan can continue to be refined, but coding should not start in the affected area
  until this is resolved.

- `nice_to_have`
  Improves confidence or readability, but does not block implementation.

- `editorial`
  Wording, formatting, navigation, duplicate phrasing, or minor clarity issue.

---

## Evidence Rule

Each finding must include all of the following:

- `checklist_item`
  The exact checklist section that failed.

- `severity`
  One of the four allowed classes above.

- `location`
  Exact file and section heading.

- `evidence`
  Quote or paraphrase of the missing, contradictory, or ambiguous fact.

- `why_it_matters`
  A short implementation or verification risk statement.

- `minimal_fix`
  The smallest document change that would close the finding.

Findings without document-local evidence are invalid.
General suggestions without a failed checklist item are invalid.

---

## Required Response Shape

The response must have exactly these sections:

### 1. Decision

One of:

- `READY FOR IMPLEMENTATION`
- `READY WITH DEFERRED DECISIONS`
- `NOT READY`

### 2. Checklist Results

One line per section from `review_closure_checklist.md`:

```text
1. Scope and Intent — PASS
2. Behavior Preservation — FAIL
...
```

### 3. Findings

Only findings backed by the contract above.
If there are none, write exactly:

```text
No findings.
```

### 4. Deferred Decisions

List only unresolved items that are acceptable to defer.
If none:

```text
None.
```

### 5. Stop or Continue

Choose exactly one:

- `STOP REVIEW LOOP`
- `RUN ONE MORE TARGETED REVIEW`

`STOP REVIEW LOOP` is required when:

- there are no `blocker` findings, and
- no checklist section is `FAIL`, and
- remaining findings are only `nice_to_have` or `editorial`.

---

## Anti-Drift Rules

The reviewer must not:

- invent new acceptance criteria not implied by the checklist,
- propose alternative architectures,
- ask for "more detail" without naming the exact missing decision,
- repeat a previously closed finding unless the current document regressed,
- convert preference into severity.
