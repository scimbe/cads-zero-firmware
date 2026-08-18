# CaDS Zero - host simulator target (SDL2)

# Homebrew and most distributions ship an sdl2-config.cmake; the ones that do
# not still ship a pkg-config file, and CI images are split roughly evenly
# between the two.
find_package(SDL2 QUIET)

add_library(cads_sdl2 INTERFACE)
if(SDL2_FOUND)
    if(TARGET SDL2::SDL2)
        target_link_libraries(cads_sdl2 INTERFACE SDL2::SDL2)
    else()
        target_include_directories(cads_sdl2 INTERFACE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(cads_sdl2 INTERFACE ${SDL2_LIBRARIES})
    endif()
    message(STATUS "CaDS Zero simulator: SDL2 via find_package")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2_PC REQUIRED IMPORTED_TARGET sdl2)
    target_link_libraries(cads_sdl2 INTERFACE PkgConfig::SDL2_PC)
    message(STATUS "CaDS Zero simulator: SDL2 ${SDL2_PC_VERSION} via pkg-config")
endif()

add_library(cads_hal_sim STATIC hal_sim.c)

target_include_directories(cads_hal_sim
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CADS_ROOT}/core)

target_compile_definitions(cads_hal_sim PUBLIC CADS_TARGET_SIM=1)

target_link_libraries(cads_hal_sim PUBLIC cads_sdl2 PRIVATE cads_flags)

add_executable(cads-zero-sim main.c)

# cads_hal_sim comes after the portable libraries: it is what resolves their
# calls into core/cads_hal.h, exactly as cads_hal_itsboard does on the board.
target_link_libraries(cads-zero-sim
    PRIVATE cads_apps cads_services cads_gui cads_hal_sim cads_flags)
