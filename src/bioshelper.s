; ============================================================================
; bioshelper.s - assembler companion part for main.c (some handy dos+ routines)
; Note: Any symbol to be reached via C in SDCC is prefixed with an underscore.
; Any parameters are passed according to __sdcccall(1) found here:
; https://sdcc.sourceforge.net/doc/sdccman.pdf
; author: pal.hansen@gmail.com

    .allow_undocumented
    .area _CODE

.include "macros_constants.inc"

; ----------------------------------------------------------------------------
; LOCAL CONSTANTS

    BIOS_CHPUT      .equ 0x00A2
    CALSLT          .equ 0x001C
    RDSLT           .equ 0x000C
    EXPTBL          .equ 0xFCC1

    BDOS            .equ 0x0005             ; "Basic Disk Operating System"
    BDOS_CONOUT     .equ 0x02               ; DOS Function 02h (_CONOUT), char in E reg
    BDOS_DOSVER     .equ 0x6F
    BDOS_IOCTL      .equ 0x4B
    BDOS_GETDATE    .equ 0x2A
    BDOS_GETTIME    .equ 0x2C

    CHGMOD          .equ 0x005F             ; BIOS routine used to initialize the screen
    LINL40          .equ 0xF3AE             ; 29/30/31... or 80
    SCRMOD          .equ 0xFCAF             ; 
    
    CHGCPU          .equ 0x0180             ; tame that turbo please
    GETCPU          .equ 0x0183
    CHGET           .equ 0x009F

; ----------------------------------------------------------------------------
; EXTERNAL REFERENCES (allowing all/any using the -g flag)
 
; ----------------------------------------------------------------------------
;
; Totals:  cycles
; MODIFIES: (No registers of course!)
_customISR::
    push	af

    ; ; line ints
	; ld		a, #1
    ; vdpWriteReg 15
    ; nop
	; in		a, (VDPPORT1)			; Clear line int flag.
	; ; rra										; is the scanline-flag (bit 0) set?
  	; ; jp 		c, line_interrupts          ; we do not need to always do something...

    ; the normal int we must read
    xor 	a                       ; get status for sreg 0 (we anyway need to reset sreg)
    vdpWriteReg 15
    nop								; obey speed
    in		a, (VDPPORT1)			; read VDP S#n to reset VBLANK IRQ

    pop		af
    ei
    ret

; ----------------------------------------------------------------------------
;  typedef struct {
;     u16 nYear;    // 0 - 1980-
;     u8  uMonth;   // 2 - 0-11
;     u8  uDay;     // 3 - 0-30
;     u8  uHours;   // 4 - 0-23
;     u8  uMinutes; // 5 - 0-59
;     u8  uSeconds; // 6 - 0-59
; } DateTime;
; IN:       HL - pointer to DateTime object
; OUT:      -
; RETURNS:  Indirectly. The input parameter data is filled with the current date and time.
; MODIFIES: ? (BIOS...)
; void getTime(DateTime* pDateTime);
_getTime::

    push    ix
    push    hl
    pop     iy

    ; --- Read Date ---

    ld      c,#BDOS_GETDATE
    call    BDOS            ; Returns:
                            ;   HL = Year (e.g., 2026)
                            ;   D  = Month (1-12)
                            ;   E  = Day (1-31)
                            ;   A  = Day of week (0=Sun, 1=Mon...6=Sat)

    dec     d
    dec     e

    ld      0(iy),l
    ld      1(iy),h
    ld      2(iy),d
    ld      3(iy),e

    ; --- Read Time ---
    ld      c,#BDOS_GETTIME
    call    BDOS            ; Returns:
                            ;   H  = Hours (0-23)
                            ;   L  = Minutes (0-59)
                            ;   D  = Seconds (0-59)
                            ;   E  = 0

    ld      4(iy),h
    ld      5(iy),l
    ld      6(iy),d

    pop     ix
    ret

; ----------------------------------------------------------------------------
; Print to console. Both '\r\n' is needed for a carriage return and newline.
; Heavy(!), as it does interslot calls per character (but print performance is
; of no concern in this program)
; IN:       HL - pointer to zero-terminated string
; MODIFIES: ? (BIOS...)
; void print(u8* szMessage)
_print::

    ; ; BDOS Variant (needs $ as ending character)
    ; ex      de, hl                  ; p to msg in de
    ; ld      c, #BDOS_STROUT         ; function code
    ; jp      BDOS

    push    ix

loop:
	ld      a, (hl)
	and     a
	jr      z, leave_me

    ld      e,a
    ld      c,#BDOS_CONOUT

    push    hl
    call    BDOS
    pop     hl

	inc     hl
	jr      loop

