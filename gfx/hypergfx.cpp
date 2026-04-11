#include "pico.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

extern "C" {
  #include "iopins.h"   
}

#include "hdmi_framebuffer.h"
#include "memmap.h"
#include "gfx.h"
#include "hypergfx.h"

#ifdef HAS_SND
#include "reSID.h"
#endif

#ifdef PET
#include "petfont.h"
#endif
#ifdef TRS
#include "font/font_m3"
#include "font/font_m4"
#include "trs_memory.h"
#endif

#ifdef HAS_USBHOST
#include "kbd.h"
#endif

#define ALIGNED __attribute__((aligned(4)))

// Graphics
#define VGA_RGB(r,g,b)   ( (((r>>5)&0x07)<<5) | (((g>>5)&0x07)<<2) | (((b>>6)&0x3)<<0) )

// screen resolution
#define HI_XRES        640
#define LO_XRES        320
#define GAME_XRES      256 

#define MAXWIDTH       640            // Max screen width
#define MAXHEIGHT      200            // Max screen height

// Sprite definition
#define SPRITEW        16             // Sprite width
#define SPRITEH_MAX    24             // Sprite height max
#define SPRITE_SIZE    (SPRITEW*SPRITEH_MAX)
#define SPRITE_NUM     16             // Max Sprite NR on same row
#define SPRITE_NUM_MAX 96             // Max reusable sprites NR 
#define SPRITE_NBTILES 64             // Sprite NB of tiles definition

// Tile definition
#define TILEW          8              // Tile width
#define TILEH          8              // Tile height
#define TILE_SIZE      (TILEW*TILEH)

#define TILE_NB_BANKS  2              // Number of banks (hack form bitmap unpack)
// Tile data max size
#define TILE_MAXDATA   (8*8*256*TILE_NB_BANKS)  // (or 16*16*64*TILE_NB_BANKS)
#define TILEMAP_SIZE   0x800

// Font
#define FONTW 8                       // Font width
#define FONTH 8                       // Font height
#define FONT_SIZE (2*2048)            // Dual font upper and lower case

// Others
#define GFX_MARGIN     128


// Screen resolution
static uint8_t video_default = VMODE_HIRES;
static int video_mode;

// Frame buffer
static ALIGNED uint8_t Bitmap[LO_XRES*MAXHEIGHT+GFX_MARGIN];
// Sprites
struct Sprite {
   uint8_t  y;
   uint8_t  h;
   uint8_t  flipH;
   uint8_t  flipV;
   uint16_t x;
   uint16_t dataIndex;
 };

struct SprYSortedItem {
   uint8_t count;
   uint8_t first;
};  

static Sprite SpriteParams[SPRITE_NUM_MAX];
static SprYSortedItem SpriteYSorted[256];
static uint8_t SprYSortedIndexes[256];
static uint8_t SprYSortedIds[SPRITE_NUM_MAX+1];
static uint8_t SpriteDataW[SPRITE_NBTILES];
static uint8_t SpriteDataH[SPRITE_NBTILES];
static uint16_t SpriteIdToDataIndex[SPRITE_NBTILES];
static uint16_t SpriteDataIndex=0;

// Sprite definition
static ALIGNED uint8_t SpriteData[SPRITE_SIZE*SPRITE_NBTILES+GFX_MARGIN];

// Tile definition
static ALIGNED uint8_t TileData[TILE_MAXDATA+GFX_MARGIN];

// Font definition (dual upper and lower case)
static uint8_t font[FONT_SIZE];

// GFX shadow memory 8000-9fff
#ifdef PET
static unsigned char mem_io[0x2000];
static unsigned char *gfxmem=&mem_io[0];
#endif
#ifdef TRS
static unsigned char *gfxmem=&memory[0xe000];
#endif

#ifdef PET
bool font_lowercase = false;
#endif
#ifdef TRS
bool font_reversed = false;
#endif
static uint16_t screen_width=640;
static bool gfx_reset=false; 

#ifdef HAS_SND
// ****************************************
// Audio code
// ****************************************
static AudioPlaySID playSID;
static uint8_t prev_sid_reg[32];

static void audio_fill_buffer( audio_sample * stream, int len )
{
  playSID.update(SOUNDRATE, (void *)stream, len);
}

static void sid_dump( void )
{
  //memcpy((void *)&buffer[0], (void *)&mem[REG_SID_BASE], 32);
  for(int i=0;i<32;i++) 
  {
    uint8_t reg = gfxmem[REG_SID_BASE+i];
    if(reg != prev_sid_reg[i]) {       
        playSID.setreg(i, reg);
        prev_sid_reg[i] = reg;                  
    } 
  }
}
#endif

// ****************************************
// Setup Video mode
// ****************************************
static void __not_in_flash("VideoRenderLineBG") VideoRenderLineBG(uint8_t * linebuffer, int scanline)
{
  uint32_t * dst32 = (uint32_t *)linebuffer;      
#ifdef NO_HYPER
  // Background color
  uint32_t bgcolor32 = 0;
#else
  // Background color
  uint32_t bgcolor32 = (gfxmem[REG_BG_COL]<<24)+(gfxmem[REG_BG_COL]<<16)+(gfxmem[REG_BG_COL]<<8)+(gfxmem[REG_BG_COL]);
  if ( BG_COL_LINE_ENA ) 
  {
    uint8_t bgcol = gfxmem[REG_LINES_BG_COL+scanline];
    bgcolor32 = (bgcol<<24)+(bgcol<<16)+(bgcol<<8)+(bgcol);
  }
#endif   
  RenderColor(dst32, bgcolor32, (screen_width/4));
}

