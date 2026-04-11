#include <string.h>
#include <cstdlib>
#include <ctype.h>

#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

#include "memmap.h" // contains config !!!

extern "C" {
  #include "iopins.h"   
}

#ifdef HAS_PETIO
#include "petbus.pio.h"
#endif

#include "hypergfx.h"


#include "edit4.h"
#include "edit480.h"
#include "edit450.h"
#include "edit48050.h"

#ifdef PETIO_A000
#include "fb.h"
//#include "vsync.h"
#endif         

#ifdef CPU_EMU
// 6502 emu
#include "mos6502.h"
#include "basic4_b000.h"
#include "basic4_c000.h"
#include "basic4_d000.h"
#include "kernal4.h"
#include "hdmi_framebuffer.h"

#ifdef HAS_USBHOST
#include "bsp/board_api.h"
#include "tusb.h"
#include "kbd.h"
extern "C" void hid_app_task(void);
#else
#include "usb_serial.h"
#endif

#define PET_MEMORY_SIZE 0x8000 // for 32k

// 6502 emu
static mos6502 mos;
static uint8_t petram[PET_MEMORY_SIZE];
static bool pet_running = true;
static bool prg_start = false;
static uint16_t prg_add_start;
static uint16_t prg_add_cur;
static uint16_t prg_wr = 0;
static uint16_t prg_size = 0;

static uint8_t _rows[0x10];
static uint8_t _row;

/*
Professionnal keyboard map
----+------------------------
row |  7  6  5  4  3  2  1  0
----+------------------------
 9  | 16 04 3A 03 39 36 33 DF
    | ^V --  : ^C  9  6  3 <-   ^V = TAB + <- + DEL, ^C = STOP,
    |                            <- = left arrow
 8  | B1 2F 15 13 4D 20 58 12
    | k1  / ^U ^S  m sp  x ^R   k9 = keypad 9, ^U = RVS + A + L,
    |                           ^S = HOME, sp = space, ^R = RVS
 7  | B2 10 0F B0 2C 4E 56 5A   ^O = Z + A + L, rp = repeat
    | k2 rp ^O k0  ,  n  v  z
    |
 6  | B3 00 19 AE 2E 42 43 00
    | k3 rs ^Y k.  .  b  c ls   ^Y = left shift + TAB + I, k. = keypad .
    |                           ls = left shift, rs = right shift
 5  | B4 DB 4F 11 55 54 45 51   ^Q = cursor down
    | k4  [  o ^Q  u  t  e  q
    |    5D]
 4  | 14 50 49 DC 59 52 57 09
    | ^T  p  i  \  y  r  w ^I   ^T = DEL, ^I = TAB
    |          C0@
 3  | B6 C0 4C 0D 4A 47 44 41
    | k6  @  l ^M  j  g  d  a   ^M = return
    |    5B[
 2  | B5 3B 4B DD 48 46 53 9B
    | k5  ;  k  ]  h  f  s ^[   ^[ = ESC
    |    5C\   3B;
 1  | B9 06 DE B7 B0 37 34 31
    | k9 --  ^ k7  0  7  4  1
    |
 0  | 05 0E 1D B8 2D 38 35 32
    |  . ^N ^] k8  -  8  5  2   ^N = both shifts + 2, ^] = cursor right
*/

static const uint8_t asciimap[8*10] = {
/*----+-----------------------------------------------*/
/*row |   7     6     5     4     3     2     1     0 */
/*----+-----------------------------------------------*/
/* 9  |*/ 0x16, 0x04, 0x3A, 0x03, 0x39, 0x36, 0x33, 0xDF,
/* 8  |*/ 0xB1, 0x2F, 0x15, 0x13, 0x4D, 0x20, 0x58, 0x12,
/* 7  |*/ 0xB2, 0x10, 0x0F, 0xB0, 0x2C, 0x4E, 0x56, 0x5A,
/* 6  |*/ 0xB3, 0x00, 0x19, 0xAE, 0x2E, 0x42, 0x43, 0x00,
/* 5  |*/ 0xB4, 0xDB, 0x4F, 0x11, 0x55, 0x54, 0x45, 0x51,
/* 4  |*/ 0x14, 0x50, 0x49, 0xDC, 0x59, 0x52, 0x57, 0x09,
/* 3  |*/ 0xB6, 0xC0, 0x4C, 0x0D, 0x4A, 0x47, 0x44, 0x41,
/* 2  |*/ 0xB5, 0x3B, 0x4B, 0xDD, 0x48, 0x46, 0x53, 0x9B,
/* 1  |*/ 0xB9, 0x06, 0xDE, 0xB7, 0x30, 0x37, 0x34, 0x31,
/* 0  |*/ 0x05, 0x0E, 0x1D, 0xB8, 0x2D, 0x38, 0x35, 0x32
};

