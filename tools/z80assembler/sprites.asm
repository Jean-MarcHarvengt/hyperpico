REG_TEXTMAP_L1:    equ $3C00
REG_TILEMAP_L1:    equ $e800
REG_TILEMAP_L0:    equ $f000
; tile id 0-255 in 8x8   tile mode
; tile id 0-63  in 16x16 tile mode

; sprites (96 max) in L2
NB_SPRITES_MAX:    equ 96

REG_SPRITE_IND:    equ $f800
REG_SPRITE_XHI:    equ $f880
REG_SPRITE_XLO:    equ $f900
REG_SPRITE_Y:      equ $f980
; id:    0-5 (max 63)
; hflip: 6
; vflip: 7

; mode
REG_VIDEO_MODE:    equ $fb00
; 0-1: resolution (0=640x200,1=320x200,2=256x200)

; bg/text color
REG_BG_COL:        equ $fb01
; RGB332
REG_FG_COL:        equ $fb0d
; RGB332
; R: 5-7, 0x20 -> 0xe0   xxx --- --
; G: 2-4, 0x04 -> 0x1c   --- xxx -- 
; B: 0-1, 0x00 -> 0x03   --- --- xx  

; layers config
REG_LAYERS_CFG:    equ $fb02
; 0: L0 on/off (1=on)
; 1: L1 on/off (1=on)    (off if HIRES and bitmap in L0!)
; 2: L2 on/off (1=on)
; 3: L2 inbetween (0 = sprites top)
; 4: bitmap/tile in L0 (0=bitmap)
; 5: petfont/tile in L1 (0=petfont)
; 6: enable scroll area in L0
; 7: enable scroll area in L1

; tiles config
REG_TILES_CFG:     equ $fb0e
; 0: L0: 0=8x8, 1=16x16
; 1: L1: 0=8x8, 1=16x16
; 2-4: xcurtain
;      0: on/off
;      1: 8/16 pixels left
; 5-7: ycurtain
;      0: on/off
;      1: 8/16 pixels top

; lines config
REG_LINES_CFG:     equ $fb03
; 0: single/perline background color
; 1: single/perline L0 xscroll
; 2: single/perline L1 xscroll

; layer scroll
REG_XSCROLL_HI:    equ $fb04
; 3-0: L0 xscroll HI
; 7-4: L1 xscroll HI
REG_XSCROLL_L0:    equ $fb05
REG_XSCROLL_L1:    equ $fb06
REG_YSCROLL_L0:    equ $fb07
REG_YSCROLL_L1:    equ $fb08
; 7-0, in pixels
; scroll area
REG_SC_START_L0:   equ $fb09
REG_SC_END_L0:     equ $fb0a
REG_SC_START_L1:   equ $fb0b
REG_SC_END_L1:     equ $fb0c
; 4-0, in tiles/characters


; vsync line (0-200, 200 is overscan) (RD)
REG_VSYNC:         equ $fb0f
;
; data transfer
REG_TLOOKUP:       equ $fa00
; used as RGB332 LUT for pixels (palette) (WR)
; also used as 256 scratch buffer for other commands (WR/RD) 

REG_TDEPTH:        equ $fb10
; WR
; 1/2/4/8 bits per pixel (using indexed CLUT)
; 9 = 8 bits RGB332 no CLUT
; 0 = compressed

REG_TCOMMAND:      equ $fb11
; WR
; 0: idle
; 1: transfer tiles data      (data=tilenr,w,h,packet pixels)
; 2: transfer sprites data    (data=spritenr,w,h,packet pixels)
; 3: transfer bitmap data     (data=xh,xl,y,wh,wl,h,w*h/packet pixels) 
; 4: transfer t/fmap col data (data=layer,col,row,size,size/packet tiles)
; 5: transfer t/fmap row data (data=layer,col,row,size,size/packet tiles)
; 6: transfer all tile 8bits data compressed (data=sizeh,sizel,pixels)
; 7: transfer all sprite 8bits data compressed (data=sizeh,sizel,pixels)
; 8: transfer bitmap 8bits data compressed (data=sizeh,sizel,pixels)
; 9: transfer font 1bit data, 8bits a time (data=sizeh,sizel,pixels)  

