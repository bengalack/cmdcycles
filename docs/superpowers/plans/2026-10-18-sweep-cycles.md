# Cycle Table with the Sweep Method (`-n k`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A new mode `-n k` that fills the existing CYCLES table (6 tests × 8 × 8 sizes) with the minimum command duration found by the phase-sweep measurement from `-g`, side by side with the old method.

**Architecture:** Per cell, a binary search in C over "does at least one of the k × 228 attempts at delay d succeed?" (`sweepProbe`). Each probe reuses the `-g` assembly (`histMeasureDelay` / `histMeasureDelayRelock`), extended with a constant-cost "stop at the first success" exit. The attempt period is calibrated once per cell; for d ≥ 48 the pad of any other d follows arithmetically. Drifting clocks (turbo R) use the existing relock-per-attempt path. The report prints the normal CYCLES table without the FAKE HISTOGRAM.

**Tech Stack:** C (SDCC 4.5, `__sdcccall(1)`), Z80 assembly (sdasz80), MSX-DOS, openMSX and real MSX hardware for runtime tests.

**Spec:** `docs/superpowers/specs/2026-10-14-sweep-cycles-design.md` (builds on `docs/superpowers/specs/2026-10-12-histogram-v2-design.md`).

## Global Constraints

- **Never commit or push** unless the user explicitly says so in that moment. Git identity is the user's (bengalack); no AI attribution lines, ever.
- Builds: from PowerShell `Set-Location C:\source\repos\cmdcycles; cmd /c "C:\source\repos\cmdcycles\build.bat"` (the user builds with `build.bat` in cmd). Expected: exit 0, no new warnings (the two existing ones, `function declarator with no prototype` and `conditional flow changed by optimizer`, may disappear when `printReportBody` gets a parameter).
- Help texts: at most 29 columns; main help **23 lines** (raised from 22 for this phase).
- `-n k`: k in 1..287 (`HIST_MAX_SWEEPS`). `-n` with `-g`, `-t`, `-i` or `-o` is an error (checked after the argument loop).
- VDP I/O rule: at least 15 cycles between two accesses to the VDP ports (see `vdpWriteReg` in `macros_constants.inc`).
- Timed assembly paths: every non-exit path must have constant length (the attempt loop is calibrated); no PUSH/POP in the attempt loop; `push ix`/`pop ix` around routines that use IX.
- Existing behaviour must not change: normal runs (old method, CYCLES + FAKE HISTOGRAM) and `-g` (counts must still sum to k × 228).
- Every task records `l__CODE`, `l__DATA`, `s__HEAP` from `out\cmdcycle.map` (baseline v0.93: `l__CODE` 0x4E3C, `l__DATA` 0x1C81, `s__HEAP` 0x6EBA).
- Version string stays as it is (the user bumps it).

## Review Focus

1. **`-g` regression through the new stop flag.** `-g` must run with "stop on success" **off**, or every histogram row collapses to counts of 0/1. Expected: `-g` rows still sum to k × 228. Test: Task 2, Step 5 (code check) and Task 4, Step 6 (runtime).
2. **Small cells (1x1 … 2x2), delay values below 48.** `dispatch_vdp_test` uses its direct stubs there, so the linear pad formula does not apply and each probe calibrates itself. Expected: no hang, plausible minima (e.g. LMMM 1x1 ≈ 15–16 on the FS-A1GT emulation), floor 20 never undercut. Test: Task 4, Step 6.
3. **Drifting clocks (turbo R).** Probes take the relock path and stop at the first success there too. Expected: header "lock per attempt", plausible table. Test: Task 4, Step 7.
4. **Search bounds.** If the initial delay (with `LEEWAY`) does not succeed, `hi` must grow; if the lower bound still succeeds, `lo` must drop (to the floor 20). Expected: results equal the `-g` minima for the same cell (spot check 8x8, 7x7). Test: Task 4, Step 6.
5. **Command-line combinations.** `-n` with `-g`, `-t`, `-i`, `-o` in any order gives the matching error; `-n` without k or out of range gives its error. Test: Task 1, Step 6.

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `src/main.c` | CLI, help, search, orchestration, report | `-n` parsing and help; `sweepPad`, `sweepProbe`, `sweepFindMin`, `runSweepTest`; `runTests` dispatch; header lines; `printReportBody(bool)` |
| `src/histogram.s` | attempt loop and measurement routines | "stop on success" exit in `hfRun`; mask set up in `_histMeasureDelay`, `_histMeasureDelayRelock`, `_histPhaseAfterRun`; relock loop stops after a success |

