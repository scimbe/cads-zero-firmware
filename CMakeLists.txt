cmake_minimum_required(VERSION 3.20)

# Toolchain files must be in place before project() runs, so the target is
# selected by pointing CMAKE_TOOLCHAIN_FILE at cmake/arm-none-eabi-gcc.cmake.
# Without it we build the host simulator and the unit tests.

project(cads-zero C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CADS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND CMAKE_MODULE_PATH ${CADS_ROOT}/cmake)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# SB121/SB122 on the Nucleo decide whether Arduino D11 lands on PA7 or PB5.
# See targets/itsboard/board.h and docs/HARDWARE.md.
set(CADS_SPI_MOSI_ON_PB5 0 CACHE STRING
    "1 after swapping SB121/SB122 so SPI1_MOSI uses PB5 and Ethernet keeps PA7")

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    set(CADS_TARGET "itsboard")
else()
    set(CADS_TARGET "sim")
endif()
message(STATUS "CaDS Zero target: ${CADS_TARGET}")

# --- optional apps -------------------------------------------------------
# Which GUI apps end up in apps/menu - and therefore in the firmware at
# all - is a build-time choice, not baked in. A shipped image built for
# one purpose does not need every diagnostic/demo app this project has
# accumulated, and RAM/flash margin is tight enough (docs/ROADMAP.md's
# own repeated notes on the linker's 48K SRAM floor, most recently hit
# by a single new explorer command) that "just build everything" is not
# free. Desktop and the menu itself are not options here - they are the
# shell every other app is reached through, not an app in their own
# right. Every option defaults ON, so an unconfigured build stays
# exactly what it always was.
option(CADS_APP_SETTINGS "Include the Settings app" ON)
option(CADS_APP_ABOUT "Include the About app" ON)
option(CADS_APP_GPIO "Include the GPIO app" ON)
option(CADS_APP_NETINFO "Include the Network Info app" ON)
option(CADS_APP_FILEBROWSER "Include the file browser app" ON)
option(CADS_APP_GAME "Include the Reflex Test game" ON)

# --- warnings ----------------------------------------------------------------
# Firmware that silently truncates an integer is firmware that fails in the
# field, so conversions are errors here rather than noise.
add_library(cads_flags INTERFACE)
target_compile_options(cads_flags INTERFACE
    -Wall
    -Wextra
    -Wshadow
    -Wundef
    -Wdouble-promotion
    -Wformat=2
    -Wno-unused-parameter
    $<$<CONFIG:Debug>:-Og>
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Release>:-Os>
    $<$<CONFIG:Release>:-g>
    -ffunction-sections
    -fdata-sections
)

# Every CADS_APP_* option above becomes a CADS_APP_*_ENABLED compile
# definition on cads_flags - which essentially every target in this
# project already links (cads_apps, cads_app_menu, and each per-app
# library alike) - rather than wiring it per target. A file that reaches
# into a specific optional app (apps/menu/cads_menu_app.c, and a few of
# apps/bringup's own single-app demo commands: explorer_gui_demo.c for
# gpio, explorer_filebrowser_demo.c for filebrowser, explorer_app_demo.c
# for gpio+game) guards that reach with #ifdef on the matching macro; a
# file that does not reach for an optional app needs no guard and sees
# no difference from before this section existed.
if(CADS_APP_SETTINGS)
    target_compile_definitions(cads_flags INTERFACE CADS_APP_SETTINGS_ENABLED)
endif()
if(CADS_APP_ABOUT)
    target_compile_definitions(cads_flags INTERFACE CADS_APP_ABOUT_ENABLED)
endif()
if(CADS_APP_GPIO)
    target_compile_definitions(cads_flags INTERFACE CADS_APP_GPIO_ENABLED)
endif()
if(CADS_APP_NETINFO)
    target_compile_definitions(cads_flags INTERFACE CADS_APP_NETINFO_ENABLED)
endif()
if(CADS_APP_FILEBROWSER)
    target_compile_definitions(cads_flags INTERFACE CADS_APP_FILEBROWSER_ENABLED)
endif()
if(CADS_APP_GAME)
    target_compile_definitions(cads_flags INTERFACE CADS_APP_GAME_ENABLED)
endif()

