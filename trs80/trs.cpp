
#include "z80.h"
#include "trs_screen.h"
#include "trs_memory.h"
#include "trs.h"
#include "io.h"
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>

#include "hypergfx.h"

extern void wait_ms(int ms);

#define CYCLES_PER_TIMER_M3 ((unsigned int) (CLOCK_MHZ_M3 * 1000000 / TIMER_HZ_M3))
#define CYCLES_PER_TIMER_M4 ((unsigned int) (CLOCK_MHZ_M4 * 1000000 / TIMER_HZ_M4))

static unsigned int cycles_per_timer = CYCLES_PER_TIMER_M3;
static unsigned int timer_hz = TIMER_HZ_M3;

static Z80Context z80ctx;

// ****************************************
// TRS Memory
// ****************************************
extern void (*writeFuncTable[32])(uint32_t,uint8_t);
extern uint8_t (*readFuncTable[32])(uint32_t);

//------------------------------------------------------------------

static tstate_t total_tstate_count = 0;

static byte z80_mem_read(int param, ushort address)
{
  return readFuncTable[address>>11](address);
  //return mem_read(address);
}

void z80_mem_write(int param, ushort address, byte data)
{
  writeFuncTable[address>>11](address,data & 0xff);
  //mem_write(address, data);
}

static byte z80_io_read(int param, ushort address)
{
  //return 0xff;
  return z80_in(address & 0xff, total_tstate_count);
}

static void z80_io_write(int param, ushort address, byte data)
{
  z80_out(address & 0xff, data, total_tstate_count);
}

static int get_ticks()
{
  static struct timeval start_tv, now;
  static int init = 0;

  if (!init) {
    gettimeofday(&start_tv, NULL);
    init = 1;
  }

  gettimeofday(&now, NULL);
  return (now.tv_sec - start_tv.tv_sec) * 1000 +
                 (now.tv_usec - start_tv.tv_usec) / 1000;
}

