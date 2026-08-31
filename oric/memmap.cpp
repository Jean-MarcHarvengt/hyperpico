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


       

#ifdef CPU_EMU
// 6502 emu

//#include "basic11b.h"
//#include "basic11bP.h"


#include "system_d.h"

SYSTEM System;
\
#include "m6502.h"
#include "system.h"
#include "ula.h"
#include "io.h"
//#include "ym2149.h"



#include "hdmi_framebuffer.h"

#ifdef HAS_USBHOST
#include "bsp/board_api.h"
#include "tusb.h"
#include "kbd.h"
extern "C" void hid_app_task(void);
#else
#include "usb_serial.h"
#endif
#endif


#define MEMORY_SIZE 0x10000

uint8_t memory[MEMORY_SIZE];
static bool got_reset=false;



static void __not_in_flash("pio_core") pio_core(void)
{
  while(true) { 
#ifdef CPU_EMU
    
    if (got_reset)
    {
      got_reset = false;
      HyperGfxReset();
      sleep_ms(30);
/*
      prev_key = 0;
      pet_start();      
      pet_running = true;
      if (hyper_enabled) {
        cmdstring_pt = &petfbcmd[0];
        send_cmdstring = true;
        repeat_cnt = 0;
      }
*/
    }
/*
    for (int i = 8; i < 408; i = i + 2) {
        hdmi_wait_line(i);
        pet_line();
    }
    pet_remaining();
*/
    //m6502DoOps(CPU_FREQ/CPU_SLOT_FREQ);
    System.nSum += m6502DoOps(CPU_FREQ/CPU_SLOT_FREQ);
    ulaDisplay2((System.vSyncCount++ >> 5) & 1);
    BYTE *crt = (BYTE *)((BYTE *)hdmi_get_line_buffer(0));
    *crt = 0xff;

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
  //hyper_enabled = true;

#ifdef CPU_EMU
#ifdef HAS_USBHOST
  //board_init();
  tuh_init(BOARD_TUH_RHPORT);
#else
  //usb_serial_init(&serial_rx);
#endif
  struct repeating_timer timer;
  //add_repeating_timer_ms(-1, repeating_timer_callback, NULL, &timer);
  if (hyper_enabled) {
    //cmdstring_pt = &petfbcmd[0];
    //send_cmdstring = true;
    //repeat_cnt = 0;
  }
#endif

  HyperGfxFlashFSInit();  
  HyperGfxInit();


#ifdef CPU_EMU
  //videoInit();
  //pet_start(); 
  systemInit(1, 0);
  System.tSum = 0;
  System.tN = 0;  
  multicore_launch_core1(pio_core);

#else
  multicore_launch_core1(pio_core);
#endif

  while(true) {
    //HyperGfxHandleGfx();    
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