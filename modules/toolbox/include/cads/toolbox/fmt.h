/*
 * CaDS Zero toolbox - integer formatting into a caller-supplied buffer.
 *
 * There is no printf here and there never will be. Linking newlib's formatted
 * output costs upwards of 10 KB of flash and, in the vfprintf variants that
 * handle floats, several hundred bytes of stack - on a part with 192 KB of RAM
 * that is a poor trade for printing a pin number. Every function here writes
 * into memory the caller already owns, allocates nothing and recurses nowhere.
 *
 * The contract is snprintf's, because it is the one every C programmer already
 * knows: the result is always NUL terminated when `size` is non-zero, and the
 * return value is the length the complete output would have had. A caller
 * detects truncation with `result >= size`.
 */

#ifndef CADS_TOOLBOX_FMT_H
#define CADS_TOOLBOX_FMT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Longest output any function here can produce, excluding the NUL.
 *
 * Padding is what sets it: the digits of a uint32_t never exceed 10, but a
 * padded field is capped at this width so a bad `width` argument cannot turn
 * into an unbounded loop.
 */
#define CADS_FMT_MAX 32u

/** Buffer size that always holds any output of this module. */
#define CADS_FMT_BUFFER (CADS_FMT_MAX + 1u)

/** Decimal, unsigned. */
size_t cads_fmt_uint(char* out, size_t size, uint32_t value);

/** Decimal, signed, with a leading '-' when negative. INT32_MIN is exact. */
size_t cads_fmt_int(char* out, size_t size, int32_t value);

/**
 * Hexadecimal.
 *
 * `digits` of 0 means "as many as the value needs", so 0 prints as "0".
 * Otherwise exactly that many digits, zero padded, clamped to 8 - a fixed
 * width is what makes a column of register dumps readable, which is the only
 * reason anything prints hex here.
 */
size_t cads_fmt_hex(char* out, size_t size, uint32_t value, uint8_t digits, bool uppercase);

/**
 * Decimal, right aligned in `width` columns using `pad`.
 *
 * `pad` of '0' places the fill after the sign ("-007"), anything else before
 * it ("  -7"), which is what every other formatter does and what a reader
 * expects. Output shorter than `width` is never truncated: a number that does
 * not fit its column widens the column rather than losing a digit.
 */
size_t cads_fmt_uint_pad(char* out, size_t size, uint32_t value, uint8_t width, char pad);
size_t cads_fmt_int_pad(char* out, size_t size, int32_t value, uint8_t width, char pad);

#ifdef __cplusplus
}
#endif

#endif /* CADS_TOOLBOX_FMT_H */
