; =============================================================================
; MONTY ON THE RUN - Z80 ENGINE CONVERSION
; =============================================================================

; Audio
REG_SIDBASE:            equ $fb18
;REG_SIDBASE:            equ $3c00

REG_VIDBASE:            equ $3c00

; vsync line (0-200, 200 is overscan) (RD)
REG_VSYNC:              equ $fb0f


dw START
 
ORG   $5000   


START:
    ; Initialize music with track number 0
    XOR A                   ; A = 0
    CALL initmusic

    ;LD A,$0f
    ;LD (REG_SIDBASE + $18), A
    ;LD A,$f0
    ;LD (REG_SIDBASE + $06), A
    ;LD A,$64
    ;LD (REG_SIDBASE + $01), A
    ;LD A,$21
    ;LD (REG_SIDBASE + $04), A

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
    CALL moff
    RET


;====================================================================
; init music
;====================================================================
initmusic:

        add  a,a          ; a*2
        ld   d,a
        add  a,a          ; + a*4
        add  a,d          ; => a = musicnum * 6

        ld   e,a
        ld   d,0
        ld   hl,songs
        add  hl,de        ; HL = songs + musicnum*6

        ld   de,currtrkhi ; copy 6 bytes of songs (3 trackpt) into currtrklo/hi
        ld   c,6
init_trk_copy:
        ld   a,(hl)
        ld   (de),a
        inc  hl
        inc  de
        dec  c
        jr   nz,init_trk_copy


        ; Clear control registers
        XOR A           ; A =0 
        LD (REG_SIDBASE + $04), A
        LD (REG_SIDBASE + $0B), A
        LD (REG_SIDBASE + $12), A
        LD (REG_SIDBASE + $17), A

        LD A, $0F       ; Full volume
        LD (REG_SIDBASE + $18), A

        LD A, $40       ; Flag init music
        LD (mstatus), A
        RET


;====================================================================
; play music
;====================================================================
playmusic:
        LD A, (counter)
        INC A
        LD (counter), A

        LD A, (mstatus) ; Test music status
        AND A           ; Set flags based on A
        JP M, moff      ; If bit 7 is set (negative), music is off
        AND $40
        JR Z, contplay  ; If bit 6 is clear, continue playing, else init

;==========
; init the song (mstatus $40)
        XOR A
        LD (counter), A

        LD B, 3         ; Loop 3 channels (X equivalent counter)
init_chan_loop:
        DEC B
        
        ; Offset indexing simulation into tables
        LD C, B
        LD B, 0
        
        LD HL, posoffset
        ADD HL, BC
        LD (HL), A
        
        LD HL, patoffset
        ADD HL, BC
        LD (HL), A
        
        LD HL, lengthleft
        ADD HL, BC
        LD (HL), A
        
        LD HL, notenum
        ADD HL, BC
        LD (HL), A
        
        LD B, C
        AND A
        JR NZ, init_chan_loop

        XOR A
        LD (mstatus), A ; Signal music play
        JP contplay


;==========
; music is off (mstatus $80 or $c0)
moff:
        LD A, (mstatus)
        AND $40
        JR Z, +l2         ; If bit6 is clear, skip killing voices

        XOR A
        LD (REG_SIDBASE + $04), A ; Kill voice 1, 2, 3 registers
        LD (REG_SIDBASE + $0B), A
        LD (REG_SIDBASE + $12), A

        LD A, $0F       ; Full volume still
        LD (REG_SIDBASE + $18), A

        LD A, $80       ; Flag no need to kill sound next time
        LD (mstatus), A

l2:     JP musicend


;==========
; music is playing
contplay:
        LD B, 3         ; Channel loop counter (X = 3)

        LD A, (speed)   ; Check processing speed
        DEC A
        LD (speed), A
        JP P, mainloop

        LD A, (resetspd)
        LD (speed), A

mainloop:
        DEC B                   ; Adjust index (0-2) and decrement
        LD C, B
        LD B, 0
        LD HL, regoffsets
        ADD HL, BC

        LD A, C                 ; save channel offset
        LD (tmpchnofst), A
        XOR A
        LD (tmpchnofst+1), A  
        LD (tmpregofst+1), A    ; save SID regs offset
        LD A, (HL)
        LD (tmpregofst), A

 ;       LD HL, posoffset        ; JMH print pointer track
 ;       ADD HL, BC
 ;       LD A, (HL)
 ;       LD HL,REG_VIDBASE
 ;       LD BC,(tmpregofst)
 ;       ADD HL,BC
 ;       CALL printhex
 ;       LD BC,(tmpchnofst)

 ;       LD HL, patoffset        ; JMH print pointer pattern
 ;       ADD HL, BC
 ;       LD A, (HL)
 ;       LD HL,REG_VIDBASE+64
 ;       LD BC,(tmpregofst)
 ;       ADD HL,BC
 ;       CALL printhex
 ;       LD BC,(tmpchnofst)


        ; Check whether a new note is needed
        LD A, (speed)           ;if speed not reset
        LD HL, resetspd         ;then skip notework
        CP (HL)
        JR Z, checknewnote
;        JP vibrato
        JP loopcont


checknewnote:
        ; Put base address word of this track in ptr_02
        LD HL, currtrkhi        ; hi and low are reversed if you look at the song table!!
        ADD HL, BC              ;put base addr.w of
        LD A, (HL)              ;this track in $2
        LD (ptr_02), A
        LD HL, currtrklo
        ADD HL, BC
        LD A, (HL)
        LD (ptr_02+1), A

        LD HL, lengthleft       ;check whether a new
        ADD HL, BC              ;note is needed
        LD A, (HL)
        DEC A
        LD (HL), A
        JP M, getnewnote
 ;       JP soundwork            ; No new note needed
        JP loopcont


printhex:
        LD B,A
        LD DE,hex
        SRL A
        SRL A
        SRL A
        SRL A
        ADD A, E
        LD E, A
        JR NC, hex1
        INC D
hex1:
        LD A,(DE)
        LD (HL),A
        INC HL
        LD DE,hex
        LD A,B
        AND $0f
        ADD A, E
        LD E, A
        JR NC, hex2
        INC D
