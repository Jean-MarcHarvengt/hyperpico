#ifndef HDMIFRAMEBUFFER_H
#define HDMIFRAMEBUFFER_H

#include "stdint.h"
#include <stdio.h>

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
    size_t framebuffer_len; // in words
    uint32_t *dma_commands;
    size_t dma_commands_len; // in words
    uint16_t width;
    uint16_t height;
    uint16_t pitch; // Number of words between rows. (May be more than a width's worth.)
    uint8_t color_depth;
    uint8_t dma_pixel_channel;
    uint8_t dma_command_channel;
} hdmi_framebuffer_obj_t;

extern void hdmi_framebuffer(hdmi_framebuffer_obj_t *self,
    uint16_t width, uint16_t height,
    uint16_t color_depth);
extern void hdmi_framebuffer_vsync(void);
extern int hdmi_framebuffer_line(void);

#endif