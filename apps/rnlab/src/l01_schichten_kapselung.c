/*
 * CaDS Zero - rnlab L01 (Schichten und Kapselung): board integration.
 *
 * `lab 01 <cmd> [args]` lands in rnlab_l01_command() (see
 * rnlab/rnlab_lesson.h). Board only - may use lwIP and the hooks in
 * cads/net/rnlab_hooks.h; pure logic belongs in l01_schichten_kapselung_logic.c.
 */

#include "rnlab/rnlab_lesson.h"

#include "l01_schichten_kapselung_logic.h"

void rnlab_l01_command(cads_cli_session_t* session, int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    cads_cli_write(session, "L01: noch nicht implementiert\r\n");
}
