// gwenesis EADK port - main.c (first draft)
//
// Adapted from retro-go's gwenesis/main/main.c, stripped of the retro-go
// framework (rg_system/rg_surface/rg_gui/rg_settings/rg_audio) and wired
// directly to EADK, following the pattern shown in nwagyu/nofrendo's main.c.
//
// Verified against the real eadk.h (uploaded): push_rect signature, keyboard
// bitmask API, and key enum names below all match.
//
// REMAINING OPEN QUESTION (needs gwenesis_vdp.h / gwenesis_vdp_gfx.c):
// does gwenesis_vdp_set_buffer() actually accept a raw RGB565 buffer, or
// does it require 8-bit palette indices (like retro-go's rg_surface path)?
// If it's indices, framebuffer must become uint8_t[FB_WIDTH*FB_HEIGHT] and
// you need a CRAM565[] lookup pass before the push_rect call.

#include <eadk.h>
#include <gwenesis.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include "storage.h"

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Gwenesis";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

// ---------------------------------------------------------------------
// gwenesis core globals it expects the platform layer to provide
// ---------------------------------------------------------------------
extern unsigned char *VRAM;
extern int zclk;
int system_clock;
int scan_line;

// Audio disabled: gwenesis' sound units still reference these symbols from
// other compilation units in the core, so we keep the buffers/indices around
// but never feed them anywhere, and force their clocks to "already done"
// (0x1000000) every frame so the run functions become no-ops.
#define AUDIO_SAMPLE_RATE (53267)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 60 + 1)
int16_t gwenesis_sn76489_buffer[AUDIO_BUFFER_LENGTH];
int sn76489_index;
int sn76489_clock;
int16_t gwenesis_ym2612_buffer[AUDIO_BUFFER_LENGTH];
int ym2612_index;
int ym2612_clock;

extern unsigned char gwenesis_vdp_regs[0x20];
extern unsigned short gwenesis_vdp_status; // must match the real definition's type
                                             // (gwenesis_vdp_mem.c) exactly - a mismatch
                                             // here is UB under strict-aliasing/LTO, not
                                             // just a cosmetic warning
extern unsigned short CRAM565[256];
extern unsigned int screen_width, screen_height;
extern int hint_pending;

// Watchdog globals from m68kcpu.c (frame-1313 freeze investigation) - see
// comment above their definitions there.
extern unsigned int gwenesis_diag_stuck_pc;
extern unsigned int gwenesis_diag_stuck_ir;
extern unsigned int gwenesis_diag_stuck_iters;
extern int gwenesis_diag_stuck_tripped;
extern unsigned int gwenesis_diag_aerr_count;
extern unsigned int gwenesis_diag_aerr_first_addr;
extern unsigned int gwenesis_diag_aerr_first_fc;
extern unsigned int gwenesis_diag_aerr_first_write;
extern unsigned int gwenesis_diag_aerr_first_pc;

// ---------------------------------------------------------------------
// Save state stubs - not wired up yet, just satisfying the linker so the
// core's save/load paths (if referenced anywhere) don't fail to link.
// Fill these in once ROM selection/storage is sorted.
// ---------------------------------------------------------------------
SaveState *saveGwenesisStateOpenForRead(const char *fileName) { return (void *)1; }
SaveState *saveGwenesisStateOpenForWrite(const char *fileName) { return (void *)1; }
int saveGwenesisStateGet(SaveState *state, const char *tagName) { return 0; }
void saveGwenesisStateSet(SaveState *state, const char *tagName, int value) {}
void saveGwenesisStateGetBuffer(SaveState *state, const char *tagName, void *buffer, int length) {}
void saveGwenesisStateSetBuffer(SaveState *state, const char *tagName, void *buffer, int length) {}

void gwenesis_io_get_buttons() {}