#endif


// PET shadow memory 8000-9fff
// unsigned char *mem;
// PET shadow memory a000-afff
#ifdef PETIO_A000
static unsigned char mem_a000[0x1000];
#endif
// PET shadow memory e000-a7ff
#ifdef PETIO_EDIT
static unsigned char mem_e000[0x0800];
#endif
static bool got_reset = false;


#ifdef CPU_EMU

#ifdef HAS_USBHOST
// ****************************************
// USB keyboard
// ****************************************
static int prev_code=0;

static uint8_t joystick0 = 0xff;
static bool kbdasjoy = false;
// Joystick macros
#define JOY_UP      (1)
#define JOY_DOWN    (2)
#define JOY_LEFT    (4)
#define JOY_RIGHT   (8) 
#define JOY_FIRE    (1+2)
#endif


static void _set(uint8_t k) {
  _rows[(k & 0xf0) >> 4] |= 1 << (k & 0x0f);
}

static void _reset(uint8_t k) {
  _rows[(k & 0xf0) >> 4] &= ~(1 << (k & 0x0f));
}

static uint8_t ascii2rowcol(uint8_t chr) 
{
  uint8_t rowcol = 0;
  for (int i=0;i<sizeof(asciimap); i++) {
    if (asciimap[i] == chr) {
      int col = 7-(i&7);
      int row = 9-(i>>3);
      rowcol = (row<<4)+col;
      break;
    }  
  } 
  return rowcol; 
}  

uint8_t readWord( uint16_t location)
{
  if (location < 0x8000)  {
    if (location < PET_MEMORY_SIZE) {
      return petram[location];
    }
    else {
      return 0xff;
    }  
  }
  else if (location < 0xa000) {
    return HyperGfxRead(location);
  }  
  else if (location < 0xb000) {
#ifdef PETIO_A000
    return mem_a000[location-0xa000];
#else
    return 0;
#endif    
  }  
  else if (location < 0xc000) {
    return basic4_b000[location-0xb000];
  }  
  else if (location < 0xd000) {
    return basic4_c000[location-0xc000];
  }  
  else if (location < 0xe000) {
    return basic4_d000[location-0xd000];
  } 
  else if (location < 0xe800) {
    if (HyperGfxIsPal()) { 
      if (!HyperGfxIsHires())      
        return edit450[location-0xe000];
      else
        return edit48050[location-0xe000];
    } 
    else {
      if (!HyperGfxIsHires())      
        return edit4[location-0xe000];
      else
        return edit480[location-0xe000];
    }    
  } 
  else if ( (location > 0xe800) && (location < 0xf000) ) {
    if (location == 0xe812)         // PORT B
      return (_rows[_row] ^ 0xff);    
    else if (location == 0xe810)    // PORT A
      return (_row | 0x80); 
#ifdef HAS_USBHOST
    else if (location == 0xe84f)    // PORT Joystick
      return (joystick0); 
#endif
    else
      return 0x00;
  }  
  else {
    return kernal4[location-0xf000];
  }
}

void writeWord( uint16_t location, uint8_t value)
{
  if (location < 0x8000) { 
    if (location < PET_MEMORY_SIZE) {
      petram[location] = value;
    }
  }
  else if (location < 0xa000) {
    HyperGfxWrite(location, value);
  }
#ifdef PETIO_A000
  else if (location < 0xb000) {
    mem_a000[location-0xa000] = value;
  }
#endif
  else if ( (location > 0xe800) && (location < 0xf000) ) {
    if (location == 0xe812)       // PORT B
    {
    } 
    else if (location == 0xe810)  // PORT A
    {
      _row = (value & 0x0f);
    }          
    else if (location == 0xe84C) {
      if (value & 0x02) 
      {
        font_lowercase = true;
      }
      else 
      {
        font_lowercase = false;
      }
    }  
  }
}

