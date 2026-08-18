/*
 * CaDS Zero - kernel primitives over FreeRTOS.
 *
 * Entirely static: there is no kernel heap at all. Every task stack, queue and
 * mutex is storage its owner declares, so the memory a running system needs is
 * a link-time fact rather than a runtime hope. See FreeRTOSConfig.h.
 *
 * Task stacks belong in CCM - 64 KB no DMA controller can reach, which makes
 * it useless for buffers and ideal for stacks. Callers choose that with a
 * section attribute; this module does not place memory on their behalf.
 */

#include "cads/kernel/kernel.h"

#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "cads_hal.h"

static bool cads_scheduler_started;

/* Compile-time proof that the opaque storage in the public header is big
 * enough for the kernel's real objects. Getting this wrong would corrupt
 * adjacent memory, and a static assert costs nothing. */
_Static_assert(sizeof(StaticSemaphore_t) <= 80, "cads_mutex_t storage too small");
_Static_assert(sizeof(StaticQueue_t) <= 80, "cads_queue_t storage too small");

void cads_kernel_init(void) {
    cads_scheduler_started = false;
}

void cads_kernel_start(void) {
    cads_scheduler_started = true;
    vTaskStartScheduler();

    /* Only reached if the scheduler could not start, which on a static
     * configuration means the idle task's stack did not fit. */
    cads_hal_panic("scheduler failed to start");
}

bool cads_kernel_running(void) {
    return cads_scheduler_started && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

uint32_t cads_kernel_ticks(void) {
    return (uint32_t)xTaskGetTickCount();
}

void cads_kernel_sleep_ms(uint32_t ms) {
    if(!cads_kernel_running()) {
        /* Before the scheduler exists there is nothing to yield to, and a
         * driver that sleeps during init would otherwise hang forever. */
        cads_hal_delay_ms(ms);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void cads_kernel_sleep_until(uint32_t* last_wake, uint32_t period_ms) {
    if(!cads_kernel_running()) {
        cads_hal_delay_ms(period_ms);
        return;
    }
    TickType_t wake = (TickType_t)*last_wake;
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(period_ms));
    *last_wake = (uint32_t)wake;
}

/* --- threads -------------------------------------------------------------- */

bool cads_thread_start(
    cads_thread_t* thread,
    const char* name,
    cads_thread_fn_t entry,
    void* context,
    uint32_t* stack,
    uint32_t stack_words,
    cads_priority_t priority) {
    if(!thread || !entry || !stack || stack_words < configMINIMAL_STACK_SIZE) return false;

    static StaticTask_t cads_task_blocks[12];
    static uint32_t cads_task_block_count;

    if(cads_task_block_count >= (sizeof(cads_task_blocks) / sizeof(cads_task_blocks[0]))) {
        return false;
    }

    StaticTask_t* control = &cads_task_blocks[cads_task_block_count++];

    thread->name = name;
    thread->stack = stack;
    thread->stack_words = stack_words;
    thread->control = control;
    thread->handle = xTaskCreateStatic(
        entry, name, stack_words, context, (UBaseType_t)priority, stack, control);

    return thread->handle != NULL;
}

uint32_t cads_thread_stack_free(const cads_thread_t* thread) {
    if(!thread || !thread->handle) return 0u;
    return (uint32_t)uxTaskGetStackHighWaterMark((TaskHandle_t)thread->handle) * sizeof(uint32_t);
}

void cads_thread_yield(void) {
    if(cads_kernel_running()) taskYIELD();
}

/* --- mutex ---------------------------------------------------------------- */

void cads_mutex_init(cads_mutex_t* mutex) {
    mutex->handle = xSemaphoreCreateMutexStatic((StaticSemaphore_t*)mutex->storage);
}

bool cads_mutex_lock(cads_mutex_t* mutex, uint32_t timeout_ms) {
    if(!cads_kernel_running()) return true; /* single threaded before start */
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake((SemaphoreHandle_t)mutex->handle, ticks) == pdTRUE;
}

void cads_mutex_unlock(cads_mutex_t* mutex) {
    if(!cads_kernel_running()) return;
    (void)xSemaphoreGive((SemaphoreHandle_t)mutex->handle);
}

/* --- queue ---------------------------------------------------------------- */

void cads_queue_init(cads_queue_t* queue, void* buffer, uint32_t capacity, uint32_t item_size) {
    queue->handle =
        xQueueCreateStatic(capacity, item_size, (uint8_t*)buffer, (StaticQueue_t*)queue->storage);
}

bool cads_queue_send(cads_queue_t* queue, const void* item, uint32_t timeout_ms) {
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xQueueSend((QueueHandle_t)queue->handle, item, ticks) == pdTRUE;
}

bool cads_queue_receive(cads_queue_t* queue, void* item, uint32_t timeout_ms) {
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive((QueueHandle_t)queue->handle, item, ticks) == pdTRUE;
}

bool cads_queue_send_from_isr(cads_queue_t* queue, const void* item) {
    BaseType_t woken = pdFALSE;
    BaseType_t sent = xQueueSendFromISR((QueueHandle_t)queue->handle, item, &woken);
    portYIELD_FROM_ISR(woken);
    return sent == pdTRUE;
}

uint32_t cads_queue_count(const cads_queue_t* queue) {
    return (uint32_t)uxQueueMessagesWaiting((QueueHandle_t)queue->handle);
}

/* --- diagnostics ---------------------------------------------------------- */

uint32_t cads_kernel_task_count(void) {
    return (uint32_t)uxTaskGetNumberOfTasks();
}

__attribute__((noreturn)) void cads_kernel_assert(const char* file, int line) {
    (void)line;
    cads_hal_panic(file ? file : "kernel assert");
}

/* --- kernel hooks --------------------------------------------------------- */

void vApplicationStackOverflowHook(TaskHandle_t task, char* name) {
    (void)task;
    /* Halt rather than continue: a task that has run off its stack has already
     * written over whatever was next in memory. */
    cads_hal_panic(name ? name : "stack overflow");
}

void vApplicationGetIdleTaskMemory(
    StaticTask_t** tcb,
    StackType_t** stack,
    uint32_t* stack_words) {
    static StaticTask_t idle_tcb;
    static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
    *tcb = &idle_tcb;
    *stack = idle_stack;
    *stack_words = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(
    StaticTask_t** tcb,
    StackType_t** stack,
    uint32_t* stack_words) {
    static StaticTask_t timer_tcb;
    static StackType_t timer_stack[configTIMER_TASK_STACK_DEPTH];
    *tcb = &timer_tcb;
    *stack = timer_stack;
    *stack_words = configTIMER_TASK_STACK_DEPTH;
}
