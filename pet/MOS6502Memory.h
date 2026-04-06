#ifndef _H_MOS6502MEMORY
#define _H_MOS6502MEMORY

#include <stdint.h>

extern  uint8_t readWord( uint16_t location);
extern  void writeWord( uint16_t location, uint8_t value);


#endif