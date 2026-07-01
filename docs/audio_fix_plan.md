# Plan: Fix NS audio stutter via PCM elasticity buffer + drain task

## Context

The local-speaker (NS / I2S) path stutters ("efekt jak cofnięta płyta"), most visibly on
`b8bb84fa_W_ukladzie_slonecznym.mp3`. The hypothesis in `docs/audio-stutter-fix.md` was that the
**file encoding** is at fault (and that a 96 KB PCM buffer + drain task already exists).
Investigation disproved both.

### What the investigation found (evidence)

Downloaded the real files from the running server (`GET /api/stream/file/<name>`) and analysed
them with ffprobe + custom frame parsers:

| File | Bitrate | Stutters? | Notes |
|------|---------|-----------|-------|
| `b8bb84fa_W_ukladzie_slonecznym.mp3` | 64k CBR | **YES** | clean |
| `9383471d_babajaga.mp3` | 64k CBR | NO (doc was wrong) | clean |
| `9625e346_Mucha_w_Mucholocie.mp3` | ~192k VBR | NO | clean |

- **The stuttering file is not defectively encoded.** Standard 44.1 kHz / joint‑stereo (MS) /
  64 kbps CBR MP3, **structurally identical to the non‑stuttering `babajaga`**: same 44‑byte
  ID3v2, no embedded art, no inter‑frame gaps, no trailing junk, **0 ffmpeg decode errors**, and
  it uses the bit reservoir *less* than babajaga. Bitrate, stereo mode and reservoir depth are
  ruled out.
- **The doc's firmware premise is stale.** On branch `modul_ns` there is **no PCM buffer and no
  drain task** for NS. The NS path is fully synchronous: `SD read → Helix decode →
  BeatTracker (beat+volume) → i2s.write` in one loop (`esp32/src/audio/audio.cpp:642-725`).

### Most probable root cause (to be CONFIRMED on device, not yet proven)

Firmware buffering fragility, file‑independent. I2S DMA is left at the audio‑tools default
**6 × 512 = 3072 B ≈ 17 ms** (`ensureLocalTransport()` only sets pins, never
`buffer_count`/`buffer_size`, `audio.cpp:412-434`). I2S writes block
(`ticks_to_wait_write = portMAX_DELAY`), so nothing decouples a stall from the DAC. SD reads
already spike to **21–26 ms (> 17 ms buffer)**, the audio task shares **core 1 with NFC
SoftSPI** (sched_gap), and every transition blocks on a synchronous 40 ms `writeSilenceGap`.
Any gap > ~17 ms between `i2s.write()` calls underruns the DAC → stutter. Which track shows it
"most" is likely SD sector layout/fragmentation, which is why re‑copying a file can *appear* to
fix it.

> This is a strong hypothesis but **must be confirmed with on‑device telemetry** (Stage 0 +
> Stage 1) before we accept it as the root cause.

### Library‑usage issues found

- `mp3Decoder.addNotifyAudioChange(audioInfoLogger)` (`audio.cpp:790`) only **logs**; never
  calls `i2s.setAudioInfo()`. I2S is hardcoded 44.1 kHz/2/16 — fine for current files, wrong
  pitch for any other rate.
- I2S DMA depth never configured (too shallow).
- Per‑chunk `vTaskDelay(pdMS_TO_TICKS(1))` (`audio.cpp:724`); idiomatic audio‑tools pacing is
  sink back‑pressure, not a fixed sleep.

### Goal

Decouple SD/decode from the DAC with a PCM elasticity ring buffer drained by a dedicated task,
so SD‑read spikes and core‑1 jitter no longer underrun I2S — **delivered in small, verifiable
stages**, each independently testable, so we can attribute every audio change. Also (later,
separately) forward decoder AudioInfo to I2S, and restore LEDs. **Must avoid the previous
`tx_chan != nullptr` panic** (race between drain and I2S reconfiguration).

