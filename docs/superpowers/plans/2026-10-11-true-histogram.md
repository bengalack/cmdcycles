# True Histogram (`-g x y`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `-g x y` mode that, for one command size and all 6 tests, records the number of successful VDP command completions for each delay value from the lowest successful delay upward, and prints it as a `## HISTOGRAM` markdown report.

**Architecture:** Two phases per test. Phase 1 is the existing `runTest()`, restricted to the one size, and finds W. Phase 2 is a new `runHistTest()` that calls new assembly routines (`_runTestComboHistNoHalts`, or `_runTestComboHist` with `-t`). These routines start at raw W + 33 and spend the full `inner*outer*13` budget on each delay value, counting successes into the existing scratch histogram, until a delay value gets zero successes. A new `printReportBodyHist()` prints the first 16 counts per test.

**Tech Stack:** C (SDCC 4.5.24, `-mz80`, `__sdcccall(1)`), Z80 assembly (sdasz80 / asxxxx syntax), MSX-DOS, openMSX for runtime testing.

**Spec:** `docs/superpowers/specs/2026-10-11-true-histogram-design.md`

## Global Constraints

- **NEVER run `git commit` (or `git add`/`git push`).** The user commits manually. Every task ends with "leave changes uncommitted".
- SDCC v4.5 C dialect, matching existing style: Hungarian prefixes (`u8 uX`, `u16 nX`, `g_` globals, `sz` strings), `for(u8 c = 0; ...)` loops, `// -----` separator comment before each function, 4-space indentation.
- Assembly style: lowercase mnemonics, `#` immediate prefix, `::` global labels, `;` comments aligned at column ~37, `.ifeq METHOD_WAIT - n` conditionals.
- Existing `_runTestCombo` and `_runTestComboNoHalts` must **not** be modified (not even to use the new macro).
- Inner loops: no new subroutine calls, no new IX/IY access in the per-attempt path; busy and success paths must take equal cycles (44).
- Help text: max **29 columns** per line, max **22 lines**. Main help ends up exactly 22 lines.
- Wait-index rounds per delay value = **13** (`iyl` 0..12); budget = `inner * outer * 13`; `-g` must reject budgets `> 65535`.
- `HIST_DISPLAY_LEN` = 32, `HIST_START_MARGIN` = 33.
- Version string becomes `"0.83"`.
- Build: `cmd //c build.bat` from the repo root (Git Bash), or `build.bat` from cmd. Output: `dska\cmdcycle.com` (a tracked file; being modified by builds is expected).

## Review Focus

1. **`-g` on a T976x machine (FS-A1GT, +1 cycle)**. The phase-2 start must subtract `nAdd` before adding the margin, and the reported winner must re-add it. Test: Task 4, Step 6 (FS-A1GT) and Step 7 (non-T976x machine).
2. **Small sizes, e.g. `-g 1 1`**. W can be about 13–15 in emulators, and the scan goes through `getNextPossibleDelay`'s mini-table down to 0 or `stub_invalid`. Expected: no hang, a row with a valid winner or `(?)`. Test: Task 4, Step 8.
3. **Budget edge**. `-i 256 -o 19` (63232) is accepted and `-i 256 -o 20` (66560) is rejected. Test: Task 1, Step 6.
4. **`-g` together with `-t` (VBLANK sync)**. This exercises `_runTestComboHist`. Expected: completes, plausible counts (increasing towards the budget). Test: Task 4, Step 9.
5. **`-g` with a sys name, e.g. `cmdcycle -g 8 8 MyMSX`**. `-g` must consume exactly two args, and the sys name must still appear in the header. Test: Task 1, Step 6.

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `src/main.c` | CLI, orchestration, report | new globals, `-g` parsing, help texts, loop bounds, `runHistTest()`, `printReportBodyHist()`, `recordAndClearHistogram` param |
| `src/bioshelper.s` | test-loop assembly | new macro `controlledWaitByMethod`, new `_runTestComboHistNoHalts`, new `_runTestComboHist` |

There is no automated test framework (the target is an MSX). Each task's checks are:
- (a) **build check**: `build.bat` with no errors;
- (b) **static checks** with a small Python helper in the scratchpad (help text width and line count);
- (c) **runtime checks in openMSX**: the human runs these, and the steps say exactly what to type and what to look for.

---

### Task 1: CLI, help texts and size-range restriction

**Files:**
- Modify: `src/main.c` (defines ~line 69-81, consts ~line 199-240, globals ~line 322-343, `initVarsAndRig`, `establishInitialDelays`, `runTest`, `printHelp` area, `commandLineParameters`)
- Test helper (scratchpad, not in repo): `<scratchpad>/check_help.py`

