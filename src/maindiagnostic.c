// DIAGNOSTIC BUILD - not the real app.
//
// Purpose: bisect the crash-and-full-reboot seen on real N0120 hardware.
// This intentionally skips select_rom() (and therefore all of storage.c's
// raw pointer dereferences of hardcoded model-detection addresses) and the
// entire gwenesis emulation core. If THIS boots cleanly and just shows
// text without crashing, the problem is in storage.c or the gwenesis core,
// not in the basic app shell/toolchain/linking setup. If THIS also
// crashes/reboots, the problem is more fundamental (entry point, linker
// script mismatch, stack setup, etc.) and has nothing to do with gwenesis
// or storage.c at all.
//
// Swap this in as main.c, build, install, and test on real hardware
// before touching anything else.

#include <eadk.h>
#include <stdint.h>

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "GwenesisDiag";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

int main(int argc, char *argv[])
{
    eadk_display_push_rect_uniform(eadk_screen_rect, eadk_color_black);

    eadk_point_t pt1 = { 8, 8 };
    eadk_display_draw_string("Gwenesis diagnostic build", pt1, true, eadk_color_white, eadk_color_black);

    eadk_point_t pt2 = { 8, 40 };
    eadk_display_draw_string("If you see this, basic app", pt2, false, eadk_color_white, eadk_color_black);

    eadk_point_t pt3 = { 8, 60 };
    eadk_display_draw_string("shell + linking + install works.", pt3, false, eadk_color_white, eadk_color_black);

    eadk_point_t pt4 = { 8, 90 };
    eadk_display_draw_string("Press Back to return home.", pt4, false, eadk_color_white, eadk_color_black);

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
