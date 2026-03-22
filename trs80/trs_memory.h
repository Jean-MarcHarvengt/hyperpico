
#pragma once

#include "trs.h"

#ifdef TRS_MODEL4
void mem_video_page(int which);
void mem_bank(int command);
void mem_map(int which);
#endif

void mem_write(unsigned int address, int value);
int mem_read(unsigned int address);
void mem_init();