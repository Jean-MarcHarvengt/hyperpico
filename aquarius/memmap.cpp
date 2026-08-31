#include <string.h>
#include <ctype.h>

#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

#include "memmap.h" // contains config !!!

extern "C" {
  #include "iopins.h"   
}

#include "tools/z80assembler/fb.h"

#if (defined(CPU_EMU) || defined(CPU_Z80))

#ifdef HAS_USBHOST
#include "bsp/board_api.h"
#include "tusb.h"
#include "kbd.h"
extern "C" void hid_app_task(void);
#else
#include "usb_serial.h"
#endif
#endif

#ifdef HAS_NETWORK
#include "network.h"
#include "lwip/apps/tftp_server.h"
#endif

#ifdef CPU_Z80
#include "clock.pio.h"
#endif
#include "busreadwrite.pio.h"

#ifdef CPU_EMU
#include "hdmi_framebuffer.h"
#endif

#include "hypergfx.h"

#include "memory.h"

//#ifdef EXPANSION_SLOT
#define PIO_DIV 2.0f // 2MHz for expansion (pio slower)
//#else
//#define PIO_DIV 1.0f // 4MHz
//#endif

static PIO bus_pio;
static uint bus_smr;
static uint bus_smw;

#ifdef CPU_Z80
static PIO clock_pio;
static uint clock_sm;
#endif

#define ADDBUS_WIDTH 16
#define ADDBUS_MASK  (MEMORY_SIZE-1) 

uint8_t memory[MEMORY_SIZE];
static uint32_t armaddr = ((uint32_t)memory);

static bool got_reset=false;

#if (defined(CPU_EMU) || defined(CPU_Z80))
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
#endif



#ifdef BUS_DEBUG
static uint8_t addr[256];
static uint8_t memptr=0;
//static uint8_t rderror=0;

static uint8_t addw[256];
static uint8_t memptw=0;
//static uint8_t wrerror=0;

static const char * digits = "0123456789ABCDEF";

static void __not_in_flash("DebugShow") DebugShow(void)
{
  uint8_t * buf;
/*      
  buf = &memory[VIDEO_START];
  buf[0] = digits[(vv>>4)&0xf];
  buf[1] = digits[(vv)&0xf];

  buf = &memory[VIDEO_START+4];
  buf[0] = digits[(aa>>12)&0xf];
  buf[1] = digits[(aa>>8)&0xf];
  buf[2] = digits[(aa>>4)&0xf];
  buf[2] = digits[(aa)&0xf];
*/
/*

  buf[0] = digits[(rderror>>4)&0xf];
  buf[1] = digits[(rderror)&0xf];        

  buf[0] = digits[(wrerror>>4)&0xf];
  buf[1] = digits[(wrerror)&0xf];        
*/

  for (int j=0;j<256;j++) {
      uint8_t s = addr[j];
      buf = &memory[VIDEO_START+2*64+64*((j>>4)&0xf) + (j&0xf)*2];
      buf[0] = digits[(s>>4)&0xf];
      buf[1] = digits[(s)&0xf];

      buf = &memory[VIDEO_START+2*64+32+64*((j>>4)&0xf) + (j&0xf)*2];
      s = addw[j];
      buf[0] = digits[(s>>4)&0xf];
      buf[1] = digits[(s)&0xf];
  }
/*        
  buf[1] = 0;
  for (int j=0;j<256;j++) {
      buf[0] = mem_datar[j];
      tft.drawText(((j)&0x1f)*8,(18+((j>>5)&0xf))*8,buf,RGBVAL16(0x00,0x00,0x00),RGBVAL16(0xFF,0xFF,0xFF),false);

      buf[0] = mem_dataw[j];
      tft.drawText(320 + ((j)&0x1f)*8,(18+((j>>5)&0xf))*8,buf,RGBVAL16(0x00,0x00,0x00),RGBVAL16(0xFF,0xFF,0xFF),false);
  }    
*/
  //__dmb();
  //sleep_ms(5000);
}
#endif




