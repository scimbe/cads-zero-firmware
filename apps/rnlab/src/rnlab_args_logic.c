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
