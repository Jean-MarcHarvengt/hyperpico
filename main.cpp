#include "pico.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"

#include "hdmi_framebuffer.h"
#include "memmap.h"


int main(void) {
//    vreg_set_voltage(VREG_VOLTAGE_1_05);
    set_sys_clock_khz(280000, true);
//    set_sys_clock_khz(300000, true);
    *((uint32_t *)(0x40010000+0x58)) = 2 << 16; //CLK_HSTX_DIV = 2 << 16; // HSTX clock/2

    stdio_init_all();
    hdmi_init(MODE_VGA_640x240);

    start_system();   
    // will never return!!
}
