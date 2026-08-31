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

#ifndef CPU_EMU
#include "busreadwrite.pio.h"
#include "clock.pio.h"
#endif

#include "hypergfx.h"


#include "edit4.h"
#include "edit480.h"
#include "edit450.h"
#include "edit48050.h"
#include "memory.h"

#ifdef PETIO_A000
#include "fb.h"
//#include "vsync.h"
#endif         

uint8_t memory[MEMORY_SIZE];

#if (defined(CPU_EMU) || defined(CPU_6502))
#ifdef CPU_EMU
// 6502 emu
#include "mos6502.h"
#endif
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


// 6502 emu
#ifdef CPU_EMU
static mos6502 mos;
#endif
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

static bool got_reset = false;



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



#if (defined(CPU_EMU) || defined(CPU_6502))


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
      memory[prg_add_cur++] = *src++;
    } 
    length  = length - 1;
    if ( length == 0) return;
  }
}

static void pet_prg_run( void )
{
  uint8_t lo,hi;
  memory[0xc7] = memory[0x28];
  memory[0xc8] = memory[0x29];

  lo = (uint8_t)(prg_add_cur & 0xff);
  hi = (uint8_t)(prg_add_cur >> 8);
  memory[0x2a] = lo;
  memory[0x2c] = lo;
  memory[0x2e] = lo;
  memory[0xc9] = lo;
  memory[0x2b] = hi;
  memory[0x2d] = hi;
  memory[0x2f] = hi;
  memory[0xca] = hi;

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
  memset((void *)&memory[0], 0, RAM_SIZE); 
#ifdef CPU_EMU
  mos.Reset();
#endif
  for (int i = sizeof(_rows); i--; )
    _rows[i] = 0;
  pet_running = true;
  prg_start = false;
  prg_wr = 0;
}


#ifdef CPU_EMU
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
#endif

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
static const char petfbcmd[] = {1,1,1, 'S', 'Y', 'S', '4', '0', '9', '6', '0' ,0x0d, 0}; // RUN 

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

#ifndef CPU_EMU
/********************************
 * PIO variables/config
********************************/ 

// 6502 PIO config
#define CONFIG_PIN_PETBUS_RW (CONFIG_PIN_BUS_DATA_BASE + 8)
#define CONFIG_PIN_PETBUS_PHI2  26

// real PET PIO config (and control pin reversed!)
//#define CONFIG_PIN_PETBUS_DATADIR 28
//#define CONFIG_PIN_PETBUS_RESET 22


// standalone PET
#define CONFIG_PIN_PETBUS_DATADIR 22
//#define CONFIG_PIN_PETBUS_RESET 28

static PIO bus_pio = pio1;
static uint bus_smw = 0;
static uint bus_smr = 1;

#ifdef CPU_6502
static PIO clock_pio;
static uint clock_sm;
#endif

#define PIO_CLK_DIV 1.0f // 8MHz/8  (pio slower)

#define PIO_BUS_DIV 1.0f // 8MHz/8  (pio slower)

#endif



