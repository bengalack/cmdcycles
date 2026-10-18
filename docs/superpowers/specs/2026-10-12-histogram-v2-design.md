# True histogram v2 (`-g x y k`): phase-swept measurement — design

Date: 2026-10-12
Status: agreed in conversation, pending spec review
Replaces the measurement part of `2026-10-11-true-histogram-design.md` (v1, never committed).

## Goal

For one command size (x, y) and all `NUM_TESTS` tests, measure the real
distribution of the command duration in Z80 cycles, and print it as a
histogram (count per exact duration) plus a normalized version (%).

## Why v1 is replaced

v1 ran back-to-back attempts with a period that depends on the delay value. A
display line is exactly **228** Z80 cycles. When the attempt period shares a
factor with 228 (= `4 * 3 * 19`), the attempts only visit some of the possible
positions within a line, and neighbouring delay values visit different subsets.
Result: noisy, sometimes decreasing counts and a hard-to-find minimum.

v2 samples every position within a line exactly once per sweep.

## Measurement principle

- For a delay value `d`, an attempt answers one yes/no question: is the command
  done `d` cycles after the `OUT` that starts it. The share of "done" over all
  attempts is the CDF value F(d).
- One **sweep** = 228 attempts whose start positions within the line are
  exactly 0, 1, ..., 227 cycles after a fixed reference (the HR edge).
- **k** sweeps per delay value: `k * 228` attempts.
- The histogram is the first difference: `H(d) = F(d) - F(d-1)` (attempts that
  finished in exactly `d` cycles). The sum over the span is `k * 228`.

## Fixed hardware facts used

- Line length: exactly 228 Z80 cycles (1368 VDP cycles). The CPU and VDP clocks
  come from the same crystal.
