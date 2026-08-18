/*
 * CaDS Zero toolbox - bounded string helpers.
 *
 * Implemented with plain loops rather than by wrapping <string.h> so the module
 * has no dependencies at all, which is what the module inventory in
 * docs/reference/module-layout.md promises. The loops are a handful of
 * instructions each; the freedom to link this into a startup path that runs
 * before the C library is initialised is worth more.
 */

#include "cads/toolbox/str.h"

/* Guard against a parse consuming an absurd run of digits: a value that cannot
 * fit is rejected anyway, and this bounds the loop when the input is not
 * terminated. Ten digits reach UINT32_MAX, eight nibbles reach it in hex. */
#define CADS_STR_MAX_DIGITS 20u

size_t cads_str_len(const char* text, size_t max) {
    size_t length = 0u;
    if(!text) return 0u;
    while(length < max && text[length] != '\0') {
        length++;
    }
    return length;
}

size_t cads_str_copy(char* dst, size_t size, const char* src) {
    size_t length = 0u;

    if(!src) src = "";
    while(src[length] != '\0') {
        length++;
    }

    if(dst && size > 0u) {
        size_t copy = length < (size - 1u) ? length : (size - 1u);
        for(size_t i = 0u; i < copy; i++) {
            dst[i] = src[i];
        }
        dst[copy] = '\0';
    }
    return length;
}

size_t cads_str_append(char* dst, size_t size, const char* src) {
    if(!dst || size == 0u) return cads_str_len(src, (size_t)-1);

    size_t used = cads_str_len(dst, size);

    /* An unterminated destination has no end to append to. Reporting the size
     * as the result still satisfies "result >= size means truncated". */
    if(used >= size) return size + cads_str_len(src, (size_t)-1);

    return used + cads_str_copy(dst + used, size - used, src);
}

int cads_str_compare_n(const char* a, const char* b, size_t max) {
    if(a == b) return 0;
    if(!a) return -1;
    if(!b) return 1;

    for(size_t i = 0u; i < max; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if(ca != cb) return ca < cb ? -1 : 1;
        if(ca == '\0') return 0;
    }
    return 0;
}

int cads_str_compare(const char* a, const char* b) {
    return cads_str_compare_n(a, b, (size_t)-1);
}

bool cads_str_equal(const char* a, const char* b) {
    return cads_str_compare(a, b) == 0;
}

bool cads_str_starts_with(const char* text, const char* prefix) {
    if(!text || !prefix) return false;
    for(size_t i = 0u; prefix[i] != '\0'; i++) {
        if(text[i] != prefix[i]) return false;
    }
    return true;
}

const char* cads_str_skip_spaces(const char* text) {
    if(!text) return text;
    while(*text == ' ' || *text == '\t') {
        text++;
    }
    return text;
}

/** Shared digit loop. `base` is 10 or 16; `limit` is the largest value that may
 *  result. Returns false on no digits or on overflow. */
static bool cads_str_parse(
    const char* text,
    uint32_t base,
    uint32_t limit,
    uint32_t* value,
    const char** end) {
    uint32_t accumulator = 0u;
    uint32_t consumed = 0u;

    for(; consumed < CADS_STR_MAX_DIGITS; consumed++) {
        char c = text[consumed];
        uint32_t digit;

        if(c >= '0' && c <= '9') {
            digit = (uint32_t)(c - '0');
        } else if(base == 16u && c >= 'a' && c <= 'f') {
            digit = (uint32_t)(c - 'a') + 10u;
        } else if(base == 16u && c >= 'A' && c <= 'F') {
            digit = (uint32_t)(c - 'A') + 10u;
        } else {
            break;
        }
        if(digit >= base) break;

        /* Check before multiplying: once the product has wrapped the evidence
         * that it did is gone. */
        if(accumulator > (limit - digit) / base) return false;
        accumulator = accumulator * base + digit;
    }

    if(consumed == 0u) return false;
    if(end) *end = &text[consumed];
    *value = accumulator;
    return true;
}

bool cads_str_to_uint(const char* text, uint32_t* value, const char** end) {
    if(!text || !value) return false;
    return cads_str_parse(cads_str_skip_spaces(text), 10u, UINT32_MAX, value, end);
}

bool cads_str_to_hex(const char* text, uint32_t* value, const char** end) {
    if(!text || !value) return false;

    const char* start = cads_str_skip_spaces(text);
    if(start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) start += 2;

    return cads_str_parse(start, 16u, UINT32_MAX, value, end);
}

bool cads_str_to_int(const char* text, int32_t* value, const char** end) {
    if(!text || !value) return false;

    const char* start = cads_str_skip_spaces(text);
    bool negative = false;

    if(*start == '-' || *start == '+') {
        negative = (*start == '-');
        start++;
    }

    /* The negative range is one wider than the positive one, and INT32_MIN has
     * to survive a round trip through cads_fmt_int(). */
    uint32_t limit = negative ? 2147483648u : 2147483647u;
    uint32_t magnitude = 0u;
    if(!cads_str_parse(start, 10u, limit, &magnitude, end)) return false;

    if(!negative) {
        *value = (int32_t)magnitude;
    } else if(magnitude == 0u) {
        *value = 0;
    } else {
        /* Split so no value above INT32_MAX is ever converted to a signed type,
         * which would be implementation defined - INT32_MIN is exactly the case
         * this function has to get right. */
        *value = -(int32_t)(magnitude - 1u) - 1;
    }
    return true;
}