static void __not_in_flash("VideoRenderLineL0") VideoRenderLineL0(uint8_t * linebuffer, int scanline)
{
#ifdef NO_HYPER
#else  
  // Layer 0
  if (LAYER_L0_ENA)
  {
    scanline = (scanline + GET_YSCROLL_L0) % MAXHEIGHT;
    int scroll = L0_XSCR_LINE_ENA?GET_XSCROLL_L0+( gfxmem[REG_LINES_L0_XSCR+scanline] | ((gfxmem[REG_LINES_XSCR_HI+scanline] & 0x0f)<<8) ):GET_XSCROLL_L0;
    if ( L0_TILE_ENA )
    {
      uint8_t bgcolor = GET_BG_COL;
      if (L0_TILE_16_ENA) 
      {
        unsigned char * tilept = &gfxmem[(scanline >> 4)*(screen_width >> 4)+REG_TILEMAP_L0];
        uint8_t * src = &TileData[(scanline&15) << 4];
        int screen_width_in_tiles = screen_width >> 4;
        if (screen_width_in_tiles == 16) { 
          TileBlitKey16_16(linebuffer, tilept, screen_width_in_tiles, bgcolor, src, scroll&15, (scroll>>3)%16);
        }
        else if (screen_width_in_tiles == 20) { 
          TileBlitKey16_20(linebuffer, tilept, screen_width_in_tiles, bgcolor, src, scroll&15, (scroll>>3)%20);
        }
        else {   
          TileBlitKey16_40(linebuffer, tilept, screen_width_in_tiles, bgcolor, src, scroll&15, (scroll>>3)%40);
        }
      }
      else
      {
        unsigned char * tilept = &gfxmem[(scanline >> 3)*(screen_width >> 3)+REG_TILEMAP_L0];
        uint8_t * src = &TileData[(scanline&7) << 3];
        int screen_width_in_tiles = screen_width >> 3;
        if (screen_width_in_tiles == 32) { 
          TileBlitKey8_32(linebuffer, tilept, screen_width_in_tiles, bgcolor, src, scroll&7, (scroll>>3)%32);
        }
        else if (screen_width_in_tiles == 40) { 
          TileBlitKey8_40(linebuffer, tilept, screen_width_in_tiles, bgcolor, src, scroll&7, (scroll>>3)%40);
        }
        else { 
          TileBlitKey8_80(linebuffer, tilept, screen_width_in_tiles, bgcolor, src, scroll&7, (scroll>>3)%80);
        }
      }
    }
    else
    {
      // Bitmap mode
      if (screen_width == GAME_XRES) {
        LineBlitKey32(linebuffer, &Bitmap[scanline*screen_width],screen_width, scroll&7, (scroll>>3)%32);
      }
      else if (screen_width == LO_XRES) {
        LineBlitKey40(linebuffer, &Bitmap[scanline*screen_width],screen_width, scroll&7, (scroll>>3)%40);
      }
      else { 
//        LineBlitKey80(linebuffer, &Bitmap[scanline*screen_width/2],screen_width/2, scroll&7, (scroll>>3)%40);
        LineBlit80(linebuffer, &Bitmap[scanline*screen_width/2],screen_width/2, scroll&7, (scroll>>3)%40);
      }
    }
  }

  // Sprites if in between L0 and L1
  if ( (LAYER_L2_ENA) && (L2_BETWEEN_ENA) ) 
  {
    Sprite16((SPRITE_NUM << 8) + SPRITE_NUM_MAX, screen_width, scanline, (uint8_t *)&SpriteParams[0], (uint8_t *)&SpriteData[0], linebuffer);    
  }
#endif    
}

static void __not_in_flash("VideoRenderLineL1") VideoRenderLineL1(uint8_t * linebuffer, int scanline)
{
#ifdef NO_HYPER
        int scroll = 0;
#ifdef PET    
        uint8_t fgcolor = 0x1c;
        int screen_width_in_chars = screen_width >> 3;
        unsigned char * charpt = &gfxmem[(scanline>>3)*screen_width_in_chars+REG_TEXTMAP_L1];    
        unsigned char * fontpt = &font[(scanline&0x7)*256+(font_lowercase?0x800:0x000)];
        if (screen_width_in_chars == 32) { 
          TextBlitKey32(linebuffer, charpt, screen_width_in_chars, fgcolor, fontpt, scroll&7, (scroll>>3)%40);          
        }
        else if (screen_width_in_chars == 40) { 
          TextBlitKey40(linebuffer, charpt, screen_width_in_chars, fgcolor, fontpt, scroll&7, (scroll>>3)%40);          
        }
        else { 
  //        TextBlit80(linebuffer, charpt, screen_width_in_chars/2, &fgcolorlut[0], fontpt, scroll&7, (scroll>>3)%40);          
          TextBlitKey80(linebuffer, charpt, screen_width_in_chars, fgcolor, fontpt, scroll&7, (scroll>>3)%40);          
        }
#endif
#ifdef TRS
        uint8_t fgcolor = 0xff;
        int screen_width_in_chars = 64; //screen_width >> 3;{ 
        unsigned char * charpt = &memory[VIDEO_START + (scanline/12)*screen_width_in_chars];    
        unsigned char * fontpt = &font[(scanline%12)*256+(font_reversed?0x800:0x000)];
        TextBlitKey64(linebuffer+64, charpt, screen_width_in_chars, fgcolor, fontpt, scroll&7, (scroll>>3)%40);          
#endif      
#else
  // Curtain V
  if ( ( (scanline < 8) && ( VCURTAIN8_ENA ) ) || ( (scanline < 16) && ( VCURTAIN16_ENA ) ) ) 
  {
    uint32_t * dst32 = (uint32_t *)linebuffer;      
    RenderColor(dst32, 0, (screen_width/4));
  }
  else
  {
    if (LAYER_L1_ENA)
    {
      scanline = (scanline + GET_YSCROLL_L1) % MAXHEIGHT;    
      if ( L1_TILE_ENA )
      {
        int scroll = L1_XSCR_LINE_ENA?GET_XSCROLL_L1+( gfxmem[REG_LINES_L1_XSCR+scanline] | ((gfxmem[REG_LINES_XSCR_HI+scanline] & 0x0f)<<8) ):GET_XSCROLL_L1;
        if (L1_TILE_16_ENA) 
        {
          unsigned char * tilept = &gfxmem[(scanline >> 4)*(screen_width >> 4)+REG_TILEMAP_L1];
          uint8_t * src = &TileData[(scanline&15) << 4];
          int screen_width_in_tiles = screen_width >> 4;
          if (screen_width_in_tiles == 16) {
            TileBlitKey16_16(linebuffer, tilept, screen_width_in_tiles, 0, src, scroll&15, (scroll>>3)%16);
          }
          else if (screen_width_in_tiles == 20) {
            TileBlitKey16_20(linebuffer, tilept, screen_width_in_tiles, 0, src, scroll&15, (scroll>>3)%20);
          }
          else {  
            TileBlitKey16_40(linebuffer, tilept, screen_width_in_tiles, 0, src, scroll&15, (scroll>>3)%40);
          }
        }
        else
        {
          unsigned char * tilept = &gfxmem[(scanline >> 3)*(screen_width >> 3)+REG_TILEMAP_L1];
          uint8_t * src = &TileData[(scanline&7) << 3];
          int screen_width_in_tiles = screen_width >> 3;
          if (screen_width_in_tiles == 32) {
            TileBlitKey8_32(linebuffer, tilept, screen_width_in_tiles, 0, src, scroll&7, (scroll>>3)%32);
          }
          else if (screen_width_in_tiles == 40) { 
            TileBlitKey8_40(linebuffer, tilept, screen_width_in_tiles, 0, src, scroll&7, (scroll>>3)%40);
          }
          else { 
            TileBlitKey8_80(linebuffer, tilept, screen_width_in_tiles, 0, src, scroll&7, (scroll>>3)%80);
          }
        }
      }
      else
      {
#ifdef PET   
        uint8_t fgcolor = GET_FG_COL;
        int screen_width_in_chars = screen_width >> 3;
        unsigned char * charpt = &gfxmem[(scanline>>3)*screen_width_in_chars+REG_TEXTMAP_L1];    
        unsigned char * fontpt = &font[(scanline&0x7)*256+(font_lowercase?0x800:0x000)];
        int scroll = L1_XSCR_LINE_ENA?GET_XSCROLL_L1+( gfxmem[REG_LINES_L1_XSCR+scanline] | ((gfxmem[REG_LINES_XSCR_HI+scanline] & 0xf0)<<4) ):GET_XSCROLL_L1;
        if (screen_width_in_chars == 32) { 
          TextBlitKey32(linebuffer, charpt, screen_width_in_chars, fgcolor, fontpt, scroll&7, (scroll>>3)%40);          
        }
        else if (screen_width_in_chars == 40) { 
          TextBlitKey40(linebuffer, charpt, screen_width_in_chars, fgcolor, fontpt, scroll&7, (scroll>>3)%40);          
        }
        else { 
  //        TextBlit80(linebuffer, charpt, screen_width_in_chars/2, &fgcolorlut[0], fontpt, scroll&7, (scroll>>3)%40);          
          TextBlitKey80(linebuffer, charpt, screen_width_in_chars, fgcolor, fontpt, scroll&7, (scroll>>3)%40);          
        }
#endif
#ifdef TRS
        uint8_t fgcolor = GET_FG_COL;
        int screen_width_in_chars = 64; //screen_width >> 3;{ 
        unsigned char * charpt = &memory[VIDEO_START + (scanline/12)*screen_width_in_chars];    
        unsigned char * fontpt = &font[(scanline%12)*256+(font_reversed?0x800:0x000)];
        int scroll = L1_XSCR_LINE_ENA?GET_XSCROLL_L1+( gfxmem[REG_LINES_L1_XSCR+scanline] | ((gfxmem[REG_LINES_XSCR_HI+scanline] & 0xf0)<<4) ):GET_XSCROLL_L1;
        TextBlitKey64(linebuffer+64, charpt, screen_width_in_chars, fgcolor, fontpt, scroll&7, (scroll>>3)%40);          
#endif        
      }
    } 

    // Layer 2
    if ( (LAYER_L2_ENA) && (!L2_BETWEEN_ENA) )
    {
      Sprite16((SPRITE_NUM << 8) + SPRITE_NUM_MAX, screen_width, scanline, (uint8_t *)&SpriteParams[0], (uint8_t *)&SpriteData[0], linebuffer); 
    }

    // Curtain H
    uint32_t color32 = 0x00000000;
    uint32_t * dst32 = (uint32_t *)linebuffer;      
    if ( HCURTAIN8_ENA )
    {
      *dst32++=color32;
      *dst32=color32;
    }
    else if ( HCURTAIN16_ENA ) 
    {
      *dst32++=color32;
      *dst32++=color32;
      *dst32++=color32;
      *dst32=color32;
    }
  }
#endif  
}