REG_TPARAMS:       equ $fb12
; WR

REG_TDATA:         equ $fb13
; WR

REG_TSTATUS:       equ $fb14
; transfer status (RD) 1=ready for async commands only

; Redefining tiles/sprite sequence
; 1. write lookup palette entries needed
; 2. write transfer mode (1/2/4/8/9)
; 3. write command 1/2
; 4. write params tile/sprite id,w,h
; 5. write data sequence (8bytes*plane for tiles, (h*2)bytes*plane for sprites)
; (any new command to reset)
;
; Transfer bitmap sequence
; 1. write lookup palette entries needed
; 2. write transfer mode (1/2/4/8/9)
; 3. write command 3
; 4. write params XH,XL,Y,WH,WL,H
; 5. write data sequence (bytes*plane /packed_bits)
; (any new command to reset)

; lines background color / scroll (200 values)
REG_LINES_BG_COL:  equ $fb38
; RGB332
REG_LINES_XSCR_HI: equ $fc00
; 7-4:  lines L1 xscroll hi, 3-0: L0 xscroll hi
REG_LINES_L0_XSCR: equ $fcc8
REG_LINES_L1_XSCR: equ $fd90
;
; Sprite collision
; only for first 16 sprites against all the rest (96) 
; LO (8bits x 96 entries, first 8 sprites, bit0 = sprite 0) 
; HI (8bits x 96 entries, last  8 sprites, bit0 = sprite 8)
REG_SPRITE_COL_LO: equ $ff00 
REG_SPRITE_COL_HI: equ $ff80
;
; Audio
; SID (see C64)
REG_SID_BASE:      equ $ff00


NB_SPRITES_TOTAL:  EQU 96
NB_SPRITES:        EQU 32

dw START
 
ORG   $5000   


START:
    ld A, 1
    ld (REG_VIDEO_MODE), A

    LD A, 4+2+1
    LD (REG_LAYERS_CFG), A

    CALL COPY_SINE_TABLE
    CALL SPRITESHIDE

    LD A, 9                 ; Transfer sprite: 8 bits no clut
    LD (REG_TDEPTH), A
    LD HL, SPRITE_MARIO
    LD (SPRITE_PTR_RAM), HL
    CALL SPRITE8BITS

WAIT_VSYNC:
    LD A, (REG_VSYNC)
    CP 200
    jp nz,WAIT_VSYNC

    CALL NEWIRQ

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
    LD A, $07               ; Petfont + bitmap + no sprites
    LD (REG_LAYERS_CFG), A
    LD A, 0                 ; 640x200
    LD (REG_VIDEO_MODE), A
    CALL SPRITESHIDE
    RET


; =============================================================================
; Subroutines
; =============================================================================

; -------------------------------------------------------------
; Hide the sprites below line 200 (and init SPRIND to 0)
; -------------------------------------------------------------

SPRITESHIDE:
    LD B, NB_SPRITES_TOTAL
    LD DE, 0                ; Used for index
HIDE:    
    LD A,0    
    LD HL, REG_SPRITE_IND
    ADD HL, DE
    LD (HL), A
    LD HL, REG_SPRITE_XHI
    ADD HL, DE
    LD (HL), A
    LD HL, REG_SPRITE_XLO
    ADD HL, DE
    LD (HL), A
    LD A,200    
    LD HL, REG_SPRITE_Y
    ADD HL, DE
    LD (HL), A
    INC DE
    DEC B
    LD A,B
    CP 0
    JR NZ, HIDE
    RET

; -------------------------------------------------------------
; Move the wave
; -------------------------------------------------------------
NEWIRQ:
    LD H, TOP/256         ; H is fixed to the page of the SINE table
    INC H
    LD L,0
    ;LD HL, $6000

    LD B, NB_SPRITES
    LD DE, REG_SPRITE_XLO
    LD A, (POSX)
    LD C,A
    INC A
    LD (POSX),A
