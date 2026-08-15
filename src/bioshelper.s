; ============================================================================
; bioshelper.s - assembler companion part for main.c (some handy dos+ routines)
; Note: Any symbol to be reached via C in SDCC is prefixed with an underscore.
; Any parameters are passed according to __sdcccall(1) found here:
; https://sdcc.sourceforge.net/doc/sdccman.pdf
; author: pal.hansen@gmail.com

    .allow_undocumented
    .area _CODE

.include "macros_constants.inc"

; ---------------------------------------------------------------------------
; Include shared constants!
; .ifeq / SYMBOLNAME - VALUE_TO_CHECK_FOR / .endif
.macro DEF sym eq val ?whatever
.define sym /val/
.endm
.include "shared_constants.def"

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

    SECOND_LINE_INT     .equ 190                ; at line 200 (gives ~21 lines, testing shows it is needed)

    INITIAL_LOOP_CYCLES .equ 33                 ; must b identical to the one in main.c     

; ----------------------------------------------------------------------------
; EXTERNAL REFERENCES (allowing all/any using the -g flag)
 
; ; ----------------------------------------------------------------------------
; ; This special interrupt routine leaves with S#2 set!
; ; Uses 
; ; Totals:  cycles
; ; MODIFIES: (No registers of course!)
; _customISR::
;     push	af
;     push    hl

;     ld      hl,#_g_uCurrentInterruptLine

;     ; the normal int we must read - check if this was a line interrupt or not
;     xor 	a                       ; get status for sreg 0 (we anyway need to reset sreg)
;     vdpWriteReg 15
;     nop								; obey speed
;     in		a, (VDPPORT1)			; read VDP S#n to reset VBLANK IRQ (F flag is set on VBLANK int)
;                                     ; bit 7	bit 6	bit 5	bit 4	bit 3	bit 2	bit 1	bit 0	
;                                     ; F	    5S	    C	    5/9SN
;     rla
    
;     jp      nc,test_line_interrupt

;     xor     a
;     ld      (_g_bACTIVE_AREA),a     ; we are in VBLANK, so set this to false

;     jp      set_line_interrupt

; test_line_interrupt:
; 	ld		a, #1
;     vdpWriteReg 15
;     nop
; 	in		a, (VDPPORT1)			; Clear line int flag.

;     ld      a,(hl)
;     or      a

;     jp      z,set_line_interrupt_x  ; if we have line 0 set, the next is line: SECOND_LINE_INT (a)

;     ; if we get here, bottommost line int is reached, and we turn off line interrupts
;     xor     a
;     ld      (_g_bACTIVE_AREA),a
;     call    _vdpEnableLineInterruptNI ; disable them (routine in vdp.s)
;     jp      leave_isr

; set_line_interrupt_x:
;     ld      a,#1
;     ld      (_g_bACTIVE_AREA),a
;     ld      a,#SECOND_LINE_INT      ; prepare reg

; set_line_interrupt: ; A: line
;     ld      (hl),a
;     vdpWriteReg 19                  ; sets value

;     ld      a, #1
;     call    _vdpEnableLineInterruptNI ; enable them (routine in vdp.s)

;     ; fall through

; leave_isr:
; 	ld		a, #2                   ; leave with S#2 set
;     vdpWriteReg 15

;     pop     hl
;     pop		af
;     ei
;     ret

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
    ld      e,0(ix)
    ld      d,1(ix)
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

; ; -------------------------------------------------------------
; ; Assumes there is alreay a JP XXXX at 0x0038 (it is always in DOS)
; ; stores the current address, and sets the new one
; ; MODIFIES: HL
; setCustomISRNI:
;     ld  hl,(0x0039)
;     ld  (_g_pInterruptOrg),hl
;     ld  hl,#_customISR
;     ld  (0x0039),hl

;     ret

; ; -------------------------------------------------------------
; ; Also restores S#0 as current status reg.
; ; MODIFIES: HL
; restoreISRAndSetS0NoLineInt_NI:
;     ld  hl,(_g_pInterruptOrg)
;     ld  (0x0039),hl

;     xor     a
;     ld      (_g_bACTIVE_AREA),a
;     call    _vdpEnableLineInterruptNI ; disable them (routine in vdp.s)

