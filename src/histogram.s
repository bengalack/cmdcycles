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
    DRIFT_WAIT          .equ 3422               ; waitExact N in the drift diagnostic: one loop pass = 16 lines (3648)
    PRE_VERNIER_WAIT    .equ 135                ; waitExact N: coarse hit -> 1st vernier sample = 426 cycles
    SYNC_WAIT1          .equ 174                ; waitExact N: vernier hit -> 1st check sample = 455 cycles (2 lines - 1)
    SYNC_WAIT2          .equ 216                ; waitExact N: 1st check -> 2nd check sample = 457 cycles (2 lines + 1)
    LOCK_MAX_MISSES     .equ 64                 ; histLock gives up after this many misses in a row
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
; Misses (vernier cap or sync check) increment HistCombo.nSyncMisses and retry,
; at most LOCK_MAX_MISSES times in a row.
;
; Sync check 1 has one cycle of slack when the CPU and VDP clocks drift (relock
; mode, e.g. turbo R): then it samples at edge - 2 instead of edge - 1 (see
; histRelaxSyncCheck1). Reason: the vernier hit lands somewhere within the
; first cycle after the edge. With drift (~0.02 cycles over these 2 lines on
; the turbo R) a hit late in that cycle makes the edge - 1 sample see the edge
; already, so ~3% of perfectly good locks were rejected. Accepting them does
; not move any attempt by a cycle; it only makes the sub-cycle positions that
; are sampled evenly spread. With coupled clocks the strict check stays.
; IN:       IX - HistCombo*, S#2 selected, DI
; OUT:      carry clear: locked. Returns 41 cycles (42 on T976x) after the start
;           of the 2nd check sample's IN, which is exactly on the HR edge (the
;           lock reference)
;           carry set: LOCK_MAX_MISSES misses in a row, no lock possible
; MODIFIES: AF, BC, DE, HL
histLock::
    xor     a
    ld      (uLockMisses),a
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
    ld      a,#LOCK_VC_CAP          ;  8
    ld      (uVcCap),a              ; 14
    ld      hl,#PRE_VERNIER_WAIT    ; 11
    call    waitExact               ; 135 + 199 (destroys DE)
    ld      de,#0                   ; 11   D = samples, E = not armed (after waitExact!)
    call    vernierCount            ; 18   Coarse hit IN -> 1st vernier IN = 426:
                                    ;      5..30 cycles before the edge 2 lines later
    jp      c,hl_miss_vernier       ; 11

    ld      hl,(nSyncWait1)         ; 17
    call    waitExact               ; hit IN -> here: 54 + 11 + 17 + N + 199 = 455 with N = SYNC_WAIT1 (-1 on T976x)
    in      a,(VDPPORT1)            ; 12   edge - 1, 2 lines later: expect HR == 0
    and     c                       ;  5   tested right away: waitExact destroys DE
    jr      nz,hl_miss_sync1        ;  8
    ld      hl,(nSyncWait2)         ; 17
    call    waitExact               ; 12 + 5 + 8 + 17 + N + 199 = 457 with N = SYNC_WAIT2 (-1 on T976x)
    in      a,(VDPPORT1)            ; 12   edge, 4 lines after the hit: expect HR == 1 (lock reference)
    and     c                       ;  5
    jr      z,hl_miss_sync2         ;  8
    or      a                       ;  5   carry clear: locked
    ret                             ; 11   lock reference IN start -> ret done: 12 + 5 + 8 + 5 + 11 = 41 (constant)

hl_miss_vernier:                    ; diagnostics: which part of the lock failed (not timed)
    ld      hl,(_g_nMissVernier)
    inc     hl
    ld      (_g_nMissVernier),hl
    jr      hl_miss

hl_miss_sync1:
    ld      hl,(_g_nMissSync1)
    inc     hl
    ld      (_g_nMissSync1),hl
    jr      hl_miss

hl_miss_sync2:
    ld      hl,(_g_nMissSync2)
    inc     hl
    ld      (_g_nMissSync2),hl
    ; FALL THROUGH

