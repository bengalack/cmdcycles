# True histogram (`-g x y`) — design

Date: 2026-10-11
Status: approved in conversation, pending spec review

## Goal

Add a real histogram mode. For one command size (x, y) and all `NUM_TESTS`
tests, count how many attempts **succeed** (VDP command reported done) at each
delay value, from the lowest delay that ever succeeds and upwards. The existing
"FAKE HISTOGRAM" (attempts-before-success) is unchanged.

## Terminology

- **Budget per delay value**: `g_uInnerIterations * g_uOuterIterations * 13`
  attempts. The wait index `iyl` runs 0..12 inclusive (13 rounds), so with
  defaults this is 64 * 3 * 13 = **2496**.
- **W**: the best delay found by the existing (fast, early-out) search.
- **Winner**: lowest delay value with at least one success in the histogram scan.
- **HIST_DISPLAY_LEN**: 32, max number of counts shown per test.
- **HIST_START_MARGIN**: 33, safety margin added to W for the histogram scan
  start, in case W was a lucky hit.

## 1. Command line and help (`main.c`)

### Parsing (`commandLineParameters`)

- `-g x y`: consumes two following arguments; error if missing.
  - `atoi`, range checks `[1, NUM_HORIZONTALS]` and `[1, NUM_VERTICALS]`.
  - Map value to index by searching `aHORZ_LEN` / `aVERT_LEN`.
  - Sets `g_bHistogramMode = true`, `g_uHistU`, `g_uHistV`, and narrows the
    size range globals (see section 3).
- `-g` may be combined with `-i` / `-o`. Success counts are u16, so after the
  argument loop, if `g_bHistogramMode` and
  `(u32)inner * outer * 13 > 65535` (inner 0 means 256) → print
  `ERROR:-g budget too large (-i*-o*13 > 65535)`, return 1.
  This means `inner * outer <= 5041`. The defaults (64 * 3) are well inside.
- `-h`: if the next argument is `-g`, call `printHelpHist()`, else `printHelp()`.
- `-1`, `-2`, `-3`, `-t` and sys name work together with `-g`.
- New globals initialised in `initVarsAndRig`.

### Main help (`g_szHelptext`)

One line added after ` -t VBLANK timing`. Result is exactly 22 lines, max 27 columns:

```
Usage: cmdcycle [opt][sys]

Measure the duration of VDP
command in MSX Z80 cycles
(outside VBLANK area).
Format is markdown, written
to stdout (use redirection)

Version: 0.83
Wait: Incremental

Options (opt):
 -h Show this help message
 -o n Outer loops (3)
 -i n Inner loops (64)
 -1 Mode: normal (default)
 -2 Mode: no sprites
 -3 Mode: no screen
 -t VBLANK timing
 -g x y Histogram (-h -g)

sys: Show sys name in report
```

### Histogram help (`g_szHelptextHist`, `printHelpHist()`)

Max 29 columns, no internals. `%d` = `NUM_HORIZONTALS` / `NUM_VERTICALS`:

```
Usage: cmdcycle -g x y [sys]

Histogram of the duration of
one command size (x*y) for
all tests.

x: 1..%d (width)
y: 1..%d (height)

-i -o -1 -2 -3 -t apply as
usual, but -i*-o max 5041.
```

## 2. Assembly routines (`bioshelper.s`)

### Interface

```c
extern u16 runTestComboHist(RunCombo* p, u16* panHistogramLastIndex);
extern u16 runTestComboHistNoHalts(RunCombo* p, u16* panHistogramLastIndex);
```

Same `RunCombo` struct and calling convention (HL, DE) as the existing routines.

- Returns (DE) the winner delay (raw, without T976x `nAdd`).
- Sets `pHistogramWinner` (7/8(ix)) to the winner's slot in the scratch histogram.
- If the start delay gets zero successes: returns 0, `pHistogramWinner` stays 0.
- Scratch histogram is filled backwards from `panHistogramLastIndex` (= start
  delay), one u16 slot per delay value. Each slot holds the **success count**.
  The CLI check guarantees the count is at most 65535 (2496 with defaults), so it fits u16.

### Register allocation (as in `_runTestComboNoHalts`)

| Reg   | Use                                                        |
|-------|------------------------------------------------------------|
| IX    | params struct (only read outside the per-attempt path)     |
| B     | inner counter                                              |
| C     | command                                                    |
| DE    | delay being attempted                                      |
| HL    | last delay with a success                                  |
| IYL   | wait index 0..12                                           |
| IYH   | line interrupt state                                       |
| HL'   | current histogram slot                                     |
| stack | outer counter                                              |

