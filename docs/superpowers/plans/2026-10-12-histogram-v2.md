# True Histogram v2 (`-g x y k`, phase-swept) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace v1's histogram measurement with a phase-swept one. For each delay value, k sweeps of 228 attempts start at exactly the positions 0..227 within a display line. The report shows the real histogram (count per exact duration) and a normalized version.

**Architecture:** A new `src/histogram.s` does the tight, cycle-exact work:
- an HR-edge lock at the top of every frame (coarse sync, then a 229-cycle vernier, then a sync check);
- a free-running attempt loop with a calibrated period of `n * 228 + 1` cycles;
- the calibration helper.

`main.c` runs the calibration arithmetic, the scan (walk down to 0 successes, walk up to 100%), the command line and the report. `vdp.s` gets a STOP at the start of the parameter setup and a selectable colour.

**Tech Stack:** C (SDCC 4.5.24, `-mz80`, `__sdcccall(1)`), Z80 assembly (sdasz80/asxxxx syntax, `.allow_undocumented`), MSX-DOS, openMSX.

**Spec:** `docs/superpowers/specs/2026-10-12-histogram-v2-design.md`

## Global Constraints

- **NEVER run `git commit`, `git add` or `git push`.** The user commits manually. Leave all changes uncommitted.
- MSX Z80 timing: +1 per M1 cycle (`ld r,r'` 5, `ld r,n` 8, `in a,(n)` 12, `out (n),a` 12, `jp` / `jp cc` 11, `jr` 13/8, `djnz` 14/9, `call` 18, `ret` 11, `ld a,(nn)` 14, `ld hl,(nn)` 17, `exx` 5, CB/ED/DD-prefixed +2: `srl h` 10, `adc hl,de` 17, `ld a,i` 11, `ld r,ixh` 10).
- **T976x**: every IN/OUT on VDP ports 0x98–0x9B costs exactly +1. It is constant; no code compensates for variation. Hand-counted intervals are adjusted by `(number of VDP-port I/O in the interval) * 1` through `histInit`.
- Style:
  - C: Hungarian prefixes, `g_` globals, a `// ----` separator before each function, 4-space indentation, `for(u8 c = 0; ...)`.
  - asm: lowercase mnemonics, `#` immediates, `::` global labels, a `; ----` header with IN/OUT/MODIFIES per routine, cycle counts in comments.
- Assembly rules:
  - SDCC needs IX intact, so `push ix` / `pop ix` around every routine that uses IX.
  - Undocumented instructions are allowed. In tight loops use IXH/IXL/IYH/IYL instead of PUSH/POP, and no indexed access.
  - IX/IY as record pointers only outside tight loops.
- Existing routines `_runTestCombo`, `_runTestComboNoHalts`, `secureActiveArea` and `SECOND_LINE_INT` are **not** changed. `bioshelper.s` must end up identical to HEAD.
- Help: at most 29 columns per line, at most 22 lines. Main help 22 lines, histogram help 12 lines.
- `-g x y k`: k in 1..287 (`HIST_MAX_SWEEPS`), tries per delay value = `k * 228`. `-t` with `-g` is an error.
- No cap on the span except memory: scratch buffer 1024 values (ERROR_CODE 5 when reached), heap (ERROR_CODE 3).
- Version `"0.90"` (0.83 is reserved for something else).
- Build (Claude, from PowerShell): `Set-Location C:\source\repos\cmdcycles; cmd /c "C:\source\repos\cmdcycles\build.bat"`. The user builds with `build.bat` in cmd. `dska\cmdcycle.com` is tracked and being modified by builds is expected.
- Memory: every task records `l__CODE`, `l__DATA` and `s__HEAP` from `out\cmdcycle.map`. Baseline before this plan: `l__CODE` 0x395A, `l__DATA` 0x899F, `s__HEAP` 0xC666 (v1 included).

## Review Focus

1. **T976x hand-counted constants.** `vc_fine_t` (15) and `SYNC_WAIT1-1` / `SYNC_WAIT2-1` must give 0 sync misses on FS-A1GT, as the normal values must on NMS 8255. If they're wrong, every lock fails and the run never ends, or the misses grow quickly. Test: Task 5, Step 6.
2. **Calibration verification.** If the measured attempt length isn't stable, `histCalibrate` counts misses on every delay value. Expected: 0 misses in openMSX. Test: Task 5, Step 6.
3. **Small sizes (`-g 1 1 1`).** Delay values of 20..47 use `dispatch_vdp_test`'s direct stubs, a different code path per value. Calibration per delay value must absorb it, and many attempts per frame exercise the offset wrap with carry. Expected: no hang, every Count ≥ 0. Test: Task 5, Step 7.
4. **Lock too late in the frame.** If misses push the lock past the guard line, the frame must be skipped (post-lock FH check), not measured below line 211. That's visible indirectly as a plausible Minimum. Test: Task 5, Step 6 (inspect `hm_frame` post-lock check in review).
5. **CLI parsing with a sys name.** `cmdcycle -g 8 8 FS-A1GT` must give the k error, not swallow the name. `cmdcycle -g 8 8 4 FS-A1GT` must work. Test: Task 4, Step 5.

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `src/histogram.s` (new) | cycle-exact lock, vernier, attempt loop, calibration helper, `waitExact`, `getImageEnd` | create |
| `build.bat` | assemble + link `histogram.s` | 2 lines |
| `src/vdp.s` | STOP first in `setVDPCmdParamsNI_asm`, colour from `_g_uCmdColor` | small edit |
| `src/bioshelper.s` | remove v1 additions, so it is identical to HEAD again | delete blocks |
| `src/main.c` | v1 cleanup, `HistCombo`, calibration, scan, CLI/help, memory check, report | edits |

There is no automated test framework (the target is an MSX). The gates are:
- the build (`build.bat`, exit 0, no new warnings);
- `git diff` checks;
- the help-text checker in the scratchpad;
- map sizes;
- openMSX runs by the human at the end (exact commands and expectations are in Task 5).

---

### Task 1: `vdp.s` — STOP first and selectable colour

**Files:**
- Modify: `src/vdp.s:228-231` (start of `setVDPCmdParamsNI_asm`), `src/vdp.s:313-315` (colour)
- Modify: `src/main.c` (global `g_uCmdColor` and its initialisation)

**Interfaces:**
- Produces: `u8 g_uCmdColor;` in main.c, read by `vdp.s` as `_g_uCmdColor` (0 = `ld a,r` as before, otherwise a fixed colour). `setVDPCmdParamsNI_asm` now always writes R#46 = 0 first.

- [ ] **Step 1: Baseline build and map sizes**

Run: `Set-Location C:\source\repos\cmdcycles; cmd /c "C:\source\repos\cmdcycles\build.bat" > $env:TEMP\b1.log 2>&1; $LASTEXITCODE; Select-String -Path out\cmdcycle.map -Pattern "l__CODE|l__DATA|s__HEAP "`
Expected: `0`, plus the baseline values from Global Constraints (write them down).

- [ ] **Step 2: Add the global in `main.c`**

After `bool g_bHistogramMode; ...` (the `// -g x y` line) add:

```c
u8                      g_uCmdColor;            // R#44 for all VDP commands. 0: random (R reg), else fixed (read by vdp.s)
```

In `initVarsAndRig`, after `g_bHistogramMode    = false;` add:

```c
    g_uCmdColor         = 0;
```

- [ ] **Step 3: Edit `setVDPCmdParamsNI_asm` in `src/vdp.s`**

