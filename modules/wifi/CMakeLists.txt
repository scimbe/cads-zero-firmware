# CaDS Zero - WiFi co-processor module (PPPoS to an ESP32 over USART6 on the
# board; an honest "no co-processor attached" stub on the simulator - see
# src/cads_wifi_sim.c's own header for why the host has no real PPP path at
# all, unlike modules/net's Ethernet netif). Depends on modules/net's cads_lwip
# target for the board build (PPP is compiled into that same library, see
# modules/net/CMakeLists.txt), so this must be add_subdirectory'd after it.

if(NOT DEFINED CADS_ROOT)
    get_filename_component(CADS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

if(NOT TARGET cads_flags)
    add_library(cads_flags INTERFACE)
    target_compile_options(cads_flags INTERFACE -Wall -Wextra -Wshadow -Wundef -Wno-unused-parameter)
endif()

add_library(cads_wifi STATIC)
target_include_directories(cads_wifi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cads_wifi PRIVATE cads_flags)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    target_sources(cads_wifi PRIVATE src/cads_wifi_board.c)
    # Same include-path duplication modules/net's CMakeLists.txt already
    # explains: targets/itsboard/CMakeLists.txt's cads_hal_itsboard target
    # does not exist yet at this point in the add_subdirectory order.
    target_include_directories(cads_wifi PRIVATE
        ${CADS_ROOT}/targets/itsboard
        ${CADS_ROOT}/targets/itsboard/hal
        ${CADS_ROOT}/core
        ${CADS_ROOT}/lib/CMSIS_6/CMSIS/Core/Include
        ${CADS_ROOT}/lib/cmsis_device_f4/Include
    )
    target_compile_definitions(cads_wifi PRIVATE STM32F429xx CADS_TARGET_ITSBOARD=1)
    target_link_libraries(cads_wifi PUBLIC cads_lwip)
else()
    target_sources(cads_wifi PRIVATE src/cads_wifi_sim.c)
    target_include_directories(cads_wifi PRIVATE ${CADS_ROOT}/core)
endif()
