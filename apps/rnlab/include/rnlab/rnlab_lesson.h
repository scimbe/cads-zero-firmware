/*
 * CaDS Zero - the interface between the `lab` dispatcher (src/rnlab.c) and
 * the eleven lessons.
 *
 * `lab NN <cmd> [args]` calls rnlab_lNN_command(session, argc, argv) with
 * argv[0] = <cmd> and argc == 0 when only `lab NN` was typed. argv points
 * into a line buffer that lives only for the duration of the call. Output
 * goes through cads_cli_write()/cads_cli_write_uint() on `session`, so it
 * reaches whichever transport (UART or TCP :4242) typed the command.
 *
 * Each lesson implements its handler in apps/rnlab/src/lNN_<slug>.c (board
 * only - may use lwIP) and keeps its pure, host-testable logic in
 * lNN_<slug>_logic.c/.h. See apps/rnlab/README.md.
 */

#ifndef RNLAB_LESSON_H
#define RNLAB_LESSON_H

#include "cads/cli/cli.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*rnlab_lesson_fn)(cads_cli_session_t* session, int argc, char* argv[]);

void rnlab_l01_command(cads_cli_session_t* session, int argc, char* argv[]);
void rnlab_l02_command(cads_cli_session_t* session, int argc, char* argv[]);
void rnlab_l03_command(cads_cli_session_t* session, int argc, char* argv[]);
void rnlab_l04_command(cads_cli_session_t* session, int argc, char* argv[]);
void rnlab_l05_command(cads_cli_session_t* session, int argc, char* argv[]);
void rnlab_l06_command(cads_cli_session_t* session, int argc, char* argv[]);
void rnlab_l07_command(cads_cli_session_t* session, int argc, char* argv[]);
void rnlab_l08_command(cads_cli_session_t* session, int argc, char* argv[]);
void rnlab_l09_command(cads_cli_session_t* session, int argc, char* argv[]);
void rnlab_l10_command(cads_cli_session_t* session, int argc, char* argv[]);
void rnlab_l11_command(cads_cli_session_t* session, int argc, char* argv[]);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_LESSON_H */
