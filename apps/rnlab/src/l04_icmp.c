/*
 * CaDS Zero - rnlab L04 (ICMP): board integration.
 *
 * `lab 04 <cmd> [args]` lands in rnlab_l04_command() (see
 * rnlab/rnlab_lesson.h). Board only - may use lwIP and the hooks in
 * cads/net/rnlab_hooks.h; pure logic belongs in l04_icmp_logic.c.
 */

#include "rnlab/rnlab_lesson.h"

#include "l04_icmp_logic.h"

void rnlab_l04_command(cads_cli_session_t* session, int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    cads_cli_write(session, "L04: noch nicht implementiert\r\n");
}
