; ============================================================================
; iowait.s - detects an extra wait cycle on VDP I/O (T976x engines), in a
; way no interrupt source can disturb.
; The frame is measured by polling the VBLANK flag with interrupts off,
; instead of being ended by the next interrupt (which may come from a
; cartridge).
; Note: Any symbol to be reached via C in SDCC is prefixed with an underscore.
; author: pal.hansen@gmail.com

    .area _CODE

.include "macros_constants.inc"

; ----------------------------------------------------------------------------
; Counts loop passes in exactly one frame: 35 cycles per pass, 36 on T976x
; (+1 on the VDP IN). NTSC: ~1707 / ~1659 passes, PAL: ~2039 / ~1982.
; Leaves S#0 selected and line interrupts off.
; OUT:      DE - loop passes in one frame
; MODIFIES: AF, BC, DE
; u16 measureVDPPollsInOneFrame(void);
_measureVDPPollsInOneFrame::
    di
    xor     a
    vdpWriteReg 15                  ; S#0
    nop                             ; VDP I/O: >= 15 cycles between accesses
    in      a,(VDPPORT1)            ; clear a VBLANK flag that is already pending
    nop
mp_sync:
    in      a,(VDPPORT1)
    rla                             ; VBLANK flag (S#0 bit 7) -> carry
    jp      nc,mp_sync              ; frame start (reading clears the flag)

    ld      bc,#0
mp_count:                           ; 35 cycles per pass (36 on T976x)
    in      a,(VDPPORT1)            ; 12 (13)
    inc     bc                      ;  7
    rla                             ;  5
    jp      nc,mp_count             ; 11

    ld      e,c
    ld      d,b                     ; return in DE
    xor     a
    call    _vdpEnableLineInterruptNI ; line interrupts off (vdp.s, modifies A only)
    ei
    ret