No test framework exists for the MSX target. Each task's gates: build; the help checker script in the scratchpad (`check_help.py`, from earlier plans); listing or `grep` checks; map sizes; human runtime checks in openMSX / on hardware (Task 4).

---

### Task 1: Command line and help

**Files:**
- Modify: `src/main.c` (globals, `initVarsAndRig`, `g_szHelptext`, new `g_szHelptextSweep`, new `printHelpSweep`, `commandLineParameters`)
- Test helper (scratchpad): `<scratchpad>/check_help.py`

**Interfaces:**
- Produces: `bool g_bSweepMode;` (set by `-n`), `void printHelpSweep(void);`. `g_nSweeps` (existing, shared with `-g`) holds k.

- [ ] **Step 1: Extend the help checker (failing test)**

In `<scratchpad>/check_help.py`:
- add `("g_szHelptextSweep", (287,)),` to `checks`;
- change `if len(lines) > 22:` to `if len(lines) > 23:` and its message `> 22` to `> 23`;
- before `print("PASS" if ok else "FAIL")` add:

```python
if " -n k Sweep method (-h -n)" not in get_c_string("g_szHelptext"):
    print("FAIL main help: no -n line"); ok = False
```

Run: `python <scratchpad>/check_help.py src/main.c`
Expected: exit 1, `FAIL g_szHelptextSweep: not found` and `FAIL main help: no -n line`.

- [ ] **Step 2: Help strings and printHelpSweep**

In `g_szHelptext`, after the line `" -g x y k Histogram (-h -g)\r\n"` add:

```c
                                                  " -n k Sweep method (-h -n)\r\n"
```

After the whole `g_szHelptextHist` definition add:

```c
                                                //"                             " // 29 chars (turbo r)
const u8                g_szHelptextSweep[]     = "Usage:cmdcycle -n k [sys]\r\n"
                                                  "\r\n"
                                                  "Cycle table measured with\r\n"
                                                  "the sweep method: every\r\n"
                                                  "start position in the line\r\n"
                                                  "is tried (k sweeps of 228).\r\n"
                                                  "\r\n"
                                                  "k: 1..%d\r\n"
                                                  "\r\n"
                                                  "-1 -2 -3 apply as usual.\r\n"
                                                  "-i -o -t -g not supported.";
```

After `printHelpHist` add:

```c
// ---------------------------------------------------------------------------
void printHelpSweep(void)
{
    sprintf(g_auBuffer, g_szHelptextSweep, HIST_MAX_SWEEPS);
    print(g_auBuffer);
}
```

- [ ] **Step 3: Global and init**

After `bool g_bHistogramMode; // -g x y` add:

```c
bool                    g_bSweepMode;           // -n k: cycle table with the sweep method
```

In `initVarsAndRig`, after `g_bHistogramMode    = false;` add:

```c
    g_bSweepMode        = false;
```

- [ ] **Step 4: Parsing**

In `commandLineParameters`:

At the top of the function body (before `if(argc > 0)`), add:

```c
    bool bInnerSet = false;             // -i / -o given (not allowed with -n)
    bool bOuterSet = false;
```

Replace the `-h` branch's body condition so that `-h -n` is handled:

```c
            if(strcmp(argv[n], "-h") == 0 || strcmp(argv[n], "--help") == 0)
            {
                if((n + 1 < argc) && (strcmp(argv[n+1], "-g") == 0))
                    printHelpHist();
                else if((n + 1 < argc) && (strcmp(argv[n+1], "-n") == 0))
                    printHelpSweep();
                else
                    printHelp();
                return 0;
            }
```

Add a branch before `else if(strcmp(argv[n], "-i") == 0)`:

```c
            else if(strcmp(argv[n], "-n") == 0)
            {
                if(n + 1 >= argc)
                {
                    print("ERROR:Missing value for -n");
                    return 1;
                }

                iVal = atoi(argv[++n]);
                if((iVal < 1) || (iVal > HIST_MAX_SWEEPS))
                {
                    sprintf(g_auBuffer, "ERROR:k for -n must be in range 1-%d", HIST_MAX_SWEEPS);
                    print(g_auBuffer);
                    return 1;
                }
                g_nSweeps    = (u16)iVal;
                g_bSweepMode = true;
            }
```

