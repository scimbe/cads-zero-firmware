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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cads/cli/cli.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Place a large, CPU-only lesson buffer in CCM instead of the tight SRAM
 * budget: `RNLAB_CCM static uint8_t buf[2048];`. CCM is NOT zeroed at boot
 * (initialise it yourself) and NEVER a DMA target - fine for anything lwIP
 * or the CPU copies into, wrong for anything handed to the Ethernet or
 * display DMA. Empty on the host, where the lesson logic is tested. */
#if defined(__arm__)
#define RNLAB_CCM __attribute__((section(".ccm")))
#else
#define RNLAB_CCM
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

/*
 * Per-lesson network hooks. The framework's dispatcher (src/rnlab_hooks.c)
 * implements the driver's rnlab_hook_*() (cads/net/rnlab_hooks.h) by calling
 * these for lesson 01..11 in order; each has a weak no-op default there. A
 * lesson implements only the ones it needs, in its own lNN_<slug>.c, with
 * exactly these signatures and without `weak`:
 *
 *   rx_frame  every received Ethernet frame (from the destination MAC, no FCS)
 *   rx_drop   true = discard it before lwIP; ORed over all lessons
 *   tx_frame  every frame lwIP hands to the driver
 *   tx_drop   true = do not send it (lwIP thinks it was); ORed over all lessons
 *   ip4_input LWIP_HOOK_IP4_INPUT; non-zero = consumed (you pbuf_free(p)),
 *             the first lesson returning non-zero wins
 *
 * Board only, called from inside cads_net_poll()/lwIP: keep them short.
 */
struct pbuf;
struct netif;

#define RNLAB_DECLARE_LESSON_HOOKS(nn)                                           \
    void rnlab_l##nn##_hook_rx_frame(const uint8_t* frame, size_t len);          \
    bool rnlab_l##nn##_hook_rx_drop(const uint8_t* frame, size_t len);           \
    void rnlab_l##nn##_hook_tx_frame(const uint8_t* frame, size_t len);          \
    bool rnlab_l##nn##_hook_tx_drop(const uint8_t* frame, size_t len);           \
    int rnlab_l##nn##_hook_ip4_input(struct pbuf* p, struct netif* inp);

RNLAB_DECLARE_LESSON_HOOKS(01)
RNLAB_DECLARE_LESSON_HOOKS(02)
RNLAB_DECLARE_LESSON_HOOKS(03)
RNLAB_DECLARE_LESSON_HOOKS(04)
RNLAB_DECLARE_LESSON_HOOKS(05)
RNLAB_DECLARE_LESSON_HOOKS(06)
RNLAB_DECLARE_LESSON_HOOKS(07)
RNLAB_DECLARE_LESSON_HOOKS(08)
RNLAB_DECLARE_LESSON_HOOKS(09)
RNLAB_DECLARE_LESSON_HOOKS(10)
RNLAB_DECLARE_LESSON_HOOKS(11)

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_LESSON_H */
