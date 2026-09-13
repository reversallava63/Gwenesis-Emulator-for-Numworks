// DIAGNOSTIC BUILD #2 - not the real app.
//
// Diagnostic #1 confirmed: basic app shell/linking/install is fine.
// This build adds back storage.c's extapp_* address-detection chain and
// the eadk_external_data fallback (exactly what select_rom() does in the
// real app), but stops there - no load_cartridge(), no power_on(), no
// gwenesis emulation loop at all. If THIS crashes, the bug is in
// storage.c's raw pointer dereferences. If THIS is also clean, the bug is
// somewhere in the gwenesis core (main.c's frame loop, gwenesis_bus.c,
// the VDP buffer trick, etc.) and we bisect further from there.

#include <eadk.h>
#include <stdint.h>
#include <stdio.h>
#include "storage.h"

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "GwenesisDiag2";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

#define MAX_ROM_ENTRIES 32
static const char *rom_names[MAX_ROM_ENTRIES];

int main(int argc, char *argv[])
{
    eadk_display_push_rect_uniform(eadk_screen_rect, eadk_color_black);

    eadk_point_t pt1 = { 8, 8 };
    eadk_display_draw_string("Diagnostic #2: storage.c test", pt1, true, eadk_color_white, eadk_color_black);

    // This is the exact call chain select_rom() makes first in the real
    // app - exercises extapp_calculatorModel(), extapp_userlandAddress(),
    // extapp_address(), extapp_isValid(), all the raw pointer
    // dereferences in storage.c.
    int rom_count = extapp_fileListWithExtension(rom_names, MAX_ROM_ENTRIES, "md");

    char line1[64];
    snprintf(line1, sizeof(line1), "rom_count = %d", rom_count);
    eadk_point_t pt2 = { 8, 40 };
    eadk_display_draw_string(line1, pt2, false, eadk_color_white, eadk_color_black);

    // storage.c survived if we got here. Now check eadk_external_data,
    // same as select_rom()'s fallback path - still no gwenesis code
    // touched.
    char line2[64];
    if (eadk_external_data != NULL)
        snprintf(line2, sizeof(line2), "external_data size = %u", (unsigned)eadk_external_data_size);
    else
        snprintf(line2, sizeof(line2), "external_data = NULL");
    eadk_point_t pt3 = { 8, 60 };
    eadk_display_draw_string(line2, pt3, false, eadk_color_white, eadk_color_black);

    eadk_point_t pt4 = { 8, 90 };
    eadk_display_draw_string("If you see all of this: storage.c is fine.", pt4, false, eadk_color_white, eadk_color_black);
    eadk_point_t pt5 = { 8, 110 };
    eadk_display_draw_string("Press Back to return home.", pt5, false, eadk_color_white, eadk_color_black);

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
