/*
 * CaDS Zero - rnlab L07 (UDP-Transport): board integration.
 *
 * `lab 07 <cmd> [args]` lands in rnlab_l07_command() (see
 * rnlab/rnlab_lesson.h). Board only - may use lwIP and the hooks in
 * cads/net/rnlab_hooks.h; pure logic belongs in l07_udp_transport_logic.c.
 */

#include "rnlab/rnlab_lesson.h"

#include "l07_udp_transport_logic.h"

void rnlab_l07_command(cads_cli_session_t* session, int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    cads_cli_write(session, "L07: noch nicht implementiert\r\n");
}
