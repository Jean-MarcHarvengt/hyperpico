
#include "trs_screen.h"
#include "trs_memory.h"
#include "grafyx.h"

#include "trs.h"
typedef unsigned long long tstate_t;

static uint8_t modeimage = 8;
static uint8_t port_0xec = 0xff;
static uint8_t last_c8 = 0x80;
static int ctrlimage = 0;


void z80_out(uint8_t address, uint8_t data, tstate_t z80_state_t_count)
{
#ifdef TRS_MODEL4
  int changes; 
#endif  
  switch(address) {
    case 0x80:
      grafyx_write_x(data);
      break;
    case 0x81:
      grafyx_write_y(data);
      break;
    case 0x82:
      grafyx_write_data(data);
      break;
    case 0x83:
      grafyx_write_mode(data);
      break;
    case 0x84:
    case 0x85:
    case 0x86:
    case 0x87:
#ifdef TRS_MODEL4      
      changes = data ^ ctrlimage;
      if (changes & 0x80) {
        mem_video_page((data & 0x80) >> 7);
        printf("mem_video_page: %d\n", (data & 0x80) >> 7);
      }
      if (changes & 0x70) {
        mem_bank((data & 0x70) >> 4);
        printf("mem_bank: %d\n", (data & 0x70) >> 4);
    	}
      if (changes & 0x08) {
        trs_screen_setInverse((data & 0x08) >> 3);
      }
      if (changes & 0x04) {
        //printf("Switch mode: %d\n", data & 4);
        trs_screen_setMode((data & 0x04) ? MODE_TEXT_80x24 : MODE_TEXT_64x16);
    	}
      if (changes & 0x03) {
        mem_map(data & 0x03);
        //printf("mem_map: %d\n", (data & 0x3));
      }
      ctrlimage = data;
#endif
      break;
    case 0xEC:
      port_0xec = data;
      break;
      // Fall through
    case 0xED:
    case 0xEE:
    case 0xEF:
      modeimage = data;
      //trs_cassette_motor((modeimage & 0x02) >> 1, z80_state_t_count);
      trs_screen_setExpanded((data & 0x04) >> 2);
#ifdef TRS_MODEL4
      trs_timer_speed((modeimage & 0x40) >> 6);
#endif
      break;
    case 0xF8:
    case 0xF9:
    case 0xFA:
    case 0xFB:
      //trs_printer_write(data);
      break;
    case 0xff:
      //trs_cassette_out(data & 3, z80_state_t_count);
      break;
    default:
      break;
  }

}

uint8_t z80_in(uint8_t address, tstate_t z80_state_t_count)
{
  switch(address) {
    case 0:
      // No joystick
      return 0xff;
    case 0x82:
      return grafyx_read_data();
    case 0xec:
      return port_0xec;
    case 0xe0:
      // This will signal that a RTC INT happened. See ROM address 0x35D8
      return ~4;
    default:
      return 0xff;

    case 0xF0:
      // For the XROM, we fake an empty disk. For other ROMs we report no floppy present
      return (getROMType() == ROM_XROM) ? 0x34 : 0xff;
    case 0xF8:
    case 0xF9:
    case 0xFA:
    //case 0xFB:
    //  return trs_printer_read();
    case 0xff:
      return (modeimage & 0x7e) /*| trs_cassette_in(z80_state_t_count)*/;
  }
  if ((port_0xec & (1 << 4)) == 0) {
    // I/O disabled
    return 0xff;
  }

  // Route interaction to external I/O bus
  if((address == 0xcf) && !(last_c8 & 0x80)) {
    z80_out(0xc8, last_c8 | 0x80, z80_state_t_count);
  }
  last_c8 |= 0x80;

  return 0;
}


