/*
 * CaDS Zero - wire framing for sealed messages (2026-08-30).
 *
 * WHAT THIS IS
 * ------------
 * cads_secure_link.h only knows how to seal/open one buffer given a key
 * and a nonce - it has no opinion on how that buffer travels over a real
 * byte stream (the board<->Mac TCP link, port 4242, the `j` console
 * command's cads_cli). This module is that: a self-describing frame
 * format plus a streaming decoder, so a caller reading raw bytes off a
 * socket (possibly split across TCP segments in any chunking, the same
 * "real streams don't deliver at protocol-boundary-aligned sizes"
 * discipline apps/marauder/cads_marauder_reader.h and
 * tools/screencast-viewer/lib/tcp-framer.js both already apply) can find
 * frame boundaries and hand complete, decrypted messages to the rest of
 * the firmware.
 *
 * WIRE FORMAT
 * -----------
 *   offset  size  field
 *   0       4     magic: CADS_SECURE_FRAME_MAGIC
 *   4       4     length, u32 little-endian - the PLAINTEXT length in
 *                 bytes (this AEAD never pads, so ciphertext is the same
 *                 length)
 *   8       24    nonce, in the clear - not secret, standard AEAD
 *                 practice: the receiver needs it to open the message,
 *                 and hiding it would buy nothing (see
 *                 cads_secure_link.h's own header on why a random nonce,
 *                 not a counter, was chosen)
 *   32      N     ciphertext, N = the length field above
 *   32+N    16    mac (CADS_SECURE_LINK_MAC_LEN)
 *
 * Total frame size = CADS_SECURE_FRAME_OVERHEAD + N.
 *
 * WHY A MAGIC AT ALL, AND WHY ITS FIRST BYTE IS >= 0x80
 * -------------------------------------------------------
 * The `j` console command's cads_cli is plain, line-buffered ASCII text
 * (0x20-0x7E) terminated by CR/LF - see modules/cli/include/cads/cli/cli.h.
 * A secure frame arriving on the same stream during any transition period
 * where both plaintext and sealed traffic exist must never be mistaken
 * for a CLI command line, and vice versa. This project already has
 * exactly this disambiguation problem solved once, the same way, for the
 * touchscreen's headless key-injection bytes
 * (apps/bringup/explorer_app_demo.c's own comment on why those are all
 * >= 0x80): a byte value a human typing on a real terminal, or cads_cli's
 * own line parser, can never produce or expect. CADS_SECURE_FRAME_MAGIC's
 * first byte follows the same rule.
 *
 * WHAT THIS IS NOT (yet)
 * -----------------------
 * Not wired into cads_cli, cads_cli_tcp, or any live transport - this
 * module only knows how to turn a plaintext buffer into a frame and back,
 * fully host-testable with zero HAL dependency, same discipline as
 * cads_secure_link.h. A caller (cads_cli's own read/write path, most
 * likely) still needs to be built to actually call this and to solve key
 * provisioning (a `security.psk` config key, not yet added to
 * modules/config - see cads_secure_link.h's own "WHAT THIS IS NOT" note,
 * still true) and nonce sourcing (cads_hal_rng_bytes() on the board).
 */
#ifndef CADS_SECURITY_SECURE_FRAME_H
#define CADS_SECURITY_SECURE_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cads/security/secure_link.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CADS_SECURE_FRAME_MAGIC_LEN 4u
/** {0xC5, 'D', 'S', '1'} - first byte >= 0x80 (see this file's own header
 *  comment on why), the rest kept ASCII-ish so the magic is still
 *  recognisable by eye in a hex dump. */
extern const uint8_t CADS_SECURE_FRAME_MAGIC[CADS_SECURE_FRAME_MAGIC_LEN];

#define CADS_SECURE_FRAME_LENGTH_LEN  4u
#define CADS_SECURE_FRAME_HEADER_LEN                                                             \
    (CADS_SECURE_FRAME_MAGIC_LEN + CADS_SECURE_FRAME_LENGTH_LEN + CADS_SECURE_LINK_NONCE_LEN)
#define CADS_SECURE_FRAME_TRAILER_LEN CADS_SECURE_LINK_MAC_LEN
/** Bytes of framing overhead around N bytes of plaintext: a complete
 *  frame is always exactly CADS_SECURE_FRAME_OVERHEAD + N bytes. */
