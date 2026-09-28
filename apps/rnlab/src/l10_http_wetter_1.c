/*
 * CaDS Zero - rnlab L10 (HTTP-Client (Wetter 1)): board integration.
 *
 * `lab 10 <cmd> [args]` lands in rnlab_l10_command() (see
 * rnlab/rnlab_lesson.h). Board only - may use lwIP and the hooks in
 * cads/net/rnlab_hooks.h; pure logic belongs in l10_http_wetter_1_logic.c.
 */

#include "rnlab/rnlab_lesson.h"

#include "l10_http_wetter_1_logic.h"

void rnlab_l10_command(cads_cli_session_t* session, int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    cads_cli_write(session, "L10: noch nicht implementiert\r\n");
}