static void pet_kdown(uint8_t asciicode, bool shiftl, bool shiftr ) {
  _set(ascii2rowcol(asciicode));
  if ( (shiftl) && (shiftr) ) _rows[0]|= 0x40;
  else if (shiftl) _rows[6]|= 0x01;
  else if (shiftr) _rows[6]|= 0x40;
}

static void pet_kup(uint8_t asciicode) {
  _reset(ascii2rowcol(asciicode));
  _rows[6] &= 0xfe;
  _rows[6] &= 0xbf;
  _rows[0] &= 0xbf; 
}


static void pet_prg_write(uint8_t * src, int length )
{
  while (1)
  {
    if (prg_wr == 0)
    {
      prg_wr++;
      prg_add_start = *src++;
    }   
    else if (prg_wr == 1)
    {
      prg_wr++;
      prg_add_start = prg_add_start + (*src++ << 8);
      prg_add_cur = prg_add_start;
      //printf("loading at %04x\n",prg_add_start);
    }   
    else
    {
      //printf("%02x\n",*src);
      petram[prg_add_cur++] = *src++;
    } 
    length  = length - 1;
    if ( length == 0) return;
  }
}

static void pet_prg_run( void )
{
  uint8_t lo,hi;
  petram[0xc7] = petram[0x28];
  petram[0xc8] = petram[0x29];

  lo = (uint8_t)(prg_add_cur & 0xff);
  hi = (uint8_t)(prg_add_cur >> 8);
  petram[0x2a] = lo;
  petram[0x2c] = lo;
  petram[0x2e] = lo;
  petram[0xc9] = lo;
  petram[0x2b] = hi;
  petram[0x2d] = hi;
  petram[0x2f] = hi;
  petram[0xca] = hi;

  pet_running = true;
  prg_start = true;
  //printf("prg size %d\n",prg_size);  
  prg_wr = 0;
}

static void pet_reset( void )
{
    got_reset = true;
    prg_wr = 0;
}

static void pet_start(void) 
{
  for (int i=0;i<PET_MEMORY_SIZE;i++) 
  {
    petram[i] = 0;
  }
 
  mos.Reset();
  for (int i = sizeof(_rows); i--; )
    _rows[i] = 0;
  pet_running = true;
  prg_start = false;
  prg_wr = 0;
}


#define PET_LINES  (260)
#define PET_CYCLES (PET_LINES*64) //16600 //9000

static void pet_line(void) 
{
  mos.Run(PET_CYCLES/PET_LINES);
}

static void pet_remaining(void) 
{
  mos.Run((PET_CYCLES/PET_LINES)*(PET_LINES-200));
  mos.IRQ();
}

// ****************************************
// Keyboard
// ****************************************
#define KEY_DEBOUNCE_MS 50
#define BOOT_SEQ_MS 700

static uint8_t prev_key = 0;
static int repeat_cnt = 0;
static bool send_cmdstring = false;
static const char * cmdstring_pt;
//static const char petlistruncmd[] = {'L', 'I', 'S', 'T', 0x0d, 'R', 'U', 'N', 0x0d, 0}; // LIST + RUN
static const char petruncmd[] = {'R', 'U', 'N', 0x0d, 0}; // RUN 
static const char petfbcmd[] = {1, 'S', 'Y', 'S', '4', '0', '9', '6', '0' ,0x0d, 0}; // RUN 

static bool repeating_timer_callback(struct repeating_timer *t) {
    if (repeat_cnt ) repeat_cnt--;
    if (repeat_cnt == 0) {
      if (prev_key) {
        pet_kup(prev_key);
        prev_key = 0;
      }
      if (send_cmdstring) {
        if (*cmdstring_pt) {
          int asciikey = (*cmdstring_pt++)&0x7f;
          if (asciikey == 1) {
            repeat_cnt = BOOT_SEQ_MS; 
            prev_key = 0;
          } 
          else
          if (asciikey == 2) {      
            prev_key = 0;
          } 
          else
          if (asciikey) {
            pet_kdown( asciikey, false, false);
            prev_key = asciikey;
            repeat_cnt = KEY_DEBOUNCE_MS; 
          }
        }
        else {
          send_cmdstring = false;
        }
      }
    } 
    return true;
}

