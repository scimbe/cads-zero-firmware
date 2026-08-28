/*
 * CaDS Zero - AEAD wrapper for the board <-> Mac link (2026-08-28).
 *
 * WHAT THIS IS
 * ------------
 * A thin, host-testable shim over Monocypher's crypto_aead_lock/unlock
 * (XChaCha20-Poly1305, RFC 8439-family - see lib/monocypher/src/monocypher.h
 * for the primitive itself). Exists so:
 *   - the rest of this firmware depends on a small CaDS-Zero-shaped API,
 *     not Monocypher's own naming, the same reasoning modules/storage
 *     wraps littlefs instead of exposing lfs.h everywhere;
 *   - the actual crypto logic has zero HAL dependency and is unit-testable
 *     with fixed keys/nonces on the host, the same "deliberately has zero
 *     HAL/GUI dependencies" discipline apps/marauder/cads_marauder_reader.h
 *     already documents for exactly this reason.
 *
 * WHAT THIS IS NOT (yet)
 * -----------------------
 * Not a wire protocol. This module only knows how to seal/open one buffer
 * given a key and a nonce - it has no opinion on how the key gets
 * provisioned onto the board, how the nonce gets generated (real hardware
 * should use cads_hal_rng_bytes(), see core/cads_hal.h's own "hardware
 * random number generator" section), or how sealed messages get framed
 * over the existing board<->Mac TCP link (port 4242, the `j` console
 * command). Wiring this into that link is tracked as a separate step, not
 * done here - see docs/reference/marauder-coprocessor.md and this
 * project's own ROADMAP.md for the fuller design discussion (fixed PSK, no
 * handshake, random-nonce-per-message specifically to avoid needing to
 * persist a send counter across reboots - see the design notes' own
 * "nonce reuse" warning for why that persistence question is not optional
 * to skip).
 *
 * SECURITY PROPERTIES, STATED EXPLICITLY
 * ---------------------------------------
 * - Confidentiality + integrity (AEAD) for one message, given a correct key
 *   and a nonce that is never reused under that same key.
 * - NO forward secrecy: this is a fixed pre-shared key with no per-session
 *   key agreement. If the key is ever extracted from a captured board, every
 *   message ever sealed under it becomes decryptable. This is a deliberate,
 *   accepted trade-off for a field pentesting device's realistic physical-
 *   capture threat model - not an oversight, but also not something a
 *   caller of this module should forget while relying on it.
 * - The 24-byte nonce MUST be unique per message under a given key. This
 *   module does not generate or track nonces itself (see above) - getting
 *   this wrong is the one mistake in this whole design that is catastrophic
 *   rather than merely weak (see the ROADMAP design discussion).
 */
#ifndef CADS_SECURITY_SECURE_LINK_H
#define CADS_SECURITY_SECURE_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CADS_SECURE_LINK_KEY_LEN   32u
#define CADS_SECURE_LINK_NONCE_LEN 24u
#define CADS_SECURE_LINK_MAC_LEN   16u

/**
 * Seal `plain` (plain_len bytes) into `cipher_out` (caller-allocated,
 * exactly plain_len bytes) plus a separate 16-byte authentication tag in
 * `mac_out`. `cipher_out` and `plain` may alias (Monocypher supports
 * in-place operation), matching crypto_aead_lock's own contract.
 *
 * `ad`/`ad_len` is optional associated data (pass NULL/0 to omit): bytes
 * that are authenticated but never encrypted - e.g. a protocol version or
 * sequence number the receiver needs to read before it can decrypt the
 * rest, or a value it wants covered by the same authentication check
 * without wanting it hidden in the first place.
 */
void cads_secure_link_seal(
    uint8_t* cipher_out, uint8_t mac_out[CADS_SECURE_LINK_MAC_LEN],
    const uint8_t key[CADS_SECURE_LINK_KEY_LEN], const uint8_t nonce[CADS_SECURE_LINK_NONCE_LEN],
    const uint8_t* ad, size_t ad_len, const uint8_t* plain, size_t plain_len);

/**
 * Open a message sealed by cads_secure_link_seal(). Returns true and fills
 * `plain_out` (caller-allocated, exactly cipher_len bytes) on success;
 * returns false on any authentication failure (wrong key, wrong nonce,
 * tampered ciphertext, tampered mac, or mismatched ad) and leaves
 * `plain_out` wiped rather than partially decrypted - a caller must never
 * use `plain_out` after a false return. `plain_out` and `cipher` may alias.
 */
bool cads_secure_link_open(
    uint8_t* plain_out, const uint8_t mac[CADS_SECURE_LINK_MAC_LEN],
    const uint8_t key[CADS_SECURE_LINK_KEY_LEN], const uint8_t nonce[CADS_SECURE_LINK_NONCE_LEN],
    const uint8_t* ad, size_t ad_len, const uint8_t* cipher, size_t cipher_len);

/**
 * Overwrite `size` bytes at `secret` with zeroes, in a way the compiler is
 * not permitted to optimize away (wraps Monocypher's own crypto_wipe -
 * exposed here so a caller holding key material doesn't need to depend on
 * monocypher.h directly just for this one call).
 */
void cads_secure_link_wipe(void* secret, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* CADS_SECURITY_SECURE_LINK_H */