All work is in `esp32/src/audio/audio.cpp` (+ `esp32/src/zbox_config.h`). Implementer must load
`.claude/skills/firmware-dev/SKILL.md` and the `embedded-refactoring` skill first (RTOS tasks,
ISR/core pinning, timing‑sensitive code).

---

## Staged plan

Each stage is a separate commit, built + soak‑tested before the next. If a stage regresses
audio, we know exactly which change did it.

### Stage 0 — Baseline (no code change beyond logging)
Capture the "before" so the fix is measurable. With current firmware, record for the
problem path:
- `[AUDIO_TEL]` (SD B/s, dec_in, drops, max_read, max_decode_write, max_total, max_sched_gap,
  slow_* counters),
- `[DIAG] HWM` (loop/led/audio/nfc) and heap / largest block,
- exact track path + audible symptom (timestamped),
- repro recipe (which file, fast‑switch or steady playback).

Deliverable: a short "baseline" note appended here with the actual numbers for
`b8bb84fa_W_ukladzie_slonecznym.mp3`. Do not move on until we have it.

### Stage 1 — Deepen I2S DMA + extend telemetry (smallest fix first)
- In `ensureLocalTransport` set `cfg.buffer_count` / `cfg.buffer_size` explicitly (e.g.
  `buffer_count 16–24`, `buffer_size 512` → ~45–70 ms) before `i2s.begin(cfg)`.
- **Do not introduce a `[PCM]` line here** — there is no ring yet, so `[PCM] used/min/max`
  would be fake telemetry. Instead just **extend `[AUDIO_TEL]`** (`audio.cpp:697-719`) with the
  DMA depth / any extra sched_gap/read maxima needed. The genuine `[PCM] used/min/max/underruns`
  line arrives in Stage 2 with the real ring.
- Re‑test the problem path. **This alone may materially reduce or eliminate the stutter** and, if
  so, confirms the buffering hypothesis cheaply. Keep this commit even if Stage 2 follows — it is
  defence in depth.

### Stage 2 — Local PCM ring + drain task (the core decoupling, NO AudioInfo reconfig)
This is the heart of the fix. I2S stays at fixed 44.1 kHz/2/16 (current behaviour); **no
`setAudioInfo()` yet** — that is Stage 3.

All of Stage 2 lives behind a compile‑time flag **`ENABLE_LOCAL_PCM_DRAIN`** (in
`zbox_config.h`, see Feature flag §). When off → the current synchronous path; when on → the
ring + drain path. This single commit must include the **minimal lifecycle safety** (2f), so the
drain task is never flashed without BT guard + sleep park. Stage 4 only *extends* hardening/tests.

**2a. `LocalPcmBufferSink` (PSRAM ring).** Model on existing `BtPcmBufferSink`
(`audio.cpp:209-331`): `portMUX`‑guarded ring (multi‑core safe), `heap_caps_malloc(SPIRAM)` with
internal fallback, `write()` that blocks via `vTaskDelay(1)` until room (feed self‑paces on
ring‑full back‑pressure), `clear()`, `readSome()`, `usedBytes()`.
- **`write()` must never block forever when the drain is parked.** During normal NS playback,
  blocking until room is correct. But on stop / BT switch / sleep / reset the drain stops
  consuming, so a full ring would hang the audio task inside `decoderStream.write()`. So `write()`
  exits early (short timeout, drop remaining) whenever `drainStop`, `activeSinkIsBt`,
  `reconfiguring`, or a `sinkDisabled` flag is set. The mode transitions (2d, 2f, Stage 4) must
  **set `sinkDisabled`/pause the drain BEFORE clearing the ring**, so the audio task is released
  from `write()` first and can't get stuck during the switch. (Mirror `BtPcmBufferSink`'s
  existing `BT_PCM_WRITE_TIMEOUT_MS` bounded‑write pattern, `audio.cpp:232-248`.)