**Interfaces:**
- Produces (used by Tasks 4):
  - `#define NUM_WAIT_ROUNDS 13`
  - `bool g_bHistogramMode;`, `u8 g_uHistU;`, `u8 g_uHistV;` (indices into `aHORZ_LEN` / `aVERT_LEN`)
  - `u8 g_uFirstU, g_uEndU, g_uFirstV, g_uEndV;` (range `[first, end)`)
  - `void printHelpHist(void);`

- [ ] **Step 1: Write the help-text checker (the failing test)**

Create `<scratchpad>/check_help.py` (scratchpad = the session scratchpad directory; never inside the repo):

```python
# Checks the MSX help texts in main.c: <= 29 columns per line, <= 22 lines.
import sys

src = open(sys.argv[1], encoding="latin-1").read()

def get_c_string(name):
    i = src.index(name + "[]")
    i = src.index("=", i) + 1
    out = ""
    while True:
        c = src[i]
        if c.isspace():
            i += 1
        elif src.startswith("//", i):
            i = src.index("\n", i)
        elif c == '"':
            j = i + 1
            while src[j] != '"':
                if src[j] == "\\":
                    out += src[j:j + 2]; j += 2
                else:
                    out += src[j]; j += 1
            i = j + 1
        elif c == ";":
            return out.replace("\\r\\n", "\n")
        else:
            raise SystemExit("unexpected text near: " + repr(src[i:i + 30]))

checks = [
    ("g_szHelptext",     ("0.83", "Random (R-reg)", 3, 64)),   # longest wait-name variant
    ("g_szHelptextHist", (8, 8)),
]

ok = True
for name, args in checks:
    try:
        text = get_c_string(name) % args
    except ValueError:
        print(f"FAIL {name}: not found"); ok = False; continue
    lines = text.split("\n")
    print(f"--- {name}: {len(lines)} lines")
    for l in lines:
        flag = "  <-- TOO WIDE" if len(l) > 29 else ""
        print(f"{len(l):2} |{l}{flag}")
        if flag: ok = False
    if len(lines) > 22:
        print(f"FAIL {name}: {len(lines)} lines > 22"); ok = False

print("PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python <scratchpad>/check_help.py src/main.c`
Expected: `FAIL g_szHelptextHist: not found` and exit code 1 (the main help passes with 21 lines).

- [ ] **Step 3: Add defines, consts, globals and init**

In `src/main.c`, after `#define DEFAULT_OUTER_LOOPS 3` add:

```c
#define NUM_WAIT_ROUNDS     13      // wait indexes 0..12 per delay value. Must match "#12" (last wait index) in bioshelper.s
```

Replace the existing `g_szHelptext` definition, adding the `-g` line after `-t`:

```c
                                                //"                             " // 29 chars (turbo r)
const u8                g_szHelptext[]          = "Usage: cmdcycle [opt][sys]\r\n"
                                                  "\r\n"
                                                  "Measure the duration of VDP\r\n"
                                                  "command in MSX Z80 cycles\r\n"
                                                  "(outside VBLANK area).\r\n"
                                                  "Format is markdown, written\r\n"
                                                  "to stdout (use redirection)\r\n"
                                                  "\r\n"
                                                  "Version: %s\r\n"
                                                  "Wait: %s\r\n"
                                                  "\r\n"
                                                  "Options (opt):\r\n"
                                                  " -h Show this help message\r\n"
                                                  // " -5 Screen 5 (default: 8)\r\n"
                                                  " -o n Outer loops (%d)\r\n"
                                                  " -i n Inner loops (%d)\r\n"
                                                  " -1 Mode: normal (default)\r\n"
                                                  " -2 Mode: no sprites\r\n"
                                                  " -3 Mode: no screen\r\n"
                                                  " -t VBLANK timing\r\n"
                                                  " -g x y Histogram (-h -g)\r\n"
                                                  "\r\n"
                                                  "sys: Show sys name in report";

                                                //"                             " // 29 chars (turbo r)
const u8                g_szHelptextHist[]      = "Usage: cmdcycle -g x y [sys]\r\n"
                                                  "\r\n"
                                                  "Histogram of the duration of\r\n"
                                                  "one command size (x*y) for\r\n"
                                                  "all tests.\r\n"
                                                  "\r\n"
                                                  "x: 1..%d (width)\r\n"
                                                  "y: 1..%d (height)\r\n"
                                                  "\r\n"
                                                  "-i -o -1 -2 -3 -t apply as\r\n"
                                                  "usual, but -i*-o max 5041.";
```

