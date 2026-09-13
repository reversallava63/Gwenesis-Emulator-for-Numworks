/*
 * Gwenesis Display Driver for Numworks Calculator (EADK Port)
 * Target Screen: 320x240 16-bit RGB565 LCD
 */

#include <eadk.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "display.h"

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

static display_scale_mode_t g_scale_mode = SCALE_CENTER_LETTERBOX;
static uint16_t g_line_buffer[SCREEN_WIDTH]; // Scratch buffer for horizontal scaling

// Initialize display and clear borders to solid black
void display_init(void) {
  eadk_rect_t full_screen = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
  eadk_display_push_rect_uniform(full_screen, 0x0000); // Black RGB565
}

// Set active display scaling mode
void display_set_scaling_mode(display_scale_mode_t mode) {
  g_scale_mode = mode;
  // Clear screen to erase leftover pillarbox/letterbox artifacts
  display_init();
}

/*
 * Blit H40 Mode (320x224) directly to LCD
 * 320px matches Numworks LCD width exactly.
 * 224px height leaves 16px vertical space (8px top, 8px bottom).
 */
static void blit_h40_direct(const uint16_t *vdp_buffer, int height) {
  int y_offset = (SCREEN_HEIGHT - height) / 2; // (240 - 224) / 2 = 8
  
  eadk_rect_t rect = {
    .x = 0,
    .y = (uint16_t)y_offset,
    .width = 320,
    .height = (uint16_t)height
  };

  // Push full 320x224 frame to LCD controller via DMA
  eadk_display_push_rect(rect, (const eadk_color_t *)vdp_buffer);
}

/*
 * Blit H32 Mode (256x224) with 1:1 centering (Pillarboxed)
 * 256px width leaves 64px horizontal space (32px left, 32px right).
 */
static void blit_h32_centered(const uint16_t *vdp_buffer, int height) {
  int x_offset = (SCREEN_WIDTH - 256) / 2;    // (320 - 256) / 2 = 32
  int y_offset = (SCREEN_HEIGHT - height) / 2; // (240 - 224) / 2 = 8

  eadk_rect_t rect = {
    .x = (uint16_t)x_offset,
    .y = (uint16_t)y_offset,
    .width = 256,
    .height = (uint16_t)height
  };

  eadk_display_push_rect(rect, (const eadk_color_t *)vdp_buffer);
}

/*
 * Blit H32 Mode (256x224) stretched horizontally to 320px
 * Uses 5:4 fast expansion (duplicates 1 pixel every 4 source pixels):
 * Src [0 1 2 3] -> Dst [0 1 2 3 3]
 */
static void blit_h32_stretched(const uint16_t *vdp_buffer, int height) {
  int y_offset = (SCREEN_HEIGHT - height) / 2;

  // Process scanline by scanline
  for (int y = 0; y < height; y++) {
    const uint16_t *src_line = &vdp_buffer[y * 256];
    
    // Fast 256px -> 320px integer expansion loop
    int dst_idx = 0;
    for (int x = 0; x < 256; x += 4) {
      g_line_buffer[dst_idx++] = src_line[x];
      g_line_buffer[dst_idx++] = src_line[x + 1];
      g_line_buffer[dst_idx++] = src_line[x + 2];
      g_line_buffer[dst_idx++] = src_line[x + 3];
      g_line_buffer[dst_idx++] = src_line[x + 3]; // Repeat 4th pixel
    }

    // Push single stretched scanline to LCD
    eadk_rect_t line_rect = {
      .x = 0,
      .y = (uint16_t)(y_offset + y),
      .width = 320,
      .height = 1
    };
    eadk_display_push_rect(line_rect, (const eadk_color_t *)g_line_buffer);
  }
}

/*
 * Primary Gwenesis Display Push API
 * Called at the end of every VDP frame step in gwenesis_run_frame()
 */
void display_push_gwenesis_frame(const uint16_t *vdp_buffer, int vdp_width, int vdp_height) {
  if (!vdp_buffer) return;

  // H40 Mode (320 pixels wide)
  if (vdp_width == 320) {
    blit_h40_direct(vdp_buffer, vdp_height);
  }
  // H32 Mode (256 pixels wide)
  else if (vdp_width == 256) {
    if (g_scale_mode == SCALE_H32_STRETCH_320) {
      blit_h32_stretched(vdp_buffer, vdp_height);
    } else {
      blit_h32_centered(vdp_buffer, vdp_height);
    }
  }
  // Fallback for custom or PAL resolutions
  else {
    eadk_rect_t rect = {0, 0, (uint16_t)vdp_width, (uint16_t)vdp_height};
    eadk_display_push_rect(rect, (const eadk_color_t *)vdp_buffer);
  }
}

// Draw OSD FPS counter at bottom of LCD
void display_draw_fps(float fps, uint32_t frame_num) {
  char text[32];
  snprintf(text, sizeof(text), "%2.1f FPS | F:%lu", fps, frame_num);

  eadk_point_t pt = {4, 228}; // Bottom 12px letterbox area
  eadk_display_draw_string(
    text,
    pt,
    false,  // Small font
    0x07E0, // Bright Green RGB565
    0x0000  // Black background
  );
}