In the `-i` branch, after `g_uInnerIterations = (u8)iVal;` add `bInnerSet = true;`. In the `-o` branch, after `g_uOuterIterations = (u8)iVal;` add `bOuterSet = true;`.

After the existing `if(g_bHistogramMode && g_bVBLANKTiming) { ... }` block add:

```c
        if(g_bSweepMode) // the old method's options do not apply to -n
        {
            if(g_bHistogramMode)
            {
                print("ERROR:-g can't be used with -n");
                return 1;
            }
            if(g_bVBLANKTiming)
            {
                print("ERROR:-t can't be used with -n");
                return 1;
            }
            if(bInnerSet || bOuterSet)
            {
                print("ERROR:-i/-o can't be used with -n");
                return 1;
            }
        }
```

- [ ] **Step 5: Checker and build**

Run: `python <scratchpad>/check_help.py src/main.c`
Expected: `g_szHelptext: 23 lines`, `g_szHelptextHist: 12 lines`, `g_szHelptextSweep: 11 lines`, no `TOO WIDE`, `PASS`.

Run the build. Expected: exit 0. Record map sizes.

- [ ] **Step 6: Runtime check (human, openMSX or hardware)**

| Command | Expected |
|---|---|
| `cmdcycle -h` | 23 lines incl. ` -n k Sweep method (-h -n)` |
| `cmdcycle -h -n` | the 11-line sweep help |
| `cmdcycle -n` | `ERROR:Missing value for -n` |
| `cmdcycle -n 0` / `-n 288` | `ERROR:k for -n must be in range 1-287` |
| `cmdcycle -n 1 -g 8 8 1` | `ERROR:-g can't be used with -n` |
| `cmdcycle -t -n 1` | `ERROR:-t can't be used with -n` |
| `cmdcycle -n 1 -i 8` / `-o 2 -n 1` | `ERROR:-i/-o can't be used with -n` |

(Without Task 3, a valid `-n 1` runs the old method; that is expected at this point.)

---

### Task 2: "Stop at the first success" in the assembly

**Files:**
- Modify: `src/histogram.s` (`hfRun`, `_histPhaseAfterRun`, `_histMeasureDelay`, `_histMeasureDelayRelock`, `_DATA`)
- Modify: `src/main.c` (`HistCombo`, `runHistTest`)

**Interfaces:**
- Produces: `HistCombo.uStopOnSuccess` (u8, offset 12): `0xFF` = a measurement ends at the first success (returns successes = 1), `0` = all attempts. `histMeasureDelay` / `histMeasureDelayRelock` honour it; `histPhaseAfterRun` always ignores it.

- [ ] **Step 1: Struct field**

In `HistCombo`, after the `nSyncMisses` line add:

```c
    u8                      uStopOnSuccess; // 12    0xFF: stop at the first success (-n probes), 0: run all attempts
```

In `runHistTest`, after `oParams.nSyncMisses = 0;` add:

```c
    oParams.uStopOnSuccess = 0;             // the histogram needs every attempt
```

- [ ] **Step 2: hfRun exit**

In `hfRun`, replace

```asm
    ld      a,b
    rra                             ; carry = busy (CE)
    ccf                             ; carry = done
    exx
    adc     hl,de                   ; successes += done (DE' = 0), no branch
    dec     bc                      ; remaining attempts
    ld      a,b
    or      c
    exx
    jp      z,hf_all_done
```

with

```asm
    ld      a,b
    rra                             ; carry = busy (CE)
    ccf                             ; carry = done
    sbc     a,a                     ; A = 0xFF if done, else 0 (carry unchanged)
    ld      e,a                     ; E = done mask (main DE is scratch here)
    exx
    adc     hl,de                   ; successes += done (DE' = 0), no branch
    dec     bc                      ; remaining attempts
    ld      a,b
    or      c
    exx
    jp      z,hf_all_done
    ld      a,(uStopMask)           ; 0xFF: stop at the first success (-n probes)
    and     e
    jp      nz,hf_all_done          ; done and stop wanted (constant cost when not taken)
```

