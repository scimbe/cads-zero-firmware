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

cads_add_unit_test(test_pubsub test_pubsub.c)
target_link_libraries(test_pubsub PRIVATE cads_toolbox)

cads_add_unit_test(test_record test_record.c)
target_link_libraries(test_record PRIVATE cads_toolbox)

cads_add_unit_test(test_log test_log.c)
target_link_libraries(test_log PRIVATE cads_toolbox)

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

# --- Ethernet diagnostics (MDIO-only, no RMII data path) -----------------------
#
# hal_eth_aneg.c and hal_eth_linklog.c only ever call cads_hal_eth_mdio_read(),
# never the board's real implementation - fake_mdio.c stands in for the whole
# MDIO bus, scripted per register, same discipline as fake_hal.h for the
# display/input HAL surface.

add_library(cads_fake_mdio STATIC fake_mdio.c)
target_include_directories(cads_fake_mdio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CADS_ROOT}/targets/itsboard/hal
)
target_link_libraries(cads_fake_mdio PRIVATE cads_flags)

cads_add_unit_test(test_eth_aneg
    test_eth_aneg.c
    ${CADS_ROOT}/targets/itsboard/hal/hal_eth_aneg.c
)
target_include_directories(test_eth_aneg PRIVATE ${CADS_ROOT}/targets/itsboard/hal)
target_link_libraries(test_eth_aneg PRIVATE cads_fake_mdio)

cads_add_unit_test(test_eth_linklog
    test_eth_linklog.c
    ${CADS_ROOT}/targets/itsboard/hal/hal_eth_linklog.c
)
target_include_directories(test_eth_linklog PRIVATE
    ${CADS_ROOT}/targets/itsboard/hal
    ${CADS_ROOT}/core
)
target_link_libraries(test_eth_linklog PRIVATE cads_fake_mdio cads_fake_hal)
