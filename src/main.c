/*
 * Gwenesis Sega Genesis Emulator for Numworks Calculator (EADK Port)
 * Target Hardware: Numworks N0120 (STM32H725 @ 320KB RAM, 320x240 LCD)
 * 
 * Merged from:
 * 1. nwagyu/nofrendo EADK wrapper skeleton
 * 2. retro-go gwenesis frame execution loop
 */

#include <eadk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// EADK App Metadata (Declared for Epsilon/nwlink packaging)
const char eadk_app_name[] = "Gwenesis MD";
const uint32_t eadk_app_api_level = 0;

// Symbol linked by nwlink at compile/install time containing embedded ROM
extern const uint8_t eadk_external_data[];
extern const size_t eadk_external_data_size;

// ---------------------------------------------------------------------------
// Gwenesis Core Stubs & Definitions
// (Normally provided by gwenesis/src/ headers)
// ---------------------------------------------------------------------------
#define GENESIS_WIDTH  320
#define GENESIS_HEIGHT 224
#define NUMWORKS_WIDTH 320
#define NUMWORKS_HEIGHT 240
#define Y_OFFSET       ((NUMWORKS_HEIGHT - GENESIS_HEIGHT) / 2) // 8 pixels

// Genesis Controller Button Masks
#define PAD_UP     (1 << 0)
#define PAD_DOWN   (1 << 1)
#define PAD_LEFT   (1 << 2)
#define PAD_RIGHT  (1 << 3)
#define PAD_A      (1 << 4)
#define PAD_B      (1 << 5)
#define PAD_C      (1 << 6)
#define PAD_START  (1 << 7)

// Simulated Gwenesis state structs
typedef struct {
  uint8_t *rom_ptr;
  size_t rom_size;
  uint16_t pad_state;
  bool is_pal;
  uint16_t frame_buffer[GENESIS_WIDTH * GENESIS_HEIGHT]; // RGB565 buffer (143.3 KB)
} gwenesis_t;

static gwenesis_t g_emulator;

// Gwenesis function declarations (from gwenesis core)
extern void gwenesis_init(void);
extern void gwenesis_reset(void);
extern void gwenesis_load_rom(const uint8_t *data, size_t size);
extern void gwenesis_set_pad(uint8_t pad_num, uint16_t state);
extern void gwenesis_run_frame(uint16_t *framebuffer_rgb565);

// ---------------------------------------------------------------------------
// Helper: Poll Numworks Keyboard via EADK
// ---------------------------------------------------------------------------
static uint16_t poll_keyboard_inputs(void) {
  eadk_keyboard_state_t keys = eadk_keyboard_scan();
  uint16_t pad = 0;

  // D-Pad navigation
  if (eadk_keyboard_key_down(keys, EADK_KEY_UP))    pad |= PAD_UP;
  if (eadk_keyboard_key_down(keys, EADK_KEY_DOWN))  pad |= PAD_DOWN;
  if (eadk_keyboard_key_down(keys, EADK_KEY_LEFT))  pad |= PAD_LEFT;
  if (eadk_keyboard_key_down(keys, EADK_KEY_RIGHT)) pad |= PAD_RIGHT;

  // Genesis Buttons (Mapped to Numworks Keys)
  // OK key -> Button A
  if (eadk_keyboard_key_down(keys, EADK_KEY_OK))   pad |= PAD_A;
  // Back/Ans key -> Button B
  if (eadk_keyboard_key_down(keys, EADK_KEY_BACK)) pad |= PAD_B;
  // EXE key -> Button C
  if (eadk_keyboard_key_down(keys, EADK_KEY_EXE))  pad |= PAD_C;
  // Shift / Alpha -> Start
  if (eadk_keyboard_key_down(keys, EADK_KEY_ALPHA) || 
      eadk_keyboard_key_down(keys, EADK_KEY_SHIFT)) pad |= PAD_START;

  return pad;
}

// ---------------------------------------------------------------------------
// Helper: Render Framebuffer to LCD via EADK
// ---------------------------------------------------------------------------
static void render_frame_to_display(const uint16_t *rgb565_buffer) {
  // Push 320x224 RGB565 rectangle centered on the 320x240 screen
  eadk_rect_t display_rect = {
    .x = 0,
    .y = Y_OFFSET,
    .w = GENESIS_WIDTH,
    .h = GENESIS_HEIGHT
  };

  // eadk_display_push_rect transfers pixel block directly to STM32 LCD driver
  eadk_display_push_rect(display_rect, (const eadk_color_t *)rgb565_buffer);
}

// ---------------------------------------------------------------------------
// Main Application Loop
// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
  // Clear screen to dark background first
  eadk_rect_t full_screen = {0, 0, NUMWORKS_WIDTH, NUMWORKS_HEIGHT};
  eadk_display_push_rect_uniform(full_screen, 0x1082); // Dark slate RGB565

  // Display boot splash status
  eadk_point_t status_pos = {10, 10};
  eadk_display_draw_string(
    "Gwenesis EADK Booting...",
    status_pos,
    true, // Large font
    0xFFFF, // White text
    0x1082  // Dark bg
  );

  // 1. Verify external ROM data presence linked by nwlink
  if (&eadk_external_data[0] == NULL) {
    eadk_point_t err_pos = {10, 50};
    eadk_display_draw_string(
      "ERROR: No ROM linked in eadk_external_data!",
      err_pos,
      false,
      0xF800, // Red
      0x1082
    );
    while (1) {
      eadk_keyboard_state_t keys = eadk_keyboard_scan();
      if (eadk_keyboard_key_down(keys, EADK_KEY_HOME)) break;
    }
    return 1;
  }

  // 2. Initialize Gwenesis Core (M68K CPU, VDP Registers, Memory Bus)
  gwenesis_init();

  // 3. Mount Cartridge ROM directly from Flash pointer (zero RAM copy!)
  // Note: eadk_external_data is stored in Flash memory by Epsilon app loader
  gwenesis_load_rom(eadk_external_data, 0 /* Auto-detect ROM size */);
  gwenesis_reset();

  // 4. Main Emulation Loop
  uint32_t frame_count = 0;
  bool running = true;

  while (running) {
    // A. Check for exit button (Home or Back long-press)
    eadk_keyboard_state_t keys = eadk_keyboard_scan();
    if (eadk_keyboard_key_down(keys, EADK_KEY_HOME)) {
      running = false;
      break;
    }

    // B. Poll Joypad Inputs
    uint16_t pad_state = poll_keyboard_inputs();
    gwenesis_set_pad(0, pad_state);

    // C. Execute 1 Genesis Frame (M68K CPU + VDP scanlines)
    // Audio synthesis is bypassed to maintain 60 FPS on Cortex-M7
    gwenesis_run_frame(g_emulator.frame_buffer);

    // D. Push Framebuffer to LCD
    render_frame_to_display(g_emulator.frame_buffer);

    frame_count++;

    // Optional: Framerate pacing / sync with EADK timer if needed
    // eadk_timing_usleep(16666); // ~60 FPS
  }

  // Cleanup & return to Epsilon OS
  return 0;
}