- Size constant in `zbox_config.h`. **Precise numbers** (176400 B/s for 44.1/2/16):
  `LOCAL_PCM_BUFFER_SIZE = 48 KB ≈ 272 ms`. Start the **prefill smaller** to limit latency:
  `LOCAL_PCM_PREFILL = 16 KB ≈ 91 ms` (not 50%). Note: prefill is the **start threshold only**,
  not a runtime floor — `used` will normally dip below it during steady playback. Tune both
  against measured underruns; raise only if Stage‑0/Stage‑1 spikes demand it.

**2b. Drain task.** `xTaskCreatePinnedToCore(localDrainTask, "pcm_drain", 4096, …, prio 3,
core 0)` — core 0 (with the I2S DMA ISR), off core‑1/NFC. State machine:
- IDLE (no NS playback, ring empty, **or `activeSinkIsBt`**, **or `drainStop`**) → short
  `vTaskDelay`, no I2S writes, set `parked=true`.
- PREFILL (track started) → wait until `used ≥ LOCAL_PCM_PREFILL`, then STREAM.
- STREAM → read a chunk (512–1024 B) from ring, scale volume + run beat detection (2e), write to
  I2S (mutex + lock‑ordering discipline §).
- Mid‑track empty ring → underrun: write one small silence frame to keep the DAC clocked, bump
  `pcmUnderruns`.
- Report drain‑task HWM, state, and `pcmUnderruns` + ring `used` min/max/current in the `[PCM]`
  line.

**2c. Sink rewire.**
- Local sink becomes `&localPcmSink` instead of `&i2s` everywhere `selectSink(&i2s, false)` is
  used (`audio.cpp:871,881`, init `788`).
- `BeatTracker` stops doing volume scaling for the NS path (see Volume path §).
- Remove the NS‑branch `vTaskDelay(1)` (`audio.cpp:724`) → `taskYIELD()`; pacing now comes from
  `LocalPcmBufferSink::write` back‑pressure.

**2d. Transition/reset — explicit, with mandatory double clear.** The old worry (doc suspect #1)
is that `decoderStream.end()` flushes a PCM tail *after* the first clear. So the clear after
`end()` is **mandatory, not optional**. The exact NS sequence in
`resetDecoderForNewTrack`/`flushStoppedPlayback` (`audio.cpp:392-410`), replacing the synchronous
`writeSilenceGap` I2S flush:
```
set sinkDisabled + pause drain  // releases audio task from write(), stops drain touching ring/I2S
localPcmSink.clear()
decoderStream.end()
localPcmSink.clear()            // MANDATORY: drop tail emitted by end()
decoderStream.begin()
rearm prefill                   // drain re-enters PREFILL
clear sinkDisabled + resume drain   // streams once new PCM passes prefill
```
Order matters: `sinkDisabled`/pause is set **before** the first `clear()` so the audio task
cannot be stuck in `LocalPcmBufferSink::write()` while the ring is being reset.
`clearActiveTransport()` (`audio.cpp:382-390`) also gains `localPcmSink.clear()` for the BT/stop
paths.

**2e. Beat/energy detection → drain task (part of THIS stage, not free).** Moving detection to
playback time also moves `millis()`, `g_audioEnergy`, `g_beatDetected` and the beat history onto
core 0/the drain task. Keep the `detectBeat` math (`audio.cpp:122-150`) identical, just
relocated to run on raw PCM in the drain loop. Verify the LED globals are accessed cross‑task the
same way they are today (they are already `volatile` and written from the audio task → either
confirm that is the existing accepted single‑writer pattern, now with the writer being the drain
task, or guard them). LED timing must be verified on device as part of Stage 2 (see Verification).

**2f. Minimal lifecycle safety (must ship in this commit).**
- BT guard: drain STREAM gated on `!activeSinkIsBt`; on local→BT clear ring + park, on BT→local
  resume (the full BT switch wiring is exercised in Stage 4, but the gate exists now).
- Sleep park: `audioDeleteTaskForSleep` signals `drainStop`, waits (bounded) for `parked`, then
  deletes both tasks. No `vTaskDelete` of the drain task while it may hold `i2sMutex` / be in
  `i2s.write`. (Bounded‑timeout + diagnostics detailed in Stage 4 / Teardown §.)

### Stage 3 — Safe AudioInfo forwarding (SEPARATE, because it caused the prior panic)
Only after Stage 2 is stable. `setAudioInfo` fires *inside* `decoderStream.write()` (audio‑task
context), concurrent with the drain task — this is exactly what produced `tx_chan != nullptr`.
- Replace the log‑only `AudioInfoLogger` so that on a SR/channel change it follows the
  **park‑then‑reconfigure** sequence (no nested locks, see Lock ordering §):
  ```
  set reconfiguring
  wait (bounded) for drain to report parked   // drain out of i2s.write
  localPcmSink.clear()                         // old-rate PCM, ring mux only, released here
  take i2sMutex                                // ring mux already released — no nesting
  i2s.setAudioInfo(info)
  release i2sMutex
  update cached rate
  rearm prefill
  clear reconfiguring                          // drain resumes
  ```
  The `i2sMutex` around `setAudioInfo` is **mandatory even though the drain is parked** — it is
  the invariant that all `i2s.*` calls hold the mutex; do not skip it just because the drain is
  paused. There is no nesting because the ring `clear()` has already acquired+released the ring
  mux before `i2sMutex` is taken.
- Gate so this is a no‑op when the new info equals the current (the common 44.1/2/16 case → no
  reconfig, no risk). Because this runs on the audio task while waiting on the drain task, ensure
  the bounded wait can't deadlock if the drain is blocked in `i2s.write` (same diagnostics as the
  teardown handshake).