#ifdef HAS_USBHOST
// ****************************************
// USB keyboard
// ****************************************
void signal_joy (int code, int pressed) {
  if ( (code == KBD_KEY_DOWN) && (pressed) ) joystick0 &= ~JOY_DOWN;
  if ( (code == KBD_KEY_DOWN) && (!pressed) ) joystick0 |= JOY_DOWN;
  if ( (code == KBD_KEY_UP) && (pressed) ) joystick0 &= ~JOY_UP;
  if ( (code == KBD_KEY_UP) && (!pressed) ) joystick0 |= JOY_UP;
  if ( (code == KBD_KEY_LEFT) && (pressed) ) joystick0 &= ~JOY_LEFT;
  if ( (code == KBD_KEY_LEFT) && (!pressed) ) joystick0 |= JOY_LEFT;
  if ( (code == KBD_KEY_RIGHT) && (pressed) ) joystick0 &= ~JOY_RIGHT;
  if ( (code == KBD_KEY_RIGHT) && (!pressed) ) joystick0 |= JOY_RIGHT;
  if ( (code == ' ') && (pressed) ) joystick0 &= ~JOY_FIRE;
  if ( (code == ' ') && (!pressed) ) joystick0 |= JOY_FIRE;
}

void kbd_signal_raw_key (int keycode, int code, int codeshifted, int flags, int pressed) {
  // LCTRL + LSHIFT + R => reset
  if ( ( (flags & (KBD_FLAG_LSHIFT + KBD_FLAG_LCONTROL)) == (KBD_FLAG_LSHIFT + KBD_FLAG_LCONTROL) ) && (!pressed) && (code == 'r') ) {
    got_reset = true;
  }

  // LCTRL + LSHIFT + J => keyboard as joystick
  if ( ( (flags & (KBD_FLAG_LSHIFT + KBD_FLAG_LCONTROL)) == (KBD_FLAG_LSHIFT + KBD_FLAG_LCONTROL) ) && (!pressed) && (code == 'j') ) {
    if (kbdasjoy == true) kbdasjoy = false; 
    else kbdasjoy = true;
  }

  //keyboard as joystick?
  if (kbdasjoy == true) {
      if (prev_code) signal_joy(prev_code, 0);
      if (code) {
        signal_joy(code, pressed);
        if (pressed) prev_code = code;
      }  
      //mem[REG_TEXTMAP_L1] = joystick0;
  }
  else {
    if (!(flags & (KBD_FLAG_RSHIFT + KBD_FLAG_RCONTROL))) {
      if (codeshifted == '&') {code = '6'; flags |= KBD_FLAG_RSHIFT; }
      else if (codeshifted == '\"') {code = '2'; flags |= KBD_FLAG_RSHIFT; }
      else if (codeshifted == '\'') {code = '7'; flags |= KBD_FLAG_RSHIFT; }
      else if (codeshifted == '(') {code = '8'; flags |= KBD_FLAG_RSHIFT; }
      else if (codeshifted == '!') {code = '1'; flags |= KBD_FLAG_RSHIFT; }
      else if (codeshifted == '*') {code = ':'; flags |= KBD_FLAG_RSHIFT; }
      else if (codeshifted == '%') {code = '5'; flags |= KBD_FLAG_RSHIFT; }
      else if (codeshifted == '?') {code = '/'; flags |= KBD_FLAG_RSHIFT; }
      else if (codeshifted == '.') {code = '.'; flags |= 0; }
      else if (codeshifted == '+') {code = ';'; flags |= KBD_FLAG_RSHIFT; }
      else if (codeshifted == '>') {code = '.'; flags |= KBD_FLAG_RSHIFT; }
      else if (codeshifted == ')') {code = '9'; flags |= KBD_FLAG_RSHIFT; }
      else if (codeshifted == '$') {code = '4'; flags |= KBD_FLAG_RSHIFT; }
      else if (codeshifted == '=') {code = '-'; flags |= KBD_FLAG_RSHIFT; }
      else if (codeshifted == '<') {code = ','; flags |= KBD_FLAG_RSHIFT; }
      else if (codeshifted == KBD_KEY_DOWN) {code = 0x11; flags |= 0; }
      else if (codeshifted == KBD_KEY_RIGHT) {code = 0x1D; flags |= 0; }
      else if (codeshifted == KBD_KEY_UP) {code = 0x11; flags |= KBD_FLAG_RSHIFT; }
      else if (codeshifted == KBD_KEY_LEFT) {code = 0x1D; flags |= KBD_FLAG_RSHIFT; }
      else if (codeshifted == KBD_KEY_ESC) {code = 0x9B; flags |= 0; }
      // no PET chars for below characters!!!
      else if (codeshifted == '@') {code = 0; flags |= 0; }
      else if (codeshifted == '[') {code = 0; flags |= 0; }
      else if (codeshifted == ']') {code = 0; flags |= 0; }
      else if (codeshifted == '^') {code = 0; flags |= 0; }
      else if (codeshifted == '{') {code = 0; flags |= 0; }
      else if (codeshifted == '}') {code = 0; flags |= 0; }
      else if (codeshifted == '_') {code = 0; flags |= 0; }
      else if ( (codeshifted >= 'a') && (codeshifted <= 'z') ) { code = toupper(code); }
      else if ( (codeshifted >= 'A') && (codeshifted <= 'Z') ) { code = codeshifted; flags |= KBD_FLAG_RSHIFT; }
      else code = codeshifted;
    }
    else {
      code = toupper(code);
    }  

    if (prev_code) pet_kup(prev_code);

    if (code) {
      if (pressed == KEY_PRESSED)
      {
        pet_kdown(code, flags & KBD_FLAG_RSHIFT, flags & KBD_FLAG_RCONTROL);
        prev_code = code;
        //printf("kdown %c\r\n", kbd_to_ascii (code, flags));
      }
      else 
      {
        pet_kup(code);
        //printf("kup %c\r\n", kbd_to_ascii (code, flags));
      }
    }    
  }  
}


