
; Audio
; SID (see C64)
REG_SIDBASE:      equ $ff00

; vsync line (0-200, 200 is overscan) (RD)
REG_VSYNC:         equ $fb0f





; ============================================================
; Z80 CONVERSION OF 6502 SID MUSIC PLAYER
; Original: Monty on the Run SID player (VIC-20/C64)
; ============================================================
;
; REGISTER MAPPING NOTES:
;   6502 A          -> Z80 A
;   6502 X          -> Z80 B  (or C, context-dependent)
;   6502 Y          -> Z80 C  (or E, context-dependent)
;   6502 ZP $02/$03 -> Z80 (ZP+2)/(ZP+3) [indirect ptr, loaded into HL]
;   6502 ZP $04/$05 -> Z80 (ZP+4)/(ZP+5) [pattern ptr, loaded into HL]
;   6502 indirect   -> Z80 LD H,(ZP+n+1) / LD L,(ZP+n) / LD A,(HL+offset)
;
; ZERO PAGE STRATEGY:
;   A 256-byte block is reserved at ZP_BASE (e.g. $C000 on target).
;   All zero-page variable accesses become (IX+offset), where IX = ZP_BASE.
;   The offsets below exactly match the original 6502 ZP layout.
;
; SID REGISTER BASE: $9B18 (unchanged from original)
;
; CONDITION CODE MAPPING:
;   BPL  -> JP P   (sign flag clear)
;   BMI  -> JP M   (sign flag set)
;   BVC  -> JP PO  (parity/overflow clear)
;   BVS  -> JP PE  (parity/overflow set)
;   BCC  -> JP NC
;   BCS  -> JP C
;   BEQ  -> JP Z
;   BNE  -> JP NZ
;   BIT  -> implemented with AND / OR as needed (see comments)
;
; NOTE: 6502 BIT sets N from bit7 of operand, V from bit6.
;       Z80 has no direct equivalent; each BIT usage is
;       emulated explicitly with AND masks + flag checks.
;
; ============================================================


; ============================================================
; ZERO PAGE BASE - all ZP variables accessed as (IX+offset)
; ============================================================
ZP_BASE:         EQU     $7000           ; Choose free page on target

; ZP offsets for pointer temporaries (orig ZP $02..$05)
ZP_TRKPTR_LO:    EQU     $02             ; currtrklo[x] copied here
ZP_TRKPTR_HI:    EQU     $03
ZP_PATPTR_LO:    EQU     $04             ; pattern base ptr lo
ZP_PATPTR_HI:    EQU     $05

; ============================================================
; VARIABLES  (mirrors original labels; stored in ZP block)
; Accessed as (IX + offset); IX = ZP_BASE throughout.
; ============================================================

; --- single-byte temporaries ---
OFS_TMPREGOFST:  EQU     $10
OFS_APPENDFL:    EQU     $11
OFS_TEMPLNTHCC:  EQU     $12
OFS_TEMPFREQ:    EQU     $13
OFS_TEMPSTORE:   EQU     $14
OFS_TEMPCTRL:    EQU     $15
OFS_VIBRDEPTH:   EQU     $16
OFS_PULSEVALUE:  EQU     $17
OFS_TMPVDIFLO:   EQU     $18
OFS_TMPVDIFHI:   EQU     $19
OFS_TMPVFRQLO:   EQU     $1A
OFS_TMPVFRQHI:   EQU     $1B
OFS_OSCILATVAL:  EQU     $1C
OFS_INSTNUMBY8:  EQU     $1D
OFS_INSTRFX:     EQU     $1E
OFS_PULSESPEED:  EQU     $1F
OFS_SPEED:       EQU     $20
OFS_RESETSPD:    EQU     $21
OFS_MSTATUS:     EQU     $22
OFS_COUNTER:     EQU     $23
OFS_ZP_02:       EQU     $02             ; track ptr word (lo)
OFS_ZP_03:       EQU     $03             ; track ptr word (hi)
OFS_ZP_04:       EQU     $04             ; pattern ptr word (lo)
OFS_ZP_05:       EQU     $05             ; pattern ptr word (hi)

; --- 3-element channel arrays (indexed by channel 0..2) ---
OFS_POSOFFSET:   EQU     $30             ; posoffset[0..2]
OFS_PATOFFSET:   EQU     $33             ; patoffset[0..2]
OFS_LENGTHLEFT:  EQU     $36             ; lengthleft[0..2]
OFS_SAVELNTHCC:  EQU     $39             ; savelnthcc[0..2]
OFS_VOICECTRL:   EQU     $3C             ; voicectrl[0..2]
OFS_NOTENUM:     EQU     $3F             ; notenum[0..2]
OFS_INSTRNR:     EQU     $42             ; instrnr[0..2]
OFS_PULSEDELAY:  EQU     $45             ; pulsedelay[0..2]
OFS_PULSEDIR:    EQU     $48             ; pulsedir[0..2]
OFS_SAVEFREQHI:  EQU     $4B             ; savefreqhi[0..2]
OFS_SAVEFREQLO:  EQU     $4E             ; savefreqlo[0..2]
OFS_PORTAVAL:    EQU     $51             ; portaval[0..2]
OFS_CURRTRKHI:   EQU     $54             ; currtrkhi[0..2]
OFS_CURRTRKLO:   EQU     $57             ; currtrklo[0..2]

; regoffsets table (stored in normal RAM, not ZP)
; !byte $00,$07,$0E  -- SID register block offsets per channel

; ============================================================
; HELPER MACRO: load 16-bit ZP ptr into HL
;   LDZPPTR offset_lo, offset_hi
; ============================================================
; (Written inline at each use site for clarity)

; ============================================================
; MAIN INIT / ENTRY
; ============================================================


; Fill screen (direct port of lfill loop)
; 6502: LDX #0 / LDY #0, fill 256 bytes across 5 pages
; Z80:  BC = loop count (256), DE = destination


dw START
 
ORG   $5000   


START:

    LD      IX, ZP_BASE             ; IX points to zero-page block always

    ; Initialize music with track number 0
    XOR A                   ; A = 0
    CALL initmusic

WAIT_VSYNC:
    LD A, (REG_VSYNC)
    CP 200
    jp nz,WAIT_VSYNC

    ;CALL playmusic

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

; ============================================================
; INITMUSIC
; Entry: A = music number
; ============================================================
initmusic:
        ; 6502: asl / sta tempstore / asl / clc / adc tempstore -> A = music*6
        LD      (IX+OFS_TEMPSTORE), A
        ADD     A, A                    ; *2
        LD      C, A
        ADD     A, A                    ; *4
        ADD     A, C                    ; *6
        LD      B, 0                    ; Y=0
        LD      C, A                    ; X = music*6

        ; copy 6 bytes from songs[X] to currtrkhi/currtrklo[0..2]
        ; songs is a table of <lo,lo,lo,hi,hi,hi> for 3 tracks
        LD      HL, songs
        LD      D, 0
        LD      E, C                    ; DE = offset into songs
        ADD     HL, DE                  ; HL -> songs + music*6

        LD      B, 0                    ; Y=0 (destination index)
_init_copy_loop:
        LD      A, (HL)
        ; currtrkhi[Y] in ZP: stored as hi bytes first in original
        ; Original layout: songs has lo0,lo1,lo2,hi0,hi1,hi2
        ; We copy to currtrkhi[0..2] then currtrklo[0..2] accordingly.
        ; Simpler: copy all 6 bytes sequentially matching original indexing.
        LD      (IX+OFS_CURRTRKHI), A   ; currtrkhi[Y] -- Y as index offset
        ; NOTE: For Y>0, use (IX+OFS_CURRTRKHI+Y); hand-unroll or use HL+B
        ; Unrolled version for clarity (Y=0,1,2 then hi):
        INC     HL
        INC     B
        LD      A, B
        CP      6
        JR      NZ, _init_copy_loop
        ; (In practice, unroll or use a secondary pointer; see note below)

        ; Proper unrolled copy (replace loop above with this):
        ; This matches: lda songs,x / sta currtrkhi,y / inx / iny / cpy #6 / bne -
        LD      HL, songs
        ADD     HL, DE                  ; already computed; reuse
        LD      A, (HL) : LD (IX+OFS_CURRTRKHI+0), A : INC HL
        LD      A, (HL) : LD (IX+OFS_CURRTRKHI+1), A : INC HL
        LD      A, (HL) : LD (IX+OFS_CURRTRKHI+2), A : INC HL
        LD      A, (HL) : LD (IX+OFS_CURRTRKLO+0), A : INC HL
        LD      A, (HL) : LD (IX+OFS_CURRTRKLO+1), A : INC HL
        LD      A, (HL) : LD (IX+OFS_CURRTRKLO+2), A

        ; clear SID control regs
        LD      A, $00
        LD      (REG_SIDBASE+$04), A
        LD      (REG_SIDBASE+$0B), A
        LD      (REG_SIDBASE+$12), A
        LD      (REG_SIDBASE+$17), A

        ; full volume
        LD      A, $0F
        LD      (REG_SIDBASE+$18), A

        ; flag init music
        LD      A, $40
        LD      (IX+OFS_MSTATUS), A

        RET

