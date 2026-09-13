/*
 * Keyboard Controller Input Header for Gwenesis EADK
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <eadk.h>
#include <stdint.h>
#include <stdbool.h>

// Genesis Joypad Button Bitmasks
#define GENESIS_PAD_UP     (1 << 0)
#define GENESIS_PAD_DOWN   (1 << 1)
#define GENESIS_PAD_LEFT   (1 << 2)
#define GENESIS_PAD_RIGHT  (1 << 3)
#define GENESIS_PAD_A      (1 << 4)
#define GENESIS_PAD_B      (1 << 5)
#define GENESIS_PAD_C      (1 << 6)
#define GENESIS_PAD_START  (1 << 7)

// Function Prototypes
uint16_t keyboard_get_genesis_pad(void);
void keyboard_poll_and_dispatch(uint64_t *old_keyboard_state);
bool keyboard_is_exit_requested(uint64_t current_state);

#endif // KEYBOARD_H