// ****************************************
// HYPER command 
// ****************************************
#include <stdio.h>
#include <ctype.h>
#include "fatfs_disk.h"
#include "ff.h"

#include "decrunch.h"


// filesystem
static bool fatfs_mounted = false;
static FATFS filesystem;
static FATFS fatfs;
static FIL file; 
static DIR dir;
static FILINFO entry;
static FRESULT fres;
#define MAX_FILES           10
#define MAX_FILENAME_SIZE   20
#define FILE_IS_DIR         0
#define FILE_IS_PRG         1
#define FILE_IS_ROM         2

static int nbFiles=0;
static int file_block_wr_pt=0;
static char files[MAX_FILES][MAX_FILENAME_SIZE];
static char scratchpad[256]={0};


// command queue
#define CMD_QUEUE_SIZE 256
typedef struct {
   uint8_t  id;
   uint8_t  p8_1;
   uint16_t p16_1;
} QueueItem;

#define MAX_CMD 32
#define MAX_PAR 32
typedef enum {
  cmd_undef=0,
  cmd_transfer_tile_data=1,
  cmd_transfer_sprite_data=2,
  cmd_transfer_bitmap_data=3,
  cmd_transfer_tilemap_col=4,
  cmd_transfer_tilemap_row=5,
  cmd_transfer_packed_tile_data=6,
  cmd_transfer_packed_sprite_data=7,
  cmd_transfer_packed_bitmap_data=8,
  cmd_transfer_font=9,

  cmd_tiles_clr=11,
  cmd_bitmap_clr=12,
  cmd_bitmap_point=13,
  cmd_bitmap_rect=14,
  cmd_bitmap_tri=15,
  
  cmd_openfile=27,
  cmd_readfile=28,
  cmd_opendir=29,
  cmd_readdir=30,
  cmd_unused=31,
} Cmd;

static uint8_t cmd;
static uint8_t cmd_params[MAX_PAR];
static int tra_h;
static uint8_t cmd_param_ind;
static uint8_t cmd_tra_depth;
static uint8_t * tra_address;
static uint16_t tra_x;
static uint16_t tra_w;
static uint16_t tra_stride;
static uint8_t tra_spr_id;
static uint8_t * tra_pal = &gfxmem[REG_TLOOKUP];

static QueueItem cmd_queue[CMD_QUEUE_SIZE];
static uint8_t cmd_queue_rd=0;
static uint8_t cmd_queue_wr=0;
static uint8_t cmd_queue_cnt=0;

static int __not_in_flash("mystrncpy") mystrncpy( char* dst, const char* src, int n )
{
   int pos = 0;
   while( (pos<n) && (*src) )
   {
    *dst++ = toupper(*src++);
    pos++;
   }
   *dst++=0;
   return pos+1;
}