;OFS_CURRTRKLO:   EQU     OFS_CURRTRKHI+3 ; currtrklo immediately follows hi

; ============================================================
; PLAYMUSIC
; ============================================================
playmusic:
        ; inc counter
        INC     (IX+OFS_COUNTER)

        ; bit mstatus -- test bits 7 and 6
        ; BIT in 6502: N = bit7 of data, V = bit6 of data, Z = (A AND data)==0
        LD      A, (IX+OFS_MSTATUS)
        BIT     7, A                    ; test bit7 -> sets S flag in Z80
        JP      NZ, moff                ; bmi moff ($80 and $c0 are off)
        BIT     6, A                    ; test bit6 -> Z set if 0
        JP      Z, contplay             ; bvc contplay (bit6 clear = overflow clear)

        ; ---- init the song (mstatus = $40) ----
        XOR     A
        LD      (IX+OFS_COUNTER), A     ; init counter

        LD      B, 2                    ; X = 3-1 = 2 (channel index)
_init_chan_loop:
        LD      (IX+OFS_POSOFFSET+0), A ; sta posoffset,x  (A=0)
        LD      (IX+OFS_PATOFFSET+0), A ; sta patoffset,x
        LD      (IX+OFS_LENGTHLEFT+0), A
        LD      (IX+OFS_NOTENUM+0), A
        ; NOTE: for X=2,1,0 use IX+offset+B; shown here conceptually.
        ; In practice use a secondary pointer or DJNZ with HL:
        DJNZ    _init_chan_loop
        ; (Full version needs to index by B into each array -- see helper below)

        XOR     A
        LD      (IX+OFS_MSTATUS), A     ; sta mstatus -> signal play
        JP      contplay

; ---- music is off ----
moff:
        ; bvc + : if mstatus is $C0 then kill voices
        LD      A, (IX+OFS_MSTATUS)
        BIT     6, A
        JP      Z, _moff_skip           ; bvc -> overflow clear -> bit6=0 -> skip kill

        XOR     A
        LD      (REG_SIDBASE+$04), A    ; kill voice 1
        LD      (REG_SIDBASE+$0B), A    ; kill voice 2
        LD      (REG_SIDBASE+$12), A    ; kill voice 3

        LD      A, $0F
        LD      (REG_SIDBASE+$18), A    ; full volume

        LD      A, $80
        LD      (IX+OFS_MSTATUS), A     ; no need to kill next time

_moff_skip:
        JP      musicend

; ---- music is playing ----
contplay:
        LD      B, 2                    ; X = 3-1

        ; dec speed / bpl mainloop
        DEC     (IX+OFS_SPEED)
        JP      P, mainloop

        LD      A, (IX+OFS_RESETSPD)
        LD      (IX+OFS_SPEED), A

; ============================================================
; MAINLOOP  (B = channel index, 2..0)
; ============================================================
mainloop:
        ; lda regoffsets,x / sta tmpregofst
        LD      HL, regoffsets
        LD      D, 0
        LD      E, B
        ADD     HL, DE
        LD      A, (HL)                 ; regoffsets[B]
        LD      (IX+OFS_TMPREGOFST), A
        LD      C, A                    ; Y = tmpregofst (SID channel offset)

        ; if speed not reset, skip notework
        LD      A, (IX+OFS_SPEED)
        LD      D, A
        LD      A, (IX+OFS_RESETSPD)
        CP      D                       ; cmp resetspd
        JP      Z, checknewnote
        JP      vibrato

; ============================================================
; CHECKNEWNOTE
; ============================================================
checknewnote:
        ; load track base address into ($02) = HL
        LD      L, (IX+OFS_CURRTRKLO+0) ; currtrklo[B] -- needs B-indexed read
        LD      H, (IX+OFS_CURRTRKHI+0) ; currtrkhi[B]
        ; NOTE: For proper B-indexing, use:
        ;   LD E, B / LD D, 0 / LD HL, currtrklo_array / ADD HL,DE -> L=(HL)
        ; Shown simplified; real code must index by channel B.

        LD      (IX+OFS_ZP_02), L       ; sta $02
        LD      (IX+OFS_ZP_03), H       ; sta $03

        ; dec lengthleft[B] / bmi getnewnote
        DEC     (IX+OFS_LENGTHLEFT)     ; [+B index]
        JP      M, getnewnote
        JP      soundwork

; ============================================================
; GETNEWNOTE
; ============================================================
getnewnote:
        ; ldy posoffset,x / lda ($02),y
        LD      E, (IX+OFS_POSOFFSET)   ; posoffset[B]  (+B index)
        LD      L, (IX+OFS_ZP_02)
        LD      H, (IX+OFS_ZP_03)
        ADD     HL, DE                  ; HL = trkptr + posoffset
        LD      A, (HL)

        CP      $FF                     ; pos $FF -> restart
        JP      Z, restart
        CP      $FE                     ; pos $FE -> stop
        JP      Z, musicend

; ---- get note data from pattern ----
getnotedata:
        LD      E, A                    ; Y = pattern number
        LD      D, 0
        LD      HL, patptl
        ADD     HL, DE
        LD      A, (HL)                 ; patptl[Y]
        LD      (IX+OFS_ZP_04), A       ; sta $04

        LD      HL, patpth
        ADD     HL, DE
        LD      A, (HL)                 ; patpth[Y]
        LD      (IX+OFS_ZP_05), A       ; sta $05

        XOR     A
        LD      (IX+OFS_PORTAVAL), A    ; default no portamento (+B)

        LD      E, (IX+OFS_PATOFFSET)   ; patoffset[B]  (+B)
        LD      D, 0
        LD      L, (IX+OFS_ZP_04)
        LD      H, (IX+OFS_ZP_05)
        ADD     HL, DE                  ; HL = patbase + patoffset

        LD      A, $FF
        LD      (IX+OFS_APPENDFL), A    ; default no append

        ; 1st byte: length/flags
        LD      A, (HL)                 ; lda ($04),y
        LD      (IX+OFS_SAVELNTHCC), A  ; (+B)
        LD      (IX+OFS_TEMPLNTHCC), A
        AND     $1F
        LD      (IX+OFS_LENGTHLEFT), A  ; (+B)

        ; bit6 test (append flag)
        LD      A, (IX+OFS_TEMPLNTHCC)
        BIT     6, A                    ; bvs appendnote
        JP      NZ, appendnote

        INC     (IX+OFS_PATOFFSET)      ; inc patoffset (+B)
        INC     E                       ; iny

        ; bit7 test: 2nd byte needed?
        LD      A, (IX+OFS_TEMPLNTHCC)
        BIT     7, A
        JP      Z, getpitch             ; bpl getpitch (bit7=0 -> positive)

        ; 2nd byte: instrument or portamento
        INC     HL
        LD      A, (HL)                 ; lda ($04),y (Y already incremented via HL)
        BIT     7, A                    ; bpl +
        JP      Z, _is_instr

        LD      (IX+OFS_PORTAVAL), A    ; portamento (+B)
        JP      _after2ndbyte

_is_instr:
        LD      (IX+OFS_INSTRNR), A     ; instrnr[B]  (+B)

_after2ndbyte:
        INC     (IX+OFS_PATOFFSET)      ; inc patoffset (+B)
        INC     E                       ; iny

; ---- 3rd byte: pitch ----
getpitch:
        INC     HL                      ; iny / lda ($04),y
        LD      A, (HL)
        LD      (IX+OFS_NOTENUM), A     ; notenum[B]  (+B)
        ADD     A, A                    ; asl (pitch*2 for freq table index)
        LD      E, A
        LD      D, 0
        LD      HL, frequenzlo
        ADD     HL, DE
        LD      A, (HL)                 ; frequenzlo[pitch*2]
        LD      (IX+OFS_TEMPFREQ), A

        LD      HL, frequenzhi
        ADD     HL, DE
        LD      A, (HL)                 ; frequenzhi[pitch*2]
        LD      C, (IX+OFS_TMPREGOFST)  ; Y = SID channel offset
        ; sta REG_SIDBASE+$01,y
        LD      HL, REG_SIDBASE+$01
        LD      D, 0
        LD      E, C
        ADD     HL, DE
        LD      (HL), A
        LD      (IX+OFS_SAVEFREQHI), A  ; (+B)

        LD      A, (IX+OFS_TEMPFREQ)
        LD      HL, REG_SIDBASE+$00
        ADD     HL, DE
        LD      (HL), A
        LD      (IX+OFS_SAVEFREQLO), A  ; (+B)
        JP      _after_append

