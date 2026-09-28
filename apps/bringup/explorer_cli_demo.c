/*
 * CaDS Zero - cads_cli reachable over the serial console, driven straight
 * from the hardware explorer's UART for `seconds`.
 *
 * Safe alongside the explorer's own line loop only because this function
 * takes exclusive ownership of cads_hal_console_read() for its entire
 * bounded run and gives it back on return - the same one-owner-at-a-time
 * discipline explorer_gui_demo.c/explorer_app_demo.c already follow for
 * the display and input.
 *
 * Fully portable (no board/sim split): cads_hal_console_read/write() are
 * both already implemented on the simulator (the explorer's own main loop
 * builds for both targets), and cads/net/net.h + cads/cli/cli_tcp.h have
 * honest simulator stubs of their own, so this file needs none.
 */

#include "explorer_cli_demo.h"

#include "cads/cli/cli.h"
#include "cads/cli/cli_tcp.h"
#include "cads/net/net.h"
#include "cads_hal.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "input_probe.h"

#define CADS_CLI_DEMO_TCP_PORT 4242u

static void cads_cli_demo_write(void* context, const char* text, size_t length) {
    (void)context;
    cads_hal_console_write(text, length);
}

void cads_explorer_cli_demo(uint32_t seconds) {
    if(!seconds) seconds = 30u;

    cads_net_init(cads_explorer_net_mac());
    bool tcp_started = cads_cli_tcp_start(CADS_CLI_DEMO_TCP_PORT);
    cads_probe_puts(tcp_started ? "# cli: TCP listener on port 4242\r\n" :
                                   "# cli: TCP not available (simulator, or bind failed)\r\n");

    cads_cli_session_t session;
    cads_cli_session_init(&session, cads_cli_demo_write, NULL);
    cads_probe_puts("# cli: serial session for ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s - 'help' for commands\r\n> ");

    uint32_t start = cads_hal_ticks_ms();
    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        cads_net_poll();
        cads_cli_tcp_service(); /* TCP commands run here, not inside lwIP (cli_tcp.h) */

        uint8_t byte;
        if(cads_hal_console_read(&byte)) {
            cads_cli_session_feed(&session, byte);
        } else {
            cads_hal_delay_ms(2u);
        }
    }

    cads_probe_puts("\r\n# cli: serial session ended\r\n");
}
