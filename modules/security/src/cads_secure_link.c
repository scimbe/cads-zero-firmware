/* CaDS Zero - AEAD wrapper implementation. See the header for the full
 * design rationale; this file is deliberately thin - crypto_aead_lock/
 * unlock already do the real work correctly, this just adapts the API
 * shape and owns the failure-wipes-output contract the header promises. */
#include "cads/security/secure_link.h"

#include "monocypher.h"

void cads_secure_link_seal(
    uint8_t* cipher_out, uint8_t mac_out[CADS_SECURE_LINK_MAC_LEN],
    const uint8_t key[CADS_SECURE_LINK_KEY_LEN], const uint8_t nonce[CADS_SECURE_LINK_NONCE_LEN],
    const uint8_t* ad, size_t ad_len, const uint8_t* plain, size_t plain_len) {
    crypto_aead_lock(cipher_out, mac_out, key, nonce, ad, ad_len, plain, plain_len);
}

bool cads_secure_link_open(
    uint8_t* plain_out, const uint8_t mac[CADS_SECURE_LINK_MAC_LEN],
    const uint8_t key[CADS_SECURE_LINK_KEY_LEN], const uint8_t nonce[CADS_SECURE_LINK_NONCE_LEN],
    const uint8_t* ad, size_t ad_len, const uint8_t* cipher, size_t cipher_len) {
    int result = crypto_aead_unlock(plain_out, mac, key, nonce, ad, ad_len, cipher, cipher_len);
    if(result != 0) {
        /* Verified against the vendored source (crypto_aead_read() in
         * monocypher.c), not assumed: on a MAC mismatch, Monocypher's own
         * crypto_aead_unlock() skips the decrypt step entirely and leaves
         * plain_out completely UNTOUCHED - not wiped, not zeroed, just
         * whatever was already there (an earlier caller's leftover
         * plaintext, if this buffer is reused across calls). This header's
         * own contract promises something stronger ("leaves plain_out
         * wiped") specifically so a caller can never accidentally trust
         * stale sensitive data from a reused buffer after a failed open -
         * enforced here, not left to the underlying library. */
        crypto_wipe(plain_out, cipher_len);
        return false;
    }
    return true;
}

void cads_secure_link_wipe(void* secret, size_t size) {
    crypto_wipe(secret, size);
}