// ---------------------------------------------------------------------
// Framebuffer: gwenesis_vdp_render_line() computes its write address as
// screen_buffer[line * 320] every call, assuming screen_buffer points at
// a full multi-line frame. But per gwenesis_vdp_gfx.c, it only ever reads
// or writes within that single line - never any other line - so it
// doesn't actually need a full frame behind it. We exploit that: rather
// than allocate FB_WIDTH*FB_HEIGHT (76,800 bytes) of RAM we never need
// more than one line of at once, we allocate one real line and, before
// each render_line() call, hand it a pointer OFFSET BY -line*320 so that
// its internal "screen_buffer[line*320]" arithmetic lands back on our
// real (small) buffer. This is safe specifically because nothing in
// gwenesis_vdp_gfx.c dereferences screen_buffer at any other offset - if
// that ever changes upstream, this trick would need revisiting.
//
// VDP_LINE_PAD covers a genuine out-of-bounds-looking write already
// present in the original code: in 256-wide (!H40) mode, render_line()
// clears "320 bytes starting at screen_buffer_line - 32" to blank the
// letterboxed edges. 32 bytes of padding before our real line, and slack
// after it, keeps that write inside our buffer instead of corrupting
// whatever RAM happens to sit next to it (which the original full-frame
// version only got away with because line 0's underflow spilled into
// unrelated BSS, not because it was actually safe).
#define FB_WIDTH  320
#define FB_HEIGHT 240 // must cover the worst case: PAL is 240 lines, NTSC is 224
enum { VDP_LINE_PAD = 32 };
static uint8_t  vdp_line_render_buf[FB_WIDTH + VDP_LINE_PAD * 2]; // one real
                                                       // scanline's worth of
                                                       // palette indices, not
                                                       // a full frame - see
                                                       // note above
static uint16_t line_buffer[FB_WIDTH];                // one converted scanline at a time (640 bytes,
                                                       // not FB_WIDTH*FB_HEIGHT*2=153600) - pushed to
                                                       // the display immediately after each line renders

// Numworks screen is 320x240 - center the 224-tall Genesis image vertically.
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
// Y offset is computed per-frame below (screen_height varies: 224 NTSC / 240 PAL)

// ---------------------------------------------------------------------
// ROM: comes from eadk_external_data, no filesystem involved. eadk.h
// already exposes the exact byte count via eadk_external_data_size, set by
// nwlink when it packages the .bin - no header-sniffing needed.
// (eadk_external_data is declared `const char*` in eadk.h, not an array.)
// ---------------------------------------------------------------------

// ---------------------------------------------------------------------
// Input: Genesis 3-button pad needs Up/Down/Left/Right/A/B/C/Start.
// Numworks doesn't have that many free buttons - this is a starting
// mapping, expect to revise it once it's in your hands.
//   D-pad  -> arrow keys
//   A      -> OK
//   B      -> Back
//   C      -> EXE
//   Start  -> Toolbox
// ---------------------------------------------------------------------
// PAD_UP/DOWN/LEFT/RIGHT/A/B/C are already defined by gwenesis_bus.h
// (enum gwenesis_bus_pad_button) - use those directly rather than
// redeclaring them. Start's real constant name/value from that same enum
// still needs confirming (see note below) - PAD_START here is a
// placeholder only, not yet verified against the header.
#ifndef PAD_START
#define PAD_START 7 // TODO: replace with the real constant from
                     // gwenesis_bus.h once confirmed - see note in chat
#endif

// Edge-triggered: only call press/release when a key's state actually
// changes, rather than every frame regardless. old_state persists across
// calls so we can diff against it.
static eadk_keyboard_state_t poll_input(eadk_keyboard_state_t old_state)
{
    eadk_keyboard_state_t state = eadk_keyboard_scan();

    struct { int pad; eadk_key_t key; } map[] = {
        { PAD_UP,    eadk_key_up },
        { PAD_DOWN,  eadk_key_down },
        { PAD_LEFT,  eadk_key_left },
        { PAD_RIGHT, eadk_key_right },
        { PAD_A,     eadk_key_ok },
        { PAD_B,     eadk_key_back },
        { PAD_C,     eadk_key_exe },
        { PAD_START, eadk_key_toolbox },
    };

    for (int i = 0; i < 8; i++)
    {
        bool was_down = eadk_keyboard_key_down(old_state, map[i].key);
        bool is_down  = eadk_keyboard_key_down(state, map[i].key);
        if (is_down != was_down)
        {
            if (is_down)
                gwenesis_io_pad_press_button(0, map[i].pad);
            else
                gwenesis_io_pad_release_button(0, map[i].pad);
        }
    }

    return state;
}