;     ; BIOS variant (does not support re-direction)
;     push    ix
; loop:
; 	ld      a, (hl)
; 	and     a
; 	jr      z, leave_me
;     ld      ix, #BIOS_CHPUT
;     call    callSlot

; 	inc     hl
; 	jr      loop

leave_me:
    pop     ix
    ret

; ----------------------------------------------------------------------------
; Uses Matsushita device
; from here: https://map.grauw.nl/resources/msx_io_ports.php#expanded_io
;
; MODIFIES:     AF, BC
; RETURN:       A (bool)
;
; bool hasTurboFeature(void) __preserves_regs(d,e,h,l,iyl,iyh);
_hasTurboFeature::

    ld      b, #0           ; return value, default 0 (false)
    in      a,(0x40)
    cpl
    ld      c,a
    ld      a,#8
    out     (0x40),a        ; out the manufacturer code 8 (Panasonic) to I/O port 40h
    in      a,(0x40)        ; read the value you have just written
    cpl                     ; complement all bits of the value
    cp      #8              ; if it does not match the value you originally wrote,
    jr      nz,bye_bye      ; it does not have the Panasonic expanded I/O ports
    in      a,(0x41)
    bit     2,a             ; is turbo mode available?
    jr      nz,bye_bye
    ld      b, #1           ; yes, it is enabled

bye_bye:

    ld      a,c
    out     (0x40),a
    ld      a, b
    ret

; ----------------------------------------------------------------------------
; Uses Matsushita device
; from here: https://map.grauw.nl/resources/msx_io_ports.php#expanded_io
;
; MODIFIES:     AF, BC
; RETURN:       A (bool)
;
; bool isTurboEnabled(void) __preserves_regs(d,e,h,l,iyl,iyh);
_isTurboEnabled::

    ld      c,#0x40
    in      a,(c)
    cpl
    ld      b,a
    ld      a,#8
    out     (c),a        ; out the manufacturer code 8 (Panasonic) to I/O port 40h

    in      a,(0x41)
    rra                     ; bit 0: is turbo mode on? 0==on
    ld      a,#0
    jr      c,bye_bye2
    inc     a
bye_bye2:
    out     (c),b
    ret

; ----------------------------------------------------------------------------
; Uses Matsushita device
; from here: https://map.grauw.nl/resources/msx_io_ports.php#expanded_io
;
; bit 0 is turbo or not. 0=turbo, 1=normal
;
; MODIFIES: AF, BC, D
;
; void enableTurbo(bool bEnable) __preserves_regs(e,h,l,iyl,iyh);
_enableTurbo::

    xor     #1              ; flip the bit
    ld      b,a

    ld      c,#0x40
    in      a,(c)
    cpl
    ld      d,a
    ld      a,#8
    out     (c),a           ; out the manufacturer code 8 (Panasonic) to I/O port 40h

    in      a,(0x41)
    and     #0b11111110
    or      b
    out     (0x41),a        ; enable turbo(?)

    out     (c),d           ; retstore org device
    ret
    
; ----------------------------------------------------------------------------
; MSX version number http://map.grauw.nl/resources/msxsystemvars.php
;
; 0 = MSX 1
; 1 = MSX 2
; 2 = MSX 2+
; 3 = MSX turbo R
;
; MODIFIES: ? (BIOS...)
; u8 getMSXType()
_getMSXType::
    push    ix                  ; just in case, as SDCC is peculiar about this register
    ld      a, (EXPTBL)         ; BIOS slot
    ld      hl, #0x002D         ; Location to read
    di
    call    RDSLT               ; interslot call. RDSLT needs slot in A, returns value in A. address in HL
    pop     ix
    ret

; --------------------
; Tiny internal helper
; IN:       IX: address of BIOS routine
callSlot:
    ld     iy, (EXPTBL-1)       ;BIOS slot in iyh
    jp      CALSLT              ;interslot call

; ----------------------------------------------------------------------------
; https://map.grauw.nl/resources/msxbios.php#msxtrbios
; IN:  A = 0 0 0 0 0 0 x x
;                      0 0 = Z80 (ROM) mode
;                      0 1 = R800 ROM  mode
;                      1 0 = R800 DRAM mode
;
; MODIFIES: ? (BIOS...)
; void change CPU();
_changeCPU::

    push    ix
    ld      ix, #CHGCPU
    call    callSlot
    pop     ix
    ret

; ----------------------------------------------------------------------------
; https://map.grauw.nl/resources/msxbios.php#msxtrbios
; OUT: A = 0 0 0 0 0 0 x x
;                      0 0 = Z80 (ROM) mode
;                      0 1 = R800 ROM  mode
;                      1 0 = R800 DRAM mode
;
; MODIFIES: ? (BIOS...)
; u8 getCPU();
_getCPU::

    push    ix
    ld      ix, #GETCPU
    call    callSlot
    pop     ix
    ret

