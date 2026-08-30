/* modules/security's wire framing - see cads/security/secure_frame.h for
 * the wire format and design rationale (magic byte >= 0x80 so a secure
 * frame can never be mistaken for cads_cli's plaintext, streaming-safe
 * decode for a real TCP consumer). */

#include <string.h>

#include "unity.h"

#include "cads/security/secure_frame.h"

void setUp(void) {
}

void tearDown(void) {
}

/* Same fixed, non-secret key/nonce/ad/plaintext as test_secure_link.c's own
 * cross-implementation vector - reused here (not re-derived) so a failure in
 * this file points at the framing layer, not a second, independently-wrong
 * copy of the crypto vector. */
static const uint8_t s_key[CADS_SECURE_LINK_KEY_LEN] = {
    0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u, 0x09u, 0x0au, 0x0bu,
    0x0cu, 0x0du, 0x0eu, 0x0fu, 0x10u, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u,
    0x18u, 0x19u, 0x1au, 0x1bu, 0x1cu, 0x1du, 0x1eu, 0x1fu};
static const uint8_t s_nonce[CADS_SECURE_LINK_NONCE_LEN] = {
    0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u, 0x09u, 0x0au, 0x0bu,
    0x0cu, 0x0du, 0x0eu, 0x0fu, 0x10u, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u};
static const uint8_t s_ad[12] = {
    0x43u, 0x61u, 0x44u, 0x53u, 0x20u, 0x5au, 0x65u, 0x72u, 0x6fu, 0x20u, 0x41u, 0x44u};
static const uint8_t s_plain[27] = {
    0x48u, 0x65u, 0x6cu, 0x6cu, 0x6fu, 0x20u, 0x66u, 0x72u, 0x6fu, 0x6du, 0x20u, 0x74u,
    0x68u, 0x65u, 0x20u, 0x53u, 0x54u, 0x4du, 0x33u, 0x32u, 0x20u, 0x62u, 0x6fu, 0x61u,
    0x72u, 0x64u, 0x21u};

static void test_round_trips(void) {
    uint8_t frame[sizeof(s_plain) + CADS_SECURE_FRAME_OVERHEAD];
    size_t frame_len = cads_secure_frame_encode(
        frame, sizeof(frame), s_key, s_nonce, s_ad, sizeof(s_ad), s_plain, sizeof(s_plain));
    TEST_ASSERT_EQUAL_UINT(sizeof(frame), frame_len);

    uint8_t plain_out[sizeof(s_plain)];
    size_t plain_len = 0u, consumed = 0u;
    cads_secure_frame_status_t status = cads_secure_frame_decode(
        frame, frame_len, s_key, s_ad, sizeof(s_ad), plain_out, sizeof(plain_out), &plain_len,
        &consumed);

    TEST_ASSERT_EQUAL(CADS_SECURE_FRAME_OK, status);
    TEST_ASSERT_EQUAL_UINT(sizeof(s_plain), plain_len);
    TEST_ASSERT_EQUAL_UINT(frame_len, consumed);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(s_plain, plain_out, sizeof(s_plain));
}

static void test_header_layout_is_the_documented_wire_format(void) {
    uint8_t frame[sizeof(s_plain) + CADS_SECURE_FRAME_OVERHEAD];
    cads_secure_frame_encode(
        frame, sizeof(frame), s_key, s_nonce, s_ad, sizeof(s_ad), s_plain, sizeof(s_plain));

    TEST_ASSERT_EQUAL_UINT8_ARRAY(CADS_SECURE_FRAME_MAGIC, frame, CADS_SECURE_FRAME_MAGIC_LEN);
    uint32_t length_field = (uint32_t)frame[4] | ((uint32_t)frame[5] << 8) |
                             ((uint32_t)frame[6] << 16) | ((uint32_t)frame[7] << 24);
    TEST_ASSERT_EQUAL_UINT32(sizeof(s_plain), length_field);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(s_nonce, frame + 8, CADS_SECURE_LINK_NONCE_LEN);
}

static void test_incomplete_header_reports_incomplete(void) {
    uint8_t frame[sizeof(s_plain) + CADS_SECURE_FRAME_OVERHEAD];
    cads_secure_frame_encode(
        frame, sizeof(frame), s_key, s_nonce, s_ad, sizeof(s_ad), s_plain, sizeof(s_plain));

    uint8_t plain_out[sizeof(s_plain)];
    size_t plain_len = 0u, consumed = 123u;
    cads_secure_frame_status_t status = cads_secure_frame_decode(
        frame, CADS_SECURE_FRAME_HEADER_LEN - 1u, s_key, s_ad, sizeof(s_ad), plain_out,
        sizeof(plain_out), &plain_len, &consumed);

    TEST_ASSERT_EQUAL(CADS_SECURE_FRAME_INCOMPLETE, status);
    TEST_ASSERT_EQUAL_UINT(0u, consumed);
}

