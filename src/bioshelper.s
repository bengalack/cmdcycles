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

    BIOS_CHPUT          .equ 0x00A2
    CALSLT              .equ 0x001C
    RDSLT               .equ 0x000C
    EXPTBL              .equ 0xFCC1

    BDOS                .equ 0x0005             ; "Basic Disk Operating System"
    BDOS_CONOUT         .equ 0x02               ; DOS Function 02h (_CONOUT), char in E reg
    BDOS_DOSVER         .equ 0x6F
    BDOS_IOCTL          .equ 0x4B
    BDOS_GETDATE        .equ 0x2A
    BDOS_GETTIME        .equ 0x2C

    CHGMOD              .equ 0x005F             ; BIOS routine used to initialize the screen
    LINL40              .equ 0xF3AE             ; 29/30/31... or 80
    SCRMOD              .equ 0xFCAF             ; 

    CHGCPU              .equ 0x0180             ; tame that turbo please
    GETCPU              .equ 0x0183
    CHGET               .equ 0x009F

    SECOND_LINE_INT     .equ 190                ; We are in 212 lines mode

    INITIAL_LOOP_CYCLES .equ 33                 ; must b identical to the one in main.c     

; ----------------------------------------------------------------------------
; EXTERNAL REFERENCES (allowing all/any using the -g flag)
 
; ----------------------------------------------------------------------------
; GetTime... with workaround for buggy behavior in BIOS. For some reason,
; the BIOS call returns bogus data (likely due to bad sync with the RTC chip).
; Hence this routine tries to remedy that by calling the BIOS multiple times,
; until the result is equal to previous.
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
; MODIFIES: all, BIOS... (even shadows!)
; void getTime(DateTime* pDateTime);
_getTime::

    push    ix
    push    hl
    pop     iy

    call    getBoth

    exx

retry_with_exx::
    exx

retry::
    call    storeBoth
    call    getBoth

    ; Start comparing every single new value with the previous,
    ; stored one. All must be equal for us to accept the result.
    ld      a,l
    cp      0(iy)
    jp      nz,retry

    ld      a,h
    cp      1(iy)
    jp      nz,retry

    ld      a,d
    cp      2(iy)
    jp      nz,retry

    ld      a,e
    cp      3(iy)
    jp      nz,retry

    exx
    ld      a,h
    cp      4(iy)
    jp      nz,retry_with_exx

    ld      a,l
    cp      5(iy)
    jp      nz,retry_with_exx

    ld      a,d
    cp      6(iy)
    jp      nz,retry_with_exx

    ; we're good!
    exx                     ; not really needed.

    pop     ix
    ret

storeBoth::
    call    storeDate
    exx
    call    storeTime
    exx
    ret

storeDate::
    ld      0(iy),l
    ld      1(iy),h
    ld      2(iy),d
    ld      3(iy),e
    ret

storeTime::
    ld      4(iy),h
    ld      5(iy),l
    ld      6(iy),d
    ret

getBoth::
    call    getDate_internal
    exx
    call    getTime_internal    ; time data is in alternate regs
    exx
    ret

getDate_internal::
    ld      c,#BDOS_GETDATE
    call    BDOS            ; Returns:
                            ;   HL = Year (e.g., 2026)
                            ;   D  = Month (1-12)
                            ;   E  = Day (1-31)
                            ;   A  = Day of week (0=Sun, 1=Mon...6=Sat)
    dec     d
    dec     e
    ret


getTime_internal::
    ld      c,#BDOS_GETTIME
    call    BDOS            ; Returns:
                            ;   H  = Hours (0-23)
                            ;   L  = Minutes (0-59)
                            ;   D  = Seconds (0-59)
                            ;   E  = 0
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
; Set screen. Strangely, this BIOS call returns in DI, even when entering in EI.
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


; ; ----------------------------------------------------------------------------
; ; Just wait until a key is pressed.
; ; IN:
; ; OUT:      a - key code
; ; MODIFIES: ? (BIOS...)
; ; u8     waitForKey(void);
; _waitForKey::

;     push    ix
;     ld      ix, #CHGET
;     call    callSlot
;     pop     ix

