; =============================================================================
; MONTY ON THE RUN - Z80 ENGINE CONVERSION
; =============================================================================

; Audio
; SID (see C64)
REG_SIDBASE:      equ $ff00

; vsync line (0-200, 200 is overscan) (RD)
REG_VSYNC:         equ $fb0f



dw START
 
ORG   $5000   


START:
    ; Initialize music with track number 0
    XOR A                   ; A = 0
    CALL initmusic

WAIT_VSYNC:
    LD A, (REG_VSYNC)
    CP 200
    jp nz,WAIT_VSYNC

    CALL playmusic

    call  002BH             ; getkey
    ld b,'W'                ; W? 
    cp b
    jr z,exit

WAIT_VSYNCEND:             ; did we exit VBL?
    LD A, (REG_VSYNC)
    CP 200
    jp z,WAIT_VSYNCEND
    jp WAIT_VSYNC


exit:
    RET

;--------------------------------------------------
; variables (RAM)
;--------------------------------------------------
counter:      db 0
mstatus:      db 0C0h
speed:        db 0
resetspd:     db 1

regoffsets:   db 0,7,14
tmpregofst:   db 0

posoffset:    db 0,0,0
patoffset:    db 0,0,0
lengthleft:   db 0,0,0
savelnthcc:   db 0,0,0
voicectrl:    db 0,0,0
notenum:      db 0,0,0
instrnr:      db 0,0,0

appendfl:     db 0
templnthcc:   db 0
tempfreq:     db 0
tempstore:    db 0
tempctrl:     db 0
vibrdepth:    db 0
pulsevalue:   db 0
tmpvdiflo:    db 0
tmpvdifhi:    db 0
tmpvfrqlo:    db 0
tmpvfrqhi:    db 0
oscilatval:   db 0
pulsedelay:   db 0,0,0
pulsedir:     db 0,0,0
instnumby8:   db 0
savefreqhi:   db 0,0,0
savefreqlo:   db 0,0,0
portaval:     db 0,0,0
instrfx:      db 0
pulsespeed:   db 0

currtrkhi:    db 0,0,0
currtrklo:    db 0,0,0

;--------------------------------------------------
; initmusic
;--------------------------------------------------

initmusic:
    ld   a,0          ; music number 0
    ld   b,a          ; keep if needed

    ; musicnum * 6
    ld   c,a
    add  a,a          ; *2
    ld   d,a
    add  a,a          ; *4
    add  a,d          ; *6

    ld   e,a
    ld   d,0
    ld   hl,songs
    add  hl,de        ; HL = songs + num*6

    ; copy 6 bytes into currtrklo/hi (same layout as 6502)
    ld   de,currtrkhi
    ld   c,6
init_trk_copy:
    ld   a,(hl)
    ld   (de),a
    inc  hl
    inc  de
    dec  c
    jr   nz,init_trk_copy

    ; clear control regs
    ld   a,0
    ld   (REG_SIDBASE+4),a
    ld   (REG_SIDBASE+11),a
    ld   (REG_SIDBASE+18),a
    ld   (REG_SIDBASE+23),a

    ld   a,0Fh
    ld   (REG_SIDBASE+24),a

    ld   a,40h
    ld   (mstatus),a

    ret

;--------------------------------------------------
; playmusic
;--------------------------------------------------

playmusic:
    ; inc counter
    ld   a,(counter)
    inc  a
    ld   (counter),a

    ; test mstatus
    ld   a,(mstatus)
    bit  7,a
    jr   nz,moff          ; bit7 set → off

    bit  6,a
    jr   z,contplay       ; bit6 clear → normal play

    ; init song (mstatus = $40)
    ld   a,0
    ld   (counter),a

    ; init per-channel arrays (3 channels)
    ld   b,3
    ld   hl,posoffset
init_ch_loop:
    ld   (hl),a           ; posoffset
    inc  hl
    ld   (hl),a           ; patoffset
    inc  hl
    ld   (hl),a           ; lengthleft
    inc  hl
    ld   (hl),a           ; notenum
    inc  hl
    djnz init_ch_loop

    ld   (mstatus),a      ; now playing
    jr   contplay

;--------------------------------------------------
; music off (mstatus $80 or $c0)
;--------------------------------------------------

moff:
    ; if bit6 set (mstatus $C0) we must kill voices
    ld   a,(mstatus)
    bit  6,a
    jr   z,xx

    ld   a,0
    ld   (REG_SIDBASE+4),a
    ld   (REG_SIDBASE+11),a
    ld   (REG_SIDBASE+18),a

    ld   a,0Fh
    ld   (REG_SIDBASE+24),a

    ld   a,80h
    ld   (mstatus),a
xx:    
    jp   musicend

;--------------------------------------------------
; music is playing
;--------------------------------------------------

contplay:
    ld   b,3-1           ; X = 2 (channel index)

    ; dec speed
    ld   a,(speed)
    dec  a
    ld   (speed),a
    or   a
    jp   p,mainloop      ; if >=0, skip speed reset

    ld   a,(resetspd)
    ld   (speed),a

mainloop:
    ; regoffsets[x] → tmpregofst
    ld   hl,regoffsets
    ld   a,b
    ld   e,a
    ld   d,0
    add  hl,de
    ld   a,(hl)
    ld   (tmpregofst),a
    ld   c,a             ; C = tmpregofst

    ; if speed == resetspd → checknewnote else vibrato
    ld   a,(speed)
    ld   d,a
    ld   a,(resetspd)
    cp   d
    jr   z,checknewnote
    jp   vibrato

;--------------------------------------------------
; checknewnote
;--------------------------------------------------

