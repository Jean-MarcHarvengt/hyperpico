
#ifndef __TRS_H__
#define __TRS_H__

#include <stdint.h>

typedef unsigned long long tstate_t;

// Model III/4 specs
#define TIMER_HZ_M3 30
#define TIMER_HZ_M4 60
#define CLOCK_MHZ_M3 2.02752
#define CLOCK_MHZ_M4 4.05504


//#define TRS_MODEL4 1

#ifdef TRS_MODEL4
#define MEM_SIZE (2 * 64738)
#else
#define MEM_SIZE (64738)
#endif

extern unsigned char memory[MEM_SIZE];;


#ifdef TRS_MODEL4
#define MAX_VIDEO_SIZE  (0x0800)
#else
#define MAX_VIDEO_SIZE  (0x0400)
#endif

//#define SUPERMEM 1

/* 512K is the largest we support. There were it seems 1MByte
   options at some point which is the full range of the mapping.
   How the mapping register worked for > 1MB is not known */
#define MAX_SUPERMEM_SIZE   (0x80000)

/* Locations for Model I, Model III, and Model 4 map 0 */
#define ROM_START       (0x0000)
#define MAX_ROM_SIZE    (0x3800)

#define PRINTER_ADDRESS (0x37E8)
#define KEYBOARD_START  (0x3800)
#define VIDEO_START     (0x3c00)
#define RAM_START       (0x4000)

#define IO_START        (0x3000)
#define MORE_IO_START   (0x3c00)


enum rom_type_t {
    ROM_FREHD = 0,
    ROM_XROM,
    ROM_TEST1,
    ROM_TEST2,
    ROM_TEST3,    
};

#define getROMType() ROM_FREHD //ROM_XROM
//#define getROMType() ROM_TEST2


extern void trs_timer_speed(int fast);
extern void trs_init(void);
extern void trs_step(void);
extern void trs_go(void);
extern void trs_pauze(void);
extern void trs_play(unsigned short entryAddr);

extern int trs_process_vkkey(int vk, bool down);
extern int trs_process_asciikey(int vk, bool down);

#endif