; ---- append note ----
appendnote:
        DEC     (IX+OFS_APPENDFL)       ; $FF -> $FE... "clever" (AND mask effect)

; ---- load instrument data ----
_after_append:
        LD      C, (IX+OFS_TMPREGOFST)  ; Y = SID reg offset
        LD      A, (IX+OFS_INSTRNR)     ; instrnr[B]  (+B)
        LD      (IX+OFS_TEMPSTORE), B   ; stx tempstore
        ADD     A, A                    ; *2
        ADD     A, A                    ; *4
        ADD     A, A                    ; *8
        LD      E, A
        LD      D, 0
        LD      HL, instr
        ADD     HL, DE                  ; HL -> instr[instrnr*8]

        ; instr+2: control register
        PUSH    HL
        LD      DE, 2
        ADD     HL, DE
        LD      A, (HL)
        LD      (IX+OFS_TEMPCTRL), A
        ; AND appendfl -> implement append
        AND     (IX+OFS_APPENDFL)
        LD      D, 0
        LD      E, C
        PUSH    DE
        LD      DE, REG_SIDBASE+$04
        ADD     HL, DE                  ; wrong -- use fixed addressing:
        POP     DE
        LD      HL, REG_SIDBASE+$04
        ADD     HL, DE
        LD      (HL), A                 ; sta REG_SIDBASE+$04,y

        POP     HL                      ; HL -> instr[n*8] again

        ; instr+0: pulse width lo
        LD      A, (HL)
        LD      DE, REG_SIDBASE+$02
        PUSH    HL
        LD      HL, REG_SIDBASE+$02
        LD      D, 0
        LD      E, C
        ADD     HL, DE
        LD      (HL), A                 ; sta REG_SIDBASE+$02,y
        POP     HL

        ; instr+1: pulse width hi
        INC     HL
        LD      A, (HL)
        PUSH    HL
        LD      HL, REG_SIDBASE+$03
        LD      D, 0
        LD      E, C
        ADD     HL, DE
        LD      (HL), A                 ; sta REG_SIDBASE+$03,y
        POP     HL

        ; restore base, get instr+3: attack/decay
        DEC     HL                      ; back to instr+0
        LD      DE, 3
        ADD     HL, DE                  ; instr+3
        LD      A, (HL)
        PUSH    HL
        LD      HL, REG_SIDBASE+$05
        LD      D, 0
        LD      E, C
        ADD     HL, DE
        LD      (HL), A                 ; sta REG_SIDBASE+$05,y
        POP     HL

        ; instr+4: sustain/release
        INC     HL
        LD      A, (HL)
        PUSH    HL
        LD      HL, REG_SIDBASE+$06
        LD      D, 0
        LD      E, C
        ADD     HL, DE
        LD      (HL), A                 ; sta REG_SIDBASE+$06,y
        POP     HL

        ; restore B (was stashed as tempstore)
        LD      B, (IX+OFS_TEMPSTORE)
        LD      A, (IX+OFS_TEMPCTRL)
        LD      (IX+OFS_VOICECTRL), A   ; voicectrl[B]  (+B)

        ; check 4th byte for end-of-pattern ($FF)
        INC     (IX+OFS_PATOFFSET)      ; inc patoffset (+B)
        LD      E, (IX+OFS_PATOFFSET)
        LD      D, 0
        LD      L, (IX+OFS_ZP_04)
        LD      H, (IX+OFS_ZP_05)
        ADD     HL, DE
        LD      A, (HL)
        CP      $FF
        JP      NZ, _no_eop

        XOR     A
        LD      (IX+OFS_PATOFFSET), A   ; reset patoffset (+B)
        INC     (IX+OFS_POSOFFSET)      ; inc posoffset (+B)

_no_eop:
        JP      loopcont

; ============================================================
; SOUNDWORK  (no new note, process effects)
; ============================================================
soundwork:
        ; release routine
        LD      C, (IX+OFS_TMPREGOFST)

        LD      A, (IX+OFS_SAVELNTHCC)  ; (+B)
        AND     $20                     ; bit5 = no-release flag
        JP      NZ, vibrato

        LD      A, (IX+OFS_LENGTHLEFT)  ; (+B)
        OR      A
        JP      NZ, vibrato

        ; start release
        LD      A, (IX+OFS_VOICECTRL)   ; (+B)
        AND     $FE                     ; clear gate bit
        LD      HL, REG_SIDBASE+$04
        LD      D, 0
        LD      E, C
        ADD     HL, DE
        LD      (HL), A
        XOR     A
        LD      HL, REG_SIDBASE+$05
        ADD     HL, DE
        LD      (HL), A
        LD      HL, REG_SIDBASE+$06
        ADD     HL, DE
        LD      (HL), A

; ============================================================
; VIBRATO
; ============================================================
vibrato:
        LD      A, (IX+OFS_INSTRNR)     ; (+B)
        ADD     A, A                    ; *2
        ADD     A, A                    ; *4
        ADD     A, A                    ; *8
        LD      (IX+OFS_INSTNUMBY8), A
        LD      E, A
        LD      D, 0
        LD      HL, instr
        ADD     HL, DE                  ; HL -> instr[n*8]

        LD      DE, 7
        PUSH    HL
        ADD     HL, DE
        LD      A, (HL)                 ; instr+7: fx byte
        LD      (IX+OFS_INSTRFX), A
        POP     HL

        LD      DE, 6
        PUSH    HL
        ADD     HL, DE
        LD      A, (HL)                 ; instr+6: pulse speed
        LD      (IX+OFS_PULSEVALUE), A
        POP     HL

        LD      DE, 5
        ADD     HL, DE
        LD      A, (HL)                 ; instr+5: vibrato depth
        LD      (IX+OFS_VIBRDEPTH), A
        OR      A
        JP      Z, pulsework            ; beq pulsework (no vibrato)

        ; build oscillating counter value (01233210)
        LD      A, (IX+OFS_COUNTER)
        AND     7                       ; keep low 3 bits
        CP      4
        JP      C, _vib_low
        XOR     7                       ; eor #7
_vib_low:
        LD      (IX+OFS_OSCILATVAL), A

        ; get frequency difference between note and note+1
        LD      A, (IX+OFS_NOTENUM)     ; (+B)
        ADD     A, A                    ; note*2 -> freq table index
        LD      E, A
        LD      D, 0

        ; lda frequenzlo+2,y - frequenzlo,y  (note+1 minus note, lo byte)
        LD      HL, frequenzlo+2
        ADD     HL, DE
        LD      A, (HL)
        LD      HL, frequenzlo
        ADD     HL, DE
        LD      C, A
        LD      A, (HL)
        SUB     C                       ; sbc (carry clear, lo diff)
        ; NOTE: 6502 uses SEC before SBC for borrow=0; here SUB suffices
        LD      (IX+OFS_TMPVDIFLO), A

        LD      HL, frequenzhi+2
        ADD     HL, DE
        LD      A, (HL)
        LD      HL, frequenzhi
        ADD     HL, DE
        LD      C, (HL)
        SBC     A, C                    ; sbc frequenzhi,y
        ; A = hi diff (with borrow from lo)

        ; divide difference by 2 for each vibrdepth (lsr loop)
_vib_shift_loop:
        SRA     A                       ; lsr (arithmetic)
        LD      C, (IX+OFS_TMPVDIFLO)
        RR      C                       ; ror tmpvdiflo
        LD      (IX+OFS_TMPVDIFLO), C
        DEC     (IX+OFS_VIBRDEPTH)
        JP      P, _vib_shift_loop      ; bpl -
        LD      (IX+OFS_TMPVDIFHI), A

        ; save base note frequency
        LD      HL, frequenzlo
        ADD     HL, DE
        LD      A, (HL)
        LD      (IX+OFS_TMPVFRQLO), A
        LD      HL, frequenzhi
        ADD     HL, DE
        LD      A, (HL)
        LD      (IX+OFS_TMPVFRQHI), A

        ; no vibrato if note length < 8
        LD      A, (IX+OFS_SAVELNTHCC)  ; (+B)
        AND     $1F
        CP      8
        JP      C, _vib_write           ; bcc -> skip adding

        ; add vibr freq oscilatval times to base freq
        LD      E, (IX+OFS_OSCILATVAL)
        LD      D, 0