// ****************************************
// AQUA Memory
// ****************************************
#ifdef CPU_EMU  
static uint8_t __not_in_flash("readNone") readNone(uint16_t address) {
  return (0xff);
#else
static void __not_in_flash("readNone") readNone(uint16_t address) {
  bus_pio->txf[bus_smr] = 0;
#endif  
}

#ifdef CPU_EMU  
static uint8_t __not_in_flash("readMEM") readMEM(uint16_t address) {
  return (memory[address]);
#else
static void __not_in_flash("readMEM") readMEM(uint16_t address) {
  bus_pio->txf[bus_smr] = 0x100 | memory[address];
#endif  
}


static void __not_in_flash("writeNone") writeNone(uint16_t address, uint8_t value) {
}

static void __not_in_flash("writeMEM") writeMEM(uint16_t address, uint8_t value) {
  memory[address]=value;
}

static void __not_in_flash("writeMEMTOPF800") writeMEMTOPF800(uint16_t address, uint8_t value) {
  //if (address < 0xfd00) // top of memory
    memory[address]=value;
}


typedef void (*WriteFunc)(uint16_t,uint8_t);
#ifdef CPU_EMU  
typedef uint8_t (*ReadFunc)(uint16_t);
ReadFunc __not_in_flash("readFuncTable") * readFuncTable;
WriteFunc __not_in_flash("writeFuncTable") * writeFuncTable;
#else
typedef void (*ReadFunc)(uint16_t);
static ReadFunc __not_in_flash("readFuncTable") * readFuncTable;
static WriteFunc __not_in_flash("writeFuncTable") * writeFuncTable;
#endif

ReadFunc __not_in_flash("readFuncTable_hyper") readFuncTable_hyper[32]
{
#if (defined(CPU_EMU) || defined(CPU_Z80))
  readMEM,   // 00        // 12K ROM MAX
  readMEM,   // 08
  readMEM,   // 10
  readMEM,   // 18
  readMEM,   // 20
  readMEM,   // 28  
  readMEM, // 30        // VRAM + CRAM (1k+1k)
  readMEM,   // 38      // RAM (2k)
  readMEM,   // 40      // first  4K
  readMEM,   // 48
#else
  readNone,  // 00
  readNone,  // 08
  readNone,  // 10
  readNone,  // 18
  readNone,  // 20
  readNone,  // 28
  readNone,  // 30
  readNone,  // 38 
  readNone,  // 40
  readNone,  // 48
#endif
  readMEM,   // 50
  readMEM,   // 58
  readMEM,   // 60
  readMEM,   // 68
  readMEM,   // 70
  readMEM,   // 78
  readMEM,   // 80
  readMEM,   // 88
  readMEM,   // 90
  readMEM,   // 98
  readMEM,   // a0
  readMEM,   // a8
  readMEM,   // b0
  readMEM,   // b8
  readMEM,   // c0
  readMEM,   // c8
  readMEM,   // d0
  readMEM,   // d8
  readMEM,   // e0
  readMEM,   // e8
  readMEM,   // f0
  readMEM    // f8
};

WriteFunc __not_in_flash("writeFuncTable_hyper") writeFuncTable_hyper[32]
{
  writeNone, // 00      // 12K ROM
  writeNone, // 08 
  writeNone, // 10
  writeNone, // 18
  writeNone, // 20
  writeNone, // 28
#if (defined(CPU_EMU) || defined(CPU_Z80))
  writeMEM, // 30       // VRAM + CRAM (1k+1k)
  writeMEM,  // 38      // RAM (2k)
  writeMEM, // 40       // first 4K RAM
  writeMEM, // 48
#else
  writeMEM, // 30       // VRAM + CRAM (1k+1k)
  writeNone,  // 38     // RAM (2k)
  writeNone, // 40      // first 4K RAM
  writeNone, // 48
#endif  
  writeMEM,  // 50
  writeMEM,  // 58
  writeMEM,  // 60
  writeMEM,  // 68
  writeMEM,  // 70
  writeMEM,  // 78
  writeMEM,  // 80 
  writeMEM,  // 88
  writeMEM,  // 90
  writeMEM,  // 98
  writeMEM,  // a0
  writeMEM,  // a8
  writeMEM,  // b0
  writeMEM,  // b8
  writeMEM,  // c0 
  writeMEM,  // c8
  writeMEM,  // d0
  writeMEM,  // d8
  writeMEM,  // e0
  writeMEM,  // e8
  writeMEM,  // f0
#if (defined(CPU_EMU) || defined(CPU_Z80))
  writeMEMTOPF800  // f8
#else
  //writeMEM    // f8
  writeMEMTOPF800    // f8 // for initial memory test!
#endif
};

ReadFunc __not_in_flash("readFuncTable_nohyper") readFuncTable_nohyper[32]
{
#if (defined(CPU_EMU) || defined(CPU_Z80))
  readMEM,   // 00      // 12K ROM MAX
  readMEM,   // 08
  readMEM,   // 10
  readMEM,   // 18
  readMEM,   // 20
  readMEM,   // 28  
  readMEM,   // 30     // VRAM + CRAM (1k+1k)
  readMEM,   // 38     // RAM (2k)
  readMEM,   // 40      
  readMEM,   // 48
#else
  readNone,  // 00
  readNone,  // 08
  readNone,  // 10
  readNone,  // 18
  readNone,  // 20
  readNone,  // 28
  readNone,  // 30
  readNone,  // 38 
  readNone,  // 40
  readNone,  // 48
#endif
/*  
  readMEM,   // 50
  readMEM,   // 58
  readMEM,   // 60
  readMEM,   // 68
  readMEM,   // 70
  readMEM,   // 78
  readMEM,   // 80
  readMEM,   // 88
  readMEM,   // 90
  readMEM,   // 98
  readMEM,   // a0
  readMEM,   // a8
  readMEM,   // b0
  readMEM,   // b8
  readMEM,   // c0
  readMEM,   // c8
  readMEM,   // d0
  readMEM,   // d8
  readMEM,   // e0
  readMEM,   // e8
  readMEM,   // f0
  readMEM    // f8
*/
  readMEM,   // 50
  readMEM,   // 58
  readMEM,   // 60
  readMEM,   // 68
  readMEM,   // 70
  readMEM,   // 78
  readNone,   // 80
  readNone,   // 88
  readNone,   // 90
  readNone,   // 98
  readNone,   // a0
  readNone,   // a8
  readNone,   // b0
  readNone,   // b8
  readMEM,   // c0
  readMEM,   // c8
  readMEM,   // d0
  readMEM,   // d8
  readMEM,   // e0
  readMEM,   // e8
  readMEM,   // f0
  readMEM    // f8

};


WriteFunc __not_in_flash("writeFuncTable_hyper") writeFuncTable_nohyper[32]
{
  writeNone, // 00      // 12K ROM
  writeNone, // 08 
  writeNone, // 10
  writeNone, // 18
  writeNone, // 20
  writeNone, // 28
#if (defined(CPU_EMU) || defined(CPU_Z80))
  writeMEM,  // 30      // VRAM + CRAM (1k+1k)
  writeMEM,  // 38      // RAM (2k)
  writeMEM,  // 40      // first 4K RAM
  writeMEM,  // 48
#else
  writeMEM,  // 30      // VRAM + CRAM (1k+1k)
  writeNone,  // 38     // RAM (2k)
  writeNone, // 40      // first 4K RAM
  writeNone, // 48
#endif  
/*
  writeMEM,  // 50
  writeMEM,  // 58
  writeMEM,  // 60
  writeMEM,  // 68
  writeMEM,  // 70
  writeMEM,  // 78
  writeMEM,  // 80 
  writeMEM,  // 88
  writeMEM,  // 90
  writeMEM,  // 98
  writeMEM,  // a0
  writeMEM,  // a8
  writeMEM,  // b0
  writeMEM,  // b8
  writeMEM,  // c0 
  writeMEM,  // c8
  writeMEM,  // d0
  writeMEM,  // d8
  writeMEM,  // e0
  writeMEM,  // e8
  writeMEM,  // f0
#if (defined(CPU_EMU) || defined(CPU_Z80))
  writeMEMTOPF800  // f8
#else
  writeMEM    // f8
#endif
*/

  
  writeMEM,  // 50
  writeMEM,  // 58
  writeMEM,  // 60
  writeMEM,  // 68
  writeMEM,  // 70
  writeMEM,  // 78
  writeNone,  // 80 
  writeNone,  // 88
  writeNone,  // 90
  writeNone,  // 98
  writeNone,  // a0
  writeNone,  // a8
  writeNone,  // b0
  writeNone,  // b8
  writeMEM,  // c0 
  writeMEM,  // c8
  writeMEM,  // d0
  writeMEM,  // d8
  writeMEM,  // e0
  writeMEM,  // e8
  writeMEM,  // f0
  writeMEM   // f8  
};



extern "C" void pioirq_asmr(void);

void __not_in_flash("__time_critical_func") pioirq_smr(void) {
  if(!pio_sm_is_rx_fifo_empty(bus_pio, bus_smr)) {
      uint16_t add = pio_sm_get(bus_pio, bus_smr);        
#ifdef EXPANSION_SLOT
      if (gpio_get(CONFIG_PIN_BUS_IOIN) )
#else         
      if (gpio_get(CONFIG_PIN_BUS_IOREQ) )
#endif       
        readFuncTable[add>>11](add);
#if (defined(CPU_Z80))
      else {
        if ((add & 0xff) == 0xff) {
          bus_pio->txf[bus_smr] = 0x100 | aqua_kb_mem_read(add >> 8);
        }
        else {
          bus_pio->txf[bus_smr] = 0x100 | 0xff;
        }
      }
#endif
      
#ifdef BUS_DEBUG                        
      addr[memptr++] = add & 0xff;           
#endif 
  }
}

extern "C" void pioirq_asmw(void);

void __not_in_flash("__time_critical_func") pioirq_smw(void) {
  if(!pio_sm_is_rx_fifo_empty(bus_pio, bus_smw)) {
      uint32_t value = pio_sm_get(bus_pio, bus_smw);
      uint16_t add = (value >> 8);
#ifdef EXPANSION_SLOT
      if (gpio_get(CONFIG_PIN_BUS_IOOUT) )
#else         
      if (gpio_get(CONFIG_PIN_BUS_IOREQ) )
#endif
        writeFuncTable[add>>11](add,value & 0xff);            

#ifdef BUS_DEBUG    
      addw[memptw++] = (value >> 8)&0xff;
#endif
  }
}

static void run_pio(void) {
    bus_pio = pio0;
    bus_smr = 0;
    bus_smw = 1;

#ifdef CPU_Z80
    clock_pio = pio1;
    clock_sm = 0;

    // Init CLOCK SM
    pio_sm_claim(clock_pio, clock_sm);
    uint clock_program_offset = pio_add_program(clock_pio, &clock_program);
    pio_sm_config cc = clock_program_get_default_config(clock_program_offset);
    // set pin as output and set
    sm_config_set_out_pins(&cc, TRS_CLOCK, 1);
    sm_config_set_set_pins(&cc, TRS_CLOCK, 1);
    // Set this pin's GPIO function (connect PIO to the pad)
    pio_gpio_init(clock_pio, TRS_CLOCK);
    // Set the pin direction to output at the PIO
    pio_sm_set_consecutive_pindirs(clock_pio, clock_sm, TRS_CLOCK, 1, true);
    // Load our configuration, and jump to the start of the program
    pio_sm_init(clock_pio, clock_sm, clock_program_offset, &cc);
    pio_sm_set_clkdiv(clock_pio, clock_sm, PIO_DIV);    
#endif

    // Init BUS SM Write
    pio_sm_claim(bus_pio, bus_smw);
    pio_sm_clear_fifos(bus_pio,bus_smw);
    uint buswrite_program_offset = pio_add_program(bus_pio, &buswrite_program);
    pio_sm_config cw = buswrite_program_get_default_config(buswrite_program_offset);
    sm_config_set_in_pins(&cw, CONFIG_PIN_BUS_DATA_BASE);
    // map the SET pin group to the Data transceiver control signals
    sm_config_set_set_pins(&cw, CONFIG_PIN_BUS_CONTROL_BASE, 3);
    // left shift into ISR & autopush every 24 bits
    sm_config_set_in_shift(&cw, false, true, 24);
    pio_sm_init(bus_pio, bus_smw, buswrite_program_offset, &cw);
    pio_sm_set_clkdiv(bus_pio, bus_smw, PIO_DIV);
    // configure the GPIOs
    //pio_sm_set_consecutive_pindirs(bus_pio, bus_smw, CONFIG_PIN_BUS_CONTROL_BASE, 3, true);
    // Ensure all transceivers disabled 
    pio_sm_set_pins_with_mask(
      bus_pio, bus_smw, ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) , 
               ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) )  ;
    pio_sm_set_pindirs_with_mask(bus_pio, bus_smw, ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) ,
      ((uint32_t)0x1 << CONFIG_PIN_BUS_WR) | ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) | ((uint32_t)0xff << CONFIG_PIN_BUS_DATA_BASE));

    // Init BUS SM Read
    pio_sm_claim(bus_pio, bus_smr);
    pio_sm_clear_fifos(bus_pio,bus_smr);
    uint busread_program_offset = pio_add_program(bus_pio, &busread_program);
    pio_sm_config cr = busread_program_get_default_config(busread_program_offset);
    // set the bus IOREQ pin as the jump pin
    //sm_config_set_jmp_pin(&cr, CONFIG_PIN_BUS_IOREQ);
    // map the IN/OUT pin group to the data signals
    sm_config_set_in_pins(&cr, CONFIG_PIN_BUS_DATA_BASE);
    sm_config_set_out_pins(&cr, CONFIG_PIN_BUS_DATA_BASE, 8);
    // map the SIDESET pin as DATADIR
    sm_config_set_sideset_pins(&cr, CONFIG_PIN_BUS_DATADIR);
    // map the SET pin group to the bus transceiver enable signals
    sm_config_set_set_pins(&cr, CONFIG_PIN_BUS_CONTROL_BASE, 3);
    // shift left, autopush, 16bits
    sm_config_set_in_shift(&cr, false, true, 16); //RX queue (get)
    // shift right, no autopush, 16bits
    sm_config_set_out_shift(&cr, true, false, 16); //TX queue (get) 
    pio_sm_init(bus_pio, bus_smr, busread_program_offset, &cr);
    // Set the pin direction to output at the PIO
    //pio_sm_set_consecutive_pindirs(bus_pio, bus_smr, CONFIG_PIN_BUS_CONTROL_BASE, 3, true);
    pio_sm_set_consecutive_pindirs(bus_pio, bus_smr, CONFIG_PIN_BUS_DATA_BASE, 8, false);

    // configure the GPIOs
    // Ensure all transceivers disabled and datadir is 1 (input) 
    pio_sm_set_pins_with_mask(
      bus_pio, bus_smr, ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) | ((uint32_t)0x1 << CONFIG_PIN_BUS_DATADIR), 
               ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) | ((uint32_t)0x1 << CONFIG_PIN_BUS_DATADIR) );
    pio_sm_set_pindirs_with_mask(bus_pio, bus_smr, ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) | ((uint32_t)0x1 << CONFIG_PIN_BUS_DATADIR) ,
      ((uint32_t)0x1 << CONFIG_PIN_BUS_RD) | ((uint32_t)0x7 << CONFIG_PIN_BUS_CONTROL_BASE) | ((uint32_t)0x1 << CONFIG_PIN_BUS_DATADIR) | ((uint32_t)0xff << CONFIG_PIN_BUS_DATA_BASE));

    // Disable input synchronization on input pins that are sampled at known stable times
    // to shave off two clock cycles of input latency
    bus_pio->input_sync_bypass |= ((0xff << CONFIG_PIN_BUS_DATA_BASE) |  ((uint32_t)0x1 << CONFIG_PIN_BUS_RD) | ((uint32_t)0x1 << CONFIG_PIN_BUS_WR));
    for(int pin = CONFIG_PIN_BUS_CONTROL_BASE; pin < CONFIG_PIN_BUS_CONTROL_BASE + 3; pin++) {
      pio_gpio_init(bus_pio, pin);
    }
    pio_gpio_init(bus_pio, CONFIG_PIN_BUS_DATADIR);
    for(int pin = CONFIG_PIN_BUS_DATA_BASE; pin < CONFIG_PIN_BUS_DATA_BASE + 8; pin++) {
      pio_gpio_init(bus_pio, pin);
      gpio_set_pulls(pin, true, false);
    }
    pio_gpio_init(bus_pio, CONFIG_PIN_BUS_RD);
    gpio_set_pulls(CONFIG_PIN_BUS_RD, false, false);
    pio_gpio_init(bus_pio, CONFIG_PIN_BUS_WR);
    gpio_set_pulls(CONFIG_PIN_BUS_WR, false, false);