# --- modules -----------------------------------------------------------------
# The toolbox is target neutral: the same sources link into the firmware and
# into the host unit tests, which is the point of it sitting at the bottom of
# the dependency graph.
add_subdirectory(modules/toolbox)

# Storage is target neutral too, in the same sense: cads_storage.c and
# cads_kv.c are plain C11 against cads/storage/flash.h, and that header has
# two implementations (src/cads_flash_stm32f4.c, src/cads_flash_host.c)
# selected the same way this top-level file already picks CADS_TARGET.
add_subdirectory(modules/storage)

# Net is target neutral in the same sense as storage: cads/net/net.h has two
# implementations (src/cads_net_board.c, a real lwIP netif over hal_eth_mac.h;
# src/cads_net_sim.c, an honest "no link" stub - see that file's own header
# for why the simulator does not pretend to have RMII hardware).
add_subdirectory(modules/net)

# cads_cli.c (the shared command table) is portable; its TCP transport has
# the same board/host split as cads/net/net.h and for the same reason.
add_subdirectory(modules/cli)

if(CADS_TARGET STREQUAL "itsboard")
    add_subdirectory(modules/kernel)
endif()

# --- portable layers ---------------------------------------------------------
add_library(cads_gui STATIC
    gui/canvas.c
    gui/cads_splash.c
    gui/assets/cads_logo.c
    gui/fonts/cads_fonts.c
)
target_include_directories(cads_gui PUBLIC
    ${CADS_ROOT}/gui
    ${CADS_ROOT}/gui/fonts
    ${CADS_ROOT}/core
)
target_link_libraries(cads_gui PUBLIC cads_toolbox cads_flags)

add_library(cads_services STATIC
    services/input/cads_input.c
)
target_include_directories(cads_services PUBLIC ${CADS_ROOT}/services)
target_link_libraries(cads_services PUBLIC cads_gui cads_flags)

# The task layer has two implementations: one that drives a scheduler and one
# for the host, which has none. Everything else in cads_apps is portable.
if(CADS_TARGET STREQUAL "itsboard")
    set(CADS_TASKS_SOURCE apps/bringup/tasks.c)
    set(CADS_EXPLORER_ETH_SOURCE apps/bringup/explorer_eth.c)
    set(CADS_EXPLORER_SCREENCAST_SOURCE apps/bringup/explorer_screencast_demo.c)
    set(CADS_EXPLORER_HTTP_SOURCE apps/bringup/explorer_http_demo.c)
    set(CADS_EXPLORER_IPERF_SOURCE apps/bringup/explorer_iperf_demo.c)
    set(CADS_EXPLORER_PKTGEN_SOURCE apps/bringup/explorer_pktgen_demo.c)
    set(CADS_EXPLORER_SNIFF_SOURCE apps/bringup/explorer_sniff_demo.c)
    set(CADS_EXPLORER_MACTABLE_SOURCE apps/bringup/explorer_mactable_demo.c)
    set(CADS_EXPLORER_L2DISCOVER_SOURCE apps/bringup/explorer_l2discover_demo.c)
    set(CADS_EXPLORER_DHCPWATCH_SOURCE apps/bringup/explorer_dhcpwatch_demo.c)
    set(CADS_EXPLORER_ARPWATCH_SOURCE apps/bringup/explorer_arpwatch_demo.c)
    set(CADS_EXPLORER_SSDPWATCH_SOURCE apps/bringup/explorer_ssdpwatch_demo.c)
    set(CADS_EXPLORER_WOL_SOURCE apps/bringup/explorer_wol_demo.c)
    set(CADS_EXPLORER_FREQ_SOURCE apps/bringup/explorer_freq_demo.c)
    set(CADS_EXPLORER_PWM_SOURCE apps/bringup/explorer_pwm_demo.c)
    set(CADS_EXPLORER_LOGIC_SOURCE apps/bringup/explorer_logic_demo.c)