- Ship with the explicit non‑44.1 kHz test (Verification). If it can't be made reliably safe,
  Stage 2 already fixes the stutter; Stage 3 can be deferred without losing the fix.

### Stage 4 — Lifecycle hardening + full test matrix
The minimal BT guard + sleep park already shipped in Stage 2f. Stage 4 makes them robust and
exercises every path.
- **BT switch wiring:** on local→BT (`audioStartBtHeadphonesMode`, `audio.cpp:858-866`) clear
  local ring + park drain; on BT→local (`audioStopBtHeadphonesMode`, `868-884`) resume. BT path
  (`btFrameSink`/`btPcmSink`/A2DP callback) stays untouched.
- **Sleep teardown** (`audioDeleteTaskForSleep`, `audio.cpp:840-845`): signal `drainStop` → wait
  for `parked` with a **bounded timeout**. Two outcomes:
  - *Parked within timeout (normal):* drain is provably out of `i2s.write`/`i2sMutex` → clean
    teardown: delete audio task + drain task, tear down I2S.
  - *Timeout expires (drain stuck, e.g. in `i2s.write` with the driver wedged):* we do **not**
    know the task is safe to delete, so **do not fake a clean teardown**. Log **CRIT** with drain
    state + drain HWM + heap/largest block, **do not `vTaskDelete` the drain task**, and fall
    back to the safest available path instead of a normal sleep — e.g. abort the sleep
    (controlled), or go straight to deep sleep / device restart that resets the I2S peripheral
    wholesale. Pick one explicitly during implementation; the invariant is: never delete a task
    that may hold `i2sMutex` / be blocked in `i2s.write(portMAX_DELAY)` (the deadlock/`tx_chan`
    trap), and never report success on a teardown that didn't actually quiesce.
- **Wake:** recreate tasks and (re)init I2S in the same order as today; drain starts IDLE until a
  PLAY arrives.

---

## Cross‑cutting design rules

### Feature flag / rollback (point 8)
Compile‑time `#define ENABLE_LOCAL_PCM_DRAIN` in `zbox_config.h`. Off → today's synchronous NS
path (BeatTracker → `&i2s` directly, no drain task, no ring). On → ring + drain path. Lets us
flash both and A/B them, and gives an instant emergency rollback without reverting code. Keep the
old path compiling until Stage 2 has soaked.
- **Default = `false`** for the first committed (compiling) Stage‑2 commit — `main`/the shared
  branch keeps the proven synchronous path. Flip to `true` only on the test branch / test flash
  while validating, and flip the default to `true` only after the full soak matrix passes.

