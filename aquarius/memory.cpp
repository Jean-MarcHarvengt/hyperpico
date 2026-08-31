#include "memory.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "z80.h"
#include "pico.h"
#include "hypergfx.h"

#include "aquarius.h"


#define ADD_SHIFT_KEY 0x100
#define REMOVE_SHIFT_KEY 0x200
#define ADD_CTRL_KEY 0x400
#define REMOVE_CTRL_KEY 0x800

typedef struct {
  uint8_t offset; // 8bits = columns
  uint16_t mask;  // 6bits = rows
} AQUAKey;

/*
COLSCAN:
 1 |  = BS  : RTN ;  .  bit0  254  0xFE
 2 |  -  /  0  p  l  ,  bit1  253  0xFD
 3 |  9  o  k  m  n  j  bit2  251  0xFB
 4 |  8  i  7  u  h  b  bit3  247  0xF7
 5 |  6  y  g  v  c  f  bit4  239  0xEF
 6 |  5  t  4  r  d  x  bit5  223  0xDF
 7 |  3  e  s  z SP  a  bit6  191  0xBF
 8 |  2  w  1  q SH CTL bit7  127  0x7F

ROWSCAN:
 9 | SH SP  d  c  h  n  l  ;  bit4  239  0xEF
10 |  q  z  r  v  u  m  p RTN bit3  247  0xF7
11 |  w  e  t  y  i  o  / BS  bit1  253  0xFD
12 |  2  3  5  6  8  9  -  =  bit0  254  0xFE
13 | CTL a  x  f  b  j  ,  .  bit5  223  0xDF
14 |  1  s  4  g  7  k  0  :  bit2  251  0xFB
*/

