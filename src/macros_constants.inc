; ----------------------------------------------------------------------------
; COMMON CONSTANTS

    VDPIO		.equ 0x98				; VRAM Data (Read/Write)
    VDPPORT1	.equ 0x99
    VDPSTREAM   .equ 0x9B

;-------------------------
; IN:  			A - the value
; 				reg - reg-number
; Modifies: A
; Set a value in reg number n
; Cost: 32 cycles (6 bytes)
;-------------------------
.macro vdpWriteReg reg
	out 	(VDPPORT1),a
	ld  	a,#reg | 0x80   ; sets write bit
	out 	(VDPPORT1),a
.endm

;-------------------------
; Modifies: AF, HL
; Unsigned
; This macro must never include parameters
; This is the fastest possible way to add an 8 bit to 16 bit: 23 cycles
;-------------------------
.macro addAtoHL ?noparametero
	ADD 	A, L
	LD 		L, A
	JR 		NC, noparametero
	INC 	H
noparametero:
.endm

;-------------------------
; Constants below come from cycle-test.z80.s
;
.macro delay5
	nop			; 5
.endm

.macro delay7
	inc hl		; 7
.endm

.macro delay8
	cp (hl)		; 8
.endm

.macro delay10
	delay5
	delay5
.endm

.macro delay11
	ld a,i		; 11
.endm

.macro delay12
	add hl,bc	; 12
.endm

.macro delay13
	jr . +2		; 13
.endm

.macro delay14
   delay7
   delay7
.endm

.macro delay15
	delay8
	delay7
.endm

.macro delay16
	delay8
	delay8
.endm

.macro delay17
	delay12
	delay5
.endm

.macro delay18
	delay11
	delay7
.endm

.macro delay19
	delay11
	delay8
.endm

.macro delay20
	delay12
	delay8
.endm

.macro delay21
	delay11
	delay10
.endm

.macro delay22
	delay12
	delay10
.endm

.macro delay23
	delay13
	delay10
.endm

.macro delay24
	delay16
	delay8
.endm

.macro delay25
	delay14
	delay11
.endm

.macro delay26
	delay14
	delay12
.endm

.macro delay27
	delay14
	delay13
.endm

.macro delay28
	delay14
	delay14
.endm

.macro delay29
	delay14
	delay15
.endm

.macro delay30
	delay14
	delay16
.endm

.macro delay31
	delay14
	delay17
.endm

.macro delay32
	delay16
	delay16
.endm

.macro delay33
	delay28
	delay5
.endm

.macro delay34
	delay26
	delay8
.endm

.macro delay35
	delay28
	delay7
.endm