#else

// ****************************************
// USB SERIAL server
// ****************************************
static int serial_rx(uint8_t* buf, int len) {
  uint8_t asciikey;

  if (len >= 1) {
    switch (buf[0]) {
      case sercmd_reset:
        pet_reset();
        break;
      case sercmd_key:        
        asciikey = toupper((char)buf[1]);
        if (asciikey) {
          pet_kdown( asciikey, false, false);
          prev_key = asciikey;
          repeat_cnt = KEY_DEBOUNCE_MS; 
        }
        break;
      case sercmd_prg:
        pet_prg_write((uint8_t *)&buf[1],len-1); 
        break;
      case sercmd_run:
        pet_prg_run();
        cmdstring_pt = &petruncmd[0];
        send_cmdstring = true;
        repeat_cnt = 0; 
        break;
      default:
        break;
    }
  }  
  return 0;
}
#endif

#endif

#ifdef HAS_PETIO
// Petbus PIO config
#define CONFIG_PIN_PETBUS_DATA_BASE 0 /* 8+1(RW) pins */
#define CONFIG_PIN_PETBUS_RW (CONFIG_PIN_PETBUS_DATA_BASE + 8)
#define CONFIG_PIN_PETBUS_CONTROL_BASE (CONFIG_PIN_PETBUS_DATA_BASE + 9) //CE DATA,ADDRLO,ADDRHI
#define CONFIG_PIN_PETBUS_PHI2  26
#define CONFIG_PIN_PETBUS_DATADIR 28
#define CONFIG_PIN_PETBUS_RESET 22

#define VALID_CYCLE ((1 << CONFIG_PIN_PETBUS_PHI2) | (1 << CONFIG_PIN_PETBUS_RESET))

const PIO pio = pio1;
const uint sm = 0;
const uint smread = 1;

#define RESET_TRESHOLD 15000
static uint32_t reset_counter = 0;

extern uint8_t cmd;

extern uint8_t __not_in_flash("cmd_params") cmd_params[MAX_PAR];
extern int tra_h;
extern uint8_t __not_in_flash("cmd_params_len") cmd_params_len[]; 
extern void __not_in_flash("traParamFuncPtr") (*traParamFuncPtr[])(void);
extern void __not_in_flash("traDataFuncPtr") (*traDataFuncPtr[])(uint8_t);
extern void pushCmdQueue(QueueItem cmd );

