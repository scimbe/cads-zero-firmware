/*
 * CaDS Zero - rnlab L03 (IPv4 und Subnetting): board integration.
 *
 * `lab 03 <cmd> [args]` lands in rnlab_l03_command() (see
 * rnlab/rnlab_lesson.h). Board only - may use lwIP and the hooks in
 * cads/net/rnlab_hooks.h; pure logic belongs in l03_ipv4_subnetting_logic.c.
 */

#include "rnlab/rnlab_lesson.h"

#include "l03_ipv4_subnetting_logic.h"

void rnlab_l03_command(cads_cli_session_t* session, int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    cads_cli_write(session, "L03: noch nicht implementiert\r\n");
}