- [ ] **Step 3: Mask setup**

In `_histPhaseAfterRun`, after `ld (uFhMask),a ; ignore the guard line` add:

```asm
    ld      (uStopMask),a           ; calibration runs all its attempts (A = 0)
```

In `_histMeasureDelay`, after `ld (uFhMask),a ; honour the guard line` add:

```asm
    ld      a,12(ix)
    ld      (uStopMask),a           ; HistCombo.uStopOnSuccess
```

In `_histMeasureDelayRelock`, after `ld (uFhMask),a ; hfRun runs one attempt and exits before its FH check` add:

```asm
    ld      a,12(ix)
    ld      (uStopMask),a           ; HistCombo.uStopOnSuccess
```

In `_histMeasureDelayRelock`, directly after `call hfRun ; one attempt, exits through hf_all_done` add:

```asm
    ld      a,(uStopMask)           ; stop at the first success? (not timed)
    or      a
    jr      z,rm_no_stop
    exx
    ld      a,h
    or      l                       ; HL' = successes
    exx
    jp      nz,rm_done
rm_no_stop:
```

In the `_DATA` area, after `uFhMask:` and its `.db 0` line add:

```asm
uStopMask:
    .db 0                           ; 0xFF: a measurement ends at the first success (HistCombo.uStopOnSuccess)
```

Note: the non-exit path gets 40 cycles longer (`sbc` 5, `ld e` 5, `ld a,(nn)` 14, `and` 5, `jp` 11). Calibration measures the real length, so nothing else changes; `HIST_OVERHEAD_MAX` (1400) still covers the attempt overhead.

Update the `hfRun` header comment line `; OUT: carry clear: all attempts done` to `; OUT: carry clear: all attempts done (or first success with uStopMask)`.

- [ ] **Step 4: Build and listing check**

Run the build. Expected: exit 0.

Run: `Set-Location C:\source\repos\cmdcycles; sdasz80 -o -l -g -w "$env:TEMP\hg.rel" src\histogram.s; Select-String -Path "$env:TEMP\hg.lst" -Pattern "sbc     a,a|uStopMask" | ForEach-Object { $_.Line }; Remove-Item "$env:TEMP\hg.*"`
Expected: `9F` for `sbc a,a`; four `uStopMask` references plus the label.

- [ ] **Step 5: -g regression check (code)**

Run: `Select-String -Path src\main.c -Pattern "uStopOnSuccess"`
Expected: the struct field and `oParams.uStopOnSuccess = 0;` in `runHistTest` (and in Task 3 the `0xFF` in `runSweepTest`). `runDriftTest` uses `memset(&oParams, 0, ...)` and never measures, so it needs nothing.

Record map sizes.

---

### Task 3: Search and orchestration

**Files:**
- Modify: `src/main.c` (constants, global, `histCalibrate`, new `sweepPad`, `sweepProbe`, `sweepFindMin`, `runSweepTest`, `runTests`)

**Interfaces:**
- Consumes: `g_bSweepMode`, `g_nSweeps` (Task 1); `HistCombo.uStopOnSuccess` (Task 2); existing `histInit`, `histMeasureDrift`, `histRelaxSyncCheck1`, `histCalibrate`, `histMeasureDelay`, `histMeasureDelayRelock`, `g_bRelock`, `g_iDrift`, `HIST_LOCK_FAILED`.
- Produces: `bool runSweepTest(void);` fills `g_anResultDelays[c][u][v]` (incl. `nAdd`); sets `ERROR_CODE = 6` on lock failure.

- [ ] **Step 1: Constant and global**

After `#define HIST_LOCK_FAILED ...` add:

```c
#define SWEEP_LINEAR_MIN    48      // -n: from this delay value on, dispatch_vdp_test is linear in d (one calibration per cell)
```

After `g_nMissPhase` (globals) add:

```c
u16                     g_nCalibLmod;           // histCalibrate: attempt length mod 228 at pad HIST_PAD_MIN (last calibration)
```

In `histCalibrate`, after `u16 nMod = histPhaseDiff(s1, s2); // attempt length mod 228 with pad HIST_PAD_MIN` add:

```c
            g_nCalibLmod = nMod;               // -n derives the pad of other delay values from this
```

