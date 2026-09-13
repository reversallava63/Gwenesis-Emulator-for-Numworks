// DIAGNOSTIC 6 - load_cartridge() is now confirmed fully working (the
// ROM_SWAP fix in gwenesis_bus.c resolved it). This extends the bisection
// to the next two steps in the original plan: power_on() and
// reset_emulation(), each with a blocking checkpoint after.

#include <eadk.h>
#include <gwenesis.h>
#include <gwenesis_bus.h>
#include <stdint.h>
#include <stdlib.h>

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Diag6";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

extern unsigned char *VRAM;

// gwenesis_sn76489.c declares sn76489_index/sn76489_clock/
// gwenesis_sn76489_buffer as extern and uses them internally (9
// references) - something has to actually DEFINE them, or the final link
// fails with "undefined reference to sn76489_clock"/"sn76489_index" the
// moment power_on()/reset_emulation() get linked in (they weren't
// referenced by anything reachable in earlier diagnostics, so this didn't
// surface until now). The original main.c defined these; this stripped-
// down diagnostic build dropped them by accident when it was created.
// Audio itself is still disabled - these buffers are never actually fed
// anywhere - but the symbols still need to exist for the link to succeed.
// ym2612's equivalents aren't referenced outside main.c anywhere in the
// current codebase, so they haven't caused a link error yet, but adding
// them back too avoids hitting this same bug again later if that changes.
#define AUDIO_SAMPLE_RATE (53267)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 60 + 1)
int16_t gwenesis_sn76489_buffer[AUDIO_BUFFER_LENGTH];
int sn76489_index;
int sn76489_clock;
int16_t gwenesis_ym2612_buffer[AUDIO_BUFFER_LENGTH];
int ym2612_index;
int ym2612_clock;

SaveState *saveGwenesisStateOpenForRead(const char *fileName) { return (void *)1; }
SaveState *saveGwenesisStateOpenForWrite(const char *fileName) { return (void *)1; }
int saveGwenesisStateGet(SaveState *state, const char *tagName) { return 0; }
void saveGwenesisStateSet(SaveState *state, const char *tagName, int value) {}
void saveGwenesisStateGetBuffer(SaveState *state, const char *tagName, void *buffer, int length) {}
void saveGwenesisStateSetBuffer(SaveState *state, const char *tagName, void *buffer, int length) {}
void gwenesis_io_get_buttons() {}

static void wait_release(void)
{
    while (true)
    {
        eadk_keyboard_state_t state = eadk_keyboard_scan();
        if (!eadk_keyboard_key_down(state, eadk_key_back) &&
            !eadk_keyboard_key_down(state, eadk_key_home))
            break;
        eadk_timing_msleep(20);
    }
}

static void checkpoint(const char *msg, int y)
{
    wait_release();
    eadk_point_t pt = { 8, (uint16_t)y };
    eadk_display_draw_string(msg, pt, false, eadk_color_white, eadk_color_black);
    while (true)
    {
        eadk_keyboard_state_t state = eadk_keyboard_scan();
        if (eadk_keyboard_key_down(state, eadk_key_back) ||
            eadk_keyboard_key_down(state, eadk_key_home))
            break;
        eadk_timing_msleep(30);
    }
    wait_release();
}

int main(int argc, char *argv[])
{
    eadk_display_push_rect_uniform(eadk_screen_rect, eadk_color_black);
    eadk_point_t title = { 8, 8 };
    eadk_display_draw_string("Diag 6: +power_on() +reset_emulation()", title, true, eadk_color_white, eadk_color_black);

    VRAM = (unsigned char *)malloc(VRAM_MAX_SIZE);
    checkpoint(VRAM ? "malloc OK" : "malloc FAILED", 30);
    if (VRAM == NULL) return 0;

    load_cartridge((void *)eadk_external_data, eadk_external_data_size);
    checkpoint("load_cartridge() OK", 50);

    power_on();
    checkpoint("power_on() OK", 70);

    reset_emulation();
    checkpoint("reset_emulation() OK - all done!", 90);

    while (true)
    {
        eadk_keyboard_state_t state = eadk_keyboard_scan();
        if (eadk_keyboard_key_down(state, eadk_key_back) ||
            eadk_keyboard_key_down(state, eadk_key_home))
            break;
        eadk_timing_msleep(30);
    }
    return 0;
}