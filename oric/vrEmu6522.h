/*
 * Troy's 6522 Emulator
 *
 * Copyright (c) 2023 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/vrEmu6522
 *
 */

#ifndef _VR_EMU_6522_H_
#define _VR_EMU_6522_H_

/* ------------------------------------------------------------------
 * LINKAGE MODES:
 * 
 * Default (nothing defined):    When your executable is using vrEmuTms9918 as a DLL
 * VR_6522_EMU_COMPILING_DLL:    When compiling vrEmuTms9918 as a DLL
 * VR_6522_EMU_STATIC:           When linking vrEmu6522 statically in your executable
 */
 

#ifdef __cplusplus
  #define VR_EMU_6522_DLLEXPORT extern "C"
#else
  #define VR_EMU_6522_DLLEXPORT extern
#endif



#include <stdint.h>
#include <stdbool.h>  

/* ------------------------------------------------------------------
 * PRIVATE DATA STRUCTURE
 */
struct vrEmu6522_s;
typedef struct vrEmu6522_s VrEmu6522;

/* ------------------------------------------------------------------
 * CONSTANTS
 */
typedef enum
{
  VIA_6522,     /* NMOS 6522 */
  VIA_65C22,    /* Standard CMOS 65C22 */
} vrEmu6522Model;


/*
 * create a new 6522
 */
VR_EMU_6522_DLLEXPORT VrEmu6522* vrEmu6522New(vrEmu6522Model model);

/*
 * destroy a 6522
 */
VR_EMU_6522_DLLEXPORT void vrEmu6522Destroy(VrEmu6522* vr6522);

/*
 * reset the 6522
 */
VR_EMU_6522_DLLEXPORT void vrEmu6522Reset(VrEmu6522* vr6522);

/*
 * write to the via
 */
//VR_EMU_6522_DLLEXPORT void vrEmu6522Write(VrEmu6522* vr6522, uint8_t addr, uint8_t data);

/*
 * read from the via
 */
//VR_EMU_6522_DLLEXPORT uint8_t vrEmu6522Read(VrEmu6522* vr6522, uint8_t addr);



/*
 * multiple clock ticks
 */
VR_EMU_6522_DLLEXPORT void vrEmu6522Ticks(VrEmu6522* vr6522, int ticks);

VR_EMU_6522_DLLEXPORT bool vrEmu6522Int(VrEmu6522* vr6522);

VR_EMU_6522_DLLEXPORT void vrEmu6522PressKeyMtx(VrEmu6522* vr6522,uint8_t line, uint8_t bcol);

VR_EMU_6522_DLLEXPORT void vrEmu6522ReleaseKeyMtx(VrEmu6522* vr6522,uint8_t line, uint8_t bcol);

VR_EMU_6522_DLLEXPORT void vrEmu6522Joystick(VrEmu6522* vr6522, uint8_t value);

VR_EMU_6522_DLLEXPORT uint8_t (*ioreadFuncTable[16])(VrEmu6522* vr6522, uint8_t addr);

VR_EMU_6522_DLLEXPORT void (*iowriteFuncTable[16])(VrEmu6522* vr6522, uint8_t addr, uint8_t data);

#endif // _VR_EMU_6522__H_
