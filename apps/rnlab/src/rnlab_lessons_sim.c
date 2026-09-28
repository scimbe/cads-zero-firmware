/*
 * CaDS Zero - lesson handlers on the simulator.
 *
 * The lessons' board files (lNN_<slug>.c) use lwIP, which the host build
 * does not have (modules/net/src/cads_net_sim.c) - so the simulator gets
 * these honest stubs instead, the same board/sim split as the explorer's
 * *_sim.c files. The lessons' pure logic is still tested on the host,
 * through tests/unit/test_rnlab_lNN.c.
 */

#include "rnlab/rnlab_lesson.h"

#define RNLAB_SIM_LESSON(nn)                                                          \
    void rnlab_l##nn##_command(cads_cli_session_t* session, int argc, char* argv[]) { \
        (void)argc;                                                                   \
        (void)argv;                                                                   \
        cads_cli_write(session, "L" #nn ": nur auf dem Board verfuegbar\r\n");       \
    }

RNLAB_SIM_LESSON(01)
RNLAB_SIM_LESSON(02)
RNLAB_SIM_LESSON(03)
RNLAB_SIM_LESSON(04)
RNLAB_SIM_LESSON(05)
RNLAB_SIM_LESSON(06)
RNLAB_SIM_LESSON(07)
RNLAB_SIM_LESSON(08)
RNLAB_SIM_LESSON(09)
RNLAB_SIM_LESSON(10)
RNLAB_SIM_LESSON(11)
