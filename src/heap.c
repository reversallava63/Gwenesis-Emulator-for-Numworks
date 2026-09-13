// Custom _sbrk() for the EADK build.
//
// newlib's malloc() calls _sbrk() to grow the heap. nosys.specs' default
// _sbrk() implementation (libgloss/libnosys/sbrk.c) expects a linker-script
// symbol called `end` marking the start of free RAM after .bss - but this
// project links with a plain `-Wl,--relocatable` partial link and no linker
// script, so `end` is never defined. That surfaces as an undefined-reference
// error at the browser simulator's full-link step (not locally, because
// --relocatable defers unresolved symbols - see PROJECT_STATUS.md).
//
// Fix: don't depend on `end` at all. Give malloc() its own fixed-size static
// array to carve up instead.
//
// Only real caller today is `VRAM = malloc(VRAM_MAX_SIZE)` in main.c
// (VRAM_MAX_SIZE == 0x10000 == 64KB, from gwenesis_vdp.h). Sized with some
// headroom above that. Raise HEAP_SIZE if future allocations need more, but
// remember every byte here is static BSS - it counts directly against the
// ~320KB RAM budget alongside the framebuffer, line_buffer, and CPU core
// tables.
#include <stddef.h>
#include <errno.h>

#define HEAP_SIZE (68 * 1024)

static unsigned char heap[HEAP_SIZE];
static unsigned char *heap_ptr = heap;

void *_sbrk(int incr)
{
    unsigned char *prev = heap_ptr;

    if ((heap_ptr + incr) > (heap + HEAP_SIZE) || (heap_ptr + incr) < heap)
    {
        errno = ENOMEM;
        return (void *)-1;
    }

    heap_ptr += incr;
    return (void *)prev;
}
