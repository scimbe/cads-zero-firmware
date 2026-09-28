/*
 * CaDS Zero - rnlab L05 (DHCP): board integration.
 *
 * `lab 05 <cmd> [args]` lands in rnlab_l05_command() (see
 * rnlab/rnlab_lesson.h). Board only - may use lwIP and the hooks in
 * cads/net/rnlab_hooks.h; pure logic belongs in l05_dhcp_logic.c.
 */

#include "rnlab/rnlab_lesson.h"

#include "l05_dhcp_logic.h"

void rnlab_l05_command(cads_cli_session_t* session, int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    cads_cli_write(session, "L05: noch nicht implementiert\r\n");
}
