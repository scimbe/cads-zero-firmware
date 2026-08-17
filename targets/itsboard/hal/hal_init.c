/*
 * CaDS Zero - bring-up order for the ITSboard.
 *
 * The order is not arbitrary. Time first, because every driver below it uses
 * delays. Console next, so that anything which fails afterwards can say so.
 * Then the bus, then the two devices hanging off it.
 */

#include "board.h"
#include "cads_hal.h"
#include "hal_spi.h"

void cads_hal_time_init(void);
void cads_hal_io_init(void);
void cads_hal_touch_init(void);

void cads_hal_init(void) {
    cads_hal_time_init();
    cads_hal_io_init();
    cads_hal_console_init(CADS_CONSOLE_BAUD);
    cads_hal_spi_init();
    cads_hal_touch_init();
    cads_hal_display_init();
}