else()
    set(CADS_TASKS_SOURCE apps/bringup/tasks_sim.c)
    set(CADS_EXPLORER_ETH_SOURCE apps/bringup/explorer_eth_sim.c)
    set(CADS_EXPLORER_SCREENCAST_SOURCE apps/bringup/explorer_screencast_demo_sim.c)
    set(CADS_EXPLORER_HTTP_SOURCE apps/bringup/explorer_http_demo_sim.c)
    set(CADS_EXPLORER_IPERF_SOURCE apps/bringup/explorer_iperf_demo_sim.c)
    set(CADS_EXPLORER_PKTGEN_SOURCE apps/bringup/explorer_pktgen_demo_sim.c)
    set(CADS_EXPLORER_SNIFF_SOURCE apps/bringup/explorer_sniff_demo_sim.c)
    set(CADS_EXPLORER_MACTABLE_SOURCE apps/bringup/explorer_mactable_demo_sim.c)
    set(CADS_EXPLORER_L2DISCOVER_SOURCE apps/bringup/explorer_l2discover_demo_sim.c)
    set(CADS_EXPLORER_DHCPWATCH_SOURCE apps/bringup/explorer_dhcpwatch_demo_sim.c)
    set(CADS_EXPLORER_ARPWATCH_SOURCE apps/bringup/explorer_arpwatch_demo_sim.c)
    set(CADS_EXPLORER_SSDPWATCH_SOURCE apps/bringup/explorer_ssdpwatch_demo_sim.c)
    set(CADS_EXPLORER_WOL_SOURCE apps/bringup/explorer_wol_demo_sim.c)
    set(CADS_EXPLORER_FREQ_SOURCE apps/bringup/explorer_freq_demo_sim.c)
    set(CADS_EXPLORER_PWM_SOURCE apps/bringup/explorer_pwm_demo_sim.c)
    set(CADS_EXPLORER_LOGIC_SOURCE apps/bringup/explorer_logic_demo_sim.c)
endif()

# GUI stack: view/dispatcher/compositor, widgets, and the full M6 app tree -
# desktop, settings, about, gpio, netinfo, filebrowser and game, plus the menu
# that ties them together. apps/gpio was the first wired in (the initial
# hardware smoke test, no Ethernet dependency, directly hardware-observable);
# the rest followed once that stack was proven on real silicon. See
# docs/ROADMAP.md M3/M6.
#
# Every app past desktop is gated by its own CADS_APP_* option (see this
# file's own "optional apps" section above) - a library that is never
# add_subdirectory()'d here never gets built at all, not just excluded
# from the final link.
add_subdirectory(gui/view)
add_subdirectory(gui/widgets)
add_subdirectory(apps/desktop)
if(CADS_APP_SETTINGS)
    add_subdirectory(apps/settings)
endif()
if(CADS_APP_ABOUT)
    add_subdirectory(apps/about)
endif()
if(CADS_APP_NETINFO)
    add_subdirectory(apps/netinfo)
endif()
if(CADS_APP_GPIO)
    add_subdirectory(apps/gpio)
endif()
if(CADS_APP_FILEBROWSER)
    add_subdirectory(apps/filebrowser)
endif()
if(CADS_APP_GAME)
    add_subdirectory(apps/game)
endif()
add_subdirectory(apps/menu)

add_library(cads_apps STATIC
    apps/bringup/bringup.c
    apps/bringup/input_probe.c
    apps/bringup/explorer.c
    apps/bringup/explorer_capture_buffer.c
    apps/bringup/explorer_gui_demo.c
    apps/bringup/explorer_app_demo.c
    apps/bringup/explorer_storage_test.c
    apps/bringup/explorer_filebrowser_demo.c
    apps/bringup/explorer_cli_demo.c
    apps/bringup/explorer_arp_demo.c
    apps/bringup/explorer_ping_demo.c
    apps/bringup/explorer_traceroute_demo.c
    apps/bringup/explorer_continuity_demo.c
    ${CADS_TASKS_SOURCE}
    ${CADS_EXPLORER_ETH_SOURCE}
    ${CADS_EXPLORER_SCREENCAST_SOURCE}
    ${CADS_EXPLORER_HTTP_SOURCE}
    ${CADS_EXPLORER_IPERF_SOURCE}
    ${CADS_EXPLORER_PKTGEN_SOURCE}
    ${CADS_EXPLORER_SNIFF_SOURCE}
    ${CADS_EXPLORER_MACTABLE_SOURCE}
    ${CADS_EXPLORER_L2DISCOVER_SOURCE}
    ${CADS_EXPLORER_DHCPWATCH_SOURCE}
    ${CADS_EXPLORER_ARPWATCH_SOURCE}
    ${CADS_EXPLORER_SSDPWATCH_SOURCE}
    ${CADS_EXPLORER_WOL_SOURCE}
    ${CADS_EXPLORER_FREQ_SOURCE}
    ${CADS_EXPLORER_PWM_SOURCE}
    ${CADS_EXPLORER_LOGIC_SOURCE}
)
if(CADS_TARGET STREQUAL "itsboard")
    target_sources(cads_apps PRIVATE apps/bringup/explorer_kernel_test.c)
    target_sources(cads_apps PRIVATE apps/bringup/explorer_fault_test.c)