static const AQUAKey ascii2Keys[] = { // Ascii to TRS keys
/* 0x00 */ {0, 0},
/* 0x01 */ {0, 0},
/* 0x02 */ {0, 0},
/* 0x03 */ {0, 0},
/* 0x04 */ {0, 0},
/* 0x05 */ {0, 0},
/* 0x06 */ {0, 0},
/* 0x07 */ {0, 0},
/* 0x08 */ {1, 2}, // backspace
/* 0x09 */ {8, ADD_CTRL_KEY | 4},	// tab
/* 0x0A */ {0, 0},
/* 0x0B */ {0, 0},
/* 0x0C */ {0, 0},
/* 0x0D */ {1, 8},	// enter
/* 0x0E */ {0, 0},
/* 0x0F */ {0, 0},
/* 0x10 */ {0, 0},
/* 0x11 */ {0, 0},
/* 0x12 */ {0, 0},
/* 0x13 */ {0, 0},
/* 0x14 */ {0, 0},
/* 0x15 */ {0, 0},
/* 0x16 */ {0, 0},
/* 0x17 */ {0, 0},
/* 0x18 */ {0, 0},
/* 0x19 */ {0, 0},
/* 0x1A */ {0, 0},
/* 0x1B */ {1, 2},	// esc
/* 0x1C */ {0, 0},
/* 0x1D */ {0, 0},
/* 0x1E */ {0, 0},
/* 0x1F */ {0, 0},
/* 0x20 */ {7, 16},	// space
/* 0x21 */ {8, ADD_SHIFT_KEY | 4},	// ! exclamation mark
/* 0x22 */ {8, ADD_SHIFT_KEY | 1},	// " double quote   
/* 0x23 */ {7, ADD_SHIFT_KEY | 1}, 	// # dies
/* 0x24 */ {6, ADD_SHIFT_KEY | 4},  // $ dollar
/* 0x25 */ {6, ADD_SHIFT_KEY | 1},  // % percent
/* 0x26 */ {5, ADD_SHIFT_KEY | 1},  // & ampersand
/* 0x27 */ {4, ADD_SHIFT_KEY | 4},  // ' singlequote
/* 0x28 */ {4, ADD_SHIFT_KEY | 1},  // ( bracket left
/* 0x29 */ {3, ADD_SHIFT_KEY | 1},  // ) bracket right
/* 0x2A */ {1, ADD_SHIFT_KEY | 4},  // * mult
/* 0x2B */ {1, ADD_SHIFT_KEY | 1},  // + plus
/* 0x2C */ {2, 32}, // , comma
/* 0x2D */ {2, 1},  // - minus
/* 0x2E */ {1, 32}, // . period
/* 0x2F */ {2, 2},  // / slash
/* 0x30 */ {2, 4},  // 0
/* 0x31 */ {8, 4},  // 1
/* 0x32 */ {8, 1},  // 2 
/* 0x33 */ {7, 1},  // 3 
/* 0x34 */ {6, 4},  // 4
/* 0x35 */ {6, 1},  // 5
/* 0x36 */ {5, 1},  // 6
/* 0x37 */ {4, 4},  // 7
/* 0x38 */ {4, 1},  // 8
/* 0x39 */ {3, 1},  // 9
/* 0x3A */ {1, 4},  // : colon
/* 0x3B */ {1, 16}, // ; semi colon
/* 0x3C */ {2, ADD_SHIFT_KEY | 32},  // <
/* 0x3D */ {1, 1},  // = equal
/* 0x3E */ {1, ADD_SHIFT_KEY | 32},  // >
/* 0x3F */ {2, ADD_SHIFT_KEY | 4},  // ?
/* 0x40 */ {1, ADD_SHIFT_KEY | 16},  // @
/* 0x41 */ {7, ADD_SHIFT_KEY | 32},   // A
/* 0x42 */ {4, ADD_SHIFT_KEY | 32},   // B
/* 0x43 */ {5, ADD_SHIFT_KEY | 16},   // C
/* 0x44 */ {6, ADD_SHIFT_KEY | 16},   // D
/* 0x45 */ {7, ADD_SHIFT_KEY | 2},    // E
/* 0x46 */ {5, ADD_SHIFT_KEY | 32},   // F
/* 0x47 */ {5, ADD_SHIFT_KEY | 4},    // G
/* 0x48 */ {4, ADD_SHIFT_KEY | 16},   // H
/* 0x49 */ {4, ADD_SHIFT_KEY | 2},    // I
/* 0x4A */ {3, ADD_SHIFT_KEY | 32},   // J
/* 0x4B */ {3, ADD_SHIFT_KEY | 4},    // K
/* 0x4C */ {2, ADD_SHIFT_KEY | 16},   // L
/* 0x4D */ {3, ADD_SHIFT_KEY | 8},    // M
/* 0x4E */ {3, ADD_SHIFT_KEY | 16},   // N
/* 0x4F */ {3, ADD_SHIFT_KEY | 2},    // O
/* 0x50 */ {2, ADD_SHIFT_KEY | 8},    // P
/* 0x51 */ {8, ADD_SHIFT_KEY | 8},    // Q
/* 0x52 */ {6, ADD_SHIFT_KEY | 8},    // R
/* 0x53 */ {7, ADD_SHIFT_KEY | 4},    // S
/* 0x54 */ {6, ADD_SHIFT_KEY | 2},    // T
/* 0x55 */ {4, ADD_SHIFT_KEY | 8},    // U
/* 0x56 */ {5, ADD_SHIFT_KEY | 8},    // V
/* 0x57 */ {8, ADD_SHIFT_KEY | 2},    // W
/* 0x58 */ {6, ADD_SHIFT_KEY | 32},   // X
/* 0x59 */ {5, ADD_SHIFT_KEY | 2},    // Y
/* 0x5A */ {7, ADD_SHIFT_KEY | 8},    // Z
/* 0x5B */ {0, 0},  // square bracket open
/* 0x5C */ {0, 0},  // backslach
/* 0x5D */ {0, 0},  // square braquet close
/* 0x5E */ {0, 0},  // ^ circonflex
/* 0x5F */ {0, 0},  // _ undescore
/* 0x60 */ {1, 2},  // `backquote
/* 0x61 */ {7, 32}, // a
/* 0x62 */ {4, 32}, // b
/* 0x63 */ {5, 16}, // c
/* 0x64 */ {6, 16}, // d
/* 0x65 */ {7, 2},  // e
/* 0x66 */ {5, 32}, // f
/* 0x67 */ {5, 4},  // g
/* 0x68 */ {4, 16}, // h
/* 0x69 */ {4, 2},  // i
/* 0x6A */ {3, 32}, // j
/* 0x6B */ {3, 4},  // k
/* 0x6C */ {2, 16}, // l
/* 0x6D */ {3, 8},  // m
/* 0x6E */ {3, 16}, // n
/* 0x6F */ {3, 2},  // o
/* 0x70 */ {2, 8},  // p
/* 0x71 */ {8, 8},  // q
/* 0x72 */ {6, 8},  // r
/* 0x73 */ {7, 4},  // s
/* 0x74 */ {6, 2},  // t
/* 0x75 */ {4, 8},  // u
/* 0x76 */ {5, 8},  // v
/* 0x77 */ {8, 2},  // w
/* 0x78 */ {6, 32}, // x
/* 0x79 */ {5, 2},  // y
/* 0x7A */ {7, 8},  // z
/* 0x7B */ {0, 0},  // curly bracket open
/* 0x7C */ {0, 0},  // or
/* 0x7D */ {0, 0},  // curly bracket close  
/* 0x7E */ {0, 0},  // tilt
/* 0x7F */ {1, 2}   // backspace
};


static const AQUAKey Keys[] = {
  {0, 0}, // VK_NONE
  {7, 128}, // VK_SPACE
  {5, 1}, //  VK_0
  {5, 2}, //  VK_1
  {5, 4}, //  VK_2
  {5, 8}, //  VK_3
  {5, 16}, //  VK_4
  {5, 32}, //  VK_5
  {5, 64}, //  VK_6
  {5, 128}, //  VK_7
  {6, 1}, //  VK_8
  {6, 2}, //  VK_9
  {5, 1}, //  VK_KP_0
  {5, 2}, //  VK_KP_1
  {5, 4}, //  VK_KP_2
  {5, 8}, //  VK_KP_3
  {5, 16}, //  VK_KP_4
  {5, 32}, //  VK_KP_5
  {5, 64}, //  VK_KP_6
  {5, 128}, //  VK_KP_7
  {6, 1}, //  VK_KP_8
  {6, 2}, //  VK_KP_9
  {1, 2}, //  VK_a
  {1, 4}, //  VK_b
  {1, 8}, //  VK_c
  {1, 16}, //  VK_d
  {1, 32}, //  VK_e
  {1, 64}, //  VK_f
  {1, 128}, //  VK_g
  {2, 1}, //  VK_h
  {2, 2}, //  VK_i
  {2, 4}, //  VK_j
  {2, 8}, //  VK_k
  {2, 16}, //  VK_l
  {2, 32}, //  VK_m
  {2, 64}, //  VK_n
  {2, 128}, //  VK_o
  {3, 1}, //  VK_p
  {3, 2}, //  VK_q
  {3, 4}, //  VK_r
  {3, 8}, //  VK_s
  {3, 16}, //  VK_t
  {3, 32}, //  VK_u
  {3, 64}, //  VK_v
  {3, 128}, //  VK_w
  {4, 1}, //  VK_x
  {4, 2}, //  VK_y
  {4, 4}, //  VK_z
  {1, 2}, //  VK_A
  {1, 4}, //  VK_B
  {1, 8}, //  VK_C
  {1, 16}, //  VK_D
  {1, 32}, //  VK_E
  {1, 64}, //  VK_F
  {1, 128}, //  VK_G
  {2, 1}, //  VK_H
  {2, 2}, //  VK_I
  {2, 4}, //  VK_J
  {2, 8}, //  VK_K
  {2, 16}, //  VK_L
  {2, 32}, //  VK_M
  {2, 64}, //  VK_N
  {2, 128}, //  VK_O
  {3, 1}, //  VK_P
  {3, 2}, //  VK_Q
  {3, 4}, //  VK_R
  {3, 8}, //  VK_S
  {3, 16}, //  VK_T
  {3, 32}, //  VK_U
  {3, 64}, //  VK_V
  {3, 128}, //  VK_W
  {4, 1}, //  VK_X
  {4, 2}, //  VK_Y
  {4, 4}, //  VK_Z
  {0, 0}, //  VK_GRAVEACCENT
  {0, 0}, //  VK_ACUTEACCENT
  {5, ADD_SHIFT_KEY | 128}, //  VK_QUOTE
  {5, 4}, //  VK_QUOTEDBL
  {6, ADD_SHIFT_KEY | 32}, //  VK_EQUALS
  {6, 32}, //  VK_MINUS
  {6, 32}, //  VK_KP_MINUS
  {6, 8}, //  VK_PLUS
  {6, 8}, //  VK_KP_PLUS
  {6, 4}, //  VK_KP_MULTIPLY
  {6, 4}, //  VK_ASTERISK
  {7, 2}, //  VK_BACKSLASH
  {6, 128}, //  VK_KP_DIVIDE
  {6, 128}, //  VK_SLASH
  {6, 64}, //  VK_KP_PERIOD
  {6, 64}, //  VK_PERIOD
  {6, REMOVE_SHIFT_KEY | 4}, //  VK_COLON
  {6, 16}, //  VK_COMMA
  {6, 8}, //  VK_SEMICOLON
  {5, 64}, //  VK_AMPERSAND
  {0, 0}, //  VK_VERTICALBAR
  {5, 8}, //  VK_HASH
  {1, REMOVE_SHIFT_KEY | 1}, //  VK_AT
  {0, 0}, //  VK_CARET
  {5, 16}, //  VK_DOLLAR
  {5, 8}, //  VK_POUND
  {0, 0}, //  VK_EURO
  {5, 32}, //  VK_PERCENT
  {5, 2}, //  VK_EXCLAIM
  {6, 128}, //  VK_QUESTION
  {0, 0}, //  VK_LEFTBRACE
  {0, 0}, //  VK_RIGHTBRACE
  {0, 0}, //  VK_LEFTBRACKET
  {0, 0}, //  VK_RIGHTBRACKET
  {6, 1}, //  VK_LEFTPAREN
  {6, 2}, //  VK_RIGHTPAREN
  {6, 16}, //  VK_LESS
  {6, 64}, //  VK_GREATER
  {0, 0}, //  VK_UNDERSCORE
  {0, 0}, //  VK_DEGREE
  {0, 0}, //  VK_SECTION
  {0, 0}, //  VK_TILDE
  {0, 0}, //  VK_NEGATION
  {8, 1}, //  VK_LSHIFT
  {8, 2}, //  VK_RSHIFT
  {0, 0}, //  VK_LALT
  {0, 0}, //  VK_RALT
  {8, 4}, //  VK_LCTRL
  {8, 4}, //  VK_RCTRL
  {0, 0}, //  VK_LGUI
  {0, 0}, //  VK_RGUI
  {7, 4}, //  VK_ESCAPE
  {0, 0}, //  VK_PRINTSCREEN
  {0, 0}, //  VK_SYSREQ
  {0, 0}, //  VK_INSERT
  {0, 0}, //  VK_KP_INSERT
  {0, 0}, //  VK_DELETE
  {0, 0}, //  VK_KP_DELETE
  {7, 32}, //  VK_BACKSPACE
  {7, 2}, //  VK_HOME
  {0, 0}, //  VK_KP_HOME
  {0, 0}, //  VK_END
  {0, 0}, //  VK_KP_END
  {0, 0}, //  VK_PAUSE
  {7, 4}, //  VK_BREAK
  {0, 0}, //  VK_SCROLLLOCK
  {0, 0}, //  VK_NUMLOCK
  {8, 8}, //  VK_CAPSLOCK
  {0, 0}, //  VK_TAB
  {7, 1}, //  VK_RETURN
  {7, 1}, //  VK_KP_ENTER
  {0, 0}, //  VK_APPLICATION
  {0, 0}, //  VK_PAGEUP
  {0, 0}, //  VK_KP_PAGEUP
  {0, 0}, //  VK_PAGEDOWN
  {0, 0}, //  VK_KP_PAGEDOWN
  {7, 8}, //  VK_UP
  {7, 8}, //  VK_KP_UP
  {7, 16}, //  VK_DOWN
  {7, 16}, //  VK_KP_DOWN
  {7, 32}, //  VK_LEFT
  {7, 32}, //  VK_KP_LEFT
  {7, 64}, //  VK_RIGHT
  {7, 64}, //  VK_KP_RIGHT
  {0, 0}, //  VK_KP_CENTER
  {8, 16}, //  VK_F1
  {8, 32}, //  VK_F2
  {8, 64}, //  VK_F3
  {5, ADD_SHIFT_KEY | 1}, //  VK_F4
  {0, 0}, //  VK_F5
  {0, 0}, //  VK_F6
  {0, 0}, //  VK_F7
  {0, 0}, //  VK_F8
  {0, 0}, //  VK_F9
  {0, 0}, //  VK_F10
  {0, 0}, //  VK_F11
  {0, 0}, //  VK_F12
  {0, 0}, //  VK_GRAVE_a
  {0, 0}, //  VK_GRAVE_e
  {0, 0}, //  VK_ACUTE_e
  {0, 0}, //  VK_GRAVE_i
  {0, 0}, //  VK_GRAVE_o
  {0, 0}, //  VK_GRAVE_u
  {0, 0}, //  VK_CEDILLA_c
  {0, 0}, //  VK_ESZETT
  {0, 0}, //  VK_UMLAUT_u
  {0, 0}, //  VK_UMLAUT_o
  {0, 0}, //  VK_UMLAUT_a
  {0, 0}, //  VK_CEDILLA_C
  {0, 0}, //  VK_TILDE_n
  {0, 0}, //  VK_TILDE_N
  {0, 0}, //  VK_UPPER_a
  {0, 0}, //  VK_ACUTE_a
  {0, 0}, //  VK_ACUTE_i
  {0, 0}, //  VK_ACUTE_o
  {0, 0}, //  VK_ACUTE_u
  {0, 0}, //  VK_UMLAUT_i
  {0, 0}, //  VK_EXCLAIM_INV
  {0, 0}, //  VK_QUESTION_INV
  {0, 0}, //  VK_ACUTE_A
  {0, 0}, //  VK_ACUTE_E
  {0, 0}, //  VK_ACUTE_I
  {0, 0}, //  VK_ACUTE_O
  {0, 0}, //  VK_ACUTE_U
  {0, 0}, //  VK_GRAVE_A
  {0, 0}, //  VK_GRAVE_E
  {0, 0}, //  VK_GRAVE_I
  {0, 0}, //  VK_GRAVE_O
  {0, 0}, //  VK_GRAVE_U
  {0, 0}, //  VK_INTERPUNCT
  {0, 0}, //  VK_DIAERESIS
  {0, 0}, //  VK_UMLAUT_e
  {0, 0}, //  VK_UMLAUT_A
  {0, 0}, //  VK_UMLAUT_E
  {0, 0}, //  VK_UMLAUT_I
  {0, 0}, //  VK_UMLAUT_O
  {0, 0}, //  VK_UMLAUT_U
  {0, 0}, //  VK_CARET_a
  {0, 0}, //  VK_CARET_e
  {0, 0}, //  VK_CARET_i
  {0, 0}, //  VK_CARET_o
  {0, 0}, //  VK_CARET_u
  {0, 0}, //  VK_CARET_A
  {0, 0}, //  VK_CARET_E
  {0, 0}, //  VK_CARET_I
  {0, 0}, //  VK_CARET_O
  {0, 0}, //  VK_CARET_U
  {0, 0}, //  VK_ASCII
  {0, 0}  //  VK_LAST
};

