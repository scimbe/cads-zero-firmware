/*
 * CaDS Zero - `lab selftest` on the board.
 *
 * Timer headroom: holds RNLAB_LESSON_TIMEOUTS (lwipopts.h) sys_timeout()s at
 * once - every lesson's timer budget together - on top of lwIP's own cyclic
 * timers, then releases them. If the pool were too small this would end in
 * lwIP's "pool MEMP_SYS_TIMEOUT is empty" assert (a panic), which is exactly
 * what the lessons must never hit; reaching the report line is the pass.
 */

#include "rnlab_selftest.h"

#include "lwip/memp.h"
#include "lwip/stats.h"
#include "lwip/timeouts.h"

static void rnlab_selftest_timeout(void* arg) {
    (void)arg; /* never fires: removed long before its 60 s are up */
}

void rnlab_selftest(cads_cli_session_t* session) {
    static uint8_t tags[RNLAB_LESSON_TIMEOUTS];
    for(uint32_t i = 0; i < RNLAB_LESSON_TIMEOUTS; i++) {
        sys_timeout(60000u, rnlab_selftest_timeout, &tags[i]);
    }

    cads_cli_write(session, "timer: ");
    cads_cli_write_uint(session, RNLAB_LESSON_TIMEOUTS);
    cads_cli_write(session, " Lektions-Timer gleichzeitig belegt, Pool ");
#if MEMP_STATS
    const struct stats_mem* pool = lwip_stats.memp[MEMP_SYS_TIMEOUT];
    cads_cli_write_uint(session, pool->used);
    cads_cli_write(session, "/");
    cads_cli_write_uint(session, pool->avail);
    cads_cli_write(session, " belegt, max ");
    cads_cli_write_uint(session, pool->max);
#else
    cads_cli_write_uint(session, MEMP_NUM_SYS_TIMEOUT);
    cads_cli_write(session, " Plaetze");
#endif

    for(uint32_t i = 0; i < RNLAB_LESSON_TIMEOUTS; i++) {
        sys_untimeout(rnlab_selftest_timeout, &tags[i]);
    }
    cads_cli_write(session, " - OK\r\n");
}