void __not_in_flash("HyperGfxHandleCmdQueue") HyperGfxHandleCmdQueue(void) {
#ifdef NO_HYPER
#else  
  unsigned int nbread; 
  if (cmd_queue_cnt)
  {
    QueueItem cmd = cmd_queue[cmd_queue_rd];
    cmd_queue_rd = (cmd_queue_rd + 1)&(CMD_QUEUE_SIZE-1);
    cmd_queue_cnt--;
    
    switch (cmd.id) {     
      case cmd_transfer_packed_tile_data:
        UnPack(0, &TileData[sizeof(TileData)-cmd.p16_1], &TileData[0], sizeof(TileData)-GFX_MARGIN);
        break;
      case cmd_transfer_packed_sprite_data:
        UnPack(0, &SpriteData[sizeof(SpriteData)-cmd.p16_1], &SpriteData[0], sizeof(SpriteData)-GFX_MARGIN);
        break;
      case cmd_transfer_packed_bitmap_data:
        UnPack(0, &Bitmap[sizeof(Bitmap)-cmd.p16_1], &Bitmap[0], sizeof(Bitmap)-GFX_MARGIN);
        //gfxmem[REG_BG_COL] = 0xff;
        break;      
      case cmd_tiles_clr:
        memset((void*)&TileData[0],0, sizeof(TileData));
        break;
      case cmd_bitmap_clr:
        memset((void*)&Bitmap[0],0, sizeof(Bitmap));
        break;
      case cmd_openfile:
        nbread = 0;
        if (fatfs_mounted) {
          strcat(scratchpad, "/");
          strcat(scratchpad, &files[cmd.p8_1][0]);
          if( !(f_open(&file, scratchpad , FA_READ)) ) {
            //f_read (&file, (void*)&gfxmem[REG_TLOOKUP+1], 255, &nbread);
            //if (!nbread) f_close(&file);
            nbread = 1;
          }
        }
        gfxmem[REG_TLOOKUP] = nbread;
        break;        
      case cmd_readfile:
        nbread = 0; 
        if (fatfs_mounted) {
          f_read (&file, (void*)&gfxmem[REG_TLOOKUP+1], cmd.p8_1, &nbread);
          if (!nbread) f_close(&file);
        }
        gfxmem[REG_TLOOKUP] = nbread;
        break;        
      case cmd_opendir:
        nbFiles = 0;
        if (fatfs_mounted) {
          file_block_wr_pt = 1;
          if (gfxmem[REG_TLOOKUP] > 0x7f) {
            gfxmem[REG_TLOOKUP] = 0;
          }  
          memcpy((void *)scratchpad, (void *)&gfxmem[REG_TLOOKUP], 256);
          f_closedir(&dir);
          fres = f_findfirst(&dir, &entry, scratchpad, "*");
          gfxmem[REG_TLOOKUP]=0xff;
          while ( (fres == FR_OK) && (entry.fname[0]) && (nbFiles<MAX_FILES) ) {  
            if (!entry.fname[0]) {
              f_closedir(&dir);
              break;
            }
            bool valid = true;
            char * filename = entry.fname;
            int size = mystrncpy(&files[nbFiles][0], filename, MAX_FILENAME_SIZE-1); // including eol (0)
            if (entry.fname[0] != '.' ) { // skip any MACOS file but also ".", ".."
              // not a directory
              if ( !(entry.fattrib & AM_DIR) ) {
#ifdef PET
                if ( (size > 4) && 
                     (filename[size-5] == '.' ) && 
                     (filename[size-4] == 'p' ) && 
                     (filename[size-3] == 'r' ) && 
                     (filename[size-2] == 'g' ) ) {
                  gfxmem[REG_TLOOKUP+file_block_wr_pt++] = FILE_IS_PRG;
                }
                else   
                if ( (size > 4) && 
                     (filename[size-5] == '.' ) && 
                     (filename[size-4] == 'b' ) && 
                     (filename[size-3] == 'i' ) && 
                     (filename[size-2] == 'n' ) ) {
                  gfxmem[REG_TLOOKUP+file_block_wr_pt++] = FILE_IS_ROM;
                }
#endif
#ifdef TRS
                if ( (size > 4) && 
                     (filename[size-5] == '.' ) && 
                     (filename[size-4] == 'c' ) && 
                     (filename[size-3] == 'm' ) && 
                     (filename[size-2] == 'd' ) ) {
                  gfxmem[REG_TLOOKUP+file_block_wr_pt++] = FILE_IS_PRG;
                }
#endif
                else {
                  valid = false;
                }
              }
              else {
                gfxmem[REG_TLOOKUP+file_block_wr_pt++] = FILE_IS_DIR;
              }
              if (valid == true) {
                file_block_wr_pt += mystrncpy((char*)&gfxmem[REG_TLOOKUP+file_block_wr_pt], filename, MAX_FILENAME_SIZE-1);
                nbFiles++;
              }  
            }               
            fres = f_findnext(&dir, &entry);
          }
        }
        gfxmem[REG_TLOOKUP] = nbFiles;
        break;
      case cmd_readdir:
        nbFiles = 0;
        if (fatfs_mounted) {
          file_block_wr_pt = 1;
          gfxmem[REG_TLOOKUP]=0xff;
          while ( (fres == FR_OK) && (entry.fname[0]) && (nbFiles<MAX_FILES) ) {  
            if (!entry.fname[0]) {
              f_closedir(&dir);
              break;
            }
            bool valid = true;
            char * filename = entry.fname;
            int size = mystrncpy(&files[nbFiles][0], filename, MAX_FILENAME_SIZE-1); // including eol (0)
            if (entry.fname[0] != '.' ) { // skip any MACOS file but also ".", ".."
              // not a directory
              if ( !(entry.fattrib & AM_DIR) ) {
#ifdef PET
                if ( (size > 4) && 
                     (filename[size-5] == '.' ) && 
                     (filename[size-4] == 'p' ) && 
                     (filename[size-3] == 'r' ) && 
                     (filename[size-2] == 'g' ) ) {
                  gfxmem[REG_TLOOKUP+file_block_wr_pt++] = FILE_IS_PRG;
                }
                else   
                if ( (size > 4) && 
                     (filename[size-5] == '.' ) && 
                     (filename[size-4] == 'b' ) && 
                     (filename[size-3] == 'i' ) && 
                     (filename[size-2] == 'n' ) ) {
                  gfxmem[REG_TLOOKUP+file_block_wr_pt++] = FILE_IS_ROM;
                }
#endif
#ifdef TRS
                if ( (size > 4) && 
                     (filename[size-5] == '.' ) && 
                     (filename[size-4] == 'c' ) && 
                     (filename[size-3] == 'm' ) && 
                     (filename[size-2] == 'd' ) ) {
                  gfxmem[REG_TLOOKUP+file_block_wr_pt++] = FILE_IS_PRG;
                }
#endif
                else {
                  valid = false;
                }
              }
              else {
                gfxmem[REG_TLOOKUP+file_block_wr_pt++] = FILE_IS_DIR;
              }
              if (valid == true) {
                file_block_wr_pt += mystrncpy((char*)&gfxmem[REG_TLOOKUP+file_block_wr_pt], filename, MAX_FILENAME_SIZE-1);
                nbFiles++;
              }  
            }               
            fres = f_findnext(&dir, &entry); 
          }
        }
        gfxmem[REG_TLOOKUP] = nbFiles;
        break;     
      default:
        break;
    }
    gfxmem[REG_TSTATUS] = 0;
  }  
#endif
}



static void __not_in_flash("pushCmdQueue") pushCmdQueue(QueueItem cmd ) {
  if (cmd_queue_cnt != 256)
  {
    gfxmem[REG_TSTATUS] = 1;     
    cmd_queue[cmd_queue_wr] = cmd;
    cmd_queue_wr = (cmd_queue_wr + 1)&(CMD_QUEUE_SIZE-1);
    cmd_queue_cnt++;
  }  
}

