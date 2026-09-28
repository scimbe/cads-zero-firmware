/*
 * CaDS Zero - rnlab L11 (Wetter-App): board integration.
 *
 * `lab 11 <cmd> [args]` lands in rnlab_l11_command() (see
 * rnlab/rnlab_lesson.h). Board only - may use lwIP and the hooks in
 * cads/net/rnlab_hooks.h; pure logic belongs in l11_wetter_app_logic.c.
 */

#include "rnlab/rnlab_lesson.h"

#include "l11_wetter_app_logic.h"

void rnlab_l11_command(cads_cli_session_t* session, int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    cads_cli_write(session, "L11: noch nicht implementiert\r\n");
}
