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



dw code_start

;ORG   $e001   
ORG   $5000   

code_start: 
;JP   tra_bitmap
JP   tra_packed_bitmap


;ds 0x5100 - $
;ORG   $5100


; #######################################@
; Draw rectangles in bitmap GFX
; #######################################@

tra_bitmap:
;ld a, $80
;ld (REG_BG_COL), a

        ld a, 7 ;4+2+1
        ld (REG_LAYERS_CFG), a

        ;ld a, 2
        ;ld (REG_LAYERS_CFG), a

        ld b,200
        ld c,200
        ld d,$ff
        ld hl,200*200
        call rect
        ld b,150
        ld c,150
        ld d,$f8
        ld hl,150*150
        call rect
        ld b,100
        ld c,100
        ld d,$f0
        ld hl,100*100
        call rect
        ld b,70
        ld c,70
        ld d,$e0
        ld hl,70*70
        call rect
        ld b,50
        ld c,50
        ld d,$c0
        ld hl,200*200
        call rect

        jp exit




; #######################################@
; Decompress bitmap
; #######################################@

tra_packed_bitmap:
        ld a, 2                 ; Hide GFX layer
        ld (REG_LAYERS_CFG), a

        ld a, 0                 ; transfer mode: 8 bits packed
        ld (REG_TDEPTH), a
        ld a, 8                 ; command = transfer packed bitmap
        ld (REG_TCOMMAND), a

        ld   de,bitmap
        ld   hl,bitmap_end
        sbc  hl,de
        ld   a,h
        ld (REG_TPARAMS), a
        ld   a,l
        ld (REG_TPARAMS), a

copy1:
	ld   a,(de)
        ld (REG_TDATA), a
	inc de
    	dec hl
    	ld a,h
    	or l
    	jp nz,copy1  


        ld a, 7 ;4+2+1          ; Show GFX
        ld (REG_LAYERS_CFG), a

exit:
;ld a, $80
;ld (REG_BG_COL), a
        ret


; #######################################@
; Draw a colored rectangle
; #######################################@

rect:
        ld a, 9                 ; transfer mode: 8 bits nolut
        ld (REG_TDEPTH), a
        ld a, 3                 ; command = transfer bitmap
        ld (REG_TCOMMAND), a
        ld a,0
        ld (REG_TPARAMS), a     ; x hi
        ld (REG_TPARAMS), a     ; x lo 
        ld (REG_TPARAMS), a     ; y
        ld a,0                  ; w hi
        ld (REG_TPARAMS), a     
        ld a,b                  ; w lo
        ld (REG_TPARAMS), a
        ld a,c                  ; h
        ld (REG_TPARAMS), a
rect0:
        ld a,d
        ld (REG_TDATA), a
        dec hl
        ld a,h
        or l
        jp nz,rect0
        ret






bitmap:
incbin 'bmp_starwars1.cru'
;incbin 'bmp_starwars2.cru'
;incbin 'bmp_darkvador.cru'
;incbin 'bmp_bobafat.cru'
;incbin 'bmp_jabba.cru'
;incbin 'bmp_padawan.cru'
bitmap_end:




