/*
 * CaDS Zero - WiFi co-processor link, host stub.
 *
 * The host has no ESP32 and, unlike modules/net's Ethernet path, no lwIP
 * netif at all to stand in for one (modules/net/CMakeLists.txt only builds
 * cads_lwip - and therefore only links PPP's sources - on the board target).
 * This file is the honest "no co-processor attached" behaviour: init() and
 * disconnect() are no-ops, connect() always ends in CADS_WIFI_FAILED once
 * tick() has been called enough times to look like a real timeout, and
 * status() never reports an address. Exists so code that calls into
 * modules/wifi (a future Settings row, say) builds and is exercisable on the
 * host - see targets/sim/hal_sim.c's own WiFi-UART stub for the layer below
 * this one.
 */
#include "cads/wifi/wifi.h"

#include "cads_hal.h"

static cads_wifi_state_t s_state = CADS_WIFI_IDLE;
static uint32_t s_fail_count;
static uint32_t s_provision_deadline_ms;

/* No real handshake to time out on the host - a few ticks is enough to
 * exercise the PROVISIONING -> FAILED transition without a real clock wait
 * dominating a test run. */
#define CADS_WIFI_SIM_TIMEOUT_MS 50u

void cads_wifi_init(void) {
    s_state = CADS_WIFI_IDLE;
    s_fail_count = 0u;
}

bool cads_wifi_connect(const char* ssid, const char* password) {
    if(ssid == NULL || ssid[0] == '\0' || password == NULL) return false;
    s_state = CADS_WIFI_PROVISIONING;
    s_provision_deadline_ms = cads_hal_ticks_ms() + CADS_WIFI_SIM_TIMEOUT_MS;
    return true;
}

void cads_wifi_disconnect(void) {
    s_state = CADS_WIFI_IDLE;
}

void cads_wifi_tick(uint32_t now_ms) {
    if(s_state == CADS_WIFI_PROVISIONING && (int32_t)(now_ms - s_provision_deadline_ms) >= 0) {
        s_state = CADS_WIFI_FAILED;
        s_fail_count++;
    }
}

void cads_wifi_status(cads_wifi_status_t* out) {
    if(out == NULL) return;
    out->state = s_state;
    out->fail_count = s_fail_count;
    out->local_ip = 0u;
    out->peer_ip = 0u;
}
