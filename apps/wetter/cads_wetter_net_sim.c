#include "cads_wetter_net.h"

#include <string.h>

static rnlab_fetch_result_t s_result;

bool cads_wetter_net_start(const char* host, uint16_t port) {
    (void)host;
    (void)port;
    memset(&s_result, 0, sizeof(s_result));
    s_result.state = RNLAB_FETCH_DONE;
    s_result.error = RNLAB_FETCH_ERR_NO_LINK;
    return false;
}

bool cads_wetter_net_busy(void) {
    return false;
}

void cads_wetter_net_service(uint32_t now_ms) {
    (void)now_ms;
}

void cads_wetter_net_abort(void) {
}

const rnlab_fetch_result_t* cads_wetter_net_result(void) {
    return &s_result;
}