- [ ] **Step 2: Pad, probe and search**

Directly before `// ---` / `bool runHistTest(void)` add:

```c
// ---------------------------------------------------------------------------
// -n: pad for delay value nD, from one calibration at nD0 (Lmod0 = attempt
// length mod 228 at pad HIST_PAD_MIN). Valid for nD, nD0 >= SWEEP_LINEAR_MIN,
// where dispatch_vdp_test's wait is exactly linear in d.
u16 sweepPad(u16 nLmod0, u16 nD0, u16 nD)
{
    u16 nLmod = (nLmod0 + (nD % HIST_SWEEP_LEN) + HIST_SWEEP_LEN - (nD0 % HIST_SWEEP_LEN)) % HIST_SWEEP_LEN; // never negative

    return HIST_PAD_MIN + ((nLmod <= 1) ? (1 - nLmod) : (HIST_SWEEP_LEN + 1 - nLmod));
}

// ---------------------------------------------------------------------------
// -n: does at least one of the k * 228 attempts at nDelay succeed?
// Returns 1, 0, or HIST_LOCK_FAILED. Stops at the first success.
u16 sweepProbe(HistCombo* p, u16 nDelay, u16 nLmod0, u16 nD0)
{
    p->nDelay = nDelay;
    p->uGuardLine = (u8)(211 - (nDelay + HIST_OVERHEAD_MAX) / HIST_SWEEP_LEN - 2); // an attempt started on this line ends by line 211

    u16 nCount;
    if(g_bRelock)                           // drifting clocks: a lock per attempt, no calibration
        nCount = histMeasureDelayRelock(p);
    else
    {
        if(nDelay < SWEEP_LINEAR_MIN)       // direct stubs: no linear model, calibrate this value
            histCalibrate(p);
        else
            p->nPad = sweepPad(nLmod0, nD0, nDelay);

        nCount = histMeasureDelay(p);
    }

    if(nCount == HIST_LOCK_FAILED)
        return HIST_LOCK_FAILED;

    return (nCount > 0) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// -n: smallest delay value at which at least one attempt succeeds, for the
// cell in *p (binary search, see the spec). nInit: initial delay of the cell
// (from establishInitialDelays, includes LEEWAY). Returns HIST_LOCK_FAILED if
// no lock was possible.
u16 sweepFindMin(HistCombo* p, u16 nInit)
{
    u16 nLmod0 = 0;
    if(!g_bRelock)                          // one calibration per cell (LINE takes another setup branch per size)
    {
        p->nDelay = nInit;
        histCalibrate(p);
        nLmod0 = g_nCalibLmod;
    }

    u16 nStep = LEEWAY + 2 * INITIAL_LOOP_CYCLES;
    u16 nHi = nInit;                        // must succeed
    u16 nLo = (nInit > nStep + HIST_MIN_DELAY) ? (nInit - nStep) : HIST_MIN_DELAY; // must fail
    u16 nRes;

    while(true)                             // widen upwards until hi succeeds
    {
        nRes = sweepProbe(p, nHi, nLmod0, nInit);
        if(nRes == HIST_LOCK_FAILED)
            return HIST_LOCK_FAILED;
        if(nRes)
            break;
        nHi += LEEWAY;
    }

    while(true)                             // widen downwards until lo fails (or the floor succeeds)
    {
        nRes = sweepProbe(p, nLo, nLmod0, nInit);
        if(nRes == HIST_LOCK_FAILED)
            return HIST_LOCK_FAILED;
        if(!nRes)
            break;
        if(nLo == HIST_MIN_DELAY)
            return HIST_MIN_DELAY;          // floor reached and it still succeeds
        nHi = nLo;
        nLo = (nLo > 2 * INITIAL_LOOP_CYCLES + HIST_MIN_DELAY) ? (nLo - 2 * INITIAL_LOOP_CYCLES) : HIST_MIN_DELAY;
    }

    while(nHi - nLo > 1)                    // invariant: probe(nLo) fails, probe(nHi) succeeds
    {
        u16 nMid = nLo + (nHi - nLo) / 2;
        nRes = sweepProbe(p, nMid, nLmod0, nInit);
        if(nRes == HIST_LOCK_FAILED)
            return HIST_LOCK_FAILED;
        if(nRes)
            nHi = nMid;
        else
            nLo = nMid;
    }

    return nHi;
}

// ---------------------------------------------------------------------------
// -n: the whole CYCLES table with the sweep method. Results (incl. the T976x
// +1, as in runTest) go into g_anResultDelays.
bool runSweepTest(void)
{
    HistCombo oParams;

    oParams.nTries         = g_nSweeps * HIST_SWEEP_LEN;
    oParams.nSyncMisses    = 0;
    oParams.uStopOnSuccess = 0xFF;          // a probe only needs one success

    u16 nAdd = g_bHasT976xEngine ? 1 : 0;   // see runTest

    histInit(g_bHasT976xEngine);
    g_uCmdColor = HIST_CMD_COLOR;

    g_iDrift  = histMeasureDrift(&oParams); // coupled clocks: free-run per frame, else a lock per attempt
    g_bRelock = (g_iDrift != 0);            // also when the drift test failed (HIST_DRIFT_FAILED)

    if(g_bRelock)
        histRelaxSyncCheck1();              // see histLock

    bool bOk = true;

    for(u8 c = 0; bOk && (c < NUM_TESTS); c++)
    {
        oParams.uCmd = aEXECUTE_CMD[c];

        for(u8 v = 0; bOk && (v < NUM_VERTICALS); v++)
        {
            oParams.uH = aVERT_LEN[v];

            for(u8 u = 0; bOk && (u < NUM_HORIZONTALS); u++)
            {
                oParams.uW = aHORZ_LEN[u];

                u16 nMin = sweepFindMin(&oParams, g_anResultDelays[c][u][v]);
                if(nMin == HIST_LOCK_FAILED)
                {
                    ERROR_CODE = 6;
                    bOk = false;
                }
                else
                    g_anResultDelays[c][u][v] = nMin + nAdd;
            }
        }
    }

    g_uCmdColor = 0;
    g_nTotalSyncMisses = oParams.nSyncMisses;

    return bOk;
}
```

