/*
 * Timing helpers for Gwenesis EADK
 *
 * Stripped down from the original draft: osd_installtimer() and
 * osd_nofrendo_ticks() were nofrendo's (NES core) timer-hook API - nothing
 * in gwenesis calls them, so they've been removed. What's left are plain
 * EADK timing wrappers, kept around for a future FPS counter or frame-pacing
 * logic if you want one later.
 */
 
#include <eadk.h>
#include <stdint.h>
 
static uint64_t g_start_ms = 0;
 
void timing_init(void)
{
    g_start_ms = eadk_timing_millis();
}
 
uint64_t timing_get_ms(void)
{
    return eadk_timing_millis();
}
 
void timing_delay_ms(uint32_t ms)
{
    eadk_timing_msleep(ms);
}