else()
    target_sources(cads_apps PRIVATE apps/bringup/explorer_kernel_test_sim.c)
    target_sources(cads_apps PRIVATE apps/bringup/explorer_fault_test_sim.c)
endif()
target_link_libraries(cads_apps PUBLIC cads_app_desktop cads_app_menu)
# cads_apps needs its own direct link against these three specifically (not
# just transitively through cads_app_menu, which links them PRIVATE and so
# does not re-export them) because explorer_gui_demo.c/
# explorer_filebrowser_demo.c/explorer_app_demo.c call straight into
# apps/gpio, apps/filebrowser and apps/game themselves - see those files'
# own CADS_APP_*_ENABLED guards for why each is conditional here too.
if(CADS_APP_GPIO)
    target_link_libraries(cads_apps PUBLIC cads_app_gpio)
endif()
if(CADS_APP_FILEBROWSER)
    target_link_libraries(cads_apps PUBLIC cads_app_filebrowser)
endif()
if(CADS_APP_GAME)
    target_link_libraries(cads_apps PUBLIC cads_app_game)
endif()
target_include_directories(cads_apps PUBLIC ${CADS_ROOT}/apps)
target_link_libraries(cads_apps PUBLIC cads_services cads_gui cads_flags cads_storage cads_net cads_cli)
if(CADS_TARGET STREQUAL "itsboard")
    target_link_libraries(cads_apps PUBLIC cads_kernel)
endif()

# --- target specific ---------------------------------------------------------
if(CADS_TARGET STREQUAL "itsboard")
    add_subdirectory(targets/itsboard)
    target_link_libraries(cads_gui PUBLIC cads_hal_itsboard)
    # cads_net_board.c calls into hal_eth_mac.h/hal_eth_mdio.h/hal_spi.h, but
    # modules/net/CMakeLists.txt cannot declare that dependency itself -
    # cads_hal_itsboard is add_subdirectory'd later than modules/net (same
    # reason it lists targets/itsboard's include paths by hand instead of
    # linking the target). Declaring it here, now that the target exists,
    # lets CMake's own link-line generation place cads_hal_itsboard whenever
    # (and however many times) the final executable actually needs it,
    # instead of trusting a fixed position in the hand-written link list.
    target_link_libraries(cads_net PUBLIC cads_hal_itsboard)

    set(CADS_LINKER_SCRIPT ${CADS_ROOT}/targets/itsboard/linker/cads_itsboard.ld)

    add_executable(cads-zero.elf targets/itsboard/main.c)
    target_link_libraries(cads-zero.elf PRIVATE cads_apps cads_services cads_gui cads_kernel cads_hal_itsboard cads_flags)
    target_link_options(cads-zero.elf PRIVATE
        -T${CADS_LINKER_SCRIPT}
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_BINARY_DIR}/cads-zero.map
        -Wl,--print-memory-usage
        -Wl,--no-warn-rwx-segments
        -nostartfiles
    )
    set_target_properties(cads-zero.elf PROPERTIES LINK_DEPENDS ${CADS_LINKER_SCRIPT})

    add_custom_command(TARGET cads-zero.elf POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:cads-zero.elf> ${CMAKE_BINARY_DIR}/cads-zero.bin
        COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:cads-zero.elf> ${CMAKE_BINARY_DIR}/cads-zero.hex
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:cads-zero.elf>
        COMMENT "Producing cads-zero.bin / .hex and reporting size"
    )
else()
    add_subdirectory(targets/sim)
    enable_testing()
    add_subdirectory(tests)
    # Needs cads-zero-sim to exist, and needs enable_testing() to have already
    # run in this directory scope for its add_test() calls to register with
    # ctest - both of which is why this is added from here rather than from
    # inside targets/sim/CMakeLists.txt itself, which runs before either.
    add_subdirectory(targets/sim/tests)
endif()
