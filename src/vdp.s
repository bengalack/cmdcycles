; ============================================================================
; vdp.s - assembler companion part for main.c
; Note: Any symbol to be reached via C in SDCC is prefixed with an underscore.
; Any parameters are passed according to __sdcccall(1) found here:
; https://sdcc.sourceforge.net/doc/sdccman.pdf
; author: pal.hansen@gmail.com

    .allow_undocumented
    .area _CODE

.include "macros_constants.inc"

; ---------------------------------------------------------------------------
; SHARED DEFINES
; .ifeq / SYMBOLNAME - VALUE_TO_CHECK_FOR / .endif
;
.define VDPCMD_LINE /0b01110000/                ; only upper nibble (lower may vary)

; ----------------------------------------------------------------------------
; LOCAL CONSTANTS

    CRTCNT              .equ 0xF3B1             ; 24 or 26

    VDP_REG0            .equ 0xF3DF             ; line interrupt enable
    LINE_INT_BITMASK    .equ #0b10000           ; to be used with VDP_REG0. Flag(1) means enabled

    VDP_REG1            .equ 0xF3E0             ; ram copy
    SCR_BITMASK         .equ #0b01000000        ; to be used with VDP_REG1. Flag(1) means enabled

    VDP_REG8            .equ 0xFFE7             ; ram copy
    SPR_BITMASK         .equ #0b00000010        ; to be used with VDP_REG8. Flag(1) means disabled

    VDP_REG9            .equ 0xFFE8             ; ram copy
    FRQ_BITMASK         .equ #0b00000010        ; to be used with VDP_REG9. Flag(1) means PAL.
    LINES212_BITMASK    .equ #0b10000000        ; to be used with VDP_REG9. Flag(1) means 212.

    VDPCMD_YMMM         .equ 0b11100000         ; FASTEST COPY BLOCK (only Y differs)


; ----------------------------------------------------------------------------
; EXTERNAL REFERENCES

; ----------------------------------------------------------------------------
; Debug function
; Assumes:
; IN: 		A - char color
; Modifies: A
;-------------------------
vdpSetBorderColorNI::
	out	    (VDPPORT1),a        ; a : [ 0..15 ]
	ld	    a,#128+7
	out	    (VDPPORT1),a
    ret

; ----------------------------------------------------------------------------
; MODIFIES: AF
;
; bool getPALRefreshRate();
_getPALRefreshRate::
    ld      a, (VDP_REG9)
    and     #FRQ_BITMASK
    srl     a
    ret

; ----------------------------------------------------------------------------
; MODIFIES: AF
;
; void vdpSetInterruptLine(u8 uLine);
_vdpSetInterruptLine::
    di
    vdpWriteReg 19
    ei
    ret

; ----------------------------------------------------------------------------
; MODIFIES: AF
;
; void vdpEnableLineInterruptNI(bool bEnable);
_vdpEnableLineInterruptNI::
    or      a
    jr      z,disable_line_interrupt

enable_line_interrupt:
	ld		a,(VDP_REG0)
	or		#LINE_INT_BITMASK
	ld		(VDP_REG0), a
    
    jr      setup_line_interrupt_done

disable_line_interrupt:
	ld		a,(VDP_REG0)
	and		#~LINE_INT_BITMASK
	ld		(VDP_REG0), a

setup_line_interrupt_done:

    vdpWriteReg 0
    ret

; ----------------------------------------------------------------------------
; We also change the number of lines for screen modes
; https://www.msx.org/wiki/VDP_Mode_Registers#Control_Register_9_.28V9938.2F9958.29
; MODIFIES: AF
;
; void vdpSet212Lines(bool b212);
_vdpSet212Lines::
    or      a
    jr      z,disable_212_lines

enable_212_lines:
    ld      a,#26
    ld      (CRTCNT),a
	ld		a,(VDP_REG9)
	or		#LINES212_BITMASK
	ld		(VDP_REG9), a
    
    jr      setup_212_lines_done

disable_212_lines:
    ld      a,#24
    ld      (CRTCNT),a
	ld		a,(VDP_REG9)
	and		#~LINES212_BITMASK
	ld		(VDP_REG9), a

setup_212_lines_done:

    di
    vdpWriteReg 9
    ei
    ret

;-----------------------------------------------
;extern void vdpSpritesEnabled(bool bEnabled);
_vdpSpritesEnabled::

    or      a
    jr      z,disable_sprites

enable_sprites:
	ld		a,(VDP_REG8)
	and		#~SPR_BITMASK
	ld		(VDP_REG8), a
    
    jr      setup_done

disable_sprites:
	ld		a,(VDP_REG8)
	or		#SPR_BITMASK
	ld		(VDP_REG8), a

setup_done:

    di
    vdpWriteReg 8
    ei
    ret

;-----------------------------------------------
;extern void setPALRefreshRate(bool bEnabled);
_setPALRefreshRate::

    or      a
    jr      z,disable_pal_refresh