// ---------------------------------------------------------------------
// ROM selection: list ROMs from extapp storage (storage.c) and let the
// user pick one. Falls back to the single bundled eadk_external_data ROM
// if storage has nothing (or isn't valid yet) - keeps the old install-time
// path working for anyone who hasn't sideloaded ROMs via storage.
//
// NOTE: "md" is a guess at the extension convention - adjust to whatever
// you actually name your Genesis ROM files (.md/.gen/.bin are all common).
// ---------------------------------------------------------------------
#define MAX_ROM_ENTRIES 32
static const char *rom_names[MAX_ROM_ENTRIES];

// Returns the index of the picked ROM in rom_names[], or -1 if storage had
// no ROMs (caller should fall back to eadk_external_data in that case).
static int rom_picker_select(int rom_count)
{
    int selected = 0;
    eadk_keyboard_state_t old_state = 0;

    while (true)
    {
        eadk_display_push_rect_uniform(eadk_screen_rect, eadk_color_black);

        eadk_point_t title_pt = { 8, 8 };
        eadk_display_draw_string("Select a ROM:", title_pt, true, eadk_color_white, eadk_color_black);

        for (int i = 0; i < rom_count; i++)
        {
            uint16_t row_y = (uint16_t)(40 + i * 20);
            bool is_selected = (i == selected);

            if (is_selected)
            {
                eadk_rect_t bar = { 0, row_y, EADK_SCREEN_WIDTH, 18 };
                eadk_display_push_rect_uniform(bar, eadk_color_white);
            }

            eadk_point_t row_pt = { 16, row_y };
            eadk_display_draw_string(rom_names[i], row_pt,
                                      false,
                                      is_selected ? eadk_color_black : eadk_color_white,
                                      is_selected ? eadk_color_white : eadk_color_black);
        }

        eadk_keyboard_state_t state = eadk_keyboard_scan();
        bool up_pressed   = eadk_keyboard_key_down(state, eadk_key_up)   && !eadk_keyboard_key_down(old_state, eadk_key_up);
        bool down_pressed = eadk_keyboard_key_down(state, eadk_key_down) && !eadk_keyboard_key_down(old_state, eadk_key_down);
        bool ok_pressed    = eadk_keyboard_key_down(state, eadk_key_ok)   && !eadk_keyboard_key_down(old_state, eadk_key_ok);
        old_state = state;

        if (up_pressed && selected > 0)
            selected--;
        if (down_pressed && selected < rom_count - 1)
            selected++;
        if (ok_pressed)
            return selected;

        eadk_timing_msleep(30);
    }
}

// Returns a pointer to the chosen ROM data and sets *out_size, or NULL if
// nothing could be loaded from either storage or the bundled external data.
static const char *select_rom(size_t *out_size)
{
    int rom_count = extapp_fileListWithExtension(rom_names, MAX_ROM_ENTRIES, "md");

    if (rom_count > 0)
    {
        int picked = rom_picker_select(rom_count);
        size_t len = 0;
        const char *data = extapp_fileRead(rom_names[picked], &len);
        if (data != NULL)
        {
            *out_size = len;
            return data;
        }
        // fall through to eadk_external_data if the read somehow failed
    }

    if (eadk_external_data != NULL)
    {
        *out_size = eadk_external_data_size;
        return eadk_external_data;
    }

    return NULL;
}


