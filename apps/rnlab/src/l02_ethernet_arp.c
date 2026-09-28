/*
 * CaDS Zero - rnlab L02 (Ethernet und ARP): board integration.
 *
 * `lab 02 <cmd> [args]` lands in rnlab_l02_command() (see
 * rnlab/rnlab_lesson.h). Board only - may use lwIP and the hooks in
 * cads/net/rnlab_hooks.h; pure logic belongs in l02_ethernet_arp_logic.c.
 */

#include "rnlab/rnlab_lesson.h"

#include "l02_ethernet_arp_logic.h"

void rnlab_l02_command(cads_cli_session_t* session, int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    cads_cli_write(session, "L02: noch nicht implementiert\r\n");
}
