#include "explorer_capture_buffer.h"

static uint8_t cads_explorer_capture_storage[CADS_EXPLORER_CAPTURE_BUFFER_SIZE];

uint8_t* cads_explorer_capture_buffer(void) {
    return cads_explorer_capture_storage;
}