static uint8_t cmd_param_ind;
static uint8_t cmd_tra_depth;

/********************************
 * petio PIO read table
********************************/ 
static void __not_in_flash("readNone") readNone(uint32_t address) {
  pio1->txf[smread] = 0;
}

static void __not_in_flash("read8000") read8000(uint32_t address) {
  pio1->txf[smread] = 0x100 | mem[address-0x8000];
}

static void __not_in_flash("read9000") read9000(uint32_t address) {
  pio1->txf[smread] = 0x100 | mem[address-0x8000];
}

static void __not_in_flash("readA000") readA000(uint32_t address) {
#ifdef PETIO_A000
  pio1->txf[smread] = 0x100 | mem_a000[address-0xa000];
#else
  pio1->txf[smread] = 0;
#endif
}

static void __not_in_flash("readE000") readE000(uint32_t address) {
#ifdef PETIO_EDIT        
  if (address < 0xe800) {
    pio1->txf[smread] = 0x100 | mem_e000[address-0xe000];
  }
  else {
    pio1->txf[smread] = 0;
  }
#else
  pio1->txf[smread] = 0;
#endif
}

static void __not_in_flash("readFuncTable") (*readFuncTable[16])(uint32_t)
{
  readNone, // 0
  readNone, // 1
  readNone, // 2 
  readNone, // 3
  readNone, // 4
  readNone, // 5
  readNone, // 6
  readNone, // 7
  readNone, // 8
  read9000, // 9
  readA000, // a
  readNone, // b
  readNone, // c
  readNone, // d
  readE000, // e
  readNone, // f
};

/********************************
 * petio PIO write table
********************************/ 
static void __not_in_flash("writeNone") writeNone(uint32_t address, uint8_t value) {
  pio_sm_drain_tx_fifo(pio,smread);
}

static void __not_in_flash("write89000") write89000(uint32_t address, uint8_t value) {
  switch (address-0x8000) 
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
      mem[address-0x8000] = value;
      break;
  } 
}

static void __not_in_flash("writeA000") writeA000(uint32_t address, uint8_t value) {
#ifdef PETIO_A000
  mem_a000[address-0xa000] = value;
#endif
}

static void __not_in_flash("writeE000") writeE000(uint32_t address, uint8_t value) {
  if (address == 0xe84C)
  {
    // e84C 12=LO, 14=HI
    if (value & 0x02)
    {
      font_lowercase = true;
    }
    else
    {
      font_lowercase = false;
    }
  }
}

static void __not_in_flash("writeFuncTable") (*writeFuncTable[16])(uint32_t,uint8_t)
{
  writeNone, // 0
  writeNone, // 1
  writeNone, // 2 
  writeNone, // 3
  writeNone, // 4
  writeNone, // 5
  writeNone, // 6
  writeNone, // 7
  write89000, // 8
  write89000, // 9
  writeA000, // a
  writeNone, // b
  writeNone, // c
  writeNone, // d
  writeE000, // e
  writeNone, // f
};


/********************************
 * petio loop
********************************/ 
void __not_in_flash("__time_critical_func") petbus_loop(void) {
  for(;;) {
    //uint32_t allgpios = sio_hw->gpio_in; 
    //if ((allgpios & VALID_CYCLE) == VALID_CYCLE) {
      uint32_t value = pio_sm_get_blocking(pio, sm);
      const bool is_write = ((value & (1u << (CONFIG_PIN_PETBUS_RW - CONFIG_PIN_PETBUS_DATA_BASE))) == 0);
      uint16_t address = (value >> 9) & 0xffff;      
      if (is_write)
      {
        writeFuncTable[address>>12](address, value & 0xff);
      }
      else {
        readFuncTable[address>>12](address);
      }      
    //}
  }
}