;     ld      a, #0
;     vdpWriteReg 15              ; set status reg #0
;     ret

; ; -------------------------------------------------------------
; ; 
; ; MODIFIES: A
; sync_with_active_area::
;     halt
;     ld      a,(_g_bACTIVE_AREA)
;     or      a
;     jp      z,sync_with_active_area
;     ret

; -------------------------------------------------------------
; Called in EI
; If value proves to work. we will try one value below, at least
; <x> amount of times. Stop when <x> attempts all fail (busy).
; IN:
;
; IN:       HL - pointer to struct
;
;           typedef struct {
;               u8                      uW;                               0
;               u8                      uH;                               1
;               u8                      uCmd;                             2
;               u8                      uInnerIterations;                 3
;               u16                     nStartDelay;                      4 & 5
;               u8                      uOuterIterations;                 6
;               u16*                    pHistogramWinner;                 7 & 8
;           } RunCombo;
;
; OUT:      DE - best delay found
;           RunCombo.nStartDelay: best delay found
;           RunCombo.anHistogramLastIndex: updated
; MODIFIES:
; u16 runTestCombo(RunCombo* p, u16* anHistogramLastIndex); HL
_runTestCombo::
    ; in a,(0x2e)

;     push    ix

;     ex      de,hl
;     ld      ixl,e 
;     ld      ixh,d               ; put pointer to struct in IX

;     ld      b,6(ix)             ; outer iterations
;     ld      c,#0

;     ; initialize interrupt
;     halt
;     di
;     call    setCustomISRNI      ; DI barely needed this fast after halt, but just as good practise
;     ei

; encore_une_fois::
;     push    bc                  ; B(outer): Outer iterations.
;     ld      b,3(ix)             ; inner iterations
;     ld      c,2(ix)             ; C: command
;     ld      l,4(ix)
;     ld      h,5(ix)             ; HL: Current Greenlit delay
;     ld      e,l
;     ld      d,h                 ; DE: Current Attempting delay
;     ld      iyl,#0              ; wait-index
;     call    runTestComboInner
;     pop     bc 
;     djnz    encore_une_fois

;     ex      de,hl               ; SDCC needs return in DE

;     di
;     call    restoreISRAndSetS0NoLineInt_NI ; turn off. Next int is normal VBLANK. no line ints are on.
;     ei

;     pop     ix
;     ret

; runTestComboInner::             ; IX: Pointer to params. returns in HL, the best delay found.

;     call    sync_with_active_area

;     push    hl
;     ld      a,iyl
;     call    doControlledWait    ; A is input. constant wait in cycles + A value.
;     pop     hl

; loopr::
;     di
;     call    setVDPParams        ; this one will add several extra cycles on Toshiba models
;     call    testVDP             ; C = Cmd, DE = delay. Result in A
;     ei
;     rr      a                   ; CE bit goes into C, but if all we are ZERO here, something is wrong
;     jr      z,bail              ; if the full value is 0 (impossible status reg), we have tried an impossible wait value
;     jp      nc,job_was_done

; job_was_not_done::              ; if not done, we must try until all attempts are done 

;     ld      a,(_g_bACTIVE_AREA) ; ensure we are still in active area and have time for a new attempt.
;     or      a
;     call    z,sync_with_active_area ; if not, we wait until we are in active area again

;     ; ld      (0xAAAA),bc         

;     djnz    loopr               ; just try again with same values

;     ld      a,#12               
;     cp      iyl                 ; ensure we jump over the gap
;     jp      z,bail              ; if we get here, we are out of ixl controlled attempts.

;     ld      b,3(ix)             ; reset inner iterations 
;     inc     iyl                 ; same run, but +1 cycle wait
;     jp      runTestComboInner   ; if next possible delay == 0, we bail

; job_was_done::
;     ld      b,3(ix)             ; success, so restart testing but try one lower DE value
;     ld      iyl,#0              ; starts with a resetted wait index
;     ld      h,d
;     ld      l,e                 
;     call    getNextPossibleDelay
;     ld      a,d
;     or      e
;     jp      nz,loopr            ; it will be much faster to go to this each time is it done. timely reset can be done when job was not done
;     ; jp      nz,runTestComboInner; if next possible delay == 0, we bail
;     ; FALL THROUGH