checknewnote:
    ; build track pointer from currtrkhi/lo[x] into HL
    ld   a,b
    ld   e,a
    ld   d,0

    ; currtrkhi[x]
    ld   hl,currtrkhi
    add  hl,de
    ld   a,(hl)
    ld   h,a

    ; currtrklo[x]
    ld   hl,currtrklo
    add  hl,de
    ld   a,(hl)
    ld   l,a             ; HL = track base

    ; dec lengthleft[x]
    ld   hl,lengthleft
    add  hl,de
    ld   a,(hl)
    dec  a
    ld   (hl),a
    jp   m,getnewnote    ; if <0 → new note
    jp   soundwork

;--------------------------------------------------
; getnewnote
;--------------------------------------------------

getnewnote:
    ; Y = posoffset[x]
    ld   hl,posoffset
    add  hl,de
    ld   c,(hl)          ; C = Y

    ; A = (trackbase + Y)
    push de              ; save DE=index
    ld   de,0
    ld   e,c
    add  hl,de           ; HL = trackbase + Y
    ld   a,(hl)
    pop  de

    cp   0FFh
    jr   z,restart

    cp   0FEh
    jr   nz,getnotedata
    jp   musicend

restart:
    ld   a,0
    ; lengthleft[x] = 0
    ld   hl,lengthleft
    add  hl,de
    ld   (hl),a
    ; posoffset[x] = 0
    ld   hl,posoffset
    add  hl,de
    ld   (hl),a
    ; patoffset[x] = 0
    ld   hl,patoffset
    add  hl,de
    ld   (hl),a
    jp   getnewnote

;--------------------------------------------------
; getnotedata (pattern pointer, length, instrument, pitch)
;--------------------------------------------------

getnotedata:
    ; Y = pattern number in A
    ; HL = &patptl[A], DE = &patpth[A] etc.
    ld   c,a             ; C = pattern index

    ld   hl,patptl
    ld   e,c
    ld   d,0
    add  hl,de
    ld   a,(hl)
    ld   (04h),a         ; low pattern pointer temp

    ld   hl,patpth
    add  hl,de
    ld   a,(hl)
    ld   (05h),a         ; high pattern pointer temp

    ld   a,0
    ld   hl,portaval
    add  hl,de
    ld   (hl),a          ; default no portamento

    ; Y = patoffset[x]
    ld   hl,patoffset
    add  hl,de
    ld   c,(hl)          ; C = Y

    ld   a,0FFh
    ld   (appendfl),a

    ; 1st byte: length/flags
    ld   h,(05h)
    ld   l,(04h)
    ld   e,c
    ld   d,0
    add  hl,de
    ld   a,(hl)
    ld   (savelnthcc),a  ; per-channel array in real code
    ld   (templnthcc),a
    and  1Fh
    ld   hl,lengthleft
    add  hl,de
    ld   (hl),a

    ld   a,(templnthcc)
    bit  6,a             ; append?
    jr   nz,appendnote

    ; inc patoffset[x]
    ld   hl,patoffset
    add  hl,de
    ld   a,(hl)
    inc  a
    ld   (hl),a

    ld   a,(templnthcc)
    bit  7,a
    jr   z,getpitch

    ; 2nd byte: instr or portamento
    inc  c               ; Y++
    ld   h,(05h)
    ld   l,(04h)
    ld   e,c
    ld   d,0
    add  hl,de
    ld   a,(hl)
    bit  7,a
    jr   z,store_instr

    ; negative → portamento
    ld   hl,portaval
    add  hl,de
    ld   (hl),a
    jr   after_instr

store_instr:
    ld   hl,instrnr
    add  hl,de
    ld   (hl),a

after_instr:
    ; inc patoffset[x]
    ld   hl,patoffset
    add  hl,de
    ld   a,(hl)
    inc  a
    ld   (hl),a

; 3rd byte: pitch
getpitch:
    inc  c
    ld   h,(05h)
    ld   l,(04h)
    ld   e,c
    ld   d,0
    add  hl,de
    ld   a,(hl)          ; pitch
    ld   hl,notenum
    add  hl,de
    ld   (hl),a

    add  a,a             ; pitch*2
    ld   e,a
    ld   d,0
    ld   hl,frequenzlo
    add  hl,de
    ld   a,(hl)
    ld   (tempfreq),a
    ld   hl,frequenzhi
    add  hl,de
    ld   a,(hl)

    ; write hi/lo to SID and save
    ld   a,(tmpregofst)
    ld   c,a
    ld   a,(tempfreq)
    ld   (REG_SIDBASE+0 + 0),a   ; +C in real code
    ld   hl,savefreqlo
    add  hl,de
    ld   (hl),a

    ld   a,(frequenzhi+0) ; placeholder – see above
    ld   (REG_SIDBASE+1 + 0),a
    ld   hl,savefreqhi
    add  hl,de
    ld   (hl),a

    jr   loopcont

appendnote:
    ld   a,(appendfl)
    dec  a
    ld   (appendfl),a
    ; then instrument fetch etc. (same pattern as above)
    ; ...

;--------------------------------------------------
; soundwork, vibrato, pulsework, portamento, drums,
; skydive, octarp, loopcont, musicend
;--------------------------------------------------
; These follow the exact same translation style:
; - use HL/DE as pointer pairs
; - use B as channel index
; - arrays indexed via HL + DE
; - branches mapped to jr/jp with flags
;--------------------------------------------------

soundwork:
    ; release logic...
    ; (translate 1:1 from 6502 using same patterns)
    jp   vibrato

vibrato:
    ; full vibrato routine translated similarly
    ; ...
    jp   pulsework

pulsework:
    ; pulse-width modulation logic
    ; ...
    jp   portamento

