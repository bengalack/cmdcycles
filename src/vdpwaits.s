;
; Test how many wait states are inserted for VDP I/O.
;
; Original file by Grauw here: https://gist.github.com/grauw/184cf27ed002f4d9d3ea1bb43c9bf1f6
; Discussed here: https://www.msx.org/forum/msx-talk/development/extra-vdp-wait-cycles
; This file is modified to compile with SDCC and to work in the background with no screen output.
;
; Expected results:
; 0 waits: 0x7C4 (60Hz) / 0x948 (50Hz) [Just test for this one, if >= to these, we or ok, otherwise we have a +1]
;
; Used with permission
; pal.hansen@gmail.com

; Entry for SDCC v4.2+
; extern u16 measureVDPCommandsInOneFrame(void);
_measureVDPCommandsInOneFrame::

	di
	ld 		a,(0x0038)
	ld 		hl,(0x0039)
	push 	af
	push 	hl				; stores the original interrupt vector
	ld 		a,#0xC3
	ld 		hl,#Interrupt
	ld 		bc,#0
	ld 		(0x0038),a
	ld 		(0x0039),hl
	ei
	call 	Wait
	call 	Write
	di
	pop 	hl				; restore the original interrupt vector
	pop 	af
	ld 		(0x0038),a
	ld 		(0x0039),hl
	ei

	ld 		e,c
	ld 		d,b				; return in DE

	ret

Wait:
	jp 		Wait

Write:
	in 		a,(0x98)
	inc 	bc
	jp		Write

Interrupt:
	in 		a,(0x99)
	pop 	af
	ei
	ret