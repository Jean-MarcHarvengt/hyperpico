#include <stdint.h>

#pragma once


#define MEMORY_SIZE 0x10000

#define VIDEO_START 0x3000
#define COLOR_START 0x3400

#define RAM_START   0x3800
#define RAM_MAX     0xFFFF

#define CLOCK_MHZ  4.05504
#define TIMER_HZ   30

typedef unsigned long long tstate_t;

extern uint8_t memory[];

extern void aqua_init(void);
extern void aqua_cycles(unsigned int tstates);
extern void aqua_pauze(void);
extern void aqua_play(unsigned short entryAddr);


void mem_write(unsigned int address, int value);
int mem_read(unsigned int address);
void mem_init();

extern int aqua_process_vkkey(int vk, bool down);
extern int aqua_process_asciikey(int vk, bool down);
extern uint8_t aqua_kb_mem_read(int address);