/* apps/marauder's PCAP-over-serial demux + TZSP builder - see
 * apps/marauder/cads_marauder_pcap.h for the full wire-format reasoning. */

#include <string.h>

#include "unity.h"

#include "cads_marauder_pcap.h"

/* --- test doubles: capture what the callbacks were handed --------------- */

#define MAX_CAPTURED_FRAMES 8

typedef struct {
    uint8_t data[CADS_MARAUDER_PCAP_FRAME_MAX];
    uint16_t len;
    uint32_t orig_len;
} captured_frame_t;

static captured_frame_t s_frames[MAX_CAPTURED_FRAMES];
static size_t s_frame_count;

static uint8_t s_passthrough[512];
static size_t s_passthrough_len;

static void frame_cb(void* ctx, const uint8_t* frame, uint16_t frame_len, uint32_t orig_len) {
    (void)ctx;
    TEST_ASSERT_TRUE(s_frame_count < MAX_CAPTURED_FRAMES);
    memcpy(s_frames[s_frame_count].data, frame, frame_len);
    s_frames[s_frame_count].len = frame_len;
    s_frames[s_frame_count].orig_len = orig_len;
    s_frame_count++;
}

static void pass_cb(void* ctx, const uint8_t* data, uint8_t len) {
    (void)ctx;
    TEST_ASSERT_TRUE(s_passthrough_len + len <= sizeof(s_passthrough));
    memcpy(s_passthrough + s_passthrough_len, data, len);
    s_passthrough_len += len;
}

static cads_marauder_pcap_t s_p;

void setUp(void) {
    s_frame_count = 0u;
    s_passthrough_len = 0u;
    cads_marauder_pcap_init(&s_p);
    cads_marauder_pcap_set_frame_cb(&s_p, frame_cb, NULL);
    cads_marauder_pcap_set_passthrough_cb(&s_p, pass_cb, NULL);
}

void tearDown(void) {
}

/* --- byte-builders for synthetic bursts ---------------------------------- */

static void put_le32(uint8_t* out, uint32_t v) {
    out[0] = (uint8_t)v;
    out[1] = (uint8_t)(v >> 8);
    out[2] = (uint8_t)(v >> 16);
    out[3] = (uint8_t)(v >> 24);
}

/* A valid 24 B pcap global header (magic + version 2.4 + zeroed
 * zone/sigfigs + an arbitrary snaplen/linktype - only the magic actually
 * matters to this parser, see cads_marauder_pcap.c). */
static size_t build_global_header(uint8_t* out) {
    put_le32(out + 0, 0xa1b2c3d4u);
    out[4] = 2u;
    out[5] = 0u; /* version_major = 2 */
    out[6] = 4u;
    out[7] = 0u; /* version_minor = 4 */
    put_le32(out + 8, 0u);
    put_le32(out + 12, 0u);
    put_le32(out + 16, 2324u); /* snaplen */
    put_le32(out + 20, 105u);  /* linktype: IEEE 802.11 */
    return 24u;
}

static size_t build_record(
    uint8_t* out, uint32_t ts_sec, uint32_t ts_usec, const uint8_t* payload, uint32_t payload_len) {
    put_le32(out + 0, ts_sec);
    put_le32(out + 4, ts_usec);
    put_le32(out + 8, payload_len);
    put_le32(out + 12, payload_len);
    if(payload_len > 0u) memcpy(out + 16, payload, payload_len);
    return 16u + payload_len;
}

static void feed_str(const char* s) {
    cads_marauder_pcap_feed(&s_p, (const uint8_t*)s, strlen(s));
}

static void feed_bytes(const uint8_t* b, size_t n) {
    cads_marauder_pcap_feed(&s_p, b, n);
}

/* --- passthrough (plain CLI text, no bursts at all) ---------------------- */

static void test_plain_cli_text_all_passthrough(void) {
    const char* text = "#scanall\nScanning. Stop with stopscan\n";
    feed_str(text);
    TEST_ASSERT_EQUAL_UINT32(0u, s_frame_count);
    TEST_ASSERT_EQUAL_STRING_LEN(text, (const char*)s_passthrough, s_passthrough_len);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(text), (uint32_t)s_passthrough_len);
}

/* A line that starts like the marker but isn't one ("[" is not illegal
 * outside SSIDs in general text) must still reach passthrough intact, in
 * order - see cads_marauder_pcap.h's "self-describing" note on why this is
 * an accepted, vanishingly rare ambiguity rather than something this parser
 * tries to fully resolve. */
