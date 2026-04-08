#ifndef HDMIFRAMEBUFFER_H
#define HDMIFRAMEBUFFER_H

#include "stdint.h"
#include <stdio.h>

#include "platform_config.h"

#define VGA_RGB(r,g,b)   ( (((r>>5)&0x07)<<5) | (((g>>5)&0x07)<<2) | (((b>>6)&0x3)<<0) )
#define R16(rgb) ((rgb>>8)&0xf8) 
#define G16(rgb) ((rgb>>3)&0xfc) 
#define B16(rgb) ((rgb<<3)&0xf8) 

#define MODE_H_SYNC_POLARITY 0
#define MODE_H_FRONT_PORCH   16
#define MODE_H_SYNC_WIDTH    96
#define MODE_H_BACK_PORCH    48
#define MODE_H_ACTIVE_PIXELS 640

#define MODE_V_SYNC_POLARITY 0
#define MODE_V_FRONT_PORCH   10
#define MODE_V_SYNC_WIDTH    2
#define MODE_V_BACK_PORCH    33
#define MODE_V_ACTIVE_LINES  480

#define MODE_H_TOTAL_PIXELS ( \
    MODE_H_FRONT_PORCH + MODE_H_SYNC_WIDTH + \
    MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS \
    )
#define MODE_V_TOTAL_LINES  ( \
    MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + \
    MODE_V_BACK_PORCH + MODE_V_ACTIVE_LINES \
    )


typedef struct {
    uint32_t *framebuffer;
    uint32_t *dma_commands;
    uint16_t dma_command_size; // per line in word (uint32_t)
    uint16_t width;
    uint16_t height;
    uint16_t pitch; // Number of words between rows. (May be more than a width's worth.)
    uint8_t dma_pixel_channel;
    uint8_t dma_command_channel;
} hdmi_framebuffer_obj_t;


typedef enum gfx_mode_t
{
  MODE_UNDEFINED   = 0,
  MODE_VGA_320x240 = 1,
  MODE_VGA_256x240 = 2,
  MODE_VGA_640x240 = 3
} gfx_mode_t;

extern void hdmi_init(gfx_mode_t mode);
extern uint8_t * hdmi_get_line_buffer(int j);
extern void hdmi_fill_screen(uint8_t color); 
extern int hdmi_raster_line(void);
extern void hdmi_vsync(void);
extern void hdmi_wait_line(int line);

#ifdef HAS_SND

//#define SOUNDRATE 22050                           // sound rate [Hz]
#define SOUNDRATE 44100                           // sound rate [Hz]
typedef short  audio_sample;


extern void audio_init(int samplesize, void (*callback)(audio_sample * stream, int len));
extern void audio_handle(void);
extern void * audio_get_buffer();
#endif


#endif