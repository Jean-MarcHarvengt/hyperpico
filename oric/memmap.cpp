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

#define MEMORY_SIZE 0x10000

uint8_t memory[MEMORY_SIZE];
static uint8_t memrom[16384];
static bool got_reset=false;

#if (defined(CPU_EMU) || defined(CPU_6502))

#include "fbrom.h"
#include "basic11b.h"
//#include "basic11bP.h"

#define CPU_SLOT_FREQ 50
#define CPU_FREQ 1000000
#define PSG_FREQ 62500
#define SOUND_FREQ 24000
#define SOUND_SLOT_FREQ 100

#define ORIC_LINES  (260)
#define CYCLE_PER_LINE  (CPU_FREQ/(CPU_SLOT_FREQ*260))
#define ORIC_CYCLES (ORIC_LINES*CYCLE_PER_LINE)


#ifdef CPU_EMU
#include "mos6502.h"
static mos6502 mos;
#endif
#include "vrEmu6522.h"
static VrEmu6522 * Vr6522;

#include "hdmi_framebuffer.h"
#ifdef HAS_USBHOST
#include "bsp/board_api.h"
#include "tusb.h"
#include "kbd.h"
extern "C" void hid_app_task(void);
#else
#include "usb_serial.h"
#endif

// 0x310 (L) and 0x320 (R) 
//  1: up
//  2: down
//  4: fire
//  8: left
// 10: right

static uint8_t joystick0 = 0xdf;
static bool kbdasjoy = false;
// Joystick macros
#define JOY_RIGHT   (1)
#define JOY_LEFT    (2)
#define JOY_FIRE    (4)
#define JOY_DOWN    (8) 
#define JOY_UP      (16)
#endif



#if (defined(CPU_EMU) || defined(CPU_6502))

// ****************************************
// Keyboard
// ****************************************
typedef struct {
  uint8_t row;
  uint16_t colmask;
} ORICKey;

#define ADD_SHIFT_KEY 0x100
#define ADD_CTRL_KEY  0x200