- [ ] **Step 3: runTests dispatch**

In `runTests`, replace

```c
    bool bOk = runTest();

    if(bOk && g_bHistogramMode)
        runHistTest();
```

with

```c
    if(g_bSweepMode)
        runSweepTest();
    else
    {
        bool bOk = runTest();

        if(bOk && g_bHistogramMode)
            runHistTest();
    }
```

- [ ] **Step 4: Build**

Run the build. Expected: exit 0. Record map sizes (expect `l__CODE` +~0x300..0x500 over Task 2).

---

### Task 4: Report, then runtime checks

**Files:**
- Modify: `src/main.c` (header strings, `printReportTop`, `printReportBody`, `main`)

**Interfaces:**
- Consumes: `g_bSweepMode`, `g_nSweeps`, `g_bRelock`, `g_iDrift`, `g_nTotalSyncMisses` and the miss counters.

- [ ] **Step 1: Header strings**

After `g_szTopLine2g` add:

```c
const u8                g_szTopLine2n[]         = "* VDP detected: %s%s, screen %d, mode: %s. (k:%u)\r\n";
```

After `g_szTopLine4_2` add:

```c
const u8                g_szTopLine4_3[]        = "* Method: phase sweep, k:%u sweeps of 228 per probe\r\n";
```

- [ ] **Step 2: printReportTop**

Inside `printReportTop`, at the top (after `u8* szT976x = ...`) add:

```c
    bool bSweep   = g_bSweepMode;
    bool bNewSync = bRealHistogram || bSweep; // HR vernier lock (-g and -n)
```

Replace the line-2 `if(bRealHistogram) sprintf(... g_szTopLine2g ...) else sprintf(... g_szTopLine2 ...)` construction with:

```c
    if(bRealHistogram)
        sprintf(g_auBuffer,
                g_szTopLine2g,
                aVDP_NAME[g_uVDPModel],
                szT976x,
                SCREEN_MODE,
                g_aszConditionNames[g_uCondition],
                g_uInnerIterations==0?256: g_uInnerIterations,
                g_uOuterIterations,
                g_nSweeps
               );
    else if(bSweep)
        sprintf(g_auBuffer,
                g_szTopLine2n,
                aVDP_NAME[g_uVDPModel],
                szT976x,
                SCREEN_MODE,
                g_aszConditionNames[g_uCondition],
                g_nSweeps
               );
    else
        sprintf(g_auBuffer,
                g_szTopLine2,
                aVDP_NAME[g_uVDPModel],
                szT976x,
                SCREEN_MODE,
                g_aszConditionNames[g_uCondition],
                g_uInnerIterations==0?256: g_uInnerIterations,
                g_uOuterIterations
               );
```

