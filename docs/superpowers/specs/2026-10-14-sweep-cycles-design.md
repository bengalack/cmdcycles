# Cycle table with the phase-sweep method (`-n k`) — design

Date: 2026-10-14
Status: draft, pending spec review
Builds on: `2026-10-12-histogram-v2-design.md` (the `-g` sweep measurement)

## Goal

A second way to produce the existing CYCLES table (6 tests × 8 × 8 sizes):
the minimum duration of each command, found with the phase-sweep measurement
from `-g` instead of the old trial-and-error algorithm. It runs side by side
with the old method (selected with `-n`), so both can be compared on several
machines. The plan is to make it the default later and retire the old method.

## Why

Findings from the `-g` work (openMSX and real hardware):

- The old method's result depends on its own loop timing (aliasing): code
  changes of a few dozen cycles moved single cells by up to ±60 cycles.
- Its fall-through after line 190 produced too-low values for long commands
  (fixed in 0.83, but it shows how sensitive the method is).
- High precision needs very long runs (`-o 64 -i 12` and more).
- The sweep tries every start position within the line once per sweep, is
  exactly reproducible in openMSX, and matched the (fixed) old method within
  ±2 cycles on real V9958/T976x hardware.

## Measurement

Same building blocks as `-g` (`histogram.s`): HR lock per frame, vernier,
sync check, free-running attempts with a calibrated period of `n * 228 + 1`,
STOP after each measurement, guard line, fixed command colour.

All new VDP I/O follows the spacing rule: at least 15 cycles between two
accesses to the VDP ports (see `vdpWriteReg` in `macros_constants.inc`).

For each cell (test, w, h) the minimum delay value `dMin` is found with a
binary search on the question "does at least one of the k * 228 attempts
succeed at delay d?" (`probe(d)`).

### Search

```
dInit  = initial delay from establishInitialDelays (coarse * 33 + LEEWAY)  ; already computed today
hi     = dInit                    ; must succeed
lo     = max(HIST_MIN_DELAY, dInit - LEEWAY - 2 * 33)   ; must fail
while !probe(hi): hi += LEEWAY                          ; widen if needed
while lo > HIST_MIN_DELAY and probe(lo): lo = max(HIST_MIN_DELAY, lo - 2 * 33)
if probe(lo) at HIST_MIN_DELAY: dMin = HIST_MIN_DELAY (floor reached)
while hi - lo > 1:
    mid = (lo + hi) / 2
    if probe(mid): hi = mid else lo = mid
dMin = hi
```

- About 7–8 probes per cell (range of ~200 cycles).
- `probe(d)` stops at the first success (see below). Only a "no" needs the
  full k * 228 attempts.
- Assumption: without noise, success is monotone in d (F(d) > 0 for all
  d ≥ dMin). With noise (real hardware, display on) a rare success can be
  missed, so dMin can be a cycle or two high; a higher k reduces that.

### Stop on first success

`hfRun` gets a constant-cost check: when the "stop on success" mask (memory,
like `uFhMask`) is set and the attempt succeeded, the loop exits through a new
third exit. The non-exit path stays the same length in every pass (the check
is part of the calibrated period). Calibration runs with the mask cleared.
`histMeasureDelay` returns as soon as the loop exits this way (successes = 1).

### Calibration once per cell

- For d ≥ 48, `dispatch_vdp_test` uses its dynamic loop, whose OUT→IN time is
  exactly d and whose overhead does not depend on d. So the attempt length is
  `L(d) = L(d0) + (d - d0)`.
- `histCalibrate` is run once per cell at `d0 = dInit` (with pad
  `HIST_PAD_MIN`). It returns `Lmod0 = L(d0) mod 228`. For any other d ≥ 48:
  `Lmod(d) = (Lmod0 + d - d0) mod 228` and
  `pad(d) = HIST_PAD_MIN + ((1 - Lmod(d)) mod 228)` — integer math in C, no
  extra frames.
- d < 48 uses the direct stubs (different code per d): calibrate per probe.
- Per cell, not per test: `setVDPCmdParamsNI_asm` takes a different branch
  for LINE when w < h, so the setup length differs between sizes.

### Start-line rotation

Removed again. The test (`g1r` = `g1o`) showed no effect, and it costs lines
per frame.

## Command line and help (`main.c`)

- `-n k`: k required, 1..`HIST_MAX_SWEEPS` (287). Sets `g_bSweepMode`,
  `g_nSweeps` (shared with `-g`).
  - Missing: `ERROR:Missing value for -n`
  - Out of range: `ERROR:k for -n must be in range 1-287`
- Not allowed together with `-n`: `-g` (`ERROR:-g can't be used with -n`),
  `-t` (`ERROR:-t can't be used with -n`), `-i` / `-o`
  (`ERROR:-i/-o can't be used with -n`; they only control the old method).
  Checked after the argument loop.
