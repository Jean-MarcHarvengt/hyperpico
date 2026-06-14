; =============================================================================
; MONTY ON THE RUN - Z80 ENGINE CONVERSION
; =============================================================================

; Audio
REG_SIDBASE:            equ $fb18
;REG_SIDBASE:            equ $3c00

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
        LD C, 0         ; Y register replacement

        ; Multiply A by 6 (asl, sta temp, asl, clc, adc temp)
        ADD A, A        ; A * 2
        LD (tempstore), A
        ADD A, A        ; A * 4
        LD B, A
        LD A, (tempstore)
        ADD A, B        ; A = Music num * 6
        
        LD E, A
        LD D, 0         ; DE = offset index X
        LD HL, songs
        ADD HL, DE      ; HL points to songs + X

init_loop:
        LD A, (HL)
        
        ; Copy pointers to current tracking variables
        ; We store currtrkhi and currtrklo sequentially in Z80 RAM
        LD DE, currtrkhi
        LD B, 0
        LD A, C
        ADD A, E
        LD E, A
        JR NC, l1
        INC D
l1:
        LD A, (HL)
        LD (DE), A

        INC HL          ; inx simulation
        INC C           ; iny simulation
        LD A, C
        CP 6
        JR NZ, init_loop

        ; Clear control registers
        XOR A           ; LD A, 0
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
        DEC B           ; Adjust 0-indexed offset loop
        LD C, B
        LD B, 0
        
        LD HL, regoffsets
        ADD HL, BC
        LD A, (HL)
        LD (tmpregofst), A
        LD E, A         ; E holds template register offset (Y tracking equivalent)

        ; Check whether a new note is needed
        LD A, (speed)
        LD HL, resetspd
        CP (HL)
        JR Z, checknewnote
        JP vibrato

checknewnote:
        ; Put base address word of this track in ptr_02
        LD HL, currtrkhi
        ADD HL, BC
        LD A, (HL)
        LD (ptr_02 + 1), A
        
        LD HL, currtrklo
        ADD HL, BC
        LD A, (HL)
        LD (ptr_02), A

        LD HL, lengthleft
        ADD HL, BC
        LD A, (HL)
        DEC A
        LD (HL), A
        JP M, getnewnote

        JP soundwork    ; No new note needed


;==========
; notework
getnewnote:
        LD HL, posoffset
        ADD HL, BC
        LD A, (HL)
        
        ; Indirect lookup: A = (ptr_02) + offset A
        LD DE, (ptr_02)
        ADD A, E
        LD E, A
        JR NC, l3
        INC D
l3:     LD A, (DE)      ; Load from current positions pointer

        CP $FF
        JR Z, restart
        CP $FE
        JR NZ, getnotedata
        JP musicend

restart:
        XOR A
        LD HL, lengthleft
        ADD HL, BC
        LD (HL), A
        LD HL, posoffset
        ADD HL, BC
        LD (HL), A
        LD HL, patoffset
        ADD HL, BC
        LD (HL), A
        JP getnewnote

getnotedata:
        ; Locate pattern base pointer address word into ptr_04
        LD E, A
        LD D, 0
        LD HL, patptl
        ADD HL, DE
        LD A, (HL)
        LD (ptr_04), A
        
        LD HL, patpth
        ADD HL, DE
        LD A, (HL)
        LD (ptr_04 + 1), A

        XOR A
        LD HL, portaval
        ADD HL, BC
        LD (HL), A      ; Default no portamento

        LD HL, patoffset
        ADD HL, BC
        LD A, (HL)
        LD E, A         ; Pattern offset value

        LD A, $FF
        LD (appendfl), A

        ; Fetch 1st byte: length/command settings
        LD HL, (ptr_04)
        ADD A, E        ; Add current offset
        LD E, A
        JR NC, l4
        INC D
l4:     LD A, (DE)      ; Fetch byte
        
        LD HL, savelnthcc
        ADD HL, BC
        LD (HL), A
        LD (templnthcc), A
        AND $1F
        LD HL, lengthleft
        ADD HL, BC
        LD (HL), A

        LD A, (templnthcc)
        AND $40         ; Check Overflow flag replacement via Bit 6 test
        JR NZ, appendnote

        LD HL, patoffset
        ADD HL, BC
        INC (HL)        ; Increment offset to next data byte

        LD A, (templnthcc)
        AND A           ; Check Sign flag replacement via Bit 7 test
        JP P, getpitch

        ; 2nd byte evaluation
        LD A, (patoffset) ; update index
        LD E, A
        LD D, 0
        LD HL, (ptr_04)
        ADD HL, DE
        LD A, (HL)
        AND A
        JP P, l5

        LD HL, portaval
        ADD HL, BC
        LD (HL), A      ; Save portamento val
        JR l6