hex2:
        LD A,(DE)
        LD (HL),A
        LD A,B
        RET


;==========
; notework
;a new note is needed. get the pattern
;number/cc from this position

getnewnote:

        LD HL, posoffset        ;get the data from
        ADD HL, BC              ;the current position
        LD A, (HL)
        LD DE, (ptr_02)         ; Indirect lookup: A = (ptr_02) + offset A
        ADD A, E
        LD E, A
        JR NC, l3
        INC D
l3:     LD A, (DE)              ; Load from current positions pointer

        CP $FF                  ;pos $ff restarts
        JR Z, restart
        CP $FE                  ;pos $fe stops music
        JR NZ, getnotedata
        JP musicend

;cc of $ff restarts this track from the
;first position

restart:
        XOR A                   ;get note immediately
        LD HL, lengthleft       ;and reset pat,pos
        ADD HL, BC
        LD (HL), A
        LD HL, posoffset
        ADD HL, BC
        LD (HL), A
        LD HL, patoffset
        ADD HL, BC
        LD (HL), A
        JP getnewnote

;get the note data from this pattern

getnotedata:

;        LD HL,REG_VIDBASE+128 ; JMH print pattern
;        LD BC,(tmpregofst)
;        ADD HL,BC
;        CALL printhex
;        LD BC,(tmpchnofst)


        ; Locate pattern base pointer address word into ptr_04
        LD E, A                 ;put base addr.w of
        LD D, 0                 ;the pattern in $4
        LD HL, patptl
        ADD HL, DE
        LD A, (HL)
        LD (ptr_04), A
        LD HL, patpth
        ADD HL, DE
        LD A, (HL)
        LD (ptr_04 + 1), A
        
        XOR A                   ;default no portamento
        LD HL, portaval
        ADD HL, BC
        LD (HL), A

        LD HL, patoffset        ;get offset into ptn
        ADD HL, BC
        LD E, (HL)              ; Pattern offset value               
        LD D, 0

        LD A, $FF               ; Default no append
        LD (appendfl), A

;1st byte is the length of the note 0-31
;bit5 signals no release (see sndwork)
;bit6 signals appended note
;bit7 signals a new instrument
;     or portamento coming up

        LD HL, (ptr_04)         ;get length of note
        ADD HL, DE
        LD A, (HL)

        LD HL, savelnthcc
        ADD HL, BC
        LD (HL), A
        LD (templnthcc), A
        AND $1F
        LD HL, lengthleft
        ADD HL, BC
        LD (HL), A

        LD A, (templnthcc)      ;test for append
        AND $40                 ; Check Overflow flag replacement via Bit 6 test
        JR NZ, appendnote
        LD HL, patoffset        ;pt to next data
        ADD HL, BC
        INC (HL)               

        LD A, (templnthcc)      ;2nd byte needed?
        AND $80                ; Check Sign flag replacement via Bit 7 test
        JR Z, getpitch

;2nd byte needed as 1st byte negative
;2nd byte is the instrument number(+ve)
;or portamento speed(-ve)
        LD HL, patoffset        ;get instr/portamento
        ADD HL, BC
        LD E, (HL)       
        LD D, 0
        LD HL, (ptr_04)
        ADD HL, DE
        LD A, (HL)

        AND A
        JP P, l5                ; positive is intrument 
        LD HL, portaval
        ADD HL, BC
        LD (HL), A              ; Save portamento val
        JR l6

l5:     LD HL, instrnr
        ADD HL, BC
        LD (HL), A              ; Save instrument number

l6:     LD HL, patoffset
        ADD HL, BC
        INC (HL)

;3rd byte is the pitch of the note
;get the 'base frequency' here
getpitch:
        LD HL, patoffset        ; get pitch of note
        ADD HL, BC
        LD E, (HL)
        LD D, 0
        LD HL, (ptr_04)
        ADD HL, DE
        LD A, (HL)

        LD HL, notenum
        ADD HL, BC
        LD (HL), A
        ADD A, A                ; pitch * 2
        
        LD E, A
        LD D, 0
        LD HL, frequenzlo
        ADD HL, DE
        LD A, (HL)              ;save the appropriate
        LD (tempfreq), A        ;base frequency

        LD HL, frequenzhi
        ADD HL, DE
        LD A, (HL)
        LD DE, (tmpregofst)
        LD HL, REG_SIDBASE + $01
        ADD HL, DE
        LD (HL), A              ; Save high frequency to chip
        LD HL, savefreqhi
        ADD HL, BC
        LD (HL), A

        LD A, (tempfreq)
        LD HL, REG_SIDBASE + $00
        ADD HL, DE
        LD (HL), A              ; Save low frequency to chip
        LD HL, savefreqlo
        ADD HL, BC
        LD (HL), A
        JR l7

appendnote:
        LD HL, appendfl         ;clever eh?
        DEC (HL)


;fetch all the initial values from the
;instrument data structure
l7:     
        LD HL, instrnr
        ADD HL, BC
        LD A, (HL)              ; Instrument number
        ADD A, A                ; Instr num * 8 
        ADD A, A
        ADD A, A
        LD E, A                 ; DE offset to instrument
        LD D, 0

        LD HL, instr + 2        ;get control reg val
        ADD HL, DE
        LD A, (HL)
        LD (tempctrl), A
        LD A, (appendfl)        ;implement append
        AND (HL)
        LD BC, (tmpregofst)
        LD IX, REG_SIDBASE + $04
        ADD IX, BC
        LD (IX + 0), A
        LD HL, instr + 0        ;get pulse width lo
        ADD HL, DE
        LD A, (HL)
        LD IX, REG_SIDBASE + $02
        ADD IX, BC
        LD (IX + 0), A
        LD HL, instr + 1        ;get pulse width hi
        ADD HL, DE
        LD A, (HL)
        LD IX, REG_SIDBASE + $03
        ADD IX, BC
        LD (IX + 0), A
        LD HL, instr + 3        ;get attack/decay
        ADD HL, DE
        LD A, (HL)
        LD IX, REG_SIDBASE + $05
        ADD IX, BC
        LD (IX + 0), A
        LD HL, instr + 4        ;get sustain/release
        ADD HL, DE
        LD A, (HL)
        LD IX, REG_SIDBASE + $06
        ADD IX, BC
        LD (IX + 0), A

        LD BC, (tmpchnofst)     ; Restore loop channel index
        LD A, (tempctrl)        ;save control reg val
        LD HL, voicectrl
        ADD HL, BC
        LD (HL), A