After `bool g_bVBLANKTiming;` add:

```c
bool                    g_bHistogramMode;       // -g x y
u8                      g_uHistU;               // -g: index into aHORZ_LEN
u8                      g_uHistV;               // -g: index into aVERT_LEN
u8                      g_uFirstU;              // sizes to test, range: [first, end)
u8                      g_uEndU;
u8                      g_uFirstV;
u8                      g_uEndV;
```

In `initVarsAndRig`, after `g_bVBLANKTiming = false;` add:

```c
    g_bHistogramMode    = false;
    g_uHistU            = 0;
    g_uHistV            = 0;
    g_uFirstU           = 0;
    g_uEndU             = NUM_HORIZONTALS;
    g_uFirstV           = 0;
    g_uEndV             = NUM_VERTICALS;
```

- [ ] **Step 4: Restrict loops, add `printHelpHist`, `findLenIndex` and `-g` parsing**

In `establishInitialDelays`, change the two inner loop headers to:

```c
        for(u8 v = g_uFirstV; v < g_uEndV; v++)
```
```c
            for(u8 u = g_uFirstU; u < g_uEndU; u++)
```

In `runTest`, change the two inner loop headers to the same two lines.

After `printHelp()` add:

```c
// ---------------------------------------------------------------------------
void printHelpHist(void)
{
    sprintf(g_auBuffer, g_szHelptextHist, NUM_HORIZONTALS, NUM_VERTICALS);
    print(g_auBuffer);
}

// ---------------------------------------------------------------------------
// Returns the index of iVal in pTable, or 0xff if not found
u8 findLenIndex(const u8* pTable, u8 uLen, s16 iVal)
{
    for(u8 n = 0; n < uLen; n++)
    {
        if((s16)pTable[n] == iVal)
            return n;
    }

    return 0xff;
}
```

In `commandLineParameters`, replace the `-h` branch with:

```c
            if(strcmp(argv[n], "-h") == 0 || strcmp(argv[n], "--help") == 0)
            {
                if((n + 1 < argc) && (strcmp(argv[n+1], "-g") == 0))
                    printHelpHist();
                else
                    printHelp();
                return 0;
            }
```

Add this branch before the `-i` branch:

```c
            else if(strcmp(argv[n], "-g") == 0)
            {
                if(n + 2 >= argc)
                {
                    print("ERROR:Missing value(s) for -g");
                    return 1;
                }

                iVal = atoi(argv[++n]);
                u8 uU = findLenIndex(aHORZ_LEN, NUM_HORIZONTALS, iVal);
                if((iVal < 1) || (iVal > NUM_HORIZONTALS) || (uU == 0xff))
                {
                    sprintf(g_auBuffer, "ERROR:x for -g must be in range 1-%d", NUM_HORIZONTALS);
                    print(g_auBuffer);
                    return 1;
                }

                iVal = atoi(argv[++n]);
                u8 uV = findLenIndex(aVERT_LEN, NUM_VERTICALS, iVal);
                if((iVal < 1) || (iVal > NUM_VERTICALS) || (uV == 0xff))
                {
                    sprintf(g_auBuffer, "ERROR:y for -g must be in range 1-%d", NUM_VERTICALS);
                    print(g_auBuffer);
                    return 1;
                }

                g_bHistogramMode = true;
                g_uHistU  = uU;
                g_uHistV  = uV;
                g_uFirstU = uU;
                g_uEndU   = uU + 1;
                g_uFirstV = uV;
                g_uEndV   = uV + 1;
            }
```

After the closing `}` of the `for(u8 n=0; ...)` loop (still inside `if(argc > 0)`), add:

```c
        if(g_bHistogramMode) // counts are u16
        {
            u16 nInner = (g_uInnerIterations == 0) ? 256 : g_uInnerIterations;
            if((u32)nInner * (u32)g_uOuterIterations * (u32)NUM_WAIT_ROUNDS > 65535ul)
            {
                print("ERROR:-g budget too large (-i*-o*13 > 65535)");
                return 1;
            }
        }
```

- [ ] **Step 5: Run the checker and the build**

Run: `python <scratchpad>/check_help.py src/main.c`
Expected: `g_szHelptext: 22 lines`, `g_szHelptextHist: 11 lines`, no `TOO WIDE`, then `PASS`.

Run: `cmd //c build.bat`
Expected: no `error` lines. Existing warnings, if any, are unchanged.

- [ ] **Step 6: Runtime check in openMSX (human)**

