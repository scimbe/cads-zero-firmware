/*
 * CaDS Zero - rnlab L06 (DNS und NAT): board integration.
 *
 * `lab 06 <cmd> [args]` lands in rnlab_l06_command() (see
 * rnlab/rnlab_lesson.h). Board only - may use lwIP and the hooks in
 * cads/net/rnlab_hooks.h; pure logic belongs in l06_dns_nat_logic.c.
 */

#include "rnlab/rnlab_lesson.h"

#include "l06_dns_nat_logic.h"

void rnlab_l06_command(cads_cli_session_t* session, int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    cads_cli_write(session, "L06: noch nicht implementiert\r\n");
}
