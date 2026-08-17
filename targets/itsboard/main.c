/*
 * CaDS Zero - firmware entry point for the ITSboard.
 *
 * Everything interesting lives above the HAL; this file exists only to bring
 * the hardware up and hand control to the portable application, which the
 * simulator runs verbatim.
 */

#include "bringup/bringup.h"
#include "cads_hal.h"

int main(void) {
    cads_hal_init();
    cads_bringup_run();

    /* cads_bringup_run() does not return today. When the scheduler lands in M2
     * this becomes the idle path. */
    for(;;) {
        __asm volatile("wfi");
    }
}
