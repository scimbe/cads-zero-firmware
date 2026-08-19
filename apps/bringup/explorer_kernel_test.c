/*
 * CaDS Zero - on-target proof that cads_timer and cads_event work under the
 * real scheduler. See explorer_kernel_test.h for why this is not a unit test.
 */

#include "explorer_kernel_test.h"

#include "cads/kernel/kernel.h"
#include "cads_hal.h"
#include "input_probe.h"

#define CADS_TEST_EVENT_BIT (1u << 0)

static cads_timer_t s_test_timer;
static cads_event_t s_test_event;
static volatile uint32_t s_callback_task_marker;

static void cads_test_timer_fired(void* context) {
    (void)context;
    /* Distinguishes "the callback ran on the timer service task" from "the
     * caller polled a flag itself and this never really left its thread" -
     * a value only the callback writes, checked by the waiter afterwards. */
    s_callback_task_marker = 0xC0FFEEu;
    cads_event_set(&s_test_event, CADS_TEST_EVENT_BIT);
}

void cads_explorer_kernel_test(void) {
    cads_probe_puts("# kernel test: timer + event under the scheduler\r\n");

    cads_event_init(&s_test_event);
    s_callback_task_marker = 0u;

    cads_timer_init(
        &s_test_timer, "test", 200u, false /* one-shot */, cads_test_timer_fired, NULL);

    uint32_t start = cads_hal_ticks_ms();
    bool started = cads_timer_start(&s_test_timer, 1000u);
    cads_probe_puts(started ? "# timer started\r\n" : "# timer FAILED to start\r\n");

    uint32_t bits = cads_event_wait(&s_test_event, CADS_TEST_EVENT_BIT, true, true, 2000u);
    uint32_t elapsed = cads_hal_ticks_ms() - start;

    bool signalled = (bits & CADS_TEST_EVENT_BIT) != 0u;
    bool timing_ok = elapsed >= 150u && elapsed <= 500u; /* 200 ms +/- scheduling slop */
    bool marker_ok = s_callback_task_marker == 0xC0FFEEu;

    cads_probe_puts("# event bits=0x");
    /* Reuse the explorer's existing hex helper indirectly is not available
     * here, so this stays decimal - the only bit that can be set is 0x1. */
    cads_probe_put_uint(bits);
    cads_probe_puts(" elapsed=");
    cads_probe_put_uint(elapsed);
    cads_probe_puts("ms marker=");
    cads_probe_put_uint(s_callback_task_marker);
    cads_probe_puts("\r\n");

    bool ok = started && signalled && timing_ok && marker_ok;
    cads_probe_puts(ok ? "# kernel test: PASS\r\n" : "# kernel test: FAIL\r\n");

    /* Leftover state must not leak into a second run of this command. */
    cads_timer_stop(&s_test_timer, 100u);
}