static const ORICKey ascii2OricKeys[] = { // Ascii to ORIC keys
/* 0x00 */ {0, 0x00},
/* 0x01 */ {0, 0x00},
/* 0x02 */ {0, 0x00},
/* 0x03 */ {0, 0x00},
/* 0x04 */ {0, 0x00},
/* 0x05 */ {0, 0x00},
/* 0x06 */ {0, 0x00},
/* 0x07 */ {0, 0x00},
/* 0x08 */ {0, 0x00},
/* 0x09 */ {2, ADD_CTRL_KEY |0x80},  // tab
/* 0x0A */ {0, 0x00},
/* 0x0B */ {0, 0x00},
/* 0x0C */ {0, 0x00},
/* 0x0D */ {7, 0x20},  // enter
/* 0x0E */ {0, 0x00},
/* 0x0F */ {0, 0x00},
/* 0x10 */ {0, 0x00},
/* 0x11 */ {0, 0x00},
/* 0x12 */ {0, 0x00},
/* 0x13 */ {0, 0x00},
/* 0x14 */ {0, 0x00},
/* 0x15 */ {0, 0x00},
/* 0x16 */ {0, 0x00},
/* 0x17 */ {0, 0x00},
/* 0x18 */ {0, 0x00},
/* 0x19 */ {0, 0x00},
/* 0x1A */ {0, 0x00},
/* 0x1B */ {1, 0x20},   // esc
/* 0x1C */ {0, 0x00},
/* 0x1D */ {0, 0x00},
/* 0x1E */ {0, 0x00},
/* 0x1F */ {0, 0x00},
/* 0x20 */ {4, 0x01},  // space
/* 0x21 */ {0, ADD_SHIFT_KEY | 0x20},  // ! exclamation mark
/* 0x22 */ {3, ADD_SHIFT_KEY | 0x80},  // " double quote   
/* 0x23 */ {0, ADD_SHIFT_KEY | 0x80},  // # dies
/* 0x24 */ {2, ADD_SHIFT_KEY | 0x08},  // $ dollar
/* 0x25 */ {0, ADD_SHIFT_KEY | 0x04},  // % percent
/* 0x26 */ {0, ADD_SHIFT_KEY | 0x01},  // & ampersand
/* 0x27 */ {3, 0x80},  // ' singlequote
/* 0x28 */ {3, ADD_SHIFT_KEY | 0x02},  // ( bracket left
/* 0x29 */ {7, ADD_SHIFT_KEY | 0x04},  // ) bracket right
/* 0x2A */ {7, ADD_SHIFT_KEY | 0x01},  // * mult
/* 0x2B */ {7, ADD_SHIFT_KEY | 0x80},  // + plus
/* 0x2C */ {4, 0x02},  // , comma
/* 0x2D */ {3, 0x08},  // - minus
/* 0x2E */ {4, 0x04},  // . period
/* 0x2F */ {7, 0x08},  // / slash
/* 0x30 */ {7, 0x04},  // 0
/* 0x31 */ {0, 0x20},  // 1
/* 0x32 */ {2, 0x40},  // 2 
/* 0x33 */ {0, 0x80},  // 3 
/* 0x34 */ {2, 0x08},  // 4
/* 0x35 */ {0, 0x04},  // 5
/* 0x36 */ {2, 0x02},  // 6
/* 0x37 */ {0, 0x01},  // 7
/* 0x38 */ {7, 0x01},  // 8
/* 0x39 */ {3, 0x02},  // 9
/* 0x3A */ {3, ADD_SHIFT_KEY | 0x04},  // : colon
/* 0x3B */ {3, 0x04},  // ; semi colon
/* 0x3C */ {4, ADD_SHIFT_KEY | 0x02},  // <
/* 0x3D */ {7, 0x80},  // = equal
/* 0x3E */ {4, ADD_SHIFT_KEY | 0x04},  // >
/* 0x3F */ {7, ADD_SHIFT_KEY | 0x08},  // ?
/* 0x40 */ {0, 0x00},  // @ MAJ
/* 0x41 */ {6, 0x20},  // A
/* 0x42 */ {2, 0x04},  // B
/* 0x43 */ {2, 0x80},  // C
/* 0x44 */ {1, 0x80},  // D
/* 0x45 */ {6, 0x08},  // E
/* 0x46 */ {1, 0x08},  // F
/* 0x47 */ {6, 0x04},  // G
/* 0x48 */ {6, 0x02},  // H
/* 0x49 */ {5, 0x02},  // I
/* 0x4A */ {1, 0x01},  // J
/* 0x4B */ {3, 0x01},  // K
/* 0x4C */ {7, 0x02},  // L
/* 0x4D */ {2, 0x01},  // M
/* 0x4E */ {0, 0x02},  // N
/* 0x4F */ {5, 0x04},  // O
/* 0x50 */ {5, 0x08},  // P
/* 0x51 */ {1, 0x40},  // Q
/* 0x52 */ {1, 0x04},  // R
/* 0x53 */ {6, 0x40},  // S
/* 0x54 */ {1, 0x02},  // T
/* 0x55 */ {5, 0x01},  // U
/* 0x56 */ {0, 0x08},  // V
/* 0x57 */ {6, 0x80},  // W
/* 0x58 */ {0, 0x40},  // X
/* 0x59 */ {6, 0x01},  // Y
/* 0x5A */ {2, 0x20},  // Z
/* 0x5B */ {5, 0x80},  // square bracket open
/* 0x5C */ {3, 0x20},  // backslach
/* 0x5D */ {5, 0x40},  // square braquet close
/* 0x5E */ {0, 0x00},  // ^ circonflex
/* 0x5F */ {0, 0x00},  // _ undescore
/* 0x60 */ {0, 0x00},  // `backquote
/* 0x61 */ {6, 0x20},  // a
/* 0x62 */ {2, 0x04},  // b
/* 0x63 */ {2, 0x80},  // c
/* 0x64 */ {1, 0x80},  // d
/* 0x65 */ {6, 0x08},  // e
/* 0x66 */ {1, 0x08},  // f
/* 0x67 */ {6, 0x04},  // g
/* 0x68 */ {6, 0x02},  // h
/* 0x69 */ {5, 0x02},  // i
/* 0x6A */ {1, 0x01},  // j
/* 0x6B */ {3, 0x01},  // k
/* 0x6C */ {7, 0x02},  // l
/* 0x6D */ {2, 0x01},  // m
/* 0x6E */ {0, 0x02},  // n
/* 0x6F */ {5, 0x04},  // o
/* 0x70 */ {5, 0x08},  // p
/* 0x71 */ {1, 0x40},  // q
/* 0x72 */ {1, 0x04},  // r
/* 0x73 */ {6, 0x40},  // s
/* 0x74 */ {1, 0x02},  // t
/* 0x75 */ {5, 0x01},  // u
/* 0x76 */ {0, 0x08},  // v
/* 0x77 */ {6, 0x80},  // w
/* 0x78 */ {0, 0x40},  // x
/* 0x79 */ {6, 0x01},  // y
/* 0x7A */ {2, 0x20},  // z
/* 0x7B */ {5, ADD_SHIFT_KEY | 0x80},  // curly bracket open
/* 0x7C */ {0, 0x00},  // or
/* 0x7D */ {5, ADD_SHIFT_KEY | 0x40},  // curly bracket close  
/* 0x7E */ {0, 0x00},  // tilt
/* 0x7F */ {5, 0x20},   // backspace

/* 0x80 */ {4, 0x40},  // down
/* 0x81 */ {4, 0x08},  // up
/* 0x82 */ {4, 0x20},  // left
/* 0x83 */ {4, 0x80},  // right
/* 0x84 */ {0, 0x00},  // pgdw
/* 0x85 */ {0, 0x00},  // pgup
/* 0x86 */ {0, 0x00},  // home
/* 0x87 */ {0, 0x00}   // end
};


