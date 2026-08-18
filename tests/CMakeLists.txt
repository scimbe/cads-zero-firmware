# Host unit tests.
#
# Configurable two ways on purpose. From the top level this is just
# add_subdirectory(tests) in the host branch; on its own
# (cmake -S tests -B build/tests) it stands up everything it needs, which is
# what lets the unit tests run when the simulator target is not buildable.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(cads-zero-tests C)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    enable_testing()
endif()

if(NOT DEFINED CADS_ROOT)
    get_filename_component(CADS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
endif()

# Same warning set as the firmware when the top level defined it; a reduced
# stand-in otherwise, so a standalone run still fails on the mistakes that
# matter rather than compiling quietly.
if(NOT TARGET cads_flags)
    add_library(cads_flags INTERFACE)
    target_compile_options(cads_flags INTERFACE
        -Wall
        -Wextra
        -Wshadow
        -Wundef
        -Wno-unused-parameter
    )
endif()

if(NOT TARGET cads_toolbox)
    add_subdirectory(${CADS_ROOT}/modules/toolbox ${CMAKE_CURRENT_BINARY_DIR}/modules/toolbox)
endif()

# Unity is a submodule, not a package. It is built without cads_flags: its
# warnings are not ours to fix, and burying our own in them helps nobody.
if(NOT EXISTS ${CADS_ROOT}/lib/Unity/src/unity.c)
    message(FATAL_ERROR
        "lib/Unity is empty. Run: git submodule update --init lib/Unity")
endif()

add_library(cads_unity STATIC ${CADS_ROOT}/lib/Unity/src/unity.c)
target_include_directories(cads_unity PUBLIC ${CADS_ROOT}/lib/Unity/src)

add_subdirectory(unit)
