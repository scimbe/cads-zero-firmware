/* CaDS Zero - wire framing for sealed messages. See the header for the
 * full wire-format and design reasoning. */
#include "cads/security/secure_frame.h"

#include <string.h>

const uint8_t CADS_SECURE_FRAME_MAGIC[CADS_SECURE_FRAME_MAGIC_LEN] = {0xC5u, 'D', 'S', '1'};

static void write_u32_le(uint8_t* out, uint32_t value) {
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
    out[2] = (uint8_t)((value >> 16) & 0xFFu);
    out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint32_t read_u32_le(const uint8_t* in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

size_t cads_secure_frame_encode(
    uint8_t* out, size_t out_size, const uint8_t key[CADS_SECURE_LINK_KEY_LEN],
    const uint8_t nonce[CADS_SECURE_LINK_NONCE_LEN], const uint8_t* ad, size_t ad_len,
    const uint8_t* plain, size_t plain_len) {
    /* Guard against a plain_len whose +overhead would wrap size_t before
     * the out_size comparison below ever runs - unreachable on real
     * hardware (nothing here ever handles a message anywhere near
     * SIZE_MAX/2), but the check is cheap and turns a would-be silent
     * buffer overflow into a clean "too small" failure instead. */
    if(plain_len > SIZE_MAX - CADS_SECURE_FRAME_OVERHEAD) return 0u;
    size_t frame_len = plain_len + CADS_SECURE_FRAME_OVERHEAD;
    if(out_size < frame_len) return 0u;

    memcpy(out, CADS_SECURE_FRAME_MAGIC, CADS_SECURE_FRAME_MAGIC_LEN);
    write_u32_le(out + CADS_SECURE_FRAME_MAGIC_LEN, (uint32_t)plain_len);
    memcpy(
        out + CADS_SECURE_FRAME_MAGIC_LEN + CADS_SECURE_FRAME_LENGTH_LEN, nonce,
        CADS_SECURE_LINK_NONCE_LEN);

    uint8_t* cipher_out = out + CADS_SECURE_FRAME_HEADER_LEN;
    uint8_t* mac_out = cipher_out + plain_len;
    cads_secure_link_seal(cipher_out, mac_out, key, nonce, ad, ad_len, plain, plain_len);

    return frame_len;
}

cads_secure_frame_status_t cads_secure_frame_decode(
    const uint8_t* in, size_t in_len, const uint8_t key[CADS_SECURE_LINK_KEY_LEN],
    const uint8_t* ad, size_t ad_len, uint8_t* plain_out, size_t plain_out_size,
    size_t* plain_len_out, size_t* consumed) {
    *consumed = 0u;

    if(in_len < CADS_SECURE_FRAME_HEADER_LEN) return CADS_SECURE_FRAME_INCOMPLETE;
    if(memcmp(in, CADS_SECURE_FRAME_MAGIC, CADS_SECURE_FRAME_MAGIC_LEN) != 0) {
        *consumed = 1u;
        return CADS_SECURE_FRAME_BAD_MAGIC;
    }

    uint32_t plain_len = read_u32_le(in + CADS_SECURE_FRAME_MAGIC_LEN);
    const uint8_t* nonce = in + CADS_SECURE_FRAME_MAGIC_LEN + CADS_SECURE_FRAME_LENGTH_LEN;

    /* frame_len cannot overflow size_t in practice (plain_len is at most
     * UINT32_MAX, size_t is at least 32 bits on every target this project
     * builds for), but computed as a wider addition anyway so a caller
     * fuzzing malformed length fields never trips UB here. */
    size_t frame_len = (size_t)plain_len + CADS_SECURE_FRAME_OVERHEAD;
    if(in_len < frame_len) return CADS_SECURE_FRAME_INCOMPLETE;

    if((size_t)plain_len > plain_out_size) return CADS_SECURE_FRAME_TOO_LARGE;

    const uint8_t* cipher = in + CADS_SECURE_FRAME_HEADER_LEN;
    const uint8_t* mac = cipher + plain_len;

    bool ok = cads_secure_link_open(plain_out, mac, key, nonce, ad, ad_len, cipher, plain_len);
    *consumed = frame_len;
    if(!ok) return CADS_SECURE_FRAME_AUTH_FAILED;

    *plain_len_out = plain_len;
    return CADS_SECURE_FRAME_OK;
}
