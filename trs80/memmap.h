#ifndef MEMMAP_H
#define MEMMAP_H


#define CPU_EMU       1
//#define CPU_Z80       1
//#define TRS_48K       1
//#define TRS_16K       1
//#define TRS_4K        1

//#define BUS_DEBUG     1
//#define HAS_ASMIRQ    1


#include "stdint.h"

//
// e000-ffff: memory map GFX+SOUND expansion
//
// RESOLUTION:
// 640x200 HI_RES  
// 320x200 LO_RES  
// 256x200 GAME_RES (New resolution suitable for 80's ARCADE games)
//
// LAYERING:
// - background color
// - L0 = bitmap (1) or tile 8x8/16x16 (scollable) 
// - L1 = petfont 8x8 or tile 8x8/16x16 (scollable)
// - L2 = sprites (with H/V flip)
// (1) 320x200 also in HIRES

//
// e000-e7ff: first byte must be 0xff (top ram)
//            rest can be anything as PRG browser/loader code 
// e800-efff: videomem expanded tiles map in L1 (tmap1)
// f000-f7ff: videomem expanded tiles map in L0 (tmap0)
//            tile id 0-255 in 8x8   tile mode
//            tile id 0-63  in 16x16 tile mode
//
// f800-f9ff: MAX 128 sprites registers (id,xhi,xlo,y)
//       id:    0-5 (max 63)
//       hflip: 6
//       vflip: 7
//
// fa00-faff: transfer lookup (if used as color palette, 256 RGB332 colors)
//       RGB332
//       R: 5-7, 0x20 -> 0xe0   xxx --- --
//       G: 2-4, 0x04 -> 0x1c   --- xxx -- 
//       B: 0-1, 0x00 -> 0x03   --- --- xx    
//
// fb00: video mode 
//       0-1: resolution (0=640x200,1=320x200,2=256x200)
//
// fb01: background color (RGB332) // 64257-65536=-1279
//
// fb02: layers config
//       0: L0 on/off (1=on)
//       1: L1 on/off (1=on)
//       2: L2 on/off (1=on)
//       3: L2 sprites between L0 and L1 (0 = sprites top)
//       4: bitmap/tile in L0 (0=bitmap)
//       5: petfont/tile in L1 (0=font)
//       6: enable scroll area in L0
//       7: enable scroll area in L1
//
// fb03: lines config 2
//       0: single/perline background color
//       1: single/perline L0 xscroll
//       2: single/perline L1 xscroll
//
// fb04: xscroll HI registers
//       3-0: L0 xscroll HI
//       7-4: L1 xscroll HI
// fb05: L0 xscroll LO
// fb06: L1 xscroll LO
// fb07: L0 yscroll
// fb08: L1 yscroll
// fb09: L0 scroll area's line start (0-24)
//       4-0
// fb0a: L0 scroll area's line end
//       4-0
// fb0b: L1 scroll area's line start (0-24)
//       4-0
// fb0c: L1 scroll area's line end
//       4-0
//
// fb0d: foreground color (RGB332)
//
// fb0e: tiles config
//       0: L0: 0=8x8, 1=16x16
//       1: L1: 0=8x8, 1=16x16
//       2-4: xcurtain
//            0: on/off
//            1: 8/16 pixels left
//       5-7: ycurtain
//            0: on/off
//            1: 8/16 pixels top
//
// fb0f: vsync line (0-200, 200 is overscan)
//
// fb10: 3-0: transfer mode (WR)
//       1/2/4/8 bits per pixel (using indexed CLUT)
//       9 = 8 bits RGB332 no CLUT
//       0 = compressed
// fb11: transfer command (WR)
//       0: idle
//       1: transfer tiles data      (data=tilenr,w,h,packet pixels)
//       2: transfer sprites data    (data=spritenr,w,h,packet pixels)
//       3: transfer bitmap data     (data=xh,xl,y,wh,wl,h,w*h/packet pixels) 
//       4: transfer t/fmap col data (data=layer,col,row,size,size/packet tiles)
//       5: transfer t/fmap row data (data=layer,col,row,size,size/packet tiles)
//       6: transfer all tile 8bits data compressed (data=sizeh,sizel,pixels)
//       7: transfer all sprite 8bits data compressed (data=sizeh,sizel,pixels)
//       8: transfer bitmap 8bits data compressed (data=sizeh,sizel,pixels)
//       9: transfer font 1bit data, 8bits a time (data=sizeh,sizel,pixels)  
//
// fb12: transfer params (WR)
// fb13: transfer data   (RD/WR)
// fb14: transfer status (RD) 1=ready
//
// Redefining tiles/sprite sequence
// 1. write lookup palette entries needed
// 2. write transfer mode (1/2/4/8/9)
// 3. write command 1/2
// 4. write params tile/sprite id,w,h
// 5. write data sequence (8bytes*plane for tiles, (h*2)bytes*plane for sprites)
// (any new command to reset)
//
// Transfer bitmap sequence
// 1. write lookup palette entries needed
// 2. write transfer mode (1/2/4/8/9)
// 3. write command 3
// 4. write params XH,XL,Y,WH,WL,H
// 5. write data sequence (bytes*plane /packed_bits)
// (any new command to reset)
//
// 9b15: PAL/NTSC (for PAL machine only)
// 0 = PAL
// 1 = NTSC
//
// fb18-fb37: 32 (re)SID registers (d400 on C64)