uint8_t keyb_buffer[8] = {0};
uint8_t anykey = 0;

void __not_in_flash("aqua_kb_reset") aqua_kb_reset(void) {
   keyb_buffer[0] = 0;
   keyb_buffer[1] = 0;
   keyb_buffer[2] = 0;
   keyb_buffer[3] = 0;
   keyb_buffer[4] = 0;
   keyb_buffer[5] = 0;
   keyb_buffer[6] = 0;
   keyb_buffer[7] = 0;   
}


uint8_t __not_in_flash("aqua_kb_mem_read") aqua_kb_mem_read(int address) {
  address = 0xff - address;
  if ( (address == 0xff) &&  (anykey) ) return 0x00;
  if (address & 0x80) return (~keyb_buffer[7]);
  if (address & 0x40) return (~keyb_buffer[6]);
  if (address & 0x20) return (~keyb_buffer[5]);
  if (address & 0x10) return (~keyb_buffer[4]);
  if (address & 0x08) return (~keyb_buffer[3]);
  if (address & 0x04) return (~keyb_buffer[2]);
  if (address & 0x02) return (~keyb_buffer[1]);
  if (address & 0x01) return (~keyb_buffer[0]);
  return 0xff;
}


int aqua_process_vkkey(int vk, bool down)
{
  static bool shiftPressed = false;
  
  int offset = Keys[vk].offset;

  if (offset != 0) {
    bool addShiftKey = Keys[vk].mask & ADD_SHIFT_KEY;
    bool removeShiftKey = Keys[vk].mask & REMOVE_SHIFT_KEY;

    uint8_t mask = Keys[vk].mask & 0xff;
    if (down) {
      keyb_buffer[offset - 1] |= mask;
      if (addShiftKey) {
        keyb_buffer[7] |= 1;
      }
      if (removeShiftKey) {
        keyb_buffer[7] &= ~1;
      }
      return vk;
    } 
    else {
      keyb_buffer[offset - 1] &= ~mask;
      if (addShiftKey && !shiftPressed) {
        keyb_buffer[7] &= ~1;
      }
      if (removeShiftKey && shiftPressed) {
        keyb_buffer[7] |= 1;
      }
      return 0;      
    }
  }
  else {
    return 0;
  }   
}