Change the sync-line condition `if(bRealHistogram)` (the one that prints `g_szTopLine2xg` / `g_szTopLine2xgF` / `g_szTopLine2xgR`) to `if(bNewSync)`.

Replace the final block

```c
    if(bRealHistogram)
    {
        print(g_szTopLine4_2);
        ...
    }
    else
        print(g_szTopLine4_1);
```

with

```c
    if(bRealHistogram || bSweep)
    {
        if(bRealHistogram)
        {
            print(g_szTopLine4_2);

            sprintf(g_auBuffer, g_szTopLine5, (u16)(g_nSweeps * HIST_SWEEP_LEN), g_nSweeps, HIST_SWEEP_LEN); // fits u16, k <= HIST_MAX_SWEEPS
            print(g_auBuffer);
        }
        else
        {
            sprintf(g_auBuffer, g_szTopLine4_3, g_nSweeps);
            print(g_auBuffer);
        }

        if(g_nTotalSyncMisses > 0) // only worth mentioning when something went wrong
        {
            sprintf(g_auBuffer, g_szTopLine6, g_nTotalSyncMisses, g_nMissVernier, g_nMissSync1, g_nMissSync2, g_nMissCalib, g_nMissPhase);
            print(g_auBuffer);
        }
    }
    else
        print(g_szTopLine4_1);
```

- [ ] **Step 3: printReportBody without the FAKE HISTOGRAM**

Change the signature and its comment:

```c
// ---------------------------------------------------------------------------
// Outputs markdown (easy to paste into github or other markdown viewers).
// bWithFakeHistogram: false in -n mode (there is no such data).
void printReportBody(bool bWithFakeHistogram)
```

Directly after the `Grand total` print (`sprintf(g_auBuffer, g_szTotalLine, ulTotal); print(g_auBuffer);`, the first one) add:

```c
    if(!bWithFakeHistogram)
        return;
```

In `main`, change `printReportBody();` to `printReportBody(!g_bSweepMode);`.

- [ ] **Step 4: Build and checks**

Run the build. Expected: exit 0; the warning `function declarator with no prototype` for `printReportBody` is gone.

Run: `python <scratchpad>/check_help.py src/main.c`. Expected: `PASS`.

Run: `Select-String -Path src\main.c -Pattern "printReportBody\("`. Expected: the definition with `bool bWithFakeHistogram` and the call `printReportBody(!g_bSweepMode)`.

Record map sizes.

- [ ] **Step 5: Leave uncommitted**

Do not commit. Report `git status --short`.

- [ ] **Step 6: Runtime checks (human), coupled-clock machine (openMSX or Sanyo/Philips/Sony/FS-A1)**

1. `cmdcycle -n 1 > n1.md`:
   - Header: `(k:1)`, `* Sync method: Line interrupts + HR vernier lock per frame`, `* Method: phase sweep, k:1 sweeps of 228 per probe`, no "No real histogram" line.
   - Body: the `## CYCLES` tables with Sum / Grand total, **no** `## FAKE HISTOGRAM`.
   - 8x8 and 7x7 cells equal the `-g` minima of the same machine (±1, e.g. FS-A1/Philips LMMM 8x8 2172, LINE 234, `-3` LMMM 1451).
   - Small cells (1x1 … 2x2) plausible, no hang.
2. Run `-n 1` a second time in the same DOS session: identical table (with display off certainly; with display on ±1 at single cells).
3. `cmdcycle -g 8 8 1`: rows still sum to 228 (regression check for the stop flag).
4. `cmdcycle > norm.md` (normal mode): CYCLES + FAKE HISTOGRAM as before.

- [ ] **Step 7: Runtime check (human), turbo R**

`cmdcycle -n 1 > n1tr.md` on the FS-A1ST: header `lock per attempt (CPU/VDP clock drift: 8..9 cycles per 1024 lines)`, plausible table close to the other machines.