// fb38-fbff: lines background color (RGB332)
// fc00-fcc7: 7-4:  lines L1 xscroll HI, 3-0: L0 xscroll HI
// fcc8-fd8F: lines L0 xscroll LO
// fd90-fe58: lines L1 xscroll LO
//
// sprite collision only for first 16 sprites against all
// 8bits x 128 entries
// ff00-ff7f: Sprite collision LO (first 8 sprites, bit0 = sprite 0)
// ff80-ffff: Sprite collision HI (last  8 sprites, bit0 = sprite 8)
// 
#define REG_TEXTMAP_L1    (0xe000 - 0xe000) // 32768
#define REG_TILEMAP_L1    (0xe800 - 0xe000) // 34816
#define REG_TILEMAP_L0    (0xf000 - 0xe000) // 36864
#define REG_SPRITE_IND    (0xf800 - 0xe000) // 38912
#define REG_SPRITE_XHI    (0xf880 - 0xe000) // 39040
#define REG_SPRITE_XLO    (0xf900 - 0xe000) // 39168
#define REG_SPRITE_Y      (0xf980 - 0xe000) // 39296
#define REG_TLOOKUP       (0xfa00 - 0xe000) // 39424
#define REG_VIDEO_MODE    (0xfb00 - 0xe000) // 39680
#define REG_BG_COL        (0xfb01 - 0xe000) // 39681 // POKE -1279,color
#define REG_LAYERS_CFG    (0xfb02 - 0xe000) // 39682
#define REG_LINES_CFG     (0xfb03 - 0xe000) // 39683
#define REG_XSCROLL_HI    (0xfb04 - 0xe000) // 39684
#define REG_XSCROLL_L0    (0xfb05 - 0xe000) // 39685
#define REG_XSCROLL_L1    (0xfb06 - 0xe000) // 39686
#define REG_YSCROLL_L0    (0xfb07 - 0xe000) // 39687
#define REG_YSCROLL_L1    (0xfb08 - 0xe000) // 39688
#define REG_SC_START_L0   (0xfb09 - 0xe000) // 39689
#define REG_SC_END_L0     (0xfb0a - 0xe000) // 39690
#define REG_SC_START_L1   (0xfb0b - 0xe000) // 39691
#define REG_SC_END_L1     (0xfb0c - 0xe000) // 39692
#define REG_FG_COL        (0xfb0d - 0xe000) // 39693
#define REG_TILES_CFG     (0xfb0e - 0xe000) // 39694
#define REG_VSYNC         (0xfb0f - 0xe000) // 39695

#define REG_TDEPTH        (0xfb10 - 0xe000) // 39696
#define REG_TCOMMAND      (0xfb11 - 0xe000) // 39697
#define REG_TPARAMS       (0xfb12 - 0xe000) // 39698
#define REG_TDATA         (0xfb13 - 0xe000) // 39699
#define REG_TSTATUS       (0xfb14 - 0xe000) // 39700
#define REG_PALNTSC       (0xfb15 - 0xe000) // 39700

#define REG_SID_BASE      (0xfb18 - 0xe000) // 39704

#define REG_LINES_BG_COL  (0xfb38 - 0xe000) // 39736
#define REG_LINES_XSCR_HI (0xfc00 - 0xe000) // 39936
#define REG_LINES_L0_XSCR (0xfcc8 - 0xe000) // 40136
#define REG_LINES_L1_XSCR (0xfd90 - 0xe000) // 40336
#define REG_SPRITE_COL_LO (0xff00 - 0xe000) // 40704
#define REG_SPRITE_COL_HI (0xff80 - 0xe000) // 40832

#define GET_VIDEO_MODE    ( gfxmem[REG_VIDEO_MODE] )
#define GET_BG_COL        ( gfxmem[REG_BG_COL] )
#define GET_FG_COL        ( gfxmem[REG_FG_COL] )
#define GET_XSCROLL_L0    ( gfxmem[REG_XSCROLL_L0] | ((gfxmem[REG_XSCROLL_HI] & 0x0f)<<8) )
#define GET_YSCROLL_L0    ( gfxmem[REG_YSCROLL_L0] )
#define GET_XSCROLL_L1    ( gfxmem[REG_XSCROLL_L1] | ((gfxmem[REG_XSCROLL_HI] & 0xf0)<<4) )
#define GET_YSCROLL_L1    ( gfxmem[REG_YSCROLL_L1] )
#define GET_SC_START_L0   ( gfxmem[REG_SC_START_L0] & 31 )
#define GET_SC_END_L0     ( gfxmem[REG_SC_END_L0] & 31 )
#define GET_SC_START_L1   ( gfxmem[REG_SC_START_L1] & 31 )
#define GET_SC_END_L1     ( gfxmem[REG_SC_END_L1] & 31 )
#define GET_TSTATUS       ( gfxmem[REG_TSTATUS] )

