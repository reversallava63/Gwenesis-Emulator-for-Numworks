/*
 * Gwenesis Display Driver Header for Numworks EADK
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <eadk.h>
#include <stdint.h>
#include <stdbool.h>

// Display Scaling Modes
typedef enum {
  SCALE_CENTER_LETTERBOX = 0, // 320x224 centered with 8px top/bottom letterbox
  SCALE_H32_STRETCH_320  = 1, // 256px -> 320px 5:4 ratio expansion
  SCALE_FULL_STRETCH_240 = 2  // 224 scanlines -> 240p full fill
} display_scale_mode_t;

// Function Prototypes
void display_init(void);
void display_set_scaling_mode(display_scale_mode_t mode);
void display_push_gwenesis_frame(const uint16_t *vdp_buffer, int vdp_width, int vdp_height);
void display_draw_fps(float fps, uint32_t frame_num);

#endif // DISPLAY_H