cmake_minimum_required(VERSION 3.12)

# Pull in PICO SDK (must be before project)
include(pico_sdk_import.cmake)

# We also need PICO EXTRAS
#include(pico_extras_import.cmake)

set(TARGET hypertrspico)
#set(TARGET hyperpetpico)

project(${TARGET} C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

include_directories(config)
include_directories(display)
include_directories(gfx)
include_directories(flashfs)
include_directories(flashfs/fatfs)
include_directories(decomp)
include_directories(resid)
include_directories(usb_serial)
include_directories(usb_kbd)
if( ${TARGET} MATCHES "hypertrspico" )
include_directories(trs80)
endif()
if( ${TARGET} MATCHES "hyperpetpico" )
include_directories(pet)
endif()
include_directories(.)

set(DISPLAY_SOURCES 	
		display/hdmi_framebuffer.cpp	
	)

set(GFXRENDER_SOURCES
		gfx/gfx_tile8.S
		gfx/gfx_tile16.S
		gfx/gfx_text8.S
		gfx/gfx_sprite16.S
		gfx/gfx_color.S
		gfx/hypergfx.cpp
	)

set(DECOMP_SOURCES 	
		decomp/decrunch.c
	)

set(SID_SOURCES 
		resid/sid.cpp
		resid/reSID.cpp	
	)

set(FF_SOURCES
		flashfs/fatfs_disk_ro.c
		flashfs/fatfs/ff.c	
		flashfs/fatfs/diskio.c
		flashfs/fatfs/ffunicode.c
	)

set(USBSERIAL_SOURCES
		usb_serial/dev_lowlevel.c
	)

set(USBKBD_SOURCES
		usb_kbd/hid_app.c
	)


if( ${TARGET} MATCHES "hyperpetpico" )
set(MAIN_SOURCES
		main.cpp
		pet/mos6502.cpp
		pet/memmap.cpp
	)
endif()

if( ${TARGET} MATCHES "hypertrspico" )
set(MAIN_SOURCES
		main.cpp
		trs80/z80.cpp
		trs80/trs_memory.cpp
		trs80/trs.cpp
		trs80/io.cpp
		trs80/memmap.cpp
		trs80/pioirq.S
	)
endif()


add_executable(${TARGET}
		${DISPLAY_SOURCES}
		${GFXRENDER_SOURCES}
		${MAIN_SOURCES}
		${DECOMP_SOURCES}
		${SID_SOURCES}		
		${FF_SOURCES}
		${USBSERIAL_SOURCES}
		${USBKBD_SOURCES}		
	)


target_link_libraries(${TARGET} pico_multicore
	    pico_stdlib
	    hardware_dma
	    hardware_spi
	    hardware_adc
	    hardware_pio
        hardware_pwm	    
        hardware_flash
        tinyusb_host 
        tinyusb_board
		tinyusb_pico_pio_usb
	)



target_compile_definitions(${TARGET} PRIVATE
        PIO_USB_DP_PIN_DEFAULT=2
		PICO_DEFAULT_PIO_USB_DP_PIN=2
    )

if( ${TARGET} MATCHES "hypertrspico" )
pico_generate_pio_header(${TARGET} ${CMAKE_CURRENT_LIST_DIR}/trs80/clock.pio)
pico_generate_pio_header(${TARGET} ${CMAKE_CURRENT_LIST_DIR}/trs80/busreadwrite.pio)
endif()

if( ${TARGET} MATCHES "hyperpetpico" )
pico_generate_pio_header(${TARGET} ${CMAKE_CURRENT_LIST_DIR}/pet/petbus.pio)
endif()

if( ${TARGET} MATCHES "hypertrspico" )
target_compile_options(${TARGET} PUBLIC -DTRS)
endif()
if( ${TARGET} MATCHES "hyperpetpico" )
target_compile_options(${TARGET} PUBLIC -DPET)
endif()


target_compile_definitions(${TARGET} PRIVATE PICO_CLOCK_AJDUST_PERI_CLOCK_WITH_SYS_CLOCK=1)

#target_compile_definitions(${TARGET} PRIVATE CFG_TUSB_DEBUG=2)
#target_compile_options(${TARGET} PUBLIC -O2)
#target_compile_options(${TARGET} PUBLIC -Wall -Wextra -Wno-unused-function -Wno-unused-parameter)
IF (NOT DEFINED N_SD_CARDS)
    SET(N_SD_CARDS 1)
ENDIF()
target_compile_definitions(${TARGET} PUBLIC N_SD_CARDS=${N_SD_CARDS})


pico_enable_stdio_uart(${TARGET} FALSE)
#pico_enable_stdio_usb(${TARGET} TRUE)

pico_add_extra_outputs(${TARGET})