Start the emulator with `run.bat` (FS-A1GT, MSX-DOS2) and at the `A>` prompt run:

| Command | Expected |
|---|---|
| `cmdcycle -h` | the 22-line help from the spec, with ` -g x y Histogram (-h -g)` |
| `cmdcycle -h -g` | the 11-line histogram help, with `x: 1..8 (width)` |
| `cmdcycle -g` | `ERROR:Missing value(s) for -g` |
| `cmdcycle -g 8` | `ERROR:Missing value(s) for -g` |
| `cmdcycle -g 0 1` | `ERROR:x for -g must be in range 1-8` |
| `cmdcycle -g 1 9` | `ERROR:y for -g must be in range 1-8` |
| `cmdcycle -g 8 8 -i 256 -o 20` | `ERROR:-g budget too large (-i*-o*13 > 65535)` |
| `cmdcycle -g 8 8 -i 256 -o 19 MyMSX > t1.md` | accepted, starts running (abort with reset after the error checks if it takes long; this only checks parsing) |
| `cmdcycle -g 8 8 MyMSX > t2.md` | completes quickly. `t2.md` has the normal CYCLES table with only the 8/8 cell non-zero, and the header line contains `MyMSX` |

- [ ] **Step 7: Leave changes uncommitted**

Do **not** commit. Report `git status --short` to the user.

---

### Task 2: `controlledWaitByMethod` macro and `_runTestComboHistNoHalts`

**Files:**
- Modify: `src/bioshelper.s` (after the `histogram_advance_and_record` macro, ~line 800, and new routine after `x_runTestComboInner` / before `_prepareLineInterruptsNI`, ~line 928)

**Interfaces:**
- Consumes: existing `secureActiveArea`, `setVDPParams`, `testVDP`, `doControlledWait`, `getNextPossibleDelay`, `prepareLineInterruptsNI`, `_cleanupLineInterruptsNI`, macros `updateHistogram`, `histogram_advance_and_record`, struct offsets of `RunCombo` (0 uW, 1 uH, 2 uCmd, 3 uInnerIterations, 4-5 nStartDelay, 6 uOuterIterations, 7-8 pHistogramWinner).
- Produces (used by Task 4):
  - `u16 runTestComboHistNoHalts(RunCombo* p, u16* panHistogramLastIndex);` — returns lowest delay with ≥1 success (raw, no T976x add), or 0 if none; sets `p->pHistogramWinner` to that delay's scratch slot (stays 0 if none). Each scratch slot (going backwards from `panHistogramLastIndex` = start delay) holds the success count for one delay value.
  - macro `controlledWaitByMethod` (also used by Task 3).

- [ ] **Step 1: Add the macro**

In `src/bioshelper.s`, directly after the `.endm` of `histogram_advance_and_record`, add:

```asm
; Same wait as the inline METHOD_WAIT blocks in x_runTestComboInner and
; runTestComboInner (which are kept as-is). Used by the histogram routines.
; IN:       IYL - wait index (METHOD_WAIT 1)
; MODIFIES: AF (HL is preserved)
.macro controlledWaitByMethod
.ifeq METHOD_WAIT - 1               ; counter
    push    hl
    ld      a,iyl
    call    doControlledWait        ; A is input. constant wait in cycles + A value.
    pop     hl
.else
.ifeq METHOD_WAIT - 2               ; random
    push    hl
    ld      a,r
    and     #0x0f
    call    doControlledWait        ; A is input. constant wait in cycles + A value.
    pop     hl
.else
                                    ; No wait
.endif
.endif
.endm
```

- [ ] **Step 2: Add `_runTestComboHistNoHalts`**

Directly before the `; ---` header of `_prepareLineInterruptsNI`, add:

```asm
; -------------------------------------------------------------
; Called in EI. True histogram variant of runTestComboNoHalts.
; Assumes anHistogram is already initialized to 0.
; For each delay value, starting at nStartDelay and counting down, the full
; budget is run: outer * 13 (wait indexes) * inner attempts. Attempts that
; SUCCEED (command done) are counted in the histogram slot for that delay.
; Stops after the first delay value with zero successes.
; Busy and success paths take the same amount of cycles (44) after the test.
;
; IN:       HL - pointer to struct (see runTestComboNoHalts)
;           DE - pointer to anHistogram[last] (slot for nStartDelay)
; OUT:      DE - lowest delay with at least one success (0 if none)
;           pHistogramWinner (7 & 8) points to the slot of that delay
; MODIFIES: AF, BC, DE, HL, IY, HL', DE'
; u16 runTestComboHistNoHalts(RunCombo* p, u16* anHistogramLastIndex); HL, DE
_runTestComboHistNoHalts::
    push    ix

    push    de
    exx
    pop     hl
    exx                             ; HL' holds pointer to u16 anHistogram[HISTOGRAM_BUFFER-1], we'll populate the 16bit array backwards

    ex      de,hl
    ld      ixl,e
    ld      ixh,d                   ; put pointer to struct in IX

    di
    call    prepareLineInterruptsNI ; IYH: will hold line-interrupt line number (0 or SECOND_LINE_INT).

    ld      c,2(ix)                 ; C: command
    ld      e,4(ix)
    ld      d,5(ix)                 ; DE: Current Attempting delay
    ld      hl,#0                   ; HL: lowest delay with a success so far (0: none)

hn_next_delay::                     ; one pass per delay value (DE)
    ld      b,6(ix)                 ; outer iterations

hn_outer::
    push    bc                      ; store for outer loop
    ld      iyl,#0                  ; wait-index

hn_round::                          ; one round per wait index (0-12)
    ld      b,3(ix)                 ; inner iterations
    call    secureActiveArea
    controlledWaitByMethod

hn_loopr::
    call    setVDPParams            ; this one will add several extra cycles on Toshiba models
    call    testVDP                 ; C = Cmd, DE = delay. Result in A
    rr      a                       ; CE bit goes into C, but if all we are ZERO here, something is wrong
    jp      z,hn_impossible         ; if the full value is 0 (impossible status reg), we have tried an impossible wait value
    jp      c,hn_busy               ; 11

    updateHistogram                 ; 33 (59 in 1 of 256). Count the success
    jp      hn_next_try             ; 11 -> 44

hn_busy::
    ld      a,i                     ; 11
    ld      a,i                     ; 11
    ld      a,i                     ; 11
    ld      a,i                     ; 11 -> 44, same as success path (delayNN macros would destroy HL)

hn_next_try::
    call    secureActiveArea
    djnz    hn_loopr                ; try again with same values

    ld      a,#12
    cp      iyl                     ; last wait index done?
    jp      z,hn_round_done
    inc     iyl                     ; same delay, but different value in doControlledWait
    jp      hn_round

hn_round_done::
    pop     bc                      ; restore for outer looping
    djnz    hn_outer

    exx
    ld      a,(hl)
    inc     hl
    or      (hl)                    ; Z: no successes at all for this delay
    dec     hl
    exx
    jp      z,hn_done

    histogram_advance_and_record    ; pHistogramWinner = this slot, move to next slot
    ld      h,d
    ld      l,e                     ; HL: lowest delay with a success so far
    call    getNextPossibleDelay    ; DE: next delay to try

    ld      a,d
    or      e
    jp      nz,hn_next_delay        ; if next possible delay == 0, we bail
    jp      hn_done

hn_impossible::
    pop     bc                      ; outer counter

hn_done::
    call    _cleanupLineInterruptsNI

    ei
    ex      de,hl                   ; SDCC needs return in DE
    pop     ix
    ret
```

- [ ] **Step 3: Build**

Run: `cmd //c build.bat`
Expected: no errors from `sdasz80 ... bioshelper.s` or from linking. (The routine is not called yet; this checks that the macro with nested `.ifeq` and the labels assemble.)

- [ ] **Step 4: Verify that existing routines are untouched**

Run: `git diff src/bioshelper.s`
Expected: only additions (`+` lines). No `-` lines and no changes inside `_runTestComboNoHalts`, `x_runTestComboInner`, `_runTestCombo` or `runTestComboInner`.

- [ ] **Step 5: Leave changes uncommitted**

Do **not** commit.

---

### Task 3: `_runTestComboHist` (VBLANK / `-t`)

**Files:**
- Modify: `src/bioshelper.s` (new routine after `runTestComboInner`, before `.area _DATA`, ~line 1143)

**Interfaces:**
- Consumes: `controlledWaitByMethod` (Task 2), `sync_with_active_area`, `setCustomISRAndPrepNI`, `restoreISRAndSetS0NoLineInt_NI`, `bACTIVE_AREA`, `setVDPParams`, `testVDP`, `getNextPossibleDelay`, macros `updateHistogram`, `histogram_advance_and_record`.
- Produces (used by Task 4): `u16 runTestComboHist(RunCombo* p, u16* panHistogramLastIndex);`, with the same contract as `runTestComboHistNoHalts`.

- [ ] **Step 1: Add `_runTestComboHist`**