static uint8_t __not_in_flash("cmd_params_len") cmd_params_len[MAX_CMD]={ 
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

//       11: tiles clr
//       12: bitmap clr
//       13: bitmap point
//       14: bitmap tri
//       15: bitmap rect
//       27  openfile                 (data=file#)
//       28  readfile                 (data=nbbytes)
//       29  opendir
//       30  readdir
//       31  unused
 
  0,3,3,6,4,4,2,2,2, 2,0,0, 0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0
}; 

static void __not_in_flash("traParamFuncDummy") traParamFuncDummy(void) {
}

static void __not_in_flash("traParamFuncTile") traParamFuncTile(void) {
  tra_spr_id = SPRITE_NBTILES; // not a sprite!
  tra_x = 0;
  tra_w = cmd_params[1];
  tra_h = cmd_params[2];
  tra_stride = tra_w;
  tra_address = &TileData[tra_h*tra_stride*(cmd_params[0])];
}

static void __not_in_flash("traParamFuncSprite") traParamFuncSprite(void) {
  tra_spr_id = cmd_params[0] & (SPRITE_NBTILES-1);
  if (!tra_spr_id) SpriteDataIndex = 0; 
  tra_x = 0;
  tra_w = SPRITEW;
  SpriteDataW[tra_spr_id]=cmd_params[1];  
  tra_h = cmd_params[2]; 
  SpriteDataH[tra_spr_id]=tra_h;
  tra_stride = tra_w;
  tra_address = &SpriteData[SpriteDataIndex];
  SpriteIdToDataIndex[tra_spr_id] = SpriteDataIndex;
  SpriteDataIndex += (tra_h*tra_stride);
}

static void __not_in_flash("traParamFuncBitmap") traParamFuncBitmap(void) {
  tra_stride = screen_width==HI_XRES?screen_width/2:screen_width;  
  tra_address = &Bitmap[tra_stride*cmd_params[2]+((cmd_params[0]<<8)+cmd_params[1])];
  tra_x = 0; 
  tra_w = (cmd_params[3]<<8)+cmd_params[4];
  tra_h = cmd_params[5];  
}

static void __not_in_flash("traParamFuncTmapcol") traParamFuncTmapcol(void) {
  tra_stride = screen_width/8;
  tra_address = &gfxmem[REG_TILEMAP_L0-TILEMAP_SIZE*cmd_params[0]+tra_stride*cmd_params[2]+cmd_params[1]];
  tra_x = 0; 
  tra_w = 1;
  tra_h = cmd_params[3];
}

static void __not_in_flash("traParamFuncTmaprow") traParamFuncTmaprow(void) {
  tra_stride = screen_width/8; 
  tra_address = &gfxmem[REG_TILEMAP_L0-TILEMAP_SIZE*cmd_params[0]+tra_stride*cmd_params[2]+cmd_params[1]];
  tra_x = 0; 
  tra_w = cmd_params[3];
  tra_h = 1; 
}

static void __not_in_flash("traParamFuncPackedTiles") traParamFuncPackedTiles(void) {
  tra_h = (cmd_params[0]<<8)+cmd_params[1];
  tra_x = sizeof(TileData)-tra_h;
  tra_w = tra_h;
  tra_address = &TileData[0];
}

static void __not_in_flash("traParamFuncPackedSprites") traParamFuncPackedSprites(void) {
  tra_h = (cmd_params[0]<<8)+cmd_params[1];
  tra_x = sizeof(SpriteData)-tra_h;
  tra_w = tra_h;
  tra_address = &SpriteData[0];
}

static void __not_in_flash("traParamFuncPackedBitmap") traParamFuncPackedBitmap(void) {
  tra_h = (cmd_params[0]<<8)+cmd_params[1];
  tra_x = sizeof(Bitmap)-tra_h;
  tra_w = tra_h;
  tra_address = &Bitmap[0];
}

static void __not_in_flash("traParamFuncFont") traParamFuncFont(void) {
  tra_h = (cmd_params[0]<<8)+cmd_params[1];
  tra_x = 0;
  tra_w = tra_h;
#ifdef PET  
  tra_address = &font[(font_lowercase?0x800:0x000)];
#endif
#ifdef TRS
  tra_address = &font[(font_reversed?0x800:0x000)];
#endif
  cmd_tra_depth = 0; // 1 byte to 1 byte (1bit data, 8 pixels at a time)
}

static void __not_in_flash("traParamFuncExecuteCommand") traParamFuncExecuteCommand(void) {
  if (cmd_queue_cnt != 256)
  {
    gfxmem[REG_TSTATUS] = 1;     
    cmd_queue[cmd_queue_wr] = {cmd};
    cmd_queue_wr = (cmd_queue_wr + 1)&(CMD_QUEUE_SIZE-1);
    cmd_queue_cnt++;
  } 
}

static void __not_in_flash("traParamFuncExecuteCommand1") traParamFuncExecuteCommand1(void) {
  if (cmd_queue_cnt != 256)
  {
    gfxmem[REG_TSTATUS] = 1; 
    cmd_queue[cmd_queue_wr] = {cmd,cmd_params[0]};
    cmd_queue_wr = (cmd_queue_wr + 1)&(CMD_QUEUE_SIZE-1);
    cmd_queue_cnt++;
  } 
}


static void __not_in_flash("traParamFuncPtr") (*traParamFuncPtr[MAX_CMD])(void) = {
  traParamFuncDummy,
  traParamFuncTile,
  traParamFuncSprite,
  traParamFuncBitmap,
  traParamFuncTmapcol,
  traParamFuncTmaprow,
  traParamFuncPackedTiles,
  traParamFuncPackedSprites,
  traParamFuncPackedBitmap,
  traParamFuncFont,
  traParamFuncDummy,
  traParamFuncExecuteCommand,
  traParamFuncExecuteCommand,
  traParamFuncDummy,
  traParamFuncDummy,
  traParamFuncDummy,
  
  traParamFuncDummy,
  traParamFuncDummy,
  traParamFuncDummy,
  traParamFuncDummy,
  traParamFuncDummy,
  traParamFuncDummy,
  traParamFuncDummy,
  traParamFuncDummy,
  traParamFuncDummy,
  traParamFuncDummy,
  traParamFuncDummy,
  traParamFuncExecuteCommand1,
  traParamFuncExecuteCommand1,
  traParamFuncExecuteCommand,
  traParamFuncExecuteCommand,
  traParamFuncDummy
};

static void __not_in_flash("traDataFunc8nolut") traDataFunc8nolut(uint8_t val) {
  tra_address[tra_x++]=val; if (tra_x == tra_w) {tra_x=0; tra_address+=tra_stride; tra_h--; };
}

static void __not_in_flash("traDataFunc8") traDataFunc8(uint8_t val) {
  tra_address[tra_x++]=tra_pal[val]; if (tra_x == tra_w) {tra_x=0; tra_address+=tra_stride; tra_h--; };
}

