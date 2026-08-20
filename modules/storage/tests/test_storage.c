/* cads/storage/storage.h against the real littlefs, backed by
 * src/cads_flash_host.c. This is the one place in the test suite that
 * exercises actual littlefs mount/format/file/directory logic - not a fake,
 * the same lfs.c the board links, over a block device that enforces the same
 * NOR "erase then program" contract the real flash does (see
 * cads_flash_host.c and test_flash.c). What is different from the board is
 * only the medium underneath cads/storage/flash.h. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "cads/storage/flash.h"
#include "cads/storage/storage.h"

void setUp(void) {
    /* Every test starts from a freshly formatted, empty volume. format()
     * itself is exercised directly by its own test below; using it here too
     * is deliberate; a setUp() that could not rely on the thing being tested
     * would not be testing very much. */
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_format());
}

void tearDown(void) {
    cads_storage_unmount();
}

static void test_mount_without_a_prior_format_is_corrupt(void) {
    /* Undo setUp()'s format so this test sees genuinely blank flash. unmount()
     * alone is not enough: it only drops the in-memory littlefs state, not
     * the medium, and the volume setUp() formatted is otherwise still sitting
     * there valid - the host's static flash array persists across every test
     * in this binary, unlike the real board which starts each test process
     * with whatever cads_flash_init() left from the last one it ran too.
     * Erasing every block directly is what actually reproduces "a board that
     * has never been formatted": all 0xFF, no littlefs superblock. */
    cads_storage_unmount();
    const cads_flash_geometry_t* geom = cads_flash_geometry();
    TEST_ASSERT_NOT_NULL(geom);
    for(uint32_t block = 0; block < geom->block_count; block++) {
        TEST_ASSERT_EQUAL_INT(CADS_FLASH_OK, cads_flash_erase_block(block));
    }

    cads_storage_file_t* file = NULL;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_ERR_NOTMOUNTED, cads_storage_open(&file, "/x", CADS_STORAGE_RDONLY));

    int status = cads_storage_mount();
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_ERR_CORRUPT, status);

    /* Recreate what setUp() expects to already be true for every other test,
     * since Unity runs setUp() before this function, not after. */
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_format());
}

static void test_format_leaves_the_volume_mounted_and_empty(void) {
    TEST_ASSERT_TRUE(cads_storage_is_mounted());

    cads_storage_stats_t stats;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(128u * 1024u, stats.block_size);
    TEST_ASSERT_EQUAL_UINT32(7u, stats.block_count);
    TEST_ASSERT_EQUAL_UINT32(896u * 1024u, stats.total_bytes);
}

static void test_write_then_read_back_round_trips(void) {
    cads_storage_file_t* file = NULL;
    TEST_ASSERT_EQUAL_INT(
        CADS_STORAGE_OK,
        cads_storage_open(&file, "/leo.txt", CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT));
    TEST_ASSERT_NOT_NULL(file);

    const char text[] = "Leo is dozing";
    TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(text), cads_storage_write(file, text, sizeof(text)));
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_close(file));

    file = NULL;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_open(&file, "/leo.txt", CADS_STORAGE_RDONLY));
    char buffer[sizeof(text)] = {0};
    TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(text), cads_storage_read(file, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING(text, buffer);
    TEST_ASSERT_EQUAL_INT32(0, cads_storage_read(file, buffer, sizeof(buffer))); /* end of file */
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_close(file));
}

static void test_contents_survive_an_unmount_and_remount(void) {
    /* The closest thing a host test can do to "power cycle": drop every
     * in-memory littlefs structure and rebuild it from what is actually on
     * the (emulated) medium. If this passes, the only way persistence could
     * still fail on the board is the flash driver itself lying about a
     * write landing - which is exactly what the hardware gate's own
     * write/reset/read-back/CRC check exists to catch, not something a host
     * test can. */
    cads_storage_file_t* file = NULL;
    TEST_ASSERT_EQUAL_INT(
        CADS_STORAGE_OK, cads_storage_open(&file, "/persist.bin", CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT));
    uint32_t value = 0xC0FFEEu;
    TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(value), cads_storage_write(file, &value, sizeof(value)));
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_close(file));

    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_unmount());
    TEST_ASSERT_FALSE(cads_storage_is_mounted());
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_mount());

    file = NULL;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_open(&file, "/persist.bin", CADS_STORAGE_RDONLY));
    uint32_t read_back = 0;
    TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(read_back), cads_storage_read(file, &read_back, sizeof(read_back)));
    TEST_ASSERT_EQUAL_HEX32(value, read_back);
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_close(file));
}