#define KEY_DEBOUNCE_MS 40
#define CND_DEBOUNCE_MS 70
#define BOOT_SEQ_MS 700

static uint8_t prev_key = 0;
static int repeat_cnt = 0;
static bool send_cmdstring = false;
static const char * cmdstring_pt;
static const char runcmd[] = {'C', 'A', 'L', 2,'L',' ','#','E','8','D','6',0x0d, 0}; // RUN 

static void oric_force_kup(uint8_t asciicode) {
  uint8_t row = ascii2OricKeys[asciicode].row;
  uint16_t colmask = ascii2OricKeys[asciicode ].colmask;;
  if (!( (row == 0) && (colmask == 0) )) {
    if (colmask & ADD_SHIFT_KEY) {
        vrEmu6522ReleaseKeyMtx(Vr6522,7, 0x10);
        memory[0x209] = 0x38;
    }
    else if (colmask & ADD_CTRL_KEY) {
        vrEmu6522ReleaseKeyMtx(Vr6522,2, 0x10);
        memory[0x209] = 0x38;  
    }
    vrEmu6522ReleaseKeyMtx(Vr6522,row, colmask & 0xff); 
  }
}

static void oric_kdown(uint8_t asciicode, bool shiftl, bool shiftr ) {
  uint8_t row = ascii2OricKeys[asciicode].row;
  uint16_t colmask = ascii2OricKeys[asciicode].colmask;
  if (!( (row == 0) && (colmask == 0) )) {
    if (asciicode == 0x09) {
      if (kbdasjoy == true) {
        memory[0xBB88] = 32;
        kbdasjoy = false; 
      } 
      else {
        memory[0xBB88] = 48;        
      kbdasjoy = true;
      } 
      if (prev_key) oric_force_kup(prev_key);   
    }
    if (kbdasjoy) {
      switch (asciicode) 
      {
        case 0x20:
          joystick0 &= ~JOY_FIRE;
          //memory[0xBB88] = 52;          
          break;
        case 0x80:
          joystick0 &= ~JOY_DOWN;
          //memory[0xBB88] = 48;
          break;
        case 0x81:
          joystick0 &= ~JOY_UP;
          //memory[0xBB88] = 49;
          break;
        case 0x82:
          joystick0 &= ~JOY_LEFT;
          //memory[0xBB88] = 50;
          break;
        case 0x83:
          joystick0 &= ~JOY_RIGHT;
          //memory[0xBB88] = 51;
          break;
      }
      vrEmu6522Joystick(Vr6522,joystick0);
    }
    else 
    {
      if (colmask & ADD_SHIFT_KEY) {
          vrEmu6522PressKeyMtx(Vr6522,7, 0x10);
          memory[0x209] = 0xA7;  
      }
      else if (colmask & ADD_CTRL_KEY) {
          vrEmu6522PressKeyMtx(Vr6522,2, 0x10);
          memory[0x209] = 0xA2;  
      }
      vrEmu6522PressKeyMtx(Vr6522,row, colmask & 0xff); 
    }
  }
}

