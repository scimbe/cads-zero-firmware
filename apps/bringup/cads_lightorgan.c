/* CaDS Zero - light organ implementation. See the header for what this is
 * and why. A 3-bit-wide "comet" bounces across OUT0..OUT15, one step every
 * CADS_LIGHTORGAN_STEP_MS while the link is active - independent of the
 * 10 ms app-tree tick rate (apps/bringup/explorer_app_demo.c), so the sweep
 * speed does not change if that loop's own cadence ever does. */
#include "cads_lightorgan.h"

#include <stdbool.h>

#include "cads_hal.h"
#include "cads_marauder.h"

#define CADS_LIGHTORGAN_WIDTH    16
#define CADS_LIGHTORGAN_COMET    3  /* lit outputs at once */
#define CADS_LIGHTORGAN_STEP_MS  40u /* ~25 steps/s - a lively but readable sweep */

static uint32_t s_next_step_ms;
static int8_t s_pos;
static int8_t s_dir = 1;
static bool s_was_active;

void cads_lightorgan_tick(uint32_t now_ms) {
    if(!cads_marauder_link_active()) {
        if(s_was_active) {
            cads_hal_adapter_outputs(0u);
            s_was_active = false;
            s_pos = 0;
            s_dir = 1;
        }
        return;
    }
    s_was_active = true;

    if((int32_t)(now_ms - s_next_step_ms) < 0) return;
    s_next_step_ms = now_ms + CADS_LIGHTORGAN_STEP_MS;

    uint16_t pattern = (uint16_t)(((1u << CADS_LIGHTORGAN_COMET) - 1u) << s_pos);
    cads_hal_adapter_outputs(pattern);

    s_pos = (int8_t)(s_pos + s_dir);
    if(s_pos >= CADS_LIGHTORGAN_WIDTH - CADS_LIGHTORGAN_COMET) {
        s_pos = CADS_LIGHTORGAN_WIDTH - CADS_LIGHTORGAN_COMET;
        s_dir = -1;
    } else if(s_pos <= 0) {
        s_pos = 0;
        s_dir = 1;
    }
}