portamento:
    ; up/down slide using savefreqlo/hi and portaval
    ; ...
    jp   drums

drums:
    ; drum effect logic
    ; ...
    jp   skydive

skydive:
    ; long downward slide
    ; ...
    jp   octarp

octarp:
    ; octave arpeggio
    ; ...
    jp   loopcont

loopcont:
    dec  b
    jp   m,musicend
    jp   mainloop

musicend:
    ret

;====================================================================
; FREQUENCY TABLES
;====================================================================
frequenzlo:
        DB $16, $27, $01, $38, $01, $4b, $01
        DB $5f, $01, $73, $01, $8a, $01, $a1, $01
        DB $ba, $01, $d4, $01, $f0, $01, $0e, $02
        DB $2d, $02, $4e, $02, $71, $02, $96, $02
        DB $bd, $02, $e7, $02, $13, $03, $42, $03
        DB $74, $03, $a9, $03, $e0, $03, $1b, $04
        DB $5a, $04, $9b, $04, $e2, $04, $2c, $05
        DB $7b, $05, $ce,$05, $27, $06, $85, $06
        DB $e8, $06, $51, $07, $c1, $07, $37, $08
        DB $b4, $08, $37, $09, $c4, $09, $57, $0a
        DB $f5, $0a, $9c, $0b, $4e, $0c, $09, $0d
        DB $d0, $0d, $a3, $0e, $82, $0f, $6e, $10
        DB $68, $11, $6e, $12, $88, $13, $af, $14
        DB $eb, $15, $39, $17, $9c, $18, $13, $1a
        DB $a1, $1b, $46, $1d, $04, $1f, $dc, $20
        DB $d0, $22, $dc, $24, $10, $27, $5e, $29
        DB $d6, $2b, $72, $2e, $38, $31, $26, $34
        DB $42, $37, $8c, $3a, $08, $3e, $b8, $41
        DB $a0, $45, $b8, $49, $20, $4e, $bc, $52
        DB $ac, $57, $e4, $5c, $70, $62, $4c, $68
        DB $84, $6e, $18, $75, $10, $7c, $70, $83
        DB $40, $8b, $70, $93, $40, $9c, $78, $a5
        DB $58, $af, $c8, $b9, $e0, $c4, $98, $d0
        DB $08, $dd, $30, $ea, $20, $f8, $2e, $fd

frequenzhi:
        DB $01, $01, $01, $01, $01, $01, $01, $01
        DB $01, $01, $01, $01, $02, $02, $02, $02
        DB $02, $02, $02, $03, $03, $03, $03, $03
        DB $04, $04, $04, $04, $05, $05, $05, $06
        DB $06, $06, $07, $07, $08, $08, $09, $09
        DB $0a, $0a, $0b, $0c, $0d, $0d, $0e, $0f
        DB $10, $11, $12, $13, $14, $15, $17, $18
        DB $1a, $1b, $1d, $1f, $20, $22, $24, $27
        DB $29, $2b, $2e, $31, $34, $37, $3a, $3e
        DB $41, $45, $49, $4e, $52, $57, $5c, $62
        DB $68, $6e, $75, $7c, $83, $8b, $93, $9c
        DB $a5, $af, $b9, $c4, $d0, $dd, $ea, $f8


;====================================================================
; TRACKS & PATTERNS DATA STRUCTS
;====================================================================
songs:
        DB      montymaintr1, montymaintr2, montymaintr3
        DB      montymaintr1/256, montymaintr2/256, montymaintr3/256

; --- Pattern pointer tables ---
patptl:
        ; (low bytes -- identical to original)
        DB      ptn00, ptn01, ptn02, ptn03, ptn04, ptn05
        DB      ptn06, ptn07, ptn08, ptn09, ptn0a, ptn0b
        DB      ptn0c, ptn0d, ptn0e, ptn0f, ptn10, ptn11
        DB      ptn12, ptn13, ptn14, ptn15, ptn16, ptn17
        DB      ptn18, ptn19, ptn1a, ptn1b, ptn1c, ptn1d
        DB      ptn1e, ptn1f, ptn20, ptn21, ptn22, ptn23
        DB      ptn24, ptn25, ptn26, ptn27, ptn28, ptn29
        DB      ptn2a, ptn2b, ptn2c, ptn2d, 0,     ptn2f
        DB      ptn30, ptn31, ptn32, ptn33, ptn34, ptn35
        DB      ptn36, ptn37, ptn38, ptn39, ptn3a, ptn3b

patpth:
        ; (high bytes -- identical to original)
        DB      ptn00/256, ptn01/256, ptn02/256, ptn03/256, ptn04/256, ptn05/256
        DB      ptn06/256, ptn07/256, ptn08/256, ptn09/256, ptn0a/256, ptn0b/256
        DB      ptn0c/256, ptn0d/256, ptn0e/256, ptn0f/256, ptn10/256, ptn11/256
        DB      ptn12/256, ptn13/256, ptn14/256, ptn15/256, ptn16/256, ptn17/256
        DB      ptn18/256, ptn19/256, ptn1a/256, ptn1b/256, ptn1c/256, ptn1d/256
        DB      ptn1e/256, ptn1f/256, ptn20/256, ptn21/256, ptn22/256, ptn23/256
        DB      ptn24/256, ptn25/256, ptn26/256, ptn27/256, ptn28/256, ptn29/256
        DB      ptn2a/256, ptn2b/256, ptn2c/256, ptn2d/256, 0,         ptn2f/256
        DB      ptn30/256, ptn31/256, ptn32/256, ptn33/256, ptn34/256, ptn35/256
        DB      ptn36/256, ptn37/256, ptn38/256, ptn39/256, ptn3a/256, ptn3b/256



