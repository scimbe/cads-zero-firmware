/*
 * CaDS Zero toolbox - integer formatting.
 *
 * Every function builds its digits into a small automatic scratch buffer and
 * then copies the result out under the caller's size limit. The detour looks
 * wasteful and is not: producing digits least-significant first means the
 * output is only known in reverse, so writing straight into the destination
 * would either need a second pass or a partially-written buffer on truncation.
 * The scratch is CADS_FMT_BUFFER bytes on the stack and nothing here recurses.
 */

#include "cads/toolbox/fmt.h"

/** Copy `length` bytes of `text` out, terminating whatever fits. Returns
 *  `length` regardless, which is the snprintf contract the header promises. */
static size_t cads_fmt_emit(char* out, size_t size, const char* text, size_t length) {
    if(out && size > 0u) {
        size_t copy = length < (size - 1u) ? length : (size - 1u);
        for(size_t i = 0u; i < copy; i++) {
            out[i] = text[i];
        }
        out[copy] = '\0';
    }
    return length;
}

/** Digits of `value` into `scratch`, most significant first. Returns the count.
 *  `scratch` must hold at least 10 characters, the widest uint32_t. */
static size_t cads_fmt_digits(char* scratch, uint32_t value) {
    char reversed[10];
    size_t count = 0u;

    do {
        reversed[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while(value != 0u);

    for(size_t i = 0u; i < count; i++) {
        scratch[i] = reversed[count - 1u - i];
    }
    return count;
}

/** Magnitude of `value` as an unsigned. Written as a subtraction from zero
 *  because negating INT32_MIN is undefined, while the unsigned wrap is not. */
static uint32_t cads_fmt_magnitude(int32_t value) {
    return value < 0 ? (uint32_t)0u - (uint32_t)value : (uint32_t)value;
}

size_t cads_fmt_uint(char* out, size_t size, uint32_t value) {
    char scratch[CADS_FMT_BUFFER];
    size_t length = cads_fmt_digits(scratch, value);
    return cads_fmt_emit(out, size, scratch, length);
}

size_t cads_fmt_int(char* out, size_t size, int32_t value) {
    char scratch[CADS_FMT_BUFFER];
    size_t length = 0u;

    if(value < 0) scratch[length++] = '-';
    length += cads_fmt_digits(&scratch[length], cads_fmt_magnitude(value));

    return cads_fmt_emit(out, size, scratch, length);
}

size_t cads_fmt_hex(char* out, size_t size, uint32_t value, uint8_t digits, bool uppercase) {
    static const char lower[] = "0123456789abcdef";
    static const char upper[] = "0123456789ABCDEF";
    const char* alphabet = uppercase ? upper : lower;

    char scratch[CADS_FMT_BUFFER];
    size_t length;

    if(digits > 8u) digits = 8u;

    if(digits == 0u) {
        /* Natural width: the position of the highest set nibble, and at least
         * one digit so zero prints as "0" rather than as nothing. */
        length = 1u;
        for(uint32_t probe = value >> 4; probe != 0u; probe >>= 4) {
            length++;
        }
    } else {
        length = digits;
    }

    for(size_t i = 0u; i < length; i++) {
        scratch[length - 1u - i] = alphabet[(value >> (4u * i)) & 0x0Fu];
    }

    return cads_fmt_emit(out, size, scratch, length);
}

/** Shared body of the padded formatters: `body` is the already-formatted
 *  number, `sign_length` how much of it is a leading sign. */
static size_t cads_fmt_pad(
    char* out,
    size_t size,
    const char* body,
    size_t body_length,
    size_t sign_length,
    uint8_t width,
    char pad) {
    char scratch[CADS_FMT_BUFFER];
    size_t target = width > CADS_FMT_MAX ? CADS_FMT_MAX : (size_t)width;

    if(body_length >= target) return cads_fmt_emit(out, size, body, body_length);

    size_t fill = target - body_length;
    size_t at = 0u;

    /* Zero fill belongs after the sign ("-007"); any other fill belongs before
     * it ("  -7"), because a minus stranded among the spaces reads as a dash. */
    if(pad == '0') {
        for(size_t i = 0u; i < sign_length; i++) {
            scratch[at++] = body[i];
        }
    } else {
        sign_length = 0u;
    }

    for(size_t i = 0u; i < fill; i++) {
        scratch[at++] = pad;
    }
    for(size_t i = sign_length; i < body_length; i++) {
        scratch[at++] = body[i];
    }

    return cads_fmt_emit(out, size, scratch, at);
}

size_t cads_fmt_uint_pad(char* out, size_t size, uint32_t value, uint8_t width, char pad) {
    char body[CADS_FMT_BUFFER];
    size_t length = cads_fmt_digits(body, value);
    return cads_fmt_pad(out, size, body, length, 0u, width, pad);
}

size_t cads_fmt_int_pad(char* out, size_t size, int32_t value, uint8_t width, char pad) {
    char body[CADS_FMT_BUFFER];
    size_t length = 0u;
    size_t sign_length = 0u;

    if(value < 0) {
        body[length++] = '-';
        sign_length = 1u;
    }
    length += cads_fmt_digits(&body[length], cads_fmt_magnitude(value));

    return cads_fmt_pad(out, size, body, length, sign_length, width, pad);
}

size_t cads_fmt_ipv4(char* out, size_t size, uint32_t ip) {
    char scratch[16]; /* "255.255.255.255" */
    size_t length = 0u;

    for(int octet = 3; octet >= 0; octet--) {
        length += cads_fmt_digits(&scratch[length], (ip >> (octet * 8)) & 0xFFu);
        if(octet > 0) scratch[length++] = '.';
    }

    return cads_fmt_emit(out, size, scratch, length);
}

size_t cads_fmt_mac(char* out, size_t size, const uint8_t mac[6]) {
    char scratch[18]; /* "XX:XX:XX:XX:XX:XX" */
    size_t length = 0u;

    for(int i = 0; i < 6; i++) {
        length += cads_fmt_hex(&scratch[length], sizeof(scratch) - length, mac[i], 2u, true);
        if(i < 5) scratch[length++] = ':';
    }

    return cads_fmt_emit(out, size, scratch, length);
}