hl_miss:
    ld      l,10(ix)                ; nSyncMisses++
    ld      h,11(ix)
    inc     hl
    ld      10(ix),l
    ld      11(ix),h
    ld      a,(uLockMisses)
    inc     a
    ld      (uLockMisses),a
    cp      #LOCK_MAX_MISSES
    jp      c,hl_wait0              ; retry
    scf                             ; no lock possible (e.g. wrong T976x detection)
    ret

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
; OUT:      carry clear: all attempts done (or first success with uStopMask)
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
    inc     iyl                     ; attempts this frame
    ld      a,#1
    vdpWriteReg 15                  ; S#1
    nop                         ; VDP I/O: >= 15 cycles between accesses (OUT -> IN is 12 without this)
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
; Relock mode only (drifting CPU/VDP clocks): sync check 1 samples one cycle
; earlier, at edge - 2 instead of edge - 1. Call once, after histInit (which
; sets the strict value). See the comment at histLock for the reason.
; The 2nd check sample is timed from the 1st one, so its wait gets one cycle
; MORE: it must stay exactly on the edge (the lock reference).
; MODIFIES: HL
; void histRelaxSyncCheck1(void);
_histRelaxSyncCheck1::
    ld      hl,(nSyncWait1)
    dec     hl                      ; one cycle less: the 1st check sample comes one cycle earlier
    ld      (nSyncWait1),hl
    ld      hl,(nSyncWait2)
    inc     hl                      ; one cycle more: the 2nd check sample (timed from the 1st) stays on the edge
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
    ld      (uStopMask),a           ; calibration runs all its attempts (A = 0)

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
    jp      c,hp_fail               ; 11   no lock possible
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
hp_fail:
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
; OUT:      DE - successes out of nTries, 0xFFFF if no lock was possible
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
    ld      a,12(ix)
    ld      (uStopMask),a           ; HistCombo.uStopOnSuccess

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
    nop                         ; VDP I/O: >= 15 cycles between accesses (OUT -> IN is 12 without this)
    in      a,(VDPPORT1)            ; clear FH