Directly before `    .area _DATA` at the end of `src/bioshelper.s`, add:

```asm
; -------------------------------------------------------------
; Called in EI. True histogram variant of runTestCombo (with halts).
; See runTestComboHistNoHalts for the algorithm.
;
; IN:       HL - pointer to struct (see runTestCombo)
;           DE - pointer to anHistogram[last] (slot for nStartDelay)
; OUT:      DE - lowest delay with at least one success (0 if none)
;           pHistogramWinner (7 & 8) points to the slot of that delay
; MODIFIES: AF, BC, DE, HL, IY, HL', DE'
; u16 runTestComboHist(RunCombo* p, u16* anHistogramLastIndex); HL, DE
_runTestComboHist::
    push    ix

    push    de
    exx
    pop     hl
    exx                             ; HL' holds pointer to u16 anHistogram[HISTOGRAM_BUFFER-1], we'll populate the 16bit array backwards

    ex      de,hl
    ld      ixl,e
    ld      ixh,d                   ; put pointer to struct in IX

    halt
    di
    call    setCustomISRAndPrepNI   ; DI barely needed this fast after halt, but just as good practise
    ei

    ld      c,2(ix)                 ; C: command
    ld      e,4(ix)
    ld      d,5(ix)                 ; DE: Current Attempting delay
    ld      hl,#0                   ; HL: lowest delay with a success so far (0: none)

hv_next_delay::                     ; one pass per delay value (DE)
    ld      b,6(ix)                 ; outer iterations

hv_outer::
    push    bc                      ; store for outer loop
    ld      iyl,#0                  ; wait-index

hv_round::                          ; one round per wait index (0-12)
    ld      b,3(ix)                 ; inner iterations
    call    sync_with_active_area
    controlledWaitByMethod

hv_loopr::
    di
    call    setVDPParams            ; this one will add several extra cycles on Toshiba models
    call    testVDP                 ; C = Cmd, DE = delay. Result in A
    ei
    rr      a                       ; CE bit goes into C, but if all we are ZERO here, something is wrong
    jp      z,hv_impossible         ; if the full value is 0 (impossible status reg), we have tried an impossible wait value
    jp      c,hv_busy               ; 11

    updateHistogram                 ; 33 (59 in 1 of 256). Count the success
    jp      hv_next_try             ; 11 -> 44

hv_busy::
    ld      a,i                     ; 11
    ld      a,i                     ; 11
    ld      a,i                     ; 11
    ld      a,i                     ; 11 -> 44, same as success path (delayNN macros would destroy HL)

hv_next_try::
    ld      a,(bACTIVE_AREA)        ; ensure we are still in active area and have time for a new attempt.
    or      a
    call    z,sync_with_active_area ; if not, we wait until we are in active area again

    djnz    hv_loopr                ; try again with same values

    ld      a,#12
    cp      iyl                     ; last wait index done?
    jp      z,hv_round_done
    inc     iyl                     ; same delay, but different value in doControlledWait
    jp      hv_round

hv_round_done::
    pop     bc                      ; restore for outer looping
    djnz    hv_outer

    exx
    ld      a,(hl)
    inc     hl
    or      (hl)                    ; Z: no successes at all for this delay
    dec     hl
    exx
    jp      z,hv_done

    histogram_advance_and_record    ; pHistogramWinner = this slot, move to next slot
    ld      h,d
    ld      l,e                     ; HL: lowest delay with a success so far
    call    getNextPossibleDelay    ; DE: next delay to try

    ld      a,d
    or      e
    jp      nz,hv_next_delay        ; if next possible delay == 0, we bail
    jp      hv_done

hv_impossible::
    pop     bc                      ; outer counter

hv_done::
    ex      de,hl                   ; SDCC needs return in DE

    di
    call    restoreISRAndSetS0NoLineInt_NI
    ei

    pop     ix
    ret

```

- [ ] **Step 2: Build**

Run: `cmd //c build.bat`
Expected: no errors.

- [ ] **Step 3: Verify that existing routines are untouched**

Run: `git diff src/bioshelper.s`
Expected: still only `+` lines.

- [ ] **Step 4: Leave changes uncommitted**

Do **not** commit.

---

### Task 4: Phase 2 orchestration, report and version

**Files:**
- Modify: `src/main.c` (defines, `g_szVersion`, title consts, extern declarations ~line 151-152, `recordAndClearHistogram`, `runTest`, new `runHistTest`, `runTests`, new `printReportBodyHist`, `main`)