static void oric_kup(uint8_t asciicode) {
  uint8_t row = ascii2OricKeys[asciicode].row;
  uint16_t colmask = ascii2OricKeys[asciicode ].colmask;;
  if (!( (row == 0) && (colmask == 0) )) {
    if (kbdasjoy) {
      switch (asciicode) 
      {
        case 0x20:
          joystick0 |= JOY_FIRE;
          //memory[0xBB88] = 32;          
          break;
        case 0x80:
          joystick0 |= JOY_DOWN;
          //memory[0xBB88] = 32;          
          break;
        case 0x81:
          joystick0 |= JOY_UP;
          //memory[0xBB88] = 32;          
          break;
        case 0x82:
          joystick0 |= JOY_LEFT;
          //memory[0xBB88] = 32;          
          break;
        case 0x83:
          joystick0 |= JOY_RIGHT;
          //memory[0xBB88] = 32;          
          break;
      }
      vrEmu6522Joystick(Vr6522,joystick0);
    }
    else 
    {
      if (colmask & ADD_SHIFT_KEY) {
          vrEmu6522ReleaseKeyMtx(Vr6522,7, 0x10);
          memory[0x209] = 0x38;
      }
      else if (colmask & ADD_CTRL_KEY) {
          vrEmu6522ReleaseKeyMtx(Vr6522,2, 0x10);
          memory[0x209] = 0x38;  
      }
      vrEmu6522ReleaseKeyMtx(Vr6522,row, colmask & 0xff); 
    }
  }
}

