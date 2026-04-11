/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Scott Shawcroft for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "hdmi_framebuffer.h"

#include <stdio.h>
#include <string.h>
#include <cstdlib>

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/pwm.h"
#include "pico/float.h"
#include "pico/stdlib.h"

#include "iopins.h"


#define MAX_FB_WIDTH   640
#define MAX_FB_HEIGHT  240

// ----------------------------------------------------------------------------
// DVI constants

#define TMDS_CTRL_00 0x354u
#define TMDS_CTRL_01 0x0abu
#define TMDS_CTRL_10 0x154u
#define TMDS_CTRL_11 0x2abu

#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))


#define HSTX_CMD_RAW         (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT  (0x1u << 12)
#define HSTX_CMD_TMDS        (0x2u << 12)
#define HSTX_CMD_TMDS_REPEAT (0x3u << 12)
#define HSTX_CMD_NOP         (0xfu << 12)

// ----------------------------------------------------------------------------
// HSTX command lists

static uint32_t vblank_line_vsync_off[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V1_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
    SYNC_V1_H1
};

static uint32_t vblank_line_vsync_on[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V0_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V0_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
    SYNC_V0_H1
};

static uint32_t vactive_line[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V1_H0,
    HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_BACK_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_TMDS | MODE_H_ACTIVE_PIXELS
};



static hdmi_framebuffer_obj_t hdmi_obj;
static gfx_mode_t gfxmode = MODE_UNDEFINED;
static int fb_width;
static int fb_height;
static __attribute__((aligned(32))) uint32_t dma_commands[(MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH + 2 * MODE_V_ACTIVE_LINES + 1) * 4]; 
static __attribute__((aligned(32))) uint8_t framebuffer[MAX_FB_WIDTH*MAX_FB_HEIGHT];
static bool vsync = true;
static hdmi_framebuffer_obj_t *active_picodvi = NULL;



#define DMA_CTRL  ( (VGA_DMA_CHANNEL+1) << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB | \
        DREQ_HSTX << DMA_CH0_CTRL_TRIG_TREQ_SEL_LSB | \
        DMA_CH0_CTRL_TRIG_IRQ_QUIET_BITS | \
        DMA_CH0_CTRL_TRIG_INCR_READ_BITS | \
        DMA_CH0_CTRL_TRIG_EN_BITS | DMA_SIZE_32 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB)


#define DMA_PIXEL_CTRL ( (VGA_DMA_CHANNEL+1) << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB | \
        DREQ_HSTX << DMA_CH0_CTRL_TRIG_TREQ_SEL_LSB | \
        DMA_CH0_CTRL_TRIG_IRQ_QUIET_BITS | \
        DMA_CH0_CTRL_TRIG_INCR_READ_BITS | \
        DMA_CH0_CTRL_TRIG_EN_BITS | DMA_SIZE_8 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB)