### I2S mutex discipline + lock ordering (points 3, 4)
A single `i2sMutex` guards **only** `i2s.write()`, `i2s.setAudioInfo()`, `i2s.begin()`.
- Do **not** hold it during ring read or volume scaling — only wrap the actual I2S call.
- **No path ever holds the ring mux and the `i2sMutex` at the same time.** The two locks are
  never nested in either direction. Concretely:
  - Drain task: `readSome()` acquires + releases the ring `portMUX`, copying into a local buffer;
    **only after the ring mux is released** does it take `i2sMutex` to write. It must never block
    on `i2sMutex` while holding the ring mux.
  - Stage 3 reconfig does **not** nest either. It does not take `i2sMutex` and then touch the
    ring. Instead it **parks the drain task first** (sets `reconfiguring`/`drainPause`, waits for
    `parked`), and only then — with the drain task guaranteed out of both locks — clears the ring
    and calls `i2s.setAudioInfo()`, then rearms prefill and resumes the drain. So ring clear and
    `setAudioInfo` happen with no contending lock holder at all.
  - Any future path that needs both must follow the same rule: acquire/release one fully before
    the other; nesting ring mux ↔ `i2sMutex` (in any order) is forbidden.
- Because `i2s.write` uses `portMAX_DELAY`, holding `i2sMutex` across it could block
  `setAudioInfo`/sleep/teardown; mitigate by (a) keeping I2S write chunks small (≤1 KB so each
  blocked write is bounded by ~a few ms of DMA), and (b) the bounded parked‑flag handshake for
  teardown (Stage 4) rather than yanking the task.
- Log drain‑task HWM and a "parked" indicator.

### Shared lifecycle/state flags must be thread‑safe (point 2)
A new task on core 0 reads state mutated from other contexts (audio task, BT/sleep callers):
`activeSinkIsBt`, `drainStop`, `sinkDisabled`, `parked`, `reconfiguring`, drain state, and any
`s_isPlaying`‑style gating the drain consults. `volatile` is the **minimum**, not a full sync
model — choose the mechanism by role:
- **Handshake / sequencing state** (`drainStop`, `sinkDisabled`, `reconfiguring`, `parked`,
  drainState): these gate ordered teardown/reconfig sequences, so a torn read or reorder breaks
  the sequence. Use a **stronger primitive**, preferably one of:
  - a small dedicated `portMUX` taken for the snapshot/update of the lifecycle flag set, or
  - **FreeRTOS task notifications / an event group** to command the drain (stop/pause/reconfig)
    and to signal `parked` back — this gives a clean, race‑free handshake without busy‑polling a
    `volatile`.