;4th byte checks for the end of pattern
;if eop found, inc the position and
;reset patoffset for new pattern

        LD HL, patoffset        ;preview 4th byte
        ADD HL, BC
        INC (HL)
        LD A, (HL)
        LD E, A
        LD D, 0
        LD HL, (ptr_04)
        ADD HL, DE
        LD A, (HL)

        CP $FF                  ; Check for end of pattern
        JR NZ, l8
        XOR A
        LD HL, patoffset        ; reset pos and pat offsets
        ADD HL, BC
        LD (HL), A
        LD HL, posoffset
        ADD HL, BC
        INC (HL)

l8:     JP loopcont




;==========
; loop check
loopcont:
        LD A, (tmpchnofst)
        LD B, A                 ; loop uses B!
        AND A                   ; Check if inner channel index reached 0
        JP Z, musicend
        JP mainloop

musicend:
        RET

;====================================================================
; FREQUENCY TABLES
;====================================================================
frequenzlo:
        DB $16
frequenzhi:
        DB $01
        DB $27,$01,$38,$01,$4b,$01
        DB $5f,$01,$73,$01,$8a,$01,$a1,$01
        DB $ba,$01,$d4,$01,$f0,$01,$0e,$02
        DB $2d,$02,$4e,$02,$71,$02,$96,$02
        DB $bd,$02,$e7,$02,$13,$03,$42,$03
        DB $74,$03,$a9,$03,$e0,$03,$1b,$04
        DB $5a,$04,$9b,$04,$e2,$04,$2c,$05
        DB $7b,$05,$ce,$05,$27,$06,$85,$06
        DB $e8,$06,$51,$07,$c1,$07,$37,$08
        DB $b4,$08,$37,$09,$c4,$09,$57,$0a
        DB $f5,$0a,$9c,$0b,$4e,$0c,$09,$0d
        DB $d0,$0d,$a3,$0e,$82,$0f,$6e,$10
        DB $68,$11,$6e,$12,$88,$13,$af,$14
        DB $eb,$15,$39,$17,$9c,$18,$13,$1a
        DB $a1,$1b,$46,$1d,$04,$1f,$dc,$20
        DB $d0,$22,$dc,$24,$10,$27,$5e,$29
        DB $d6,$2b,$72,$2e,$38,$31,$26,$34
        DB $42,$37,$8c,$3a,$08,$3e,$b8,$41
        DB $a0,$45,$b8,$49,$20,$4e,$bc,$52
        DB $ac,$57,$e4,$5c,$70,$62,$4c,$68
        DB $84,$6e,$18,$75,$10,$7c,$70,$83
        DB $40,$8b,$70,$93,$40,$9c,$78,$a5
        DB $58,$af,$c8,$b9,$e0,$c4,$98,$d0
        DB $08,$dd,$30,$ea,$20,$f8,$2e,$fd


;====================================================================
; Variables
;====================================================================
ptr_02:     DEFW 0              ; Replacement for 6502 zero page $02/$03
ptr_04:     DEFW 0              ; Replacement for 6502 zero page $04/$05
tmpregofst: DEFW 0              ; SID reg offset as pair reg
tmpchnofst: DEFW 0              ; CHANNEL offset as pair reg
tempstore:  DEFW 0
regoffsets: DB $00, $07, $0E
posoffset:  DB $00, $00, $00
patoffset:  DB $00, $00, $00
lengthleft: DB $00, $00, $00
savelnthcc: DB $00, $00, $00
voicectrl:  DB $00, $00, $00
notenum:    DB $00, $00, $00
instrnr:    DB $00, $00, $00
appendfl:   DB $00
templnthcc: DB $00
tempfreq:   DB $00
tempctrl:   DB $00
vibrdepth:  DB $00
pulsevalue: DB $00
tmpvdiflo:  DB $00
tmpvdifhi:  DB $00
tmpvfrqlo:  DB $00
tmpvfrqhi:  DB $00
oscilatval: DB $00
pulsedelay: DB $00, $00, $00
pulsedir:   DB $00, $00, $00
speed:      DB $00
resetspd:   DB $01
instnumby8: DB $00
mstatus:    DB $C0
savefreqhi: DB $00, $00, $00
savefreqlo: DB $00, $00, $00
portaval:   DB $00, $00, $00
instrfx:    DB $00
pulsespeed: DB $00
counter:    DB $00
currtrkhi:  DB $00, $00, $00
currtrklo:  DB $00, $00, $00




;;====================================
;monty on the run main theme
;====================================

songs:
 DB montymaintr1
 DB montymaintr2
 DB montymaintr3
 DB montymaintr1/256
 DB montymaintr2/256
 DB montymaintr3/256


;====================================
;pointers to the patterns

;low pointers
patptl:
 DB ptn00
 DB ptn01
 DB ptn02
 DB ptn03
 DB ptn04
 DB ptn05
 DB ptn06
 DB ptn07
 DB ptn08
 DB ptn09
 DB ptn0a
 DB ptn0b
 DB ptn0c
 DB ptn0d
 DB ptn0e
 DB ptn0f
 DB ptn10
 DB ptn11
 DB ptn12
 DB ptn13
 DB ptn14
 DB ptn15
 DB ptn16
 DB ptn17
 DB ptn18
 DB ptn19
 DB ptn1a
 DB ptn1b
 DB ptn1c
 DB ptn1d
 DB ptn1e
 DB ptn1f
 DB ptn20
 DB ptn21
 DB ptn22
 DB ptn23
 DB ptn24
 DB ptn25
 DB ptn26
 DB ptn27
 DB ptn28
 DB ptn29
 DB ptn2a
 DB ptn2b
 DB ptn2c
 DB ptn2d
 DB 0
 DB ptn2f
 DB ptn30
 DB ptn31
 DB ptn32
 DB ptn33
 DB ptn34
 DB ptn35
 DB ptn36
 DB ptn37
 DB ptn38
 DB ptn39
 DB ptn3a
 DB ptn3b