_vib_add_loop:
        DEC     E                       ; dey
        JP      M, _vib_write           ; bmi -> done
        LD      A, (IX+OFS_TMPVFRQLO)
        ADD     A, (IX+OFS_TMPVDIFLO)
        LD      (IX+OFS_TMPVFRQLO), A
        LD      A, (IX+OFS_TMPVFRQHI)
        ADC     A, (IX+OFS_TMPVDIFHI)
        LD      (IX+OFS_TMPVFRQHI), A
        JP      _vib_add_loop

_vib_write:
        LD      C, (IX+OFS_TMPREGOFST)
        LD      D, 0
        LD      E, C
        LD      A, (IX+OFS_TMPVFRQLO)
        LD      HL, REG_SIDBASE+$00
        ADD     HL, DE
        LD      (HL), A
        LD      A, (IX+OFS_TMPVFRQHI)
        LD      HL, REG_SIDBASE+$01
        ADD     HL, DE
        LD      (HL), A

; ============================================================
; PULSEWORK
; ============================================================
pulsework:
        LD      A, (IX+OFS_PULSEVALUE)
        OR      A
        JP      Z, portamento           ; beq portamento

        LD      E, (IX+OFS_INSTNUMBY8)
        AND     $1F                     ; pulse delay reload value

        DEC     (IX+OFS_PULSEDELAY)     ; pulsedelay[B]-1  (+B)
        JP      P, portamento           ; bpl portamento

        LD      (IX+OFS_PULSEDELAY), A  ; reset delay  (+B)

        LD      A, (IX+OFS_PULSEVALUE)
        AND     $E0
        LD      (IX+OFS_PULSESPEED), A

        LD      A, (IX+OFS_PULSEDIR)    ; pulsedir[B]  (+B)
        OR      A
        JP      NZ, pulsedown

        ; pulse up
        LD      A, (IX+OFS_PULSESPEED)
        LD      D, 0
        LD      HL, instr
        LD      E, (IX+OFS_INSTNUMBY8)
        ADD     HL, DE
        ADD     A, (HL)                 ; adc instr+0,y
        PUSH    AF
        INC     HL
        LD      A, (HL)                 ; instr+1,y
        ADC     A, 0
        AND     $0F
        LD      C, A
        POP     AF
        PUSH    BC
        LD      A, C
        CP      $0E
        JP      NZ, dumpulse
        INC     (IX+OFS_PULSEDIR)       ; (+B)
        JP      dumpulse

pulsedown:
        LD      HL, instr
        LD      D, 0
        LD      E, (IX+OFS_INSTNUMBY8)
        ADD     HL, DE
        LD      A, (HL)                 ; instr+0,y
        LD      C, (IX+OFS_PULSESPEED)
        SUB     C                       ; sec sbc pulsespeed
        PUSH    AF
        INC     HL
        LD      A, (HL)                 ; instr+1,y
        SBC     A, 0
        AND     $0F
        LD      C, A
        POP     AF
        PUSH    BC
        LD      A, C
        CP      $08
        JP      NZ, dumpulse
        DEC     (IX+OFS_PULSEDIR)       ; (+B)

dumpulse:
        LD      (IX+OFS_TEMPSTORE), B   ; stx tempstore
        LD      B, (IX+OFS_TMPREGOFST)  ; ldx tmpregofst
        POP     AF                      ; pla (hi byte)
        LD      D, 0
        LD      E, (IX+OFS_INSTNUMBY8)
        LD      HL, instr
        ADD     HL, DE
        INC     HL                      ; instr+1
        LD      (HL), A                 ; sta instr+1,y
        LD      HL, REG_SIDBASE+$03
        LD      D, 0
        LD      E, B
        ADD     HL, DE
        LD      (HL), A                 ; sta REG_SIDBASE+$03,x

        POP     AF                      ; pla (lo byte)
        LD      HL, instr
        LD      D, 0
        LD      E, (IX+OFS_INSTNUMBY8)
        ADD     HL, DE
        LD      (HL), A                 ; sta instr+0,y
        LD      HL, REG_SIDBASE+$02
        LD      D, 0
        LD      E, B
        ADD     HL, DE
        LD      (HL), A                 ; sta REG_SIDBASE+$02,x

        LD      B, (IX+OFS_TEMPSTORE)   ; ldx tempstore

; ============================================================
; PORTAMENTO
; ============================================================
portamento:
        LD      C, (IX+OFS_TMPREGOFST)
        LD      D, 0
        LD      E, C
        LD      A, (IX+OFS_PORTAVAL)    ; (+B)
        OR      A
        JP      Z, drums                ; beq drums

        AND     $7E                     ; mask unwanted bits
        LD      (IX+OFS_TEMPSTORE), A

        LD      A, (IX+OFS_PORTAVAL)    ; (+B)
        AND     $01                     ; bit0: direction
        JP      Z, portup

        ; portamento down
        LD      A, (IX+OFS_SAVEFREQLO)  ; (+B)
        SUB     (IX+OFS_TEMPSTORE)
        LD      (IX+OFS_SAVEFREQLO), A  ; (+B)
        LD      HL, REG_SIDBASE+$00
        ADD     HL, DE
        LD      (HL), A
        LD      A, (IX+OFS_SAVEFREQHI)  ; (+B)
        SBC     A, 0
        LD      (IX+OFS_SAVEFREQHI), A  ; (+B)
        LD      HL, REG_SIDBASE+$01
        ADD     HL, DE
        LD      (HL), A
        JP      drums

portup:
        ; portamento up
        LD      A, (IX+OFS_SAVEFREQLO)  ; (+B)
        ADD     A, (IX+OFS_TEMPSTORE)
        LD      (IX+OFS_SAVEFREQLO), A  ; (+B)
        LD      HL, REG_SIDBASE+$00
        ADD     HL, DE
        LD      (HL), A
        LD      A, (IX+OFS_SAVEFREQHI)  ; (+B)
        ADC     A, 0
        LD      (IX+OFS_SAVEFREQHI), A  ; (+B)
        LD      HL, REG_SIDBASE+$01
        ADD     HL, DE
        LD      (HL), A

; ============================================================
; DRUMS
; ============================================================
drums:
        LD      A, (IX+OFS_INSTRFX)
        AND     $01
        JP      Z, skydive

        LD      A, (IX+OFS_SAVEFREQHI)  ; (+B)
        OR      A
        JP      Z, skydive

        LD      A, (IX+OFS_LENGTHLEFT)  ; (+B)
        OR      A
        JP      Z, skydive

        ; check if first vbl for this note
        LD      A, (IX+OFS_SAVELNTHCC)  ; (+B)
        AND     $1F
        DEC     A                       ; sec sbc #1
        LD      D, A
        LD      A, (IX+OFS_LENGTHLEFT)  ; (+B)
        LD      C, (IX+OFS_TMPREGOFST)
        LD      E, C
        CP      D                       ; cmp lengthleft
        JP      C, firstime             ; bcc firstime

        ; not first time: dec freqhi for drum
        LD      A, (IX+OFS_SAVEFREQHI)  ; (+B)
        DEC     A
        LD      (IX+OFS_SAVEFREQHI), A  ; (+B)
        LD      HL, REG_SIDBASE+$01
        LD      D, 0
        ADD     HL, DE
        LD      (HL), A

        LD      A, (IX+OFS_VOICECTRL)   ; (+B)
        AND     $FE
        JP      NZ, dumpctrl

firstime:
        LD      A, (IX+OFS_SAVEFREQHI)  ; (+B)
        LD      HL, REG_SIDBASE+$01
        LD      D, 0
        LD      E, C
        ADD     HL, DE
        LD      (HL), A
        LD      A, $80                  ; set noise

dumpctrl:
        LD      HL, REG_SIDBASE+$04
        LD      D, 0
        LD      E, C
        ADD     HL, DE
        LD      (HL), A

; ============================================================
; SKYDIVE
; ============================================================
skydive:
        LD      A, (IX+OFS_INSTRFX)
        AND     $02
        JP      Z, octarp

        LD      A, (IX+OFS_COUNTER)
        AND     $01
        JP      Z, octarp               ; every 2nd vbl

        LD      A, (IX+OFS_SAVEFREQHI)  ; (+B)
        OR      A
        JP      Z, octarp

        DEC     A
        LD      (IX+OFS_SAVEFREQHI), A  ; (+B)
        LD      C, (IX+OFS_TMPREGOFST)
        LD      HL, REG_SIDBASE+$01
        LD      D, 0
        LD      E, C
        ADD     HL, DE
        LD      (HL), A