/********************************
 * read table
********************************/ 
#ifdef CPU_EMU  
static uint8_t __not_in_flash("readMEM") readMEM(uint16_t address) {
  return (memory[address]);
#else
static void __not_in_flash("readMEM") readMEM(uint16_t address) {
  bus_pio->txf[bus_smr] = 0x100 | memory[address];
#endif  
}

#ifdef CPU_EMU  
static uint8_t __not_in_flash("readNone") readNone(uint16_t address) {
  return (memory[address]);
#else
static void __not_in_flash("readNone") readNone(uint16_t address) {
//  bus_pio->txf[bus_smr] = 0x100 | memory[address];
  bus_pio->txf[bus_smr] = 0;
#endif  
}

#ifdef CPU_EMU  
static uint8_t __not_in_flash("read9000") read9000(uint16_t address) {
  return (memory[address]);
#else
static void __not_in_flash("read9000") read9000(uint16_t address) {
  bus_pio->txf[bus_smr] = 0x100 | memory[address];
#endif  
}

#ifdef CPU_EMU  
static uint8_t __not_in_flash("readA000") readA000(uint16_t address) {
#ifdef PETIO_A000
  return (memory[address]);
#else
  return 0;
#endif
#else
static void __not_in_flash("readA000") readA000(uint16_t address) {
#ifdef PETIO_A000
  bus_pio->txf[bus_smr] = 0x100 | memory[address];
#else
  bus_pio->txf[bus_smr] = 0;
#endif
#endif  
}

#ifdef CPU_EMU  
static uint8_t __not_in_flash("readE000") readE000(uint16_t address) {
  if (address < 0xe800) {
    return (memory[address]);
  }
  else {    
    if (address == 0xe812)         // PORT B
      return (_rows[_row] ^ 0xff);    
    else if (address == 0xe810)    // PORT A
      return (_row | 0x80); 
#ifdef HAS_USBHOST
    else if (address == 0xe84f)    // PORT Joystick
      return (joystick0); 
#endif
    else
      return 0x00;
  } 
#else
static void __not_in_flash("readE000") readE000(uint16_t address) {
  if (address < 0xe800) {
#ifdef PETIO_EDIT
    bus_pio->txf[bus_smr] = 0x100 | memory[address];
#else
    bus_pio->txf[bus_smr] = 0;
#endif
  }
#ifdef CPU_6502
  else {
    if (address == 0xe812)         // PORT B
      bus_pio->txf[bus_smr] = 0x100 | (_rows[_row] ^ 0xff);
    else if (address == 0xe810)    // PORT A
      bus_pio->txf[bus_smr] = 0x100 | (_row | 0x80); 
#ifdef HAS_USBHOST
    else if (address == 0xe84f)    // PORT Joystick
      bus_pio->txf[bus_smr] = 0x100 | joystick0; 
#endif
    else
      bus_pio->txf[bus_smr] = 0x100 | 0;
  }
#else
  bus_pio->txf[bus_smr] = 0;
#endif  
#endif  
}

#ifdef CPU_EMU  
static uint8_t __not_in_flash("readFuncTable") (*readFuncTable[16])(uint16_t)
#else
static void __not_in_flash("readFuncTable") (*readFuncTable[16])(uint16_t)
#endif
{
#if (defined(PET_16K) || defined(PET_32K))
  readNone, // 0 (first 16k)
  readNone, // 1
  readNone, // 2 
  readNone, // 3
#else
  readMEM, // 0
  readMEM, // 1
  readMEM, // 2 
  readMEM, // 3
#endif
#if (defined(PET_32K))
  readNone, // 4 (second 16k)
  readNone, // 5
  readNone, // 6
  readNone, // 7
#else
  readMEM, // 4
  readMEM, // 5
  readMEM, // 6
  readMEM, // 7
#endif
#if (defined(PET_16K) || defined(PET_32K))
  readNone, // 8
  read9000, // 9
  readA000, // a
  readNone, // b
  readNone, // c
  readNone, // d
  readE000, // e
  readNone, // f
#else
  readMEM,  // 8
  readMEM,  // 9
  readMEM,  // a
  readMEM,  // b
  readMEM,  // c
  readMEM,  // d
  readE000, // e
  readMEM,  // f
#endif  
};

/********************************
 * write table
********************************/ 
static void __not_in_flash("writeNone") writeNone(uint16_t address, uint8_t value) {

#ifdef CPU_EMU  
#else
//  pio_sm_drain_tx_fifo(bus_pio,bus_smr);
#endif  
}

static void __not_in_flash("writeMEM") writeMEM(uint16_t address, uint8_t value) {
#ifdef CPU_EMU
  memory[address] = value;  
#else
  memory[address] = value; 
#endif  
}


static void __not_in_flash("writeA000") writeA000(uint16_t address, uint8_t value) {
#ifdef CPU_EMU  
  memory[address] = value;  
#else
#ifdef PETIO_A000
  memory[address] = value;  
#endif
#endif  
}

static void __not_in_flash("writeE000") writeE000(uint16_t address, uint8_t value) {
#ifdef CPU_EMU
  if (address == 0xe812)       // PORT B
  {
  } 
  else if (address == 0xe810)  // PORT A
  {
    _row = (value & 0x0f);
  }          
  else if (address == 0xe84C) {
    if (value & 0x02) 
    {
      font_lowercase = true;
    }
    else 
    {
      font_lowercase = false;
    }
  }  
#else
#ifdef CPU_6502  
  if (address == 0xe812)       // PORT B
  {
  } 
  else if (address == 0xe810)  // PORT A
  {
    _row = (value & 0x0f);
  } else  
#endif  
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
#endif  
}

static void __not_in_flash("writeFuncTable") (*writeFuncTable[16])(uint16_t,uint8_t)
{
#if (defined(PET_16K) || defined(PET_32K))
  writeNone, // 0 (first 16k)
  writeNone, // 1
  writeNone, // 2 
  writeNone, // 3
#else
  writeMEM, // 0
  writeMEM, // 1
  writeMEM, // 2 
  writeMEM, // 3
#endif
#if (defined(PET_32K))
  writeNone, // 4 (second 16k)
  writeNone, // 5
  writeNone, // 6
  writeNone, // 7
#else
  writeMEM, // 4
  writeMEM, // 5
  writeMEM, // 6
  writeMEM, // 7
#endif
  //HyperGfxWrite, // 8
  writeMEM,
  HyperGfxWrite, // 9
  writeA000, // a
  writeNone, // b
  writeNone, // c
  writeNone, // d
  writeE000, // e
  writeNone, // f
};


#ifdef CPU_EMU
uint8_t readWord( uint16_t location)
{
  return readFuncTable[location>>12](location);
}

void writeWord( uint16_t location, uint8_t value)
{
  writeFuncTable[location>>12](location,value);
}

#else

#ifdef BUS_DEBUG
static char hex[16] = {'0','1','2','3','4','5','6','7','8','9',1,2,3,4,5,6};
static int firstadd=0;
#endif

/********************************
 * PIO code and init
********************************/ 

void __not_in_flash("__time_critical_func") pioirq_smw(void) {
  //if(!pio_sm_is_rx_fifo_empty(bus_pio, bus_smw)) {
    uint32_t value = pio_sm_get(bus_pio, bus_smw);
    const bool is_write = ((value & (1u << (CONFIG_PIN_PETBUS_RW))) == 0);
    uint16_t address = (value >> 9) & 0xffff;
    if (is_write)
    {
#ifdef BUS_DEBUG
      if (firstadd < 64) {
        //memory[0x8050+firstadd++] = hex[(value>4) & 0xf];
        //memory[0x8050+firstadd++] = hex[(value>) & 0xf];
        memory[0x8050+firstadd++] = hex[(address>>12) & 0xf];
        memory[0x8050+firstadd++] = hex[(address>>8)  & 0xf];
        memory[0x8050+firstadd++] = hex[(address>>4)  & 0xf];
        memory[0x8050+firstadd++] = hex[(address)     & 0xf];
      }
#endif
      writeFuncTable[address>>12](address, value & 0xff);
    }
    else {
#ifdef BUS_DEBUG
      if (firstadd <64) {
        //memory[0x8000+firstadd++] = hex[(value>4) & 0xf];
        //memory[0x8000+firstadd++] = hex[(value) & 0xf];
        memory[0x8000+firstadd++] = hex[(address>>12) & 0xf];
        memory[0x8000+firstadd++] = hex[(address>>8)  & 0xf];
        memory[0x8000+firstadd++] = hex[(address>>4)  & 0xf];
        memory[0x8000+firstadd++] = hex[(address)     & 0xf];
      }
      //bus_pio->txf[bus_smr] = 0xea;
#endif
      readFuncTable[address>>12](address);
    }     
  //}
}

void run_pio(void)
{
#ifdef CPU_6502
    clock_pio = pio0;
    clock_sm = 0;

    // Init CLOCK SM
    pio_sm_claim(clock_pio, clock_sm);
    uint clock_program_offset = pio_add_program(clock_pio, &clock_program);
    pio_sm_config cc = clock_program_get_default_config(clock_program_offset);
    // set pin as output and set
    //sm_config_set_out_pins(&cc, PET_CLOCK, 1);
    sm_config_set_set_pins(&cc, PET_CLOCK, 1);
    // Set this pin's GPIO function (connect PIO to the pad)
    pio_gpio_init(clock_pio, PET_CLOCK);
    // Set the pin direction to output at the PIO
    pio_sm_set_consecutive_pindirs(clock_pio, clock_sm, PET_CLOCK, 1, true);
    // Load our configuration, and jump to the start of the program
    pio_sm_init(clock_pio, clock_sm, clock_program_offset, &cc);
    pio_sm_set_clkdiv(clock_pio, clock_sm, PIO_CLK_DIV);    
#endif

  // Init PETBUS write SM
  pio_sm_claim(bus_pio, bus_smw);
  //pio_sm_clear_fifos(bus_pio,bus_smw);
  uint buswrite_program_offset = pio_add_program(bus_pio, &buswrite_program);
  pio_sm_config c = buswrite_program_get_default_config(buswrite_program_offset);
  // map the IN pin group to the data signals
  sm_config_set_in_pins(&c, CONFIG_PIN_BUS_DATA_BASE);
  // map the SET pin group to the bus transceiver enable signals
  sm_config_set_set_pins(&c, CONFIG_PIN_BUS_CONTROL_BASE, 3);
  // set the bus R/W pin as the jump pin
  sm_config_set_jmp_pin(&c, CONFIG_PIN_PETBUS_RW);
  // configure left shift into ISR & autopush every 25 bits
  sm_config_set_in_shift(&c, false, true, 24+1);
  pio_sm_init(bus_pio, bus_smw, buswrite_program_offset, &c);
  pio_sm_set_clkdiv(bus_pio, bus_smw, PIO_BUS_DIV);

  // configure the GPIOs
  // Ensure all transceivers disabled (1) and datadir is 0 (input) 
  // value,mask
  pio_sm_set_pins_with_mask(
      bus_pio, bus_smw, ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) | ((uint32_t)0x1 << CONFIG_PIN_PETBUS_DATADIR) , 
               ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) | ((uint32_t)0x1 << CONFIG_PIN_PETBUS_DATADIR) );
  // dir(1=out,0=in),mask
  pio_sm_set_pindirs_with_mask(bus_pio, bus_smw, ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) | ((uint32_t)0x1 << CONFIG_PIN_PETBUS_DATADIR),
      ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) | ((uint32_t)0x1 << CONFIG_PIN_PETBUS_DATADIR) | ((uint32_t)0x1ff << CONFIG_PIN_BUS_DATA_BASE) | ((uint32_t)0x1 << CONFIG_PIN_PETBUS_PHI2));

  // value,mask
  //pio_sm_set_pins_with_mask(
  //  bus_pio, bus_smw, ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE), 
  //           ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) | ((uint32_t)0x1 << CONFIG_PIN_PETBUS_DATADIR)) ;
  // dir(1=out,0=in),mask
  //pio_sm_set_pindirs_with_mask(bus_pio, bus_smw, ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) /*| ((uint32_t)0x1 << CONFIG_PIN_PETBUS_DATADIR)*/ ,
  //  ((uint32_t)0x1 << CONFIG_PIN_PETBUS_PHI2) | ((uint32_t)0x1 << CONFIG_PIN_PETBUS_RW) | ((uint32_t)0xff << CONFIG_PIN_BUS_DATA_BASE) | ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) /*| ((uint32_t)0x1 << CONFIG_PIN_PETBUS_DATADIR)*/ );

  // Init PETBUS read SM
  pio_sm_claim(bus_pio, bus_smr);
  //pio_sm_clear_fifos(bus_pio,bus_smr);
  uint busread_program_offset = pio_add_program(bus_pio, &busread_program);
  pio_sm_config cread = busread_program_get_default_config(busread_program_offset);
  // map the IN/OUT pin group to the data signals
  sm_config_set_in_pins(&cread, CONFIG_PIN_BUS_DATA_BASE);
  sm_config_set_out_pins(&cread, CONFIG_PIN_BUS_DATA_BASE, 8);
  // map the SET pin group to the Data transceiver control signals (+ CS 9000/A000/E000)
  sm_config_set_set_pins(&cread, CONFIG_PIN_PETBUS_DATADIR, 1);
  pio_sm_init(bus_pio, bus_smr, busread_program_offset, &cread);
  pio_sm_set_clkdiv(bus_pio, bus_smr, PIO_BUS_DIV);
  // Set the pin direction to output at the PIO
  pio_sm_set_consecutive_pindirs(bus_pio, bus_smr, CONFIG_PIN_BUS_DATA_BASE, 8, false);

  // configure the GPIOs
  // Ensure all transceivers disabled (1) and datadir is 0 (input)
  // value,mask 
  //pio_sm_set_pins_with_mask(
  //    bus_pio, bus_smr, ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE), 
  //             ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) | ((uint32_t)0x1 << CONFIG_PIN_PETBUS_DATADIR) );
  // dir(1=out,0=in),mask
  //pio_sm_set_pindirs_with_mask(bus_pio, bus_smr, /*((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) |*/ ((uint32_t)0x1 << CONFIG_PIN_PETBUS_DATADIR),
  //    ((uint32_t)0x1 << CONFIG_PIN_PETBUS_PHI2)  | ((uint32_t)0xff << CONFIG_PIN_BUS_DATA_BASE) | /*((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) |*/ ((uint32_t)0x1 << CONFIG_PIN_BUS_DATADIR) );

  // Disable input synchronization on input pins that are sampled at known stable times
  // to shave off two clock cycles of input latency
  bus_pio->input_sync_bypass |= (0x1ff << CONFIG_PIN_BUS_DATA_BASE);
  
  // output: CONTROL and DATADIR
  for(int pin = CONFIG_PIN_BUS_CONTROL_BASE; pin < (CONFIG_PIN_BUS_CONTROL_BASE + 3); pin++) {
      pio_gpio_init(bus_pio, pin);
  }
  pio_gpio_init(bus_pio, CONFIG_PIN_PETBUS_DATADIR);

  // input: DATA pin and RW
  for(int pin = CONFIG_PIN_BUS_DATA_BASE; pin < (CONFIG_PIN_BUS_DATA_BASE+8+1); pin++) {
      pio_gpio_init(bus_pio, pin);
      gpio_set_pulls(pin, true, false);
  }
  pio_gpio_init(bus_pio, CONFIG_PIN_PETBUS_PHI2);
  //gpio_set_pulls(CONFIG_PIN_PETBUS_PHI2, true, false);

  //pio_gpio_init(bus_pio, CONFIG_PIN_PETBUS_RESET);
  //gpio_set_pulls(CONFIG_PIN_PETBUS_RESET, true, false);
  

  // Set pio IRQ to tell us when the RX FIFO is NOT empty
  pio_set_irq1_source_mask_enabled(bus_pio,(1u << pio_get_rx_fifo_not_empty_interrupt_source(bus_smw)), true);
  irq_set_exclusive_handler ((bus_pio == pio0) ? PIO0_IRQ_1 : PIO1_IRQ_1, pioirq_smw );
  irq_set_enabled((bus_pio == pio0) ? PIO0_IRQ_1 : PIO1_IRQ_1, true); // Enable the IRQ
  
  irq_set_priority ((bus_pio == pio0) ? PIO0_IRQ_0 : PIO1_IRQ_0, PICO_DEFAULT_IRQ_PRIORITY-64);
  //irq_set_priority ((bus_pio == pio0) ? PIO0_IRQ_1 : PIO1_IRQ_1, PICO_DEFAULT_IRQ_PRIORITY-64);


#ifdef CPU_6502
    gpio_init(CONFIG_PET_IRQ);
    gpio_set_dir(CONFIG_PET_IRQ, GPIO_OUT);
    gpio_put(CONFIG_PET_IRQ, 1);


    gpio_init(PET_RESET);
    gpio_set_dir(PET_RESET, GPIO_OUT);
    gpio_put(PET_RESET, 1);
    sleep_ms(50);
    gpio_put(PET_RESET, 0);
    sleep_ms(500);
    pio_sm_set_enabled(clock_pio, clock_sm, true);
    pio_enable_sm_mask_in_sync(bus_pio, (1 << bus_smr) | (1 << bus_smw));
    pio_sm_clear_fifos(bus_pio,bus_smr);
    pio_sm_clear_fifos(bus_pio,bus_smw);
    gpio_put(PET_RESET, 1);
#else
    pio_enable_sm_mask_in_sync(bus_pio, (1 << bus_smr) | (1 << bus_smw));
    pio_sm_clear_fifos(bus_pio,bus_smr);
    pio_sm_clear_fifos(bus_pio,bus_smw);
#endif
}
#endif




/********************************
 * Check for reset
********************************/ 
#define RESET_TRESHOLD 15000
static uint32_t reset_counter = 0;
static bool last_reset_state = false;

static bool __not_in_flash("poll_reset") poll_reset(void)
{  
  bool retval = false;
  // low is reset => true
  bool reset_state = !(gpio_get(PET_RESET));
  //bool reset_state = !(sio_hw->gpio_in & (1 << PET_RESET));
  if (reset_state) {
    if (!last_reset_state) {
      if (reset_counter < RESET_TRESHOLD) {
        reset_counter++;
        memory[0x8000] = memory[0x8000] + 1;
        retval = true;
      }
    }
  }
  else {
    reset_counter = 0;
  }
  last_reset_state = reset_state;
  return retval;
}

static void __not_in_flash("pio_core") pio_core(void)
{
#ifndef CPU_EMU
  run_pio();
#endif  
  while(true) { 
    if (got_reset)
    {
      got_reset = false;
      HyperGfxReset();
#if (defined(CPU_EMU) || defined(CPU_6502))
      sleep_ms(30);
      prev_key = 0;
#endif
#ifdef CPU_EMU
      pet_start();      
      pet_running = true;
#endif
#if (defined(CPU_EMU) || defined(CPU_6502))
      if (hyper_enabled) {
        cmdstring_pt = &petfbcmd[0];
        send_cmdstring = true;
        repeat_cnt = 0;
      }
#endif
    }
#ifdef CPU_EMU
    for (int i = 8; i < 408; i = i + 2) {
        hdmi_wait_line(i);
        pet_line();
    }
    pet_remaining();
#endif
    __dmb();
  }
}


void start_system(void) 
{
  hyper_enabled = true;

#if (defined(CPU_EMU) || defined(CPU_6502))
#ifdef HAS_USBHOST
  //board_init();
  tuh_init(BOARD_TUH_RHPORT);
#else
  usb_serial_init(&serial_rx);
#endif
  struct repeating_timer timer;
  add_repeating_timer_ms(-1, repeating_timer_callback, NULL, &timer);
  if (hyper_enabled) {
    cmdstring_pt = &petfbcmd[0];
    send_cmdstring = true;
    repeat_cnt = 0;
  }
#endif

  HyperGfxFlashFSInit();  
  HyperGfxInit();

  if (hyper_enabled) {
    // A000 area content is file browser default
#ifdef PETIO_A000
    memcpy((void *)&memory[0xa000], (void *)fb, sizeof(fb));
#endif 
  }
  memcpy((void *)&memory[0xb000], (void *)basic4_b000, sizeof(basic4_b000));
  memcpy((void *)&memory[0xc000], (void *)basic4_c000, sizeof(basic4_c000));
  memcpy((void *)&memory[0xd000], (void *)basic4_d000, sizeof(basic4_d000));
#ifdef PETIO_EDIT  
  if (HyperGfxIsPal()) { 
    if (!HyperGfxIsHires())      
      memcpy((void *)&memory[0xe000], (void *)edit450, sizeof(edit450));
    else
      memcpy((void *)&memory[0xe000], (void *)edit48050, sizeof(edit48050));
  } 
  else {
    if (!HyperGfxIsHires())      
      memcpy((void *)&memory[0xe000], (void *)edit4, sizeof(edit4));
    else
      memcpy((void *)&memory[0xe000], (void *)edit480, sizeof(edit480));
  }    
#endif  
  memcpy((void *)&memory[0xf000], (void *)kernal4, sizeof(kernal4));

  pet_start(); 
  multicore_launch_core1(pio_core);
  // Configure RESET as input pin now!
#ifndef CPU_6502
  gpio_init(PET_RESET);
#else
  // wait for pio reset finished
  sleep_ms(600);
#endif
  gpio_set_dir(PET_RESET, GPIO_IN);
  gpio_set_pulls(PET_RESET, false, false);


  while(true) {    
    HyperGfxHandleGfx();
#ifdef CPU_6502     
    gpio_put(CONFIG_PET_IRQ, 0);
    sleep_us(5);
    gpio_put(CONFIG_PET_IRQ, 1);
#endif
    HyperGfxHandleCmdQueue();
    if (got_reset == false) {
      got_reset = poll_reset();
    }
#if (defined(CPU_EMU) || defined(CPU_6502))
#ifdef HAS_USBHOST
    // tinyusb host task
    tuh_task();
    hid_app_task();
#endif
#endif        
    __dmb();        
  }
}