#ifdef EXPANSION_SLOT   
    gpio_init(CONFIG_PIN_BUS_IOIN);
    gpio_set_dir(CONFIG_PIN_BUS_IOIN, GPIO_IN);
    gpio_set_pulls(CONFIG_PIN_BUS_IOIN, false, false);
    gpio_init(CONFIG_PIN_BUS_IOOUT);
    gpio_set_dir(CONFIG_PIN_BUS_IOOUT, GPIO_IN);
    gpio_set_pulls(CONFIG_PIN_BUS_IOOUT, false, false);    
#else
    gpio_init(CONFIG_PIN_BUS_IOREQ);
    gpio_set_dir(CONFIG_PIN_BUS_IOREQ, GPIO_IN);
    gpio_set_pulls(CONFIG_PIN_BUS_IOREQ, false, false);
#endif   
    pio_sm_set_clkdiv(bus_pio, bus_smr, PIO_DIV);

    // Set pio IRQs to tell us when the RX FIFO is NOT empty
    pio_set_irq0_source_mask_enabled(bus_pio,(1u << pio_get_rx_fifo_not_empty_interrupt_source(bus_smr)), true);
#ifdef HAS_ASMIRQ
    irq_set_exclusive_handler ((bus_pio == pio0) ? PIO0_IRQ_0 : PIO1_IRQ_0,  pioirq_asmr ); 