l5:     LD HL, instrnr
        ADD HL, BC
        LD (HL), A      ; Save instrument number

l6:     LD HL, patoffset
        ADD HL, BC
        INC (HL)

getpitch:
        LD A, (patoffset)
        LD E, A
        LD D, 0
        LD HL, (ptr_04)
        ADD HL, DE
        LD A, (HL)      ; 3rd byte: pitch tracking
        
        LD HL, notenum
        ADD HL, BC
        LD (HL), A
        ADD A, A        ; Pitch * 2
        
        LD E, A
        LD D, 0
        LD HL, frequenzlo
        ADD HL, DE
        LD A, (HL)
        LD (tempfreq), A
        
        LD HL, frequenzhi
        ADD HL, DE
        LD A, (HL)
        
        LD DE, (tmpregofst)
        LD D, 0
        LD HL, REG_SIDBASE + $01
        ADD HL, DE
        LD (HL), A      ; Save high frequency to chip
        
        LD HL, savefreqhi
        ADD HL, BC
        LD (HL), A
        
        LD A, (tempfreq)
        LD HL, REG_SIDBASE + $00
        ADD HL, DE
        LD (HL), A      ; Save low frequency to chip
        
        LD HL, savefreqlo
        ADD HL, BC
        LD (HL), A
        JR l7

appendnote:
        LD HL, appendfl
        DEC (HL)

l7:     LD DE, (tmpregofst)
        LD D, 0
        LD HL, instrnr
        ADD HL, BC
        LD A, (HL)      ; Instrument number
        
        LD (tempstore), BC ; Save channel register footprint index 
        ADD A, A        ; Instr num * 8 calculation
        ADD A, A
        ADD A, A
        LD E, A
        LD D, 0
        
        LD HL, instr + 2
        ADD HL, DE
        LD A, (HL)
        LD (tempctrl), A
        
        LD A, (appendfl)
        AND (HL)
        LD IX, REG_SIDBASE + $04
        ADD IX, DE
        LD (IX + 0), A

        LD HL, instr + 0
        ADD HL, DE
        LD A, (HL)
        LD IX, REG_SIDBASE + $02
        ADD IX, DE
        LD (IX + 0), A

        LD HL, instr + 1
        ADD HL, DE
        LD A, (HL)
        LD IX, REG_SIDBASE + $03
        ADD IX, DE
        LD (IX + 0), A

        LD HL, instr + 3
        ADD HL, DE
        LD A, (HL)
        LD IX, REG_SIDBASE + $05
        ADD IX, DE
        LD (IX + 0), A

        LD HL, instr + 4
        ADD HL, DE
        LD A, (HL)
        LD IX, REG_SIDBASE + $06
        ADD IX, DE
        LD (IX + 0), A

        LD BC, (tempstore) ; Restore loop registers
        LD A, (tempctrl)
        LD HL, voicectrl
        ADD HL, BC
        LD (HL), A

        LD HL, patoffset
        ADD HL, BC
        INC (HL)
        LD A, (HL)
        LD E, A
        LD D, 0
        LD HL, (ptr_04)
        ADD HL, DE
        LD A, (HL)

        CP $FF          ; Check for end of pattern
        JR NZ, l8

        XOR A
        LD HL, patoffset
        ADD HL, BC
        LD (HL), A
        LD HL, posoffset
        ADD HL, BC
        INC (HL)

l8:     JP loopcont


;==========
; soundwork
soundwork:
        LD DE, (tmpregofst)
        LD D, 0
        
        LD HL, savelnthcc
        ADD HL, BC
        LD A, (HL)
        AND $20
        JR NZ, vibrato

        LD HL, lengthleft
        ADD HL, BC
        LD A, (HL)
        AND A
        JR NZ, vibrato

        LD HL, voicectrl
        ADD HL, BC
        LD A, (HL)
        AND $FE         ; Kill gate / start release
        LD IX, REG_SIDBASE + $04
        ADD IX, DE
        LD (IX + 0), A
        
        XOR A
        LD IX, REG_SIDBASE + $05
        ADD IX, DE
        LD (IX + 0), A
        LD IX, REG_SIDBASE + $06
        ADD IX, DE
        LD (IX + 0), A


;==========
; vibrato routine
vibrato:
        LD HL, instrnr
        ADD HL, BC
        LD A, (HL)
        ADD A, A        ; Instrument * 8
        ADD A, A
        ADD A, A
        LD (instnumby8), A
        
        LD E, A
        LD D, 0
        LD HL, instr + 7
        ADD HL, DE
        LD A, (HL)
        LD (instrfx), A

        LD HL, instr + 6
        ADD HL, DE
        LD A, (HL)
        LD (pulsevalue), A

        LD HL, instr + 5
        ADD HL, DE
        LD A, (HL)
        LD (vibrdepth), A
        JP Z, pulsework

        LD A, (counter)
        AND 7
        CP 4
        JR C, l9
        XOR 7