- MSX Z80 timing: +1 wait state per M1 cycle (the existing code's convention).
- **T976x engines** (`g_bHasT976xEngine`): every `IN`/`OUT` on the VDP ports
  0x98-0x9B costs **exactly +1** cycle (`in a,(n)` 13, `in a,(c)` 15, `outi` 19,
  ...). This is constant. No code compensates for any variation of it.
- S#2: CE = bit 0, HR = bit 5. S#1: FH = bit 0 (line interrupt flag).
- **VDP I/O spacing**: two accesses to the VDP ports must be at least 15 cycles
  apart (start of one I/O cycle to the start of the next). `vdpWriteReg`
  ends with an OUT, so an `in a,(0x99)` directly after it is only 12 cycles
  away and needs a `nop` in between. (Found as a bug in the first v2 build:
  the FH check in the attempt loop broke this rule; on the FS-A1ST the
  attempt length then varied.)

## 1. Command line and help (`main.c`)

- `-g x y k`: all three are required.
  - x in `[1, NUM_HORIZONTALS]`, y in `[1, NUM_VERTICALS]`, mapped to indices
    with `findLenIndex` (as in v1).
  - k in `[1, HIST_MAX_SWEEPS]` (287, because 287 * 228 = 65436 fits u16).
    Stored in `g_nSweeps` (u16).
  - Missing values: `ERROR:Missing value(s) for -g`.
  - k out of range: `ERROR:k for -g must be in range 1-287`.
- `-t` together with `-g`: `ERROR:-t can't be used with -g` (checked after the
  argument loop, so the order doesn't matter).
- `-i`, `-o`, `-1`, `-2`, `-3` and the sys name work with `-g`. `-i`/`-o` only
  affect phase 1 (finding W).
- v1's `-i*-o*13` budget check and `NUM_WAIT_ROUNDS` are removed.
- `-h -g` shows the histogram help (as in v1).

Main help (22 lines, ≤ 29 columns). Changed lines only:

```
Usage:cmdcycle [opt] [sys]
...
 -g x y k Histogram (-h -g)
```

Histogram help (12 lines, ≤ 29 columns; `%d` = `NUM_HORIZONTALS`,
`NUM_VERTICALS`, `HIST_MAX_SWEEPS`):

```
Usage:cmdcycle -g x y k [sys]

Histogram of the duration of
one command size (x*y) for
all tests.

x: 1..%d (width)
y: 1..%d (height)
k: 1..%d sweeps of 228

-i -o -1 -2 -3 apply as
usual. -t is not supported.
```

## 2. VDP parameter setup (`vdp.s`, `setVDPCmdParamsNI_asm`)

Two changes, both before any existing code in the routine:

1. **STOP first**: `ld b,a` / `xor a` / `vdpWriteReg 46` / `ld a,b`. This
   guarantees that no previous command is running when the registers are
   streamed. B is overwritten later in the routine anyway. This applies to **all
   modes**; normal-mode results may shift slightly compared with older reports
   (the setup takes longer, which changes the attempt period of the existing
   routines; the measured OUT→IN window is unchanged).
2. **Colour**: `ld a,r` (R#44) becomes
   `ld a,(_g_uCmdColor)` / `or a` / `jr nz,1$` / `ld a,r` / `1$:`.
   `g_uCmdColor = 0` (default) keeps today's behaviour. `-g` sets
   `HIST_CMD_COLOR` (non-zero, `0xAA`), which removes the colour as a possible
   source of variation for the logical commands. The path taken is constant for a
   whole run.

## 3. Assembly (`bioshelper.s`)

### 3.1 Data set by `histInit` (T976x path as data, not as duplicated code)

```c
extern void histInit(bool bT976x);
```

Sets `g_uIoExtra` (0 or 1) and all **hand-counted** wait constants:
`nVernierPad`, the pre-vernier wait, and the sync-check waits. Each
hand-counted loop has a comment with its cycle count and its number of
VDP-port I/O instructions. Its T976x constant = normal constant − (number of
VDP-port I/O) * 1.

Everything else (the attempt body) is **calibrated**, never hand-counted.

### 3.2 `waitExact` primitive

`HL = N` (N ≥ 48): waits N + C_wait cycles, C_wait constant. It uses the same
decomposition as `dispatch_vdp_test`'s dynamic path (16-cycle loop + remainder
stub, `Total = 16*B + 32 + R`), without the OUT/IN. It is used for the attempt
pad, the frame offset, the vernier pad, and the sync-check waits.

- Minimum N is 48 because B ≥ 1 (16 + 32 + 0). All uses are well above that.
  If a calculated N ends up below 48, 228 is added (same position within the
  line, one line later).
- C_wait is constant for every N ≥ 48. Unlike `dispatch_vdp_test`, `waitExact`
  has **no** `H != 0` / `L >= 48` checks at its entry (their cost depends on N).
  It always takes the dynamic path: the same instructions for every N, only the
  values differ.

### 3.3 Hand-counted loops

- **HR=0 wait** (no timing requirement): `in a,(0x99)` / `and b` / `jp nz`.
- **Coarse sync**: unrolled block, as many steps as `jr` range allows (about 24),
  each `in a,(0x99)` (12) / `and b` (5, B = 0x20) / `jr nz,coarse_hit` (8 when
  not taken) = **25** cycles per sample (26 on T976x). The block ends with
  `jp z` back to its start. All `jr nz` exits cost the same (13).
- **Pre-vernier wait**: places the first vernier sample just before the next
  HR edge while HR is still 0. If the first sample reads 1, the routine goes
  back to the coarse sync (no miss counted).
- **Vernier**: period exactly **229**:
  `in a,(0x99)` (12, 13 on T976x) / `and b` (5) / `jp nz,hit` (11) /
  `ld hl,(nVernierPad)` (17) / `call waitExact` / `jp loop` (11). Each sample is
  1 cycle later in the line than the previous one. At most 30 samples,
  otherwise back to the coarse sync and `nSyncMisses++`.
- **Phase measurement** (`measurePhase`, used by calibration): the same
  229-cycle vernier from an arbitrary position. It counts the samples `s` from
  the first sample up to and including the first sample with HR=1 that follows a
  sample with HR=0. Result: `s` in 1..228 (u8), `s = ((edge − φ − 1) mod 228) + 1`
  where φ is the start position.
- **Sync check** (after each lock): sample once on the next line at edge − 1
  (expect HR=0), and once on the line after at the edge (expect HR=1). On
  mismatch: `nSyncMisses++` and relock.

### 3.4 Attempt body (constant length, no VDP-dependent timing)

General rules for all new assembly:

- Undocumented instructions are allowed (`.allow_undocumented` is already set).
  When registers run out in a tight loop, use the 8-bit halves IXH/IXL/IYH/IYL
  rather than PUSH/POP (which are expensive).
- SDCC needs IX intact: every routine that uses IX starts with `push ix` and
  ends with `pop ix`, as the existing routines do.
- Outside tight loops, IX/IY may be used as record pointers (`ld a,0(ix)`, ...).
  `histMeasureDelay` uses IX as `HistCombo*` outside the loop (reading
  parameters, writing `nSyncMisses` / `uBodyMod228`), saves it in
  `pHistParams`, and reloads it from there after the loop.

Registers during the loop: IXH = W, IXL = H, IYH = command, IYL = attempts this
frame. Alternate set: BC' = remaining attempts, DE' = 0, HL' = successes.
A, BC, DE and HL are scratch. IX is **not** a struct pointer here: the routine
stores `HistCombo*` in `pHistParams` at entry and uses it only outside the loop.
The guard-line mask lives in memory (`uFhMask`: 1 = honour the guard line,
0 = ignore it during calibration).

The `setVDPParams` / `testVDP` wrappers (three PUSH/POP pairs each) are not used;
the body calls `setVDPCmdParamsNI_asm` and `dispatch_vdp_test` directly and
reloads what they destroy.

```
hf_try:
    ld      d,ixh                   ; W
    ld      e,ixl                   ; H
    ld      a,iyh                   ; command
    call    setVDPCmdParamsNI_asm   ; STOP + parameters (destroys AF, BC, DE, HL)
    ld      hl,(nDelay)
    ld      c,iyh
    call    dispatch_vdp_test       ; OUT, exactly d, IN -> A
    ld      b,a
    xor     a
    vdpWriteReg 46                  ; STOP right after the measurement
    ld      a,b
    rra                             ; carry = busy (CE)
    ccf                             ; carry = done
    exx
    adc     hl,de                   ; HL' += done (DE' = 0), no branch
    dec     bc                      ; BC' = remaining attempts
    ld      a,b
    or      c
    exx
    jp      z,hf_all_done           ; remaining == 0
    inc     iyl                     ; attempts this frame
    <select S#1, in a,(0x99), select S#2>   ; constant
    ld      b,a
    ld      a,(uFhMask)
    and     b                       ; FH, masked
    jp      nz,hf_frame_end         ; guard line reached
    ld      hl,(nPad)
    call    waitExact               ; pad P
    jp      hf_try
```

The loop is a subroutine `hfRun` (entered at `hf_try` with `call`), so both
calibration and the frame loop can use it. Its two exits return with `ret`:
`hf_all_done` returns with carry clear (all attempts done), `hf_frame_end`
returns with carry set (guard line reached). Both exits have a constant cost.

The loop period L = body + checks + pad. The aim is `L = n * 228 + 1`, so every
attempt starts exactly 1 cycle later within the line than the previous one.
`updateHistogram` is **not** used: its carry case would make L variable.

### 3.5 Calibration (per delay value)

The exit through `hf_all_done` has the same cost in every run, so:

1. Lock, run the loop with BC' = 1, `uFhMask` = 0, pad P0 (minimum), then
   `s1 = measurePhase`.
2. Lock, run with BC' = 2 (otherwise the same), then `s2 = measurePhase`.
3. `Lmod = s1 − s2`, plus 228 if negative. This is L mod 228 for pad P0.
4. `P = P0 + (1 − Lmod)`, plus 228 if negative. Now L ≡ 1 (mod 228).
5. Verify with P using steps 1–3: the result must be exactly 1. Otherwise
   `nSyncMisses++` and recalibrate (at most 3 times, then continue with the
   last P; the misses are visible in the report).
6. `uBodyMod228 = Lmod` (diagnostic).

No division is used: only one subtraction plus a conditional add of 228 per step.

### 3.6 `histMeasureDelay` (one delay value per call)

```c
u16 histMeasureDelay(HistCombo* p);   // returns #successes out of p->nTries
```

```
prologue    push ix, (pHistParams) = p, di, prepareLineInterruptsNI (existing, sets IYH = 0),
            then load IXH/IXL/IYH from *p (after that call, because it overwrites IYH)
calibrate   section 3.5 -> nPad
i = 0
frame:      R#19 = 0, clear FH, poll FH (S#1)           ; wait for line 0
            R#19 = p->uGuardLine, clear FH, select S#2
            coarse sync -> pre-vernier wait -> vernier -> sync check
            waitExact(offset base + i)
            IYL = 0, uFhMask = 1, call hfRun
            carry set (guard line):
                i += IYL; if i >= 228: i -= 228        ; IYL ≤ ~25, sum fits u8
                goto frame
            carry clear (all attempts done):
                _cleanupLineInterruptsNI (existing), ei, DE = HL' (successes), restore IX, ret
```

BC' is initialised to `p->nTries` once (after calibration). The offset `i`
carries over between frames, so each sweep covers 0..227 exactly once,
whatever the frame boundaries are.

### 3.7 Removed from `bioshelper.s`

v1's `_runTestComboHist`, `_runTestComboHistNoHalts`, the
`controlledWaitByMethod` macro, and the `hn_*` / `hv_*` labels. The existing
routines (`_runTestCombo`, `_runTestComboNoHalts`, `secureActiveArea`,
`SECOND_LINE_INT`, ...) are not changed.

### 3.8 Drifting CPU/VDP clocks (added after the first real-hardware runs)

Most MSX machines derive the CPU clock from the VDP crystal (÷6), so the two
never drift. The turbo R (FS-A1ST) does not: measured drift about +9 CPU cycles
per 1024 lines (~36 ppm, ~2 cycles per frame, the HR edge comes earlier than
the CPU expects). Free-running attempts then slide across positions, and the
period calibration regularly gets an off-by-one, which distorts the histogram
(negative counts, wrong shape).

- **Drift test**: at the start of every `-g` run, `histMeasureDrift` locks,
  waits exactly 1024 lines counted in CPU cycles (`histDriftPhase`, a
  hand-counted loop without VDP I/O) and measures where the HR edge is.
  The hidden option `-d` prints this test for several wait lengths.
- **Drift 0**: free-run per frame with calibration, as described above.
- **Drift ≠ 0, or the test failed**: `histMeasureDelayRelock` — a lock before
  **every** attempt, then a wait of exactly i cycles (i = 0..227, +1 per
  attempt), then one attempt. No period calibration. Drift can only act within
  one attempt (~0.1 cycle on the turbo R). The header says "lock per attempt"
  and shows the measured drift.
- **Sync check 1 slack in relock mode** (`histRelaxSyncCheck1`): check 1 samples
  at edge − 2 instead of edge − 1. With drift, a vernier hit late in the first
  cycle after the edge makes the edge − 1 sample see the edge already over the
  2 lines to check 1, so ~3% of good locks were rejected. Accepting them does
  not move any attempt by a cycle; it only spreads the sampled sub-cycle
  positions evenly. With coupled clocks the strict check stays. The wait
  before check 2 gets one cycle **more** at the same time: check 2 is timed
  from check 1 and must stay exactly on the edge (v0.92 forgot this, so check 2
  failed for almost every lock on the turbo R).

### 3.9 Wait-state detection and CPU timing check

- The T976x detection uses `measureVDPPollsInOneFrame` (`iowait.s`): it polls
  the VBLANK flag with interrupts off and counts 35-cycle loop passes (36 with
  the T976x +1) in exactly one frame. Cartridge interrupts cannot disturb it.
  Threshold in the middle: NTSC 1683, PAL 2010.
- Polling has one race: reading S#0 at the very moment the VDP sets the VBLANK
  flag can return 0 and clear it, so a frame boundary is lost and the count
  covers two frames (seen as 3319 = 2 x 1659 on the turbo R). A lost flag can
  only make a count too large, so three frames are measured and the smallest
  count is used (`T976X_MEASUREMENTS`).
- A count outside both expected windows (NTSC 1635..1731, PAL 1954..2067)
  means the CPU is slowed by something else, e.g. a cartridge inserting wait
  states (found with a PicoVerse firmware that provides mapper RAM). The
  program then stops with `ERROR:Unexpected CPU timing (cartridge wait
  states?)`; `-d` still runs to show the count.

## 4. C orchestration (`main.c`)

### Struct

```c
typedef struct {
    u8                      uW;             // 0
    u8                      uH;             // 1
    u8                      uCmd;           // 2
    u8                      uGuardLine;     // 3  last line an attempt may start on
    u16                     nDelay;         // 4 & 5
    u16                     nTries;         // 6 & 7  k * 228
    u16                     nSyncMisses;    // 8 & 9  out, accumulated
    u8                      uBodyMod228;    // 10     out, last calibration
} HistCombo;
```

The first three fields keep the same order as in `RunCombo` for consistency.
The assembly reads them once at entry into IXH/IXL/IYH (see 3.4).

### Constants

```c
#define HIST_SWEEP_LEN      228     // Z80 cycles per display line
#define HIST_MAX_SWEEPS     287     // 287 * 228 = 65436, fits u16
#define HIST_MIN_DELAY      20      // dispatch_vdp_test can't make all values below 20
#define HIST_OVERHEAD_MAX   900     // safe upper bound for setup + checks + pad, in cycles
#define HIST_CMD_COLOR      0xAA
#define STACK_RESERVE       512
```

### `runHistTest()` (replaces v1), per test c

```
d0   = g_anResultDelays[c][u][v] - nAdd                    ; raw W from phase 1
p.uGuardLine per call = 211 - (d + HIST_OVERHEAD_MAX) / 228 - 1
walk down: d = d0, d0-1, ...: count -> scratch (stored in reverse order)
           stop when count == 0 or d == HIST_MIN_DELAY
move the down part so that the lowest d is at index 0
walk up:   d = d0+1, ...: count -> next index
           stop when count == nTries
           if the index reaches SCRATCH_HIST_SIZE: stop, set ERROR_CODE 5, mark the row as truncated
span = lowest d with count > 0 .. first d with count == nTries
store the CDF counts of the span: myMalloc(len), copy,
      g_aoHistogram[c][u][v] = { pan, len, nHistogramStartValue = low + nAdd }
      (myMalloc failure: ERROR_CODE 3, as today)
clear the used part of scratch
```

There is no other cap on the span. The only limits are memory: the scratch
buffer (1024 values) and the heap (worst case `6 * 1024 * 2` = 12 KB plus
~2.2 KB from phase 1, which fits in `MY_HEAP` = 28000).

`recordAndClearHistogram` gets back its original signature (v1's
`bTrimTrailingOnes` parameter is removed). It is used by phase 1 only.

### `runTests()`

```c
establishInitialDelays();
bool bOk = runTest();
if(bOk && g_bHistogramMode)
    runHistTest();
```

`histInit(g_bHasT976xEngine)` is called once before the tests. `g_uCmdColor`
is set to `HIST_CMD_COLOR` in `-g` mode (after phase 1, so phase 1 behaves as
before) and set back to 0 afterwards.

### Memory check (new, all modes)

At startup: `nTop = *(u16*)0x0006` (top of the DOS program area). The image end
comes from a small asm helper that returns the linker symbol `s__HEAP`. If
`nTop - imageEnd < STACK_RESERVE`, print `ERROR:Not enough memory` and return 1.

## 5. Report (`main.c`)

### Header in `-g` mode

```
cmdcycle v0.90 - <date> - <sys>
* VDP detected: V9958 (T976x), screen 8, mode: Active display. (-i:64 -o:3 k:4)
* Sync method: Line interrupts + HR vernier lock per frame
* Result data on this T976x based system have a +1 cycle included
* Histogram type: True histogram
* Attempts per delay value: 912 (k:4 * 228)
* Sync misses: 0
```

- `k:` is added to line 2 in `-g` mode only. Normal runs keep
  `(-i:.. -o:..)`.
- In `-g` mode the sync line replaces `* Sync method: %s, wait method: %s`.
- `* Sync misses: %u` is the sum over all tests and delay values. The line is only
  printed when the sum is greater than 0.

### Body (`printReportBodyHist`, replaces v1's)

```
## HISTOGRAM
### Size 8x8

| Operation          | Minimum | Count                                                 |
|--------------------|:-------:|:------------------------------------------------------|
| Copy: LMMM-TEOR    | 2032    | 4 26 12 0 0 ...
...

| Operation          | Minimum | Normalized (%)                                        |
|--------------------|:-------:|:------------------------------------------------------|
| Copy: LMMM-TEOR    | 2032    | 0.44 2.85 1.32 0.00 0.00 ...
...
```

- Minimum = `nHistogramStartValue` (includes `nAdd`).
- Count: `H(d) = F(d) − F(d−1)` with F(low − 1) = 0, one number per delay value
  from the minimum up to the first delay value where all attempts succeed. Shown
  as signed values (`%d`): in openMSX they are never negative; on real hardware
  a negative value means noise and is shown as it is.
- Normalized: `H(d) * 10000 / (k * 228)` printed as `%d.%02d` with the sign
  handled. Each row sums to 100%.
- Each number is printed directly with `print`, without collecting the whole row
  in `g_auHistStrBuf`, so row length is not limited by that buffer.
- A row with length 0 prints `(?)` as the Minimum and no numbers.
- A row truncated by the scratch limit ends with `...` (ERROR_CODE 5 is printed
  at the end, as the existing errors are).
- No Sum / Grand total lines (each row sums to k * 228, or 100%).

## Findings on real hardware

### The result can depend on the start-up (observed 2026-10)

**Observation** (Sony HB-F1XD, V9938, coupled CPU/VDP clocks, `-g 8 8 1`):

- The minimum for LINE-TEOR 8x8 was **234** after most start-ups and **235**
  after some (235 is the rarer one). The difference is one single start
  position out of 228.
- Within one start-up (the same DOS session) the value never changed: 4 runs
  in a row gave 234 every time, and after a start-up that gave 235, 5 runs in a
  row gave 235 every time.
- It changed only across a **reset or a full power cycle** (both were seen to
  change it).
- Consistent with this, k8 = exactly 8 * k1 within a run with the display off,
  and a k128 run after a cartridge swap (power cycle) had slightly different
  lowest positions (LINE, LMMM, YMMM) than earlier k1/k8 runs.

**Interpretation (theory, not confirmed)**: on these machines the CPU clock is
the VDP clock divided by 6. After a reset the divider starts at one of its 6
possible phases, depending on exactly when the reset ends relative to the
crystal. The CPU's I/O then lands on one of 6 VDP sub-positions per CPU cycle,
fixed until the next reset. Most of the 228 start positions keep their
duration, a few move by one cycle.

**Consequences**:

- One `-g` run measures the histogram for **one** CPU/VDP phase. A higher k
  does not change that phase; only another start-up does.
- A complete reference (e.g. for an emulator's VDP model) is the union over
  several start-ups, ideally covering all 6 phases.
- The turbo R (drifting clocks, see 3.8) passes through all sub-positions
  during a single run, so its histogram is already such a union.
- Not yet confirmed: that the divider phase is the cause, rather than other
  state that a reset sets (a way to check: count how often each value appears
  over many start-ups; with 6 equally likely phases the frequencies should be
  multiples of about 1/6).

## Memory

- Each implementation task records `l__CODE`, `l__DATA` and the image end
  (`s__HEAP`) from `out\cmdcycle.map`.
- Expected: about +1 to 1.5 KB of code compared with v1. Moving `MY_HEAP` out of
  the image is **not** part of this design (it can be added later if needed).

## Version

`g_szVersion` is `"0.90"` (0.83 is reserved for something else).

## Out of scope

- A two-stage vernier (possible later speed-up, about 7%).
- Locking once for the whole run (one lock per frame is required).
- `-t` in `-g` mode.
- Changes to the existing `_runTestCombo` / `_runTestComboNoHalts` /
  `secureActiveArea`.
- Moving `MY_HEAP` out of the image.
- A `Maximum` column.

## Testing

Build with `build.bat` (cmd), then run in openMSX (FS-A1GT = T976x, and a
non-T976x machine such as Philips_NMS_8255) and on real hardware:

- `cmdcycle -h`, `cmdcycle -h -g`: line counts and widths as above.
- `-g 8 8` (k missing), `-g 8 8 0`, `-g 8 8 288`, `-g 8 8 4 -t` → errors.
- `cmdcycle -g 8 8 1 > g1.md` on both machines:
  - `Sync misses: 0`.
  - In openMSX every Count is ≥ 0 and each row sums to 228.
  - Minimum is close to the 8x8 CYCLES value of a normal run.
- `cmdcycle -g 8 8 4 > g4.md`: Normalized rows close to the k=1 run (in
  openMSX: identical).
- `cmdcycle -g 1 1 1 > g11.md`: small delays; no hang, valid rows.
- A normal run (no `-g`) still produces the usual report. Values may shift
  slightly because of the STOP in the setup.