int aqua_process_asciikey(int vk, bool down)
{
  int retval = 0;
  static bool shiftPressed = false;
  static bool ctrlPressed = false;
  
  int offset = ascii2Keys[vk].offset;

  if (offset != 0) {
    bool addShiftKey = ascii2Keys[vk].mask & ADD_SHIFT_KEY;
    bool removeShiftKey = ascii2Keys[vk].mask & REMOVE_SHIFT_KEY;
    bool addCtrlKey = ascii2Keys[vk].mask & ADD_CTRL_KEY;
    bool removeCtrlKey = ascii2Keys[vk].mask & REMOVE_CTRL_KEY;

    uint8_t mask = ascii2Keys[vk].mask & 0xff;
    if (down) {
      keyb_buffer[offset - 1] |= mask;
      if (addShiftKey) {
        keyb_buffer[7] |= 16;
      }
      if (removeShiftKey) {
        keyb_buffer[7] &= ~16;
      }
      /*
      if (addCtrlKey) {
        keyb_buffer[7] |= 32;
      }
      if (removeCtrlKey) {
        keyb_buffer[7] &= ~32;
      }
      */
      retval = vk;
    } 
    else {
      keyb_buffer[offset - 1] &= ~mask;
      if (addShiftKey && !shiftPressed) {
        keyb_buffer[7] &= ~16;
      }
      if (removeShiftKey && shiftPressed) {
        keyb_buffer[7] |= 16;
      }
      /*
      if (addCtrlKey && !ctrlPressed) {
        keyb_buffer[7] &= ~32;
      }
      if (removeCtrlKey && ctrlPressed) {
        keyb_buffer[7] |= 32;
      }
      */
    }
  }
  anykey = keyb_buffer[0] | keyb_buffer[1] | keyb_buffer[2] | keyb_buffer[3] | keyb_buffer[4] | keyb_buffer[5] | keyb_buffer[6] | keyb_buffer[7];
  return retval;
}


