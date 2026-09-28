#include "rnlab_args_logic.h"

#include "cads/toolbox/str.h"

int rnlab_split_args(char* buffer, char* argv[], int max) {
    int argc = 0;
    char* cursor = buffer;
    while(cursor && *cursor != '\0' && argc < max) {
        while(*cursor == ' ' || *cursor == '\t') cursor++;
        if(*cursor == '\0') break;
        argv[argc++] = cursor;
        if(argc == max) break; /* the last word keeps the rest of the line */
        while(*cursor != '\0' && *cursor != ' ' && *cursor != '\t') cursor++;
        if(*cursor != '\0') *cursor++ = '\0';
    }
    return argc;
}

bool rnlab_parse_ipv4(const char* text, uint32_t* ip) {
    if(!text || !ip) return false;
    uint32_t value = 0u;
    const char* cursor = text;
    for(int octet = 0; octet < 4; octet++) {
        uint32_t part;
        const char* end;
        if(*cursor < '0' || *cursor > '9') return false;
        if(!cads_str_to_uint(cursor, &part, &end) || part > 255u || end - cursor > 3) return false;
        value = (value << 8) | part;
        cursor = end;
        if(octet < 3) {
            if(*cursor != '.') return false;
            cursor++;
        }
    }
    if(*cursor != '\0') return false;
    *ip = value;
    return true;
}

bool rnlab_parse_lesson(const char* text, uint32_t* lesson) {
    if(!text || !lesson) return false;
    uint32_t value;
    const char* end;
    if(*text < '0' || *text > '9') return false;
    if(!cads_str_to_uint(text, &value, &end) || *end != '\0' || end - text > 2) return false;
    if(value < 1u || value > 11u) return false;
    *lesson = value;
    return true;
}

/* Order = code: board_key.py's up..f2 are 0x80..0x87 = CadsKeyUp..F2 + 0x80,
 * which is also Sn's positional binding (services/input/cads_input.h). */
const char* const rnlab_key_names[] = {
    "up", "down", "left", "right", "ok", "back", "f1", "f2", "quit", NULL,
};

static char rnlab_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool rnlab_name_equal(const char* a, const char* b) {
    while(*a && *b) {
        if(rnlab_lower(*a) != *b) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

bool rnlab_key_lookup(const char* name, uint8_t* code) {
    if(!name || !code) return false;
    for(uint32_t i = 0; rnlab_key_names[i] != NULL; i++) {
        if(rnlab_name_equal(name, rnlab_key_names[i])) {
            *code = (uint8_t)(0x80u + i);
            return true;
        }
    }
    /* s0..s7: the button under the display, same binding as the keys */
    if(rnlab_lower(name[0]) == 's' && name[1] >= '0' && name[1] <= '7' && name[2] == '\0') {
        *code = (uint8_t)(0x80u + (uint32_t)(name[1] - '0'));
        return true;
    }
    return false;
}