- `-1`, `-2`, `-3` and the sys name work as usual.

Main help: ≤ 29 columns, and **23 lines** (the line limit is raised from 22
to 23 for this phase; it goes back down when the old method's options are
removed). The description stays as it is; one option line is added.

```
Usage:cmdcycle [opt] [sys]

Measure the duration of VDP
command in MSX Z80 cycles
(outside VBLANK area).
Format is markdown, written
to stdout (use redirection)

Version: %s
Wait: %s

Options (opt):
 -h Show this help message
 -o n Outer loops (%d)
 -i n Inner loops (%d)
 -1 Mode: normal (default)
 -2 Mode: no sprites
 -3 Mode: no screen
 -t VBLANK timing
 -g x y k Histogram (-h -g)
 -n k Sweep method (-h -n)

sys: Show sys name in report
```

(Lines: 1 usage, 1 blank, 5 description, 1 blank, 2 version/wait, 1 blank,
1 "Options", 9 options, 1 blank, 1 sys = 23.)

`-h -n` (new `printHelpSweep`, ≤ 29 columns):

```
Usage:cmdcycle -n k [sys]

Cycle table measured with
the sweep method: every
start position in the line
is tried (k sweeps of 228).

k: 1..%d

-1 -2 -3 apply as usual.
-i -o -t -g not supported.
```

## C orchestration (`main.c`)

- `runTests()`: `establishInitialDelays()` as today (all sizes), then
  `g_bSweepMode ? runSweepTest() : runTest()`, then `runHistTest()` for `-g`
  as today.
- New `runSweepTest()`: `histInit`, fixed colour, then for each test, size:
  the search above, result `dMin + nAdd` into `g_anResultDelays[c][u][v]`
  (same table as the old method, so `printReportBody` prints it unchanged).
- `HistCombo` is reused. New field: `bStopOnSuccess` (offset 12, replacing
  `uRotLines`).
- `histCalibrate` returns (or stores) `Lmod` so the pad can be derived for
  other d. New helper `sweepPad(Lmod0, d0, d)`.
- New helper `probe(p, d)` = guard line + pad (calibrate when d < 48) +
  `histMeasureDelay` with stop-on-success; returns bool. `HIST_LOCK_FAILED`
  → ERROR_CODE 6 and abort, as in `-g`.

## Report

- Header like normal runs, with these differences in `-n` mode:
  - line 2 ends with `(k:%u)` instead of `(-i:%d -o:%d)`;
  - sync line: `* Sync method: Line interrupts + HR vernier lock per frame`;
  - instead of the "No real histogram" line:
    `* Method: phase sweep, k:%u sweeps of 228 per probe`;
  - `* Sync misses: %u` only when > 0.
- Body: the `## CYCLES` tables exactly as today (incl. Sum / Grand total).
- The FAKE HISTOGRAM section is **not** printed in `-n` mode (there is no
  such data). `printReportBody` gets a parameter `bWithFakeHistogram`.

## Memory

- Expected +1 to 1.5 KB code (C search and helpers, small asm changes).
  The image was 0xD27C; the HIMEM check guards it at runtime.
- Each implementation task records `l__CODE`, `l__DATA`, `s__HEAP`.
- If it gets too tight: lower `MY_HEAP_SIZE` (the fake histogram heap is
  unused in `-n` mode), as discussed before.

## Run time (estimate)

- One full "no" probe: k * 228 attempts at about 11–22 attempts per frame,
  so about 10–20 frames at k=1.
- About 7–8 probes per cell, roughly half stop early: ~1–1.5 s per cell.
- 6 × 64 cells + one calibration per cell (~4 frames): about 8–12 minutes at
  k=1 (60 Hz). k multiplies only the "no" probes.

## Out of scope

- Replacing the old method as default (later, after comparisons).
- Noise refinement beyond k (e.g. re-checking d−1, d−2 with a higher k).
- Changing the `-g` report or measurement (apart from removing the rotation
  and adding the stop-on-success option, which `-g` leaves off).
- Moving `MY_HEAP` out of the image.

## Testing

- `cmdcycle -h` (23 lines, ≤ 29 columns), `cmdcycle -h -n`.
- `-n` (k missing), `-n 0`, `-n 288`, `-n 1 -g 8 8 1`, `-n 1 -t`, `-n 1 -i 8`
  → errors.
- openMSX (old version, same as the published numbers):
  `cmdcycle -n 1 > n1.md`
  - every cell ≤ the fixed old method (0.83) + 0, except where 0.83 missed
    positions (then lower); no cell lower than the `-g` minimum for the same
    size (spot-check 7x7, 8x8);
  - run twice: identical tables.
- Real hardware (FS-A1ST, Sanyo): `-n 1` and `-n 8`, compare with 0.83 and
  `-g` results; `-1`, `-2`, `-3`.
- Normal mode and `-g` still work as before.
