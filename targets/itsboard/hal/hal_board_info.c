/*
 * CaDS Zero - what this board is, as data.
 *
 * Everything above the HAL reads capabilities from here rather than testing
 * for a particular board, so porting to different hardware means writing a new
 * descriptor and a new HAL, not editing the GUI.
 */

#include "board.h"
#include "cads_hal.h"

static const cads_board_info_t cads_itsboard_info = {
    .board_name = "ITSboard (NUCLEO-F429ZI + ITS adapter + Waveshare 4\")",
    .mcu_name = "STM32F429ZI",
    .cpu_hz = CADS_SYSCLK_HZ,

    .display_width = CADS_LCD_WIDTH,
    .display_height = CADS_LCD_HEIGHT,

    /* The shield drives the panel through a shift register chain with no
     * return path, so nothing can ever be read back. Code that would otherwise
     * do a read-modify-write on video memory has to keep its own copy. */
    .display_readable = false,

    .button_count = 8, /* S0..S7 on PF0..PF7 */
    .has_touch = true,
    .has_network = true,
    .has_storage = true, /* littlefs in flash bank 2 */

    .flash_bytes = 1024u * 1024u, /* bank 1; bank 2 is the filesystem */
    .ram_bytes = 192u * 1024u,    /* DMA capable; the 64 KB CCM is separate */

    /* Measured, not derived: 448 233 us for 153 600 pixels at the /16 divider.
     * The theoretical maximum for this bus is 351 000, so the driver is at
     * 97% of what the hardware can carry. */
    .display_pixels_per_second = 342000u,
};

const cads_board_info_t* cads_hal_board_info(void) {
    return &cads_itsboard_info;
}