**Interfaces:**
- Consumes:
  - `u16 runTestComboHistNoHalts(RunCombo* p, u16* panHistogramLastIndex);` (Task 2)
  - `u16 runTestComboHist(RunCombo* p, u16* panHistogramLastIndex);` (Task 3)
  - `g_bHistogramMode`, `g_uHistU`, `g_uHistV` (Task 1)
- Produces: `bool runHistTest(void);`, `void printReportBodyHist(void);`, `bool recordAndClearHistogram(RunCombo*, u16*, u16, Histogram*, bool bTrimTrailingOnes);`

- [ ] **Step 1: Defines, consts, externs**

After the `NUM_WAIT_ROUNDS` define add:

```c
#define HIST_DISPLAY_LEN    16      // -g: max counts shown per test in the report
#define HIST_START_MARGIN   33      // -g: safety, histogram scan starts at W + this (W may be a lucky hit)
```

Change `g_szVersion` to:

```c
const u8                g_szVersion[]           = "0.83";
```

After `const u8 g_szTitle2[] = "## FAKE HISTOGRAM\r\n";` add:

```c
const u8                g_szTitle3[]            = "## HISTOGRAM\r\n";
```

After `extern u16 runTestComboNoHalts(...)` add:

```c
extern u16      runTestComboHist(RunCombo* p, u16* panHistogramLastIndex);
extern u16      runTestComboHistNoHalts(RunCombo* p, u16* panHistogramLastIndex);
```

- [ ] **Step 2: `recordAndClearHistogram` gets `bTrimTrailingOnes`**

Change the signature and the comment above it to:

```c
// ---------------------------------------------------------------------------
// Copy the actual histogram to a new buffer and store a pointer to it in the
// RunCombo struct, as well as some key data. bTrimTrailingOnes: fake histogram
// only, drop the trailing streak of 1s.
bool recordAndClearHistogram(RunCombo* pParams, u16* panHistogramLastIndex, u16 nBestDelay, Histogram* pRecord, bool bTrimTrailingOnes)
```

Wrap the existing `while(p >= pHistogramWinner)` loop (and nothing else) in `if(bTrimTrailingOnes)`:

```c
    if(bTrimTrailingOnes)
    {
        while(p >= pHistogramWinner) // find the first of the last 1s in a streak of 1s. If none, use last element
        {
            if( *p > 1 )
                break;

            pLast = p;
            p--;
        }
    }
```

(`pLast` starts as `panHistogramLastIndex`, so without trimming the length is `panHistogramLastIndex - pHistogramWinner + 1`.)

In `runTest`, update the call to pass `true`:

```c
                if(!recordAndClearHistogram(&oParams, panHistogramLastIndex, nBestDelay, &g_aoHistogram[c][u][v], true))
```

- [ ] **Step 3: Add `runHistTest` and update `runTests`**

Directly after `runTest()` add:

```c
// ---------------------------------------------------------------------------
// Phase 2 of -g mode: real histogram, starting a bit above the delay found by
// runTest (W). Counts successes per delay value, see runTestComboHistNoHalts.
bool runHistTest(void)
{
    RunCombo oParams;

    oParams.uInnerIterations = g_uInnerIterations;
    oParams.uOuterIterations = g_uOuterIterations;
    oParams.uW = aHORZ_LEN[g_uHistU];
    oParams.uH = aVERT_LEN[g_uHistV];

    u16* panHistogramLastIndex = &g_anScratchHistogram[SCRATCH_HIST_SIZE-1];
    u16 nAdd = g_bHasT976xEngine ? 1 : 0; // see runTest

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

In `runTests()`, replace `    runTest();` with:

```c
    bool bOk = runTest();

    if(bOk && g_bHistogramMode)
        runHistTest();