/********************************
 * Initialization
********************************/ 
void petbus_init(void)
{ 
  // Init PETBUS read SM
  uint progra_offsetread = pio_add_program(pio, &petbus_device_read_program);
  pio_sm_claim(pio, smread);
  pio_sm_config cread = petbus_device_read_program_get_default_config(progra_offsetread);
  // map the OUT pin group to the data signals
  sm_config_set_out_pins(&cread, CONFIG_PIN_PETBUS_DATA_BASE, 8);
  // map the SET pin group to the Data transceiver control signals (+ CS 9000/A000/E000)
  sm_config_set_set_pins(&cread, CONFIG_PIN_PETBUS_DATADIR, 1);
  pio_sm_init(pio, smread, progra_offsetread, &cread);

  // Init PETBUS main SM
  uint progra_offset = pio_add_program(pio, &petbus_program);
  pio_sm_claim(pio, sm);
  pio_sm_config c = petbus_program_get_default_config(progra_offset);
  // set the bus R/W pin as the jump pin
  sm_config_set_jmp_pin(&c, CONFIG_PIN_PETBUS_RW);
  // map the IN pin group to the data signals
  sm_config_set_in_pins(&c, CONFIG_PIN_PETBUS_DATA_BASE);
  // map the SET pin group to the bus transceiver enable signals
  sm_config_set_set_pins(&c, CONFIG_PIN_PETBUS_CONTROL_BASE, 3);
  // configure left shift into ISR & autopush every 25 bits
  sm_config_set_in_shift(&c, false, true, 24+1);
  pio_sm_init(pio, sm, progra_offset, &c);

  // configure the GPIOs
  // Ensure all transceivers disabled and datadir is 1 (input) 
  pio_sm_set_pins_with_mask(
      pio, sm, ((uint32_t)0x7 << CONFIG_PIN_PETBUS_CONTROL_BASE) | ((uint32_t)0x1 << CONFIG_PIN_PETBUS_DATADIR) , 
               ((uint32_t)0x7 << CONFIG_PIN_PETBUS_CONTROL_BASE) | ((uint32_t)0x1 << CONFIG_PIN_PETBUS_DATADIR) );
  pio_sm_set_pindirs_with_mask(pio, sm, ((uint32_t)0x7 << CONFIG_PIN_PETBUS_CONTROL_BASE) | ((uint32_t)0x1 << CONFIG_PIN_PETBUS_DATADIR),
      ((uint32_t)0x1 << CONFIG_PIN_PETBUS_PHI2) | ((uint32_t)0x1 << CONFIG_PIN_PETBUS_RESET) | ((uint32_t)0x7 << CONFIG_PIN_PETBUS_CONTROL_BASE) | ((uint32_t)0x1 << CONFIG_PIN_PETBUS_DATADIR) | ((uint32_t)0x1ff << CONFIG_PIN_PETBUS_DATA_BASE));

  // Disable input synchronization on input pins that are sampled at known stable times
  // to shave off two clock cycles of input latency
  pio->input_sync_bypass |= (0x1ff << CONFIG_PIN_PETBUS_DATA_BASE);
  
  pio_gpio_init(pio, CONFIG_PIN_PETBUS_PHI2);
  gpio_set_pulls(CONFIG_PIN_PETBUS_PHI2, false, false);
  pio_gpio_init(pio, CONFIG_PIN_PETBUS_RESET);
  gpio_set_pulls(CONFIG_PIN_PETBUS_RESET, false, false);

  for(int pin = CONFIG_PIN_PETBUS_CONTROL_BASE; pin < CONFIG_PIN_PETBUS_CONTROL_BASE + 3; pin++) {
      pio_gpio_init(pio, pin);
  }
  pio_gpio_init(pio, CONFIG_PIN_PETBUS_DATADIR);

  for(int pin = CONFIG_PIN_PETBUS_DATA_BASE; pin < CONFIG_PIN_PETBUS_DATA_BASE + 9; pin++) {
      pio_gpio_init(pio, pin);
      gpio_set_pulls(pin, false, false);
  }

//  pio_sm_set_enabled(pio, sm, true);

  // Disable all interrupts on this core
  /*
  irq_set_enabled(TIMER_IRQ_0, false);
  irq_set_enabled(TIMER_IRQ_1, false);
  irq_set_enabled(TIMER_IRQ_2, false);
  irq_set_enabled(TIMER_IRQ_3, false);
  irq_set_enabled(PWM_IRQ_WRAP, false);
  irq_set_enabled(USBCTRL_IRQ, false);
  irq_set_enabled(XIP_IRQ, false);
  irq_set_enabled(PIO0_IRQ_0, false);
  irq_set_enabled(PIO0_IRQ_1, false);
  irq_set_enabled(PIO1_IRQ_0, false);
  irq_set_enabled(PIO1_IRQ_1, false);
  irq_set_enabled(DMA_IRQ_0, false);
  irq_set_enabled(DMA_IRQ_1, false);
  irq_set_enabled(IO_IRQ_BANK0, false);
  irq_set_enabled(IO_IRQ_QSPI , false);
  irq_set_enabled(SIO_IRQ_PROC0, false);
  irq_set_enabled(SIO_IRQ_PROC1, false);
  irq_set_enabled(CLOCKS_IRQ  , false);
  irq_set_enabled(SPI0_IRQ  , false);
  irq_set_enabled(SPI1_IRQ  , false);
  irq_set_enabled(UART0_IRQ , false);
  irq_set_enabled(UART1_IRQ , false);
  irq_set_enabled(I2C0_IRQ, false);
  irq_set_enabled(I2C1_IRQ, false);
  irq_set_enabled(RTC_IRQ, false);
  */

  pio_enable_sm_mask_in_sync(pio, (1 << sm) | (1 << smread));
  pio_sm_clear_fifos(pio,sm);
  pio_sm_clear_fifos(pio,smread);
}