; bail::
;     ld      4(ix),l             ; updating this struct value in case we do new runs
;     ld      5(ix),h             ; 

    ret

; -------------------------------------------------------------
; This function will always take "65 + 14 + A" cycles (79 + A)
; (+ 18 cycles for the call itself)
; IN:       A - wait index (0-15): 
; MODIFIES: AF, HL
doControlledWait::
    ld      hl,#wait_jump_table

    add     a,a
    addAtoHL                    ; constant cycle use inside this

    ld      a,(hl)
    inc     hl
    ld      h,(hl)
    ld      l,a

    jp      (hl)

wait_jump_table:: ; some of these may trash HL
    .dw delay_stub0
    .dw delay_stub1
    .dw delay_stub2
    .dw delay_stub3
    .dw delay_stub4
    .dw delay_stub5
    .dw delay_stub6
    .dw delay_stub7
    .dw delay_stub8
    .dw delay_stub9
    .dw delay_stub10
    .dw delay_stub11
    .dw delay_stub12
    .dw delay_stub13
    .dw delay_stub14
    .dw delay_stub15

delay_stub0::
    delay14
    ret
delay_stub1::
    delay15
    ret
delay_stub2::
    delay16
    ret    
delay_stub3::
    delay17
    ret
delay_stub4::
    delay18
    ret
delay_stub5::
    delay19
    ret
delay_stub6::
    delay20
    ret
delay_stub7::
    delay21
    ret
delay_stub8::
    delay22
    ret
delay_stub9::
    delay23
    ret
delay_stub10::
    delay24
    ret
delay_stub11::
    delay25
    ret
delay_stub12::
    delay26
    ret
delay_stub13::
    delay27
    ret
delay_stub14::
    delay28
    ret
delay_stub15::
    delay29
    ret


; ----------------------------------
; Macros "for readability only"
; Cost: 33+42 (255 of 256 runs) or 59+42 (1 of 256 runs)
; modifies F, HL'
.macro updateHistogram ?local_knob
    exx                         ; 5
    inc     (hl)                ; 12, affects z-flag!
    jp      nz,local_knob       ; 13/8
    inc     hl                  ; 7
    inc     (hl)                ; 12
    dec     hl                  ; 7
local_knob:
    exx                         ; 5
.endm

.macro histogram_advance_and_record ?local_knob2
    exx
    ld      7(ix),l             ; 21
    ld      8(ix),h             ; 21
    dec     hl
    dec     hl

;     ; test for out of bounds
;     ld      a,#<_g_anScratchHistogram
;     cp      l
;     jr      nz,local_knob2
;     ld      a,#>_g_anScratchHistogram
;     cp      h
;     jr      nz,local_knob2

;     in      a,(x2e) ; stop if we are out of bounds

; local_knob2:
    exx
.endm

; .macro histogram_backtrack
;     exx
;     inc     hl
;     inc     hl
;     exx
; .endm

; -------------------------------------------------------------
; Called in EI
; Assumes anHistogram is already initialized to 0. 
; No HALT, just switching status registers all the time and
; checking if we are in active area or not. Not setting
; custome ISR.
; If value proves to work. we will try one value below, at least
; <x> amount of times. Stop when <x> attempts all fail (busy).
; 
; Risk: Not properly catered for if histogram runs out of bounds. TODO?
;
; IN:       HL - pointer to struct
;
;           typedef struct {
;               u8                      uW;                               0
;               u8                      uH;                               1
;               u8                      uCmd;                             2
;               u8                      uInnerIterations;                 3
;               u16                     nStartDelay;                      4 & 5
;               u8                      uOuterIterations;                 6
;               u16*                    pHistogramWinner;                 7 & 8
;           } RunCombo;
;
; OUT:      DE - best delay found
;           anHistogramLastIndex: updated
; MODIFIES: AF, BC, DE, HL, IY, HL', DE'
; u16 runTestCombo(RunCombo* p, u16* anHistogramLastIndex); HL, DE
_runTestComboNoHalts::
    ; in a,(0x2e)
    push    ix

    push    de
    exx
    pop     hl
    exx                             ; HL' holds pointer to u16 anHistogram[HISTOGRAM_BUFFER-1], we'll populate the 16bit array backwards

    ex      de,hl
    ld      ixl,e 
    ld      ixh,d                   ; put pointer to struct in IX

    di
    call    prepareLineInterruptsNI ; ixl: will hold line-interrupt line number (0 or SECOND_LINE_INT). 

    ld      b,6(ix)                 ; outer iterations
    ld      c,2(ix)                 ; C: command

    ld      l,4(ix)
    ld      h,5(ix)                 ; HL: Current Greenlit delay
    ld      e,l
    ld      d,h                     ; DE: Current Attempting delay