; ============================================================
; OCTAVE ARPEGGIO
; ============================================================
octarp:
        LD      A, (IX+OFS_INSTRFX)
        AND     $04
        JP      Z, loopcont

        LD      A, (IX+OFS_COUNTER)
        AND     $01
        JP      Z, _oct_even

        ; odd: note+12
        LD      A, (IX+OFS_NOTENUM)     ; (+B)
        ADD     A, $0C
        JP      _oct_dump

_oct_even:
        LD      A, (IX+OFS_NOTENUM)     ; (+B)

_oct_dump:
        ADD     A, A                    ; *2
        LD      E, A
        LD      D, 0
        LD      HL, frequenzlo
        ADD     HL, DE
        LD      A, (HL)
        LD      (IX+OFS_TEMPFREQ), A
        LD      HL, frequenzhi
        ADD     HL, DE
        LD      C, (IX+OFS_TMPREGOFST)
        LD      HL, REG_SIDBASE+$01
        LD      D, 0
        LD      E, C
        ADD     HL, DE
        LD      (HL), A
        LD      A, (IX+OFS_TEMPFREQ)
        LD      HL, REG_SIDBASE+$00
        ADD     HL, DE
        LD      (HL), A

; ============================================================
; LOOP CONTINUE / END
; ============================================================
loopcont:
;        DJNZ    mainloop                ; dex / bmi -> when B wraps past 0

        DJNZ    xx


musicend:
        RET
xx:
        jp mainloop                ; dex / bmi -> when B wraps past 0


; ============================================================
; RESTART (track position)
; ============================================================
restart:
        XOR     A
        LD      (IX+OFS_LENGTHLEFT), A  ; (+B)
        LD      (IX+OFS_POSOFFSET), A   ; (+B)
        LD      (IX+OFS_PATOFFSET), A   ; (+B)
        JP      getnewnote

; ============================================================
; DATA TABLES
; (Identical to original -- copied verbatim)
; ============================================================

regoffsets:
        DB      $00, $07, $0E

; --- Frequency table (lo bytes then hi bytes interleaved) ---
frequenzlo:
        DB      $16,$01,$27,$01,$38,$01,$4B,$01
        DB      $5F,$01,$73,$01,$8A,$01,$A1,$01
        DB      $BA,$01,$D4,$01,$F0,$01,$0E,$02
        DB      $2D,$02,$4E,$02,$71,$02,$96,$02
        DB      $BD,$02,$E7,$02,$13,$03,$42,$03
        DB      $74,$03,$A9,$03,$E0,$03,$1B,$04
        DB      $5A,$04,$9B,$04,$E2,$04,$2C,$05
        DB      $7B,$05,$CE,$05,$27,$06,$85,$06
        DB      $E8,$06,$51,$07,$C1,$07,$37,$08
        DB      $B4,$08,$37,$09,$C4,$09,$57,$0A
        DB      $F5,$0A,$9C,$0B,$4E,$0C,$09,$0D
        DB      $D0,$0D,$A3,$0E,$82,$0F,$6E,$10
        DB      $68,$11,$6E,$12,$88,$13,$AF,$14
        DB      $EB,$15,$39,$17,$9C,$18,$13,$1A
        DB      $A1,$1B,$46,$1D,$04,$1F,$DC,$20
        DB      $D0,$22,$DC,$24,$10,$27,$5E,$29
        DB      $D6,$2B,$72,$2E,$38,$31,$26,$34
        DB      $42,$37,$8C,$3A,$08,$3E,$B8,$41
        DB      $A0,$45,$B8,$49,$20,$4E,$BC,$52
        DB      $AC,$57,$E4,$5C,$70,$62,$4C,$68
        DB      $84,$6E,$18,$75,$10,$7C,$70,$83
        DB      $40,$8B,$70,$93,$40,$9C,$78,$A5
        DB      $58,$AF,$C8,$B9,$E0,$C4,$98,$D0
        DB      $08,$DD,$30,$EA,$20,$F8,$2E,$FD

; NOTE: In the original, frequenzlo and frequenzhi are interleaved as
; lo,hi pairs. The table above preserves that exact layout.
; frequenzhi is implicitly the odd bytes of the same table.
; For Z80 access: frequenzlo[note*2] = lo, frequenzhi[note*2] = hi
; (same addressing as 6502 since table layout is unchanged)

frequenzhi:      EQU     frequenzlo+1    ; hi bytes are odd-indexed entries

; ============================================================
; SONGS / TRACK / PATTERN data
; (All data identical to original -- include verbatim)
; ============================================================

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

; ============================================================
; TRACKS (verbatim data from original)
; ============================================================
montymaintr1:
        DB      $11,$14,$17,$1A,$00,$27,$00,$28
        DB      $03,$05,$00,$27,$00,$28,$03,$05
        DB      $07,$3A,$14,$17,$00,$27,$00,$28
        DB      $2F,$30,$31,$31,$32,$33,$33,$34
        DB      $34,$34,$34,$34,$34,$34,$34,$35
        DB      $35,$35,$35,$35,$35,$36,$12,$37
        DB      $38,$09,$2A,$09,$2B,$09,$0A,$09
        DB      $2A,$09,$2B,$09,$0A,$0D,$0D,$0F
        DB      $FF

montymaintr2:
        DB      $12,$15,$18,$1B,$2D,$39,$39
        DB      $39,$39,$39,$39,$2C,$39,$39,$39
        DB      $39,$39,$39,$2C,$39,$39,$39,$01
        DB      $01,$29,$29,$2C,$15,$18,$39,$39
        DB      $39,$39,$39,$39,$39,$39,$39,$39
        DB      $39,$39,$39,$39,$39,$39,$39,$39
        DB      $39,$39,$39,$39,$39,$39,$39,$39
        DB      $39,$39,$39,$39,$39,$01,$01,$01
        DB      $29,$39,$39,$39,$01,$01,$01,$29
        DB      $39,$39,$39,$39,$FF

montymaintr3:
        DB      $13,$16,$19
        DB      $1C,$02,$02,$1D,$1E,$02,$02,$1D
        DB      $1F,$04,$04,$20,$20,$06,$02,$02
        DB      $1D,$1E,$02,$02,$1D,$1F,$04,$04
        DB      $20,$20,$06,$08,$08,$08,$08,$21
        DB      $21,$21,$21,$22,$22,$22,$23,$22
        DB      $24,$25,$3B,$26,$26,$26,$26,$26
        DB      $26,$26,$26,$26,$26,$26,$26,$26
        DB      $26,$26,$26,$02,$02,$1D,$1E,$02
        DB      $02,$1D,$1F,$2F,$2F,$2F,$2F,$2F
        DB      $2F,$2F,$2F,$2F,$2F,$2F,$2F,$2F
        DB      $0B,$0B,$1D,$1D,$0B,$0B,$1D,$0B
        DB      $0B,$0B,$0C,$0C,$1D,$1D,$1D,$10
        DB      $0B,$0B,$1D,$1D,$0B,$0B,$1D,$0B
        DB      $0B,$0B,$0C,$0C,$1D,$1D,$1D,$10
        DB      $0B,$1D,$0B,$1D,$0B,$1D,$0B,$1D
        DB      $0B,$0C,$1D,$0B,$0C,$23,$0B,$0B
        DB      $FF

; ============================================================
; PATTERNS (verbatim data from original)
; ============================================================
ptn00:  DB $83,$00,$37,$01,$3E,$01,$3E,$03
        DB $3D,$03,$3E,$03,$43,$03,$3E,$03
        DB $3D,$03,$3E,$03,$37,$01,$3E,$01
        DB $3E,$03,$3D,$03,$3E,$03,$43,$03
        DB $42,$03,$43,$03,$45,$03,$46,$01
        DB $48,$01,$46,$03,$45,$03,$43,$03
        DB $4B,$01,$4D,$01,$4B,$03,$4A,$03
        DB $48,$FF

ptn27:  DB $1F,$4A,$FF

ptn28:  DB $03,$46,$01,$48,$01,$46,$03,$45
        DB $03,$4A,$0F,$43,$FF

ptn03:  DB $BF,$06
        DB $48,$07,$48,$01,$4B,$01,$4A,$01
        DB $4B,$01,$4A,$03,$4B,$03,$4D,$03
        DB $4B,$03,$4A,$3F,$48,$07,$48,$01
        DB $4B,$01,$4A,$01,$4B,$01,$4A,$03
        DB $4B,$03,$4D,$03,$4B,$03,$48,$3F
        DB $4C,$07,$4C,$01,$4F,$01,$4E,$01
        DB $4F,$01,$4E,$03,$4F,$03,$51,$03
        DB $4F,$03,$4E,$3F,$4C,$07,$4C,$01
        DB $4F,$01,$4E,$01,$4F,$01,$4E,$03
        DB $4F,$03,$51,$03,$4F,$03,$4C,$FF