/********************************
 * Check for reset
********************************/ 
extern bool petbus_poll_reset(void)
{  
  bool retval = false;
  // low is reset => true
  bool reset_state = !(sio_hw->gpio_in & (1 << CONFIG_PIN_PETBUS_RESET));
  if (reset_state) {
    if (!got_reset) {
      if (reset_counter < RESET_TRESHOLD) {
        reset_counter++;
        mem[0] = mem[0] + 1; 
      }
      else {
        got_reset = true;
        retval = true;
      }
    }
  }
  else {
    got_reset = false;
    reset_counter = 0;
  }
  return retval;
}
#endif


static void __not_in_flash("pio_core") pio_core(void)
{
  while(true) { 
#ifdef CPU_EMU
    if (got_reset)
    {
      got_reset = false;
      HyperGfxReset();
      sleep_ms(30);
      prev_key = 0;
      pet_start();      
      pet_running = true;
#ifndef NO_HYPER      
      cmdstring_pt = &petfbcmd[0];
      send_cmdstring = true;
      repeat_cnt = 0;
#endif      
    }
    for (int i = 8; i < 408; i = i + 2) {
        hdmi_wait_line(i);
        pet_line();
    }
    pet_remaining();
#else
    if (got_reset)
    {
      got_reset = false;
      HyperGfxReset();
    }
#endif 
    __dmb();
  }
}


void start_system(void) 
{
#ifdef CPU_EMU
#ifdef HAS_USBHOST
  //board_init();
  tuh_init(BOARD_TUH_RHPORT);
#else
  usb_serial_init(&serial_rx);
#endif
  struct repeating_timer timer;
  add_repeating_timer_ms(-1, repeating_timer_callback, NULL, &timer);
#ifndef NO_HYPER
  cmdstring_pt = &petfbcmd[0];
  send_cmdstring = true;
  repeat_cnt = 0;
#endif  
#endif

  HyperGfxFlashFSInit();  
  HyperGfxInit();

#ifndef NO_HYPER
  // A000 area content is file browser default
#ifdef PETIO_A000
  memcpy((void *)&mem_a000[0], (void *)fb, sizeof(fb));
//  memcpy((void *)&mem_a000[0], (void *)vsync, sizeof(vsync));
#endif 
#endif

#ifdef CPU_EMU
  pet_start(); 
  multicore_launch_core1(pio_core);

#else
  multicore_launch_core1(pio_core);
#ifdef BUS_DEBUG
  memptr = 0;
  memptw = 0;
#endif
#endif

  while(true) {
    HyperGfxHandleGfx();    
#ifdef BUS_DEBUG
    DebugShow();    
#endif
    HyperGfxHandleCmdQueue();
#if (defined(CPU_EMU) || defined(CPU_Z80))
#ifdef HAS_USBHOST
    // tinyusb host task
    tuh_task();
    hid_app_task();
#endif
#endif        
    __dmb();        
  }
}