outer_loop::
    push    bc                      ; store for outer loop
    ld      b,3(ix)                 ; inner iterations
    ld      iyl,#0                  ; wait-index

    call    x_runTestComboInner

    pop     bc                      ; restore for outer looping
    djnz    outer_loop

    ; exx
    ; in      a,(0x2e)                ; just for debugging: stop to see histogram in mem
    ; exx

    call    _cleanupLineInterruptsNI

    ei
    ex      de,hl                   ; SDCC needs return in DE
    pop     ix
    ret

x_runTestComboInner::               ; IX: Pointer to params. returns in HL, the best delay found.

    call    secureActiveArea

.ifeq METHOD_WAIT - 1                ; counter
    push    hl
    ld      a,iyl
    call    doControlledWait        ; A is input. constant wait in cycles + A value.
    pop     hl
.else
.ifeq METHOD_WAIT - 2                ; random
    push    hl
    ld      a,r
    and     #0x0f
    call    doControlledWait        ; A is input. constant wait in cycles + A value.
    pop     hl
.else
                                    ; No wait
.endif
.endif

x_loopr::
    updateHistogram                 ; x cycles. put in a macro to make it more readable.
    call    setVDPParams            ; this one will add several extra cycles on Toshiba models
    call    testVDP                 ; C = Cmd, DE = delay. Result in A
    rr      a                       ; CE bit goes into C, but if all we are ZERO here, something is wrong
    ret     z                       ; if the full value is 0 (impossible status reg), we have tried an impossible wait value
    jp      nc,x_job_was_done

x_job_was_not_done::                ; if not done, we must try until all attempts are done 

    call    secureActiveArea
    djnz    x_loopr                 ; just try again with same values

    ld      a,#12
    cp      iyl                     ; ensure we jump over the gap
    ret     z                       ; if we get here, we are out of iyl controlled attempts.

    ld      b,3(ix)                 ; reset inner iterations 
    inc     iyl                     ; same run, but different value in doControlledWait
    jp      x_runTestComboInner 

x_job_was_done::
    histogram_advance_and_record
    ld      b,3(ix)                 ; success, so restart testing but try one lower DE value
    ld      iyl,#0                  ; starts with a resetted wait index (0), works with x_runTestComboInner, gives (1) if we loop to x_loopr below (ok, then).
    ld      h,d
    ld      l,e                 
    call    getNextPossibleDelay

    ld      a,d
    or      e
    jp      nz,x_runTestComboInner  ; if next possible delay == 0, we bail
    ; jp      nz,x_loopr              ; goes out of display area... it will be much faster to go to this each time is it done. timely reset can be done when job was not done
    ret                             ; we actually hit 12 (which only happens in emulators)

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
    in      a,(VDPPORT1)        ; read VDP S#n to reset VBLANK IRQ (F flag is set on VBLANK int)

	xor     a                   ; ensure there are no interrupts (read flag)
    vdpWriteReg 15
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

    ld      a,#SECOND_LINE_INT  ; toggle 0 and / 200
    ld      iyh,a
    vdpWriteReg 19              ; sets the line for the next interrupt
    jp      goodbye

; --------------------
_in_active_area:
    in      a,(VDPPORT1)        ; FH bit is in bit 0 (https://www.msx.org/wiki/VDP_Status_Registers#Status_Register_1)
    rra
    jr      nc,goodbye

    xor     a                   ; toggle 0 and / 200
    ld      iyh,a
    vdpWriteReg 19              ; sets the line for the next interrupt
    ; FALL THROUGH

goodbye:
	ld		a,#2                ; leave with S#2 set
    vdpWriteReg 15

    ret


; ----------------------------------------------------------------------------
    .area _DATA

uIYH:    
    .db 0                       ; store across C-functions (to make multiple algorithms use the same code)