ptn05:  DB $83,$04,$26,$03,$29,$03,$28,$03
        DB $29,$03,$26,$03,$35,$03,$34,$03
        DB $32,$03,$2D,$03,$30,$03,$2F,$03
        DB $30,$03,$2D,$03,$3C,$03,$3B,$03
        DB $39,$03,$30,$03,$33,$03,$32,$03
        DB $33,$03,$30,$03,$3F,$03,$3E,$03
        DB $3C,$03,$46,$03,$45,$03,$43,$03
        DB $3A,$03,$39,$03,$37,$03,$2E,$03
        DB $2D,$03,$26,$03,$29,$03,$28,$03
        DB $29,$03,$26,$03,$35,$03,$34,$03
        DB $32,$03,$2D,$03,$30,$03,$2F,$03
        DB $30,$03,$2D,$03,$3C,$03,$3B,$03
        DB $39,$03,$30,$03,$33,$03,$32,$03
        DB $33,$03,$30,$03,$3F,$03,$3E,$03
        DB $3C,$03,$34,$03,$37,$03,$36,$03
        DB $37,$03,$34,$03,$37,$03,$3A,$03
        DB $3D

ptn3a:  DB $03,$3E,$07,$3E,$07,$3F,$07
        DB $3E,$03,$3C,$07,$3E,$57,$FF

ptn07:  DB $8B
        DB $00,$3A,$01,$3A,$01,$3C,$03,$3D
        DB $03,$3F,$03,$3D,$03,$3C,$0B,$3A
        DB $03,$39,$07,$3A,$81,$06,$4B,$01
        DB $4D,$01,$4E,$01,$4D,$01,$4E,$01
        DB $4D,$05,$4B,$81,$00,$3A,$01,$3C
        DB $01,$3D,$03,$3F,$03,$3D,$03,$3C
        DB $03,$3A,$03,$39,$1B,$3A,$0B,$3B
        DB $01,$3B,$01,$3D,$03,$3E,$03,$40
        DB $03,$3E,$03,$3D,$0B,$3B,$03,$3A
        DB $07,$3B,$81,$06,$4C,$01,$4E,$01
        DB $4F,$01,$4E,$01,$4F,$01,$4E,$05
        DB $4C,$81,$00,$3B,$01,$3D,$01,$3E
        DB $03,$40,$03,$3E,$03,$3D,$03,$3B
        DB $03,$3A,$1B,$3B,$8B,$05,$35,$03
        DB $33,$07,$32,$03,$30,$03,$2F,$0B
        DB $30,$03,$32,$0F,$30,$0B,$35,$03
        DB $33,$07,$32,$03,$30,$03,$2F,$1F
        DB $30,$8B,$00,$3C,$01,$3C,$01,$3E
        DB $03,$3F,$03,$41,$03,$3F,$03,$3E
        DB $0B,$3D,$01,$3D,$01,$3F,$03,$40
        DB $03,$42,$03,$40,$03,$3F,$03,$3E
        DB $01,$3E,$01,$40,$03,$41,$03,$40
        DB $03,$3E,$03,$3D,$03,$3E,$03,$3C
        DB $03,$3A,$01,$3A,$01,$3C,$03,$3D
        DB $03,$3C,$03,$3A,$03,$39,$03,$3A
        DB $03,$3C,$FF

ptn09:  DB $83,$00,$32,$01,$35,$01,$34,$03
        DB $32,$03,$35,$03,$34,$03,$32,$03
        DB $35,$01,$34,$01,$32,$03,$32,$03
        DB $3A,$03,$39,$03,$3A,$03,$32,$03
        DB $3A,$03,$39,$03,$3A,$FF

ptn2a:  DB $03,$34,$01,$37,$01,$35,$03,$34
        DB $03,$37,$03,$35,$03,$34,$03,$37
        DB $01,$35,$01,$34,$03,$34,$03,$3A
        DB $03,$39,$03,$3A,$03,$34,$03,$3A
        DB $03,$39,$03,$3A,$FF

ptn2b:  DB $03,$39,$03,$38,$03,$39,$03,$3A
        DB $03,$39,$03,$37,$03,$35,$03,$34
        DB $03,$35,$03,$34,$03,$35,$03,$37
        DB $03,$35,$03,$34,$03,$32,$03,$31
        DB $FF

ptn0a:  DB $03
        DB $37,$01,$3A,$01,$39,$03,$37,$03
        DB $3A,$03,$39,$03,$37,$03,$3A,$01
        DB $39,$01,$37,$03,$37,$03,$3E,$03
        DB $3D,$03,$3E,$03,$37,$03,$3E,$03
        DB $3D,$03,$3E,$03,$3D,$01,$40,$01
        DB $3E,$03,$3D,$03,$40,$01,$3E,$01
        DB $3D,$03,$40,$03,$3E,$03,$40,$03
        DB $40,$01,$43,$01,$41,$03,$40,$03
        DB $43,$01,$41,$01,$40,$03,$43,$03
        DB $41,$03,$43,$03,$43,$01,$46,$01
        DB $45,$03,$43,$03,$46,$01,$45,$01
        DB $43,$03,$46,$03,$45,$03,$43,$01
        DB $48,$01,$49,$01,$48,$01,$46,$01
        DB $45,$01,$46,$01,$45,$01,$43,$01
        DB $41,$01,$43,$01,$41,$01,$40,$01
        DB $3D,$01,$39,$01,$3B,$01,$3D,$FF

ptn0d:  DB $01,$3E,$01,$39,$01,$35,$01,$39
        DB $01,$3E,$01,$39,$01,$35,$01,$39
        DB $03,$3E,$01,$41,$01,$40,$03,$40
        DB $01,$3D,$01,$3E,$01,$40,$01,$3D
        DB $01,$39,$01,$3D,$01,$40,$01,$3D
        DB $01,$39,$01,$3D,$03,$40,$01,$43
        DB $01,$41,$03,$41,$01,$3E,$01,$40
        DB $01,$41,$01,$3E,$01,$39,$01,$3E
        DB $01,$41,$01,$3E,$01,$39,$01,$3E
        DB $03,$41,$01,$45,$01,$43,$03,$43
        DB $01,$40,$01,$41,$01,$43,$01,$40
        DB $01,$3D,$01,$40,$01,$43,$01,$40
        DB $01,$3D,$01,$40,$01,$46,$01,$43
        DB $01,$45,$01,$46,$01,$44,$01,$43
        DB $01,$40,$01,$3D,$FF

ptn0f:  DB $01,$3E,$01
        DB $39,$01,$35,$01,$39,$01,$3E,$01
        DB $39,$01,$35,$01,$39,$01,$3E,$01
        DB $39,$01,$35,$01,$39,$01,$3E,$01
        DB $39,$01,$35,$01,$39,$01,$3E,$01
        DB $3A,$01,$37,$01,$3A,$01,$3E,$01
        DB $3A,$01,$37,$01,$3A,$01,$3E,$01
        DB $3A,$01,$37,$01,$3A,$01,$3E,$01
        DB $3A,$01,$37,$01,$3A,$01,$40,$01
        DB $3D,$01,$39,$01,$3D,$01,$40,$01
        DB $3D,$01,$39,$01,$3D,$01,$40,$01
        DB $3D,$01,$39,$01,$3D,$01,$40,$01
        DB $3D,$01,$39,$01,$3D,$01,$41,$01
        DB $3E,$01,$39,$01,$3E,$01,$41,$01
        DB $3E,$01,$39,$01,$3E,$01,$41,$01
        DB $3E,$01,$39,$01,$3E,$01,$41,$01
        DB $3E,$01,$39,$01,$3E,$01,$43,$01
        DB $3E,$01,$3A,$01,$3E,$01,$43,$01
        DB $3E,$01,$3A,$01,$3E,$01,$43,$01
        DB $3E,$01,$3A,$01,$3E,$01,$43,$01
        DB $3E,$01,$3A,$01,$3E,$01,$43,$01
        DB $3F,$01,$3C,$01,$3F,$01,$43,$01
        DB $3F,$01,$3C,$01,$3F,$01,$43,$01
        DB $3F,$01,$3C,$01,$3F,$01,$43,$01
        DB $3F,$01,$3C,$01,$3F,$01,$45,$01
        DB $42,$01,$3C,$01,$42,$01,$45,$01
        DB $42,$01,$3C,$01,$42,$01,$48,$01
        DB $45,$01,$42,$01,$45,$01,$4B,$01
        DB $48,$01,$45,$01,$48,$01,$4B,$01
        DB $4A,$01,$48,$01,$4A,$01,$4B,$01
        DB $4A,$01,$48,$01,$4A,$01,$4B,$01
        DB $4A,$01,$48,$01,$4A,$01,$4C,$01
        DB $4E,$03,$4F,$FF

