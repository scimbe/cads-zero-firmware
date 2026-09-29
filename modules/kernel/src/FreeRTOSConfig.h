/*
 * CaDS Zero - FreeRTOS configuration for STM32F429ZI.
 *
 * Two choices here are load bearing and should not be changed casually.
 *
 * THERE IS NO HEAP.
 * configSUPPORT_DYNAMIC_ALLOCATION is 0: every task, queue, mutex and timer is
 * allocated statically by its owner. On a device with 192 KB of RAM, no MMU
 * and no way to recover from an allocation failure at three in the morning,
 * the memory a running system needs should be a fact the linker can check, not
 * a hope the heap fulfils.
 *
 * The practical consequence is that the whole 64 KB of CCM holds nothing but
 * task stacks - a few kilobytes - leaving the entire 192 KB of DMA-capable
 * SRAM for the framebuffer, the display staging buffers and the Ethernet
 * descriptors, which is where memory is actually scarce.
 *
 * Re-enabling dynamic allocation is a decision, not a convenience. If some
 * future library demands malloc, give it its own pool rather than turning this
 * back on.
 *
 * INTERRUPT PRIORITIES.
 * configMAX_SYSCALL_INTERRUPT_PRIORITY is 5, so any ISR that calls a FromISR
 * API must have a numerically GREATER priority value (= lower urgency). The
 * drivers comply: the display DMA is 6 and the console UART is 8. An ISR at a
 * lower number than 5 must not touch the kernel at all - it is above the
 * masking level and would corrupt kernel state.
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1  /* CLZ on Cortex-M4 */
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      180000000u
#define configTICK_RATE_HZ                      1000u
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                128  /* words, so 512 bytes */
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS            1
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0

#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        0

#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                16
#define configTIMER_TASK_STACK_DEPTH            256

/* Diagnostics. Stack overflow checking is on in every build, not just debug:
 * silent stack corruption on a device with no MMU is not worth the handful of
 * cycles saved. */
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            0  /* no heap to fail */
/* On: vApplicationIdleHook (apps/bringup/tasks.c) rechecks the stack-guard
 * sentinels. The MSP - the shared ISR/handler stack - has no other overflow
 * check at all (configCHECK_FOR_STACK_OVERFLOW below sees task stacks only,
 * and only at a context switch). */
#define configUSE_IDLE_HOOK                     1
/* On: vApplicationTickHook (kernel.c) feeds the independent watchdog every
 * SysTick. See core/cads_hal.h's own comment on cads_hal_watchdog_init for
 * why the tick rather than an application task. */
#define configUSE_TICK_HOOK                     1
#define configUSE_TRACE_FACILITY                1
#define configGENERATE_RUN_TIME_STATS           0
#define configRECORD_STACK_HIGH_ADDRESS         1

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetCurrentTaskHandle       1

/* Cortex-M4 NVIC: 4 priority bits on STM32. */
#define configPRIO_BITS                         4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* Let the kernel own the three exceptions it needs, under the names the
 * generated vector table uses. */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

/* An assert that halts with the machine intact beats one that returns and
 * lets a corrupted scheduler keep running. */
extern void cads_kernel_assert(const char* file, int line);
#define configASSERT(x)                                  \
    do {                                                 \
        if((x) == 0) cads_kernel_assert(__FILE__, __LINE__); \
    } while(0)

#endif /* FREERTOS_CONFIG_H */
