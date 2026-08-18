/*
 * gui/canvas.c, compiled for the host.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * canvas.c places its framebuffer and staging buffers in the .dmaram linker
 * section. Mach-O has no such section and clang rejects the attribute outright
 * ("mach-o section specifier requires a segment and section separated by a
 * comma"), so on macOS the file does not compile at all - not for these tests
 * and not for targets/sim either.
 *
 * The placement is load bearing on the board (CCM has no DMA access), so the
 * fix belongs in canvas.c as a guard on __arm__ or on a CADS_DMARAM macro, not
 * here. Until that lands, this neutralises __attribute__ for canvas.c only:
 * every system header and every project header is pulled in first, so the
 * macro is defined after everything that legitimately uses attributes has
 * already been parsed, and it applies to exactly one translation unit.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "canvas.h"

#ifdef __APPLE__
#define __attribute__(unused_attributes)
#endif

#include "canvas.c"
