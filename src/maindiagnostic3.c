// DIAGNOSTIC BUILD #3 - not the real app.
//
// Diagnostics #1 and #2 confirmed: app shell/linking/install is fine, and
// storage.c is fine. This build adds back the real init sequence from
// main.c - malloc(VRAM_MAX_SIZE), load_cartridge(), power_on(),
// reset_emulation() - which exercises heap.c's custom _sbrk, the ym2612.c
// stub, gwenesis_bus.c, and z80inst.c/gwenesis_sn76489.c. It stops BEFORE
// the per-frame loop, which is where the VDP single-line-buffer pointer
// trick lives (the newest and riskiest change from last session, and the
// top suspect). If THIS crashes, the bug is in init. If THIS is clean,
// the bug is specifically in the frame loop / VDP buffer trick.

#include <eadk.h>
#include <gwenesis.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "storage.h"

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "GwenesisDiag3";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

extern unsigned char *VRAM;
extern int zclk;
int system_clock;
int scan_line;

#define AUDIO_SAMPLE_RATE (53267)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 60 + 1)
int16_t gwenesis_sn76489_buffer[AUDIO_BUFFER_LENGTH];
int sn76489_index;
int sn76489_clock;
int16_t gwenesis_ym2612_buffer[AUDIO_BUFFER_LENGTH];
int ym2612_index;
int ym2612_clock;

extern unsigned char gwenesis_vdp_regs[0x20];
extern unsigned short gwenesis_vdp_status;
extern unsigned short CRAM565[256];
extern unsigned int screen_width, screen_height;
extern int hint_pending;

SaveState *saveGwenesisStateOpenForRead(const char *fileName) { return (void *)1; }
SaveState *saveGwenesisStateOpenForWrite(const char *fileName) { return (void *)1; }
int saveGwenesisStateGet(SaveState *state, const char *tagName) { return 0; }
void saveGwenesisStateSet(SaveState *state, const char *tagName, int value) {}
void saveGwenesisStateGetBuffer(SaveState *state, const char *tagName, void *buffer, int length) {}
void saveGwenesisStateSetBuffer(SaveState *state, const char *tagName, void *buffer, int length) {}

void gwenesis_io_get_buttons() {}

static void status(const char *msg, int y)
{
    eadk_point_t pt = { 8, (uint16_t)y };
    eadk_display_draw_string(msg, pt, false, eadk_color_white, eadk_color_black);
}

int main(int argc, char *argv[])
{
    eadk_display_push_rect_uniform(eadk_screen_rect, eadk_color_black);
    eadk_point_t title = { 8, 8 };
    eadk_display_draw_string("Diagnostic #3: init sequence", title, true, eadk_color_white, eadk_color_black);

    if (eadk_external_data == NULL)
    {
        status("external_data is NULL, aborting", 40);
        goto wait_for_back;
    }
    status("external_data OK", 40);

    VRAM = (unsigned char *)malloc(VRAM_MAX_SIZE);
    if (VRAM == NULL)
    {
        status("malloc(VRAM) FAILED - out of heap", 60);
        goto wait_for_back;
    }
    status("malloc(VRAM) OK", 60);

    load_cartridge((void *)eadk_external_data, eadk_external_data_size);
    status("load_cartridge() OK", 80);

    power_on();
    status("power_on() OK", 100);

    reset_emulation();
    status("reset_emulation() OK", 120);

    status("All init steps survived. Press Back.", 150);

wait_for_back:
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