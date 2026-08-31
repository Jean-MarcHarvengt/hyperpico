#ifndef IOPINS_H
#define IOPINS_H

#include "platform_config.h"

#define VGA_DMA_CHANNEL 2 // requires 2 channels
#define AUD_DMA_CHANNEL 4 // requires 1 channel
#define PIO_DMA_CHANNEL 5 // requires 2 channels pio

//#####################################################
// Clock
#define TRS_CLOCK              27  // Z80 clock output on Z80 CPU standalone
#define CONFIG_PIN_BUS_IOOUT   27  // OR IO OUT on TRS80 real system 
#define HYPERGFX_ENA_INPUT     27  // on real TRS80 MODEL III !
#define PET_CLOCK              27  // 6502 clock output on 6502 CPU standalone (PET VOUT on real system = unused)


// Reset
#define TRS_RESET              28
#define PET_RESET              28  //( PET VIN on real system)



// Bus PIO config
#define CONFIG_PIN_BUS_DATA_BASE 0 /* 8 pins */
#define CONFIG_PIN_BUS_CONTROL_BASE (CONFIG_PIN_BUS_DATA_BASE + 9) //DATA,ADDRLO,ADDRHI


// Z80/TRS80
//#define CONFIG_PIN_BUS_RD      8  Z80/TRS80 RD input
//#define CONFIG_PIN_BUS_WR      26 Z80/TRS80 WR input
// 6502/PET
//#define CONFIG_PIN_BUS_RW      8  6502/PET RW input
//#define PET_CLOCK_IN           26 6502/PET PHI2_GPIO input

#define CONFIG_PIN_BUS_DATADIR 22 //(PET INPUT RESET obsolete)



#define CONFIG_PIN_BUS_IOREQ   20 // Z80 IOREQ input (PET UNUSED)
#define CONFIG_PIN_BUS_IOIN    20 // OR IO IN input on TRS80 real system
#define CONFIG_PET_IRQ         20 // OR PET_IRQ output to 6502 CPU standalone



// Speaker
#define AUDIO_PIN       21

// 2 buttons
#define PIN_KEY_USER1   28
#define PIN_KEY_USER2   27 

// HDMI
#define HDMI_D0_PLUS    0 // GPIO12
#define HDMI_D0_MINUS   1 // GPIO13
#define HDMI_CLK_PLUS   2 // GPIO14 
#define HDMI_CLK_MINUS  3 // GPIO15
#define HDMI_D2_PLUS    4 // GPIO16
#define HDMI_D2_MINUS   5 // GPIO17
#define HDMI_D1_PLUS    6 // GPIO18
#define HDMI_D1_MINUS   7 // GPIO19



#endif
