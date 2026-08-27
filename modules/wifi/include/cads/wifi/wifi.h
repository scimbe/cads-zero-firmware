/*
 * CaDS Zero - WiFi via an ESP32 co-processor, PPPoS over USART6.
 *
 * The ESP32 owns the WiFi radio and its own IP stack; this firmware only
 * ever sees a point-to-point serial link. On top of that raw byte stream:
 *
 *   1. A one-line plaintext bootstrap tells the ESP32 which network to join
 *      (see cads_wifi_connect()'s doc comment for the exact line format).
 *   2. Once the ESP32 answers "WIFI OK", the link switches to PPP framing
 *      (RFC 1661/1662, lwIP's PPPoS) and this module never touches the raw
 *      bytes again - lwIP owns them from there.
 *   3. PPP's IPCP phase gives this board a real IP address and the ESP32
 *      NAT-forwards it onto the WiFi network, so every existing IP-based
 *      tool (ping, ARP is Ethernet-only so it does NOT apply here, iperf,
 *      the HTTP status page...) works over this netif unmodified once it is
 *      the route to a given destination.
 *
 * See docs/reference/wifi-coprocessor.md for the full protocol and the
 * ESP32-side firmware that implements the other end of it - NOT verified on
 * hardware as of this writing (no ESP32 wired to the board yet). This module
 * builds and is exercised on the host (docs/reference/wifi-coprocessor.md's
 * "What is and isn't tested" section is explicit about the boundary).
 */
#ifndef CADS_WIFI_H
#define CADS_WIFI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CADS_WIFI_IDLE = 0,       /**< not connecting; init() called, nothing else   */
    CADS_WIFI_PROVISIONING,   /**< bootstrap line sent, waiting for the ESP32    */
    CADS_WIFI_NEGOTIATING,    /**< ESP32 acked; PPP LCP/IPCP under way           */
    CADS_WIFI_UP,             /**< PPP link up, netif has an address            */
    CADS_WIFI_FAILED,         /**< provisioning or PPP negotiation failed        */
} cads_wifi_state_t;

typedef struct {
    cads_wifi_state_t state;
    uint32_t local_ip;  /**< host byte order; 0 unless state == CADS_WIFI_UP */
    uint32_t peer_ip;   /**< the ESP32's address on this link, same terms    */
    uint32_t fail_count; /**< PPP link drops/failures since init(), for the UI */
} cads_wifi_status_t;

/** One-time setup: brings up the USART6 link and the (not-yet-connected) PPP
 *  netif. Safe to call whether or not an ESP32 is actually wired - with
 *  nothing on the other end, cads_wifi_connect() simply times out. */
void cads_wifi_init(void);

/**
 * Start joining `ssid`/`password`. Sends the bootstrap line
 *     WIFI SSID="<ssid>" PASS="<password>"\r\n
 * (quotes inside either value are rejected up front - see the .c file) and
 * arms a timeout waiting for "WIFI OK\r\n". Non-blocking: call
 * cads_wifi_tick() regularly afterward and watch cads_wifi_status() for the
 * state to leave CADS_WIFI_PROVISIONING / CADS_WIFI_NEGOTIATING.
 *
 * `ssid` must be non-empty; `password` may be empty (open network) but not
 * NULL. Returns false without sending anything if either string would not
 * fit the wire format (an embedded '"' or a length past
 * CADS_CONFIG_SSID_MAX/CADS_CONFIG_PASS_MAX).
 */
bool cads_wifi_connect(const char* ssid, const char* password);

/** Tear down the PPP link (if any) and return to CADS_WIFI_IDLE. Does not
 *  tell the ESP32 to disassociate from WiFi - it only ends the PPP session
 *  on this side; a future protocol version may add an explicit "WIFI OFF"
 *  line for that. */
void cads_wifi_disconnect(void);

/** Drains the UART link and drives the bootstrap/PPP state machine forward.
 *  Call at the same cadence as cads_net_poll() (both are cheap when idle -
 *  a UART ring drain and, once up, lwIP's own PPP timers). */
void cads_wifi_tick(uint32_t now_ms);

void cads_wifi_status(cads_wifi_status_t* out);

#ifdef __cplusplus
}
#endif

#endif /* CADS_WIFI_H */
