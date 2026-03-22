#include <string.h>

#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

#include "memmap.h" // contains config !!!

extern "C" {
  #include "iopins.h"   
}

#ifdef CPU_Z80
#include "clock.pio.h"
#endif
#include "busreadwrite.pio.h"

#include "hypergfx.h"

#include "trs_memory.h"
#include "trs_screen.h"



#define PIO_DIV 1.0f

static PIO bus_pio;
static uint bus_smr;
static uint bus_smw;

#ifdef CPU_Z80
static PIO clock_pio;
static uint clock_sm;
#endif

#define ADDBUS_WIDTH 16
#define ADDBUS_MASK  (MEM_SIZE-1) 

uint8_t memory[MEM_SIZE]; //__attribute__ ((aligned (MEM_SIZE)));
static uint32_t armaddr = ((uint32_t)memory);

static bool got_reset=false;

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
// TRS Memory
// ****************************************
#ifdef CPU_EMU  
static uint8_t __not_in_flash("readNone") readNone(uint32_t address) {
  return (0xff);
#else
static void __not_in_flash("readNone") readNone(uint32_t address) {
  bus_pio->txf[bus_smr] = 0;
#endif  
}

#ifdef CPU_EMU  
static uint8_t __not_in_flash("readMEM") readMEM(uint32_t address) {
  if (address == 0) got_reset=true;
  return (memory[address]);
#else
static void __not_in_flash("readMEM") readMEM(uint32_t address) {
  if (address == 0) got_reset=true;
  bus_pio->txf[bus_smr] = 0x100 | memory[address];  
#endif  
}

#include "trs_keyboard.h"
#ifdef CPU_EMU  
static uint8_t __not_in_flash("readVKMEM") readVKMEM(uint32_t address) {
  if (address >= VIDEO_START )
    return (memory[address]);  
  else
    return trs_kb_mem_read(address);
#else
static void __not_in_flash("readVKMEM") readVKMEM(uint32_t address) {
  if (address >= VIDEO_START )
    bus_pio->txf[bus_smr] = 0x100 | memory[address];  
  else
    bus_pio->txf[bus_smr] = 0x100 | trs_kb_mem_read(address);
#endif  
}

static void __not_in_flash("writeNone") writeNone(uint32_t address, uint8_t value) {
}

static void __not_in_flash("writeMEM") writeMEM(uint32_t address, uint8_t value) {
  memory[address]=value;
}

static void __not_in_flash("writeMEMTOP") writeMEMTOP(uint32_t address, uint8_t value) {
  if (address >= 0xe701) // top of memory, begining File Browser rom
    memory[address]=value;
}


#ifdef CPU_EMU  
uint8_t __not_in_flash("readFuncTable") (*readFuncTable[32])(uint32_t)
#else
void __not_in_flash("readFuncTable") (*readFuncTable[32])(uint32_t)
#endif
{
#if (defined(CPU_EMU) || defined(CPU_Z80))
  readMEM,   // 00
  readMEM,   // 08
  readMEM,   // 10
  readMEM,   // 18
  readMEM,   // 20
  readMEM,   // 28
  readMEM,   // 30
  readVKMEM, // 38
#else
  readNone,  // 00
  readNone,  // 08
  readNone,  // 10
  readNone,  // 18
  readNone,  // 20
  readNone,  // 28
  readNone,  // 30  
  readNone,  // 38
#endif
#if (defined(TRS_4K) || defined(TRS_16K) || defined(TRS_48K))
  readNone,  // 40
  readNone,  // 48
#else
  readMEM,   // 40
  readMEM,   // 48
#endif
#if (defined(TRS_16K) || defined(TRS_48K))
  readNone,  // 50
  readNone,  // 58
  readNone,  // 60
  readNone,  // 68
  readNone,  // 70
  readNone,  // 78
#else
  readMEM,   // 50
  readMEM,   // 58
  readMEM,   // 60
  readMEM,   // 68
  readMEM,   // 70
  readMEM,   // 78
#endif
#if (defined(TRS_48K))
  readNone,   // 80
  readNone,   // 88
  readNone,   // 90
  readNone,   // 98
  readNone,   // a0
  readNone,   // a8
  readNone,   // b0
  readNone,   // b8
  readNone,   // c0
  readNone,   // c8
  readNone,   // d0
  readNone,   // d8
  readNone,   // e0
  readNone,   // e8
  readNone,   // f0
  readNone,   // f8
#else
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
  readMEM,   // f8
#endif
};

void __not_in_flash("writeFuncTable") (*writeFuncTable[32])(uint32_t,uint8_t)
{
  writeNone, // 00
  writeNone, // 08
  writeNone, // 10
  writeNone, // 18
  writeNone, // 20
  writeNone, // 28
  writeNone, // 30
  writeMEM,  // 38 // keyboard + video
  writeMEM,  // 40
  writeMEM,  // 48
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
  writeMEMTOP,    // e0
  HyperGfxWrite,  // e8
  HyperGfxWrite,  // f0
  HyperGfxWrite,  // f8
};


static void __not_in_flash("blink") blink(void)
//void __not_in_flash_func(blink)()
{
    asm(R"(
      //push    {r1}
      ldr     r0, =memory+0x3cff
      ldrb    r1, [r0]
      adds    r1, r1,#1
      strb    r1, [r0]
      //pop     {r1}
    )");
}


//extern "C" int32_t getkeyb(uint32_t add);

extern "C" void pioirq_asmr(void);

void __not_in_flash("__time_critical_func") pioirq_smr(void) {
  if(!pio_sm_is_rx_fifo_empty(bus_pio, bus_smr)) {
      uint16_t add = pio_sm_get(bus_pio, bus_smr);       
      /*
      if (gpio_get(CONFIG_PIN_BUS_IOREQ) ) {
        if ( (add >= 0x3800) && (add < 0x3900) )
          bus_pio->txf[bus_smr] = trs_kb_mem_read(add);
        else
          bus_pio->txf[bus_smr] = memory[add];
      }
      else
        bus_pio->txf[bus_smr] = 0xff;
      */
      
#if (defined(CPU_EMU) || defined(CPU_Z80))
      if (gpio_get(CONFIG_PIN_BUS_IOREQ) )
#endif
        readFuncTable[add>>11](add);
#if (defined(CPU_EMU) || defined(CPU_Z80))
      else
        bus_pio->txf[bus_smr] = 0xff;
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
      
      //if (add >= MAX_ROM_SIZE) memory[add]=value & 0xff;
      
#ifdef CPU_Z80
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
#ifdef CPU_Z80   
    //pio_gpio_init(bus_pio, CONFIG_PIN_BUS_IOREQ);
    //gpio_set_pulls(CONFIG_PIN_BUS_IOREQ, false, false);
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
    gpio_init(TRS_RESET);
    gpio_set_dir(TRS_RESET, GPIO_IN);
    gpio_set_pulls(TRS_RESET, false, false);
    gpio_init(CONFIG_PIN_BUS_IOOUT);
    gpio_set_dir(CONFIG_PIN_BUS_IOOUT, GPIO_IN);
    gpio_set_pulls(CONFIG_PIN_BUS_IOOUT, false, false);
    gpio_init(CONFIG_PIN_BUS_IOIN);
    gpio_set_dir(CONFIG_PIN_BUS_IOIN, GPIO_IN);
    gpio_set_pulls(CONFIG_PIN_BUS_IOIN, false, false);
    pio_enable_sm_mask_in_sync(bus_pio, (1 << bus_smr) | (1 << bus_smw));
#endif
}

extern void SystemReset(void);

static void __not_in_flash("pio_core") pio_core(void)
{
#ifndef CPU_EMU
  run_pio();
#endif
  while(true) { 
#ifdef CPU_EMU
    trs_step();
#endif 
    if (got_reset) {
        got_reset = false;
        SystemReset();
    }
    //HdmiHandleAudio(); 
    __dmb();
  }
}

/*
.ORG   $e001   

start:    
LD   hl,scr   
LD   (hl),$48   
INC   hl   
LD   (hl),$45   
INC   hl   
LD   (hl),$4C   
INC   hl   
LD   (hl),$4C   
INC   hl   
LD   (hl),$4f   
INC   hl   
JP   start   
.ORG   $3C00   
SCR:      
*/


// system (not working)
// /57345 or -8191
//
// poke (working)
//POKE 16526,1
//POKE 16527,224
//a=usr(0)
const unsigned char browser_rom[] = {   
0x00,0x21,0x00,0x3C,0x36,0x48,0x23,0x36,0x45,0x23,0x36,0x4C,
0x23,0x36,0x4C,0x23,0x36,0x4F,0x23,0xC3,0x01,0xE0
};            


void start_system(void) {
  HyperGfxFlashFSInit();  
  //trs_screen_setMode(MODE_TEXT_80x24);
  trs_screen_setMode(MODE_TEXT_64x16);
#if (defined(CPU_EMU) || defined(CPU_Z80))  
  mem_init();
#endif  

  HyperGfxInit();

  //memcpy((void*)&memory[0xe000], (void*)browser_rom, sizeof(browser_rom));

#ifdef CPU_EMU
  trs_init();
  multicore_launch_core1(pio_core);
#else
  multicore_launch_core1(pio_core);

#ifdef BUS_DEBUG
  memptr = 0;
  memptw = 0;
#endif
#ifdef CPU_Z80
  sleep_ms(500);
  gpio_put(TRS_RESET, 1);
#endif

#endif

  while(true) {
    HyperGfxHandleGfx();    
#ifdef BUS_DEBUG
    DebugShow();    
#endif
    HyperGfxHandleCmdQueue();    
    __dmb();        
  }
}