;high pointers
patpth:
 DB ptn00/256
 DB ptn01/256
 DB ptn02/256
 DB ptn03/256
 DB ptn04/256
 DB ptn05/256
 DB ptn06/256
 DB ptn07/256
 DB ptn08/256
 DB ptn09/256
 DB ptn0a/256
 DB ptn0b/256
 DB ptn0c/256
 DB ptn0d/256
 DB ptn0e/256
 DB ptn0f/256
 DB ptn10/256
 DB ptn11/256
 DB ptn12/256
 DB ptn13/256
 DB ptn14/256
 DB ptn15/256
 DB ptn16/256
 DB ptn17/256
 DB ptn18/256
 DB ptn19/256
 DB ptn1a/256
 DB ptn1b/256
 DB ptn1c/256
 DB ptn1d/256
 DB ptn1e/256
 DB ptn1f/256
 DB ptn20/256
 DB ptn21/256
 DB ptn22/256
 DB ptn23/256
 DB ptn24/256
 DB ptn25/256
 DB ptn26/256
 DB ptn27/256
 DB ptn28/256
 DB ptn29/256
 DB ptn2a/256
 DB ptn2b/256
 DB ptn2c/256
 DB ptn2d/256
 DB 0
 DB ptn2f/256
 DB ptn30/256
 DB ptn31/256
 DB ptn32/256
 DB ptn33/256
 DB ptn34/256
 DB ptn35/256
 DB ptn36/256
 DB ptn37/256
 DB ptn38/256
 DB ptn39/256
 DB ptn3a/256
 DB ptn3b/256


;====================================
;tracks
;====================================

;track1
montymaintr1:
 DB $11,$14,$17,$1a,$00,$27,$00,$28
 DB $03,$05,$00,$27,$00,$28,$03,$05
 DB $07,$3a,$14,$17,$00,$27,$00,$28
 DB $2f,$30,$31,$31,$32,$33,$33,$34
 DB $34,$34,$34,$34,$34,$34,$34,$35
 DB $35,$35,$35,$35,$35,$36,$12,$37
 DB $38,$09,$2a,$09,$2b,$09,$0a,$09
 DB $2a,$09,$2b,$09,$0a,$0d,$0d,$0f
 DB $ff

;track2
montymaintr2:
 DB $12,$15,$18,$1b,$2d,$39,$39
 DB $39,$39,$39,$39,$2c,$39,$39,$39
 DB $39,$39,$39,$2c,$39,$39,$39,$01
 DB $01,$29,$29,$2c,$15,$18,$39,$39
 DB $39,$39,$39,$39,$39,$39,$39,$39
 DB $39,$39,$39,$39,$39,$39,$39,$39
 DB $39,$39,$39,$39,$39,$39,$39,$39
 DB $39,$39,$39,$39,$39,$01,$01,$01
 DB $29,$39,$39,$39,$01,$01,$01,$29
 DB $39,$39,$39,$39,$ff

;track3
montymaintr3:
 DB $13,$16,$19
 DB $1c,$02,$02,$1d,$1e,$02,$02,$1d
 DB $1f,$04,$04,$20,$20,$06,$02,$02
 DB $1d,$1e,$02,$02,$1d,$1f,$04,$04
 DB $20,$20,$06,$08,$08,$08,$08,$21
 DB $21,$21,$21,$22,$22,$22,$23,$22
 DB $24,$25,$3b,$26,$26,$26,$26,$26
 DB $26,$26,$26,$26,$26,$26,$26,$26
 DB $26,$26,$26,$02,$02,$1d,$1e,$02
 DB $02,$1d,$1f,$2f,$2f,$2f,$2f,$2f
 DB $2f,$2f,$2f,$2f,$2f,$2f,$2f,$2f
 DB $0b,$0b,$1d,$1d,$0b,$0b,$1d,$0b
 DB $0b,$0b,$0c,$0c,$1d,$1d,$1d,$10
 DB $0b,$0b,$1d,$1d,$0b,$0b,$1d,$0b
 DB $0b,$0b,$0c,$0c,$1d,$1d,$1d,$10
 DB $0b,$1d,$0b,$1d,$0b,$1d,$0b,$1d
 DB $0b,$0c,$1d,$0b,$0c,$23,$0b,$0b
 DB $ff


;====================================
;patterns
;====================================

ptn00:
 DB $83,$00,$37,$01,$3e,$01,$3e,$03
 DB $3d,$03,$3e,$03,$43,$03,$3e,$03
 DB $3d,$03,$3e,$03,$37,$01,$3e,$01
 DB $3e,$03,$3d,$03,$3e,$03,$43,$03
 DB $42,$03,$43,$03,$45,$03,$46,$01
 DB $48,$01,$46,$03,$45,$03,$43,$03
 DB $4b,$01,$4d,$01,$4b,$03,$4a,$03
 DB $48,$ff

ptn27:
 DB $1f,$4a,$ff

ptn28:
 DB $03,$46,$01,$48,$01,$46,$03,$45
 DB $03,$4a,$0f,$43,$ff

ptn03:
 DB $bf,$06
 DB $48,$07,$48,$01,$4b,$01,$4a,$01
 DB $4b,$01,$4a,$03,$4b,$03,$4d,$03
 DB $4b,$03,$4a,$3f,$48,$07,$48,$01
 DB $4b,$01,$4a,$01,$4b,$01,$4a,$03
 DB $4b,$03,$4d,$03,$4b,$03,$48,$3f
 DB $4c,$07,$4c,$01,$4f,$01,$4e,$01
 DB $4f,$01,$4e,$03,$4f,$03,$51,$03
 DB $4f,$03,$4e,$3f,$4c,$07,$4c,$01
 DB $4f,$01,$4e,$01,$4f,$01,$4e,$03
 DB $4f,$03,$51,$03,$4f,$03,$4c,$ff

