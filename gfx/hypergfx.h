#ifndef HYPERGFX_H
#define HYPERGFX_H

#include "stdint.h"
#include "pico_dsp.h"

// Moved to CMakeLists.txt
//#define PET   1
//#define TRS   1

//#define NO_HYPER   1

extern PICO_DSP tft;
#ifdef PET
extern bool font_lowercase;
#endif
#ifdef TRS
extern bool font_reversed;
#endif


extern void HyperGfxInit(void);
extern void HyperGfxFlashFSInit(void);
extern void HyperGfxHandleGfx(void);
extern void HyperGfxHandleCmdQueue(void);

extern void HyperGfxWrite(uint32_t address, uint8_t value);

#endif