static void test_incomplete_body_reports_incomplete(void) {
    uint8_t frame[sizeof(s_plain) + CADS_SECURE_FRAME_OVERHEAD];
    size_t frame_len = cads_secure_frame_encode(
        frame, sizeof(frame), s_key, s_nonce, s_ad, sizeof(s_ad), s_plain, sizeof(s_plain));

    /* A full header (so the length field is readable) but the frame is cut
     * short by one byte - a real TCP segment boundary landing mid-frame. */
    uint8_t plain_out[sizeof(s_plain)];
    size_t plain_len = 0u, consumed = 123u;
    cads_secure_frame_status_t status = cads_secure_frame_decode(
        frame, frame_len - 1u, s_key, s_ad, sizeof(s_ad), plain_out, sizeof(plain_out),
        &plain_len, &consumed);

    TEST_ASSERT_EQUAL(CADS_SECURE_FRAME_INCOMPLETE, status);
    TEST_ASSERT_EQUAL_UINT(0u, consumed);
}

static void test_wrong_magic_reports_bad_magic_and_resyncs_one_byte(void) {
    uint8_t frame[sizeof(s_plain) + CADS_SECURE_FRAME_OVERHEAD];
    size_t frame_len = cads_secure_frame_encode(
        frame, sizeof(frame), s_key, s_nonce, s_ad, sizeof(s_ad), s_plain, sizeof(s_plain));
    frame[0] ^= 0xFFu; /* corrupt the magic's first byte */

    uint8_t plain_out[sizeof(s_plain)];
    size_t plain_len = 0u, consumed = 0u;
    cads_secure_frame_status_t status = cads_secure_frame_decode(
        frame, frame_len, s_key, s_ad, sizeof(s_ad), plain_out, sizeof(plain_out), &plain_len,
        &consumed);

    TEST_ASSERT_EQUAL(CADS_SECURE_FRAME_BAD_MAGIC, status);
    TEST_ASSERT_EQUAL_UINT(1u, consumed);
}

static void test_tampered_ciphertext_fails_auth_and_consumes_whole_frame(void) {
    uint8_t frame[sizeof(s_plain) + CADS_SECURE_FRAME_OVERHEAD];
    size_t frame_len = cads_secure_frame_encode(
        frame, sizeof(frame), s_key, s_nonce, s_ad, sizeof(s_ad), s_plain, sizeof(s_plain));
    frame[CADS_SECURE_FRAME_HEADER_LEN] ^= 0x01u; /* flip a ciphertext byte */

    uint8_t plain_out[sizeof(s_plain)];
    memset(plain_out, 0xAAu, sizeof(plain_out));
    size_t plain_len = 0u, consumed = 0u;
    cads_secure_frame_status_t status = cads_secure_frame_decode(
        frame, frame_len, s_key, s_ad, sizeof(s_ad), plain_out, sizeof(plain_out), &plain_len,
        &consumed);

    TEST_ASSERT_EQUAL(CADS_SECURE_FRAME_AUTH_FAILED, status);
    TEST_ASSERT_EQUAL_UINT(frame_len, consumed);
    /* cads_secure_link_open()'s own failure-wipes-the-output contract
     * (test_secure_link.c's test_failure_wipes_the_output_buffer) must
     * still hold through this layer - a caller that ignores the status
     * and reads plain_out anyway must never see the 0xAA sentinel NOR the
     * real plaintext. */
    uint8_t zeroes[sizeof(s_plain)];
    memset(zeroes, 0, sizeof(zeroes));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(zeroes, plain_out, sizeof(plain_out));
}

static void test_wrong_ad_fails_auth(void) {
    uint8_t frame[sizeof(s_plain) + CADS_SECURE_FRAME_OVERHEAD];
    size_t frame_len = cads_secure_frame_encode(
        frame, sizeof(frame), s_key, s_nonce, s_ad, sizeof(s_ad), s_plain, sizeof(s_plain));

    uint8_t wrong_ad[sizeof(s_ad)];
    memcpy(wrong_ad, s_ad, sizeof(s_ad));
    wrong_ad[0] ^= 0x01u;

    uint8_t plain_out[sizeof(s_plain)];
    size_t plain_len = 0u, consumed = 0u;
    cads_secure_frame_status_t status = cads_secure_frame_decode(
        frame, frame_len, s_key, wrong_ad, sizeof(wrong_ad), plain_out, sizeof(plain_out),
        &plain_len, &consumed);

    TEST_ASSERT_EQUAL(CADS_SECURE_FRAME_AUTH_FAILED, status);
}