l9:     LD (oscilatval), A

        LD HL, notenum
        ADD HL, BC
        LD A, (HL)
        ADD A, A
        LD E, A
        LD D, 0
        
        LD HL, frequenzlo + 2
        ADD HL, DE
        LD A, (HL)
        LD HL, frequenzlo
        ADD HL, DE
        SUB (HL)
        LD (tmpvdiflo), A
        
        LD HL, frequenzhi + 2
        ADD HL, DE
        LD A, (HL)
        LD HL, frequenzhi
        ADD HL, DE
        SBC A, (HL)

l10:    SRL A           ; Divide delta values by 2 per depth unit
        LD H, A
        LD A, (tmpvdiflo)
        RRA
        LD (tmpvdiflo), A
        LD A, H
        LD HL, vibrdepth
        DEC (HL)
        JP P, l10
        LD (tmpvdifhi), A

        LD HL, frequenzlo
        ADD HL, DE
        LD A, (HL)
        LD (tmpvfrqlo), A
        LD HL, frequenzhi
        ADD HL, DE
        LD A, (HL)
        LD (tmpvfrqhi), A

        LD HL, savelnthcc
        ADD HL, BC
        LD A, (HL)
        AND $1F
        CP 8
        JR C, l12

        LD A, (oscilatval)
        LD E, A

l11:    DEC E
        JP M, l12
        LD A, (tmpvfrqlo)
        LD HL, tmpvdiflo
        ADD A, (HL)
        LD (tmpvfrqlo), A
        LD A, (tmpvfrqhi)
        LD HL, tmpvdifhi
        ADC A, (HL)
        LD (tmpvfrqhi), A
        JR l11

l12:    LD DE, (tmpregofst)
        LD D, 0
        LD A, (tmpvfrqlo)
        LD HL, REG_SIDBASE + $00
        ADD HL, DE
        LD (HL), A
        LD A, (tmpvfrqhi)
        LD HL, REG_SIDBASE + $01
        ADD HL, DE
        LD (HL), A


;==========
; pulse-width timbre routine
pulsework:
        LD A, (pulsevalue)
        AND A
        JP Z, portamento

        LD A, (instnumby8)
        LD E, A
        LD D, 0
        
        LD HL, pulsedelay
        ADD HL, BC
        DEC (HL)
        JP P, portamento

        LD A, (pulsevalue)
        AND $1F
        LD (HL), A      ; Reset delay

        LD A, (pulsevalue)
        AND $E0
        LD (pulsespeed), A

        LD HL, pulsedir
        ADD HL, BC
        LD A, (HL)
        AND A
        JR NZ, pulsedown

        LD A, (pulsespeed)
        LD IX, instr + 0
        ADD IX, DE
        ADD A, (IX + 0)
        LD H, A         ; Temporary push simulation registers
        LD A, 0
        LD IX, instr + 1
        ADD IX, DE
        ADC A, (IX + 0)
        AND $0F
        LD L, A
        CP $0E
        JR NZ, dumpulse
        LD HL, pulsedir
        ADD HL, BC
        INC (HL)
        JR dumpulse

pulsedown:
        LD IX, instr + 0
        ADD IX, DE
        LD A, (IX + 0)
        LD HL, pulsespeed
        SUB (HL)
        LD H, A
        LD IX, instr + 1
        ADD IX, DE
        LD A, (IX + 0)
        SBC A, 0
        AND $0F
        LD L, A
        CP $08
        JR NZ, dumpulse
        LD HL, pulsedir
        ADD HL, BC
        DEC (HL)

dumpulse:
        LD (tempstore), BC
        LD DE, (tmpregofst)
        LD D, 0
        
        LD A, L
        LD IX, instr + 1
        ADD IX, DE
        LD (IX + 0), A
        LD HL, REG_SIDBASE + $03
        ADD HL, DE
        LD (HL), A
        
        LD A, H
        LD IX, instr + 0
        ADD IX, DE
        LD (IX + 0), A
        LD HL, REG_SIDBASE + $02
        ADD HL, DE
        LD (HL), A
        
        LD BC, (tempstore)