```

- [ ] **Step 4: Add `printReportBodyHist` and use it**

Directly after `printReportBody()` add:

```c
// ---------------------------------------------------------------------------
// Outputs markdown. -g mode: one row per test, the first count is for the
// winner delay (in parentheses), the next for winner+1, and so on.
void printReportBodyHist(void)
{
    print(g_szTitle3);
    print(g_szEmptyLine);

    u8 szShortBuff[10];
    const u8 szCyclesTemplate[] = "(%hu)%c";

    u8 szShortBuff2[10];
    const u8 szStub[] = "%hu ";

    for(u8 t = 0; t < NUM_TESTS; t++)
    {
        sprintf(g_auBuffer,
                g_szHistHeader1,
                aTEST_NAME[t]
               );

        print(g_auBuffer);
        print(g_szHistHeader2);

        Histogram* pHist = &g_aoHistogram[t][g_uHistU][g_uHistV];

        u8* pHistTail = g_auHistStrBuf; // append position, so we never rescan the string
        *pHistTail = '\0';              // reset string buffer

        if(pHist->nHistogramLength == 0)
        {
            szShortBuff[0] = '(';
            szShortBuff[1] = '?';
            szShortBuff[2] = ')';
            szShortBuff[3] = '\0';
        }
        else
        {
            sprintf(szShortBuff, szCyclesTemplate, pHist->nHistogramStartValue, (char)'\0');

            u16 nShow = pHist->nHistogramLength;
            if(nShow > HIST_DISPLAY_LEN)
                nShow = HIST_DISPLAY_LEN;

            for(u16 x = 0; x < nShow; x++) // max 16 * "65535 " fits easily in g_auHistStrBuf
            {
                sprintf(szShortBuff2, szStub, pHist->panHistogram[x]);
                strcpy(pHistTail, szShortBuff2);
                pHistTail += strlen(szShortBuff2);
            }
        }

        sprintf(g_auBuffer,
                g_szHistResultLine,
                aHORZ_LEN[g_uHistU],
                aVERT_LEN[g_uHistV],
                szShortBuff
               );

        print(g_auBuffer);
        print(g_auHistStrBuf);
        print(g_szEmptyLine); // "\r\n"
        print(g_szEmptyLine);
    }
}
```

In `main`, replace

```c
    printReportTop(&oTimestamp0, false);
    printReportBody();
```

with

```c
    printReportTop(&oTimestamp0, g_bHistogramMode);

    if(g_bHistogramMode)
        printReportBodyHist();
    else
        printReportBody();
```

- [ ] **Step 5: Build and help check**

Run: `cmd //c build.bat`
Expected: no errors.

Run: `python <scratchpad>/check_help.py src/main.c`
Expected: `PASS` (main help shows `Version: 0.83`).

- [ ] **Step 6: Runtime check on a T976x machine (human)**

`run.bat` (FS-A1GT). At `A>`: `cmdcycle -g 8 8 FS-A1GT > g88.md`, then compare with the existing `dska/fs-a1gt.md`.
Expected in `dska/g88.md`:
- The header line has `v0.83` and `FS-A1GT`, and there is a `* Histogram type: True histogram` line.
- `## HISTOGRAM` followed by 6 blocks in `aTEST_NAME` order.
- Each row looks like `| 8x8 (NNNN)          | c1 c2 ... c32 `, with exactly 32 counts (fewer only if the scan was shorter).
- The winner is roughly equal to the 8x8 cell (bottom right) of the matching CYCLES table in `fs-a1gt.md` (2034, 1427, 1346, 757, 1597, 213), give or take a few cycles. The first count is small, the counts mostly increase, and the last counts approach 2496 (full budget with defaults).
- The `Duration test` line is plausible (several minutes) and no `ERROR:` line is printed.

- [ ] **Step 7: Runtime check on a non-T976x machine (human)**

In `run.bat` set `MACHINE=Philips_NMS_8255` (V9938) and run `cmdcycle -g 8 8 NMS8255 > g88b.md`.
Expected: same structure, no `(T976x)` in the header, plausible winners.

- [ ] **Step 8: Runtime check with small and edge sizes (human)**

On FS-A1GT: `cmdcycle -g 1 1 > g11.md` and `cmdcycle -g 1 8 > g18.md`.
Expected: the run completes (no hang), and each row has either a winner and counts or `(?)`. The winners are about the same as the 1x1 and 1x8 CYCLES cells in `fs-a1gt.md` (for example 15 for LMMM 1x1).

- [ ] **Step 9: Runtime check with VBLANK sync and modes (human)**

On FS-A1GT: `cmdcycle -g 8 8 -t > g88t.md`, `cmdcycle -g 4 4 -3 > g44n.md`, `cmdcycle -g 8 8 -i 16 -o 1 > g88s.md`.
Expected:
- `g88t.md` header says `Sync method: VBLANK interrupt`.
- `g44n.md` says `mode: No display`.
- In `g88s.md` the header shows `(-i:16 -o:1)` and the highest counts are 208 (16 × 1 × 13).
- All of them complete with plausible rows.

- [ ] **Step 10: Normal mode regression (human)**

`cmdcycle > norm.md` (long run).
Expected: same structure as `fs-a1gt.md` (CYCLES + FAKE HISTOGRAM, with the `* No real histogram ...` line), and values in the same range.

- [ ] **Step 11: Leave changes uncommitted**

Do **not** commit. Report `git status --short` and `git diff --stat` to the user.
