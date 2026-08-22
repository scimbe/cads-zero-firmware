#ifndef CADS_EXPLORER_CAPTURE_BUFFER_H
#define CADS_EXPLORER_CAPTURE_BUFFER_H

#include <stdint.h>

/**
 * One shared, full-frame-size (1536 B) static buffer for the explorer's
 * promiscuous-capture commands (currently `C` sniff, `M` mactable, `N`
 * l2discover).
 *
 * WHY SHARED RATHER THAN ONE PER COMMAND
 * -----------------------------------------
 * explorer_gui_demo.c/explorer_app_demo.c already established "one owner
 * at a time" for the display; the explorer REPL itself enforces the same
 * thing for every command it dispatches - only one `case` runs, ever,
 * before returning to read the next line. Each capture command
 * previously declared its own `static uint8_t frame[1536]`, which is
 * correct in isolation but adds up: three such buffers cost 3x the RAM
 * of the one that is ever actually live, on a board where
 * targets/itsboard/linker/cads_itsboard.ld's own
 * ASSERT(__cads_heap_size >= 48K, ...) already has the whole firmware
 * living within single-digit-percent margin of that floor (see
 * scripts/check_ram_budget.py's own file header). Adding this command's
 * own third private buffer is what pushed the link past that floor for
 * the first time - real, measured, not a hypothetical.
 *
 * A caller must NOT hold a pointer from this function across a call into
 * another command's capture loop - there is no reentrancy or locking
 * here, by design, for the same reason the display's "one owner"
 * convention has none: the explorer's own single-command-at-a-time
 * dispatch is the only synchronisation this needs.
 */
#define CADS_EXPLORER_CAPTURE_BUFFER_SIZE 1536u

uint8_t* cads_explorer_capture_buffer(void);

#endif /* CADS_EXPLORER_CAPTURE_BUFFER_H */
