/*
 * CaDS Zero - the M4 hardware gate, reached from the hardware explorer's 'u'
 * command. See explorer_storage_test.h for the two-run-across-a-reset
 * protocol this implements.
 */

#include "explorer_storage_test.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cads/storage/flash.h"
#include "cads/storage/storage.h"
#include "input_probe.h" /* cads_probe_puts / cads_probe_put_uint */

#ifdef CADS_TARGET_ITSBOARD
#include "board.h"
#include "cads/toolbox/fmt.h"
#include "cads_hal.h"

/*
 * Standard CRC-32/ISO-HDLC (the one everything from zlib to Ethernet FCS
 * uses, polynomial 0xEDB88320 reflected) - public domain algorithm, not
 * project-specific code. Bit-by-bit rather than table-based: this runs once
 * per explorer command, not per frame, and the table would cost 1 KB of
 * flash to save perhaps 300 ms over ~1.1 MB - not a trade this firmware
 * needs.
 */
static uint32_t cads_storage_test_crc32(const void* data, uint32_t length) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFu;
    for(uint32_t i = 0; i < length; i++) {
        crc ^= bytes[i];
        for(uint32_t bit = 0; bit < 8u; bit++) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static void cads_storage_test_print_crc(const char* label) {
    uint32_t length = CADS_FS_BASE - CADS_FLASH_APP_BASE;
    uint32_t crc = cads_storage_test_crc32((const void*)CADS_FLASH_APP_BASE, length);

    cads_probe_puts("# firmware region CRC32 (");
    cads_probe_puts(label);
    cads_probe_puts("): 0x");
    char digits[CADS_FMT_BUFFER];
    size_t n = cads_fmt_hex(digits, sizeof(digits), crc, 8u, true);
    cads_hal_console_write(digits, n);
    cads_probe_puts("\r\n");
}
#endif /* CADS_TARGET_ITSBOARD */

#define CADS_STORAGE_TEST_PATH "/cads_test.bin"
#define CADS_STORAGE_TEST_MAGIC 0x5A4F4C43u /* "CLOZ" - CaDS Leo, little endian */

typedef struct {
    uint32_t magic;
    uint8_t pattern[60]; /* incrementing bytes, easy to eyeball if this ever needs a hex dump */
} cads_storage_test_payload_t;

static void cads_storage_test_fill_payload(cads_storage_test_payload_t* payload) {
    payload->magic = CADS_STORAGE_TEST_MAGIC;
    for(uint32_t i = 0; i < sizeof(payload->pattern); i++) {
        payload->pattern[i] = (uint8_t)i;
    }
}

static bool cads_storage_test_write_file(void) {
    cads_storage_file_t* file = NULL;
    int rc = cads_storage_open(
        &file, CADS_STORAGE_TEST_PATH, CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT | CADS_STORAGE_TRUNC);
    if(rc != CADS_STORAGE_OK) {
        cads_probe_puts("# storage: open for write failed: ");
        cads_probe_puts(cads_storage_status_text(rc));
        cads_probe_puts("\r\n");
        return false;
    }

    cads_storage_test_payload_t payload;
    cads_storage_test_fill_payload(&payload);

    int32_t written = cads_storage_write(file, &payload, sizeof(payload));
    int close_rc = cads_storage_close(file);

    if(written != (int32_t)sizeof(payload)) {
        cads_probe_puts("# storage: short or failed write\r\n");
        return false;
    }
    if(close_rc != CADS_STORAGE_OK) {
        cads_probe_puts("# storage: close failed: ");
        cads_probe_puts(cads_storage_status_text(close_rc));
        cads_probe_puts("\r\n");
        return false;
    }
    return true;
}

static bool cads_storage_test_verify_file(void) {
    cads_storage_file_t* file = NULL;
    int rc = cads_storage_open(&file, CADS_STORAGE_TEST_PATH, CADS_STORAGE_RDONLY);
    if(rc != CADS_STORAGE_OK) {
        cads_probe_puts("# storage: open for read failed: ");
        cads_probe_puts(cads_storage_status_text(rc));
        cads_probe_puts(" (the test file from the first run should still be here)\r\n");
        return false;
    }

    cads_storage_test_payload_t expected;
    cads_storage_test_fill_payload(&expected);

    cads_storage_test_payload_t actual;
    memset(&actual, 0, sizeof(actual));
    int32_t read = cads_storage_read(file, &actual, sizeof(actual));
    cads_storage_close(file);

    if(read != (int32_t)sizeof(actual)) {
        cads_probe_puts("# storage: short or failed read\r\n");
        return false;
    }
    if(memcmp(&expected, &actual, sizeof(expected)) != 0) {
        cads_probe_puts("# storage: content mismatch - did not survive the reset intact\r\n");
        return false;
    }
    return true;
}

void cads_explorer_storage_test(void) {
#ifdef CADS_TARGET_ITSBOARD
    cads_storage_test_print_crc("before");
#endif

    int rc = cads_storage_mount();
    bool ok;

    if(rc == CADS_STORAGE_ERR_CORRUPT) {
        cads_probe_puts("# storage: no filesystem found - formatting (this is the first run)\r\n");
        rc = cads_storage_format();
        if(rc != CADS_STORAGE_OK) {
            cads_probe_puts("# storage: format failed: ");
            cads_probe_puts(cads_storage_status_text(rc));
            cads_probe_puts("\r\n");
            ok = false;
        } else {
            ok = cads_storage_test_write_file();
            if(ok) {
                cads_probe_puts(
                    "# storage: test file written. Reset the board (not a reflash) and run "
                    "'u' again to verify it survived.\r\n");
            }
        }
    } else if(rc == CADS_STORAGE_OK) {
        cads_probe_puts("# storage: existing filesystem mounted - checking persistence\r\n");
        ok = cads_storage_test_verify_file();
    } else {
        cads_probe_puts("# storage: mount failed: ");
        cads_probe_puts(cads_storage_status_text(rc));
        cads_probe_puts("\r\n");
        ok = false;
    }

    cads_storage_unmount();

    cads_probe_puts(ok ? "# storage test: PASS\r\n" : "# storage test: FAIL\r\n");

#ifdef CADS_TARGET_ITSBOARD
    cads_storage_test_print_crc("after");
#endif
}

void cads_explorer_flash_raw_test(void) {
    int rc = cads_flash_init();
    cads_probe_puts("# flash: init rc=");
    cads_probe_put_uint((uint32_t)(rc < 0 ? -rc : rc));
    cads_probe_puts(rc == CADS_FLASH_OK ? " (ok)\r\n" : " (FAIL)\r\n");
    if(rc != CADS_FLASH_OK) return;

    const cads_flash_geometry_t* geom = cads_flash_geometry();
    cads_probe_puts("# flash: base(decimal)=");
    cads_probe_put_uint(geom->base);
    cads_probe_puts(" size=");
    cads_probe_put_uint(geom->size);
    cads_probe_puts(" block_size=");
    cads_probe_put_uint(geom->block_size);
    cads_probe_puts(" block_count=");
    cads_probe_put_uint(geom->block_count);
    cads_probe_puts("\r\n");

    cads_probe_puts("# flash: erasing block 0\r\n");
    rc = cads_flash_erase_block(0);
    cads_probe_puts("# flash: erase rc=");
    cads_probe_put_uint((uint32_t)(rc < 0 ? -rc : rc));
    cads_probe_puts(rc == CADS_FLASH_OK ? " (ok)\r\n" : " (FAIL)\r\n");
    if(rc != CADS_FLASH_OK) return;

    uint32_t erased = 0;
    rc = cads_flash_read(0, &erased, sizeof(erased));
    cads_probe_puts("# flash: read after erase rc=");
    cads_probe_put_uint((uint32_t)(rc < 0 ? -rc : rc));
    cads_probe_puts(", value(decimal)=");
    cads_probe_put_uint(erased);
    cads_probe_puts(erased == 0xFFFFFFFFu ? " (as expected)\r\n" : " (UNEXPECTED)\r\n");

    uint32_t pattern = 0x12345678u;
    rc = cads_flash_program(0, &pattern, sizeof(pattern));
    cads_probe_puts("# flash: program rc=");
    cads_probe_put_uint((uint32_t)(rc < 0 ? -rc : rc));
    cads_probe_puts(rc == CADS_FLASH_OK ? " (ok)\r\n" : " (FAIL)\r\n");
    if(rc != CADS_FLASH_OK) return;

    uint32_t programmed = 0;
    rc = cads_flash_read(0, &programmed, sizeof(programmed));
    cads_probe_puts("# flash: read after program rc=");
    cads_probe_put_uint((uint32_t)(rc < 0 ? -rc : rc));
    cads_probe_puts(", value(decimal)=");
    cads_probe_put_uint(programmed);
    cads_probe_puts(programmed == pattern ? " (matches)\r\n" : " (MISMATCH)\r\n");

    cads_probe_puts(
        (rc == CADS_FLASH_OK && programmed == pattern) ? "# flash raw test: PASS\r\n"
                                                         : "# flash raw test: FAIL\r\n");
}