hm_wait_line0:
    in      a,(VDPPORT1)
    rra
    jp      nc,hm_wait_line0        ; top of the active area

    ld      a,3(ix)
    vdpWriteReg 19                  ; line interrupt at the guard line
    nop                         ; VDP I/O: >= 15 cycles between accesses (OUT -> IN is 12 without this)
    in      a,(VDPPORT1)            ; clear FH (S#1 still selected)
    ld      a,#2
    vdpWriteReg 15                  ; S#2

    call    histLock
    jp      c,hm_fail               ; 11   no lock possible

    ld      a,#1                    ; lock done above the guard line? (same cost when yes)
    vdpWriteReg 15
    nop                         ; VDP I/O: >= 15 cycles between accesses (OUT -> IN is 12 without this)
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

hm_fail:
    call    _cleanupLineInterruptsNI
    ei
    ld      de,#0xFFFF              ; HIST_LOCK_FAILED (more than any nTries)
    pop     ix
    ret

; ----------------------------------------------------------------------------
; Same measurement as histMeasureDelay, for machines whose CPU and VDP clocks
; drift (e.g. turbo R): a new lock before EVERY attempt, then a wait of
; exactly i cycles (i = 0..227, +1 per attempt), then one attempt. Drift can
; only act within one attempt (~0.1 cycle), and no period calibration is
; needed (HistCombo.nPad is not used).
; IN:       HL - HistCombo*
; OUT:      DE - successes out of nTries, 0xFFFF if no lock was possible
; MODIFIES: AF, BC, DE, HL, IY, BC', DE', HL'
; u16 histMeasureDelayRelock(HistCombo* p); HL
_histMeasureDelayRelock::
    push    ix

    push    hl
    pop     ix
    ld      (pHistParams),hl
    call    hist_load_params

    xor     a
    ld      (uHfOffset),a           ; position i of the next attempt
    ld      (uFhMask),a             ; hfRun runs one attempt and exits before its FH check
    ld      a,12(ix)
    ld      (uStopMask),a           ; HistCombo.uStopOnSuccess

    ld      l,6(ix)
    ld      h,7(ix)
    ld      (nRelockLeft),hl        ; attempts left
    ld      de,#0
    ld      hl,#0
    exx                             ; DE' = 0, HL' = successes
    di
    call    prepareLineInterruptsNI

rm_frame:
    ld      ix,(pHistParams)        ; IX = p again (the attempt used IXH/IXL)

    xor     a
    vdpWriteReg 19                  ; line interrupt at line 0
    ld      a,#1
    vdpWriteReg 15                  ; S#1
    nop                             ; VDP I/O: >= 15 cycles between accesses (OUT -> IN is 12 without this)
    in      a,(VDPPORT1)            ; clear FH
rm_wait_line0:
    in      a,(VDPPORT1)
    rra
    jp      nc,rm_wait_line0        ; top of the active area

    ld      a,3(ix)
    vdpWriteReg 19                  ; line interrupt at the guard line
    nop                             ; VDP I/O: >= 15 cycles between accesses (OUT -> IN is 12 without this)
    in      a,(VDPPORT1)            ; clear FH (S#1 still selected)
    ld      a,#2
    vdpWriteReg 15                  ; S#2

rm_attempt:
    call    histLock
    jp      c,rm_fail               ; 11   no lock possible

    ld      a,#1                    ; lock done above the guard line? (same cost when yes)
    vdpWriteReg 15
    nop                             ; VDP I/O: >= 15 cycles between accesses (OUT -> IN is 12 without this)
    in      a,(VDPPORT1)
    ld      b,a
    ld      a,#2
    vdpWriteReg 15
    bit     0,b
    jp      nz,rm_frame             ; too late: next frame

    call    hist_load_regs
    ld      a,(uHfOffset)
    ld      l,a
    ld      h,#0
    ld      de,#OFFSET_BASE
    add     hl,de
    call    waitExact               ; position i: this attempt starts i cycles later in the line
    exx
    ld      bc,#1                   ; one attempt
    exx
    ld      iyl,#0
    call    hfRun                   ; one attempt, exits through hf_all_done

    ld      a,(uStopMask)           ; stop at the first success? (not timed)
    or      a
    jr      z,rm_no_stop
    exx
    ld      a,h
    or      l                       ; HL' = successes
    exx
    jp      nz,rm_done
rm_no_stop:

    ld      a,(uHfOffset)           ; next position (not timed)
    inc     a
    cp      #LINE_LEN
    jr      c,rm_pos_ok
    xor     a
rm_pos_ok:
    ld      (uHfOffset),a

    ld      hl,(nRelockLeft)
    dec     hl
    ld      (nRelockLeft),hl
    ld      a,h
    or      l
    jp      z,rm_done

    ld      ix,(pHistParams)        ; for histLock (misses)
    ld      a,#1                    ; guard line passed during this attempt?
    vdpWriteReg 15                  ; S#1
    nop                             ; VDP I/O: >= 15 cycles between accesses (OUT -> IN is 12 without this)
    in      a,(VDPPORT1)            ; FH (reading clears it)
    ld      b,a
    ld      a,#2
    vdpWriteReg 15                  ; S#2
    bit     0,b
    jp      nz,rm_frame             ; yes: next frame
    jp      rm_attempt

rm_done:
    call    _cleanupLineInterruptsNI
    ei
    exx                             ; HL = successes
    ex      de,hl                   ; SDCC needs return in DE
    pop     ix
    ret

rm_fail:
    call    _cleanupLineInterruptsNI
    ei
    ld      de,#0xFFFF              ; HIST_LOCK_FAILED (more than any nTries)
    pop     ix
    ret

; ----------------------------------------------------------------------------
; Diagnostic (-d): drift between the CPU clock and the VDP clock. Lock to the
; HR edge, wait exactly nBlocks * 16 lines counted in CPU cycles (no VDP I/O
; in the wait), then measure where the HR edge is. With coupled clocks the
; result is the same for every nBlocks; a difference between two nBlocks
; values is the drift (in CPU cycles) over the extra lines.
; IN:       HL - HistCombo* (only nSyncMisses is used, by histLock)
;           DE - nBlocks (1..255, E is used)
; OUT:      A - vernier samples until the HR edge (0..228), 0xFF if failed
; MODIFIES: AF, BC, DE, HL
; u8 histDriftPhase(HistCombo* p, u16 nBlocks); HL, DE
_histDriftPhase::
    push    ix

    push    hl
    pop     ix                      ; IX = p (histLock counts misses there)
    ld      a,e
    ld      (uDriftBlocks),a

    di
    call    prepareLineInterruptsNI
    ld      a,#2
    vdpWriteReg 15                  ; S#2

    call    histLock
    jp      c,hd_fail               ; 11   no lock possible

    ld      a,(uDriftBlocks)        ; 14
    ld      c,a                     ;  5
hd_loop:                            ; exactly 16 lines = 3648 cycles per pass, no VDP I/O
    ld      hl,#DRIFT_WAIT          ; 11
    call    waitExact               ; N + 199 (keeps C)
    dec     c                       ;  5
    jp      nz,hd_loop              ; 11   11 + N + 199 + 5 + 11 = 3648 with N = DRIFT_WAIT

    ld      c,#HR_MASK
    ld      de,#0
    ld      a,#PHASE_VC_CAP
    ld      (uVcCap),a
    call    vernierCount
    ld      a,d
    jr      nc,hd_ok
hd_fail:
    ld      a,#0xFF
hd_ok:
    push    af                      ; outside any timed path
    call    _cleanupLineInterruptsNI
    pop     af
    ei

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
uStopMask:
    .db 0                           ; 0xFF: a measurement ends at the first success (HistCombo.uStopOnSuccess)
uHfOffset:
    .db 0                           ; start position of the next frame's first attempt (0..227)
uVcCap:
    .db 0                           ; vernierCount: max samples
uDriftBlocks:
    .db 0                           ; histDriftPhase: number of 16-line blocks to wait
nRelockLeft:
    .dw 0                           ; histMeasureDelayRelock: attempts left
uLockMisses:
    .db 0                           ; histLock: misses in a row
pVcFine:
    .dw 0                           ; vernierCount: vc_fine_n or vc_fine_t (histInit)
nSyncWait1:
    .dw 0                           ; histLock: SYNC_WAIT1 (-1 on T976x)
nSyncWait2:
    .dw 0                           ; histLock: SYNC_WAIT2 (-1 on T976x)