static void sync_time_with_host()
{
  unsigned int curtime;
  unsigned int deltatime;
  static unsigned int lasttime = 0;
  static int count = 0;

  deltatime = 1000 / timer_hz;

  curtime = get_ticks();

  if (lasttime + deltatime > curtime) {
    wait_ms((lasttime + deltatime - curtime) /* / get_ticks_ms()*/);
    //if ((count++ % 100) == 0) printf("DELAY: %d\n", (lasttime + deltatime - curtime) / portTICK_PERIOD_MS);
  }
  curtime = get_ticks();

  lasttime += deltatime;
  if ((lasttime + deltatime) < curtime) {
    lasttime = curtime;
  }
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

static void z80_reset()
{
  Z80RESET(&z80ctx);
  //mem_init();
  trs_screen_setMode(MODE_TEXT_64x16);
  trs_screen_setInverse(false);
}

static void z80_run()
{
  unsigned last_tstate_count = z80ctx.tstates;
  Z80Execute(&z80ctx);
  total_tstate_count += z80ctx.tstates - last_tstate_count;
  if (z80ctx.tstates >= cycles_per_timer) {
    sync_time_with_host();
    z80ctx.tstates -=  cycles_per_timer;
    z80ctx.int_req = 1;
  }
}

void trs_timer_speed(int fast)
{
#ifndef TRS_MODEL4 
  fast = 0;
#endif  
  timer_hz = fast ? TIMER_HZ_M4 : TIMER_HZ_M3;
  cycles_per_timer = fast ? CYCLES_PER_TIMER_M4 : CYCLES_PER_TIMER_M3;
}

static bool pauzed = false;
static bool reset = false;
static unsigned short resetAddr;

void trs_init(void)
{
  z80_reset(0);
}

void trs_step(void)
{
  if (!pauzed) {
    if (reset) {
        z80_reset(resetAddr);
        //memset(&z80ctx, 0, sizeof(Z80Context));
        //Z80RESET(&z80ctx);
        //z80ctx.PC = resetAddr;
        reset = false;
    }
    z80_run();
  }  
}

void trs_go(void)
{
  trs_init();
  while(true) {
    trs_step();
  }
}

void trs_pauze(void)
{
  pauzed = true;
}

void trs_play(unsigned short entryAddr)
{
  resetAddr = entryAddr;
  reset = true;
  pauzed = false;
}


static uint8_t currentMonitorMode = 0;
static uint8_t width;
static uint8_t height;
static ushort  screen_chars;
static uint8_t char_width;
static uint8_t char_height;


void trs_screen_setMode(uint8_t mode)
{
  bool hires = false;
  bool changeResolution = false;
  uint8_t changes = mode ^ currentMonitorMode;

  if (mode & MODE_TEXT_64x16) {
    width = 64;
    height = 16;
    screen_chars = 64 * 16;
    char_width = TRS_M3_CHAR_WIDTH;
    char_height = TRS_M3_CHAR_HEIGHT;  
    if (changes & MODE_TEXT_64x16) {
      changeResolution = true;
    }
  }

  if (mode & MODE_TEXT_80x24) {
    width = 80;
    height = 24;
    screen_chars = 80 * 24;
    char_width = TRS_M4_CHAR_WIDTH;
    char_height = TRS_M4_CHAR_HEIGHT;
    //trs_font = (byte*) font_m4;
    if (changes & MODE_TEXT_80x24) {
      changeResolution = true;
      hires = true;
    }
  }

  if (changes & MODE_GRAFYX) {
    if ((mode & MODE_GRAFYX) && (currentMonitorMode & MODE_TEXT_64x16)) {
      // Enable Grafyx but the current resolution is 64x16
      hires = true;
      changeResolution = true;
    }
    if (!(mode & MODE_GRAFYX) && (currentMonitorMode & MODE_TEXT_64x16)) {
      // Disable Grafyx and switch back to low res
      hires = false;
      changeResolution = true;
    }
  }

  if (changeResolution) {
    // clear
  }
  currentMonitorMode &= ~(MODE_TEXT_64x16 | MODE_TEXT_80x24);
  currentMonitorMode |= mode;
}

void trs_screen_setExpanded(int flag)
{
  int bit = flag ? MODE_EXPANDED : 0;
  if ((currentMonitorMode ^ bit) & MODE_EXPANDED) {
    currentMonitorMode ^= MODE_EXPANDED;
    //refresh();
  }
}


void trs_screen_setInverse(int flag)
{
  if (flag) {
    currentMonitorMode |= MODE_INVERSE;
    font_reversed = true;
  } else {
    currentMonitorMode &= ~MODE_INVERSE;
    font_reversed = false;
  }
}


void grafyx_write_x(int value) {}
void grafyx_write_y(int value) {}
void grafyx_write_data(int value) {}
int grafyx_read_data(void) {return memory[VIDEO_START]; }
void grafyx_write_mode(int value) {}


#define ADD_SHIFT_KEY 0x100
#define REMOVE_SHIFT_KEY 0x200

typedef struct {
  uint8_t offset;
  uint16_t mask;
} TRSKey;

static const TRSKey ascii2TrsKeys[] = { // Ascii to TRS keys
/* 0x00 */ {0, 0},
/* 0x01 */ {0, 0},
/* 0x02 */ {0, 0},
/* 0x03 */ {0, 0},
/* 0x04 */ {0, 0},
/* 0x05 */ {0, 0},
/* 0x06 */ {0, 0},
/* 0x07 */ {0, 0},
/* 0x08 */ {0, 0},
/* 0x09 */ {0, 0},  // tab
/* 0x0A */ {0, 0},
/* 0x0B */ {0, 0},
/* 0x0C */ {0, 0},
/* 0x0D */ {7, 1},  // enter
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
/* 0x1B */ {7, 4},   // esc
/* 0x1C */ {0, 0},
/* 0x1D */ {0, 0},
/* 0x1E */ {0, 0},
/* 0x1F */ {0, 0},
/* 0x20 */ {7, 128}, // space
/* 0x21 */ {5, 2},   // ! exclamation mark
/* 0x22 */ {5, 4},   // " double quote   
/* 0x23 */ {5, 8},   // # dies
/* 0x24 */ {5, 16},  // $ dollar
/* 0x25 */ {5, 32},  // % percent
/* 0x26 */ {5, 64},  // & ampersand
/* 0x27 */ {5, ADD_SHIFT_KEY | 128},  // ' singlequote
/* 0x28 */ {6, ADD_SHIFT_KEY | 1},   // ( bracket left
/* 0x29 */ {6, ADD_SHIFT_KEY | 2},   // ) bracket right
/* 0x2A */ {6, 4},   // * mult
/* 0x2B */ {6, 8},   // + plus
/* 0x2C */ {6, 16},  // , comma
/* 0x2D */ {6, 32},  // - minus
/* 0x2E */ {6, 64},  // . period
/* 0x2F */ {6, 128}, // / slash
/* 0x30 */ {5, 1},   // 0
/* 0x31 */ {5, 2},   // 1
/* 0x32 */ {5, 4},   // 2 
/* 0x33 */ {5, 8},   // 3 
/* 0x34 */ {5, 16},  // 4
/* 0x35 */ {5, 32},  // 5
/* 0x36 */ {5, 64},  // 6
/* 0x37 */ {5, 128}, // 7
/* 0x38 */ {6, 1},   // 8
/* 0x39 */ {6, 2},   // 9
/* 0x3A */ {6, REMOVE_SHIFT_KEY | 4},   // : colon
/* 0x3B */ {6, 8},   // ; semi colon
/* 0x3C */ {6, 16},  // <
/* 0x3D */ {6, ADD_SHIFT_KEY | 32},     // = equal
/* 0x3E */ {6, 64},  // >
/* 0x3F */ {6, 128}, // ?
/* 0x40 */  {1, REMOVE_SHIFT_KEY | 1},  // @
/* 0x41 */ {1, 2},   // A
/* 0x42 */ {1, 4},   // B
/* 0x43 */ {1, 8},   // C
/* 0x44 */ {1, 16},  // D
/* 0x45 */ {1, 32},  // E
/* 0x46 */ {1, 64},  // F
/* 0x47 */ {1, 128}, // G
/* 0x48 */ {2, 1},   // H
/* 0x49 */ {2, 2},   // I
/* 0x4A */ {2, 4},   // J
/* 0x4B */ {2, 8},   // K
/* 0x4C */ {2, 16},  // L
/* 0x4D */ {2, 32},  // M
/* 0x4E */ {2, 64},  // N
/* 0x4F */ {2, 128}, // O
/* 0x50 */ {3, 1},   // P
/* 0x51 */ {3, 2},   // Q
/* 0x52 */ {3, 4},   // R
/* 0x53 */ {3, 8},   // S
/* 0x54 */ {3, 16},  // T
/* 0x55 */ {3, 32},  // U
/* 0x56 */ {3, 64},  // V
/* 0x57 */ {3, 128}, // W
/* 0x58 */ {4, 1},   // X
/* 0x59 */ {4, 2},   // Y
/* 0x5A */ {4, 4},   // Z
/* 0x5B */ {0, 0},   // square bracket open
/* 0x5C */ {7, 2},   // backslach
/* 0x5D */ {0, 0},   // square braquet close
/* 0x5E */ {0, 0},   // ^ circonflex
/* 0x5F */ {0, 0},   // _ undescore
/* 0x60 */ {0, 0},   // `backquote
/* 0x61 */ {1, 2},   // a
/* 0x62 */ {1, 4},   // b
/* 0x63 */ {1, 8},   // c
/* 0x64 */ {1, 16},  // d
/* 0x65 */ {1, 32},  // e
/* 0x66 */ {1, 64},  // f
/* 0x67 */ {1, 128}, // g
/* 0x68 */ {2, 1},   // h
/* 0x69 */ {2, 2},   // i
/* 0x6A */ {2, 4},   // j
/* 0x6B */ {2, 8},   // k
/* 0x6C */ {2, 16},  // l
/* 0x6D */ {2, 32},  // m
/* 0x6E */ {2, 64},  // n
/* 0x6F */ {2, 128}, // o
/* 0x70 */ {3, 1},   // p
/* 0x71 */ {3, 2},   // q
/* 0x72 */ {3, 4},   // r
/* 0x73 */ {3, 8},   // s
/* 0x74 */ {3, 16},  // t
/* 0x75 */ {3, 32},  // u
/* 0x76 */ {3, 64},  // v
/* 0x77 */ {3, 128}, // w
/* 0x78 */ {4, 1},   // x
/* 0x79 */ {4, 2},   // y
/* 0x7A */ {4, 4},   // z
/* 0x7B */ {0, 0},   // curly bracket open
/* 0x7C */ {0, 0},   // or
/* 0x7D */ {0, 0},   // curly bracket close  
/* 0x7E */ {0, 0},   // tilt
/* 0x7F */  {7, 32}  // backspace
};


static const TRSKey trsKeys[] = {
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

int __not_in_flash("trs_kb_mem_read") trs_kb_mem_read(int address) {
  if (address & 0x80) return keyb_buffer[7];
  if (address & 0x40) return keyb_buffer[6];
  if (address & 0x20) return keyb_buffer[5];
  if (address & 0x10) return keyb_buffer[4];
  if (address & 0x01) return keyb_buffer[0];
  if (address & 0x02) return keyb_buffer[1];
  if (address & 0x04) return keyb_buffer[2];
  if (address & 0x08) return keyb_buffer[3];
  return 0;
}


int trs_process_vkkey(int vk, bool down)
{
  static bool shiftPressed = false;
  
  int offset = trsKeys[vk].offset;

  if (offset != 0) {
    bool addShiftKey = trsKeys[vk].mask & ADD_SHIFT_KEY;
    bool removeShiftKey = trsKeys[vk].mask & REMOVE_SHIFT_KEY;
    uint8_t mask = trsKeys[vk].mask & 0xff;
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


int trs_process_asciikey(int vk, bool down)
{
  static bool shiftPressed = false;
  
  int offset = ascii2TrsKeys[vk].offset;

  if (offset != 0) {
    bool addShiftKey = ascii2TrsKeys[vk].mask & ADD_SHIFT_KEY;
    bool removeShiftKey = ascii2TrsKeys[vk].mask & REMOVE_SHIFT_KEY;
    uint8_t mask = ascii2TrsKeys[vk].mask & 0xff;
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