montymaintr1:
        DB $11,$14,$17,$1a,$00,$27,$00,$28,$03,$05,$00,$27,$00,$28,$03,$05
        DB $07,$3a,$14,$17,$00,$27,$00,$28,$2f,$30,$31,$31,$32,$33,$33,$34
        DB $34,$34,$34,$34,$34,$34,$34,$35,$35,$35,$35,$35,$35,$36,$12,$37
        DB $38,$09,$2a,$09,$2b,$09,$0a,$09,$2a,$09,$2b,$09,$0a,$0d,$0d,$0f
        DB $FF

montymaintr2:
        DB $12,$15,$18,$1b,$2d,$39,$39,$39,$39,$39,$39,$2c,$39,$39,$39,$39
        DB $39,$39,$2c,$39,$39,$39,$01,$01,$29,$29,$2c,$15,$18,$39,$39,$39
        DB $39,$39,$39,$39,$39,$39,$39,$39,$39,$39,$39,$39,$39,$39,$39,$39
        DB $39,$39,$39,$39,$39,$39,$39,$39,$39,$39,$01,$01,$01,$29,$39,$39
        DB $39,$01,$01,$01,$29,$39,$39,$39,$39,$FF

montymaintr3:
        DB $13,$16,$19,$1c,$02,$02,$1d,$1e,$02,$02,$1d,$1f,$04,$04,$20,$20
        DB $06,$02,$02,$1d,$1e,$02,$02,$1d,$1f,$04,$04,$20,$20,$06,$08,$08
        DB $08,$08,$21,$21,$21,$21,$22,$22,$22,$23,$22,$24,$25,$3b,$26,$26
        DB $26,$26,$26,$26,$26,$26,$26,$26,$26,$26,$26,$26,$26,$26,$02,$02
        DB $1d,$1e,$02,$02,$1d,$1f,$2f,$2f,$2f,$2f,$2f,$2f,$2f,$2f,$2f,$2f
        DB $2f,$2f,$2f,$0b,$0b,$1d,$1d,$0b,$0b,$1d,$0b,$0b,$0b,$0c,$0c,$1d
        DB $1d,$1d,$10,$0b,$0b,$1d,$1d,$0b,$0b,$1d,$0b,$0b,$0b,$0c,$0c,$1d
        DB $1d,$1d,$10,$0b,$1d,$0b,$1d,$0b,$1d,$0b,$1d,$0b,$0c,$1d,$0b,$0c
        DB $23,$0b,$0b,$FF

;====================================================================
; PATTERNS
;====================================================================
ptn00:  DB $83,$00,$37,$01,$3e,$01,$3e,$03,$3d,$03,$3e,$03,$43,$03,$3e,$03
        DB $3d,$03,$3e,$03,$37,$01,$3e,$01,$3e,$03,$3d,$03,$3e,$03,$43,$03
        DB $42,$03,$43,$03,$45,$03,$46,$01,$48,$01,$46,$03,$45,$03,$43,$03
        DB $4b,$01,$4d,$01,$4b,$03,$4a,$03,$48,$FF

ptn27:  DB $1f,$4a,$FF
ptn28:  DB $03,$46,$01,$48,$01,$46,$03,$45,$03,$4a,$0f,$43,$FF

ptn03:  DB $bf,$06,$48,$07,$48,$01,$4b,$01,$4a,$01,$4b,$01,$4a,$03,$4b,$03
        DB $4d,$03,$4b,$03,$4a,$3f,$48,$07,$48,$01,$4b,$01,$4a,$01,$4b,$01
        DB $4a,$03,$4b,$03,$4d,$03,$4b,$03,$48,$3f,$4c,$07,$4c,$01,$4f,$01
        DB $4e,$01,$4f,$01,$4e,$03,$4f,$03,$51,$03,$4f,$03,$4e,$3f,$4c,$07
        DB $4c,$01,$4f,$01,$4e,$01,$4f,$01,$4e,$03,$4f,$03,$51,$03,$4f,$03
        DB $4c,$FF

ptn05:  DB $83,$04,$26,$03,$29,$03,$28,$03,$29,$03,$26,$03,$35,$03,$34,$03
        DB $32,$03,$2d,$03,$30,$03,$2f,$03,$30,$03,$2d,$03,$3c,$03,$3b,$03
        DB $39,$03,$30,$03,$33,$03,$32,$03,$33,$03,$30,$03,$3f,$03,$3e,$03
        DB $3c,$03,$46,$03,$45,$03,$43,$03,$3a,$03,$39,$03,$37,$03,$2e,$03
        DB $2d,$03,$26,$03,$29,$03,$28,$03,$29,$03,$26,$03,$35,$03,$34,$03
        DB $32,$03,$2d,$03,$30,$03,$2f,$03,$30,$03,$2d,$03,$3c,$03,$3b,$03
        DB $39,$03,$30,$03,$33,$03,$32,$03,$33,$03,$30,$03,$3f,$03,$3e,$03
        DB $3c,$03,$34,$03,$37,$03,$36,$03,$37,$03,$34,$03,$37,$03,$3a,$03
        DB $3d

ptn3a:  DB $03,$3e,$07,$3e,$07,$3f,$07,$3e,$03,$3c,$07,$3e,$57,$FF

