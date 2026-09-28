/*
 * CaDS Zero - rnlab L09 (Congestion Control): board integration.
 *
 * `lab 09 <cmd> [args]` lands in rnlab_l09_command() (see
 * rnlab/rnlab_lesson.h). Board only - may use lwIP and the hooks in
 * cads/net/rnlab_hooks.h; pure logic belongs in l09_congestion_control_logic.c.
 */

#include "rnlab/rnlab_lesson.h"

#include "l09_congestion_control_logic.h"

void rnlab_l09_command(cads_cli_session_t* session, int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    cads_cli_write(session, "L09: noch nicht implementiert\r\n");
}
