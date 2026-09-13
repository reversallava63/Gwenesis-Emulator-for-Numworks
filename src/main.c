/*
 * Gwenesis Sega Genesis Emulator for Numworks Calculator (EADK Port)
 * Target Hardware: Numworks N0120 (STM32H725 @ 320KB RAM, 320x240 LCD)
 * 
 * Merged from:
 * 1. nwagyu/nofrendo EADK wrapper skeleton (main.c, video.c, input.c)
 * 2. retro-go gwenesis frame execution loop
 */

#include <eadk.h>
#undef false
#undef true
#undef bool

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// EADK App Metadata (Declared with exact section attributes for Epsilon/nwlink)
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Gwenesis MD";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

// Symbol linked by nwlink containing embedded Genesis ROM in Flash memory
extern const uint8_t eadk_external_data[];

// ---------------------------------------------------------------------------
// Gwenesis Core Declarations (from gwenesis retro-go core)
// ---------------------------------------------------------------------------
#define VDP_CYCLES_PER_LINE  488
#define LINES_PER_FRAME_NTSC 262
#define LINES_PER_FRAME_PAL  313
#define STATUS_VIRQPENDING   0x0080

extern unsigned char gwenesis_vdp_regs[0x20];
extern unsigned int gwenesis_vdp_status;
extern unsigned short CRAM565[256];
extern unsigned int screen_width, screen_height;
extern int system_clock, scan_line, hint_pending;

// Core functions
extern void load_cartridge(const void *data, size_t size);
extern void power_on(void);
extern void reset_emulation(void);
extern void m68k_run(int cycles);
extern void gwenesis_vdp_set_buffer(void *buffer);
extern void gwenesis_vdp_render_config(void);
extern void gwenesis_vdp_render_line(int line);
extern void gwenesis_io_pad_press_button(int pad, int button);
extern void gwenesis_io_pad_release_button(int pad, int button);

// Single 320x224 RGB565 Framebuffer (143.36 KB in SRAM)
static uint16_t g_framebuffer[320 * 224];

// Dummy rand override to avoid newlib heap allocation
int rand(void) {
  return 0;
}

// ---------------------------------------------------------------------------
// Helper: Poll Numworks Keyboard via EADK keyboard scan
// ---------------------------------------------------------------------------
static void poll_keyboard_inputs(uint64_t *old_state) {
  uint64_t current_state = eadk_keyboard_scan();

  // Genesis 3-Button Keymap Mapping
  typedef struct {
    eadk_key_t key;
    int genesis_button;
  } key_map_t;

  const key_map_t map[] = {
    {eadk_key_up,        0}, // Up
    {eadk_key_down,      1}, // Down
    {eadk_key_left,      2}, // Left
    {eadk_key_right,     3}, // Right
    {eadk_key_ok,        4}, // Button A
    {eadk_key_back,      5}, // Button B
    {eadk_key_exe,       6}, // Button C
    {eadk_key_backspace, 7}, // Start
    {eadk_key_shift,     7}  // Start (Alt)
  };

  for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
    bool wasDown = eadk_keyboard_key_down(*old_state, map[i].key);
    bool isDown = eadk_keyboard_key_down(current_state, map[i].key);

    if (isDown != wasDown) {
      if (isDown) {
        gwenesis_io_pad_press_button(0, map[i].genesis_button);
      } else {
        gwenesis_io_pad_release_button(0, map[i].genesis_button);
      }
    }
  }

  *old_state = current_state;
}

// ---------------------------------------------------------------------------
// Main EADK Entry Point
// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
  // Clear LCD screen to black
  eadk_display_push_rect_uniform(eadk_screen_rect, eadk_color_black);

  // 1. Verify external ROM address from eadk_external_data
  if (eadk_external_data == NULL) {
    return 1;
  }

  // 2. Load Cartridge & Power On
  load_cartridge(eadk_external_data, 0);
  power_on();
  reset_emulation();

  // 3. Configure VDP buffer
  gwenesis_vdp_set_buffer((void *)g_framebuffer);

  uint64_t old_keyboard_state = 0;
  bool running = true;

  // 4. Gwenesis Frame Execution Loop (Adapted from retro-go main.c)
  while (running) {
    // Poll keyboard inputs
    poll_keyboard_inputs(&old_keyboard_state);

    // Check for exit trigger (Back + Home chord or Home key)
    if (eadk_keyboard_key_down(old_keyboard_state, eadk_key_home)) {
      break;
    }

    int lines_per_frame = LINES_PER_FRAME_NTSC;
    screen_width = 320;
    screen_height = 224;

    gwenesis_vdp_render_config();

    system_clock = 0;
    scan_line = 0;

    // Line-by-line M68K CPU + VDP scanline loop
    while (scan_line < lines_per_frame) {
      m68k_run(system_clock + VDP_CYCLES_PER_LINE);

      if (scan_line < screen_height) {
        gwenesis_vdp_render_line(scan_line);
      }

      scan_line++;
      system_clock += VDP_CYCLES_PER_LINE;
    }

    // Push 320x224 frame centered on 320x240 LCD (8px y-offset)
    const int yoffset = (EADK_SCREEN_HEIGHT - 224) / 2;
    eadk_rect_t rect = {0, (uint16_t)yoffset, 320, 224};
    eadk_display_push_rect(rect, (const eadk_color_t *)g_framebuffer);
  }

  return 0;
}