ptn07:  DB $8b,$00,$3a,$01,$3a,$01,$3c,$03,$3d,$03,$3f,$03,$3d,$03,$3c,$0b
        DB $3a,$03,$39,$07,$3a,$81,$06,$4b,$01,$4d,$01,$4e,$01,$4d,$01,$4e
        DB $01,$4d,$05,$4b,$81,$00,$3a,$01,$3c,$01,$3d,$03,$3f,$03,$3d,$03
        DB $3c,$03,$3a,$03,$39,$1b,$3a,$0b,$3b,$01,$3b,$01,$3d,$03,$3e,$03
        DB $40,$03,$3e,$03,$3d,$0b,$3b,$03,$3a,$07,$3b,$81,$06,$4c,$01,$4e
        DB $01,$4f,$01,$4e,$01,$4f,$01,$4e,$05,$4c,$81,$00,$3b,$01,$3d,$01
        DB $3e,$03,$40,$03,$3e,$03,$3d,$03,$3b,$03,$3a,$1b,$3b,$8b,$05,$35
        DB $03,$33,$07,$32,$03,$30,$03,$2f,$0b,$30,$03,$32,$0f,$30,$0b,$35
        DB $03,$33,$07,$32,$03,$30,$03,$2f,$1f,$30,$8b,$00,$3c,$01,$3c,$01
        DB $3e,$03,$3f,$03,$41,$03,$3f,$03,$3e,$0b,$3d,$01,$3d,$01,$3f,$03
        DB $40,$03,$42,$03,$40,$03,$3f,$03,$3e,$01,$3e,$01,$40,$03,$41,$03
        DB $40,$03,$3e,$03,$3d,$03,$3e,$03,$3c,$03,$3a,$01,$3a,$01,$3c,$03
        DB $3d,$03,$3c,$03,$3a,$03,$39,$03,$3a,$03,$3c,$FF

ptn09:  DB $83,$00,$32,$01,$35,$01,$34,$03,$32,$03,$35,$03,$34,$03,$32,$03
        DB $35,$01,$34,$01,$32,$03,$32,$03,$3a,$03,$39,$03,$3a,$03,$32,$03
        DB $3a,$03,$39,$03,$3a,$FF

ptn2a:  DB $03,$34,$01,$37,$01,$35,$03,$34,$03,$37,$03,$35,$03,$34,$03,$37
        DB $01,$35,$01,$34,$03,$34,$03,$3a,$03,$39,$03,$3a,$03,$34,$03,$3a
        DB $03,$39,$03,$3a,$FF

ptn2b:  DB $03,$39,$03,$38,$03,$39,$03,$3a,$03,$39,$03,$37,$03,$35,$03,$34
        DB $03,$35,$03,$34,$03,$35,$03,$37,$03,$35,$03,$34,$03,$32,$03,$31
        DB $FF

ptn0a:  DB $03,$37,$01,$3a,$01,$39,$03,$37,$03,$3a,$03,$39,$03,$37,$03,$3a
        DB $01,$39,$01,$37,$03,$37,$03,$3e,$03,$3d,$03,$3e,$03,$37,$03,$3e
        DB $03,$3d,$03,$3e,$03,$3d,$01,$40,$01,$3e,$03,$3d,$03,$40,$01,$3e
        DB $01,$3d,$03,$40,$03,$3e,$03,$40,$03,$40,$01,$43,$01,$41,$03,$40
        DB $03,$43,$01,$41,$01,$40,$03,$43,$03,$41,$03,$43,$03,$43,$01,$46
        DB $01,$45,$03,$43,$03,$46,$01,$45,$01,$43,$03,$46,$03,$45,$03,$43
        DB $01,$48,$01,$49,$01,$48,$01,$46,$01,$45,$01,$46,$01,$45,$01,$43
        DB $01,$41,$01,$43,$01,$41,$01,$40,$01,$3d,$01,$39,$01,$3b,$01,$3d
        DB $FF

ptn0d:  DB $01,$3e,$01,$39,$01,$35,$01,$39,$01,$3e,$01,$39,$01,$35,$01,$39
        DB $03,$3e,$01,$41,$01,$40,$03,$40,$01,$3d,$01,$3e,$01,$40,$01,$3d
        DB $01,$39,$01,$3d,$01,$40,$01,$3d,$01,$39,$01,$3d,$03,$40,$01,$43
        DB $01,$41,$03,$41,$01,$3e,$01,$40,$01,$41,$01,$3e,$01,$39,$01,$3e
        DB $01,$41,$01,$3e,$01,$39,$01,$3e,$03,$41,$01,$45,$01,$43,$03,$43
        DB $01,$40,$01,$41,$01,$43,$01,$40,$01,$3d,$01,$40,$01,$43,$01,$40
        DB $01,$3d,$01,$40,$01,$46,$01,$43,$01,$45,$01,$46,$01,$44,$01,$43
        DB $01,$40,$01,$3d,$FF