static bool repeating_timer_callback(struct repeating_timer *t) {
    if (repeat_cnt ) repeat_cnt--;
    if (repeat_cnt == 0) {
      if (prev_key) {
        oric_kup(prev_key);
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
            repeat_cnt = CND_DEBOUNCE_MS; 
          } 
          else
          if (asciikey < 0x88) {
            oric_kdown( asciikey, false, false);
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
static int prev_code = 0;

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
    if (prev_code) oric_kup(prev_code);
    return;
  }

  // LCTRL + LSHIFT + J => keyboard as joystick
  if ( ( (flags & (KBD_FLAG_LSHIFT + KBD_FLAG_LCONTROL)) == (KBD_FLAG_LSHIFT + KBD_FLAG_LCONTROL) ) && (!pressed) && (code == 'j') ) {
    if (kbdasjoy == true) kbdasjoy = false; 
    else kbdasjoy = true;
    if (prev_code) oric_kup(prev_code);
    return;
  }

  //keyboard as joystick?
  if (kbdasjoy == true) {
      if (prev_code) signal_joy(prev_code, 0);
      if (code) {
        signal_joy(code, pressed);
        if (pressed) prev_code = code;
      }  
  }
  else {
    if (!(flags & (KBD_FLAG_RSHIFT + KBD_FLAG_RCONTROL))) {
/*
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
      else if (codeshifted == KBD_KEY_ESC) {code = 0x9B; flags |= 0; }
*/
      // no chars for below characters!!!
      if (codeshifted == '@') {code = 0; flags |= 0; }
      else if (codeshifted == '^') {code = 0; flags |= 0; }
      else if (codeshifted == '_') {code = 0; flags |= 0; }
      else if (codeshifted == KBD_KEY_DOWN) {code = 0x80; flags |= 0; }
      else if (codeshifted == KBD_KEY_RIGHT) {code = 0x83; flags |= 0; }
      else if (codeshifted == KBD_KEY_UP) {code = 0x81; flags |= KBD_FLAG_RSHIFT; }
      else if (codeshifted == KBD_KEY_LEFT) {code = 0x82; flags |= KBD_FLAG_RSHIFT; }
      else if ( (codeshifted >= 'a') && (codeshifted <= 'z') ) { code = toupper(code); }
      else if ( (codeshifted >= 'A') && (codeshifted <= 'Z') ) { code = codeshifted; flags |= KBD_FLAG_RSHIFT; }
      else code = codeshifted;
    }
    else {
      code = toupper(code);
    }  

    if (prev_code) oric_kup(prev_code);

    if (code) {
      if (pressed == KEY_PRESSED)
      {
        oric_kdown(code, flags & KBD_FLAG_RSHIFT, flags & KBD_FLAG_RCONTROL);
        prev_code = code;
        //printf("kdown %c\r\n", kbd_to_ascii (code, flags));
      }
      else 
      {
        oric_kup(code);
        //printf("kup %c\r\n", kbd_to_ascii (code, flags));
      }
    }    
  }  
}

#else

static uint16_t startadd=0;
static uint16_t stopadd=0;

// ****************************************
// USB SERIAL server
// ****************************************
static int serial_rx(uint8_t* buf, int len) {
  uint8_t asciikey;

  if (len >= 1) {
    switch (buf[0]) {
      case sercmd_reset:
        break;
      case sercmd_key:        
        asciikey = toupper((char)buf[1]);
        if (asciikey < 0x88) {
          if (prev_key != 0) {
            oric_kup(prev_key);
            prev_key = 0;            
          }
          oric_kdown( asciikey, false, false);
          prev_key = asciikey;
          repeat_cnt = KEY_DEBOUNCE_MS; 
        }
        break;
      case sercmd_prg:
        if (len>3) {
          if (!startadd) {
            startadd = (buf[1]<<8)+buf[2];
            memory[0x9a] = (startadd & 0xff); // lo
            memory[0x9b] = (startadd >> 8);   // hi
            //memory[0x02a9] = (startadd & 0xff); // lo
            //memory[0x02aa] = (startadd >> 8);   // hi
          }
          int i;
          for (i=0; i < (len-3); i++) {
            memory[((buf[1]<<8)+buf[2])+i] = buf[3+i];
          }
          stopadd = ((buf[1]<<8)+buf[2])+i;
          //memory[0x2ab] = (stopadd & 0xff); // lo
          //memory[0x2ac] = (stopadd >> 8);   // hi
          memory[0x9c] = (stopadd & 0xff); // lo
          memory[0x9d] = (stopadd >> 8);   // hi
          //memory[0x2ad] = 0 ;   // autorun
          //memory[0x2ae] = 0;   // filetype basic
        }
        break;
      case sercmd_run:
        cmdstring_pt = &runcmd[0];
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
static uint8_t __not_in_flash("readCD000") readCD000(uint16_t address) {
  if (memory[REG_HYPGFXENA] != 1) {
    return (memrom[address & 0x3FFF]);
  } 
  else {
    return (memory[address]);
  }
#else
static void __not_in_flash("readCD000") readCD000(uint16_t address) {
  if (memory[REG_HYPGFXENA] != 1) {
    bus_pio->txf[bus_smr] = 0x100 | memrom[address & 0x3FFF];
  } 
  else {
    bus_pio->txf[bus_smr] = 0x100 | memory[address];
  }
#endif  
}

#ifdef CPU_EMU  
static uint8_t __not_in_flash("readNone") readNone(uint16_t address) {
  return (memory[address]);
#else
static void __not_in_flash("readNone") readNone(uint16_t address) {
  bus_pio->txf[bus_smr] = 0;
#endif  
}

#ifdef CPU_EMU  
static uint8_t __not_in_flash("readROM") readROM(uint16_t address) {
  return (memrom[address & 0x3FFF]);
#else
static void __not_in_flash("readROM") readROM(uint16_t address) {
  bus_pio->txf[bus_smr] = 0x100 | memrom[address & 0x3FFF];
#endif  
}

#ifdef CPU_EMU  
static uint8_t __not_in_flash("read0000") read0000(uint16_t address) {
  if ((address & 0xFF00) == 0x0300) {
    //if ( (address == 0310) || (address == 0310) ) return joystick0;
    //return vrEmu6522Read(Vr6522,address);   
    return ioreadFuncTable[address & 0x0F](Vr6522,address & 0x0F);
  }
  else
    return (memory[address]);
  }
#else


static void __not_in_flash("read0000") read0000(uint16_t address) {
#ifdef CPU_6502
  if ((address & 0xFF00) == 0x0300) {
    //bus_pio->txf[bus_smr] = 0x100 | vrEmu6522Read(Vr6522,address); 
    bus_pio->txf[bus_smr] = 0x100 | ioreadFuncTable[address & 0x0F](Vr6522,address & 0x0F);
  }
  else {
    bus_pio->txf[bus_smr] = 0x100 | memory[address];
  }
#else
  bus_pio->txf[bus_smr] = 0;  
#endif  
}
#endif


#ifdef CPU_EMU  
static uint8_t __not_in_flash("readFuncTable") (*readFuncTable[16])(uint16_t)
#else
static void __not_in_flash("readFuncTable") (*readFuncTable[16])(uint16_t)
#endif
{
#if (defined(ORIC_64K))
  readNone, // 0 (first 4k, io)
  readNone, // 1
  readNone, // 2 
  readNone, // 3
  readNone, // 4
  readNone, // 5
  readNone, // 6
  readNone, // 7
  readNone, // 8
  readNone, // 9
  readNone, // a
  readNone, // b
#else
  read0000, // 0
  readMEM,  // 1
  readMEM,  // 2 
  readMEM,  // 3
  readMEM,  // 4
  readMEM,  // 5
  readMEM,  // 6
  readMEM,  // 7
  readMEM,  // 8
  readMEM,  // 9
  readMEM,  // a
  readMEM,  // b
#endif
#if (defined(ORIC_64K))
  readNone, // c
  readNone, // d
  readNone, // e
  readNone  // f
#else
  readCD000,  // c
  readCD000,  // d
  readCD000,  // e
  readROM   // f
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
  memory[address] = value; 
}

static void __not_in_flash("writeD000") writeD000(uint16_t address, uint8_t value) {
  if (address == REG_HYPGFXENA) memory[address] = value;
  else if (memory[REG_HYPGFXENA] != 1) {
    memory[address] = value;  
  }
  else {
//  if (memory[REG_HYPGFXENA] == 1) {
    HyperGfxWrite(address,value);
  }
}


static void __not_in_flash("write0000") write0000(uint16_t address, uint8_t value) {
#if (defined(CPU_EMU) || defined(CPU_6502) )
  if ((address & 0xFF00) == 0x0300) {      // I/O
    iowriteFuncTable[address & 0x0F](Vr6522,address & 0x0F,value);
  }
  else {
    memory[address] = value;      
  }
#else
  memory[address] = value; 
#endif  
}


static void __not_in_flash("writeFuncTable") (*writeFuncTable[16])(uint16_t,uint8_t)
{
#if (defined(ORIC_64K))
  writeMEM, // 0
#else
  write0000, // 0
#endif  
  writeMEM,  // 1
  writeMEM,  // 2 
  writeMEM,  // 3
  writeMEM,  // 4 
  writeMEM,  // 5
  writeMEM,  // 6
  writeMEM,  // 7
  writeMEM,  // 8
  writeMEM,  // 9
  writeMEM,  // a
  writeMEM,  // b
#if (defined(ORIC_64K))  
  writeNone, // c
  writeNone, // d
  writeNone, // e
  writeNone  // f
#else
  writeMEM,  // c
  writeD000, // d
  writeMEM,  // e
  writeMEM   // f
#endif  
};


#ifdef CPU_EMU

uint8_t readWord( uint16_t addr)
{
  return readFuncTable[addr>>12](addr);
}

void writeWord( uint16_t addr, uint8_t value)
{
  writeFuncTable[addr>>12](addr,value);
}

//#include "ym2149.h"

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
    //sm_config_set_in_shift(&cc, false, true, 32);
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
//    uint32_t value = pio_sm_get_blocking(clock_pio, clock_sm);
//    vrEmu6522Ticks(Vr6522, 1 );
//    gpio_put(CONFIG_PET_IRQ, !vrEmu6522Int(Vr6522)); 
    //uint8_t * linebuffer = hdmi_get_line_buffer(2);
    //*linebuffer++=nn;
    //*linebuffer++=nn;
    //*linebuffer++=nn;
    //*linebuffer++=nn;
    if (got_reset)
    {
      got_reset = false;
      HyperGfxReset();
#if (defined(CPU_EMU) || defined(CPU_6502))
      sleep_ms(30);
      prev_key = 0;
#endif
      memset(&memory[0], 0, 0xc000);
#if (defined(CPU_EMU) || defined(CPU_6502))
#ifdef CPU_EMU
      mos.Reset();
#endif
      vrEmu6522Reset( Vr6522);
      if (hyper_enabled) {
        //cmdstring_pt = &fbcmd[0];
        //send_cmdstring = true;
        //repeat_cnt = 0;
      }
#endif
    }

#if (defined(CPU_EMU) || defined(CPU_6502))

    for (int i = 8; i < 408; i = i + 2) {
        hdmi_wait_line(i);
#ifdef CPU_EMU
        mos.Run(ORIC_CYCLES/ORIC_LINES);
#endif
        vrEmu6522Ticks(Vr6522, ORIC_CYCLES/ORIC_LINES);
#ifdef CPU_EMU
        if (vrEmu6522Int(Vr6522)) mos.IRQ(); 
#else
        gpio_put(CONFIG_PET_IRQ, !vrEmu6522Int(Vr6522));        
#endif  
    }
#ifdef CPU_EMU
    mos.Run((ORIC_CYCLES/ORIC_LINES)*(ORIC_LINES-200));
#endif
    vrEmu6522Ticks(Vr6522, (ORIC_CYCLES/ORIC_LINES)*(ORIC_LINES-200) );
#ifdef CPU_EMU
    if (vrEmu6522Int(Vr6522)) mos.IRQ(); 
#else
    gpio_put(CONFIG_PET_IRQ, !vrEmu6522Int(Vr6522)); 
#endif 

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
    //cmdstring_pt = &fbcmd[0];
    //send_cmdstring = true;
    //repeat_cnt = 0;
  }
#endif

  HyperGfxFlashFSInit();  
  HyperGfxInit();

  memset(&memory[0], 0, 0xc000);

#if (defined(CPU_EMU) || defined(CPU_6502))
  memcpy((void *)&memrom[0], (void *)&basic11bData[0], sizeof(basic11bData));
//  memcpy((void *)&memrom[0], (void *)&basic11bPData[0], sizeof(basic11bPData));


  //memcpy((void *)&memory[0xe000], (void *)&basic11bData[0x2000], 0x2000);


  //e85c patch cload"
  memrom[0x285c] = 0x28; // PLP
  memrom[0x285d] = 0x78; // SEI
  memrom[0x285e] = 0xa9; // LDA #1
  memrom[0x285f] = 0x01; // STA REG_HYPGFXENA
  memrom[0x2860] = 0x8d;
  memrom[0x2861] = 0x16;
  memrom[0x2862] = 0xdb; // switch to ram here
  memrom[0x2863] = 0xea; // NOP
  
  memrom[0x2864] = 0;    // JMP start
  memrom[0x2865] = 0;
  memrom[0x2866] = 0;
  memrom[0x2867] = 0xa9; // LDA #0
  memrom[0x2868] = 0x00; // STA REG_HYPGFXENA
  memrom[0x2869] = 0x8d;
  memrom[0x286a] = 0x16;
  memrom[0x286b] = 0xdb; // switch back here
  memrom[0x286c] = 0xea; // NOP return CLOAD" if exit file browser
  memrom[0x286d] = 0x58; // CLI
  memrom[0x286e] = 0x60; // RTS

  memrom[0x286f] = 0xa9; // LDA #0
  memrom[0x2870] = 0x00; // STA REG_HYPGFXENA
  memrom[0x2871] = 0x8d;
  memrom[0x2872] = 0x16;
  memrom[0x2873] = 0xdb; // switch back here

  memrom[0x2874] = 0xea; // NOP return CLOAD" if run program
  memrom[0x2875] = 0x58; // CLI
  memrom[0x2876] = 0x4c; // JMP $e8d6
  memrom[0x2877] = 0xd6;
  memrom[0x2878] = 0xe8;
 
  memcpy((void *)&memory[0xe85c], (void *)&fbrom[0], sizeof(fbrom));

#ifdef CPU_EMU
  mos.Reset();
#endif
  Vr6522 = vrEmu6522New(VIA_65C22);
  vrEmu6522Reset( Vr6522);
#endif
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