#else
    irq_set_exclusive_handler ((bus_pio == pio0) ? PIO0_IRQ_0 : PIO1_IRQ_0,  pioirq_smr  ); 
#endif    
    irq_set_enabled((bus_pio == pio0) ? PIO0_IRQ_0 : PIO1_IRQ_0, true); // Enable the IRQ

    pio_set_irq1_source_mask_enabled(bus_pio,(1u << pio_get_rx_fifo_not_empty_interrupt_source(bus_smw)), true);
#ifdef HAS_ASMIRQ
    irq_set_exclusive_handler ((bus_pio == pio0) ? PIO0_IRQ_1 : PIO1_IRQ_1, pioirq_asmw  );
#else
    irq_set_exclusive_handler ((bus_pio == pio0) ? PIO0_IRQ_1 : PIO1_IRQ_1, pioirq_smw );
#endif
    irq_set_enabled((bus_pio == pio0) ? PIO0_IRQ_1 : PIO1_IRQ_1, true); // Enable the IRQ

    irq_set_priority ((bus_pio == pio0) ? PIO0_IRQ_0 : PIO1_IRQ_0, PICO_DEFAULT_IRQ_PRIORITY-64);
    irq_set_priority ((bus_pio == pio0) ? PIO0_IRQ_1 : PIO1_IRQ_1, PICO_DEFAULT_IRQ_PRIORITY-64);


