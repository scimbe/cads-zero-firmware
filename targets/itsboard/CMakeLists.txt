# CaDS Zero - ITSboard target (STM32F429ZI)

add_library(cads_hal_itsboard STATIC
    startup/startup_stm32f429.c
    startup/vectors_stm32f429.c
    startup/fault_handlers.c
    hal/hal_clock.c
    hal/hal_time.c
    hal/hal_console.c
    hal/hal_spi.c
    hal/hal_display.c
    hal/hal_touch.c
    hal/hal_io.c
    hal/hal_ports.c
    hal/hal_board_info.c
    hal/hal_eth_mdio.c
    hal/hal_eth_mac.c
    hal/hal_eth_tdr.c
    hal/hal_eth_aneg.c
    hal/hal_eth_linklog.c
    hal/hal_eth_mmc.c
    hal/hal_pktgen_timer.c
    hal/hal_freqcounter.c
    hal/hal_pwm.c
    hal/hal_sample_timer.c
    hal/hal_init.c
)

target_include_directories(cads_hal_itsboard PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/hal
    ${CADS_ROOT}/core
    ${CADS_ROOT}/lib/CMSIS_6/CMSIS/Core/Include
    ${CADS_ROOT}/lib/cmsis_device_f4/Include
)

target_compile_definitions(cads_hal_itsboard PUBLIC
    STM32F429xx
    CADS_TARGET_ITSBOARD=1
    CADS_SPI_MOSI_ON_PB5=${CADS_SPI_MOSI_ON_PB5}
)

# startup/fault_handlers.c formats hex dumps through cads/toolbox/fmt.h - the
# one place this library reaches into the toolbox instead of staying purely
# register-level, because hand-rolling hex formatting a second time is worse
# than one small dependency on code already proven not to allocate or lock.
target_link_libraries(cads_hal_itsboard PUBLIC cads_toolbox)

# The generated vector table must track the CMSIS header it was derived from.
add_custom_target(cads_regen_vectors
    COMMAND ${Python3_EXECUTABLE} ${CADS_ROOT}/scripts/gen_vectors.py
            ${CADS_ROOT}/lib/cmsis_device_f4/Include/stm32f429xx.h
            ${CMAKE_CURRENT_SOURCE_DIR}/startup/vectors_stm32f429.c
    COMMENT "Regenerating the STM32F429 vector table"
    VERBATIM
)