static void test_stat_reports_type_and_size(void) {
    cads_storage_file_t* file = NULL;
    cads_storage_open(&file, "/f.bin", CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT);
    uint8_t payload[10] = {0};
    cads_storage_write(file, payload, sizeof(payload));
    cads_storage_close(file);
    cads_storage_mkdir("/d");

    cads_storage_info_t info;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_stat("/f.bin", &info));
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_TYPE_FILE, info.type);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), info.size);
    TEST_ASSERT_EQUAL_STRING("f.bin", info.name);

    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_stat("/d", &info));
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_TYPE_DIR, info.type);

    TEST_ASSERT_TRUE(cads_storage_exists("/f.bin"));
    TEST_ASSERT_FALSE(cads_storage_exists("/nope"));
}

static void test_remove_and_rename(void) {
    cads_storage_file_t* file = NULL;
    cads_storage_open(&file, "/a.bin", CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT);
    cads_storage_close(file);

    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_rename("/a.bin", "/b.bin"));
    TEST_ASSERT_FALSE(cads_storage_exists("/a.bin"));
    TEST_ASSERT_TRUE(cads_storage_exists("/b.bin"));

    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_remove("/b.bin"));
    TEST_ASSERT_FALSE(cads_storage_exists("/b.bin"));
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_ERR_NOENT, cads_storage_remove("/b.bin"));
}

static void test_directory_listing_skips_dot_and_dotdot(void) {
    cads_storage_mkdir("/apps");
    cads_storage_file_t* file = NULL;
    cads_storage_open(&file, "/apps/gpio", CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT);
    cads_storage_close(file);
    cads_storage_open(&file, "/apps/netinfo", CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT);
    cads_storage_close(file);

    cads_storage_dir_t* dir = NULL;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_dir_open(&dir, "/apps"));

    int seen = 0;
    cads_storage_info_t info;
    int rc;
    while((rc = cads_storage_dir_read(dir, &info)) == 1) {
        TEST_ASSERT_NOT_EQUAL(0, strcmp(info.name, "."));
        TEST_ASSERT_NOT_EQUAL(0, strcmp(info.name, ".."));
        seen++;
    }
    TEST_ASSERT_EQUAL_INT(0, rc); /* clean end of directory, not an error */
    TEST_ASSERT_EQUAL_INT(2, seen);
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_dir_close(dir));
}

static void test_file_handle_pool_is_exhausted_not_grown(void) {
    cads_storage_file_t* handles[CADS_STORAGE_MAX_FILES];
    for(uint32_t i = 0; i < CADS_STORAGE_MAX_FILES; i++) {
        char path[16];
        path[0] = '/';
        path[1] = (char)('a' + i);
        path[2] = '\0';
        TEST_ASSERT_EQUAL_INT(
            CADS_STORAGE_OK, cads_storage_open(&handles[i], path, CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT));
    }

    cads_storage_file_t* one_too_many = NULL;
    TEST_ASSERT_EQUAL_INT(
        CADS_STORAGE_ERR_NOSLOT,
        cads_storage_open(&one_too_many, "/one_more", CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT));
    TEST_ASSERT_NULL(one_too_many);

    for(uint32_t i = 0; i < CADS_STORAGE_MAX_FILES; i++) {
        cads_storage_close(handles[i]);
    }
}

static void test_format_is_refused_while_a_handle_is_open(void) {
    cads_storage_file_t* file = NULL;
    cads_storage_open(&file, "/held.bin", CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT);

    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_ERR_BUSY, cads_storage_format());
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_ERR_BUSY, cads_storage_unmount());

    cads_storage_close(file);
}

static void test_close_of_null_is_a_harmless_no_op(void) {
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_close(NULL));
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_dir_close(NULL));
}

static void test_status_text_is_never_null(void) {
    TEST_ASSERT_NOT_NULL(cads_storage_status_text(CADS_STORAGE_OK));
    TEST_ASSERT_NOT_NULL(cads_storage_status_text(CADS_STORAGE_ERR_NOSPC));
    TEST_ASSERT_NOT_NULL(cads_storage_status_text(12345)); /* unknown code */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mount_without_a_prior_format_is_corrupt);
    RUN_TEST(test_format_leaves_the_volume_mounted_and_empty);
    RUN_TEST(test_write_then_read_back_round_trips);
    RUN_TEST(test_contents_survive_an_unmount_and_remount);
    RUN_TEST(test_stat_reports_type_and_size);
    RUN_TEST(test_remove_and_rename);
    RUN_TEST(test_directory_listing_skips_dot_and_dotdot);
    RUN_TEST(test_file_handle_pool_is_exhausted_not_grown);
    RUN_TEST(test_format_is_refused_while_a_handle_is_open);
    RUN_TEST(test_close_of_null_is_a_harmless_no_op);
    RUN_TEST(test_status_text_is_never_null);
    return UNITY_END();
}
