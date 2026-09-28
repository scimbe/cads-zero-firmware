#include "cads_wetter_net.h"

#include "l10_http_wetter_1.h"

bool cads_wetter_net_start(const char* host, uint16_t port) {
    return rnlab_l10_fetch_start(host, port, RNLAB_L10_DEFAULT_PATH, false);
}

bool cads_wetter_net_busy(void) {
    return rnlab_l10_fetch_busy();
}

void cads_wetter_net_service(uint32_t now_ms) {
    rnlab_l10_fetch_service(now_ms);
}

void cads_wetter_net_abort(void) {
    rnlab_l10_fetch_abort();
}

const rnlab_fetch_result_t* cads_wetter_net_result(void) {
    return rnlab_l10_fetch_result();
}