Replace

```asm
setVDPCmdParamsNI_asm:: ; call from asm using D: width, E: height, A = CMD (just for testing)
    ; Plan: Put NXL in D, NYL in E, SXL in H, SYL in L

	ld    	c, #VDPSTREAM
```

with

```asm
setVDPCmdParamsNI_asm:: ; call from asm using D: width, E: height, A = CMD (just for testing)
    ; Plan: Put NXL in D, NYL in E, SXL in H, SYL in L

    ld      b,a                 ; STOP any running command first (R#46 = 0), so registers are never
    xor     a                   ; streamed while a previous command is still executing
    vdpWriteReg 46
    ld      a,b

	ld    	c, #VDPSTREAM
```

Replace

```asm
    ld a,r                      ; for debugging
    ; dec     a                   ;set color 255, for visual debugging
	out     (VDPSTREAM),a       ;COLOR (44)
```

with

```asm
    ld      a,(_g_uCmdColor)    ; 0: random colour (R reg, for visual debugging), else fixed (-g mode)
    or      a
    jr      nz,color_is_set
    ld      a,r
color_is_set:
	out     (VDPSTREAM),a       ;COLOR (44)
```

Add one comment line above `_setVDPCmdParamsNI::` (the `; MODIFIES: AF, BC, DE, HL` line stays as it is):

```asm
; Starts with a STOP (R#46 = 0). Colour (R#44) comes from g_uCmdColor (0 = random).
```

- [ ] **Step 4: Build and record sizes**

Run the build command from Step 1.
Expected: exit `0`, only the two existing warnings (`function declarator with no prototype` and `conditional flow changed by optimizer`). Record `l__CODE`, `l__DATA`, `s__HEAP`; `l__CODE` should grow by about 0x10.

- [ ] **Step 5: Leave uncommitted**

Do not commit. Run `git diff --stat` and note it.

---

### Task 2: `src/histogram.s` — cycle-exact measurement routines

**Files:**
- Create: `src/histogram.s`
- Modify: `build.bat` (assemble line, link list)