ptn05:
 DB $83,$04,$26,$03,$29,$03,$28,$03
 DB $29,$03,$26,$03,$35,$03,$34,$03
 DB $32,$03,$2d,$03,$30,$03,$2f,$03
 DB $30,$03,$2d,$03,$3c,$03,$3b,$03
 DB $39,$03,$30,$03,$33,$03,$32,$03
 DB $33,$03,$30,$03,$3f,$03,$3e,$03
 DB $3c,$03,$46,$03,$45,$03,$43,$03
 DB $3a,$03,$39,$03,$37,$03,$2e,$03
 DB $2d,$03,$26,$03,$29,$03,$28,$03
 DB $29,$03,$26,$03,$35,$03,$34,$03
 DB $32,$03,$2d,$03,$30,$03,$2f,$03
 DB $30,$03,$2d,$03,$3c,$03,$3b,$03
 DB $39,$03,$30,$03,$33,$03,$32,$03
 DB $33,$03,$30,$03,$3f,$03,$3e,$03
 DB $3c,$03,$34,$03,$37,$03,$36,$03
 DB $37,$03,$34,$03,$37,$03,$3a,$03
 DB $3d

ptn3a:
 DB $03,$3e,$07,$3e,$07,$3f,$07
 DB $3e,$03,$3c,$07,$3e,$57,$ff

ptn07:
 DB $8b
 DB $00,$3a,$01,$3a,$01,$3c,$03,$3d
 DB $03,$3f,$03,$3d,$03,$3c,$0b,$3a
 DB $03,$39,$07,$3a,$81,$06,$4b,$01
 DB $4d,$01,$4e,$01,$4d,$01,$4e,$01
 DB $4d,$05,$4b,$81,$00,$3a,$01,$3c
 DB $01,$3d,$03,$3f,$03,$3d,$03,$3c
 DB $03,$3a,$03,$39,$1b,$3a,$0b,$3b
 DB $01,$3b,$01,$3d,$03,$3e,$03,$40
 DB $03,$3e,$03,$3d,$0b,$3b,$03,$3a
 DB $07,$3b,$81,$06,$4c,$01,$4e,$01
 DB $4f,$01,$4e,$01,$4f,$01,$4e,$05
 DB $4c,$81,$00,$3b,$01,$3d,$01,$3e
 DB $03,$40,$03,$3e,$03,$3d,$03,$3b
 DB $03,$3a,$1b,$3b,$8b,$05,$35,$03
 DB $33,$07,$32,$03,$30,$03,$2f,$0b
 DB $30,$03,$32,$0f,$30,$0b,$35,$03
 DB $33,$07,$32,$03,$30,$03,$2f,$1f
 DB $30,$8b,$00,$3c,$01,$3c,$01,$3e
 DB $03,$3f,$03,$41,$03,$3f,$03,$3e
 DB $0b,$3d,$01,$3d,$01,$3f,$03,$40
 DB $03,$42,$03,$40,$03,$3f,$03,$3e
 DB $01,$3e,$01,$40,$03,$41,$03,$40
 DB $03,$3e,$03,$3d,$03,$3e,$03,$3c
 DB $03,$3a,$01,$3a,$01,$3c,$03,$3d
 DB $03,$3c,$03,$3a,$03,$39,$03,$3a
 DB $03,$3c,$ff

ptn09:
 DB $83,$00,$32,$01,$35,$01,$34,$03
 DB $32,$03,$35,$03,$34,$03,$32,$03
 DB $35,$01,$34,$01,$32,$03,$32,$03
 DB $3a,$03,$39,$03,$3a,$03,$32,$03
 DB $3a,$03,$39,$03,$3a,$ff

ptn2a:
 DB $03,$34,$01,$37,$01,$35,$03,$34
 DB $03,$37,$03,$35,$03,$34,$03,$37
 DB $01,$35,$01,$34,$03,$34,$03,$3a
 DB $03,$39,$03,$3a,$03,$34,$03,$3a
 DB $03,$39,$03,$3a,$ff

ptn2b:
 DB $03,$39,$03,$38,$03,$39,$03,$3a
 DB $03,$39,$03,$37,$03,$35,$03,$34
 DB $03,$35,$03,$34,$03,$35,$03,$37
 DB $03,$35,$03,$34,$03,$32,$03,$31
 DB $ff

ptn0a:
 DB $03
 DB $37,$01,$3a,$01,$39,$03,$37,$03
 DB $3a,$03,$39,$03,$37,$03,$3a,$01
 DB $39,$01,$37,$03,$37,$03,$3e,$03
 DB $3d,$03,$3e,$03,$37,$03,$3e,$03
 DB $3d,$03,$3e,$03,$3d,$01,$40,$01
 DB $3e,$03,$3d,$03,$40,$01,$3e,$01
 DB $3d,$03,$40,$03,$3e,$03,$40,$03
 DB $40,$01,$43,$01,$41,$03,$40,$03
 DB $43,$01,$41,$01,$40,$03,$43,$03
 DB $41,$03,$43,$03,$43,$01,$46,$01
 DB $45,$03,$43,$03,$46,$01,$45,$01
 DB $43,$03,$46,$03,$45,$03,$43,$01
 DB $48,$01,$49,$01,$48,$01,$46,$01
 DB $45,$01,$46,$01,$45,$01,$43,$01
 DB $41,$01,$43,$01,$41,$01,$40,$01
 DB $3d,$01,$39,$01,$3b,$01,$3d,$ff

ptn0d:
 DB $01,$3e,$01,$39,$01,$35,$01,$39
 DB $01,$3e,$01,$39,$01,$35,$01,$39
 DB $03,$3e,$01,$41,$01,$40,$03,$40
 DB $01,$3d,$01,$3e,$01,$40,$01,$3d
 DB $01,$39,$01,$3d,$01,$40,$01,$3d
 DB $01,$39,$01,$3d,$03,$40,$01,$43
 DB $01,$41,$03,$41,$01,$3e,$01,$40
 DB $01,$41,$01,$3e,$01,$39,$01,$3e
 DB $01,$41,$01,$3e,$01,$39,$01,$3e
 DB $03,$41,$01,$45,$01,$43,$03,$43
 DB $01,$40,$01,$41,$01,$43,$01,$40
 DB $01,$3d,$01,$40,$01,$43,$01,$40
 DB $01,$3d,$01,$40,$01,$46,$01,$43
 DB $01,$45,$01,$46,$01,$44,$01,$43
 DB $01,$40,$01,$3d,$ff

