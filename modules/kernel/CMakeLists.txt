# CaDS Zero - kernel module. FreeRTOS plus a thin, statically allocated wrapper.

set(FREERTOS ${CADS_ROOT}/lib/FreeRTOS-Kernel)

add_library(cads_kernel STATIC
    src/kernel.c
    ${FREERTOS}/tasks.c
    ${FREERTOS}/queue.c
    ${FREERTOS}/list.c
    ${FREERTOS}/timers.c
    ${FREERTOS}/event_groups.c
    ${FREERTOS}/stream_buffer.c
    ${FREERTOS}/portable/GCC/ARM_CM4F/port.c
)

target_include_directories(cads_kernel
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CADS_ROOT}/core
)

# FreeRTOS's own headers and the config are PUBLIC because port.c compiled into
# this target needs them, and because a caller that wants the raw API can reach
# it deliberately rather than by accident.
target_include_directories(cads_kernel PUBLIC
    ${FREERTOS}/include
    ${FREERTOS}/portable/GCC/ARM_CM4F
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(cads_kernel PUBLIC cads_flags)

# FreeRTOS is not warning-clean under this project's settings, and it is not
# our code to fix.
set_source_files_properties(
    ${FREERTOS}/tasks.c ${FREERTOS}/queue.c ${FREERTOS}/list.c ${FREERTOS}/timers.c
    ${FREERTOS}/event_groups.c ${FREERTOS}/stream_buffer.c
    ${FREERTOS}/portable/GCC/ARM_CM4F/port.c
    PROPERTIES COMPILE_OPTIONS "-Wno-conversion;-Wno-sign-conversion;-Wno-shadow;-Wno-cast-qual"
)