enable_pal_refresh:
	ld		a,(VDP_REG9)
	or		#FRQ_BITMASK
	ld		(VDP_REG9), a
    
    jr      setup_pal_refresh_done

disable_pal_refresh:
	ld		a,(VDP_REG9)
	and		#~FRQ_BITMASK
	ld		(VDP_REG9), a

setup_pal_refresh_done:

    di
    vdpWriteReg 9
    ei
    ret

;-----------------------------------------------
;extern void vdpScreenEnabled(bool bEnabled);
_vdpScreenEnabled::

    or      a
    jr      z,disable_screen

enable_screen:
	ld		a,(VDP_REG1)
	or		#SCR_BITMASK
	ld		(VDP_REG1), a
    
    jr      setup_done2

disable_screen:
	ld		a,(VDP_REG1)
	and		#~SCR_BITMASK
	ld		(VDP_REG1), a

setup_done2:

    di
    vdpWriteReg 1
    ei
    ret

; -----------------------------------------------------------------------------
; General for block copy, fill and line. Line does not really use 32-35,
; and YMMM does not use 40-41.
; Does everything at (256-w,256-h) in page 0 => same place in page 1
; We pass the planned command, but we do NOT execute it.
; A line has thickness 0, so we need to decrease both horz and vert extent by 1.
; This routine has 16 outs (12 for lines), ie. +16 (12 for lines) outs on some VDPs
; MODIFIES: AF, BC, DE, HL
; void setVDPCmdParamsNI(u8 w, u8 h); A, L 
; void setVDPCmdParamsNI(u8 uCmd, u16 oHW); // oWH: COMBO2BYTES.  A, DE 

_setVDPCmdParamsNI::

    ; (HEIGHT in E, AND WIDTH in D)
    ; ld      e,a
    ; ld      d,l

setVDPCmdParamsNI_asm:: ; call from asm using D: width, E: height, A = CMD (just for testing)
    ; Plan: Put NXL in D, NYL in E, SXL in H, SYL in L

	ld    	c, #VDPSTREAM

    ex      af, af'

    ; Set common upper left corner for all
    xor     a
    sub     d
    ld      h, a

    xor     a
    sub     e
    ld      l, a

    ex      af, af'

    ; do things differently for line command?
    and     #0b11110000
    cp      #VDPCMD_LINE
    jp      nz,not_a_line_command

line_command::
    ld      b,#1
    ld      a,d
    cp      e                   ; if width < height, swap NXL (long) and NYL (short)
    jr      nc,no_swap
    
swap:
    ld      d,e
    ld      e,a
    inc     b
    ; FALL THROUGH

no_swap:
    dec     d
    dec     e
    dec     b                   ; b=0, for all except vertical inclined lines
	ld    	a,#36				; Set "Stream mode" from reg 36
    vdpWriteReg 17
    jp      line_goes_from_here

not_a_line_command:
    ld      b,#0
	ld    	a,#32				; Set "Stream mode" from reg 32
    vdpWriteReg 17

    ld      a,h
	out     (VDPSTREAM),a       ;SXL (32) - IGNORED IN YMMM

    xor     a
	out     (VDPSTREAM),a       ;SXH (33) - IGNORED IN YMMM

    ld      a,l
	out     (VDPSTREAM),a       ;SYL (34)

    xor     a
	out     (VDPSTREAM),a       ;SYH (35) (page)

line_goes_from_here:
    ld      a,h
	out     (VDPSTREAM),a       ;DXL (36) - YMMM: BOTH SOURCE AND DESTINATION

    xor     a
	out     (VDPSTREAM),a       ;DXH (37) - YMMM: BOTH SOURCE AND DESTINATION

    ld      a,l
	out     (VDPSTREAM),a       ;DYL (38)

    ld      a,#1                ;=> page 1
	out     (VDPSTREAM),a       ;DYH (39) (page)

    ld      a,d                 ;WIDTH or LONG
	out     (VDPSTREAM),a       ;NXL or LONG-L (40) - IGNORED IN YMMM

    xor     a
	out     (VDPSTREAM),a       ;NXH or LONG-H (41) - IGNORED IN YMMM

    ld      a,e                 ;HEIGHT or SHORT
	out     (VDPSTREAM),a       ;NYL or SHORT-L (42)

    xor     a
	out     (VDPSTREAM),a       ;NYH or SHORT-H (43)

    ld a,r                      ; for debugging
    ; dec     a                   ;set color 255, for visual debugging
	out     (VDPSTREAM),a       ;COLOR (44)

    ld      a,b                 ;sets directions to rightwards and downwards, and MAYBE he MAJ flag for lines
	out     (VDPSTREAM),a       ;ARG (45)

	ret

;-------------------------
; Modifies: AF,B
; u8 getVDPModel(void);
;-------------------------
_getVDPModel::

	di
	ld	a,#1
    vdpWriteReg 15              ;select status register 1

	in	a,(VDPPORT1)
	and #0b00111110
    rra
    ld b,a    

	xor a
    vdpWriteReg 15              ; restore 0 as selected status reg
    ei

    ld a,b
    ret