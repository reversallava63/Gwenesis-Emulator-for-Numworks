/*
 * Sound Driver / Stub for Gwenesis Numworks EADK
 * Audio hardware is stubbed on EADK to conserve Cortex-M7 cycles.
 */

#include <eadk.h>
#include <stdint.h>
#include <stdbool.h>

void sound_init(void) {
  // Audio stub: Numworks EADK API does not expose hardware DAC/PCM output
}

void sound_update(void) {
  // Dummy audio frame update
}

void sound_close(void) {
  // Dummy audio cleanup
}