;==========
; portamento routine
portamento:
        LD DE, (tmpregofst)
        LD D, 0
        LD HL, portaval
        ADD HL, BC
        LD A, (HL)
        AND A
        JR Z, drums

        AND $7E
        LD (tempstore), A

        LD HL, portaval
        ADD HL, BC
        LD A, (HL)
        AND $01
        JR Z, portup

        ; Portamento down
        LD HL, savefreqlo
        ADD HL, BC
        LD A, (HL)
        LD HL, tempstore
        SUB (HL)
        LD HL, savefreqlo
        ADD HL, BC
        LD (HL), A
        LD HL, REG_SIDBASE + $00
        ADD HL, DE
        LD (HL), A
        
        LD HL, savefreqhi
        ADD HL, BC
        LD A, (HL)
        SBC A, 0
        LD HL, savefreqhi
        ADD HL, BC
        LD (HL), A
        LD HL, REG_SIDBASE + $01
        ADD HL, DE
        LD (HL), A
        JR drums

portup:
        LD HL, savefreqlo
        ADD HL, BC
        LD A, (HL)
        LD HL, tempstore
        ADD A, (HL)
        LD HL, savefreqlo
        ADD HL, BC
        LD (HL), A
        LD HL, REG_SIDBASE + $00
        ADD HL, DE
        LD (HL), A
        
        LD HL, savefreqhi
        ADD HL, BC
        LD A, (HL)
        ADC A, 0
        LD HL, savefreqhi
        ADD HL, BC
        LD (HL), A
        LD HL, REG_SIDBASE + $01
        ADD HL, DE
        LD (HL), A


;==========
; drum routines
drums:
        LD A, (instrfx)
        AND $01
        JR Z, skydive

        LD HL, savefreqhi
        ADD HL, BC
        LD A, (HL)
        AND A
        JR Z, skydive

        LD HL, lengthleft
        ADD HL, BC
        LD A, (HL)
        AND A
        JR Z, skydive

        LD HL, savelnthcc
        ADD HL, BC
        LD A, (HL)
        AND $1F
        DEC A
        LD HL, lengthleft
        ADD HL, BC
        CP (HL)
        LD DE, (tmpregofst)
        LD D, 0
        JR C, firstime

        LD HL, savefreqhi
        ADD HL, BC
        DEC (HL)
        LD A, (HL)
        LD HL, REG_SIDBASE + $01
        ADD HL, DE
        LD (HL), A

        LD HL, voicectrl
        ADD HL, BC
        LD A, (HL)
        AND $FE
        JR NZ, dumpctrl

firstime:
        LD HL, savefreqhi
        ADD HL, BC
        LD A, (HL)
        LD HL, REG_SIDBASE + $01
        ADD HL, DE
        LD (HL), A
        LD A, $80

dumpctrl:
        LD HL, REG_SIDBASE + $04
        ADD HL, DE
        LD (HL), A


;==========
; skydive
skydive:
        LD A, (instrfx)
        AND $02
        JR Z, octarp

        LD A, (counter)
        AND $01
        JR Z, octarp

        LD HL, savefreqhi
        ADD HL, BC
        LD A, (HL)
        AND A
        JR Z, octarp

        DEC (HL)
        LD A, (HL)
        LD DE, (tmpregofst)
        LD D, 0
        LD HL, REG_SIDBASE + $01
        ADD HL, DE
        LD (HL), A


;==========
; octave arpeggio
octarp:
        LD A, (instrfx)
        AND $04
        JR Z, loopcont

        LD A, (counter)
        AND $01
        JR Z, l13

        LD HL, notenum
        ADD HL, BC
        LD A, (HL)
        ADD A, $0C
        JR l14

l13:    LD HL, notenum
        ADD HL, BC
        LD A, (HL)

l14:    ADD A, A
        LD E, A
        LD D, 0
        LD HL, frequenzlo
        ADD HL, DE
        LD A, (HL)
        LD (tempfreq), A
        
        LD HL, frequenzhi
        ADD HL, DE
        LD A, (HL)
        LD DE, (tmpregofst)
        LD D, 0
        LD HL, REG_SIDBASE + $01
        ADD HL, DE
        LD (HL), A
        
        LD A, (tempfreq)
        LD HL, REG_SIDBASE + $00
        ADD HL, DE
        LD (HL), A


;==========
; loop check
loopcont:
        LD A, C
        AND A           ; Check if inner channel index reached 0
        JP M, musicend
        LD B, C
        JP mainloop

musicend:
        RET


;====================================================================
; DATA STORAGE MAPPED TO RAM (Zero Page Safe Variables Alternatives)
;====================================================================
ptr_02:     DEFW 0      ; Replacement for 6502 zero page $02/$03
ptr_04:     DEFW 0      ; Replacement for 6502 zero page $04/$05

regoffsets: DB $00, $07, $0E
tmpregofst: DB $00
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
tempstore:  DEFW 0      ; Allocated as word to backup BC pairs securely
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
        DB      montymaintr1/256, montymaintr2/256, montymaintr3/256
        DB      montymaintr1, montymaintr2, montymaintr3

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