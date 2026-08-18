# The toolbox is the bottom of the dependency graph: no HAL, no libc, nothing
# target specific. That is what lets the unit tests link it on the host and the
# firmware link the same object files for the board.

add_library(cads_toolbox STATIC
    src/ring.c
    src/fmt.c
    src/tap.c
    src/str.c
)

target_include_directories(cads_toolbox
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# cads_flags carries the project's warning set. It is defined by the top level
# build; when this module is configured on its own - which is how it gets built
# under a bare ctest run - an empty stand-in keeps the target graph valid.
if(NOT TARGET cads_flags)
    add_library(cads_flags INTERFACE)
endif()

target_link_libraries(cads_toolbox PRIVATE cads_flags)