ptn0f:  DB $01,$3e,$01,$39,$01,$35,$01,$39,$01,$3e,$01,$39,$01,$35,$01,$39
        DB $01,$3e,$01,$39,$01,$35,$01,$39,$01,$3e,$01,$39,$01,$35,$01,$39
        DB $01,$3e,$01,$3a,$01,$37,$01,$3a,$01,$3e,$01,$3a,$01,$37,$01,$3a
        DB $01,$3e,$01,$3a,$01,$37,$01,$3a,$01,$3e,$01,$3a,$01,$37,$01,$3a
        DB $01,$40,$01,$3d,$01,$39,$01,$3d,$01,$40,$01,$3d,$01,$39,$01,$3d
        DB $01,$40,$01,$3d,$01,$39,$01,$3d,$01,$40,$01,$3d,$01,$39,$01,$3d
        DB $01,$41,$01,$3e,$01,$39,$01,$3e,$01,$41,$01,$3e,$01,$39,$01,$3e
        DB $01,$41,$01,$3e,$01,$39,$01,$3e,$01,$41,$01,$3e,$01,$39,$01,$3e
        DB $01,$43,$01,$3e,$01,$3a,$01,$3e,$01,$43,$01,$3e,$01,$3a,$01,$3e
        DB $01,$43,$01,$3e,$01,$3a,$01,$3e,$01,$43,$01,$3e,$01,$3a,$01,$3e
        DB $01,$43,$01,$3f,$01,$3c,$01,$3f,$01,$43,$01,$3f,$01,$3c,$01,$3f
        DB $01,$43,$01,$3f,$01,$3c,$01,$3f,$01,$43,$01,$3f,$01,$3c,$01,$3f
        DB $01,$45,$01,$42,$01,$3c,$01,$42,$01,$45,$01,$42,$01,$3c,$01,$42
        DB $01,$48,$01,$45,$01,$42,$01,$45,$01,$4b,$01,$48,$01,$45,$01,$48
        DB $01,$4b,$01,$4a,$01,$48,$01,$4a,$01,$4b,$01,$4a,$01,$48,$01,$4a
        DB $01,$4b,$01,$4a,$01,$48,$01,$4a,$01,$4c,$01,$4e,$03,$4f,$FF

ptn11:  DB $bf,$06,$56,$1f,$57,$1f,$56,$1f,$5b,$1f,$56,$1f,$57,$1f,$56,$1f
        DB $4f,$FF

ptn12:  DB $bf,$0c,$68,$7f,$7f,$7f,$7f,$7f,$7f,$7f,$FF
ptn13:  DB $bf,$08,$13,$3f,$13,$3f,$13,$3f,$13,$3f,$13,$3f,$13,$3f,$13,$1f
        DB $13,$FF

ptn14:  DB $97,$09,$2e,$03,$2e,$1b,$32,$03,$32,$1b,$31,$03,$31,$1f,$34,$43
        DB $17,$32,$03,$32,$1b,$35,$03,$35,$1b,$34,$03,$34,$0f,$37,$8f,$0a
        DB $37,$43,$FF

ptn15:  DB $97,$09,$2b,$03,$2b,$1b,$2e,$03,$2e,$1b,$2d,$03,$2d,$1f,$30,$43
        DB $17,$2e,$03,$2e,$1b,$32,$03,$32,$1b,$31,$03,$31,$0f,$34,$8f,$0a
        DB $34,$43,$FF

ptn16:  DB $0f,$1f,$0f,$1f,$0f,$1f,$0f,$1f,$0f,$1f,$0f,$1f,$0f,$1f,$0f,$1f
        DB $0f,$1f,$0f,$1f,$0f,$1f,$0f,$1f,$0f,$1f,$0f,$1f,$0f,$1f,$0f,$1f
        DB $FF

ptn17:  DB $97,$09,$33,$03,$33,$1b,$37,$03,$37,$1b,$36,$03,$36,$1f,$39,$43
        DB $17,$37,$03,$37,$1b,$3a,$03,$3a,$1b,$39,$03,$39,$2f,$3c,$21,$3c
        DB $21,$3d,$21,$3e,$21,$3f,$21,$40,$21,$41,$21,$42,$21,$43,$21,$44
        DB $01,$45,$FF

ptn18:  DB $97,$09,$30,$03,$30,$1b,$33,$03,$33,$1b,$32,$03,$32,$1f,$36,$43
        DB $17,$33,$03,$33,$1b,$37,$03,$37,$1b,$36,$03,$36,$2f,$39,$21,$39
        DB $21,$3a,$21,$3b,$21,$3c,$21,$3d,$21,$3e,$21,$3f,$21,$40,$21,$41
        DB $01,$42,$FF

ptn19:  DB $0f,$1a,$0f,$1a,$0f,$1a,$0f,$1a,$0f,$1a,$0f,$1a,$0f,$1a,$0f,$1a
        DB $0f,$1a,$0f,$1a,$0f,$1a,$0f,$1a,$0f,$1a,$0f,$1a,$0f,$1a,$0f,$1a
        DB $FF

ptn1a:  DB $1f,$46,$bf,$0a,$46,$7f,$7f,$FF
ptn1b:  DB $1f,$43,$bf,$0a,$43,$7f,$FF

ptn1c:  DB $83,$02,$13,$03,$13,$03,$1e,$03,$1f,$03,$13,$03,$13,$03,$1e,$03
        DB $1f,$03,$13,$03,$13,$03,$1e,$03,$1f,$03,$13,$03,$13,$03,$1e,$03
        DB $1f,$03,$13,$03,$13,$03,$1e,$03,$1f,$03,$13,$03,$13,$03,$1e,$03
        DB $1f,$03,$13,$03,$13,$03,$1e,$03,$1f,$03,$13,$03,$13,$03,$1e,$03
        DB $1f,$FF

ptn29:  DB $8f,$0b,$38,$4f,$FF
ptn2c:  DB $83,$0e,$32,$07,$32,$07,$2f,$07,$2f,$03,$2b,$87,$0b,$46,$83,$0e
        DB $2c,$03,$2c,$8f,$0b,$32,$FF

ptn2d:  DB $43,$83,$0e,$32,$03,$32,$03,$2f,$03,$2f,$03,$2c,$87,$0b,$38,$FF

ptn39:  DB $83,$01,$43,$01,$4f,$01,$5b,$87,$03,$2f,$83,$01,$43,$01,$4f,$01
        DB $5b,$87,$03,$2f,$83,$01,$43,$01,$4f,$01,$5b,$87,$03,$2f,$83,$01
        DB $43,$01,$4f,$01,$5b,$87,$03,$2f,$83,$01,$43,$01,$4f,$01,$5b,$87
        DB $03,$2f,$83,$01,$43,$01,$4f,$01,$5b,$87,$03,$2f

