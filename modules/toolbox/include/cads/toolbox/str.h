/*
 * CaDS Zero toolbox - bounded string helpers.
 *
 * The C library's string functions are unbounded (strcpy, strcat, strlen) or
 * bounded in a way that still surprises people (strncpy does not always
 * terminate). Firmware parses console input, so the difference between "the
 * command was too long" and "the stack is now somebody else's" is these
 * functions. Everything here takes the size of the destination and always
 * leaves it NUL terminated.
 *
 * The parsers return false rather than a plausible-looking zero. That is not
 * pedantry: the hardware explorer once set the backlight to 0% because a
 * truncated "b 90" parsed as an unremarkable zero and the command reported
 * success, and the black panel looked exactly like a display fault.
 */

#ifndef CADS_TOOLBOX_STR_H
#define CADS_TOOLBOX_STR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Length of `text`, examining at most `max` bytes. */
size_t cads_str_len(const char* text, size_t max);

/**
 * Copy `src` into `dst`, always terminating.
 *
 * Returns the length of `src`, so `result >= size` means the copy was
 * truncated - the same contract as strlcpy and as cads_fmt.
 */
size_t cads_str_copy(char* dst, size_t size, const char* src);

/** Append `src` to `dst`. Returns the length the result would have had. */
size_t cads_str_append(char* dst, size_t size, const char* src);

/** Negative, zero or positive, like strcmp. NULL sorts before everything. */
int cads_str_compare(const char* a, const char* b);

/** As cads_str_compare(), over at most `max` characters. */
int cads_str_compare_n(const char* a, const char* b, size_t max);

bool cads_str_equal(const char* a, const char* b);
bool cads_str_starts_with(const char* text, const char* prefix);

/** First character that is not a space or a tab. Never returns NULL for a
 *  non-NULL argument. */
const char* cads_str_skip_spaces(const char* text);

/**
 * Parse a decimal, hexadecimal or optionally signed decimal prefix.
 *
 * Leading spaces are skipped. Parsing stops at the first character that is not
 * a digit; `end`, when not NULL, is set to it, which is how a command loop
 * reads several arguments from one line. Returns false when no digit was
 * consumed or when the value does not fit, and then leaves `*value` untouched.
 *
 * cads_str_to_hex() accepts an optional "0x" prefix.
 */
bool cads_str_to_uint(const char* text, uint32_t* value, const char** end);
bool cads_str_to_int(const char* text, int32_t* value, const char** end);
bool cads_str_to_hex(const char* text, uint32_t* value, const char** end);

#ifdef __cplusplus
}
#endif

#endif /* CADS_TOOLBOX_STR_H */
