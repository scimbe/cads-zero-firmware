/*
 * CaDS Zero - rnlab L08 (TCP-Flusskontrolle): board integration.
 *
 * `lab 08 <cmd> [args]` lands in rnlab_l08_command() (see
 * rnlab/rnlab_lesson.h). Board only - may use lwIP and the hooks in
 * cads/net/rnlab_hooks.h; pure logic belongs in l08_tcp_flusskontrolle_logic.c.
 */

#include "rnlab/rnlab_lesson.h"

#include "l08_tcp_flusskontrolle_logic.h"

void rnlab_l08_command(cads_cli_session_t* session, int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    cads_cli_write(session, "L08: noch nicht implementiert\r\n");
}
