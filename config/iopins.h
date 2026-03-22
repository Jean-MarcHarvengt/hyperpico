#ifndef IOPINS_H
#define IOPINS_H

#include "platform_config.h"

#define VGA_DMA_CHANNEL 2 // requires 2 channels
#define TFT_DMA_CHANNEL 2 // requires 1 channel
#define AUD_DMA_CHANNEL 4 // requires 1 or 3 channels
#define PIO_DMA_CHANNEL 5 // requires 2 channels pio

//#####################################################
// Clock
#define TRS_CLOCK              27  // Z80 clock output
#define CONFIG_PIN_BUS_IOOUT   27  // OR IO OUT on TRS80 real system (PET VOUT)

// Reset
#define TRS_RESET              28  //( PET VIN)

// Bus PIO config
#define CONFIG_PIN_BUS_DATA_BASE 0 /* 8 pins */
#define CONFIG_PIN_BUS_CONTROL_BASE (CONFIG_PIN_BUS_DATA_BASE + 9) //DATA,ADDRLO,ADDRHI

//#define CONFIG_PIN_BUS_RD      8  Z80/TRS80 RD //(PET INPUT RW) 
//#define CONFIG_PIN_BUS_WR      26 Z80/TRS80 WR //(PET INPUT PHI2)
#define CONFIG_PIN_BUS_DATADIR 22 //(PET INPUT RESET)


#define CONFIG_PIN_BUS_IOREQ   20 // Z80 IOREQ input (PET UNUSED)
#define CONFIG_PIN_BUS_IOIN    20 // OR IO IN input on TRS80 real system

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
