# CaDS Zero - config module. Portable like modules/storage: the parse/serialize
# half is pure C over caller buffers, load/save wrap cads/storage, and both
# implementations link the same object file on host and board.

# The factory configuration: ONE file (config/factory.txt) feeds the ITS and the
# student firmware image, Settings -> Factory reset and the unit tests. Point
# CADS_FACTORY_CONFIG at another file to build an image with other values.
include(${CMAKE_SOURCE_DIR}/cmake/cads_factory_config.cmake)
set(CADS_FACTORY_CONFIG "${CMAKE_SOURCE_DIR}/config/factory.txt" CACHE FILEPATH
    "Factory configuration baked into the firmware (key = value, see config/factory.txt)")
set(CADS_FACTORY_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
# A relative path (-DCADS_FACTORY_CONFIG=config/my-lab.txt) is relative to the
# source tree, not to the build directory CMake would otherwise resolve it in.
get_filename_component(CADS_FACTORY_CONFIG_ABS "${CADS_FACTORY_CONFIG}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
cads_generate_factory_config("${CADS_FACTORY_CONFIG_ABS}"
    "${CADS_FACTORY_GENERATED_DIR}/cads/config/cads_factory_config.h")

add_library(cads_config STATIC
    src/cads_config.c
)

target_include_directories(cads_config PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CADS_FACTORY_GENERATED_DIR})

target_link_libraries(cads_config PUBLIC cads_toolbox cads_storage PRIVATE cads_flags)