; ----------------------------------------------------------------------------
; Set screen.
; IN:       A - mode, as in screen (https://www.msx.org/wiki/SCREEN)
; OUT:      A - previous mode
; MODIFIES: ? (BIOS...)
; u8 changeMode(u8 uModeNum)
_changeMode::

    ld      b,a
    ld      a,(SCRMOD)
    push    af
    ld      a,b

    push    ix
    ld      ix, #CHGMOD
    call    callSlot
    pop     ix

    pop     af
    ret


; ----------------------------------------------------------------------------
; Just wait until a key is pressed.
; IN:
; OUT:      a - key code
; MODIFIES: ? (BIOS...)
; u8     waitForKey(void);
_waitForKey::

    push    ix
    ld      ix, #CHGET
    call    callSlot
    pop     ix

    ret

; ----------------------------------------------------------------------------
; Check what user has issued at command line.
; IN:
; OUT:      a - boolean, true if user output is to screen, false if redirected to file/pipe
; MODIFIES: ? (BIOS...)
; bool userOutputsToScreen(void);
_userOutputsToScreen::
    ld      c,#BDOS_DOSVER
    call    BDOS
    or      a               ; If A != 0, it's MSX-DOS 1 (Function 6Fh doesn't exist)
    jr      nz,.is_dos1
    
    ld      a,b             ; B holds the MSX-DOS kernel major version
    cp      #2
    jr      c,.is_dos1      ; If version < 2, treat as DOS 1

    ; 2. We are on MSX-DOS 2+: Safe to call _IOCTL
    ld      c,#BDOS_IOCTL
    ld      a,#0            ; Subfunction 0: Get channel attributes
    ld      b,#1            ; Handle 1 = STDOUT
    call    BDOS

    or      a               ; Check error code
    jr      nz,.not_piped   ; If call failed, default to standard screen output

    bit     7,e             ; Bit 7 = 0 if redirected to a file/pipe
    jr      z,.is_piped
    jr      .not_piped

.is_dos1:                   ; MSX-DOS 1 has no redirection/pipes, output is always directly to console
.not_piped:                 ; Direct screen output
    ld a,#1                 ; bool:true
    ret

.is_piped:                  ; Redirected output
    xor a                   ; bool:false
    ret

; ----------------------------------------------------------------------------
; Set linewidth, accepts 40 or 80, I believe. Needs changeMode call after this.
; IN:       A
; MODIFIES: ? (BIOS...)
; void setLineWidth(u8 uWidth)
_setLineWidth::
    ld     (LINL40),a
    ret

; -------------------------------------------------------------
; IN:       A - VDP command to be sent to port 0x9B
; OUT:      A - number of 33-cycle iterations
; MODIFIES: AF, E
_getInitialDelayNI::

    ld      e,a

	ld		a, #2
    vdpWriteReg 15      ; set status reg #2

    ld      a,e
    ld      e,#0
    out     (0x9B),a    ; kicks it off

lp:                     ; each loop is 33 cycles
    in      a,(0x99)
    inc     e           ; may affect flags, but don't care
    rrca
    jp      c,lp

	xor     a
    vdpWriteReg 15      ; set status reg #0

    ld      a,e         ; return value
    ret

; -------------------------------------------------------------
; Wait one third of a line (227.5/3 = almost 78 cycles)
; MODIFIES: Nothing!
waitOneThirdOfRasterLine:
    
    ; calling this  ; 18
    push    af      ; 12
    jr      .+2    ; 13
    jr      .+2    ; 13
    pop     af      ; 11
    ret             ; 11 (=78)

; -------------------------------------------------------------
; Counts downwards.
; IN:       HL current
; OUT:      DE next
; MODIFIES: AF,DE
; returns 0 when there are none
getNextPossibleDelay:

    ld      a,h
    or      a
    jr      nz,one_lower ; > 255 is: -= 1

    ld      a,l
    cp      #20
    jr      nc,one_lower ; >=20 is: -= 1

    ld      d,#0            ; otherwise (0-19); look up value below
    push    hl
    ld      hl,#mini_table
    addAtoHL
    ld      e,(hl)
    pop     hl
    ret

one_lower::
    ld      d,h
    ld      e,l
    dec     de
    ret