Reused: `secureActiveArea`, `sync_with_active_area`, `setVDPParams`, `testVDP`,
`doControlledWait`, `getNextPossibleDelay`, `updateHistogram`,
`histogram_advance_and_record`, `prepareLineInterruptsNI`,
`_cleanupLineInterruptsNI`, `setCustomISRAndPrepNI`,
`restoreISRAndSetS0NoLineInt_NI`.

### New macro `controlledWaitByMethod`

Contains the `METHOD_WAIT` `.ifeq` block (incl. `push hl` / `pop hl` around
`doControlledWait`). Used **only** by the two new routines. The existing
`_runTestCombo` / `_runTestComboNoHalts` are **not** changed.

### `_runTestComboHistNoHalts` (line interrupts — primary)

```
prologue as _runTestComboNoHalts (HL' = last index, IX = struct, di,
prepareLineInterruptsNI, C = cmd, DE = start delay)

hist_next_delay:                 ; one pass per delay value (DE)
    ld b,6(ix)                   ; outer
hist_outer:
    push bc
    ld iyl,#0
hist_round:                      ; one round per wait index 0..12
    ld b,3(ix)                   ; inner
    call secureActiveArea
    controlledWaitByMethod
hist_loopr:
    call setVDPParams
    call testVDP                 ; C = cmd, DE = delay -> A
    rr a
    jp z,hist_impossible         ; status 0: impossible delay -> pop bc, treat as 0 successes
    jp c,hist_busy
    updateHistogram              ; count SUCCESS only (33)
    jp hist_next_try             ; (11) -> success path = 44
hist_busy:
    ld a,i x4                    ; 4 * 11 = 44, equalise busy/success path lengths
hist_next_try:
    call secureActiveArea
    djnz hist_loopr
    ld a,#12 / cp iyl / jp z,hist_round_done
    inc iyl
    jp hist_round
hist_round_done:
    pop bc
    djnz hist_outer
    (HL') == 0 ? -> hist_done    ; full budget with no success: stop
    histogram_advance_and_record ; pHistogramWinner = HL', HL' -= 2
    ld h,d / ld l,e              ; HL = this delay
    call getNextPossibleDelay
    ld a,d / or e
    jp nz,hist_next_delay
hist_done:
    _cleanupLineInterruptsNI, ei, ex de,hl, pop ix, ret
```

HL starts at 0, so a start delay with zero successes returns 0.

Timing rules:
- `updateHistogram` runs after `in a,(0x99)`, so it is outside the measured window.
- The busy path is padded with 4 x `ld a,i` (44 cycles) to match the success
  path's `updateHistogram` + `jp` (33 + 11 = 44 cycles; 70 in 1 of 256
  increments, which we accept). `ld a,i` is used rather than the `delayNN`
  macros because those use `inc hl` and would destroy HL.
  This keeps the raster phase of the next attempt independent of the previous
  result.
- No subroutine calls or IX/IY use are added to the per-attempt path beyond
  what the existing routines already do.

### `_runTestComboHist` (VBLANK, `-t`)

Same structure, using `_runTestCombo`'s synchronisation:
- `halt` / `setCustomISRAndPrepNI` / `restoreISRAndSetS0NoLineInt_NI`
- `sync_with_active_area` at the start of each round
- `di` / `ei` around `setVDPParams` + `testVDP`
- after each attempt: `ld a,(bACTIVE_AREA) / or a / call z,sync_with_active_area`
  (replaces `secureActiveArea` in `hist_next_try`)

## 3. C orchestration (`main.c`)

### Size range globals

`g_uFirstU`, `g_uEndU`, `g_uFirstV`, `g_uEndV`. `initVarsAndRig` sets the full
range (0..`NUM_HORIZONTALS`, 0..`NUM_VERTICALS`). `-g` narrows each to the one
selected index (`-g 8 8` → u 7..8, v 7..8). `establishInitialDelays` and
`runTest` loop over `[first, end)` instead of `[0, NUM_x)`. Normal mode is
unchanged.

### Phase 1

Existing `runTest()` (only the loop bounds change). Produces W in
`g_anResultDelays[c][u][v]` (includes `nAdd`).

### Phase 2: new `runHistTest()`

