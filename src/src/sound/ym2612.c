// Stub YM2612 (FM sound chip) implementation for the EADK port.
//
// Audio is intentionally disabled on this target (see PROJECT_STATUS.md) -
// ym2612_clock is force-set to a "done" sentinel every frame in main.c, so
// ym2612_run()'s body never actually executes. But gwenesis_bus.c still
// calls YM2612Init/Config/ResetChip/Write/Read directly (register writes
// still need to "succeed" for game logic that pokes the chip, even though
// no sound comes out), so those symbols have to exist as real, non-inline
// functions - static inline stubs in the header didn't work because not
// every call site got inlined by the compiler (see PROJECT_STATUS.md).
//
// IMPORTANT: earlier, the *real* retro-go ym2612.c (full FM synthesis
// engine) got copied in wholesale instead of a stub. That pulled in ~50KB
// of static lookup tables (sin_tab, tl_tab, lfo_pm_table, the internal
// ym2612 channel-state struct) plus the YM2612Update mixing routine - none
// of it reachable-but-dead-code-eliminable, because YM2612Init() is called
// unconditionally from power_on() in main.c, so the linker can't prove any
// of it is unused. That's what blew the RAM budget and made NumWorks'
// linker.ld fail with "cannot move location counter backwards". This file
// replaces that - deliberately no tables, no synthesis, just the minimum
// needed so the bus/CPU cores link and run.
#include "ym2612.h"
#include <stdint.h>

void YM2612Init(void)
{
}

void YM2612Config(unsigned char dac_bits)
{
    (void)dac_bits;
}

void YM2612ResetChip(void)
{
}

void YM2612Write(unsigned int a, unsigned int v, int target)
{
    (void)a;
    (void)v;
    (void)target;
}

unsigned int YM2612Read(int target)
{
    (void)target;
    return 0;
}

void ym2612_run(int target)
{
    // Audio disabled: main.c pins ym2612_clock to a "done" sentinel every
    // frame, so this should never actually be reached - but keep it a
    // real no-op rather than relying on that, in case anything calls in
    // directly.
    (void)target;
}

void gwenesis_ym2612_save_state(void)
{
}

void gwenesis_ym2612_load_state(void)
{
}