static void __not_in_flash("traDataFunc4") traDataFunc4(uint8_t val) {
  tra_address[tra_x++]=tra_pal[val>>4]; if (tra_x == tra_w) {tra_x=0; tra_address+=tra_stride; tra_h--; };
  if (tra_h) {
    tra_address[tra_x++]=tra_pal[val&0xf]; if (tra_x == tra_w) {tra_x=0; tra_address+=tra_stride; tra_h--; };
  }  
}

static void __not_in_flash("traDataFunc2") traDataFunc2(uint8_t val) {
  tra_address[tra_x++]=tra_pal[(val>>6)&0x3]; if (tra_x == tra_w) {tra_x=0; tra_address+=tra_stride; tra_h--; };
  if (tra_h) {
    tra_address[tra_x++]=tra_pal[(val>>4)&0x3]; if (tra_x == tra_w) {tra_x=0; tra_address+=tra_stride; tra_h--; };
    if (tra_h) {
      tra_address[tra_x++]=tra_pal[(val>>2)&0x3]; if (tra_x == tra_w) {tra_x=0; tra_address+=tra_stride; tra_h--; };
      if (tra_h) {
        tra_address[tra_x++]=tra_pal[(val)&0x3]; if (tra_x == tra_w) {tra_x=0; tra_address+=tra_stride; tra_h--; };
      }
    }
  }
}

static void __not_in_flash("traDataFunc1") traDataFunc1(uint8_t val) {
  tra_address[tra_x++]=tra_pal[(val>>7)&0x1]; if (tra_x == tra_w) {tra_x=0; tra_address+=tra_stride; tra_h--; };
  if (tra_h) {
    tra_address[tra_x++]=tra_pal[(val>>6)&0x1]; if (tra_x == tra_w) {tra_x=0; tra_address+=tra_stride; tra_h--; };
    if (tra_h) {
      tra_address[tra_x++]=tra_pal[(val>>5)&0x1]; if (tra_x == tra_w) {tra_x=0; tra_address+=tra_stride; tra_h--; };
      if (tra_h) {
        tra_address[tra_x++]=tra_pal[(val>>4)&0x1]; if (tra_x == tra_w) {tra_x=0; tra_address+=tra_stride; tra_h--; };
        if (tra_h) {
          tra_address[tra_x++]=tra_pal[(val>>3)&0x1]; if (tra_x == tra_w) {tra_x=0; tra_address+=tra_stride; tra_h--; };
          if (tra_h) {
            tra_address[tra_x++]=tra_pal[(val>>2)&0x1]; if (tra_x == tra_w) {tra_x=0; tra_address+=tra_stride; tra_h--; };
            if (tra_h) {
              tra_address[tra_x++]=tra_pal[(val>>1)&0x1]; if (tra_x == tra_w) {tra_x=0; tra_address+=tra_stride; tra_h--; };
              if (tra_h) {
                tra_address[tra_x++]=tra_pal[(val)&0x1]; if (tra_x == tra_w) {tra_x=0; tra_address+=tra_stride; tra_h--; };
              }
            }
          }
        }
      }
    }
  }
}

static void __not_in_flash("traDataFunc8nolutPacked") traDataFunc8nolutPacked(uint8_t val) {
  if (tra_h > 0) {tra_address[tra_x++]=val; tra_h--;};
}

static void __not_in_flash("traDataFuncPtr") (*traDataFuncPtr[])(uint8_t) = {
  traDataFunc8nolutPacked, // 0
  traDataFunc1, // 1
  traDataFunc2, // 2 
  traDataFunc2, // 3
  traDataFunc4, // 4
  traDataFunc4, // 5
  traDataFunc4, // 6
  traDataFunc4, // 7
  traDataFunc8, // 8
  traDataFunc8nolut, // 9
  traDataFunc8nolut, // 10
  traDataFunc8nolut, // 11
  traDataFunc8nolut, // 12
  traDataFunc8nolut, // 13
  traDataFunc8nolut, // 14
  traDataFunc8nolut  // 15
};

void __not_in_flash("HyperGfxWrite") HyperGfxWrite(uint16_t address, uint8_t value) {
#ifndef NO_HYPER
#ifdef PET  
  switch (address-0x8000) 
#endif
#ifdef TRS
  switch (address-0xe000) 
#endif
  {  
    case REG_TDEPTH:
      cmd_tra_depth = value&0x0f;
      break;
    case REG_TCOMMAND:
      cmd_param_ind = 0;
      cmd = value & (MAX_CMD-1);
      if (!cmd_params_len[cmd]) {
        traParamFuncPtr[cmd]();
      }
      break;
    case REG_TPARAMS:
      if (cmd_param_ind < MAX_PAR) cmd_params[cmd_param_ind++]=value;
      if (cmd_param_ind == cmd_params_len[cmd]) {
        traParamFuncPtr[cmd]();
      }
      break;
    case REG_TDATA:
      if (tra_h)
      {
        traDataFuncPtr[cmd_tra_depth](value);
        if (!tra_h) {
          switch (cmd) 
          {
            case cmd_transfer_packed_tile_data:
              pushCmdQueue({cmd_transfer_packed_tile_data,(uint8_t)0,(uint16_t)((cmd_params[0]<<8)+cmd_params[1])});
              break;
            case cmd_transfer_packed_sprite_data:
              pushCmdQueue({cmd_transfer_packed_sprite_data,(uint8_t)0,(uint16_t)((cmd_params[0]<<8)+cmd_params[1])});
              break;
            case cmd_transfer_packed_bitmap_data:
              pushCmdQueue({cmd_transfer_packed_bitmap_data,(uint8_t)0,(uint16_t)((cmd_params[0]<<8)+cmd_params[1])});
              break;
          }
        }  
      }   
      break;
    default:
#ifdef PET  
      gfxmem[address-0x8000] = value;
#endif
#ifdef TRS
      gfxmem[address-0xe000] = value;
#endif
      break;
  } 
#else
#ifdef PET  
  gfxmem[address-0x8000] = value;
#endif
#ifdef TRS
  gfxmem[address-0xe000] = value;
#endif
#endif
}

uint8_t __not_in_flash("HyperGfxread") HyperGfxRead(uint16_t address) {
#ifdef PET
  return gfxmem[address-0x8000];  
#endif
#ifdef TRS
  return gfxmem[address-0xe000];;
#endif
}