```c
#define HIST_DISPLAY_LEN    16      // max counts shown per test in the report
#define HIST_START_MARGIN   33      // safety: histogram scan starts at W + this

// ---------------------------------------------------------------------------
// Phase 2 of -g mode: real histogram, starting a bit above the delay found by runTest
bool runHistTest(void)
{
    RunCombo oParams;

    oParams.uInnerIterations = g_uInnerIterations;
    oParams.uOuterIterations = g_uOuterIterations;
    oParams.uW = aHORZ_LEN[g_uHistU];
    oParams.uH = aVERT_LEN[g_uHistV];

    u16* panHistogramLastIndex = &g_anScratchHistogram[SCRATCH_HIST_SIZE-1];
    u16 nAdd = g_bHasT976xEngine ? 1 : 0;

    for(u8 c = 0; c < NUM_TESTS; c++)
    {
        oParams.uCmd             = aEXECUTE_CMD[c];
        oParams.nStartDelay      = g_anResultDelays[c][g_uHistU][g_uHistV] - nAdd + HIST_START_MARGIN; // raw W + margin
        oParams.pHistogramWinner = (u16*)0x0000;

        u16 nWinner;
        if(g_bVBLANKTiming)
            nWinner = runTestComboHist(&oParams, panHistogramLastIndex);
        else
            nWinner = runTestComboHistNoHalts(&oParams, panHistogramLastIndex);

        if(oParams.pHistogramWinner == (u16*)0x0000) // not even the start delay succeeded
        {
            g_aoHistogram[c][g_uHistU][g_uHistV].nHistogramLength = 0; // printed as "(?)"
            continue;                                                   // scratch is still all zeros
        }

        nWinner += nAdd;
        g_anResultDelays[c][g_uHistU][g_uHistV] = nWinner;

        if(!recordAndClearHistogram(&oParams, panHistogramLastIndex, nWinner, &g_aoHistogram[c][g_uHistU][g_uHistV], false))
            return false;
    }

    return true;
}
```

- Phase 2 overwrites the phase-1 `g_aoHistogram` entry for this size. The
  phase-1 copy is a few bytes left on `MY_HEAP`, which is acceptable.
- The scan covers at least 34 delay values (W + 33 down to W). If phase 2 finds
  successes below W, it keeps scanning down, so the true winner is still found.
- The scratch histogram (1024 slots) is far larger than any expected scan length.

### `recordAndClearHistogram`

New parameter `bool bTrimTrailingOnes`:
- `true` (from `runTest`): current behaviour (trim the streak of 1s).
- `false` (from `runHistTest`): length = `panHistogramLastIndex - pHistogramWinner + 1`.

Clearing logic unchanged.

### `runTests()`

```c
establishInitialDelays();
runTest();
if(g_bHistogramMode)
    runHistTest();
```

Screen/sprite condition handling around these calls is unchanged.

## 4. Report (`main.c`)

- `printReportTop(&oTimestamp0, g_bHistogramMode)` — existing `g_szTopLine4_2`
  ("True histogram") is printed in `-g` mode.
- Body: `g_bHistogramMode ? printReportBodyHist() : printReportBody()`.
- `printReportBodyHist()`:
  - `## HISTOGRAM` + empty line
  - per test: `g_szHistHeader1` (`aTEST_NAME[t]`), `g_szHistHeader2`, one row
    using `g_szHistResultLine` (`x`, `y`, `"(winner)"`), then the first
    `min(HIST_DISPLAY_LEN, nHistogramLength)` counts as `"%hu "`, then `\r\n`
    and an empty line.
  - `panHistogram[0]` = winner, `[1]` = winner + 1, and so on.
  - Length 0 → `(?)`, no counts.
  - No Sum / Grand total lines.
- Duration lines unchanged.
- `g_szVersion` → `"0.83"`.

Example (`cmdcycle -g 8 8`):

```
## HISTOGRAM

| Copy: LMMM-TEOR    |                                                       |
|--------------------|:------------------------------------------------------|
| 8x8 (213)          | 1 1 1 2 3 10 400 900 1200 4608 4608 4992 4992 4992 4992 4992 

| Copy: HMMM         |                                                       |
|--------------------|:------------------------------------------------------|
| 8x8 (1427)         | <16 numbers>
...
```

## Out of scope

- Changes to the existing `_runTestCombo` / `_runTestComboNoHalts` routines.
- Changes to the fake histogram output.
- Counts above 65535 (budget is rejected at the command line instead).

## Testing

- Build with `build.bat` (SDCC v4.2+) and run in openMSX (and on real HW if available):
  - `cmdcycle -h`: 22 lines, ≤ 29 columns, shows the `-g` line.
  - `cmdcycle -h -g`: histogram help.
  - `-g 0 1`, `-g 9 1`, `-g 1`, `-g 8 8 -i 256 -o 20` → errors.
  - `-g 8 8 -i 10 -o 2` and `-o 2 -g 8 8` → accepted; header shows `-i:10 -o:2` / `-o:2`.
  - `cmdcycle -g 8 8 > out.md`: 6 blocks. The winner per test is ≤ the 8x8
    CYCLES value from a normal run (± noise), counts increase towards 2496,
    and there are at most 32 counts per row.
  - Same with `-t`, and with `-1` / `-2` / `-3`.
  - A normal run (no `-g`) gives the same output format as before.
