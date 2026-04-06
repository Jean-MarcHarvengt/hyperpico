#ifndef HYPERGFX_H
#define HYPERGFX_H

#include "stdint.h"

//#define NO_HYPER   1

#ifdef PET
extern bool font_lowercase;
#endif
#ifdef TRS
extern bool font_reversed;
#endif

#define VMODE_HIRES    0
#define VMODE_LORES    1
#define VMODE_GAMERES  2

extern void HyperGfxInit(void);
extern void HyperGfxFlashFSInit(void);
extern void HyperGfxReset(void);

extern void HyperGfxHandleGfx(void);
extern void HyperGfxHandleCmdQueue(void);

extern void HyperGfxWrite(uint16_t address, uint8_t value);
extern uint8_t HyperGfxRead(uint16_t address);

extern void HyperGfxVsync(void);
extern bool HyperGfxIsHires(void);
extern bool HyperGfxIsPal(void);


#endif