/*
    irq_set_enabled(TIMER1_IRQ_0, false);
    irq_set_enabled(TIMER1_IRQ_1, false);
    irq_set_enabled(TIMER1_IRQ_2, false);
    irq_set_enabled(TIMER1_IRQ_3, false);
    irq_set_enabled(PWM_IRQ_WRAP_1, false);
    irq_set_enabled(USBCTRL_IRQ, false);
    //irq_set_enabled(XIP_IRQ, false);
    //irq_set_enabled(PIO0_IRQ_0, false);
    //irq_set_enabled(PIO0_IRQ_1, false);
    //irq_set_enabled(PIO1_IRQ_0, false);
    //irq_set_enabled(PIO1_IRQ_1, false);
    irq_set_enabled(DMA_IRQ_0, false);
    irq_set_enabled(DMA_IRQ_1, false);
    irq_set_enabled(IO_IRQ_BANK0, false);
    irq_set_enabled(IO_IRQ_QSPI , false);
    //irq_set_enabled(SIO_IRQ_PROC0, false);
    //irq_set_enabled(SIO_IRQ_PROC1, false);
    irq_set_enabled(CLOCKS_IRQ  , false);
    irq_set_enabled(SPI0_IRQ  , false);
    irq_set_enabled(SPI1_IRQ  , false);
    irq_set_enabled(UART0_IRQ , false);
    irq_set_enabled(UART1_IRQ , false);
    irq_set_enabled(I2C0_IRQ, false);
    irq_set_enabled(I2C1_IRQ, false);
    //irq_set_enabled(RTC_IRQ, false);
*/

    // Pass the top bits of the MEM's base address to the state machine
    //pio_sm_put_blocking(bus_pio, bus_smr, armaddr >> ADDBUS_WIDTH);
    pio_sm_clear_fifos(bus_pio,bus_smr);
    pio_sm_clear_fifos(bus_pio,bus_smw);

