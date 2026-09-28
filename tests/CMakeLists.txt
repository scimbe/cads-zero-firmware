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

if(NOT TARGET cads_diag)
    add_subdirectory(${CADS_ROOT}/modules/diag ${CMAKE_CURRENT_BINARY_DIR}/modules/diag)
endif()

# netx is the active-tooling counterpart to toolbox: pure frame builders with
# no HAL/lwIP deps, so it builds and tests standalone exactly the same way.
if(NOT TARGET cads_netx)
    add_subdirectory(${CADS_ROOT}/modules/netx ${CMAKE_CURRENT_BINARY_DIR}/modules/netx)
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

# Screen gallery: a host executable that renders every application view to a
# PPM, for reviewing all of the firmware's surfaces at once. Only wired up when
# the app targets exist (the full top-level host build), and it borrows
# cads_fake_hal from tests/unit as its HAL backend, so it is added after
# add_subdirectory(unit). It renders every app's views by name, so it also
# needs every one of those apps built (not the case with praktikum/start's
# student defaults, which leave filebrowser/game/active out).
if(TARGET cads_app_menu AND TARGET cads_gui AND TARGET cads_fake_hal AND
   TARGET cads_app_settings AND TARGET cads_app_about AND TARGET cads_app_gpio AND
   TARGET cads_app_netinfo AND TARGET cads_app_filebrowser AND TARGET cads_app_game AND
   TARGET cads_app_active)
    add_executable(cads_gallery gallery/gallery.c)
    target_link_libraries(cads_gallery PRIVATE
        cads_gui cads_gui_view cads_gui_widgets
        cads_app_desktop cads_app_menu cads_app_settings cads_app_about
        cads_app_gpio cads_app_netinfo cads_app_filebrowser cads_app_game
        cads_app_active
        cads_net cads_storage cads_toolbox cads_fake_hal cads_flags)
    target_include_directories(cads_gallery PRIVATE
        ${CADS_ROOT}/gui ${CADS_ROOT}/gui/fonts
        ${CADS_ROOT}/gui/view ${CADS_ROOT}/gui/widgets
        ${CADS_ROOT}/apps/desktop ${CADS_ROOT}/apps/menu ${CADS_ROOT}/apps/settings
        ${CADS_ROOT}/apps/about ${CADS_ROOT}/apps/gpio ${CADS_ROOT}/apps/netinfo
        ${CADS_ROOT}/apps/filebrowser ${CADS_ROOT}/apps/game ${CADS_ROOT}/apps/active
        ${CADS_ROOT}/services
        ${CADS_ROOT}/modules/net/include ${CADS_ROOT}/modules/storage/include
        ${CADS_ROOT}/core)

    # Run it as a smoke test: rendering every view must not crash. This is the
    # test that caught (and now guards against) an uninitialised-buffer stack
    # overflow in the game status-text drawing - which was non-deterministic,
    # so it also runs under the ASAN build in CI where the overflow is certain
    # to be caught, not just probable.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/gallery_out)
    add_test(NAME gallery_renders
        COMMAND cads_gallery ${CMAKE_CURRENT_BINARY_DIR}/gallery_out)
    set_tests_properties(gallery_renders PROPERTIES TIMEOUT 60)
endif()
