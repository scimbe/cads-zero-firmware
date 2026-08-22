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

cads_add_unit_test(test_mactable test_mactable.c)
target_link_libraries(test_mactable PRIVATE cads_toolbox)

cads_add_unit_test(test_freqcounter test_freqcounter.c)
target_link_libraries(test_freqcounter PRIVATE cads_toolbox)

cads_add_unit_test(test_l2discover test_l2discover.c)
target_link_libraries(test_l2discover PRIVATE cads_toolbox)

cads_add_unit_test(test_dhcpwatch test_dhcpwatch.c)
target_link_libraries(test_dhcpwatch PRIVATE cads_toolbox)

cads_add_unit_test(test_arpwatch test_arpwatch.c)
target_link_libraries(test_arpwatch PRIVATE cads_toolbox)

cads_add_unit_test(test_ssdpwatch test_ssdpwatch.c)
target_link_libraries(test_ssdpwatch PRIVATE cads_toolbox)

# --- storage --------------------------------------------------------------
#
# Sources live in modules/storage/tests/, not here - modules/storage's own
# CMakeLists.txt only adds that directory when configured standalone (see
# its comment), so the same three files are wired in again here for the
# combined `ctest --test-dir build/host` run, exactly the way toolbox's
# tests already are.

cads_add_unit_test(test_flash ${CADS_ROOT}/modules/storage/tests/test_flash.c)
target_link_libraries(test_flash PRIVATE cads_storage)

cads_add_unit_test(test_storage ${CADS_ROOT}/modules/storage/tests/test_storage.c)
target_link_libraries(test_storage PRIVATE cads_storage)

cads_add_unit_test(test_kv ${CADS_ROOT}/modules/storage/tests/test_kv.c)
target_link_libraries(test_kv PRIVATE cads_storage)

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

# --- M6 app tree ---------------------------------------------------------------
#
# Unlike test_canvas, this links the real cads_gui (not canvas_host.c): it
# never compiles canvas.c itself as one of this binary's own translation
# units, only links the already-built library transitively through
# cads_app_desktop/cads_app_menu, so there is no duplicate-symbol conflict
# to work around - the exact link graph cads-zero-sim itself already
# proves works on host, with cads_fake_hal standing in for cads_hal_sim.

cads_add_unit_test(test_app_tree test_app_tree.c)
target_link_libraries(test_app_tree PRIVATE
    cads_fake_hal
    cads_app_desktop
    cads_app_menu
    cads_app_settings
    cads_app_about
    cads_app_gpio
    cads_app_netinfo
    cads_app_filebrowser
    cads_app_game
)

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