static Z80Context z80ctx;

// ****************************************
// AQUA Memory
// ****************************************
typedef uint8_t (*ReadFunc)(uint16_t);
extern ReadFunc __not_in_flash("readFuncTable") * readFuncTable;
typedef void (*WriteFunc)(uint16_t,uint8_t);
extern WriteFunc __not_in_flash("writeFuncTable") * writeFuncTable;

//------------------------------------------------------------------

static tstate_t total_tstate_count = 0;

static byte z80_mem_read(int param, ushort address)
{
  //return readFuncTable[address>>11](address);
  return mem_read(address);
}

void z80_mem_write(int param, ushort address, byte data)
{
  //writeFuncTable[address>>11](address,data & 0xff);
  mem_write(address, data);
}

static byte z80_io_read(int param, ushort address)
{
  byte retval=0x00;
  switch (address & 0xff) 
  {
    case 0xfc:
        retval = 0xff;
    case 0xfd:
        retval = (HyperGfxIsVsync()?0xfe:0xff); 
    case 0xfe:
        retval = 0xff;
    case 0xff:
      retval = aqua_kb_mem_read(address >> 8); 
      break;
	default:
		break;
  }
  return retval;
}

static void z80_io_write(int param, ushort address, byte data)
{
  //z80_out(address & 0xff, data, total_tstate_count);
}



