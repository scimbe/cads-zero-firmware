# One executable per subject, so a failure names the module before anything is
# read, and so a test that hangs cannot take the rest of the suite with it.

function(cads_add_unit_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE cads_unity cads_flags)
    add_test(NAME ${name} COMMAND ${name})
    # A unit test that runs for a minute is a hung unit test.
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

# --- toolbox ------------------------------------------------------------------

cads_add_unit_test(test_ring test_ring.c)
target_link_libraries(test_ring PRIVATE cads_toolbox)

cads_add_unit_test(test_fmt test_fmt.c)
target_link_libraries(test_fmt PRIVATE cads_toolbox)

cads_add_unit_test(test_tap test_tap.c)
target_link_libraries(test_tap PRIVATE cads_toolbox)

cads_add_unit_test(test_str test_str.c)
target_link_libraries(test_str PRIVATE cads_toolbox)

# --- portable firmware layers -------------------------------------------------
#
# The subjects are compiled into the test binaries rather than linked from
# cads_gui / cads_services: canvas.c needs the Mach-O workaround in
# canvas_host.c to build on the host at all, and linking the firmware library
# as well would then define every canvas symbol twice.

add_library(cads_fake_hal STATIC fake_hal.c)
target_include_directories(cads_fake_hal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CADS_ROOT}/core
)
target_link_libraries(cads_fake_hal PRIVATE cads_flags)

cads_add_unit_test(test_canvas
    test_canvas.c
    canvas_host.c
    ${CADS_ROOT}/gui/fonts/cads_fonts.c
)
target_include_directories(test_canvas PRIVATE
    ${CADS_ROOT}/gui
    ${CADS_ROOT}/gui/fonts
)
target_link_libraries(test_canvas PRIVATE cads_fake_hal)

cads_add_unit_test(test_input
    test_input.c
    ${CADS_ROOT}/services/input/cads_input.c
)
target_include_directories(test_input PRIVATE ${CADS_ROOT}/services/input)
target_link_libraries(test_input PRIVATE cads_fake_hal)