static void test_false_start_bracket_still_passes_through(void) {
    feed_str("[BUF/BEG only a false start\n");
    TEST_ASSERT_EQUAL_UINT32(0u, s_frame_count);
    TEST_ASSERT_EQUAL_STRING_LEN("[BUF/BEG only a false start\n", (const char*)s_passthrough, s_passthrough_len);
}

/* --- a real capture: global header present on the first burst ------------ */

static void test_first_burst_decodes_global_header_and_one_record(void) {
    uint8_t buf[64];
    size_t n = 0u;
    n += build_global_header(buf + n);
    const uint8_t payload[] = {0xc0, 0x00, 0x3a, 0x01, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    n += build_record(buf + n, 1000u, 500u, payload, sizeof(payload));

    feed_str("[BUF/BEGIN]");
    feed_bytes(buf, n);
    feed_str("[BUF/CLOSE]");

    TEST_ASSERT_EQUAL_UINT32(1u, s_frame_count);
    TEST_ASSERT_EQUAL_UINT16(sizeof(payload), s_frames[0].len);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), s_frames[0].orig_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, s_frames[0].data, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)s_passthrough_len);
}

/* A second burst in the same session carries no global header - just
 * records straight after "[BUF/BEGIN]". Two records in one burst. */
static void test_second_burst_no_global_header_two_records(void) {
    const uint8_t p1[] = {0x40, 0x00, 0x01, 0x02};
    const uint8_t p2[] = {0x80, 0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t buf[64];
    size_t n = 0u;
    n += build_record(buf + n, 2000u, 1u, p1, sizeof(p1));
    n += build_record(buf + n, 2000u, 2u, p2, sizeof(p2));

    feed_str("[BUF/BEGIN]");
    feed_bytes(buf, n);
    feed_str("[BUF/CLOSE]");

    TEST_ASSERT_EQUAL_UINT32(2u, s_frame_count);
    TEST_ASSERT_EQUAL_UINT16(sizeof(p1), s_frames[0].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(p1, s_frames[0].data, sizeof(p1));
    TEST_ASSERT_EQUAL_UINT16(sizeof(p2), s_frames[1].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(p2, s_frames[1].data, sizeof(p2));
}

/* Zero queued records: "[BUF/BEGIN]" immediately followed by
 * "[BUF/CLOSE]" - must resync cleanly to scan for the next begin marker. */
static void test_empty_burst_then_next_burst_still_decodes(void) {
    feed_str("[BUF/BEGIN][BUF/CLOSE]");
    TEST_ASSERT_EQUAL_UINT32(0u, s_frame_count);

    const uint8_t p1[] = {0x11, 0x22};
    uint8_t buf[32];
    size_t n = build_record(buf, 3000u, 0u, p1, sizeof(p1));
    feed_str("[BUF/BEGIN]");
    feed_bytes(buf, n);
    feed_str("[BUF/CLOSE]");

    TEST_ASSERT_EQUAL_UINT32(1u, s_frame_count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(p1, s_frames[0].data, sizeof(p1));
}

/* A record whose incl_len is 0 (the pcap format allows it) fires
 * immediately with a valid, empty frame. */
static void test_zero_length_record(void) {
    uint8_t buf[16];
    size_t n = build_record(buf, 4000u, 0u, NULL, 0u);
    feed_str("[BUF/BEGIN]");
    feed_bytes(buf, n);
    feed_str("[BUF/CLOSE]");

    TEST_ASSERT_EQUAL_UINT32(1u, s_frame_count);
    TEST_ASSERT_EQUAL_UINT16(0u, s_frames[0].len);
}

/* incl_len larger than CADS_MARAUDER_PCAP_FRAME_MAX: captured frame is
 * truncated to the cap, orig_len still reports the true size, and - the
 * part that actually matters - the excess bytes are correctly consumed
 * (not left dangling to desync the next record). */
static void test_oversized_record_truncates_and_stays_in_sync(void) {
    uint8_t big[CADS_MARAUDER_PCAP_FRAME_MAX + 40u];
    for(size_t i = 0; i < sizeof(big); i++) big[i] = (uint8_t)(i & 0xFFu);
    uint8_t buf[sizeof(big) + 16u];
    size_t n = build_record(buf, 5000u, 0u, big, (uint32_t)sizeof(big));

    const uint8_t p2[] = {0x99};
    uint8_t buf2[32];
    size_t n2 = build_record(buf2, 5001u, 0u, p2, sizeof(p2));

    feed_str("[BUF/BEGIN]");
    feed_bytes(buf, n);
    feed_bytes(buf2, n2);
    feed_str("[BUF/CLOSE]");

    TEST_ASSERT_EQUAL_UINT32(2u, s_frame_count);
    TEST_ASSERT_EQUAL_UINT16(CADS_MARAUDER_PCAP_FRAME_MAX, s_frames[0].len);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(big), s_frames[0].orig_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(big, s_frames[0].data, CADS_MARAUDER_PCAP_FRAME_MAX);
    /* the second record decoded cleanly right after the truncated one */
    TEST_ASSERT_EQUAL_UINT16(sizeof(p2), s_frames[1].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(p2, s_frames[1].data, sizeof(p2));
}

/* Every atom (marker, header, payload) must be resumable one byte at a
 * time - a UART read can return however many bytes happen to be queued. */
static void test_record_split_across_many_tiny_feeds(void) {
    uint8_t buf[64];
    size_t n = 0u;
    n += build_global_header(buf + n);
    const uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8};
    n += build_record(buf + n, 6000u, 0u, payload, sizeof(payload));

    feed_str("[BUF/BEGIN]");
    for(size_t i = 0; i < n; i++) feed_bytes(buf + i, 1u);
    feed_str("[BUF/CLOSE]");

    TEST_ASSERT_EQUAL_UINT32(1u, s_frame_count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, s_frames[0].data, sizeof(payload));
}

/* CLI text before and after a burst is not lost or reordered. */
static void test_cli_text_around_a_burst(void) {
    const uint8_t p1[] = {0xde, 0xad};
    uint8_t buf[32];
    size_t n = build_record(buf, 7000u, 0u, p1, sizeof(p1));

    feed_str("Starting raw capture\n");
    feed_str("[BUF/BEGIN]");
    feed_bytes(buf, n);
    feed_str("[BUF/CLOSE]");
    feed_str("Stopped\n");

    TEST_ASSERT_EQUAL_UINT32(1u, s_frame_count);
    TEST_ASSERT_EQUAL_STRING_LEN(
        "Starting raw capture\nStopped\n", (const char*)s_passthrough, s_passthrough_len);
}

/* --- TZSP builder --------------------------------------------------------- */

static void test_tzsp_build_header_bytes_exact(void) {
    const uint8_t frame[] = {0xaa, 0xbb, 0xcc};
    uint8_t out[16];
    size_t n = cads_marauder_tzsp_build(out, sizeof(out), frame, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT32(CADS_MARAUDER_TZSP_HDR_LEN + sizeof(frame), (uint32_t)n);
    const uint8_t expect_hdr[CADS_MARAUDER_TZSP_HDR_LEN] = {0x01u, 0x00u, 0x00u, 0x12u, 0x01u};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect_hdr, out, CADS_MARAUDER_TZSP_HDR_LEN);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame, out + CADS_MARAUDER_TZSP_HDR_LEN, sizeof(frame));
}

static void test_tzsp_build_empty_frame(void) {
    uint8_t out[8];
    size_t n = cads_marauder_tzsp_build(out, sizeof(out), NULL, 0u);
    TEST_ASSERT_EQUAL_UINT32(CADS_MARAUDER_TZSP_HDR_LEN, (uint32_t)n);
}

static void test_tzsp_build_too_small_returns_zero(void) {
    const uint8_t frame[] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t out[4]; /* smaller than even the 5 B header */
    size_t n = cads_marauder_tzsp_build(out, sizeof(out), frame, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)n);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_plain_cli_text_all_passthrough);
    RUN_TEST(test_false_start_bracket_still_passes_through);
    RUN_TEST(test_first_burst_decodes_global_header_and_one_record);
    RUN_TEST(test_second_burst_no_global_header_two_records);
    RUN_TEST(test_empty_burst_then_next_burst_still_decodes);
    RUN_TEST(test_zero_length_record);
    RUN_TEST(test_oversized_record_truncates_and_stays_in_sync);
    RUN_TEST(test_record_split_across_many_tiny_feeds);
    RUN_TEST(test_cli_text_around_a_burst);
    RUN_TEST(test_tzsp_build_header_bytes_exact);
    RUN_TEST(test_tzsp_build_empty_frame);
    RUN_TEST(test_tzsp_build_too_small_returns_zero);
    return UNITY_END();
}