void __not_in_flash("VideoRenderUpdate") VideoRenderUpdate(void)
{
#ifdef HAS_SND
    sid_dump();  
#endif

#ifndef NO_HYPER

#ifdef PET    
    int vmode = GET_VIDEO_MODE;
    if (video_mode != vmode) {
      if (vmode == 0) {hdmi_init(MODE_VGA_640x240);screen_width=640;}  
      else if (vmode == 1) {hdmi_init(MODE_VGA_320x240);screen_width=320;} 
      else {hdmi_init(MODE_VGA_256x240);screen_width=256;}
      video_mode = vmode;
    }
#endif
    
    // sort sprites by Y, max amount sprites per scanline
    memset((void*)&SpriteYSorted[0],0, sizeof(SpriteYSorted));
    for (int i = 0; i < SPRITE_NUM_MAX; i++)
    { 
      if (gfxmem[REG_SPRITE_Y+i] < MAXHEIGHT)
      {  
        SpriteYSorted[gfxmem[REG_SPRITE_Y+i]].count++;
      }
      else {
        SpriteParams[i].y = MAXHEIGHT;  
      }   
    }  
    // Sort sprites by Y
    int nextid = 1;
    for (int i = 0; i < SPRITE_NUM_MAX; i++)
    { 
      uint8_t y = gfxmem[REG_SPRITE_Y+i];
      if (y < MAXHEIGHT) {      
        if ( (SpriteYSorted[y].count) && (SpriteYSorted[y].first == 0) ) {
          SpriteYSorted[y].first = nextid;
          nextid += SpriteYSorted[y].count;
          SprYSortedIndexes[y] = 0;
        }
        if (SpriteYSorted[y].count) 
        {
          SprYSortedIds[SpriteYSorted[y].first+SprYSortedIndexes[y]] = i;
          SprYSortedIndexes[y]++;
        }
      }
    } 
    // update sprites params
    int cur = 0;
    for (int j = 0; j < MAXHEIGHT; j++)
    {
      for (int i=0;i<SpriteYSorted[j].count; i++) 
      {
        if (i<SPRITE_NUM)
        {
          if (cur < SPRITE_NUM_MAX) {
            int ind = SprYSortedIds[SpriteYSorted[j].first+i];
            uint8_t id = gfxmem[REG_SPRITE_IND+ind];
            uint16_t x = (gfxmem[REG_SPRITE_XHI+ind]<<8)+gfxmem[REG_SPRITE_XLO+ind];    
            uint8_t y = gfxmem[REG_SPRITE_Y+ind];
            SpriteParams[cur].x = x;
            SpriteParams[cur].dataIndex = SpriteIdToDataIndex[id & 0x3f];
            SpriteParams[cur].y = y;
            SpriteParams[cur].h = SpriteDataH[id & 0x3f];
            SpriteParams[cur].flipH = (id & 0x40);
            SpriteParams[cur].flipV = (id & 0x80);
            cur++;       
          }  
        }
        else 
        {
          break;
        }
      }  
    }
    for (int i=cur; i < SPRITE_NUM_MAX; i++)
    {
      SpriteParams[cur].y = MAXHEIGHT;
    }

    // sprite collision for first 8 sprites
    for (int i = 0; i < SPRITE_NUM_MAX; i++)
    {
      uint16_t x1 = (gfxmem[REG_SPRITE_XHI+i]<<8)+gfxmem[REG_SPRITE_XLO+i];    
      uint8_t y1 = gfxmem[REG_SPRITE_Y+i];
      uint8_t id1 = gfxmem[REG_SPRITE_IND+i]&0x3f;
      // hflip
      if (gfxmem[REG_SPRITE_IND+i] & 0x40) x1 = SPRITEW-x1;
      // vflip
      if (gfxmem[REG_SPRITE_IND+i] & 0x80) y1 = SpriteDataW[id1]-y1;
      uint8_t colbits = 0;
      if (id1) {
        for (int k = 0; k < 8; k++)
        {
          if (k != i) {
            uint16_t x = (gfxmem[REG_SPRITE_XHI+k]<<8)+gfxmem[REG_SPRITE_XLO+k];    
            uint8_t y  = gfxmem[REG_SPRITE_Y+k];
            uint8_t id = gfxmem[REG_SPRITE_IND+k]&0x3f;
            // hflip
            if (gfxmem[REG_SPRITE_IND+k] & 0x40) x = SPRITEW-x;
            // vflip
            if (gfxmem[REG_SPRITE_IND+k] & 0x80) y = SpriteDataW[id]-y;            
            if ( (id) && (x >= x1) && (x < (x1+SpriteDataW[id1])) && (y >= y1) && (y < (y1+SpriteDataH[id1])) )
            {
              colbits += (1<<k);
            }
            
            if ( (id) && (x1 >= x) && (x1 < (x+SpriteDataW[id])) && (y1 >= y) && (y1 < (y+SpriteDataH[id])) )
            {
              colbits += (1<<k);
            }
          }           
        }
      }   
      gfxmem[REG_SPRITE_COL_LO+i] = colbits;
      /*
      colbits = 0;
      if (id1) {
        for (int k = 8; k < 16; k++)
        {
          if (k != i) {
            uint16_t x = (gfxmem[REG_SPRITE_XHI+k]<<8)+gfxmem[REG_SPRITE_XLO+k];    
            uint8_t y  = gfxmem[REG_SPRITE_Y+k];
            uint8_t id = gfxmem[REG_SPRITE_IND+k]&0x3f;
            // hflip
            if (gfxmem[REG_SPRITE_IND+k] & 0x40) x = SPRITEW-x;
            // vflip
            if (gfxmem[REG_SPRITE_IND+k] & 0x80) y = SpriteDataW[id]-y;            
            if ( (id) && (x >= x1) && (x < (x1+SpriteDataW[id1])) && (y >= y1) && (y < (y1+SpriteDataH[id1])) )
            {
              colbits += (1<<(k-8));
            }
            
            if ( (id) && (x1 >= x) && (x1 < (x+SpriteDataW[id])) && (y1 >= y) && (y1 < (y+SpriteDataH[id])) )
            {
              colbits += (1<<(k-8));
            }
          }           
        }
      }   
      gfxmem[REG_SPRITE_COL_HI+i] = colbits;
      */       
    }   
#endif
}