XLOOP:    
    LD L, C                 ; L = current offset index 
    LD A, (HL)              ; Read from SINE table
    SRL A                   ; Shift right (lsr equivalent)
    LD (DE),A
    INC DE
    INC C
    INC C
    INC C
    INC C
    INC C
    DEC B
    LD A,B
    CP 0    
    JR NZ, XLOOP

    LD B, NB_SPRITES
    LD DE, REG_SPRITE_Y
    LD A, (POSY)
    LD C,A
    INC A
    LD (POSY),A
YLOOP:    
    LD L, C                 ; L = current offset index 
    LD A, (HL)              ; Read from SINE table
    SRL A                   ; Shift right (lsr equivalent)
    LD (DE),A
    INC DE
    INC C
    INC C
    INC C
    INC C
    INC C
    DEC B
    LD A,B
    CP 0    
    JR NZ, YLOOP

    RET


; -------------------------------------------------------------
; Copy sine table above code (256 bytes aligned)
; -------------------------------------------------------------

COPY_SINE_TABLE:
    LD B, 0
    LD HL, SINE
    ;LD DE, $6000
    LD D, TOP/256
    INC D
    LD E,0
COPY:
    LD A,(HL)
    LD (DE),A
    INC DE
    INC HL
    DEC B
    LD A,B
    CP 0    
    JR NZ, COPY
    RET

; -------------------------------------------------------------
; Sprite Streaming Routine
; -------------------------------------------------------------
SPRITE8BITS:
    LD A, 2                 ; Command = transfer sprite
    LD (REG_TCOMMAND), A 
    
    ; Parameter = sprite ID passed via internal tracker registers
    ; Assuming standard conversion sequence
    LD A, 0
    LD (REG_TPARAMS), A
    LD A, 16
    LD (REG_TPARAMS), A 
    LD A, 24
    LD (REG_TPARAMS), A

    LD HL, (SPRITE_PTR_RAM)
    PUSH HL                 ; Preserve address references

    LD B, 0                 ; Run up 256 byte streaming loop
SPRITE80:
    LD A, (HL)
    LD (REG_TDATA), A
    INC HL
    DJNZ SPRITE80           ; Loops exactly 256 times equivalent to standard page loop

    ; Tail block loop handling (128 bytes remainder stream execution)
    LD B, 128
SPRITE81:
    LD A, (HL)
    LD (REG_TDATA), A
    INC HL
    DJNZ SPRITE81

    POP HL                  ; Restore tracking variables 
    LD (SPRITE_PTR_RAM), HL
    RET


; =============================================================================
; Data Blocks & Tables
; =============================================================================
POSX: DB $00
POSY: DB $20

SPRITE_PTR_RAM:
    DW 0

SINE:
    DB $80, $83, $86, $89, $8C, $90, $93, $96
    DB $99, $9C, $9F, $A2, $A5, $A8, $AB, $AE
    DB $B1, $B3, $B6, $B9, $BC, $BF, $C1, $C4
    DB $C7, $C9, $CC, $CE, $D1, $D3, $D5, $D8
    DB $DA, $DC, $DE, $E0, $E2, $E4, $E6, $E8
    DB $EA, $EB, $ED, $EF, $F0, $F1, $F3, $F4
    DB $F5, $F6, $F8, $F9, $FA, $FA, $FB, $FC
    DB $FD, $FD, $FE, $FE, $FE, $FF, $FF, $FF
    DB $FF, $FF, $FF, $FF, $FE, $FE, $FE, $FD
    DB $FD, $FC, $FB, $FA, $FA, $F9, $F8, $F6
    DB $F5, $F4, $F3, $F1, $F0, $EF, $ED, $EB
    DB $EA, $E8, $E6, $E4, $E2, $E0, $DE, $DC
    DB $DA, $D8, $D5, $D3, $D1, $CE, $CC, $C9
    DB $C7, $C4, $C1, $BF, $BC, $B9, $B6, $B3
    DB $B1, $AE, $AB, $A8, $A5, $A2, $9F, $9C
    DB $99, $96, $93, $90, $8C, $89, $86, $83
    DB $80, $7D, $7A, $77, $74, $70, $6D, $6A
    DB $67, $64, $61, $5E, $5B, $58, $55, $52
    DB $4F, $4D, $4A, $47, $44, $41, $3F, $3C
    DB $39, $37, $34, $32, $2F, $2D, $2B, $28
    DB $26, $24, $22, $20, $1E, $1C, $1A, $18
    DB $16, $15, $13, $11, $10, $0F, $0D, $0C
    DB $0B, $0A, $08, $07, $06, $06, $05, $04
    DB $03, $03, $02, $02, $02, $01, $01, $01
    DB $01, $01, $01, $01, $02, $02, $02, $03
    DB $03, $04, $05, $06, $06, $07, $08, $0A
    DB $0B, $0C, $0D, $0F, $10, $11, $13, $15
    DB $16, $18, $1A, $1C, $1E, $20, $22, $24
    DB $26, $28, $2B, $2D, $2F, $32, $34, $37
    DB $39, $3C, $3F, $41, $44, $47, $4A, $4D
    DB $4F, $52, $55, $58, $5B, $5E, $61, $64
    DB $67, $6A, $6D, $70, $74, $77, $7A, $7D

