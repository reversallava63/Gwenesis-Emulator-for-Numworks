/*
 * Keyboard controller input mapping for Gwenesis EADK
 */

#include <eadk.h>
#include <stdint.h>
#include <stdbool.h>

#include "keyboard.h"

// External Gwenesis IO press/release prototypes
extern void gwenesis_io_pad_press_button(int pad, int button);
extern void gwenesis_io_pad_release_button(int pad, int button);

uint16_t keyboard_get_genesis_pad(void) {
  uint64_t state = eadk_keyboard_scan();
  uint16_t pad = 0;

  // Directional Pad
  if (eadk_keyboard_key_down(state, eadk_key_up))    pad |= GENESIS_PAD_UP;
  if (eadk_keyboard_key_down(state, eadk_key_down))  pad |= GENESIS_PAD_DOWN;
  if (eadk_keyboard_key_down(state, eadk_key_left))  pad |= GENESIS_PAD_LEFT;
  if (eadk_keyboard_key_down(state, eadk_key_right)) pad |= GENESIS_PAD_RIGHT;

  // Action Buttons
  if (eadk_keyboard_key_down(state, eadk_key_ok))   pad |= GENESIS_PAD_A; // Button A
  if (eadk_keyboard_key_down(state, eadk_key_back)) pad |= GENESIS_PAD_B; // Button B
  if (eadk_keyboard_key_down(state, eadk_key_exe))  pad |= GENESIS_PAD_C; // Button C
  
  // Start Button
  if (eadk_keyboard_key_down(state, eadk_key_shift) || 
      eadk_keyboard_key_down(state, eadk_key_alpha)) pad |= GENESIS_PAD_START;

  return pad;
}

void keyboard_poll_and_dispatch(uint64_t *old_state) {
  uint64_t current_state = eadk_keyboard_scan();

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

bool keyboard_is_exit_requested(uint64_t current_state) {
  return eadk_keyboard_key_down(current_state, eadk_key_home);
}