#ifdef CPU_Z80
    gpio_init(TRS_RESET);
    gpio_set_dir(TRS_RESET, GPIO_OUT);
    gpio_put(TRS_RESET, 1);
    sleep_ms(100);
    gpio_put(TRS_RESET, 0);
    pio_enable_sm_mask_in_sync(bus_pio, (1 << bus_smr) | (1 << bus_smw));
    pio_sm_set_enabled(clock_pio, clock_sm, true);
#else
    pio_enable_sm_mask_in_sync(bus_pio, (1 << bus_smr) | (1 << bus_smw));
#endif
}




// ****************************************
// Timer
// ****************************************
#define KEY_DEBOUNCE_MS 50
#define BOOT_SEQ_MS 700

static int repeat_cnt = 0;
static bool send_cmdstring = false;
static char * cmdstring_pt;
//static char initcmd[] = {0x01, 0x0d, 0x01, 0x01, 0x0d, 1, 1, 1, 1, 2, 'x','=','u','s','r','(','0',')',0x0d, 0};
static char initcmd[] = {0x01, 0x0d, 0};
static char runcmd[] = {'x','=','u','s','r','(','0',')',0x0d, 0};
static char copycmd[] = {1, 2,0}; // copy FB after 10 sec


static int prev_key = 0;


// system (not working)
// /57345 or -8191
//
//Start File Browser (working)
//POKE 16526,0
//POKE 16527,235
//a=usr(0)


