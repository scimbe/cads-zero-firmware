# CaDS Zero - security module (board <-> Mac AEAD link).
#
# Self-contained like modules/toolbox and modules/storage: usable both as
# add_subdirectory(modules/security) from the top level and configured
# standalone, which is what lets the host unit tests run without the rest
# of the firmware.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(cads-zero-security C)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

if(NOT DEFINED CADS_ROOT)
    get_filename_component(CADS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

if(NOT TARGET cads_flags)
    add_library(cads_flags INTERFACE)
    target_compile_options(cads_flags INTERFACE -Wall -Wextra -Wshadow -Wundef -Wno-unused-parameter)
endif()

if(NOT EXISTS ${CADS_ROOT}/lib/monocypher/src/monocypher.c)
    message(FATAL_ERROR "lib/monocypher is empty - vendored directly (not a git submodule; see "
        "this file's own comment on cads_monocypher below), should not happen")
endif()

# Monocypher is vendored, not ours - built as its own library rather than
# folded into cads_security's sources for the same reason lib/littlefs gets
# the same treatment in modules/storage/CMakeLists.txt: its own code style
# is not ours to enforce cads_flags' warnings against.
#
# Vendored as two plain files (src/monocypher.c + .h) fetched directly from
# the upstream GitHub release tag v4.0.3 (commit ab2b16dd619ad5f6979a4fbe
# 69cfa324a6fcc35f), not a git submodule - Monocypher does not publish
# signed release tarballs or checksums to pin against, so the commit SHA
# itself, fetched over HTTPS, is the integrity anchor; the fetched bytes
# were independently re-fetched by tag name (`4.0.3`) and hash-compared
# byte-for-byte against the pinned-commit fetch before being committed here
# (sha256 f1f838cd...074123 for monocypher.c, fcaf6ed7...26fd7b3 for the
# header) precisely because this is cryptography source, where a silent
# transcription error is a security bug, not a build break. No dependencies,
# no dynamic allocation anywhere in it - fits a no-heap target as-is.
add_library(cads_monocypher STATIC
    ${CADS_ROOT}/lib/monocypher/src/monocypher.c
)
target_include_directories(cads_monocypher PUBLIC ${CADS_ROOT}/lib/monocypher/src)

add_library(cads_security STATIC
    src/cads_secure_link.c
)

target_include_directories(cads_security
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(cads_security PRIVATE cads_flags PUBLIC cads_monocypher)

# Unlike modules/storage, no standalone-configure tests/ subdirectory here -
# this module's tests live in the project's own tests/unit/ (test_secure_link.c)
# alongside test_marauder_reader.c and friends, since that is the convention
# actually exercised by this project's own build (cmake --preset host), and
# duplicating it here would be a second copy to keep in sync for a
# configuration path nothing currently uses.