#define CADS_SECURE_FRAME_OVERHEAD (CADS_SECURE_FRAME_HEADER_LEN + CADS_SECURE_FRAME_TRAILER_LEN)

/**
 * Encode one frame: seals `plain` (plain_len bytes) under `key`/`nonce`
 * (see cads_secure_link.h - the caller draws the nonce, this module never
 * generates one) and writes the complete wire frame to `out`.
 *
 * `ad`/`ad_len` is optional associated data (see cads_secure_link.h) -
 * authenticated but never placed on the wire by this function itself; a
 * caller that wants the receiver to see it must send it separately (e.g.
 * as its own plaintext field before this frame) and reconstruct the same
 * bytes before calling cads_secure_frame_decode().
 *
 * Returns the total frame length written (always plain_len +
 * CADS_SECURE_FRAME_OVERHEAD) on success, or 0 if `out_size` is too small
 * to hold it - nothing is written in that case.
 */
size_t cads_secure_frame_encode(
    uint8_t* out, size_t out_size, const uint8_t key[CADS_SECURE_LINK_KEY_LEN],
    const uint8_t nonce[CADS_SECURE_LINK_NONCE_LEN], const uint8_t* ad, size_t ad_len,
    const uint8_t* plain, size_t plain_len);

typedef enum {
    /** A complete, authentic frame was decoded - `*plain_len_out` and
     *  `*consumed` are both valid. */
    CADS_SECURE_FRAME_OK = 0,
    /** `in` does not yet hold a complete frame - wait for more bytes and
     *  call again with the same start plus whatever arrived since.
     *  `*consumed` is 0. */
    CADS_SECURE_FRAME_INCOMPLETE,
    /** The bytes at `in[0..MAGIC_LEN)` do not match
     *  CADS_SECURE_FRAME_MAGIC - not a secure frame (could be plaintext
     *  cads_cli traffic, or a desynced stream). `*consumed` is 1, so a
     *  caller resyncing byte-by-byte can just call again after advancing
     *  one byte. */
    CADS_SECURE_FRAME_BAD_MAGIC,
    /** The frame's own length field exceeds `plain_out_size` (the
     *  caller's buffer) - `*consumed` is 0, since this is a caller sizing
     *  problem, not a stream-corruption one; a caller than cannot grow
     *  its buffer has no safe way to skip exactly this frame without
     *  first knowing its own length, which it now does
     *  (CADS_SECURE_FRAME_HEADER_LEN..+4 bytes of `in`). */
    CADS_SECURE_FRAME_TOO_LARGE,
    /** A complete frame was present but failed to open (wrong key, wrong
     *  nonce, tampered ciphertext/mac, or an `ad` mismatch against what
     *  the caller passed in). `*consumed` is the full frame length, so a
     *  caller can discard exactly this one frame and keep reading -
     *  `plain_out` is left wiped, matching cads_secure_link_open()'s own
     *  contract. */
    CADS_SECURE_FRAME_AUTH_FAILED,
} cads_secure_frame_status_t;

/**
 * Decode one frame from the front of `in` (in_len bytes - may be a
 * partial frame, or hold more than one frame's worth; only the first
 * frame is ever decoded per call). `ad`/`ad_len` must be exactly what the
 * sender authenticated (see cads_secure_frame_encode()'s own note) or the
 * open fails.
 *
 * On CADS_SECURE_FRAME_OK: `plain_out` (caller-allocated, at least
 * `plain_out_size` bytes) holds the decrypted message,
 * `*plain_len_out` is its length, and `*consumed` is how many bytes of
 * `in` this frame occupied - advance the stream position by exactly that
 * much before the next call. On any other status, see that status's own
 * doc comment above for what `*consumed` means and what to do next.
 */
cads_secure_frame_status_t cads_secure_frame_decode(
    const uint8_t* in, size_t in_len, const uint8_t key[CADS_SECURE_LINK_KEY_LEN],
    const uint8_t* ad, size_t ad_len, uint8_t* plain_out, size_t plain_out_size,
    size_t* plain_len_out, size_t* consumed);

#ifdef __cplusplus
}
#endif

#endif /* CADS_SECURITY_SECURE_FRAME_H */
