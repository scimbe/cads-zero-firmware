#include "explorer_capture_buffer.h"

#ifdef CADS_APP_RNLAB_ENABLED
#include "cads_hal.h"

/* CPU-only (frames are memcpy'd in by cads_hal_eth_mac_receive(), written
 * out by the flash driver), so the lab moves it to CCM to free SRAM. */
CADS_CCM_SECTION static uint8_t cads_explorer_capture_storage[CADS_EXPLORER_CAPTURE_BUFFER_SIZE];
#else
static uint8_t cads_explorer_capture_storage[CADS_EXPLORER_CAPTURE_BUFFER_SIZE];
#endif

uint8_t* cads_explorer_capture_buffer(void) {
    return cads_explorer_capture_storage;
}