;     ret

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
; Respects the active area. Rigged for the non-halt version.
; Leaves with S#2 set
; _prepareLineInterruptsNI must be called once before this
; one is called (in sequences)
; Must call _cleanupLineInterruptsNI after the sequence of calls
; before interrupt is re-enabled (as we must set S#0)
; IN:       A - VDP command to be sent to port 0x9B
; OUT:      A - number of 33-cycle iterations
; MODIFIES: AF, E
; extern u8 getInitialDelayNI(u8 uVDP_CMD);
_getInitialDelayNI::

    ld      e,a

    ; ALIGN WITH RASTER
    ld      a,(uIYH)
    ld      iyh,a
    call    secureActiveArea ; this one sets status reg #2
    ld      a,iyh
    ld      (uIYH),a

    ld      a,e
    ld      e,#0
    out     (0x9B),a    ; kicks it off

lp:                     ; each loop is 33 cycles
    in      a,(0x99)
    inc     e           ; may affect flags, but don't care
    rrca
    jp      c,lp

    ld      a,e         ; return value
    ret

; -------------------------------------------------------------
; Call it from C. We wrap the IYH value around the asm-routines.
; MODIFIES: AF, IYH
; extern void prepareLineInterruptsNI(void) __preserves_regs(b,c,d,e,h,l,iyl);
_prepareLineInterruptsNI::
    call    prepareLineInterruptsNI
    ld      a,iyh
    ld      (uIYH),a
    ret

; -------------------------------------------------------------
; Helper. Called before test loop. Must be "cleaned up" after.
; MODIFIES: AF, IYH
prepareLineInterruptsNI::
    ld      iyh,#0              ; current interrupt line
    ld      a,iyh
    vdpWriteReg 19              ; sets the line for the interrupt (starting with 0)

    ld      a, #1
    jp      _vdpEnableLineInterruptNI ; modifies A only

; -------------------------------------------------------------
; Helper. Called before test loop. Must be "cleaned up" after.
; MODIFIES:
; extern void cleanupLineInterruptsNI(void) __preserves_regs(b,c,d,e,h,l,iyl,iyh);
_cleanupLineInterruptsNI::
    xor     a
    call    _vdpEnableLineInterruptNI ; modifies A only

	ld		a, #1               ; ensure there are no pending line inter (read flag)
    vdpWriteReg 15
    nop                         ; VDP I/O: >= 15 cycles between accesses (OUT -> IN is 12 without this)
    in      a,(VDPPORT1)        ; read VDP S#n to reset VBLANK IRQ (F flag is set on VBLANK int)

	xor     a                   ; ensure there are no interrupts (read flag)
    vdpWriteReg 15
    nop                         ; VDP I/O: >= 15 cycles between accesses (OUT -> IN is 12 without this)
    in      a,(VDPPORT1)        ; read VDP S#n to reset VBLANK IRQ (F flag is set on VBLANK int)

    ret

; -------------------------------------------------------------
; Helper. Leaves with S#2 set. Assumes line ints enabled.
; IN:       IYH: current line interrupt line (0 or SECOND_LINE_INT)
; OUT:      IYH: next line interrupt line (0 or SECOND_LINE_INT)
; MODIFIES: AF, IYH
secureActiveArea::
	ld		a,#1                ; we will look for pending line interrupts
    vdpWriteReg 15

    ld      a,iyh               ; if this one is 0, we are NOT in the active area, and must wait
    or      a
    jp      nz,_in_active_area

; --------------------
in_vblank:
    in      a,(VDPPORT1)
    rra
    jp      nc,in_vblank        ; 11 cycles. total: 28. Loop until we are in active area (FH: bit 0)

    ld      a,#SECOND_LINE_INT  ; toggle 0 and / 190
    ld      iyh,a
    vdpWriteReg 19              ; sets the line for the next interrupt
    jp      goodbye

; --------------------
_in_active_area:
    in      a,(VDPPORT1)        ; FH bit is in bit 0 (https://www.msx.org/wiki/VDP_Status_Registers#Status_Register_1)
    rra
    jr      nc,goodbye

    xor     a                   ; toggle 0 and / 190
    ld      iyh,a
    vdpWriteReg 19              ; sets the line for the next interrupt
    jp      in_vblank           ; past line 190: no attempt now, wait for the active area (line 0)

goodbye:
	ld		a,#2                ; leave with S#2 set
    vdpWriteReg 15

    ret

; ----------------------------------------------------------------------------
    .area _DATA

uIYH:
    .db 0                       ; store across C-functions (to make multiple algorithms use the same code)

