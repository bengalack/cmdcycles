; ============================================================================
; cycle-test.z80.s - from cycle-test.z80.txt
; Note: Any symbol to be reached via C in SDCC is prefixed with an underscore.
; Any parameters are passed according to __sdcccall(1) found here:
; https://sdcc.sourceforge.net/doc/sdccman.pdf
; macros moved to macros_constants.inc
; author: Wouter Vermaelen, prepared for SDCC / asxxxx by pal.hansen@gmail.com

; CYCLE DELAY MACROS FOR MSX Z80
;  These may destroy the HL and AF registers
; delay 1-4, 6, 9 not possible
.include "macros_constants.inc"

; -------------------------------------------------------------
; Stub for SDCC
; IN:       A  - VDP command to be sent to port 0x9B
;           DE - Target delay N (cycles between OUT and IN)
; OUT:      A  - CMD status after the wait
;                (https://www.msx.org/wiki/VDP_Status_Registers#Status_Register_2)
;                bit 0 is set if busy
; MODIFIES: AF, BC, DE, HL
; 
; u16 dispatch_vdp_test(u8 uVDP_CMD, u16 nTargetDelay);  // A, DE
; 
_dispatch_vdp_test::

    ld c,a
    ex de, hl
    ; FALL THROUGH

; EXACT VDP READ DELAY DISPATCHER
; Input:  HL = Target delay N (cycles between OUT and IN)
;         C  = VDP write value (Command byte written to port 0x9B)
; Output: A  = VDP read value  (Status byte read from 0x99)

dispatch_vdp_test::
	ld a,h
	or a
	jr nz,dynamic_loop
	ld a,l
	cp #48
	jr nc,dynamic_loop

direct_stub:   ; 0 <= N <= 47
	ld a,l
	add a,a
	ld e,a
	ld d,#0
	ld hl,#stub_jump_table
	add hl,de
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	
	ld a,c
	jp (hl)

dynamic_loop:   ; 48 <= N
	ld a,l
	and #15		; A = HL & 15
	
	srl h
	rr l
	srl h
	rr l
	srl h
	rr l
	srl h
	rr l
	ld b,l		; B = HL / 16
	dec b
	dec b		; B = (N - 32) / 16   = (N / 16) - 2

	add a,a
	ld e,a
	ld d,#0
	ld hl,#loop_pad_table
	add hl,de
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	
	ld a,c
	jp (hl)


stub_jump_table:
.dw stub_invalid, stub_invalid, stub_invalid, stub_invalid
.dw stub_invalid, stub_invalid, stub_invalid, stub_invalid
.dw stub_invalid, stub_invalid, stub_invalid, stub_invalid
.dw stub_12,      stub_invalid, stub_14,      stub_invalid
.dw stub_invalid, stub_17,      stub_invalid, stub_19
.dw stub_20,      stub_21,      stub_22,      stub_23
.dw stub_24,      stub_25,      stub_26,      stub_27
.dw stub_28,      stub_29,      stub_30,      stub_31
.dw stub_32,      stub_33,      stub_34,      stub_35
.dw stub_36,      stub_37,      stub_38,      stub_39
.dw stub_40,      stub_41,      stub_42,      stub_43
.dw stub_44,      stub_45,      stub_46,      stub_47

loop_pad_table:
.dw loop_pad_00, loop_pad_01, loop_pad_02, loop_pad_03
.dw loop_pad_04, loop_pad_05, loop_pad_06, loop_pad_07
.dw loop_pad_08, loop_pad_09, loop_pad_10, loop_pad_11
.dw loop_pad_12, loop_pad_13, loop_pad_14, loop_pad_15


; DIRECT STUBS (N < 48)

stub_invalid::
	ld a,#0; added by bengalack (0 is an invalid S#2 value)
	ret

stub_12::
	out (0x9B),a
	in a,(0x99)	; 12	- THIS ONE IS TOO FAST for the VDP (15 minimum)
	ret

stub_14::
	ld c,#0x99
	out (0x9B),a
	in a,(c)	; 14	- THIS ONE IS TOO FAST for the VDP (15 minimum)
	ret

stub_17::
	out (0x9B),a
	delay5
	in a,(0x99)	; 12 + 5 = 17
	ret

stub_19::
	out (0x9B),a
	delay7
	in a,(0x99)	; 12 + 7 = 19
	ret

stub_20::
	out (0x9B),a
	delay8
	in a,(0x99)	; 12 + 8 = 20
	ret

stub_21::
	ld c,#0x99
	out (0x9B),a
	delay7
	in a,(c)	; 14 + 7 = 21
	ret

stub_22::
	out (0x9B),a
	delay10
	in a,(0x99)	; 12 + 10 = 22
	ret

stub_23::
	out (0x9B),a
	delay11
	in a,(0x99)	; 12 + 11 = 23
	ret

stub_24::
	out (0x9B),a
	delay12
	in a,(0x99)	; 12 + 12 = 24
	ret

stub_25::
	out (0x9B),a
	delay13
	in a,(0x99)	; 12 + 13 = 25
	ret

stub_26::
	out (0x9B),a
	delay14
	in a,(0x99)	; 12 + 14 = 26
	ret

stub_27::
	out (0x9B),a
	delay15
	in a,(0x99)	; 12 + 15 = 27
	ret

stub_28::
	out (0x9B),a
	delay16
	in a,(0x99)	; 12 + 16 = 28
	ret

stub_29::
	out (0x9B),a
	delay17
	in a,(0x99)	; 12 + 17 = 29
	ret

stub_30::
	out (0x9B),a
	delay18
	in a,(0x99)	; 12 + 18 = 30
	ret

stub_31::
	out (0x9B),a
	delay19
	in a,(0x99)	; 12 + 19 = 31
	ret

stub_32::
	out (0x9B),a
	delay20
	in a,(0x99)	; 12 + 20 = 32
	ret

stub_33::
	out (0x9B),a
	delay21
	in a,(0x99)	; 12 + 21 = 33
	ret

stub_34::
	out (0x9B),a
	delay22
	in a,(0x99)	; 12 + 22 = 34
	ret

stub_35::
	out (0x9B),a
	delay23
	in a,(0x99)	; 12 + 23 = 35
	ret

stub_36::
	out (0x9B),a
	delay24
	in a,(0x99)	; 12 + 24 = 36
	ret

stub_37::
	out (0x9B),a
	delay25
	in a,(0x99)	; 12 + 25 = 37
	ret

stub_38::
	out (0x9B),a
	delay26
	in a,(0x99)	; 12 + 26 = 38
	ret

stub_39::
	out (0x9B),a
	delay27
	in a,(0x99)	; 12 + 27 = 39
	ret

stub_40::
	out (0x9B),a
	delay28
	in a,(0x99)	; 12 + 28 = 40
	ret

stub_41::
	out (0x9B),a
	delay29
	in a,(0x99)	; 12 + 29 = 41
	ret

stub_42::
	out (0x9B),a
	delay30
	in a,(0x99)	; 12 + 30 = 42
	ret

stub_43::
	out (0x9B),a
	delay31
	in a,(0x99)	; 12 + 31 = 43
	ret

stub_44::
	out (0x9B),a
	delay32
	in a,(0x99)	; 12 + 32 = 44
	ret

stub_45::
	out (0x9B),a
	delay33
	in a,(0x99)	; 12 + 33 = 45
	ret

stub_46::
	out (0x9B),a
	delay34
	in a,(0x99)	; 12 + 34 = 46
	ret

stub_47::
	out (0x9B),a
	delay35
	in a,(0x99)	; 12 + 35 = 47
	ret



; DYNAMIC LOOP ROUTINES (N >= 48)
; Formula: Total = 16 * B + 32 + R cycles  (where 1 <= B <= 256)
; Dispatcher setup: B = (N - 32) >> 4, R = N & 15

loop_pad_00:
	out (0x9B),a
loop_00:
	dec b
	jp nz,loop_00
	delay20
	in a,(0x99)
	ret

loop_pad_01:
	out (0x9B),a
loop_01:
	dec b
	jp nz,loop_01
	delay21
	in a,(0x99)
	ret

loop_pad_02:
	out (0x9B),a
loop_02:
	dec b
	jp nz,loop_02
	delay22
	in a,(0x99)
	ret

loop_pad_03:
	out (0x9B),a
loop_03:
	dec b
	jp nz,loop_03
	delay23
	in a,(0x99)
	ret

loop_pad_04:
	out (0x9B),a
loop_04:
	dec b
	jp nz,loop_04
	delay24
	in a,(0x99)
	ret

loop_pad_05:
	out (0x9B),a
loop_05:
	dec b
	jp nz,loop_05
	delay25
	in a,(0x99)
	ret

loop_pad_06:
	out (0x9B),a
loop_06:
	dec b
	jp nz,loop_06
	delay26
	in a,(0x99)
	ret

loop_pad_07:
	out (0x9B),a
loop_07:
	dec b
	jp nz,loop_07
	delay27
	in a,(0x99)
	ret

loop_pad_08:
	out (0x9B),a
loop_08:
	dec b
	jp nz,loop_08
	delay28
	in a,(0x99)
	ret

loop_pad_09:
	out (0x9B),a
loop_09:
	dec b
	jp nz,loop_09
	delay29
	in a,(0x99)
	ret

loop_pad_10:
	out (0x9B),a
loop_10:
	dec b
	jp nz,loop_10
	delay30
	in a,(0x99)
	ret

loop_pad_11:
	out (0x9B),a
loop_11:
	dec b
	jp nz,loop_11
	delay31
	in a,(0x99)
	ret

loop_pad_12:
	out (0x9B),a
loop_12:
	dec b
	jp nz,loop_12
	delay32
	in a,(0x99)
	ret

loop_pad_13:
	out (0x9B),a
loop_13:
	dec b
	jp nz,loop_13
	delay33
	in a,(0x99)
	ret

loop_pad_14:
	out (0x9B),a
loop_14:
	dec b
	jp nz,loop_14
	delay34
	in a,(0x99)
	ret

loop_pad_15:
	out (0x9B),a
loop_15:
	dec b
	jp nz,loop_15
	delay35
	in a,(0x99)
	ret