- **Simple telemetry‑only flags** (where a one‑cycle race doesn't corrupt a sequence) may stay
  `volatile` single‑word with one writer.
- `activeSinkIsBt` must not stay a plain cross‑core `bool` read without a barrier — make it
  `volatile`/atomic or gate the sink behind the same critical section that switches it. One writer
  per flag; the drain task is read‑only on the ones it doesn't own and owns `parked` exclusively.
- Today's loose globals are acceptable only because they were single‑task; the drain task breaks
  that assumption, so audit every flag it touches.

### Volume path (point 6)
- **BT:** unchanged — keeps scaling as today.
- **NS:** ring holds **raw** decoded PCM; the **drain task** applies volume just before I2S,
  reading the existing `volatile int outputVolumePercent` (already updated by
  `audioSetOutputVolumePercent`, `audio.cpp:847-856`). No locks, no allocation (reuse a
  stack/static scratch like `BeatTracker::scratch_`). Result: near‑instant NS volume response.
- `BeatTracker::setVolumePercent` must no longer affect the NS sink output (it may keep the value
  only if beat detection needs it — current detection is energy‑based and volume‑independent, so
  it does not).

### Beat LED placement (point 5 — PRODUCT DECISION, default chosen, flag for review)
If beat detection stays in `BeatTracker` (decode time), LEDs lead the audio by ~the buffer
latency (≤ ~272 ms; ~91 ms once only prefill is buffered). For a music toy this *may* be
visible. **Default: move beat/energy detection into the drain task** (implemented in Stage 2e),
computed on raw PCM right before volume+I2S, so LEDs are sample‑aligned with sound. This is more
than a CPU cost — it relocates `millis()`/`g_audioEnergy`/`g_beatDetected`/history onto core 0
(see 2e for the cross‑task safety check). LED timing is verified on device in Stage 2. If the
drain‑task CPU cost turns out to matter, fall back to decode‑time detection and accept the lead.
Confirm this choice during review.

---

## Critical files
- `esp32/src/audio/audio.cpp` — `LocalPcmBufferSink`, drain task + state machine, `i2sMutex`,
  sink rewire, volume relocation, beat relocation, `clearActiveTransport`/reset changes, BT‑guard,
  sleep teardown handshake, `[PCM]` telemetry, (Stage 3) AudioInfo forwarding. Reuse:
  `BtPcmBufferSink` (209‑331), `BeatTracker` (66‑161), `ensureLocalTransport` (412‑434),
  `selectSink` (375‑380), reset (392‑410), feed loop (642‑725), `audioInit` (776‑795),
  `audioDeleteTaskForSleep` (840‑845), BT mode (858‑884), volume (847‑856).
- `esp32/src/zbox_config.h` — `LOCAL_PCM_BUFFER_SIZE` (48 KB), `LOCAL_PCM_PREFILL` (16 KB), I2S
  DMA depth (if defined here); restore `ENABLE_LEDS true`.

## Verification (per stage + full matrix)
Build/test gates each stage:
1. `pio run -e lolin_d32_pro` (build).
2. `pio test -e native` (31/31 must stay green — they cover the reducer, not audio).

On‑device matrix (Stage 1 onward; mandatory before declaring done):
3. Problem track `b8bb84fa…` end‑to‑end **and** fast next/previous for several minutes. Success
   criteria (prefill is a start threshold, **not** a runtime floor — `used` will normally dip
   below it during steady playback):
   - `pcmUnderruns == 0`,
   - ring `min_used > 0` during stable playback,
   - `used` only reaches 0 at stop / transition / sleep / BT switch,
   - log `used` min/max/current (don't require `used ≥ prefill`),
   - no audible stutter. A/B vs `babajaga` & `mucha`.
4. **pause / resume / stop** mid‑playback (no clicks, no stale tail, ring cleared on stop).
5. **BT on/off** both during playback and while idle; verify local drain parks under BT and
   resumes after, no I2S contention.
6. **sleep/wake** both while idle and right after playback; confirm clean drain‑task park (no
   deadlock, no `tx_chan` panic) and correct task/I2S re‑init order on wake.
7. **20–30 min soak** test on the problem track / playlist.
8. Throughout, log and check **audio‑task HWM + drain‑task HWM + heap/largest block** (no
   downward drift; stacks have margin).
9. (Stage 3 only) play a non‑44.1 kHz MP3 (or synthesise one) → correct pitch, clean
   `[AUDIO] Decoder: SR=…` → I2S reconfig with no panic.
10. Volume change mid‑track → near‑instant NS response; beat LEDs aligned per chosen placement.
11. Update `docs/audio-stutter-fix.md` to reflect the confirmed root cause and the implemented fix.

## Notes / risks
- HEAD warns "nie dotykać bo wybuchnie" — staged commits + per‑stage soak are the safeguard.
- The `i2sMutex` + parked‑flag handshake is the specific guard against the prior
  `tx_chan != nullptr` race; never call any `i2s.*` outside the mutex once the drain task exists,
  and never delete the drain task while it may be inside `i2s.write`.
- Backend encoding is **not** changed — it is not the cause. (Raising backend bitrate from 64k in
  `server/routers/admin.py` is unrelated quality, not a stutter fix.)