static bool repeating_timer_callback(struct repeating_timer *t) {
    if (repeat_cnt ) repeat_cnt--;
    if (repeat_cnt == 0) {
      if (prev_key) {
        aqua_process_asciikey(prev_key, false);
        prev_key = 0;
      }
      if (send_cmdstring) {
        if (*cmdstring_pt) {
          int asciikey = *cmdstring_pt++&0x7f;
          if (asciikey == 1) {
            repeat_cnt = BOOT_SEQ_MS; 
            prev_key = 0;
          } 
          else if (asciikey == 2) {
            if (hyper_enabled) {
              memcpy((void*)&memory[fb[1]*256+fb[0]],(void*)&fb[2], sizeof(fb)-2);
              memory[16526] = fb[0];
              memory[16527] = fb[1];
              memory[0x3c00] = 'F';
              memory[0x3c01] = 'B';
              memory[0x3c02] = ' ';
              memory[0x3c03] = 'L';
              memory[0x3c04] = 'O';
              memory[0x3c05] = 'A';
              memory[0x3c06] = 'D';
              memory[0x3c07] = 'E';
              memory[0x3c08] = 'D';
              writeFuncTable_hyper[31]=HyperGfxWrite;
              prev_key = 0;              
            }
          } 
          else if (asciikey) if ( aqua_process_asciikey(asciikey, true) ) 
          {
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

#if (defined(CPU_EMU) || defined(CPU_Z80))

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

//    if (prev_code) pet_kup(prev_code);
    if (prev_code) aqua_process_vkkey(prev_code, false);

    if (code) {
      if (pressed == KEY_PRESSED)
      {
        //pet_kdown(code, flags & KBD_FLAG_RSHIFT, flags & KBD_FLAG_RCONTROL);
        aqua_process_vkkey(code, true); 
        prev_code = code;
        //printf("kdown %c\r\n", kbd_to_ascii (code, flags));
      }
      else 
      {
        //pet_kup(code);
        aqua_process_vkkey(code, false); 
        //printf("kup %c\r\n", kbd_to_ascii (code, flags));
      }
    }    
  }  
}


#else
//static uint8_t memo[0x10000];

// ****************************************
// USB SERIAL server
// ****************************************
//static int serial_rx(uint8_t* buf, int len) {
static int __not_in_flash_func(serial_rx)(uint8_t* buf, int len) {
  int asciikey;
  uint16_t addr;

  if (len >= 1) {
    
    switch (buf[0]) {
      case sercmd_reset:
#ifdef CPU_EMU
        got_reset = true;
#endif        
        break;
      case sercmd_key:        
        asciikey = buf[1]&0x7f;
        if (send_cmdstring == false) {
          if (asciikey) if ( aqua_process_asciikey(asciikey, true) ) 
          {        
            prev_key = asciikey;
            repeat_cnt = KEY_DEBOUNCE_MS; 
          }          
        }
        break;
      case sercmd_prg:
        if (len>3) {
          //addr = 0x4000; //0x3c00;
          //addr = ((uint16_t)buf[1]<<8)+buf[2];
          //sleep_ms(100);
          //memory[0x3c00] = buf[3]; //memory[0x3c00]+1;
//          for (int i=0; i < (len-3); i++) {
//            memory[0x3c00+i] = buf[i<62?i:0];
//            if (i<62) memory[0x3c00+i] = buf[i];
//            if ( ( (addr+i) >= 0x4000 ) && ((addr+i) < 0x4100 ) ) memory[addr+i] = buf[i];
//            memory[addr+i] = buf[i];
//          }
          //memory[(buf[1]<<8)+buf[2]] = buf[3];
          //memcpy((void*)&memo[(buf[1]<<8)+buf[2]],(void*)&buf[3], len-3);
          for (int i=0; i < (len-3); i++) {
            memory[((buf[1]<<8)+buf[2])+i] = buf[3+i];
          }
        }
        break;
      case sercmd_run:
#ifdef CPU_EMU
        aqua_play(0);
        //got_reset= true; //
#endif        
        //memory[14340] = 0x10;
        //memory[14341] = 0xe0;
        //cmdstring_pt = &runcmd[0];
        //send_cmdstring = true; 
        break;
      default:
        break;
    }
  }  
  return 0;
}
#endif

#endif

/********************************
 * Network
********************************/ 
#ifdef HAS_NETWORK
static int tftp_handle;

static void* tftp_open(const char* fname, const char* mode, u8_t is_write)
{
  printf("TFTP open: %s\n", fname);
  memory[0x3c00] = 'A';
  LWIP_UNUSED_ARG(mode);


  return (void*)&tftp_handle;
}

static void tftp_close(void* handle)
{
  memory[0x3c01] = 'B';
  printf("TFTP close\n");
}

static int tftp_read(void* handle, void* buf, int bytes)
{
  return 0;
}

static int tftp_write(void* handle, struct pbuf* p)
{

    memory[0x3c02] = 'C';
    while (p != NULL) {
      printf("TFTP write %d\n",p->len);
      //pet_prg_write((uint8_t *)p->payload,p->len);  
      p = p->next;
    }

  return 0;
}

/* For TFTP client only */
static void tftp_error(void* handle, int err, const char* msg, int size)
{
//  char message[100];

  LWIP_UNUSED_ARG(handle);

//  memset(message, 0, sizeof(message));
//  MEMCPY(message, msg, LWIP_MIN(sizeof(message)-1, (size_t)size));

//  printf("TFTP error: %d (%s)", err, message);
}

static const struct tftp_context tftp_ctx = {
  tftp_open,
  tftp_close,
  tftp_read,
  tftp_write,
  tftp_error
};
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
  bool reset_state = !(gpio_get(TRS_RESET));
  if (reset_state) {
    if (!last_reset_state) {
      if (reset_counter < RESET_TRESHOLD) {
        reset_counter++;
#if (defined(CPU_EMU) || defined(CPU_Z80))
#else
        if (hyper_enabled) {
          repeat_cnt = 0;
          cmdstring_pt = &copycmd[0];
          send_cmdstring = true;
          writeFuncTable_hyper[31]=writeMEMTOPF800;
        }      
#endif
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

#define LINE_CYCLES ((CLOCK_MHZ*1000000)/(60*200)) // 260 lines
#define BLANK_CYCLES (LINE_CYCLES*60)

static void __not_in_flash("pio_core") pio_core(void)
{
#ifndef CPU_EMU
  run_pio();
#endif
  while(true) { 
#ifdef CPU_EMU
    for (int i = 8; i < 408; i = i + 2) {
        hdmi_wait_line(i);
        aqua_cycles(LINE_CYCLES);
    }
    aqua_cycles(BLANK_CYCLES);
#endif
    if (got_reset) {
        got_reset = false;
#ifdef CPU_EMU
        aqua_play(0);
#endif
        HyperGfxReset();
#if (defined(CPU_EMU) || defined(CPU_Z80))
        //trs_kb_reset();
        prev_key = 0;
        repeat_cnt = 0;
        if (hyper_enabled) {
          writeFuncTable_hyper[31]=writeMEMTOPF800;
          cmdstring_pt = &initcmd[0];
          send_cmdstring = true;
        }
#endif
    }
    //HdmiHandleAudio(); 
    __dmb();
  }
}

void start_system(void) 
{  
  mem_init();

#ifdef CPU_Z80
    hyper_enabled = false; // JMH
#else
  gpio_init(HYPERGFX_ENA_INPUT);
  gpio_set_dir(HYPERGFX_ENA_INPUT, GPIO_IN);
  gpio_set_pulls(HYPERGFX_ENA_INPUT, true, false);
  sleep_us(50);  
  if ( !gpio_get(HYPERGFX_ENA_INPUT) ) {
    hyper_enabled = true;
  }
#endif

  if (hyper_enabled) {
    readFuncTable=readFuncTable_hyper;
    writeFuncTable=writeFuncTable_hyper;
  }
  else {    
    readFuncTable=readFuncTable_nohyper;
    writeFuncTable=writeFuncTable_nohyper;
  }

#ifdef HAS_NETWORK 
  uint32_t ip = wifi_init();
  if (ip)
  {
    tftp_init_common(LWIP_TFTP_MODE_SERVER, &tftp_ctx);
    while (true) {
        __dmb();    
    }

  }
#endif


#if (defined(CPU_EMU) || defined(CPU_Z80))
  struct repeating_timer timer;
  add_repeating_timer_ms(-1, repeating_timer_callback, NULL, &timer); 
#ifdef HAS_USBHOST
    //board_init();
    // init host stack on configured roothub port
    tuh_init(BOARD_TUH_RHPORT);
#else
    usb_serial_init(&serial_rx);
#endif
  prev_key = 0;
  if (hyper_enabled) {
    repeat_cnt = 0;
    cmdstring_pt = &initcmd[0];
    send_cmdstring = true;
  }   
#else
  if (hyper_enabled) {
    repeat_cnt = 0;
    cmdstring_pt = &copycmd[0];
    send_cmdstring = true;
    writeFuncTable_hyper[31]=writeMEMTOPF800;
  }
#endif


#ifdef CPU_EMU
  aqua_init();
  multicore_launch_core1(pio_core);
#else
  multicore_launch_core1(pio_core);
#ifdef BUS_DEBUG
  memptr = 0;
  memptw = 0;
#endif
#ifdef CPU_Z80
  // release RESET if pico controls Z80!
  sleep_ms(500);
  gpio_put(TRS_RESET, 1);
  sleep_us(50);
//  gpio_set_dir(TRS_RESET, GPIO_IN);
//  gpio_set_pulls(TRS_RESET, true, false);
#endif
#endif

  // Configure RESET as input pin now!
#ifndef CPU_Z80
  gpio_init(TRS_RESET);
#endif
  gpio_set_dir(TRS_RESET, GPIO_IN);
  gpio_set_pulls(TRS_RESET, false, false);

  //trs_screen_setMode(MODE_TEXT_80x24);

  HyperGfxFlashFSInit();
  HyperGfxInit();

  while(true) {
    HyperGfxHandleGfx();    
#ifdef BUS_DEBUG
    DebugShow();    
#endif
    HyperGfxHandleCmdQueue();
    if (got_reset == false) {
      got_reset = poll_reset();
    }

#if (defined(CPU_EMU) || defined(CPU_Z80))
#ifdef HAS_USBHOST
    // tinyusb host task
    tuh_task();
    hid_app_task();
#endif
#else
    repeating_timer_callback(nullptr);    
#endif        
    __dmb();
  }
}

void wait_ms(int ms) {
  sleep_ms(ms);
}