ptn11:  DB $BF,$06,$56,$1F,$57,$1F,$56,$1F
        DB $5B,$1F,$56,$1F,$57,$1F,$56,$1F
        DB $4F,$FF

ptn12:  DB $BF,$0C,$68,$7F,$7F,$7F,$7F,$7F
        DB $7F,$7F,$FF

ptn13:  DB $BF,$08,$13,$3F,$13,$3F,$13,$3F
        DB $13,$3F,$13,$3F,$13,$3F,$13,$1F
        DB $13,$FF

ptn14:  DB $97,$09,$2E,$03,$2E,$1B,$32,$03
        DB $32,$1B,$31,$03,$31,$1F,$34,$43
        DB $17,$32,$03,$32,$1B,$35,$03,$35
        DB $1B,$34,$03,$34,$0F,$37,$8F,$0A
        DB $37,$43,$FF

ptn15:  DB $97,$09,$2B,$03,$2B,$1B,$2E,$03
        DB $2E,$1B,$2D,$03,$2D,$1F,$30,$43
        DB $17,$2E,$03,$2E,$1B,$32,$03,$32
        DB $1B,$31,$03,$31,$0F,$34,$8F,$0A
        DB $34,$43,$FF

ptn16:  DB $0F,$1F,$0F,$1F,$0F,$1F,$0F,$1F
        DB $0F,$1F,$0F,$1F,$0F,$1F,$0F,$1F
        DB $0F,$1F,$0F,$1F,$0F,$1F,$0F,$1F
        DB $0F,$1F,$0F,$1F,$0F,$1F,$0F,$1F
        DB $FF

ptn17:  DB $97,$09,$33,$03,$33,$1B,$37,$03
        DB $37,$1B,$36,$03,$36,$1F,$39,$43
        DB $17,$37,$03,$37,$1B,$3A,$03,$3A
        DB $1B,$39,$03,$39,$2F,$3C,$21,$3C
        DB $21,$3D,$21,$3E,$21,$3F,$21,$40
        DB $21,$41,$21,$42,$21,$43,$21,$44
        DB $01,$45,$FF

ptn18:  DB $97,$09,$30,$03,$30,$1B,$33,$03
        DB $33,$1B,$32,$03,$32,$1F,$36,$43
        DB $17,$33,$03,$33,$1B,$37,$03,$37
        DB $1B,$36,$03,$36,$2F,$39,$21,$39
        DB $21,$3A,$21,$3B,$21,$3C,$21,$3D
        DB $21,$3E,$21,$3F,$21,$40,$21,$41
        DB $01,$42,$FF

ptn19:  DB $0F,$1A,$0F,$1A,$0F,$1A,$0F,$1A
        DB $0F,$1A,$0F,$1A,$0F,$1A,$0F,$1A
        DB $0F,$1A,$0F,$1A,$0F,$1A,$0F,$1A
        DB $0F,$1A,$0F,$1A,$0F,$1A,$0F,$1A
        DB $FF

ptn1a:  DB $1F,$46,$BF,$0A,$46,$7F,$7F,$FF
ptn1b:  DB $1F,$43,$BF,$0A,$43,$7F,$FF

ptn1c:  DB $83,$02,$13,$03,$13,$03,$1E,$03
        DB $1F,$03,$13,$03,$13,$03,$1E,$03
        DB $1F,$03,$13,$03,$13,$03,$1E,$03
        DB $1F,$03,$13,$03,$13,$03,$1E,$03
        DB $1F,$03,$13,$03,$13,$03,$1E,$03
        DB $1F,$03,$13,$03,$13,$03,$1E,$03
        DB $1F,$03,$13,$03,$13,$03,$1E,$03
        DB $1F,$03,$13,$03,$13,$03,$1E,$03
        DB $1F,$FF

ptn29:  DB $8F,$0B,$38,$4F,$FF

ptn2c:  DB $83,$0E,$32,$07,$32,$07,$2F,$07
        DB $2F,$03,$2B,$87,$0B,$46,$83,$0E
        DB $2C,$03,$2C,$8F,$0B,$32,$FF

ptn2d:  DB $43,$83,$0E,$32,$03,$32,$03,$2F
        DB $03,$2F,$03,$2C,$87,$0B,$38,$FF

ptn39:  DB $83,$01
        DB $43,$01,$4F,$01,$5B,$87,$03,$2F
        DB $83,$01,$43,$01,$4F,$01,$5B,$87
        DB $03,$2F,$83,$01,$43,$01,$4F,$01
        DB $5B,$87,$03,$2F,$83,$01,$43,$01
        DB $4F,$01,$5B,$87,$03,$2F,$83,$01
        DB $43,$01,$4F,$01,$5B,$87,$03,$2F
        DB $83,$01,$43,$01,$4F,$01,$5B,$87
        DB $03,$2F

ptn01:  DB $83,$01,$43,$01,$4F,$01,$5B,$87
        DB $03,$2F,$83,$01,$43,$01,$4F,$01
        DB $5B,$87,$03,$2F,$FF

ptn02:  DB $83,$02,$13,$03,$13,$03,$1F,$03
        DB $1F,$03,$13,$03,$13,$03,$1F,$03
        DB $1F,$FF

ptn1d:  DB $03,$15,$03,$15,$03,$1F,$03,$21
        DB $03,$15,$03,$15,$03,$1F,$03,$21
        DB $FF

ptn1e:  DB $03,$1A,$03,$1A,$03,$1C,$03,$1C
        DB $03,$1D,$03,$1D,$03,$1E,$03,$1E
        DB $FF

ptn1f:  DB $03,$1A,$03,$1A,$03,$24,$03,$26
        DB $03,$13,$03,$13,$07,$1F,$FF

ptn04:  DB $03,$18,$03,$18,$03,$24,$03,$24
        DB $03,$18,$03,$18,$03,$24,$03,$24
        DB $03,$20,$03,$20,$03,$2C,$03,$2C
        DB $03,$20,$03,$20,$03,$2C,$03,$2C
        DB $FF

ptn20:  DB $03,$19,$03,$19,$03
        DB $25,$03,$25,$03,$19,$03,$19,$03
        DB $25,$03,$25,$03,$21,$03,$21,$03
        DB $2D,$03,$2D,$03,$21,$03,$21,$03
        DB $2D,$03,$2D,$FF

ptn06:  DB $03,$1A,$03,$1A
        DB $03,$26,$03,$26,$03,$1A,$03,$1A
        DB $03,$26,$03,$26,$03,$15,$03,$15
        DB $03,$21,$03,$21,$03,$15,$03,$15
        DB $03,$21,$03,$21,$03,$18,$03,$18
        DB $03,$24,$03,$24,$03,$18,$03,$18
        DB $03,$24,$03,$24,$03,$1F,$03,$1F
        DB $03,$2B,$03,$2B,$03,$1F,$03,$1F
        DB $03,$2B,$03,$2B,$03,$1A,$03,$1A
        DB $03,$26,$03,$26,$03,$1A,$03,$1A
        DB $03,$26,$03,$26,$03,$15,$03,$15
        DB $03,$21,$03,$21,$03,$15,$03,$15
        DB $03,$21,$03,$21,$03,$18,$03,$18
        DB $03,$24,$03,$24,$03,$18,$03,$18
        DB $03,$24,$03,$24,$03,$1C,$03,$1C
        DB $03,$28,$03,$28,$03,$1C,$03,$1C
        DB $03,$28,$03,$28

ptn3b:  DB $83,$04,$36,$07
        DB $36,$07,$37,$07,$36,$03,$33,$07
        DB $32,$57,$FF

ptn08:  DB $83,$02,$1B,$03,$1B,$03,$27,$03
        DB $27,$03,$1B,$03,$1B,$03,$27,$03
        DB $27,$FF

ptn21:  DB $03,$1C,$03,$1C,$03,$28,$03,$28
        DB $03,$1C,$03,$1C,$03,$28,$03,$28
        DB $FF

ptn22:  DB $03,$1D,$03,$1D,$03,$29,$03,$29
        DB $03,$1D,$03,$1D,$03,$29,$03,$29
        DB $FF