static void z80_reset(ushort entryAddr)
{
  //mem_init();
  memset(&z80ctx, 0, sizeof(Z80Context));
  Z80RESET(&z80ctx);
  z80ctx.PC = entryAddr;
  z80ctx.memRead = z80_mem_read;
  z80ctx.memWrite = z80_mem_write;
  z80ctx.ioRead = z80_io_read;
  z80ctx.ioWrite = z80_io_write;
}



static bool pauzed = false;
static bool reset = false;
static unsigned short resetAddr;

void aqua_init(void)
{
  z80_reset(0);
}

void aqua_cycles(unsigned int tstates)
{
  if (!pauzed) {
    if (reset) {
        z80_reset(resetAddr);
        reset = false;
    }
    Z80ExecuteTStates(&z80ctx, tstates);
  }  
}

void aqua_play(unsigned short entryAddr)
{
  resetAddr = entryAddr;
  reset = true;
  pauzed = false;
}

void aqua_pauze(void)
{
  pauzed = true;
}


void mem_init()
{
  	memcpy((void*)memory, (void*)aquarius, sizeof(aquarius));
  
}

int mem_read(unsigned int address)
{
    address &= 0xffff; /* allow callers to be sloppy */

  	if (address >= RAM_START) {
  		return memory[address];
  	}
  	else if (address >= COLOR_START) {
         	return memory[address];
  	} 
  	else if (address >= VIDEO_START) {
         	return memory[address];
  	} 
  	else if (address < sizeof(aquarius)) {
  		return memory[address];
  	}
    return 0xff;
}


void mem_write(unsigned int address, int value)
{
  address &= 0xffff;
  	
	if (address >= RAM_START) {	
    	memory[address] = value;
	} 
	else if (address >= COLOR_START) {
       	memory[address] = value;
	} 
	else if (address >= VIDEO_START) {
       	memory[address] = value;
	} 
}

/*
 * Words are stored with the low-order byte in the lower address.
 */
int mem_read_word(int address)
{
    int rval;

    rval = mem_read(address++);
    rval |= mem_read(address & 0xffff) << 8;
    return rval;
}

void mem_write_word(int address, int value)
{
    mem_write(address++, value & 0xff);
    mem_write(address, value >> 8);
}


