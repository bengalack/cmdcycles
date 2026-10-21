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
; Counts loop passes in exactly one frame: 32 cycles per pass, 33 on T976x
; (+1 on the VDP IN). NTSC: ~1867 / ~1810 passes, PAL: ~2230 / ~2162.
; IN F,(C) sets S from bit 7 (the VBLANK flag), so JP P loops until it is set.
; Leaves S#0 selected and line interrupts off.
; OUT:      DE - loop passes in one frame
; MODIFIES: AF, BC, DE
; u16 measureVDPPollsInOneFrame(void);
_measureVDPPollsInOneFrame::
    di
    xor     a
    vdpWriteReg 15                  ; S#0
    ld      c,#VDPPORT1
    .db     0xED,0x70               ; in f,(c) (sdasz80 has no mnemonic): clear a VBLANK flag already pending (>= 15 cycles after the OUT)
    nop                             ; VDP I/O: >= 15 cycles between accesses (14 without this)
mp_sync:
    .db     0xED,0x70               ; in f,(c)
    jp      p,mp_sync               ; frame start (reading clears the flag)

    ld      de,#0
mp_count:                           ; 32 cycles per pass (33 on T976x)
    .db     0xED,0x70               ; 14 (15)  in f,(c): ED prefix = 2 M1 waits
    inc     de                      ;  7
    jp      p,mp_count              ; 11       S = VBLANK flag

    xor     a
    call    _vdpEnableLineInterruptNI ; line interrupts off (vdp.s, modifies A only)
    ei
    ret