mini_table: ; (given a previous delay, which is the next, valid, lower delay)
    .db     0       ; 0
    .db     0       ; 1
    .db     0       ; 2
    .db     0       ; 3
    .db     0       ; 4
    .db     0       ; 5
    .db     0       ; 6
    .db     0       ; 7
    .db     0       ; 8
    .db     0       ; 9
    .db     0       ;10
    .db     0       ;11
    .db     0       ;12
    .db     12      ;13
    .db     12      ;14
    .db     14      ;15
    .db     14      ;16
    .db     14      ;17
    .db     17      ;18
    .db     17      ;19

; -------------------------------------------------------------
; IN:       IY: pointer to struct
; MODIFIES: AF
setVDPParams:
    push    bc
    push    de
    push    hl
    ld      e,0(iy)
    ld      d,1(iy)
    call    setVDPCmdParamsNI_asm ; E: width, D: height (MODIFIES: AF, C, DE, HL)
    pop     hl
    pop     de
    pop     bc

    ret

; -------------------------------------------------------------
testVDP:
    push    bc
    push    de
    push    hl
    ex      de,hl ; test with DE (forget current HL)
    call    dispatch_vdp_test ; C = Cmd, HL = delay. Result in A
    pop     hl
    pop     de
    pop     bc
    ret

; -------------------------------------------------------------
; Assumes screen disabled.
; If value proves to work. we will try one value below, at least
; <x> amount of times. Stop when <x> attempts all fail (busy).
; IN:
;    typedef struct {
;        u8                      uW;            0
;        u8                      uH;            1
;        u8                      uCmd;          2
;        u8                      uIterations;   3
;        u16                     nStartDelay;   4
;        u8                      uFirstWait;    6
;    } RunCombo;
; OUT:
; MODIFIES:
; u16 runTestCombo(RunCombo* p); HL
_runTestCombo::
    ; in a,(0x2e)

    ; ld      b,#1                ; random number sets the times we do this
    ld      a,(_g_uOuterIterations)
    ld      b,a
    ex      de,hl
    ld      iyl,e 
    ld      iyh,d               ; put pointer to struct in IY

    ld      c,#0

mer:
    push    bc
    di
    call    runTestCombo_inner
    pop     bc 
    inc     c
    djnz    mer

	xor     a
    vdpWriteReg 15              ; set status reg #0

    ei
    ex      de,hl
    ret                         ; SDCC needs return in DE

runTestCombo_inner::            ; IY: Pointer to paramsreturns in HL, the best delay found.

    ; wait?
    xor     a
    ld      b,6(iy)
    or      b
    jr      z,loopr_start

    ei
    halt
    di
    ld      a,c
    add     b                   ; TODO FIX THIS
    ld      b,a                 ; this makes delays increase for each round (may overflow)

    ; call    waitOneThirdOfRasterLine
    ; call    waitOneThirdOfRasterLine
    ; call    waitOneThirdOfRasterLine
    ; call    waitOneThirdOfRasterLine

wloop::
    ; call    waitOneThirdOfRasterLine
    djnz    wloop

	ld		a, #2
    vdpWriteReg 15              ; set status reg #2


    ; the real loop
loopr_start::
    ld      c,2(iy)             ; the command in C
    ld      l,4(iy)
    ld      h,5(iy)             ; HL: Current Greenlit delay
    ld      e,l
    ld      d,h                 ; DE: Current Attempting delay
    ld      b,3(iy)             ; the initial number of iterations per try

    xor     a
    or      b
    ret     z                   ; if NO iterations, we just return. this is wrong

loopr::
    call    setVDPParams
    call    testVDP             ; C = Cmd, DE = delay. Result in A
    rr      a                   ; CE bit goes into C, but if all we are ZERO here, something is wrong
    jr      z,bail              ; if the full value is 0 (impossible status reg), we have tried an impossible wait value
    jp      nc,job_was_done

job_was_not_done::              ; if not done, we must try until all attempts are done 
    djnz    loopr               ; just try again with same values
    jp      bail                ; if we get here, we are out of attempts.

job_was_done::
    ld      b,3(iy)             ; success, so restart testing but try one lower
    ld      h,d
    ld      l,e                 
    call    getNextPossibleDelay
    ld      a,d
    or      e
    jr      nz,loopr            ; if next possible delay == 0, we bail

bail::
    ld      4(iy),l             ; updating this struct value in case we do new runs
    ld      5(iy),h             ; 

    ret

; -------------------------------------------------------------
; Wild guess, if this really works.
; IN:       -
; OUT:      A - bool
; MODIFIES: AF
; bool hasT976xEngine(void)
_hasT976xEngine::

    in      a, (0xF4)           ; Read internal T9769 status register
    cp      #0xFF               ; Is it unmapped / floating open bus?
    jr      z,nothing_here

    ld      a,#1                ; yes, it is a T976x engine
    ret

nothing_here:
    xor     a                   ; Yamaha (S3527/S1985) or discrete ASIC
    ret