void __not_in_flash("HyperGfxInit") HyperGfxInit(void) 
{
#ifdef NO_HYPER
#else
#ifdef HAS_SND
  audio_init(1024, audio_fill_buffer);
  playSID.begin(SOUNDRATE, 512); 
#endif
  memset((void*)&Bitmap[0],0, sizeof(Bitmap));
  memset((void*)&TileData[0],0, sizeof(TileData));
  memset((void*)&SpriteData[0],0, sizeof(SpriteData));

#ifdef PET
  uint8_t palntsc = gfxmem[REG_PALNTSC];
  memset((void*)&gfxmem[0x0000], 0, 0x2000); // all registers and videomem  
#endif
#ifdef PET
  gfxmem[REG_PALNTSC] = palntsc;
  SET_BG_COL(VGA_RGB(0x00,0x00,0x00));
  SET_FG_COL(VGA_RGB(0x00,0xff,0x00));
#endif
#ifdef TRS
  // end of TRS memory
  memory[0xe000] = 0xff;
  SET_BG_COL(VGA_RGB(0x00,0x00,0x00));
  SET_FG_COL(VGA_RGB(0xff,0xff,0xff));
#endif  
  // 0: L0 tiles + L1 petfont
  // 1: L0 tiles + L1 tiles
  // 2: L0 tiles
  // 3: L0 bitmap  
//  SET_LAYER_MODE( LAYER_L0_TILE | LAYER_L1_TILE | LAYER_L2_SPRITE );
//  SET_LAYER_MODE( LAYER_L0_TILE | LAYER_L1_PETFONT | LAYER_L2_SPRITE );
  SET_LAYER_MODE( LAYER_L0_TILE | LAYER_L1_PETFONT | LAYER_L2_SPRITE | LAYER_L2_INBETW );    

  // prepare sprites
  for (int i = 0; i < SPRITE_NUM_MAX; i++)
  {
    SpriteParams[i].x = 0;
    SpriteParams[i].h = 0;
    SpriteParams[i].y = MAXHEIGHT;
    SpriteParams[i].flipH = 0;
    SpriteParams[i].flipV = 0;
    SpriteParams[i].dataIndex = 0;
  }

  // Init tile map with something
  /*
  for (int i=0;i<TILEMAP_SIZE;i++) 
  {
    gfxmem[REG_TILEMAP_L0+i] = i&255;  // L0 tiles
    gfxmem[REG_TEXTMAP_L1+i] = i&255;  // L1 text
    gfxmem[REG_TILEMAP_L1+i] = i&255;  // L1 tiles
  }
  */
  /*
  // init raster colors
  for (int i=0;i<MAXHEIGHT;i++) 
  {
    gfxmem[REG_LINES_BG_COL+i] = VGA_RGB(i&7*32,0,0); // Lines BG colors
  }
  */
  if ( video_default == VMODE_HIRES ) {
    SET_VIDEO_MODE(0);
  }  
  else {
    SET_VIDEO_MODE(1);
  }  
#endif

#ifdef PET
  int cheight = 8;
  uint8_t* src=(uint8_t*)&petfont[0x0000];
#endif
#ifdef TRS
  int cheight = 12;
  uint8_t* src=(uint8_t*)&font_m3[0x0000];
#endif  
  uint8_t* dst=font;
  for (int j=0; j<cheight; j++) {
    for (int i=0; i<256; i++) {
      *dst++ = src[i*cheight+j];
    }
  }
#ifdef PET
  src=(uint8_t*)&petfont[0x0800];
#endif
#ifdef TRS
  src=(uint8_t*)&font_m3[0x0000];
#endif  
  for (int j=0; j<cheight; j++) {
    for (int i=0; i<256; i++) {
#ifdef PET
      *dst++ = src[i*cheight+j];
#endif
#ifdef TRS
//      *dst++ = ~src[i*cheight+j];
#endif
    }
  }
}

void HyperGfxFlashFSInit(void)
{
  fatfs_mounted = mount_fatfs_disk();
  if (fatfs_mounted) {
    fres = f_mount(&filesystem, "/", 1);
#ifdef PET
    // read HYPERPET.CFG
    if( !(f_open(&file, "HYPERPET.CFG" , FA_READ)) ) {
      while (f_gets(scratchpad, 256, &file) != NULL)  {
        if (!strncmp(scratchpad, "model=", 6)) {
        }
        else 
        if (!strncmp(scratchpad, "columns=", 8)) {
          if ( (scratchpad[8]=='8') && (scratchpad[9]=='0') ) {
            video_default = VMODE_HIRES;
            screen_width=640;
          }
          else 
          if ( (scratchpad[8]=='4') && (scratchpad[9]=='0') ) {
            video_default = VMODE_LORES;
            screen_width=320;
          }
        }
        else 
        if (!strncmp(scratchpad, "ram=", 4)) {
        }
#ifndef HAS_PETIO
        else 
        if (!strncmp(scratchpad, "keyboard=", 9)) {
#ifdef HAS_USBHOST          
          if ( ( scratchpad[9]=='u') && (scratchpad[10]=='k') ) {
            kbd_set_locale(KLAYOUT_UK);
          }
          else if ( ( scratchpad[9]=='b') && (scratchpad[10]=='e') ) {
            kbd_set_locale(KLAYOUT_BE);
          }
#endif 
        }
#endif        
      }
      f_close(&file);
    }
    //// force lores
    //video_default = VMODE_LORES;
    //screen_width=320;
#endif
#ifdef TRS
    // read HYPERTRS.CFG
    if( !(f_open(&file, "HYPERTRS.CFG" , FA_READ)) ) {
      while (f_gets(scratchpad, 256, &file) != NULL)  {
        if (!strncmp(scratchpad, "ram=", 4)) {
          if ( (scratchpad[4]=='8') && (scratchpad[4]=='0') ) {
          }
        }
        else 
        if (!strncmp(scratchpad, "keyboard=", 9)) {
#ifdef HAS_USBHOST          
          if ( ( scratchpad[9]=='u') && (scratchpad[10]=='k') ) {
            kbd_set_locale(KLAYOUT_UK);
          }
          else if ( ( scratchpad[9]=='b') && (scratchpad[10]=='e') ) {
            kbd_set_locale(KLAYOUT_BE);
          }
#endif 
        }
      }
      f_close(&file);
    }
#ifdef HAS_USBHOST          
    kbd_set_locale(KLAYOUT_BE);
#endif
#endif
  }
}

void __not_in_flash("HyperGfxReset") HyperGfxReset(void)
{
  gfx_reset = true;
}

void __not_in_flash("HyperGfxHandleGfx") HyperGfxHandleGfx(void)
{
  int scanline = 0;
  //hdmi_wait_line(8);
  for (int i = 8; i < 408; i = i + 2) {
      hdmi_wait_line(i);
      uint8_t * linebuffer = hdmi_get_line_buffer(scanline);
#ifndef NO_HYPER     
      gfxmem[REG_VSYNC] = scanline;
#endif
      VideoRenderLineBG(linebuffer, scanline);            
      VideoRenderLineL0(linebuffer, scanline);
#ifdef TRS             
      if (scanline < 192)
#endif
      VideoRenderLineL1(linebuffer, scanline);
      scanline++;
  }
#ifndef NO_HYPER     
  gfxmem[REG_VSYNC] = MAXHEIGHT;
#endif
  if (gfx_reset) {
    gfx_reset = false;
    HyperGfxInit();
  }
  VideoRenderUpdate();
#ifdef HAS_SND  
  audio_handle();
#endif  
}

void HyperGfxVsync(void) {
  hdmi_wait_line(400);  
}

bool HyperGfxIsHires(void) {
  return (video_mode == VMODE_HIRES);   
}

bool HyperGfxIsPal(void) {
  return (gfxmem[REG_PALNTSC] == 0);   
}

