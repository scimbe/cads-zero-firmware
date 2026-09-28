/*
 * CaDS Zero - rnlab argument parsing. Pure logic, no HAL, no lwIP - host
 * tested by tests/unit/test_rnlab_args.c.
 */

#ifndef RNLAB_ARGS_LOGIC_H
#define RNLAB_ARGS_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Most words a `lab` line is split into. */
#define RNLAB_ARGV_MAX 8u

/**
 * Split `buffer` in place at spaces/tabs into at most `max` words. Returns
 * the word count; words beyond `max` are left joined to the last one's
 * tail untouched (the caller sees argc == max). `buffer` must be writable.
 */
int rnlab_split_args(char* buffer, char* argv[], int max);

/** Parse dotted-quad "a.b.c.d" into host byte order. False on anything
 *  else (missing octets, a value > 255, trailing garbage). */
bool rnlab_parse_ipv4(const char* text, uint32_t* ip);

/** Parse a lesson number "1".."11" or "01".."11". False otherwise. */
bool rnlab_parse_lesson(const char* text, uint32_t* lesson);

/** Reserved console bytes of the app tree's headless key injection
 *  (apps/bringup/explorer_app_demo.c, scripts/board_key.py): 0x80..0x87 are
 *  the eight logical keys, 0x88 leaves the app tree. */
#define RNLAB_KEY_CODE_QUIT 0x88u

/** Look up a `lab key` name - up, down, left, right, ok, back, f1, f2, quit
 *  (scripts/board_key.py's names) or s0..s7 (the physical buttons, bound
 *  positionally: Sn = key n), case-insensitive. False for anything else. */
bool rnlab_key_lookup(const char* name, uint8_t* code);

/** The names rnlab_key_lookup() accepts, for `lab key help`: a NULL-
 *  terminated list, canonical names first. */
extern const char* const rnlab_key_names[];

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_ARGS_LOGIC_H */
