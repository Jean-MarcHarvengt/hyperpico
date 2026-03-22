#include "pico.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "pico_dsp.h"
#include "usb_serial.h"

#include "memmap.h"

#include "trs_memory.h"

PICO_DSP tft;

#ifdef CPU_EMU
#define KEY_DEBOUNCE_MS 50
#define BOOT_SEQ_MS 1500
#else
#define KEY_DEBOUNCE_MS 50
#define BOOT_SEQ_MS 1500
#endif

#if (defined(CPU_EMU) || defined(CPU_Z80))
#include "tools/z80assembler/fb.h"
#endif

// ****************************************
// Keyboard
// ****************************************
static int prev_key = 0;
static int repeat_cnt = 0;
static bool send_cmdstring = false;
static char * cmdstring_pt;

//static char trsinitcmd[] = {0x01, 0x0d, 0x01, 0x0d, 0};

#if (defined(CPU_EMU) || defined(CPU_Z80))
static char trsinitcmd[] = {0x01, 0x0d, 0x01, 0x0d, 1, 2, 'x','=','u','s','r','(','0',')',0x0d, 0};
#endif
static char trsruncmd[] = {'x','=','u','s','r','(','0',')',0x0d, 0};


//uint8_t chr=0;
bool repeating_timer_callback(struct repeating_timer *t) {
    //memory[0x3d04] = chr++;  
    //return true;
    if (repeat_cnt ) repeat_cnt--;
    if (repeat_cnt == 0) {
      if (prev_key) {
        trs_process_asciikey(prev_key, false);
        prev_key = 0;
      }
      if (send_cmdstring) {
        if (*cmdstring_pt) {
          int asciikey = *cmdstring_pt++&0x7f;
          if (asciikey == 1) {
            repeat_cnt = BOOT_SEQ_MS; 
            prev_key = 0;
          } 
          else
          if (asciikey == 2) {
#if (defined(CPU_EMU) || defined(CPU_Z80))
            memcpy((void*)&memory[fb[1]*256+fb[0]],(void*)&fb[2], sizeof(fb)-2);
            memory[16526] = fb[0];
            memory[16527] = fb[1];
#endif            
            prev_key = 0;
          } 
          else
          if (asciikey) if ( trs_process_asciikey(asciikey, true) ) {
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

void wait_ms(int ms) {
  sleep_ms(ms);
}

// ****************************************
// USB SERIAL server
// ****************************************
static int serial_rx(uint8_t* buf, int len) {
  int asciikey;

  if (len >= 1) {
    
    switch (buf[0]) {
      case sercmd_reset:
        trs_play(0);
        break;
      case sercmd_key:        
        //cmd[3] = toupper((char)buf[1]);
        asciikey = buf[1]&0x7f;
        if (asciikey) if ( trs_process_asciikey(asciikey, true) ) {
          prev_key = asciikey;
          repeat_cnt = KEY_DEBOUNCE_MS; 
        }
        //pet_command( &cmd[0] );
        break;
      case sercmd_prg:
        //trs_pauze();
        for (int i=0; i < (len-3); i++) {
          memory[((buf[1]<<8)+buf[2])+i] = buf[3+i];
        }   
        break;
      case sercmd_run:
        memory[16526] = buf[2];
        memory[16527] = buf[1];
        cmdstring_pt = &trsruncmd[0];
        send_cmdstring = true; 
        //trs_play((buf[1]<<8)+buf[2]);
        break;
      default:
        break;
    }
  }  
  return 0;
}



int main(void) {
//    vreg_set_voltage(VREG_VOLTAGE_1_05);
    set_sys_clock_khz(280000, true);
//    set_sys_clock_khz(300000, true);
    *((uint32_t *)(0x40010000+0x58)) = 2 << 16; //CLK_HSTX_DIV = 2 << 16; // HSTX clock/2

    stdio_init_all();
    tft.begin(MODE_VGA_640x240);
	  tft.startRefresh();

#if (defined(CPU_EMU) || defined(CPU_Z80))

#ifdef HAS_USBHOST
    board_init();
    printf("Init USB...\n");
    
    printf("TinyUSB Host HID Controller Example\r\n");
    printf("Note: Events only displayed for explicit supported controllers\r\n");
    // init host stack on configured roothub port
    tuh_init(BOARD_TUH_RHPORT);
#endif
    usb_serial_init(&serial_rx);
    struct repeating_timer timer;
    add_repeating_timer_ms(-1, repeating_timer_callback, NULL, &timer); 

    cmdstring_pt = &trsinitcmd[0];
    send_cmdstring = true; 
#endif

    start_system();   
    // will never return!!
}


void SystemReset(void) {
    prev_key = 0;
    cmdstring_pt = &trsinitcmd[0];
    send_cmdstring = true; 
}