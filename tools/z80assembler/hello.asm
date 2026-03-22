db 1 ; code segment
db (code_end-code_start)+2
dw code_start

;ORG   $e001   
ORG   $4000   

code_start: 
start:
LD   hl,scr   
LD   (hl),$48   
INC   hl   
LD   (hl),$45   
INC   hl   
LD   (hl),$4C   
INC   hl   
LD   (hl),$4C   
INC   hl   
LD   (hl),$4f   
INC   hl   
LD   (hl),$20
INC   hl   
LD   (hl),$4C   
INC   hl   
LD   (hl),$4f   
INC   hl   
LD   (hl),$4C   
INC   hl   
LD   (hl),$4f   

JP   start

code_end:


db 2 ; entry point
db 2
dw code_start





ORG   $3C00   
scr: 





;	org 0x4000
;	db "AB"
;	dw start
;text:	db "This is the source which generated this program:\r\n"
;	incbin 'hello.asm'
;	db 0
	
     


;ld a, 80
;	ld (0xf3ae), a	; width 80
;	xor a
;	call CHGMOD	; screen 0
;	ld hl, text
;loop:	ld a, (hl)
;	and a		; set the z flag if A is 0
;	jr z,stop	; and return in that case
;	call CHPUT
;	inc hl
;	jr loop
;stop:	jr stop
;	ds 0x8000 - $	; fill up the rest of the page