static void test_undersized_output_buffer_reports_too_large(void) {
    uint8_t frame[sizeof(s_plain) + CADS_SECURE_FRAME_OVERHEAD];
    size_t frame_len = cads_secure_frame_encode(
        frame, sizeof(frame), s_key, s_nonce, s_ad, sizeof(s_ad), s_plain, sizeof(s_plain));

    uint8_t plain_out[4]; /* smaller than the real 27-byte plaintext */
    size_t plain_len = 0u, consumed = 123u;
    cads_secure_frame_status_t status = cads_secure_frame_decode(
        frame, frame_len, s_key, s_ad, sizeof(s_ad), plain_out, sizeof(plain_out), &plain_len,
        &consumed);

    TEST_ASSERT_EQUAL(CADS_SECURE_FRAME_TOO_LARGE, status);
    TEST_ASSERT_EQUAL_UINT(0u, consumed);
}

static void test_undersized_encode_buffer_returns_zero(void) {
    uint8_t frame[sizeof(s_plain) + CADS_SECURE_FRAME_OVERHEAD - 1u]; /* one byte short */
    size_t frame_len = cads_secure_frame_encode(
        frame, sizeof(frame), s_key, s_nonce, s_ad, sizeof(s_ad), s_plain, sizeof(s_plain));
    TEST_ASSERT_EQUAL_UINT(0u, frame_len);
}

static void test_two_frames_back_to_back_decode_independently(void) {
    uint8_t stream[2u * (sizeof(s_plain) + CADS_SECURE_FRAME_OVERHEAD)];
    size_t first_len = cads_secure_frame_encode(
        stream, sizeof(stream), s_key, s_nonce, s_ad, sizeof(s_ad), s_plain, sizeof(s_plain));

    /* A second frame needs its own nonce (never reuse one under the same
     * key - see cads_secure_link.h) - flip one bit of the fixed test nonce
     * rather than pull in an RNG for this host test. */
    uint8_t nonce2[CADS_SECURE_LINK_NONCE_LEN];
    memcpy(nonce2, s_nonce, sizeof(nonce2));
    nonce2[0] ^= 0x01u;
    size_t second_len = cads_secure_frame_encode(
        stream + first_len, sizeof(stream) - first_len, s_key, nonce2, s_ad, sizeof(s_ad), s_plain,
        sizeof(s_plain));

    uint8_t plain_out[sizeof(s_plain)];
    size_t plain_len = 0u, consumed = 0u;

    cads_secure_frame_status_t status1 = cads_secure_frame_decode(
        stream, first_len + second_len, s_key, s_ad, sizeof(s_ad), plain_out, sizeof(plain_out),
        &plain_len, &consumed);
    TEST_ASSERT_EQUAL(CADS_SECURE_FRAME_OK, status1);
    TEST_ASSERT_EQUAL_UINT(first_len, consumed);

    cads_secure_frame_status_t status2 = cads_secure_frame_decode(
        stream + consumed, first_len + second_len - consumed, s_key, s_ad, sizeof(s_ad), plain_out,
        sizeof(plain_out), &plain_len, &consumed);
    TEST_ASSERT_EQUAL(CADS_SECURE_FRAME_OK, status2);
    TEST_ASSERT_EQUAL_UINT(second_len, consumed);
}

static void test_empty_plaintext_round_trips(void) {
    uint8_t frame[CADS_SECURE_FRAME_OVERHEAD];
    size_t frame_len =
        cads_secure_frame_encode(frame, sizeof(frame), s_key, s_nonce, NULL, 0u, NULL, 0u);
    TEST_ASSERT_EQUAL_UINT(sizeof(frame), frame_len);

    uint8_t plain_out[1];
    size_t plain_len = 123u, consumed = 0u;
    cads_secure_frame_status_t status = cads_secure_frame_decode(
        frame, frame_len, s_key, NULL, 0u, plain_out, sizeof(plain_out), &plain_len, &consumed);
    TEST_ASSERT_EQUAL(CADS_SECURE_FRAME_OK, status);
    TEST_ASSERT_EQUAL_UINT(0u, plain_len);
    TEST_ASSERT_EQUAL_UINT(frame_len, consumed);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_round_trips);
    RUN_TEST(test_header_layout_is_the_documented_wire_format);
    RUN_TEST(test_incomplete_header_reports_incomplete);
    RUN_TEST(test_incomplete_body_reports_incomplete);
    RUN_TEST(test_wrong_magic_reports_bad_magic_and_resyncs_one_byte);
    RUN_TEST(test_tampered_ciphertext_fails_auth_and_consumes_whole_frame);
    RUN_TEST(test_wrong_ad_fails_auth);
    RUN_TEST(test_undersized_output_buffer_reports_too_large);
    RUN_TEST(test_undersized_encode_buffer_returns_zero);
    RUN_TEST(test_two_frames_back_to_back_decode_independently);
    RUN_TEST(test_empty_plaintext_round_trips);
    return UNITY_END();
}