ptn0f:
 DB $01,$3e,$01
 DB $39,$01,$35,$01,$39,$01,$3e,$01
 DB $39,$01,$35,$01,$39,$01,$3e,$01
 DB $39,$01,$35,$01,$39,$01,$3e,$01
 DB $39,$01,$35,$01,$39,$01,$3e,$01
 DB $3a,$01,$37,$01,$3a,$01,$3e,$01
 DB $3a,$01,$37,$01,$3a,$01,$3e,$01
 DB $3a,$01,$37,$01,$3a,$01,$3e,$01
 DB $3a,$01,$37,$01,$3a,$01,$40,$01
 DB $3d,$01,$39,$01,$3d,$01,$40,$01
 DB $3d,$01,$39,$01,$3d,$01,$40,$01
 DB $3d,$01,$39,$01,$3d,$01,$40,$01
 DB $3d,$01,$39,$01,$3d,$01,$41,$01
 DB $3e,$01,$39,$01,$3e,$01,$41,$01
 DB $3e,$01,$39,$01,$3e,$01,$41,$01
 DB $3e,$01,$39,$01,$3e,$01,$41,$01
 DB $3e,$01,$39,$01,$3e,$01,$43,$01
 DB $3e,$01,$3a,$01,$3e,$01,$43,$01
 DB $3e,$01,$3a,$01,$3e,$01,$43,$01
 DB $3e,$01,$3a,$01,$3e,$01,$43,$01
 DB $3e,$01,$3a,$01,$3e,$01,$43,$01
 DB $3f,$01,$3c,$01,$3f,$01,$43,$01
 DB $3f,$01,$3c,$01,$3f,$01,$43,$01
 DB $3f,$01,$3c,$01,$3f,$01,$43,$01
 DB $3f,$01,$3c,$01,$3f,$01,$45,$01
 DB $42,$01,$3c,$01,$42,$01,$45,$01
 DB $42,$01,$3c,$01,$42,$01,$48,$01
 DB $45,$01,$42,$01,$45,$01,$4b,$01
 DB $48,$01,$45,$01,$48,$01,$4b,$01
 DB $4a,$01,$48,$01,$4a,$01,$4b,$01
 DB $4a,$01,$48,$01,$4a,$01,$4b,$01
 DB $4a,$01,$48,$01,$4a,$01,$4c,$01
 DB $4e,$03,$4f,$ff

ptn11:
 DB $bf,$06,$56,$1f,$57,$1f,$56,$1f
 DB $5b,$1f,$56,$1f,$57,$1f,$56,$1f
 DB $4f,$ff

ptn12:
 DB $bf,$0c,$68,$7f,$7f,$7f,$7f,$7f
 DB $7f,$7f,$ff

ptn13:
 DB $bf,$08,$13,$3f,$13,$3f,$13,$3f
 DB $13,$3f,$13,$3f,$13,$3f,$13,$1f
 DB $13,$ff

ptn14:
 DB $97,$09,$2e,$03,$2e,$1b,$32,$03
 DB $32,$1b,$31,$03,$31,$1f,$34,$43
 DB $17,$32,$03,$32,$1b,$35,$03,$35
 DB $1b,$34,$03,$34,$0f,$37,$8f,$0a
 DB $37,$43,$ff

ptn15:
 DB $97,$09,$2b,$03,$2b,$1b,$2e,$03
 DB $2e,$1b,$2d,$03,$2d,$1f,$30,$43
 DB $17,$2e,$03,$2e,$1b,$32,$03,$32
 DB $1b,$31,$03,$31,$0f,$34,$8f,$0a
 DB $34,$43,$ff

ptn16:
 DB $0f,$1f,$0f,$1f,$0f,$1f,$0f,$1f
 DB $0f,$1f,$0f,$1f,$0f,$1f,$0f,$1f
 DB $0f,$1f,$0f,$1f,$0f,$1f,$0f,$1f
 DB $0f,$1f,$0f,$1f,$0f,$1f,$0f,$1f
 DB $ff

ptn17:
 DB $97,$09,$33,$03,$33,$1b,$37,$03
 DB $37,$1b,$36,$03,$36,$1f,$39,$43
 DB $17,$37,$03,$37,$1b,$3a,$03,$3a
 DB $1b,$39,$03,$39,$2f,$3c,$21,$3c
 DB $21,$3d,$21,$3e,$21,$3f,$21,$40
 DB $21,$41,$21,$42,$21,$43,$21,$44
 DB $01,$45,$ff

ptn18:
 DB $97,$09,$30,$03,$30,$1b,$33,$03
 DB $33,$1b,$32,$03,$32,$1f,$36,$43
 DB $17,$33,$03,$33,$1b,$37,$03,$37
 DB $1b,$36,$03,$36,$2f,$39,$21,$39
 DB $21,$3a,$21,$3b,$21,$3c,$21,$3d
 DB $21,$3e,$21,$3f,$21,$40,$21,$41
 DB $01,$42,$ff

ptn19:
 DB $0f,$1a,$0f,$1a,$0f,$1a,$0f,$1a
 DB $0f,$1a,$0f,$1a,$0f,$1a,$0f,$1a
 DB $0f,$1a,$0f,$1a,$0f,$1a,$0f,$1a
 DB $0f,$1a,$0f,$1a,$0f,$1a,$0f,$1a
 DB $ff

ptn1a:
 DB $1f,$46,$bf,$0a,$46,$7f,$7f,$ff

ptn1b:
 DB $1f,$43,$bf,$0a,$43,$7f,$ff

ptn1c:
 DB $83,$02,$13,$03,$13,$03,$1e,$03
 DB $1f,$03,$13,$03,$13,$03,$1e,$03
 DB $1f,$03,$13,$03,$13,$03,$1e,$03
 DB $1f,$03,$13,$03,$13,$03,$1e,$03
 DB $1f,$03,$13,$03,$13,$03,$1e,$03
 DB $1f,$03,$13,$03,$13,$03,$1e,$03
 DB $1f,$03,$13,$03,$13,$03,$1e,$03
 DB $1f,$03,$13,$03,$13,$03,$1e,$03
 DB $1f,$ff