SPRITE_MARIO:
    DB $00,$00,$00,$00,$00,$00,$24,$24,$24,$24,$24,$00,$00,$00,$00,$00
    DB $00,$00,$00,$00,$24,$24,$e5,$e1,$e9,$f9,$e9,$24,$00,$00,$00,$00
    DB $00,$00,$00,$24,$e1,$e1,$a1,$a1,$d0,$f9,$ff,$24,$00,$00,$00,$00
    DB $00,$00,$24,$a1,$e5,$a1,$81,$24,$24,$24,$24,$24,$24,$00,$00,$00
    DB $00,$24,$a5,$a1,$81,$24,$24,$24,$24,$24,$24,$24,$24,$24,$00,$00
    DB $00,$24,$f6,$24,$24,$44,$e9,$24,$e9,$24,$ed,$00,$00,$00,$00,$00
    DB $24,$f6,$8d,$f6,$24,$e9,$f6,$24,$f6,$24,$f6,$8d,$6c,$00,$00,$00
    DB $24,$e9,$88,$f6,$24,$24,$fa,$f6,$fa,$f6,$fa,$f6,$f6,$68,$00,$00
    DB $24,$40,$e9,$f6,$24,$f6,$f6,$44,$e9,$ed,$e9,$ed,$e9,$68,$00,$00
    DB $00,$24,$40,$e9,$ed,$f6,$44,$20,$20,$20,$20,$20,$24,$00,$00,$00
    DB $00,$00,$24,$84,$e9,$e9,$e9,$e9,$20,$24,$24,$24,$00,$00,$00,$00
    DB $00,$00,$68,$81,$84,$84,$88,$88,$68,$29,$24,$00,$00,$00,$00,$00
    DB $00,$00,$68,$a5,$a5,$e9,$68,$4e,$7a,$7a,$29,$24,$24,$00,$00,$00
    DB $00,$24,$68,$ff,$ff,$ff,$8d,$ff,$ff,$9b,$ff,$49,$24,$24,$00,$00
    DB $24,$64,$88,$ff,$ff,$8d,$4e,$ff,$ff,$9b,$ff,$24,$88,$88,$24,$00
    DB $88,$88,$68,$ff,$ff,$68,$2e,$4e,$52,$7a,$29,$20,$88,$88,$24,$ff

    DB $88,$88,$49,$68,$6c,$2e,$2e,$2e,$2e,$09,$ff,$88,$88,$88,$00,$00
    DB $24,$88,$88,$24,$24,$09,$05,$05,$09,$24,$24,$88,$88,$24,$00,$00
    DB $00,$24,$24,$00,$00,$00,$00,$00,$00,$00,$00,$24,$24,$00,$00,$00
    DB $00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00
    DB $00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00
    DB $00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00
    DB $00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00
    DB $00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00

TOP:
