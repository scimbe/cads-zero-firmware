# Bake the central factory configuration (config/factory.txt) into a header.
#
#   cads_generate_factory_config(<factory file> <output header>)
#
# The file uses the same `key = value` grammar as the board's /config.txt. The
# generated header carries
#
#   CADS_FACTORY_CONFIG_TEXT   the normalised file (no comments, one
#                              `key = value\n` per line) as a C string - the
#                              firmware parses it in cads_config_defaults()
#   CADS_FACTORY_CONFIG_REV    32-bit revision, the first 8 hex digits of the
#                              MD5 of that text; a board re-seeds /config.txt
#                              when the revision it stored differs
#   CADS_FACTORY_<KEY>         every value as a string, KEY = upper-case key
#                              with '.' -> '_' (CADS_FACTORY_NET_IP), so unit
#                              tests and C code can read a single value
#
# Anything that is not `key = value`, a duplicate key, or a value with a quote
# or backslash stops the configure: a typo must not silently build the wrong
# image (same stance as the *.profile parser in the top-level CMakeLists.txt).
# The file is a configure dependency, so editing it regenerates the header.

function(cads_generate_factory_config factory_file out_header)
    if(NOT EXISTS "${factory_file}")
        message(FATAL_ERROR "CADS_FACTORY_CONFIG '${factory_file}' does not exist")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${factory_file}")

    file(STRINGS "${factory_file}" raw_lines)
    set(seen_keys "")
    set(normalised "")
    set(c_text "")
    set(c_values "")
    foreach(line IN LISTS raw_lines)
        string(REGEX REPLACE "#.*$" "" line "${line}")
        string(STRIP "${line}" line)
        if(line STREQUAL "")
            continue()
        endif()
        if(NOT line MATCHES "^([a-z0-9_]+\\.[a-z0-9_]+)[ \t]*=[ \t]*(.*)$")
            message(FATAL_ERROR "CADS_FACTORY_CONFIG ${factory_file}: unrecognised line '${line}' (expected 'section.key = value')")
        endif()
        set(key "${CMAKE_MATCH_1}")
        set(value "${CMAKE_MATCH_2}")
        string(STRIP "${value}" value)
        if(value MATCHES "[\"\\\\]")
            message(FATAL_ERROR "CADS_FACTORY_CONFIG ${factory_file}: value of '${key}' contains a quote or backslash")
        endif()
        if(key IN_LIST seen_keys)
            message(FATAL_ERROR "CADS_FACTORY_CONFIG ${factory_file}: '${key}' is set twice")
        endif()
        # Only keys the firmware's parser knows, with a value of the right shape.
        # The parser skips an unknown key and falls back on a malformed value
        # without a word - fine for a hand-edited /config.txt, wrong for the one
        # file an image is built from (a typo here would ship). wifi.ssid and
        # wifi.password are device-local on purpose and not accepted here.
        set(bool_keys boot.autostart display.fast_clock net.dhcp net.mac_random active.armed wifi.enabled)
        set(ip_keys net.ip net.netmask net.gateway wifi.pcap_target)
        set(octet "(0|[1-9][0-9]?|1[0-9][0-9]|2[0-4][0-9]|25[0-5])")
        if(key IN_LIST bool_keys)
            if(NOT value MATCHES "^(0|1|on|off|true|false|yes|no)$")
                message(FATAL_ERROR "CADS_FACTORY_CONFIG ${factory_file}: '${key} = ${value}' is not a boolean (0/1, on/off, true/false, yes/no)")
            endif()
        elseif(key IN_LIST ip_keys)
            if(NOT value MATCHES "^${octet}\\.${octet}\\.${octet}\\.${octet}$")
                message(FATAL_ERROR "CADS_FACTORY_CONFIG ${factory_file}: '${key} = ${value}' is not a dotted-decimal IPv4 address")
            endif()
        elseif(key STREQUAL "display.brightness")
            if(NOT value MATCHES "^[0-9]+$")
                message(FATAL_ERROR "CADS_FACTORY_CONFIG ${factory_file}: '${key} = ${value}' is not a number")
            endif()
        elseif(key STREQUAL "wifi.uart")
            if(NOT value MATCHES "^[a-z0-9_]+$" OR value STREQUAL "")
                message(FATAL_ERROR "CADS_FACTORY_CONFIG ${factory_file}: '${key} = ${value}' is not a UART name")
            endif()
        else()
            message(FATAL_ERROR "CADS_FACTORY_CONFIG ${factory_file}: unknown key '${key}' (known: ${bool_keys} ${ip_keys} display.brightness wifi.uart)")
        endif()
        list(APPEND seen_keys "${key}")
        string(APPEND normalised "${key} = ${value}\n")
        string(APPEND c_text "    \"${key} = ${value}\\n\" \\\n")
        string(TOUPPER "${key}" macro)
        string(REPLACE "." "_" macro "${macro}")
        string(APPEND c_values "#define CADS_FACTORY_${macro} \"${value}\"\n")
        # An IPv4 value also gets a numeric twin (host byte order) for static
        # initialisers, e.g. the net module's built-in address before the
        # config has been read: CADS_FACTORY_NET_IP_U32.
        if(value MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
            foreach(octet ${CMAKE_MATCH_1} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4})
                if(octet GREATER 255)
                    message(FATAL_ERROR "CADS_FACTORY_CONFIG ${factory_file}: '${key} = ${value}' has an octet above 255")
                endif()
            endforeach()
            math(EXPR u32 "(${CMAKE_MATCH_1} << 24) | (${CMAKE_MATCH_2} << 16) | (${CMAKE_MATCH_3} << 8) | ${CMAKE_MATCH_4}" OUTPUT_FORMAT HEXADECIMAL)
            string(APPEND c_values "#define CADS_FACTORY_${macro}_U32 ${u32}u\n")
        endif()
    endforeach()

    string(MD5 digest "${normalised}")
    string(SUBSTRING "${digest}" 0 8 rev)

    set(content
"/* Generated by cmake/cads_factory_config.cmake from ${factory_file} - do not edit. */
#ifndef CADS_FACTORY_CONFIG_GENERATED_H
#define CADS_FACTORY_CONFIG_GENERATED_H

#define CADS_FACTORY_CONFIG_REV 0x${rev}u
#define CADS_FACTORY_CONFIG_TEXT \\
${c_text}
${c_values}
#endif
")
    # file(CONFIGURE) leaves the file untouched when the content is unchanged,
    # so an unrelated reconfigure does not rebuild everything that includes it.
    file(CONFIGURE OUTPUT "${out_header}" CONTENT "${content}" @ONLY)
    message(STATUS "CaDS Zero factory config: ${factory_file} (rev ${rev})")
    set(CADS_FACTORY_CONFIG_REV_HEX "${rev}" PARENT_SCOPE)
endfunction()