static void set_hdmi_framebuffer(hdmi_framebuffer_obj_t *self, uint16_t width, uint16_t height) {
    bool half_width = (width == 320);

    irq_set_enabled(DMA_IRQ_0, false);
    dma_channel_abort(VGA_DMA_CHANNEL);
    dma_channel_abort(VGA_DMA_CHANNEL+1);
    sleep_ms(30);
    memset((void*)&framebuffer[0],0, MAX_FB_WIDTH*MAX_FB_HEIGHT);

    self->width = width;
    self->height = height;
    self->pitch = self->width / sizeof(uint32_t);
    if (half_width) self->pitch=self->pitch*2; 

    // We compute all DMA transfers needed for a single frame. This ensure we don't have any super
    // quick interrupts that we need to respond to. Each transfer takes two words, trans_count and
    // read_addr. Active pixel lines need two transfers due to different read addresses. When pixel
    // doubling, then we must also set transfer size.
    self->dma_command_size = (half_width?4:2)*sizeof(uint32_t)*2;
    self->dma_pixel_channel = VGA_DMA_CHANNEL; 
    self->dma_command_channel = VGA_DMA_CHANNEL+1;

    size_t command_word = 0;
    size_t frontporch_start = MODE_V_TOTAL_LINES - MODE_V_FRONT_PORCH;
    size_t frontporch_end = frontporch_start + MODE_V_FRONT_PORCH;
    size_t vsync_start = 0;
    size_t vsync_end = vsync_start + MODE_V_SYNC_WIDTH;
    size_t backporch_start = vsync_end;
    size_t backporch_end = backporch_start + MODE_V_BACK_PORCH;
    size_t active_start = backporch_end;


    uint32_t dma_write_addr = (uint32_t)&hstx_fifo_hw->fifo;
    // Write ctrl and write_addr once when not pixel doubling because they don't
    // change. (write_addr doesn't change when pixel doubling either but we need
    // to rewrite it because it is after the ctrl register.)
    if (!half_width) {
        dma_channel_hw_addr(self->dma_pixel_channel)->al1_ctrl = DMA_CTRL;
        dma_channel_hw_addr(self->dma_pixel_channel)->al1_write_addr = dma_write_addr;
    }
    for (size_t v_scanline = 0; v_scanline < MODE_V_TOTAL_LINES; v_scanline++) {
        if (half_width) {
            self->dma_commands[command_word++] = DMA_CTRL;
            self->dma_commands[command_word++] = dma_write_addr;
        }
        if (vsync_start <= v_scanline && v_scanline < vsync_end) {
            self->dma_commands[command_word++] = count_of(vblank_line_vsync_on);
            self->dma_commands[command_word++] = (uintptr_t)vblank_line_vsync_on;
        } else if (backporch_start <= v_scanline && v_scanline < backporch_end) {
            self->dma_commands[command_word++] = count_of(vblank_line_vsync_off);
            self->dma_commands[command_word++] = (uintptr_t)vblank_line_vsync_off;
        } else if (frontporch_start <= v_scanline && v_scanline < frontporch_end) {
            self->dma_commands[command_word++] = count_of(vblank_line_vsync_off);
            self->dma_commands[command_word++] = (uintptr_t)vblank_line_vsync_off;
        } else {
            self->dma_commands[command_word++] = count_of(vactive_line);
            self->dma_commands[command_word++] = (uintptr_t)vactive_line;
            size_t row = (v_scanline - active_start)/2;
            size_t transfer_count = self->pitch;
            if (half_width) {
                self->dma_commands[command_word++] = DMA_PIXEL_CTRL;
                self->dma_commands[command_word++] = dma_write_addr;
                // When pixel doubling, we do one transfer per pixel and it gets
                // mirrored into the rest of the word.
                transfer_count = self->width;
            }
            self->dma_commands[command_word++] = transfer_count;
            uint32_t *row_start = &self->framebuffer[row * self->pitch];
            self->dma_commands[command_word++] = (uintptr_t)row_start;
        }
    }
    // Last command is NULL which will trigger an IRQ.
    if (half_width) {
        self->dma_commands[command_word++] = DMA_CH0_CTRL_TRIG_IRQ_QUIET_BITS |
            DMA_CH0_CTRL_TRIG_EN_BITS;
        self->dma_commands[command_word++] = 0;
    }
    self->dma_commands[command_word++] = 0;
    self->dma_commands[command_word++] = 0;

    // Configure HSTX's TMDS encoder for RGB332
    hstx_ctrl_hw->expand_tmds =
        2 << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
            0 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB |
            2 << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
            29 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB |
            1 << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
            26 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;
    size_t shifts_before_empty = half_width?2:4; //((32 / 8) % 32);
    // Pixels come in 32 bits at a time. color_depth dictates the number
    // of pixels per word. Control symbols (RAW) are an entire 32-bit word.
    hstx_ctrl_hw->expand_shift =
        shifts_before_empty << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
            8 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
            1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
            0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;

    // Serial output config: clock period of 5 cycles, pop from command
    // expander every 5 cycles, shift the output shiftreg by 2 every cycle.
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EXPAND_EN_BITS |
        5u << HSTX_CTRL_CSR_CLKDIV_LSB |
            5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
            2u << HSTX_CTRL_CSR_SHIFT_LSB |
            HSTX_CTRL_CSR_EN_BITS;

    // Note we are leaving the HSTX clock at the SDK default of 125 MHz; since
    // we shift out two bits per HSTX clock cycle, this gives us an output of
    // 250 Mbps, which is very close to the bit clock for 480p 60Hz (252 MHz).
    // If we want the exact rate then we'll have to reconfigure PLLs.

    // Assign clock pair to two neighbouring pins:
    hstx_ctrl_hw->bit[HDMI_CLK_PLUS]  = HSTX_CTRL_BIT0_CLK_BITS;
    hstx_ctrl_hw->bit[HDMI_CLK_MINUS] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    uint32_t lane_data_sel_bits;
    // lane 0
    lane_data_sel_bits = (0 * 10    ) << HSTX_CTRL_BIT0_SEL_P_LSB |
                         (0 * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;    
    hstx_ctrl_hw->bit[HDMI_D0_PLUS]  = lane_data_sel_bits;
    hstx_ctrl_hw->bit[HDMI_D0_MINUS] = lane_data_sel_bits | HSTX_CTRL_BIT0_INV_BITS;
    // lane 1
    lane_data_sel_bits = (1 * 10    ) << HSTX_CTRL_BIT0_SEL_P_LSB |
                         (1 * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;    
    hstx_ctrl_hw->bit[HDMI_D1_PLUS]  = lane_data_sel_bits;
    hstx_ctrl_hw->bit[HDMI_D1_MINUS] = lane_data_sel_bits | HSTX_CTRL_BIT0_INV_BITS;
    // lane 2
    lane_data_sel_bits = (2 * 10    ) << HSTX_CTRL_BIT0_SEL_P_LSB |
                         (2 * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;    
    hstx_ctrl_hw->bit[HDMI_D2_PLUS]  = lane_data_sel_bits;
    hstx_ctrl_hw->bit[HDMI_D2_MINUS] = lane_data_sel_bits | HSTX_CTRL_BIT0_INV_BITS;
    for (int i = 12; i <= 19; ++i) {
      gpio_set_function(i, (gpio_function_t)0); // HSTX
    }

    dma_channel_config c;
    c = dma_channel_get_default_config(self->dma_command_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    // This wraps the transfer back to the start of the write address.
    size_t wrap = 3; // 8 bytes because we write two DMA registers.
    volatile uint32_t *write_addr = &dma_hw->ch[self->dma_pixel_channel].al3_transfer_count;
    if (half_width) {
        wrap = 4; // 16 bytes because we write all four DMA registers.
        write_addr = &dma_hw->ch[self->dma_pixel_channel].al3_ctrl;
    }
    channel_config_set_ring(&c, true, wrap);
    // No chain because we use an interrupt to reload this channel instead of a
    // third channel.
    dma_channel_configure(
        self->dma_command_channel,
        &c,
        write_addr,
        self->dma_commands,
        (1 << wrap) / sizeof(uint32_t),
        false
        );

    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
 }


static void __not_in_flash_func(dma_irq_handler)(void) {
    //if ( gfx_mode != gfx_mode_requested ) set_hdmi_framebuffer(&hdmi_obj, fb_width, fb_height);
    uint ch_num = active_picodvi->dma_pixel_channel;
    dma_hw->intr = 1u << ch_num;

    // Set the read_addr back to the start and trigger the first transfer (which
    // will trigger the pixel channel).
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    ch = &dma_hw->ch[active_picodvi->dma_command_channel];
    ch->al3_read_addr_trig = (uintptr_t)active_picodvi->dma_commands;

    vsync = (vsync==true)?false:true;
}

static void hdmi_framebuffer(hdmi_framebuffer_obj_t *self, uint16_t width, uint16_t height) {

    set_hdmi_framebuffer(self, width, height);

    active_picodvi = self;
    dma_hw->ints0 = (1u << self->dma_pixel_channel);
    dma_hw->inte0 = (1u << self->dma_pixel_channel);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_priority (DMA_IRQ_0, 0);    
    irq_set_enabled(DMA_IRQ_0, true);

    dma_irq_handler();
}


void __not_in_flash("hdmi_init") hdmi_init(gfx_mode_t mode)
{
    hdmi_obj.framebuffer = (uint32_t *)framebuffer;
    hdmi_obj.dma_commands = dma_commands;
    switch(mode) {
        case MODE_VGA_320x240:
          fb_width = 320;
          fb_height = 240;
          break;

        case MODE_VGA_256x240:
          fb_width = 320;
          fb_height = 240;
          break;

        case MODE_VGA_640x240:
          fb_width = 640;
          fb_height = 240;
          break;
    }
    hdmi_framebuffer(&hdmi_obj, fb_width, fb_height);
    gfxmode = mode;
}

 
uint8_t * __not_in_flash("hdmi_get_line_buffer") hdmi_get_line_buffer(int j) {
  return (&framebuffer[((gfxmode==MODE_VGA_256x240)?(320-256)/2:0) + j*MAX_FB_WIDTH]);
}

void __not_in_flash("hdmi_fill_screen") hdmi_fill_screen(uint8_t color) {
  int i,j;
  uint8_t color8 = VGA_RGB(R16(color),G16(color),B16(color));
  for (j=0; j<fb_height; j++)
  {
    uint8_t * dst=&framebuffer[j*MAX_FB_WIDTH];
    for (i=0; i<fb_width; i++)
    {
      *dst++ = color8;
    }
  }
}

int __not_in_flash("hdmi_raster_line") hdmi_raster_line(void) {
    dma_channel_hw_t *ch = &dma_hw->ch[active_picodvi->dma_command_channel];
    uintptr_t line = (uintptr_t)ch->read_addr - (uintptr_t)active_picodvi->dma_commands;
    return (line/active_picodvi->dma_command_size);
}

void __not_in_flash("hdmi_vsync") hdmi_vsync(void) {
    volatile bool vb=vsync; 
    while (vsync==vb) {
        __dmb();
    };
}

void __not_in_flash("hdmi_wait_line") hdmi_wait_line(int line)
{
  while (hdmi_raster_line() != line) {};
}


#ifdef HAS_SND

static void (*fillsamples)(audio_sample * stream, int len) = nullptr;
static audio_sample * snd_buffer;       // samples buffer (1 malloc for 2 buffers)
static uint16_t snd_nb_samples;         // total nb samples (mono) later divided by 2
static uint16_t snd_sample_ptr = 0;     // sample index
static audio_sample * audio_buffers[2]; // pointers to 2 samples buffers 
static volatile int cur_audio_buffer;
static volatile int last_audio_buffer;
static int pwm_dma_chan;

/********************************
 * Processing
********************************/ 
// fill half buffer depending on current position
static void __not_in_flash("pwm_audio_handle_buffer") pwm_audio_handle_buffer(void)
{
  if (last_audio_buffer == cur_audio_buffer) {
    return;
  }
  audio_sample *buf = audio_buffers[last_audio_buffer];
  last_audio_buffer = cur_audio_buffer;
  fillsamples(buf, snd_nb_samples);
}

static void __isr __time_critical_func(AUDIO_isr)()
{
  cur_audio_buffer = 1 - cur_audio_buffer;  
  dma_hw->ch[pwm_dma_chan].al3_read_addr_trig = (intptr_t)audio_buffers[cur_audio_buffer];
  dma_hw->ints1 = (1u << pwm_dma_chan);  
}


static void pwm_audio_reset(void)
{
  memset((void*)snd_buffer,0, snd_nb_samples*sizeof(uint8_t));
}

/********************************
 * Initialization
********************************/ 
static void pwm_audio_init(int buffersize, void (*callback)(audio_sample * stream, int len))
{
  fillsamples = callback;
  snd_nb_samples = buffersize;
  snd_sample_ptr = 0;
  snd_buffer =  (audio_sample*)malloc(snd_nb_samples*sizeof(audio_sample));
  if (snd_buffer == NULL) {
    printf("sound buffer could not be allocated!!!!!\n");
    return;  
  }
  memset((void*)snd_buffer,128, snd_nb_samples*sizeof(audio_sample));

  gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);

  int audio_pin_slice = pwm_gpio_to_slice_num(AUDIO_PIN);
  pwm_set_gpio_level(AUDIO_PIN, 0);

  // Setup PWM for audio output
  pwm_config config = pwm_get_default_config();
  pwm_config_set_clkdiv(&config, (((float)SOUNDRATE)/1000));
  pwm_config_set_wrap(&config, 254);
  pwm_init(audio_pin_slice, &config, true);

  snd_nb_samples = snd_nb_samples/2;
  audio_buffers[0] = &snd_buffer[0];
  audio_buffers[1] = &snd_buffer[snd_nb_samples];

  // Each sample played from a single DMA channel
  // Setup DMA channel to drive the PWM
  pwm_dma_chan = AUD_DMA_CHANNEL;
  dma_channel_config pwm_dma_chan_config = dma_channel_get_default_config(pwm_dma_chan);
  // Transfer 16 bits at once, increment read address to go through sample
  // buffer, always write to the same address (PWM slice CC register).
  channel_config_set_transfer_data_size(&pwm_dma_chan_config, DMA_SIZE_16);
  channel_config_set_read_increment(&pwm_dma_chan_config, true);
  channel_config_set_write_increment(&pwm_dma_chan_config, false);
  // Transfer on PWM cycle end
  channel_config_set_dreq(&pwm_dma_chan_config, DREQ_PWM_WRAP0 + audio_pin_slice);

  // Setup the channel and set it going
  dma_channel_configure(
      pwm_dma_chan,
      &pwm_dma_chan_config,
      &pwm_hw->slice[audio_pin_slice].cc, // Write to PWM counter compare
      snd_buffer, // Read values from audio buffer
      snd_nb_samples,
      false // Start immediately if true.
  );

  // Setup interrupt handler to fire when PWM DMA channel has gone through the
  // whole audio buffer
  dma_channel_set_irq1_enabled(pwm_dma_chan, true);
  irq_set_exclusive_handler(DMA_IRQ_1, AUDIO_isr);
  //irq_set_priority (DMA_IRQ_1, PICO_DEFAULT_IRQ_PRIORITY-8);
  irq_set_enabled(DMA_IRQ_1, true);
  dma_channel_start(pwm_dma_chan);
}

void audio_init(int samplesize, void (*callback)(audio_sample * stream, int len))
{
  pwm_audio_init(samplesize, callback);
}

void __not_in_flash("audio_handle") audio_handle(void)
{
    if (fillsamples != NULL) pwm_audio_handle_buffer();
}

void * audio_get_buffer(void)
{
  void *buf = audio_buffers[cur_audio_buffer==0?1:0];
  return buf; 
}

#endif