int main(int argc, char *argv[])
{
    eadk_display_push_rect_uniform(eadk_screen_rect, eadk_color_black);

    size_t rom_size = 0;
    const char *rom_data = select_rom(&rom_size);
    if (rom_data == NULL)
        return 1; // no ROM in storage, and none bundled at install time either

    VRAM = (unsigned char *)malloc(VRAM_MAX_SIZE); // check VRAM_MAX_SIZE fits your
                                                     // heap budget before shipping -
                                                     // static allocation may be safer
                                                     // on a memory-constrained target

    load_cartridge((void *)rom_data, rom_size);

    power_on();
    reset_emulation();

    // Audio permanently disabled: force these clocks "done" every frame below.

    eadk_keyboard_state_t old_kb_state = 0;
    int diag_frame_num = 0;

    // Raw, monotonic counter incremented once per scanline LOOP ITERATION
    // (not once per frame). Independent of whether the scanline loop or
    // the frame loop ever completes. If this stops climbing, the freeze
    // is genuinely inside a single scanline iteration (m68k_run/z80_run/
    // vdp render/etc). If it keeps climbing past when "frame 1313" was
    // last seen, the freeze is NOT there - it's either in code that runs
    // after the scanline loop finishes (hint counter/IRQ block reached
    // the bottom without incrementing scan_line somehow, or something
    // after the scanline while() but before diag_frame_num++), or the
    // display itself has stopped updating even though execution continues.
    static unsigned long scanline_ticks = 0;

    while (true)
    {
        old_kb_state = poll_input(old_kb_state);

        // Home key exits back to the Epsilon app launcher.
        if (eadk_keyboard_key_down(old_kb_state, eadk_key_home))
            break;

        // Non-blocking live frame counter - no pause, just draw and keep
        // going, so the actual failure frame number is visible in real
        // time instead of being masked by periodic blocking checkpoints.
        {
            char diag_buf[24];
            snprintf(diag_buf, sizeof(diag_buf), "d: frame %d", diag_frame_num);
            eadk_point_t diag_pt = { 8, 0 };
            eadk_display_draw_string(diag_buf, diag_pt, false, eadk_color_white, eadk_color_black);
        }

        int lines_per_frame = REG1_PAL ? LINES_PER_FRAME_PAL : LINES_PER_FRAME_NTSC;
        int hint_counter = gwenesis_vdp_regs[10];

        screen_width = REG12_MODE_H40 ? 320 : 256;
        screen_height = REG1_PAL ? 240 : 224;
        uint16_t y_offset = (uint16_t)((SCREEN_HEIGHT - screen_height) / 2);

        gwenesis_vdp_render_config();

        system_clock = 0;
        zclk = 0; // Z80 still emulated (needed for game logic on many titles,
                  // not just sound) - only the *audio chips* are disabled
        ym2612_clock = 0x1000000;   // force done -> no-op
        sn76489_clock = 0x1000000;  // force done -> no-op
        scan_line = 0;

        while (scan_line < lines_per_frame)
        {
            // Raw per-iteration tick, independent of frame completion.
            // Drawn FIRST, before anything else in the loop body, so it
            // reflects "we entered this scanline iteration" regardless of
            // what happens after. snprintf+draw is cheap relative to
            // m68k_run/z80_run, shouldn't meaningfully perturb timing.
            scanline_ticks++;
            {
                char tick_buf[24];
                snprintf(tick_buf, sizeof(tick_buf), "t:%lu sl:%d", scanline_ticks, scan_line);
                eadk_point_t tick_pt = { 8, 16 };
                eadk_display_draw_string(tick_buf, tick_pt, false, eadk_color_white, eadk_color_black);
            }

            // Non-blocking stage marker - overwritten every scanline, so
            // whatever's showing when a freeze happens tells us exactly
            // which call is stuck, without needing to guess/request more
            // files blind.
            eadk_point_t diag_stage_pt = { 200, 0 };
            eadk_display_draw_string("m68k_run..", diag_stage_pt, false, eadk_color_white, eadk_color_black);
            m68k_run(system_clock + VDP_CYCLES_PER_LINE);

            eadk_display_draw_string("z80_run...", diag_stage_pt, false, eadk_color_white, eadk_color_black);
            z80_run(system_clock + VDP_CYCLES_PER_LINE);

            eadk_display_draw_string("post-cpu..", diag_stage_pt, false, eadk_color_white, eadk_color_black);

            if (scan_line < screen_height)
            {
                // gwenesis_vdp_render_line() always computes its write
                // address as screen_buffer[scan_line * 320], assuming a
                // full-frame buffer. Offsetting by -scan_line*320 makes
                // that arithmetic land on our real one-line buffer
                // instead - see the comment above vdp_line_render_buf.
                eadk_display_draw_string("vdp_setbuf", diag_stage_pt, false, eadk_color_white, eadk_color_black);
                gwenesis_vdp_set_buffer(
                    (uint8_t *)(vdp_line_render_buf + VDP_LINE_PAD - scan_line * 320));
                eadk_display_draw_string("vdp_render", diag_stage_pt, false, eadk_color_white, eadk_color_black);
                gwenesis_vdp_render_line(scan_line);

                // Convert just this line's palette indices to RGB565 and
                // push it immediately - avoids needing a full-frame RGB565
                // buffer (was 153KB, now this reuses a 640-byte line buffer
                // every scanline instead).
                eadk_display_draw_string("rgb_conv..", diag_stage_pt, false, eadk_color_white, eadk_color_black);
                const uint8_t *src_line = vdp_line_render_buf + VDP_LINE_PAD;
                for (int x = 0; x < screen_width; x++)
                    line_buffer[x] = CRAM565[src_line[x]];

                eadk_display_draw_string("push_rect.", diag_stage_pt, false, eadk_color_white, eadk_color_black);
                eadk_rect_t line_rect = { 0, (uint16_t)(y_offset + scan_line), (uint16_t)screen_width, 1 };
                eadk_display_push_rect(line_rect, (const eadk_color_t *)line_buffer);
            }
            eadk_display_draw_string("post-vdp..", diag_stage_pt, false, eadk_color_white, eadk_color_black);

            if ((scan_line == 0) || (scan_line > screen_height))
                hint_counter = REG10_LINE_COUNTER;

            if (--hint_counter < 0)
            {
                if ((REG0_LINE_INTERRUPT != 0) && (scan_line <= screen_height))
                {
                    hint_pending = 1;
                    if ((gwenesis_vdp_status & STATUS_VIRQPENDING) == 0)
                        m68k_update_irq(4);
                }
                hint_counter = REG10_LINE_COUNTER;
            }

            scan_line++;

            if (scan_line == screen_height)
            {
                if (REG1_VBLANK_INTERRUPT != 0)
                {
                    gwenesis_vdp_status |= STATUS_VIRQPENDING;
                    m68k_set_irq(6);
                }
                z80_irq_line(1);
            }
            if (scan_line == (screen_height + 1))
                z80_irq_line(0);

            system_clock += VDP_CYCLES_PER_LINE;
        }

        diag_frame_num++;

        // Watchdog readout (frame-1313 freeze investigation): the stage
        // marker at (200,0) already proved the freeze is a genuine
        // never-returning loop inside a single m68k_run() call, so the
        // watchdog in m68kcpu.c now breaks that loop after an abnormally
        // high iteration count and records where it was stuck. Once
        // tripped, m68k_run() returns (instead of hanging forever), so
        // this frame loop keeps advancing and this text becomes visible
        // and stays showing the trip evidence - emulation state past
        // this point is garbage/diagnostic-only, that's expected and fine.
        {
            char wd_buf[40];
            if (gwenesis_diag_stuck_tripped)
                snprintf(wd_buf, sizeof(wd_buf), "WD ir:%04x pc:%06x it:%u",
                          gwenesis_diag_stuck_ir, gwenesis_diag_stuck_pc,
                          gwenesis_diag_stuck_iters);
            else
                snprintf(wd_buf, sizeof(wd_buf), "wd: ok");
            eadk_point_t wd_pt = { 8, 232 };
            eadk_display_draw_string(wd_buf, wd_pt, false, eadk_color_white, eadk_color_black);

            // Address-error diagnostic (survives longjmp, unlike the
            // watchdog above) - drawn on its own line since this is the
            // one that should actually explain "wd: ok" during a freeze.
            char aerr_buf[40];
            snprintf(aerr_buf, sizeof(aerr_buf), "AE n:%u a:%06x pc:%06x",
                      gwenesis_diag_aerr_count, gwenesis_diag_aerr_first_addr,
                      gwenesis_diag_aerr_first_pc);
            eadk_point_t aerr_pt = { 8, 216 };
            eadk_display_draw_string(aerr_buf, aerr_pt, false, eadk_color_white, eadk_color_black);
        }

        m68k.cycles -= system_clock;
    }

    return 0;
}