ptn01:  DB $83,$01,$43,$01,$4f,$01,$5b,$87,$03,$2f,$83,$01,$43,$01,$4f,$01
        DB $5b,$87,$03,$2f,$FF

ptn02:  DB $83,$02,$13,$03,$13,$03,$1f,$03,$1f,$03,$13,$03,$13,$03,$1f,$03
        DB $1f,$FF

ptn1d:  DB $03,$15,$03,$15,$03,$1f,$03,$21,$03,$15,$03,$15,$03,$1f,$03,$21
        DB $FF

ptn1e:  DB $03,$1a,$03,$1a,$03,$1c,$03,$1c,$03,$1d,$03,$1d,$03,$1e,$03,$1e
        DB $FF

ptn1f:  DB $03,$1a,$03,$1a,$03,$24,$03,$26,$03,$13,$03,$13,$07,$1f,$FF

ptn04:  DB $03,$18,$03,$18,$03,$24,$03,$24,$03,$18,$03,$18,$03,$24,$03,$24
        DB $03,$20,$03,$20,$03,$2c,$03,$2c,$03,$20,$03,$20,$03,$2c,$03,$2c
        DB $FF

ptn20:  DB $03,$19,$03,$19,$03,$25,$03,$25,$03,$19,$03,$19,$03,$25,$03,$25
        DB $03,$21,$03,$21,$03,$2d,$03,$2d,$03,$21,$03,$21,$03,$2d,$03,$2d
        DB $FF

ptn06:  DB $03,$1a,$03,$1a,$03,$26,$03,$26,$03,$1a,$03,$1a,$03,$26,$03,$26
        DB $03,$15,$03,$15,$03,$21,$03,$21,$03,$15,$03,$15,$03,$21,$03,$21
        DB $03,$18,$03,$18,$03,$24,$03,$24,$03,$18,$03,$18,$03,$24,$03,$24
        DB $03,$1f,$03,$1f,$03,$2b,$03,$2b,$03,$1f,$03,$1f,$03,$2b,$03,$2b
        DB $03,$1a,$03,$1a,$03,$26,$03,$26,$03,$1a,$03,$1a,$03,$26,$03,$26
        DB $03,$15,$03,$15,$03,$21,$03,$21,$03,$15,$03,$15,$03,$21,$03,$21
        DB $03,$18,$03,$18,$03,$24,$03,$24,$03,$18,$03,$18,$03,$24,$03,$24
        DB $03,$1c,$03,$1c,$03,$28,$03,$28,$03,$1c,$03,$1c,$03,$28,$03,$28

ptn3b:  DB $83,$04,$36,$07,$36,$07,$37,$07,$36,$03,$33,$07,$32,$57,$FF

ptn08:  DB $83,$02,$1b,$03,$1b,$03,$27,$03,$27,$03,$1b,$03,$1b,$03,$27,$03
        DB $27,$FF

ptn21:  DB $03,$1c,$03,$1c,$03,$28,$03,$28,$03,$1c,$03,$1c,$03,$28,$03,$28
        DB $FF

ptn22:  DB $03,$1d,$03,$1d,$03,$29,$03,$29,$03,$1d,$03,$1d,$03,$29,$03,$29
        DB $FF

ptn23:  DB $03,$18,$03,$18,$03,$24,$03,$24,$03,$18,$03,$18,$03,$24,$03,$24
        DB $FF

ptn24:  DB $03,$1e,$03,$1e,$03,$2a,$03,$2a,$03,$1e,$03,$1e,$03,$2a,$03,$2a
        DB $FF

ptn25:  DB $83,$05,$26,$01,$4a,$01,$34,$03,$29,$03,$4c,$03,$4a,$03,$31,$03
        DB $4a,$03,$24,$03,$22,$01,$46,$01,$30,$03,$25,$03,$48,$03,$46,$03
        DB $2d,$03,$46,$03,$24,$FF

ptn0b:  DB $83,$02,$1a,$03,$1a,$03,$26,$03,$26,$03,$1a,$03,$1a,$03,$26,$03
        DB $26,$FF

ptn0c:  DB $03,$13,$03,$13,$03,$1d,$03,$1f,$03,$13,$03,$13,$03,$1d,$03,$1f
        DB $FF

ptn26:  DB $87,$02,$1a,$87,$03,$2f,$83,$02,$26,$03,$26,$87,$03,$2f,$FF
ptn10:  DB $07,$1a,$4f,$47,$FF
ptn0e:  DB $03,$1f,$03,$1f,$03,$24,$03,$26,$07,$13,$47,$FF

ptn30:  DB $bf,$0f,$32,$0f,$32,$8f,$90,$30,$3f,$32,$13,$32,$03,$32,$03,$35
        DB $03,$37,$3f,$37,$0f,$37,$8f,$90,$30,$3f,$32,$13,$32,$03,$2d,$03
        DB $30,$03,$32,$FF

ptn31:  DB $0f,$32,$af,$90,$35,$0f,$37,$a7,$99,$37,$07,$35,$3f,$32,$13,$32
        DB $03,$32,$a3,$e8,$35,$03,$37,$0f,$35,$af,$90,$37,$0f,$37,$a7,$99
        DB $37,$07,$35,$3f,$32,$13,$32,$03,$2d,$a3,$e8,$30,$03,$32,$FF

ptn32:  DB $07,$32,$03,$39,$13,$3c,$a7,$9a,$37,$a7,$9b,$38,$07,$37,$03,$35
        DB $03,$32,$03,$39,$1b,$3c,$a7,$9a,$37,$a7,$9b,$38,$07,$37,$03,$35
        DB $03,$32,$03,$39,$03,$3c,$03,$3e,$03,$3c,$07,$3e,$03,$3c,$03,$39
        DB $a7,$9a,$37,$a7,$9b,$38,$07,$37,$03,$35,$03,$32,$af,$90,$3c,$1f
        DB $3e,$43,$03,$3e,$03,$3c,$03,$3e,$FF

ptn33:  DB $03,$3e,$03,$3e,$a3,$e8,$3c,$03,$3e,$03,$3e,$03,$3e,$a3,$e8,$3c
        DB $03,$3e,$03,$3e,$03,$3e,$a3,$e8,$3c,$03,$3e,$03,$3e,$03,$3e,$a3
        DB $e8,$3c,$03,$3e,$af,$91,$43,$1f,$41,$43,$03,$3e,$03,$41,$03,$43
        DB $03,$43,$03,$43,$a3,$e8,$41,$03,$43,$03,$43,$03,$43,$a3,$e8,$41
        DB $03,$43,$03,$45,$03,$48,$a3,$fd,$45,$03,$44,$01,$43,$01,$41,$03
        DB $3e,$03,$3c,$03,$3e,$2f,$3e,$bf,$98,$3e,$43,$03,$3e,$03,$3c,$03
        DB $3e,$FF

ptn34:  DB $03,$4a,$03,$4a,$a3,$f8,$48,$03,$4a,$03,$4a,$03,$4a,$a3,$f8,$48
        DB $03,$4a,$FF

ptn35:  DB $01,$51,$01,$54,$01,$51,$01,$54,$01,$51,$01,$54,$01,$51,$01,$54
        DB $01,$51,$01,$54,$01,$51,$01,$54,$01,$51,$01,$54,$01,$51,$01,$54
        DB $FF

ptn36:  DB $01,$50,$01,$4f,$01,$4d,$01,$4a,$01,$4f,$01,$4d,$01,$4a,$01,$48
        DB $01,$4a,$01,$48,$01,$45,$01,$43,$01,$44,$01,$43,$01,$41,$01,$3e
        DB $01,$43,$01,$41,$01,$3e,$01,$3c,$01,$3e,$01,$3c,$01,$39,$01,$37
        DB $01,$38,$01,$37,$01,$35,$01,$32,$01,$37,$01,$35,$01,$32,$01,$30
        DB $FF

ptn37:  DB $5f,$5f,$5f,$47,$83,$0e,$32,$07,$32,$07,$2f,$03,$2f,$07,$2f,$97
        DB $0b,$3a,$5f,$5f,$47,$8b,$0e,$32,$03,$32,$03,$2f,$03,$2f,$47,$97
        DB $0b,$3a,$5f,$5f,$47,$83,$0e,$2f,$0b,$2f,$03,$2f,$03,$2f,$87,$0b
        DB $30,$17,$3a,$5f,$8b,$0e,$32,$0b,$32,$0b,$2f,$0b,$2f,$07,$2c,$07
        DB $2c,$FF

ptn38:  DB $87,$0b,$34,$17,$3a,$5f,$5f,$84,$0e,$32,$04,$32,$05,$32,$04,$2f
        DB $04,$2f,$05,$2f,$47,$97,$0b,$3a,$5f,$5f,$84,$0e,$32,$04,$32,$05
        DB $32,$04,$2f,$04,$2f,$05,$2f,$FF

ptn2f:  DB $03,$1a,$03,$1a,$03,$24,$03,$26,$03,$1a,$03,$1a,$03,$18,$03,$19
        DB $03,$1a,$03,$1a,$03,$24,$03,$26,$03,$1a,$03,$1a,$03,$18,$03,$19
        DB $03,$18,$03,$18,$03,$22,$03,$24,$03,$18,$03,$18,$03,$16,$03,$17
        DB $03,$18,$03,$18,$03,$22,$03,$24,$03,$18,$03,$18,$03,$16,$03,$17
        DB $03,$13,$03,$13,$03,$1d,$03,$1f,$03,$13,$03,$13,$03,$1d,$03,$1e
        DB $03,$13,$03,$13,$03,$1d,$03,$1f,$03,$13,$03,$13,$03,$1d,$03,$1e
        DB $03,$1a,$03,$1a,$03,$24,$03,$26,$03,$1a,$03,$1a,$03,$18,$03,$19
        DB $03,$1a,$03,$1a,$03,$24,$03,$26,$03,$1a,$03,$1a,$03,$18,$03,$19
        DB $FF


;====================================================================
; INSTRUMENTS
;====================================================================
instr:
        DB $80,$09,$41,$48,$60,$03,$81,$00
        DB $00,$08,$81,$02,$08,$00,$00,$01
        DB $a0,$02,$41,$09,$80,$00,$00,$00
        DB $00,$02,$81,$09,$09,$00,$00,$05
        DB $00,$08,$41,$08,$50,$02,$00,$04
        DB $00,$01,$41,$3f,$c0,$02,$00,$00
        DB $00,$08,$41,$04,$40,$02,$00,$00
        DB $00,$08,$41,$09,$00,$02,$00,$00
        DB $00,$09,$41,$09,$70,$02,$5f,$04
        DB $00,$09,$41,$4a,$69,$02,$81,$00
        DB $00,$09,$41,$40,$6f,$00,$81,$02
        DB $80,$07,$81,$0a,$0a,$00,$00,$01
        DB $00,$09,$41,$3f,$ff,$01,$e7,$02
        DB $00,$08,$41,$90,$f0,$01,$e8,$02
        DB $00,$08,$41,$06,$0a,$00,$00,$01
        DB $00,$09,$41,$19,$70,$02,$a8,$00