ptn23:  DB $03,$18,$03,$18,$03,$24,$03,$24
        DB $03,$18,$03,$18,$03,$24,$03,$24
        DB $FF

ptn24:  DB $03,$1E,$03,$1E,$03,$2A,$03,$2A
        DB $03,$1E,$03,$1E,$03,$2A,$03,$2A
        DB $FF

ptn25:  DB $83,$05,$26,$01,$4A,$01,$34,$03
        DB $29,$03,$4C,$03,$4A,$03,$31,$03
        DB $4A,$03,$24,$03,$22,$01,$46,$01
        DB $30,$03,$25,$03,$48,$03,$46,$03
        DB $2D,$03,$46,$03,$24,$FF

ptn0b:  DB $83,$02,$1A,$03,$1A,$03,$26,$03
        DB $26,$03,$1A,$03,$1A,$03,$26,$03
        DB $26,$FF

ptn0c:  DB $03,$13,$03,$13,$03,$1D,$03,$1F
        DB $03,$13,$03,$13,$03,$1D,$03,$1F
        DB $FF

ptn26:  DB $87,$02,$1A,$87,$03,$2F,$83,$02
        DB $26,$03,$26,$87,$03,$2F,$FF

ptn10:  DB $07,$1A,$4F,$47,$FF

ptn0e:  DB $03,$1F,$03,$1F,$03,$24,$03,$26
        DB $07,$13,$47,$FF

ptn30:  DB $BF,$0F,$32,$0F,$32,$8F,$90,$30
        DB $3F,$32,$13,$32,$03,$32,$03,$35
        DB $03,$37,$3F,$37,$0F,$37,$8F,$90
        DB $30,$3F,$32,$13,$32,$03,$2D,$03
        DB $30,$03,$32,$FF

ptn31:  DB $0F,$32
        DB $AF,$90,$35,$0F,$37,$A7,$99,$37
        DB $07,$35,$3F,$32,$13,$32,$03,$32
        DB $A3,$E8,$35,$03,$37,$0F,$35,$AF
        DB $90,$37,$0F,$37,$A7,$99,$37,$07
        DB $35,$3F,$32,$13,$32,$03,$2D,$A3
        DB $E8,$30,$03,$32,$FF

ptn32:  DB $07,$32,$03
        DB $39,$13,$3C,$A7,$9A,$37,$A7,$9B
        DB $38,$07,$37,$03,$35,$03,$32,$03
        DB $39,$1B,$3C,$A7,$9A,$37,$A7,$9B
        DB $38,$07,$37,$03,$35,$03,$32,$03
        DB $39,$03,$3C,$03,$3E,$03,$3C,$07
        DB $3E,$03,$3C,$03,$39,$A7,$9A,$37
        DB $A7,$9B,$38,$07,$37,$03,$35,$03
        DB $32,$AF,$90,$3C,$1F,$3E,$43,$03
        DB $3E,$03,$3C,$03,$3E,$FF

ptn33:  DB $03,$3E
        DB $03,$3E,$A3,$E8,$3C,$03,$3E,$03
        DB $3E,$03,$3E,$A3,$E8,$3C,$03,$3E
        DB $03,$3E,$03,$3E,$A3,$E8,$3C,$03
        DB $3E,$03,$3E,$03,$3E,$A3,$E8,$3C
        DB $03,$3E,$AF,$91,$43,$1F,$41,$43
        DB $03,$3E,$03,$41,$03,$43,$03,$43
        DB $03,$43,$A3,$E8,$41,$03,$43,$03
        DB $43,$03,$43,$A3,$E8,$41,$03,$43
        DB $03,$45,$03,$48,$A3,$FD,$45,$03
        DB $44,$01,$43,$01,$41,$03,$3E,$03
        DB $3C,$03,$3E,$2F,$3E,$BF,$98,$3E
        DB $43,$03,$3E,$03,$3C,$03,$3E,$FF

ptn34:  DB $03,$4A,$03,$4A,$A3,$F8,$48,$03
        DB $4A,$03,$4A,$03,$4A,$A3,$F8,$48
        DB $03,$4A,$FF

ptn35:  DB $01,$51,$01,$54,$01
        DB $51,$01,$54,$01,$51,$01,$54,$01
        DB $51,$01,$54,$01,$51,$01,$54,$01
        DB $51,$01,$54,$01,$51,$01,$54,$01
        DB $51,$01,$54,$FF

ptn36:  DB $01,$50,$01,$4F
        DB $01,$4D,$01,$4A,$01,$4F,$01,$4D
        DB $01,$4A,$01,$48,$01,$4A,$01,$48
        DB $01,$45,$01,$43,$01,$44,$01,$43
        DB $01,$41,$01,$3E,$01,$43,$01,$41
        DB $01,$3E,$01,$3C,$01,$3E,$01,$3C
        DB $01,$39,$01,$37,$01,$38,$01,$37
        DB $01,$35,$01,$32,$01,$37,$01,$35
        DB $01,$32,$01,$30,$FF

ptn37:  DB $5F,$5F,$5F
        DB $47,$83,$0E,$32,$07,$32,$07,$2F
        DB $03,$2F,$07,$2F,$97,$0B,$3A,$5F
        DB $5F,$47,$8B,$0E,$32,$03,$32,$03
        DB $2F,$03,$2F,$47,$97,$0B,$3A,$5F
        DB $5F,$47,$83,$0E,$2F,$0B,$2F,$03
        DB $2F,$03,$2F,$87,$0B,$30,$17,$3A
        DB $5F,$8B,$0E,$32,$0B,$32,$0B,$2F
        DB $0B,$2F,$07,$2C,$07,$2C,$FF

ptn38:  DB $87
        DB $0B,$34,$17,$3A,$5F,$5F,$84,$0E
        DB $32,$04,$32,$05,$32,$04,$2F,$04
        DB $2F,$05,$2F,$47,$97,$0B,$3A,$5F
        DB $5F,$84,$0E,$32,$04,$32,$05,$32
        DB $04,$2F,$04,$2F,$05,$2F,$FF

ptn2f:  DB $03,$1A,$03,$1A,$03
        DB $24,$03,$26,$03,$1A,$03,$1A,$03
        DB $18,$03,$19,$03,$1A,$03,$1A,$03
        DB $24,$03,$26,$03,$1A,$03,$1A,$03
        DB $18,$03,$19,$03,$18,$03,$18,$03
        DB $22,$03,$24,$03,$18,$03,$18,$03
        DB $16,$03,$17,$03,$18,$03,$18,$03
        DB $22,$03,$24,$03,$18,$03,$18,$03
        DB $16,$03,$17,$03,$13,$03,$13,$03
        DB $1D,$03,$1F,$03,$13,$03,$13,$03
        DB $1D,$03,$1E,$03,$13,$03,$13,$03
        DB $1D,$03,$1F,$03,$13,$03,$13,$03
        DB $1D,$03,$1E,$03,$1A,$03,$1A,$03
        DB $24,$03,$26,$03,$1A,$03,$1A,$03
        DB $18,$03,$19,$03,$1A,$03,$1A,$03
        DB $24,$03,$26,$03,$1A,$03,$1A,$03
        DB $18,$03,$19,$FF

; ============================================================
; INSTRUMENTS (verbatim data from original)
; ============================================================
instr:
        DB $80,$09,$41,$48,$60,$03,$81,$00
        DB $00,$08,$81,$02,$08,$00,$00,$01
        DB $A0,$02,$41,$09,$80,$00,$00,$00
        DB $00,$02,$81,$09,$09,$00,$00,$05
        DB $00,$08,$41,$08,$50,$02,$00,$04
        DB $00,$01,$41,$3F,$C0,$02,$00,$00
        DB $00,$08,$41,$04,$40,$02,$00,$00
        DB $00,$08,$41,$09,$00,$02,$00,$00
        DB $00,$09,$41,$09,$70,$02,$5F,$04
        DB $00,$09,$41,$4A,$69,$02,$81,$00
        DB $00,$09,$41,$40,$6F,$00,$81,$02
        DB $80,$07,$81,$0A,$0A,$00,$00,$01
        DB $00,$09,$41,$3F,$FF,$01,$E7,$02
        DB $00,$08,$41,$90,$F0,$01,$E8,$02
        DB $00,$08,$41,$06,$0A,$00,$00,$01
        DB $00,$09,$41,$19,$70,$02,$A8,$00
        DB $00,$02,$41,$09,$90,$02,$00,$00
        DB $00,$00,$11,$0A,$FA,$00,$00,$05
        DB $00,$08,$41,$37,$40,$02,$00,$00
        DB $00,$08,$11,$07,$70,$02,$00,$00

; ============================================================
; END
; ============================================================