**Interfaces:**
- Consumes:
  - `setVDPCmdParamsNI_asm` (vdp.s: A = cmd, D = W, E = H; MODIFIES AF, BC, DE, HL);
  - `dispatch_vdp_test` (cycle-test.z80.s: HL = d, C = cmd, returns A = S#2; MODIFIES AF, BC, DE, HL);
  - `prepareLineInterruptsNI` (bioshelper.s: R#19 = 0, IE1 on, IYH = 0; MODIFIES AF, IYH);
  - `_cleanupLineInterruptsNI` (bioshelper.s: IE1 off, flags cleared, S#0; MODIFIES A);
  - macros `vdpWriteReg`, `delay15`, `delay16`, `delay20`..`delay35` (macros_constants.inc).
- Produces (C prototypes for Task 3):
  ```c
  extern void     histInit(bool bT976x);                          // A
  extern u8       histPhaseAfterRun(HistCombo* p, u16 nTries);    // HL, DE -> A (0..228, 0xFF = failed)
  extern u16      histMeasureDelay(HistCombo* p);                 // HL -> DE (successes)
  extern u16      getImageEnd(void);                              // -> DE (s__HEAP)
  ```
  `HistCombo` offsets: 0 uW, 1 uH, 2 uCmd, 3 uGuardLine, 4-5 nDelay, 6-7 nTries, 8-9 nPad, 10-11 nSyncMisses.

- [ ] **Step 1: Create `src/histogram.s`**

```asm
; ============================================================================
; histogram.s - phase-swept VDP command measurement for the -g mode
; (true histogram). See docs/superpowers/specs/2026-10-12-histogram-v2-design.md
; Note: Any symbol to be reached via C in SDCC is prefixed with an underscore.
; Any parameters are passed according to __sdcccall(1) found here:
; https://sdcc.sourceforge.net/doc/sdccman.pdf
;
; Principle: a display line is exactly 228 Z80 cycles. At the top of every
; frame we lock to the HR edge (S#2 bit 5) with 1-cycle precision, then run
; attempts with a period of exactly n * 228 + 1 cycles, so each attempt starts
; 1 cycle later within the line than the previous one. k sweeps of 228
; attempts cover every start position k times.
;
; Cycle counts are MSX Z80 cycles (+1 per M1). On T976x systems every IN/OUT on
; the VDP ports costs exactly +1 more; hand-counted intervals get that from
; histInit, everything in the attempt loop is calibrated (see main.c).
;
; author: pal.hansen@gmail.com

    .allow_undocumented
    .area _CODE

.include "macros_constants.inc"

; ----------------------------------------------------------------------------
; LOCAL CONSTANTS

    HR_MASK             .equ 0x20               ; S#2 bit 5: HR
    LINE_LEN            .equ 228                ; Z80 cycles per display line
    VC_DJNZ             .equ 6                  ; djnz count in vernierCount
    LOCK_VC_CAP         .equ 34                 ; max vernier samples when locking (needs <= 31)
    PHASE_VC_CAP        .equ 240                ; max vernier samples when measuring a phase (needs <= 228)
    PRE_VERNIER_WAIT    .equ 135                ; waitExact N: coarse hit -> 1st vernier sample = 426 cycles
    SYNC_WAIT1          .equ 174                ; waitExact N: vernier hit -> 1st check sample = 455 cycles (2 lines - 1)
    SYNC_WAIT2          .equ 224                ; waitExact N: 1st check -> 2nd check sample = 457 cycles (2 lines + 1)
    OFFSET_BASE         .equ 48                 ; waitExact N for offset 0 at the start of a frame

; ----------------------------------------------------------------------------
; Exact wait. Same decomposition as dispatch_vdp_test's dynamic path, but
; without the N < 48 checks, so the overhead is the same for every N.
; IN:       HL - N (48..4095)
; COST:     N + 199 cycles, including the call and the ret
; MODIFIES: AF, B, DE, HL (C is preserved)
waitExact::
    ld      a,l                     ;  5
    and     #15                     ;  8   A = N & 15
    srl     h                       ; 10
    rr      l                       ; 10
    srl     h                       ; 10
    rr      l                       ; 10
    srl     h                       ; 10
    rr      l                       ; 10
    srl     h                       ; 10
    rr      l                       ; 10
    ld      b,l                     ;  5   B = N / 16
    dec     b                       ;  5
    dec     b                       ;  5   B = N / 16 - 2 (>= 1)
    add     a,a                     ;  5
    ld      e,a                     ;  5
    ld      d,#0                    ;  8
    ld      hl,#wait_exact_table    ; 11
    add     hl,de                   ; 12
    ld      a,(hl)                  ;  8
    inc     hl                      ;  7
    ld      h,(hl)                  ;  8
    ld      l,a                     ;  5
    jp      (hl)                    ;  5   = 182. Then 16 * B + (20 + R) + 11 (ret)

wait_exact_table:
.dw we_00, we_01, we_02, we_03
.dw we_04, we_05, we_06, we_07
.dw we_08, we_09, we_10, we_11
.dw we_12, we_13, we_14, we_15

we_00:
    dec     b
    jp      nz,we_00
    delay20
    ret
we_01:
    dec     b
    jp      nz,we_01
    delay21
    ret
we_02:
    dec     b
    jp      nz,we_02
    delay22
    ret
we_03:
    dec     b
    jp      nz,we_03
    delay23
    ret
we_04:
    dec     b
    jp      nz,we_04
    delay24
    ret
we_05:
    dec     b
    jp      nz,we_05
    delay25
    ret
we_06:
    dec     b
    jp      nz,we_06
    delay26
    ret
we_07:
    dec     b
    jp      nz,we_07
    delay27
    ret
we_08:
    dec     b
    jp      nz,we_08
    delay28
    ret
we_09:
    dec     b
    jp      nz,we_09
    delay29
    ret
we_10:
    dec     b
    jp      nz,we_10
    delay30
    ret
we_11:
    dec     b
    jp      nz,we_11
    delay31
    ret
we_12:
    dec     b
    jp      nz,we_12
    delay32
    ret
we_13:
    dec     b
    jp      nz,we_13
    delay33
    ret
we_14:
    dec     b
    jp      nz,we_14
    delay34
    ret
we_15:
    dec     b
    jp      nz,we_15
    delay35
    ret

; ----------------------------------------------------------------------------
; Vernier: samples HR once every 229 cycles (1 cycle later in the line each
; time) and stops at the first sample with HR == 1 that follows a sample with
; HR == 0. Hand-counted; 1 VDP-port I/O per period (the IN).
; IN:       C - HR_MASK, D - 0, E - 0 (not armed), (uVcCap) - max samples
;           (pVcFine) - vc_fine_n or vc_fine_t (histInit), S#2 selected
; OUT:      carry clear: hit. D = samples before the hit (0..228). The ret is
;           54 cycles (55 on T976x) after the start of the hit sample's IN.
;           carry set: more than (uVcCap) samples
; MODIFIES: AF, B, DE, HL
vernierCount::
vc_loop:
    in      a,(VDPPORT1)            ; 12 (13 on T976x)
    and     c                       ;  5   A = HR
    ld      b,a                     ;  5
    and     e                       ;  5   HR and armed
    jp      nz,vc_hit               ; 11
    ld      a,b                     ;  5
    xor     c                       ;  5   HR_MASK if HR == 0
    or      e                       ;  5
    ld      e,a                     ;  5   armed after the first sample with HR == 0 (sticky)
    inc     d                       ;  5
    ld      a,(uVcCap)              ; 14
    cp      d                       ;  5
    jp      c,vc_fail               ; 11   more than uVcCap samples
    ld      b,#VC_DJNZ              ;  8
vc_djnz:
    djnz    vc_djnz                 ; 14 * 6 - 5 = 79
    ld      hl,(pVcFine)            ; 17
    jp      (hl)                    ;  5   = 202 (203 on T976x)

vc_fine_n:                          ; normal: 202 + 16 + 11 = 229
    delay16
    jp      vc_loop                 ; 11

vc_fine_t:                          ; T976x: 203 + 15 + 11 = 229
    delay15
    jp      vc_loop                 ; 11

vc_hit:
    or      a                       ;  5   carry clear
    ret                             ; 11   (hit IN start -> here incl. ret: 54, 55 on T976x)

vc_fail:
    scf
    ret

; ----------------------------------------------------------------------------
; Lock to the HR edge: coarse sync (25-cycle samples), vernier (1-cycle
; steps), then a sync check: one sample 2 lines - 1 after the vernier hit
; (must be HR == 0), one sample 4 lines after the hit (must be HR == 1).
; Misses (vernier cap or sync check) increment HistCombo.nSyncMisses and retry.
; IN:       IX - HistCombo*, S#2 selected, DI
; OUT:      returns 54 cycles (55 on T976x) after the start of the 2nd check
;           sample's IN, which is exactly on the HR edge (the lock reference)
; MODIFIES: AF, BC, DE, HL
histLock::
    ld      c,#HR_MASK

hl_wait0:                           ; no timing requirement: wait until HR == 0
    in      a,(VDPPORT1)
    and     c
    jp      nz,hl_wait0

hl_coarse:                          ; 25 cycles per sample (26 on T976x): 24 x (in/and/jr nz) + in/and/jp z
    in      a,(VDPPORT1)            ; 1
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 2
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 3
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 4
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 5
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 6
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 7
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 8
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 9
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 10
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 11
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 12
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 13
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 14
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 15
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 16
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 17
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 18
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 19
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 20
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 21
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 22
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 23
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 24
    and     c
    jr      nz,hl_coarse_hit
    in      a,(VDPPORT1)            ; 25 (loops back: this sample interval is 28)
    and     c
    jp      z,hl_coarse

hl_coarse_hit:                      ; the HR edge was within the last 25 (26) cycles
    ld      de,#0                   ; 11   D = samples, E = not armed
    ld      a,#LOCK_VC_CAP          ;  8
    ld      (uVcCap),a              ; 14
    ld      hl,#PRE_VERNIER_WAIT    ; 11
    call    waitExact               ; 135 + 199. Coarse hit IN -> 1st vernier IN = 426:
                                    ; 5..30 cycles before the edge 2 lines later
    call    vernierCount            ; 18
    jp      c,hl_miss               ; 11

    ld      hl,(nSyncWait1)         ; 17
    call    waitExact               ; hit IN -> here: 54 + 11 + 17 + N + 199 = 455 with N = SYNC_WAIT1 (-1 on T976x)
    in      a,(VDPPORT1)            ; 12   edge - 1, 2 lines later: expect HR == 0
    ld      e,a                     ;  5
    ld      hl,(nSyncWait2)         ; 17
    call    waitExact               ; 12 + 5 + 17 + N + 199 = 457 with N = SYNC_WAIT2 (-1 on T976x)
    in      a,(VDPPORT1)            ; 12   edge, 4 lines after the hit: expect HR == 1 (lock reference)
    and     c                       ;  5
    jr      z,hl_miss               ;  8
    ld      a,e                     ;  5
    and     c                       ;  5
    jr      nz,hl_miss              ;  8
    ret                             ; 11   lock reference IN start -> ret done: 12 + 5 + 8 + 5 + 5 + 8 + 11 = 54 (constant)

hl_miss:
    ld      l,10(ix)                ; nSyncMisses++
    ld      h,11(ix)
    inc     hl
    ld      10(ix),l
    ld      11(ix),h
    jp      hl_wait0

; ----------------------------------------------------------------------------
; Loads the per-delay values from HistCombo into fixed memory.
; IN:       IX - HistCombo*
; MODIFIES: HL
hist_load_params:
    ld      l,4(ix)
    ld      h,5(ix)
    ld      (nHfDelay),hl
    ld      l,8(ix)
    ld      h,9(ix)
    ld      (nHfPad),hl
    ret

; ----------------------------------------------------------------------------
; Loads the attempt loop's 8-bit registers. After this IX is no longer a
; pointer (reload it from pHistParams).
; IN:       IX - HistCombo*
; OUT:      IXH - W, IXL - H, IYH - command
; MODIFIES: A, DE, IX, IYH
hist_load_regs:
    ld      e,0(ix)
    ld      d,1(ix)
    ld      a,2(ix)
    ld      iyh,a
    ld      ixh,e
    ld      ixl,d
    ret

; ----------------------------------------------------------------------------
; The attempt loop. No PUSH/POP, no indexed access, no VDP-dependent branch:
; every pass takes exactly the same time L (calibrated to n * 228 + 1).
; IN:       IXH - W, IXL - H, IYH - command, IYL - attempts this frame
;           BC' - remaining attempts (> 0), DE' - 0, HL' - successes
;           (nHfDelay), (nHfPad), (uFhMask), S#2 selected, DI
; OUT:      carry clear: all attempts done
;           carry set: guard line reached (FH), IYL = attempts this frame
; MODIFIES: AF, BC, DE, HL, IYL, BC', HL'
hfRun::
hf_try:
    ld      d,ixh                   ; W
    ld      e,ixl                   ; H
    ld      a,iyh                   ; command
    call    setVDPCmdParamsNI_asm   ; STOP + parameters (MODIFIES AF, BC, DE, HL)
    ld      hl,(nHfDelay)
    ld      c,iyh
    call    dispatch_vdp_test       ; OUT, exactly d cycles, IN -> A = S#2
    ld      b,a
    xor     a
    vdpWriteReg 46                  ; STOP right after the measurement
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
    inc     iyl                     ; attempts this frame
    ld      a,#1
    vdpWriteReg 15                  ; S#1
    in      a,(VDPPORT1)            ; FH in bit 0 (reading clears it)
    ld      b,a
    ld      a,#2
    vdpWriteReg 15                  ; S#2
    ld      a,(uFhMask)
    and     b                       ; FH, masked (0 during calibration)
    jp      nz,hf_frame_end
    ld      hl,(nHfPad)
    call    waitExact               ; pad: makes the period n * 228 + 1
    jp      hf_try

hf_all_done:
    or      a                       ; carry clear
    ret

hf_frame_end:
    scf
    ret

; ----------------------------------------------------------------------------
; Sets the T976x-dependent constants of the hand-counted intervals. Each of
; those intervals contains exactly 1 VDP-port IN.
; IN:       A - bT976x
; MODIFIES: AF, HL
; void histInit(bool bT976x);
_histInit::
    or      a
    jr      nz,hi_t976x

    ld      hl,#vc_fine_n
    ld      (pVcFine),hl
    ld      hl,#SYNC_WAIT1
    ld      (nSyncWait1),hl
    ld      hl,#SYNC_WAIT2
    ld      (nSyncWait2),hl
    ret

hi_t976x:
    ld      hl,#vc_fine_t
    ld      (pVcFine),hl
    ld      hl,#SYNC_WAIT1-1
    ld      (nSyncWait1),hl
    ld      hl,#SYNC_WAIT2-1
    ld      (nSyncWait2),hl
    ret

; ----------------------------------------------------------------------------
; Calibration helper: lock, run nTries attempts (guard line ignored) with the
; pad in HistCombo.nPad, then measure where in the line we are.
; IN:       HL - HistCombo*, DE - nTries (1 or 2)
; OUT:      A - vernier samples until the HR edge (0..228), 0xFF if failed
; MODIFIES: AF, BC, DE, HL, IY, BC', DE', HL'
; u8 histPhaseAfterRun(HistCombo* p, u16 nTries); HL, DE
_histPhaseAfterRun::
    push    ix

    push    hl
    pop     ix                      ; IX = p (record pointer, outside the loop)
    ld      (pHistParams),hl
    call    hist_load_params

    xor     a
    ld      (uFhMask),a             ; ignore the guard line

    ld      b,d
    ld      c,e
    ld      de,#0
    ld      hl,#0
    exx                             ; BC' = nTries, DE' = 0, HL' = 0

    di
    call    prepareLineInterruptsNI
    ld      a,#2
    vdpWriteReg 15                  ; S#2

    call    histLock
    call    hist_load_regs
    ld      iyl,#0
    call    hfRun                   ; ends through hf_all_done

    ld      c,#HR_MASK
    ld      de,#0
    ld      a,#PHASE_VC_CAP
    ld      (uVcCap),a
    call    vernierCount
    ld      a,d
    jr      nc,hp_ok
    ld      a,#0xFF
hp_ok:
    push    af                      ; outside any timed path
    call    _cleanupLineInterruptsNI
    pop     af
    ei

    pop     ix
    ret

; ----------------------------------------------------------------------------
; Measures one delay value: k * 228 attempts, frame by frame. Each frame:
; wait for line 0, guard line in R#19, lock, offset wait, attempt loop until
; the guard line. The offset carries over between frames, so each sweep
; covers the start positions 0..227 exactly once.
; IN:       HL - HistCombo* (nPad from calibration)
; OUT:      DE - successes out of nTries
; MODIFIES: AF, BC, DE, HL, IY, BC', DE', HL'
; u16 histMeasureDelay(HistCombo* p); HL
_histMeasureDelay::
    push    ix

    push    hl
    pop     ix
    ld      (pHistParams),hl
    call    hist_load_params

    xor     a
    ld      (uHfOffset),a
    inc     a
    ld      (uFhMask),a             ; honour the guard line

    ld      c,6(ix)
    ld      b,7(ix)                 ; nTries
    ld      de,#0
    ld      hl,#0
    exx                             ; BC' = remaining, DE' = 0, HL' = successes

    di
    call    prepareLineInterruptsNI

hm_frame:
    ld      ix,(pHistParams)        ; IX = p again (the loop used IXH/IXL)

    xor     a
    vdpWriteReg 19                  ; line interrupt at line 0
    ld      a,#1
    vdpWriteReg 15                  ; S#1
    in      a,(VDPPORT1)            ; clear FH
hm_wait_line0:
    in      a,(VDPPORT1)
    rra
    jp      nc,hm_wait_line0        ; top of the active area

    ld      a,3(ix)
    vdpWriteReg 19                  ; line interrupt at the guard line
    in      a,(VDPPORT1)            ; clear FH (S#1 still selected)
    ld      a,#2
    vdpWriteReg 15                  ; S#2

    call    histLock

    ld      a,#1                    ; lock done above the guard line? (same cost when yes)
    vdpWriteReg 15
    in      a,(VDPPORT1)
    ld      b,a
    ld      a,#2
    vdpWriteReg 15
    bit     0,b
    jp      nz,hm_frame             ; too late (misses): next frame

    call    hist_load_regs
    ld      a,(uHfOffset)
    ld      l,a
    ld      h,#0
    ld      de,#OFFSET_BASE
    add     hl,de
    call    waitExact               ; offset i: first attempt at position i
    ld      iyl,#0
    call    hfRun
    jp      nc,hm_done

    ld      a,(uHfOffset)           ; next frame continues the sweep: i = (i + IYL) mod 228
    add     a,iyl
    jr      c,hm_wrap_carry
    cp      #LINE_LEN
    jr      c,hm_store
    sub     #LINE_LEN
    jr      hm_store
hm_wrap_carry:
    add     a,#256-LINE_LEN         ; (A + 256) - 228
hm_store:
    ld      (uHfOffset),a
    jp      hm_frame

hm_done:
    call    _cleanupLineInterruptsNI
    ei
    exx                             ; HL = successes
    ex      de,hl                   ; SDCC needs return in DE
    pop     ix
    ret

; ----------------------------------------------------------------------------
; u16 getImageEnd(void);  returns the first free address after the program image
_getImageEnd::
    ld      de,#s__HEAP
    ret

; ----------------------------------------------------------------------------
    .area _DATA

pHistParams:
    .dw 0                           ; HistCombo* while IX is used as IXH/IXL
nHfDelay:
    .dw 0                           ; d for dispatch_vdp_test
nHfPad:
    .dw 0                           ; waitExact N at the end of each attempt
uFhMask:
    .db 0                           ; 1: stop at the guard line, 0: ignore it (calibration)
uHfOffset:
    .db 0                           ; start position of the next frame's first attempt (0..227)
uVcCap:
    .db 0                           ; vernierCount: max samples
pVcFine:
    .dw 0                           ; vernierCount: vc_fine_n or vc_fine_t (histInit)
nSyncWait1:
    .dw 0                           ; histLock: SYNC_WAIT1 (-1 on T976x)
nSyncWait2:
    .dw 0                           ; histLock: SYNC_WAIT2 (-1 on T976x)
```

- [ ] **Step 2: Add `histogram.s` to `build.bat`**

After the line `sdasz80 -o -s -p -g -w out\vdpwaits.rel src\vdpwaits.s` add:

```bat
sdasz80 -o -s -p -g -w out\histogram.rel src\histogram.s
```

In the active `sdcc ...` line (the one without `@REM`), insert `out\histogram.rel` after `out\vdpwaits.rel`:

```bat
sdcc --code-loc 0x0180 --data-loc 0 -mz80 --no-std-crt0 --opt-code-speed  out\dos_crt.rel out\vdp.rel out\bioshelper.rel out\cycle-test.z80.rel out\vdpwaits.rel out\histogram.rel src\main.c -o out\%NAME%.ihx
```

- [ ] **Step 3: Build**

Run the build command.
Expected: exit 0, no `Error:` lines from `sdasz80 ... histogram.s`, and the same two warnings as before. If sdasz80 rejects an undocumented instruction (`ld ixh,e`, `ld ixl,d`, `add a,iyl`), use `.db` encodings with a comment: `ld ixh,e` = `0xDD,0x63`, `ld ixl,d` = `0xDD,0x6A`, `add a,iyl` = `0xFD,0x85`. Record that as a ruling.

- [ ] **Step 4: Verify the cycle-critical encodings in a listing**

Run: `Set-Location C:\source\repos\cmdcycles; sdasz80 -o -l -g -w "$env:TEMP\hg.rel" src\histogram.s; Select-String -Path "$env:TEMP\hg.lst" -Pattern "vc_loop::|vc_fine_n:|vc_fine_t:|hl_coarse:|hl_coarse_hit:" -Context 0,3`
Expected:
- the listing shows `vc_loop` starting with `DB 99` (`in a,(0x99)`);
- `vc_fine_n` expands to two `BE` (`cp (hl)`) bytes;
- `vc_fine_t` expands to `BE 23` (`cp (hl)`, `inc hl`);
- `hl_coarse_hit` comes right after the `jp z` (no gap).

Then delete the temporary files.

- [ ] **Step 5: Record sizes, leave uncommitted**

Record `l__CODE`, `l__DATA` and `s__HEAP`. Expected: `l__CODE` +~0x350. Do not commit.

---

### Task 3: Remove v1, add `HistCombo`, calibration and the scan (`main.c`, `bioshelper.s`)

**Files:**
- Modify: `src/bioshelper.s` (delete v1 blocks)
- Modify: `src/main.c` (defines, typedefs, externs, globals, `recordAndClearHistogram`, `runTest`, `runHistTest`, new `histPhaseDiff`, `histCalibrate`, `histMeasure`)

**Interfaces:**
- Consumes: `histInit`, `histPhaseAfterRun`, `histMeasureDelay` (Task 2), `g_uCmdColor` (Task 1).
- Produces (for Tasks 4 and 5):
  - `u16 g_nSweeps;` (k)
  - `u16 g_nTotalSyncMisses;`
  - `bool g_abHistTruncated[NUM_TESTS];`
  - `#define HIST_SWEEP_LEN 228`, `#define HIST_MAX_SWEEPS 287`
  - `g_aoHistogram[c][u][v]` for the -g size: `panHistogram` = CDF counts per delay value from `nHistogramStartValue` (the minimum, incl. `nAdd`) upward, `nHistogramLength` values (0 = none).

- [ ] **Step 1: Remove v1 from `bioshelper.s`**

Delete exactly these three blocks:
1. From the comment line `; Same wait as the inline METHOD_WAIT blocks in x_runTestComboInner and` through the `.endm` of `.macro controlledWaitByMethod`, including the blank line before the comment.
2. From the `; ---` header line above `; Called in EI. True histogram variant of runTestComboNoHalts.` through the `ret` after `hn_done::` (the blank line before the header too).
3. From the `; ---` header line above `; Called in EI. True histogram variant of runTestCombo (with halts).` through the `ret` after `hv_done::`.

Run: `git diff HEAD --stat -- src/bioshelper.s`
Expected: **no output** (identical to HEAD). If there is output, show `git diff HEAD -- src/bioshelper.s` and fix the leftovers. Never restore with `git checkout`.

- [ ] **Step 2: Defines, typedef, externs, globals in `main.c`**

Replace the three lines

```c
#define NUM_WAIT_ROUNDS     13      // wait indexes 0..12 per delay value. Must match "#12" (last wait index) in bioshelper.s
#define HIST_DISPLAY_LEN    32      // -g: max counts shown per test in the report
#define HIST_START_MARGIN   33      // -g: safety, histogram scan starts at W + this (W may be a lucky hit)
```

with

```c
#define NUM_WAIT_ROUNDS     13      // wait indexes 0..12 per delay value. Must match "#12" (last wait index) in bioshelper.s
#define HIST_DISPLAY_LEN    32      // -g: max counts shown per test in the report (v1 report, removed in Task 5)

#define HIST_SWEEP_LEN      228     // -g: Z80 cycles per display line = start positions per sweep
#define HIST_MAX_SWEEPS     287     // -g: 287 * 228 = 65436, fits u16
#define HIST_MIN_DELAY      20      // -g: dispatch_vdp_test can't make all delay values below 20
#define HIST_OVERHEAD_MAX   1400    // -g: safe upper bound in cycles for one attempt minus d (setup, checks, pad)
#define HIST_PAD_MIN        48      // -g: waitExact minimum (histogram.s)
#define HIST_CALIB_TRIES    3       // -g: calibration attempts per delay value
#define HIST_CMD_COLOR      0xAA    // -g: fixed colour for all commands (no colour-dependent timing)
```

(`NUM_WAIT_ROUNDS` and `HIST_DISPLAY_LEN` stay until Task 4/5 remove their last users.)

After the `Histogram` typedef add:

```c
typedef struct {
    u8                      uW;             // 0
    u8                      uH;             // 1
    u8                      uCmd;           // 2
    u8                      uGuardLine;     // 3     last line an attempt may start on
    u16                     nDelay;         // 4 & 5
    u16                     nTries;         // 6 & 7 k * 228
    u16                     nPad;           // 8 & 9 waitExact N per attempt, from calibration
    u16                     nSyncMisses;    // 10 & 11 accumulated by histogram.s and histCalibrate
} HistCombo;
```

Replace

```c
extern u16      runTestComboHist(RunCombo* p, u16* panHistogramLastIndex);
extern u16      runTestComboHistNoHalts(RunCombo* p, u16* panHistogramLastIndex);
```

with nothing, and after the `// from vdpwaitasm.s` block add:

```c

// from histogram.s
extern void     histInit(bool bT976x);
extern u8       histPhaseAfterRun(HistCombo* p, u16 nTries); // samples until the HR edge, 0xFF = failed
extern u16      histMeasureDelay(HistCombo* p);              // successes out of p->nTries
extern u16      getImageEnd(void);
```

After the `u8 g_uCmdColor;` line (Task 1) add:

```c
u16                     g_nSweeps;              // -g: k
u16                     g_nTotalSyncMisses;     // -g: sync misses over the whole run
bool                    g_abHistTruncated[NUM_TESTS]; // -g: span hit the scratch buffer limit
```

In `initVarsAndRig`, after `g_uCmdColor         = 0;` add:

```c
    g_nSweeps           = 1;
    g_nTotalSyncMisses  = 0;
    memset(g_abHistTruncated, 0, sizeof(g_abHistTruncated));
```

- [ ] **Step 3: Revert `recordAndClearHistogram` to its original form**

Replace the comment and signature

```c
// Copy the actual histogram to a new buffer and store a pointer to it in the
// RunCombo struct, as well as some key data. bTrimTrailingOnes: fake histogram
// only, drop the trailing streak of 1s.
bool recordAndClearHistogram(RunCombo* pParams, u16* panHistogramLastIndex, u16 nBestDelay, Histogram* pRecord, bool bTrimTrailingOnes)
```

with

```c
// Copy the actual histogram to a new buffer and store a pointer to it in the
// RunCombo struct, as well as some key data
bool recordAndClearHistogram(RunCombo* pParams, u16* panHistogramLastIndex, u16 nBestDelay, Histogram* pRecord)
```

and replace

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

with

```c
    while(p >= pHistogramWinner) // find the first of the last 1s in a streak of 1s. If none, use last element
    {
        if( *p > 1 )
            break;

        pLast = p;
        p--;
    }
```

In `runTest`, change `..., &g_aoHistogram[c][u][v], true))` to `..., &g_aoHistogram[c][u][v]))`.

Run: `git diff HEAD -- src/main.c | Select-String -Pattern "recordAndClearHistogram|streak"`
Expected: no `+`/`-` lines mentioning `bTrimTrailingOnes`, i.e. the function equals HEAD.

- [ ] **Step 4: Replace v1 `runHistTest` with calibration and scan**

Replace the whole v1 block from `// Phase 2 of -g mode: real histogram, starting a bit above the delay found by` through the closing `}` of `bool runHistTest(void)` with:

```c
// (s1 - s2) mod 228, for vernier sample counts 0..228 (228 is the same as 0)
u16 histPhaseDiff(u8 s1, u8 s2)
{
    u16 n1 = (s1 >= HIST_SWEEP_LEN) ? (u16)(s1 - HIST_SWEEP_LEN) : s1;
    u16 n2 = (s2 >= HIST_SWEEP_LEN) ? (u16)(s2 - HIST_SWEEP_LEN) : s2;

    return (n1 >= n2) ? (n1 - n2) : (n1 + HIST_SWEEP_LEN - n2);
}

// ---------------------------------------------------------------------------
// Finds the pad that makes one attempt exactly n * 228 + 1 cycles long, and
// verifies it (see histogram.s). Stores it in p->nPad. Every failed attempt
// counts as a sync miss; after HIST_CALIB_TRIES the last pad is used.
bool histCalibrate(HistCombo* p)
{
    for(u8 n = 0; n < HIST_CALIB_TRIES; n++)
    {
        p->nPad = HIST_PAD_MIN;

        u8 s1 = histPhaseAfterRun(p, 1);
        u8 s2 = histPhaseAfterRun(p, 2);

        if((s1 != 0xff) && (s2 != 0xff))
        {
            u16 nMod = histPhaseDiff(s1, s2); // attempt length mod 228 with pad HIST_PAD_MIN

            p->nPad = HIST_PAD_MIN + ((nMod <= 1) ? (1 - nMod) : (HIST_SWEEP_LEN + 1 - nMod));

            s1 = histPhaseAfterRun(p, 1);
            s2 = histPhaseAfterRun(p, 2);

            if((s1 != 0xff) && (s2 != 0xff) && (histPhaseDiff(s1, s2) == 1))
                return true;
        }

        p->nSyncMisses++;
    }

    return false;
}

// ---------------------------------------------------------------------------
// One delay value: guard line, calibration, measurement.
// Returns the number of successes out of p->nTries.
u16 histMeasure(HistCombo* p, u16 nDelay)
{
    p->nDelay = nDelay;
    p->uGuardLine = (u8)(211 - (nDelay + HIST_OVERHEAD_MAX) / HIST_SWEEP_LEN - 2); // an attempt started on this line ends by line 211

    histCalibrate(p); // on failure the last pad is used, the misses show in the report

    return histMeasureDelay(p);
}

// ---------------------------------------------------------------------------
// Phase 2 of -g mode: for each test, walk down from W (phase 1) until no
// attempt succeeds, then up until all attempts succeed. Stores the CDF counts
// (successes per delay value) of the span in g_aoHistogram.
bool runHistTest(void)
{
    HistCombo oParams;

    oParams.uW          = aHORZ_LEN[g_uHistU];
    oParams.uH          = aVERT_LEN[g_uHistV];
    oParams.nTries      = g_nSweeps * HIST_SWEEP_LEN;
    oParams.nSyncMisses = 0;

    u16 nAdd = g_bHasT976xEngine ? 1 : 0; // see runTest

    histInit(g_bHasT976xEngine);
    g_uCmdColor = HIST_CMD_COLOR;

    bool bOk = true;

    for(u8 c = 0; c < NUM_TESTS; c++)
    {
        oParams.uCmd = aEXECUTE_CMD[c];

        Histogram* pRecord = &g_aoHistogram[c][g_uHistU][g_uHistV];
        u16 nStart = g_anResultDelays[c][g_uHistU][g_uHistV] - nAdd; // raw W

        // Walk down: scratch[k] = successes for nStart - k
        u16 nLen   = 0;
        u16 nDelay = nStart;
        while(true)
        {
            u16 nCount = histMeasure(&oParams, nDelay);
            g_anScratchHistogram[nLen++] = nCount;

            if((nCount == 0) || (nDelay <= HIST_MIN_DELAY))
                break;

            if(nLen >= SCRATCH_HIST_SIZE)
            {
                g_abHistTruncated[c] = true;
                ERROR_CODE = 5;
                break;
            }

            nDelay--;
        }

        u16 nLowest = nDelay;

        for(u16 a = 0, b = nLen - 1; a < b; a++, b--) // reverse: index 0 = nLowest
        {
            u16 nTmp = g_anScratchHistogram[a];
            g_anScratchHistogram[a] = g_anScratchHistogram[b];
            g_anScratchHistogram[b] = nTmp;
        }

        // Walk up until all attempts succeed
        nDelay = nStart;
        while(g_anScratchHistogram[nLen - 1] != oParams.nTries)
        {
            if(nLen >= SCRATCH_HIST_SIZE)
            {
                g_abHistTruncated[c] = true;
                ERROR_CODE = 5;
                break;
            }

            nDelay++;
            g_anScratchHistogram[nLen++] = histMeasure(&oParams, nDelay);
        }

        // The span starts at the first delay value with any success
        u16 nFirst = 0;
        while((nFirst < nLen) && (g_anScratchHistogram[nFirst] == 0))
            nFirst++;

        u16 nSpan = nLen - nFirst;

        if(nSpan == 0)
            pRecord->nHistogramLength = 0; // printed as "(?)"
        else
        {
            u16* pSpan = myMalloc(nSpan);

            if(pSpan == NULL)
            {
                ERROR_CODE = 3;
                bOk = false;
            }
            else
            {
                memcpy(pSpan, &g_anScratchHistogram[nFirst], nSpan * sizeof(u16));

                pRecord->panHistogram         = pSpan;
                pRecord->nHistogramLength     = nSpan;
                pRecord->nHistogramStartValue = nLowest + nFirst + nAdd;
            }
        }

        memset(g_anScratchHistogram, 0, nLen * sizeof(u16));

        if(!bOk)
            break;
    }

    g_uCmdColor = 0;
    g_nTotalSyncMisses = oParams.nSyncMisses;

    return bOk;
}
```

- [ ] **Step 5: Build**

Run the build command.
Expected: exit 0, only the two existing warnings. The v1 `printReportBodyHist` still compiles, because `HIST_DISPLAY_LEN` is kept.

Run: `Select-String -Path src\main.c -Pattern "runTestComboHist|HIST_START_MARGIN|bTrimTrailingOnes"`
Expected: no matches.

- [ ] **Step 6: Record sizes, leave uncommitted**

Record `l__CODE`, `l__DATA`, `s__HEAP`. Do not commit.

---

### Task 4: Command line, help texts, memory check (`main.c`)

**Files:**
- Modify: `src/main.c` (help strings, `printHelpHist`, `commandLineParameters`, `main`)
- Test helper (scratchpad): `<scratchpad>/check_help.py` (exists from v1; argument tuple changes)

**Interfaces:**
- Consumes: `g_nSweeps`, `HIST_MAX_SWEEPS`, `getImageEnd`.
- Produces: `#define STACK_RESERVE 512`.

- [ ] **Step 1: Update the help checker (failing test)**

In `<scratchpad>/check_help.py`, change the `checks` entry for `g_szHelptextHist` to `("g_szHelptextHist", (8, 8, 287)),`, change `except ValueError:` to `except (ValueError, TypeError):` (a format/argument mismatch then reports instead of crashing), and add these lines before `print("PASS" if ok else "FAIL")`:

```python
first = get_c_string("g_szHelptext").split("\n")[0]
if first != "Usage:cmdcycle [opt] [sys]":
    print("FAIL main usage line: " + repr(first)); ok = False
try:
    hist = get_c_string("g_szHelptextHist") % (8, 8, 287)
except (ValueError, TypeError):
    hist = ""
if len(hist.split("\n")) != 12 or "k: 1..287 sweeps of 228" not in hist:
    print("FAIL hist help content"); ok = False
```

Run: `python <scratchpad>/check_help.py src/main.c`
Expected: exit code 1, with `FAIL g_szHelptextHist: not found` (3 arguments for a text with only 2 `%d`), `FAIL main usage line: 'Usage: cmdcycle [opt][sys]'` and `FAIL hist help content`.

- [ ] **Step 2: Help strings**

In `g_szHelptext` replace `"Usage: cmdcycle [opt][sys]\r\n"` with `"Usage:cmdcycle [opt] [sys]\r\n"` and `" -g x y Histogram (-h -g)\r\n"` with `" -g x y k Histogram (-h -g)\r\n"`.

Replace the whole `g_szHelptextHist` definition with:

```c
                                                //"                             " // 29 chars (turbo r)
const u8                g_szHelptextHist[]      = "Usage:cmdcycle -g x y k [sys]\r\n"
                                                  "\r\n"
                                                  "Histogram of the duration of\r\n"
                                                  "one command size (x*y) for\r\n"
                                                  "all tests.\r\n"
                                                  "\r\n"
                                                  "x: 1..%d (width)\r\n"
                                                  "y: 1..%d (height)\r\n"
                                                  "k: 1..%d sweeps of 228\r\n"
                                                  "\r\n"
                                                  "-i -o -1 -2 -3 apply as\r\n"
                                                  "usual. -t is not supported.";
```

In `printHelpHist`, change the `sprintf` to:

```c
    sprintf(g_auBuffer, g_szHelptextHist, NUM_HORIZONTALS, NUM_VERTICALS, HIST_MAX_SWEEPS);
```

- [ ] **Step 3: Parsing**

In the `-g` branch, replace `if(n + 2 >= argc)` with `if(n + 3 >= argc)`, and after the `y` check (before `g_bHistogramMode = true;`) add:

```c
                iVal = atoi(argv[++n]);
                if((iVal < 1) || (iVal > HIST_MAX_SWEEPS))
                {
                    sprintf(g_auBuffer, "ERROR:k for -g must be in range 1-%d", HIST_MAX_SWEEPS);
                    print(g_auBuffer);
                    return 1;
                }
                g_nSweeps = (u16)iVal;
```

Replace the whole budget check

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

with

```c
        if(g_bHistogramMode && g_bVBLANKTiming) // the histogram needs exact timing with interrupts off
        {
            print("ERROR:-t can't be used with -g");
            return 1;
        }
```

- [ ] **Step 4: Memory check**

After `#define LEEWAY ...` add:

```c
#define STACK_RESERVE       512     // bytes that must stay free between the program image and the top of the DOS program area
```

In `main`, directly after the `if(argc > 0) { ... }` block that calls `commandLineParameters`, add:

```c
    // ------------------------------------
    // Enough memory between the image and the stack?
    u16 nTop = *(u16*)0x0006; // top of the DOS program area (BDOS entry)
    u16 nEnd = getImageEnd();
    if((nTop < nEnd) || ((u16)(nTop - nEnd) < STACK_RESERVE))
    {
        print("ERROR:Not enough memory");
        return 1;
    }
```

- [ ] **Step 5: Checker, build, then runtime check (human)**

Run: `python <scratchpad>/check_help.py src/main.c`
Expected: `g_szHelptext: 22 lines`, `g_szHelptextHist: 12 lines`, no `TOO WIDE`, `PASS`.

Run the build. Expected: exit 0, only the two existing warnings.

Human, in openMSX (`run.bat`), at `A>`:

| Command | Expected |
|---|---|
| `cmdcycle -h` | 22 lines, first line `Usage:cmdcycle [opt] [sys]`, line ` -g x y k Histogram (-h -g)` |
| `cmdcycle -h -g` | 12 lines incl. `k: 1..287 sweeps of 228` and `usual. -t is not supported.` |
| `cmdcycle -g 8 8` | `ERROR:Missing value(s) for -g` |
| `cmdcycle -g 8 8 FS-A1GT` | `ERROR:k for -g must be in range 1-287` |
| `cmdcycle -g 8 8 0` / `-g 8 8 288` | the k range error |
| `cmdcycle -g 8 8 4 -t` and `cmdcycle -t -g 8 8 4` | `ERROR:-t can't be used with -g` |

- [ ] **Step 6: Record sizes, leave uncommitted**

Record `l__CODE`, `l__DATA`, `s__HEAP`. Do not commit.

---

### Task 5: Report header and body (`main.c`), full runtime checks

**Files:**
- Modify: `src/main.c` (constants, `printReportTop`, `printReportBodyHist`, remove `NUM_WAIT_ROUNDS`, `HIST_DISPLAY_LEN`, `g_szTopLine5` v1)

**Interfaces:**
- Consumes: `g_nSweeps`, `g_nTotalSyncMisses`, `g_abHistTruncated`, `g_aoHistogram` (CDF spans), `HIST_SWEEP_LEN`.

- [ ] **Step 1: Strings**

Replace `const u8 g_szTopLine5[] = "* Attempts per delay value: %u (-i * -o * %d)\r\n";` with:

```c
const u8                g_szTopLine2g[]         = "* VDP detected: %s%s, screen %d, mode: %s. (-i:%d -o:%d k=%u)\r\n";
const u8                g_szTopLine2xg[]        = "* Sync method: Line interrupts + HR vernier lock per frame\r\n";
const u8                g_szTopLine5[]          = "* Attempts per delay value: %u (k=%u * %d)\r\n";
const u8                g_szTopLine6[]          = "* Sync misses: %u\r\n";
```

After `const u8 g_szTitle3[] = "## HISTOGRAM\r\n";` add:

```c
const u8                g_szHistSize[]          = "### Size %dx%d\r\n";
const u8                g_szHistTabHeader1[]    = "| Operation          | Minimum | %-54s|\r\n";
const u8                g_szHistTabHeader2[]    = "|--------------------|:-------:|:------------------------------------------------------|\r\n";
const u8                g_szHistTabRow[]        = "| %-18s | %-7s | "; // the numbers are printed one by one after this
const u8                g_szHistColCount[]      = "Count";
const u8                g_szHistColNorm[]       = "Normalized (%)";
```

Delete the defines `NUM_WAIT_ROUNDS` and `HIST_DISPLAY_LEN` (their last users are replaced below).

- [ ] **Step 2: `printReportTop`**

Replace the `sprintf(g_auBuffer, g_szTopLine2, ...)` call, the following `print`, and the `g_szTopLine2x` `sprintf` + `print` with:

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

    print(g_auBuffer);

    if(bRealHistogram)
        print(g_szTopLine2xg);
    else
    {
        sprintf(g_auBuffer,
                g_szTopLine2x,
                g_bVBLANKTiming?g_szSyncMethod1:g_szSyncMethod0,
                g_szWaitMethod
               );

        print(g_auBuffer);
    }
```

Replace the `bRealHistogram` block

```c
        print(g_szTopLine4_2);

        u16 nInner = (g_uInnerIterations == 0) ? 256 : g_uInnerIterations;
        sprintf(g_auBuffer, g_szTopLine5, (u16)(nInner * g_uOuterIterations * NUM_WAIT_ROUNDS), NUM_WAIT_ROUNDS); // fits u16, checked in commandLineParameters
        print(g_auBuffer);
```

with

```c
        print(g_szTopLine4_2);

        sprintf(g_auBuffer, g_szTopLine5, (u16)(g_nSweeps * HIST_SWEEP_LEN), g_nSweeps, HIST_SWEEP_LEN); // fits u16, k <= HIST_MAX_SWEEPS
        print(g_auBuffer);

        sprintf(g_auBuffer, g_szTopLine6, g_nTotalSyncMisses);
        print(g_auBuffer);
```

- [ ] **Step 3: Replace `printReportBodyHist`**

Replace the whole v1 function (from its `// ---` header comment `// Outputs markdown. -g mode: one row per test, ...` through its closing `}`) with:

```c
// ---------------------------------------------------------------------------
// Outputs markdown. -g mode: per test the histogram H(d) = F(d) - F(d-1), where
// F(d) is the number of successes at delay value d (F = 0 below the minimum).
// One table with counts, one normalized to % of all attempts per delay value.
void printReportBodyHist(void)
{
    print(g_szTitle3);
    sprintf(g_auBuffer, g_szHistSize, aHORZ_LEN[g_uHistU], aVERT_LEN[g_uHistV]);
    print(g_auBuffer);
    print(g_szEmptyLine);

    s32 lTotal = (s32)g_nSweeps * HIST_SWEEP_LEN;
    u8  szMin[8];

    for(u8 uTable = 0; uTable < 2; uTable++)
    {
        sprintf(g_auBuffer, g_szHistTabHeader1, (uTable == 0) ? g_szHistColCount : g_szHistColNorm);
        print(g_auBuffer);
        print(g_szHistTabHeader2);

        for(u8 t = 0; t < NUM_TESTS; t++)
        {
            Histogram* pHist = &g_aoHistogram[t][g_uHistU][g_uHistV];

            if(pHist->nHistogramLength == 0)
                strcpy(szMin, "(?)");
            else
                sprintf(szMin, "%u", pHist->nHistogramStartValue);

            sprintf(g_auBuffer, g_szHistTabRow, aTEST_NAME[t], szMin);
            print(g_auBuffer);

            u16 nPrev = 0;
            for(u16 x = 0; x < pHist->nHistogramLength; x++) // printed one by one: no row length limit
            {
                u16 nCdf = pHist->panHistogram[x];
                s32 lH   = (s32)nCdf - (s32)nPrev; // can be negative on real HW (noise)
                nPrev    = nCdf;

                if(uTable == 0)
                    sprintf(g_auBuffer, "%ld ", lH);
                else
                {
                    s32 lHundredths = lH * 10000 / lTotal; // % with 2 decimals, integer math
                    u32 ulAbs = (lHundredths < 0) ? (u32)(-lHundredths) : (u32)lHundredths;
                    sprintf(g_auBuffer, "%s%lu.%02lu ", (lHundredths < 0) ? "-" : "", ulAbs / 100, ulAbs % 100);
                }

                print(g_auBuffer);
            }

            if(g_abHistTruncated[t])
                print("...");

            print(g_szEmptyLine);
        }

        print(g_szEmptyLine);
    }
}
```

- [ ] **Step 4: Build and static checks**

Run the build. Expected: exit 0, only the two existing warnings.

Run: `Select-String -Path src\main.c -Pattern "NUM_WAIT_ROUNDS|HIST_DISPLAY_LEN|HIST_START_MARGIN|runTestComboHist"`
Expected: no matches.

Run: `python <scratchpad>/check_help.py src/main.c`. Expected: `PASS`.

Run: `git diff HEAD --stat -- src/bioshelper.s`. Expected: no output.

- [ ] **Step 5: Record final sizes, check memory headroom**

Record `l__CODE`, `l__DATA`, `s__HEAP`. Expected: `s__HEAP` below 0xCA00. If it is above, report it before the runtime checks: the startup memory check will tell on the machine.

- [ ] **Step 6: Runtime check, both machines (human)**

openMSX `run.bat` with `MACHINE=Panasonic_FS-A1GT` (T976x), then with `MACHINE=Philips_NMS_8255`:
`cmdcycle -g 8 8 1 TEST > g1.md`

Expected in `g1.md`:
- The header has `(-i:64 -o:3 k=1)`, `* Sync method: Line interrupts + HR vernier lock per frame`, `* Attempts per delay value: 228 (k=1 * 228)` and **`* Sync misses: 0`**.
- `## HISTOGRAM`, `### Size 8x8`, then two tables of 6 rows: Count and Normalized (%).
- In openMSX every Count is ≥ 0, each Count row sums to 228, and each Normalized row sums to about 100 (truncation can lose up to about 0.01 per number).
- The Minimum is close to the 8x8 CYCLES value of a normal run on the same machine.
- No `ERROR:` line.

If misses > 0 or the run never ends on one machine only, the T976x constants (`vc_fine_t`, `SYNC_WAIT1-1`, `SYNC_WAIT2-1`) or the normal ones are wrong. Stop and report.

- [ ] **Step 7: Runtime check, k and small sizes (human)**

On FS-A1GT:
- `cmdcycle -g 8 8 4 > g4.md`: Normalized rows identical to `g1.md` in openMSX, header `k=4`, attempts `912`.
- `cmdcycle -g 1 1 1 > g11.md`: completes, rows valid (or `(?)`), Counts ≥ 0.
- `cmdcycle -g 8 8 1 -3 > g1n.md`: `mode: No display`, completes.

- [ ] **Step 8: Normal mode regression (human)**

`cmdcycle > norm.md` on FS-A1GT: the usual CYCLES + FAKE HISTOGRAM report. Values may differ slightly from `fs-a1gt.md` (STOP in the setup changes the attempt period of the existing routines).

- [ ] **Step 9: Leave uncommitted**

Do not commit. Report `git status --short` and `git diff --stat`.