#define SET_VIDEO_MODE(x) { gfxmem[REG_VIDEO_MODE] = x; }
#define SET_BG_COL(x)     { gfxmem[REG_BG_COL] = x; }
#define SET_FG_COL(x)     { gfxmem[REG_FG_COL] = x; }
#define SET_XSCROLL_L0(x) { gfxmem[REG_XSCROLL_L0] = x & 0xff; gfxmem[REG_XSCROLL_HI] &= 0xf0; gfxmem[REG_XSCROLL_HI] |= (x>>8); }
#define SET_XSCROLL_L1(x) { gfxmem[REG_XSCROLL_L1] = x & 0xff; gfxmem[REG_XSCROLL_HI] &= 0x0f; gfxmem[REG_XSCROLL_HI] |= ((x>>4)&0xf0); }
#define SET_YSCROLL_L0(x) { gfxmem[REG_YSCROLL_L0] = x; }
#define SET_YSCROLL_L1(x) { gfxmem[REG_YSCROLL_L1] = x; }
#define SET_SC_START_L0(x) { gfxmem[REG_SC_START_L0] = x & 31; }
#define SET_SC_END_L0(x)  { gfxmem[REG_SC_END_L0] = x & 31; }
#define SET_SC_START_L1(x) { gfxmem[REG_SC_START_L1] = x & 31; }
#define SET_SC_END_L1(x)  { gfxmem[REG_SC_END_L1] = x & 31; }

#define SET_LAYER_MODE(x) { gfxmem[REG_LAYERS_CFG] = x; }
#define SET_LINE_MODE(x)  { gfxmem[REG_LINES_CFG] = x; }
#define SET_TILE_MODE(x)  { gfxmem[REG_TILES_CFG] = x; }


#define VIDEO_MODE_HIRES  ( gfxmem[REG_VIDEO_MODE] == 0 )  
#define LAYER_L0_ENA      ( gfxmem[REG_LAYERS_CFG] & 0x1  )  
#define LAYER_L1_ENA      ( gfxmem[REG_LAYERS_CFG] & 0x2  )  
#define LAYER_L2_ENA      ( gfxmem[REG_LAYERS_CFG] & 0x4  )
#define L2_BETWEEN_ENA    ( gfxmem[REG_LAYERS_CFG] & 0x8  )
#define L0_TILE_ENA       ( gfxmem[REG_LAYERS_CFG] & 0x10 )  
#define L1_TILE_ENA       ( gfxmem[REG_LAYERS_CFG] & 0x20 )
#define L0_AREA_ENA       ( gfxmem[REG_LAYERS_CFG] & 0x40 )   
#define L1_AREA_ENA       ( gfxmem[REG_LAYERS_CFG] & 0x80 )   

#define BG_COL_LINE_ENA   ( gfxmem[REG_LINES_CFG]  & 0x01 )
#define L0_XSCR_LINE_ENA  ( gfxmem[REG_LINES_CFG]  & 0x02 )
#define L1_XSCR_LINE_ENA  ( gfxmem[REG_LINES_CFG]  & 0x04 )

#define L0_TILE_16_ENA    ( gfxmem[REG_TILES_CFG]  & 0x01 )
#define L1_TILE_16_ENA    ( gfxmem[REG_TILES_CFG]  & 0x02 )
#define HCURTAIN8_ENA     ( (gfxmem[REG_TILES_CFG] & (0x04+0x08)) == (0x04) )
#define HCURTAIN16_ENA    ( (gfxmem[REG_TILES_CFG] & (0x04+0x08)) == (0x04+0x08) )
#define VCURTAIN8_ENA     ( (gfxmem[REG_TILES_CFG] & (0x20+0x40)) == (0x20) )
#define VCURTAIN16_ENA    ( (gfxmem[REG_TILES_CFG] & (0x20+0x40)) == (0x20+0x40) )

#define LAYER_L0_BITMAP   ( 0x1 )  
#define LAYER_L0_TILE     ( 0x1 | 0x10 )  
#define LAYER_L1_PETFONT  ( 0x2 )  
#define LAYER_L1_TILE     ( 0x2 | 0x20)
#define LAYER_L2_SPRITE   ( 0x4 )
#define LAYER_L2_INBETW   ( 0x8 )
#define LAYER_L0_AREA     ( 0x40)
#define LAYER_L1_AREA     ( 0x80)

#define LINE_BG_COL       ( 0x1 )
#define LINE_L0_XSCR      ( 0x2 )
#define LINE_L1_XSCR      ( 0x4 )

#define L0_TILE_16        ( 0x1 )  
#define L1_TILE_16        ( 0x2 )  
#define HCURTAIN_8        ( 0x4 )  
#define HCURTAIN_16       ( 0x4 + 0x8 ) 
#define VCURTAIN_8        ( 0x20 )  
#define VCURTAIN_16       ( 0x20 + 0x40 )  

extern uint8_t memory[];

extern void start_system(void);


#endif