ptn29:
 DB $8f,$0b,$38,$4f,$ff

ptn2c:
 DB $83,$0e,$32,$07,$32,$07,$2f,$07
 DB $2f,$03,$2b,$87,$0b,$46,$83,$0e
 DB $2c,$03,$2c,$8f,$0b,$32,$ff

ptn2d:
 DB $43,$83,$0e,$32,$03,$32,$03,$2f
 DB $03,$2f,$03,$2c,$87,$0b,$38,$ff

ptn39:
 DB $83,$01
 DB $43,$01,$4f,$01,$5b,$87,$03,$2f
 DB $83,$01,$43,$01,$4f,$01,$5b,$87
 DB $03,$2f,$83,$01,$43,$01,$4f,$01
 DB $5b,$87,$03,$2f,$83,$01,$43,$01
 DB $4f,$01,$5b,$87,$03,$2f,$83,$01
 DB $43,$01,$4f,$01,$5b,$87,$03,$2f
 DB $83,$01,$43,$01,$4f,$01,$5b,$87
 DB $03,$2f

ptn01:
 DB $83,$01,$43,$01,$4f,$01,$5b,$87
 DB $03,$2f,$83,$01,$43,$01,$4f,$01
 DB $5b,$87,$03,$2f,$ff

ptn02:
 DB $83,$02,$13,$03,$13,$03,$1f,$03
 DB $1f,$03,$13,$03,$13,$03,$1f,$03
 DB $1f,$ff

ptn1d:
 DB $03,$15,$03,$15,$03,$1f,$03,$21
 DB $03,$15,$03,$15,$03,$1f,$03,$21
 DB $ff

ptn1e:
 DB $03,$1a,$03,$1a,$03,$1c,$03,$1c
 DB $03,$1d,$03,$1d,$03,$1e,$03,$1e
 DB $ff

ptn1f:
 DB $03,$1a,$03,$1a,$03,$24,$03,$26
 DB $03,$13,$03,$13,$07,$1f,$ff

ptn04:
 DB $03,$18,$03,$18,$03,$24,$03,$24
 DB $03,$18,$03,$18,$03,$24,$03,$24
 DB $03,$20,$03,$20,$03,$2c,$03,$2c
 DB $03,$20,$03,$20,$03,$2c,$03,$2c
 DB $ff

ptn20:
 DB $03,$19,$03,$19,$03
 DB $25,$03,$25,$03,$19,$03,$19,$03
 DB $25,$03,$25,$03,$21,$03,$21,$03
 DB $2d,$03,$2d,$03,$21,$03,$21,$03
 DB $2d,$03,$2d,$ff

ptn06:
 DB $03,$1a,$03,$1a
 DB $03,$26,$03,$26,$03,$1a,$03,$1a
 DB $03,$26,$03,$26,$03,$15,$03,$15
 DB $03,$21,$03,$21,$03,$15,$03,$15
 DB $03,$21,$03,$21,$03,$18,$03,$18
 DB $03,$24,$03,$24,$03,$18,$03,$18
 DB $03,$24,$03,$24,$03,$1f,$03,$1f
 DB $03,$2b,$03,$2b,$03,$1f,$03,$1f
 DB $03,$2b,$03,$2b,$03,$1a,$03,$1a
 DB $03,$26,$03,$26,$03,$1a,$03,$1a
 DB $03,$26,$03,$26,$03,$15,$03,$15
 DB $03,$21,$03,$21,$03,$15,$03,$15
 DB $03,$21,$03,$21,$03,$18,$03,$18
 DB $03,$24,$03,$24,$03,$18,$03,$18
 DB $03,$24,$03,$24,$03,$1c,$03,$1c
 DB $03,$28,$03,$28,$03,$1c,$03,$1c
 DB $03,$28,$03,$28

ptn3b:
 DB $83,$04,$36,$07
 DB $36,$07,$37,$07,$36,$03,$33,$07
 DB $32,$57,$ff

ptn08:
 DB $83,$02,$1b,$03,$1b,$03,$27,$03
 DB $27,$03,$1b,$03,$1b,$03,$27,$03
 DB $27,$ff

ptn21:
 DB $03,$1c,$03,$1c,$03,$28,$03,$28
 DB $03,$1c,$03,$1c,$03,$28,$03,$28
 DB $ff

ptn22:
 DB $03,$1d,$03,$1d,$03,$29,$03,$29
 DB $03,$1d,$03,$1d,$03,$29,$03,$29
 DB $ff

ptn23:
 DB $03,$18,$03,$18,$03,$24,$03,$24
 DB $03,$18,$03,$18,$03,$24,$03,$24
 DB $ff

ptn24:
 DB $03,$1e,$03,$1e,$03,$2a,$03,$2a
 DB $03,$1e,$03,$1e,$03,$2a,$03,$2a
 DB $ff

ptn25:
 DB $83,$05,$26,$01,$4a,$01,$34,$03
 DB $29,$03,$4c,$03,$4a,$03,$31,$03
 DB $4a,$03,$24,$03,$22,$01,$46,$01
 DB $30,$03,$25,$03,$48,$03,$46,$03
 DB $2d,$03,$46,$03,$24,$ff

ptn0b:
 DB $83,$02,$1a,$03,$1a,$03,$26,$03
 DB $26,$03,$1a,$03,$1a,$03,$26,$03
 DB $26,$ff

ptn0c:
 DB $03,$13,$03,$13,$03,$1d,$03,$1f
 DB $03,$13,$03,$13,$03,$1d,$03,$1f
 DB $ff

ptn26:
 DB $87,$02,$1a,$87,$03,$2f,$83,$02
 DB $26,$03,$26,$87,$03,$2f,$ff

ptn10:
 DB $07,$1a,$4f,$47,$ff

ptn0e:
 DB $03,$1f,$03,$1f,$03,$24,$03,$26
 DB $07,$13,$47,$ff

ptn30:
 DB $bf,$0f,$32,$0f,$32,$8f,$90,$30
 DB $3f,$32,$13,$32,$03,$32,$03,$35
 DB $03,$37,$3f,$37,$0f,$37,$8f,$90
 DB $30,$3f,$32,$13,$32,$03,$2d,$03
 DB $30,$03,$32,$ff

ptn31:
 DB $0f,$32
 DB $af,$90,$35,$0f,$37,$a7,$99,$37
 DB $07,$35,$3f,$32,$13,$32,$03,$32
 DB $a3,$e8,$35,$03,$37,$0f,$35,$af
 DB $90,$37,$0f,$37,$a7,$99,$37,$07
 DB $35,$3f,$32,$13,$32,$03,$2d,$a3
 DB $e8,$30,$03,$32,$ff

ptn32:
 DB $07,$32,$03
 DB $39,$13,$3c,$a7,$9a,$37,$a7,$9b
 DB $38,$07,$37,$03,$35,$03,$32,$03
 DB $39,$1b,$3c,$a7,$9a,$37,$a7,$9b
 DB $38,$07,$37,$03,$35,$03,$32,$03
 DB $39,$03,$3c,$03,$3e,$03,$3c,$07
 DB $3e,$03,$3c,$03,$39,$a7,$9a,$37
 DB $a7,$9b,$38,$07,$37,$03,$35,$03
 DB $32,$af,$90,$3c,$1f,$3e,$43,$03
 DB $3e,$03,$3c,$03,$3e,$ff

ptn33:
 DB $03,$3e
 DB $03,$3e,$a3,$e8,$3c,$03,$3e,$03
 DB $3e,$03,$3e,$a3,$e8,$3c,$03,$3e
 DB $03,$3e,$03,$3e,$a3,$e8,$3c,$03
 DB $3e,$03,$3e,$03,$3e,$a3,$e8,$3c
 DB $03,$3e,$af,$91,$43,$1f,$41,$43
 DB $03,$3e,$03,$41,$03,$43,$03,$43
 DB $03,$43,$a3,$e8,$41,$03,$43,$03
 DB $43,$03,$43,$a3,$e8,$41,$03,$43
 DB $03,$45,$03,$48,$a3,$fd,$45,$03
 DB $44,$01,$43,$01,$41,$03,$3e,$03
 DB $3c,$03,$3e,$2f,$3e,$bf,$98,$3e
 DB $43,$03,$3e,$03,$3c,$03,$3e,$ff

ptn34:
 DB $03,$4a,$03,$4a,$a3,$f8,$48,$03
 DB $4a,$03,$4a,$03,$4a,$a3,$f8,$48
 DB $03,$4a,$ff

ptn35:
 DB $01,$51,$01,$54,$01
 DB $51,$01,$54,$01,$51,$01,$54,$01
 DB $51,$01,$54,$01,$51,$01,$54,$01
 DB $51,$01,$54,$01,$51,$01,$54,$01
 DB $51,$01,$54,$ff

ptn36:
 DB $01,$50,$01,$4f
 DB $01,$4d,$01,$4a,$01,$4f,$01,$4d
 DB $01,$4a,$01,$48,$01,$4a,$01,$48
 DB $01,$45,$01,$43,$01,$44,$01,$43
 DB $01,$41,$01,$3e,$01,$43,$01,$41
 DB $01,$3e,$01,$3c,$01,$3e,$01,$3c
 DB $01,$39,$01,$37,$01,$38,$01,$37
 DB $01,$35,$01,$32,$01,$37,$01,$35
 DB $01,$32,$01,$30,$ff

ptn37:
 DB $5f,$5f,$5f
 DB $47,$83,$0e,$32,$07,$32,$07,$2f
 DB $03,$2f,$07,$2f,$97,$0b,$3a,$5f
 DB $5f,$47,$8b,$0e,$32,$03,$32,$03
 DB $2f,$03,$2f,$47,$97,$0b,$3a,$5f
 DB $5f,$47,$83,$0e,$2f,$0b,$2f,$03
 DB $2f,$03,$2f,$87,$0b,$30,$17,$3a
 DB $5f,$8b,$0e,$32,$0b,$32,$0b,$2f
 DB $0b,$2f,$07,$2c,$07,$2c,$ff

ptn38:
 DB $87
 DB $0b,$34,$17,$3a,$5f,$5f,$84,$0e
 DB $32,$04,$32,$05,$32,$04,$2f,$04
 DB $2f,$05,$2f,$47,$97,$0b,$3a,$5f
 DB $5f,$84,$0e,$32,$04,$32,$05,$32
 DB $04,$2f,$04,$2f,$05,$2f,$ff

ptn2f:
 DB $03,$1a,$03,$1a,$03
 DB $24,$03,$26,$03,$1a,$03,$1a,$03
 DB $18,$03,$19,$03,$1a,$03,$1a,$03
 DB $24,$03,$26,$03,$1a,$03,$1a,$03
 DB $18,$03,$19,$03,$18,$03,$18,$03
 DB $22,$03,$24,$03,$18,$03,$18,$03
 DB $16,$03,$17,$03,$18,$03,$18,$03
 DB $22,$03,$24,$03,$18,$03,$18,$03
 DB $16,$03,$17,$03,$13,$03,$13,$03
 DB $1d,$03,$1f,$03,$13,$03,$13,$03
 DB $1d,$03,$1e,$03,$13,$03,$13,$03
 DB $1d,$03,$1f,$03,$13,$03,$13,$03
 DB $1d,$03,$1e,$03,$1a,$03,$1a,$03
 DB $24,$03,$26,$03,$1a,$03,$1a,$03
 DB $18,$03,$19,$03,$1a,$03,$1a,$03
 DB $24,$03,$26,$03,$1a,$03,$1a,$03
 DB $18,$03,$19,$ff


;====================================
;instruments
;====================================

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
 DB $00,$02,$41,$09,$90,$02,$00,$00
 DB $00,$00,$11,$0a,$fa,$00,$00,$05
 DB $00,$08,$41,$37,$40,$02,$00,$00
 DB $00,$08,$11,$07,$70,$02,$00,$00

hex:
        DB '0','1','2','3','4','5','6','7','8','9'
        DB 'a','b','c','d','e','f'        