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
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"

#include "cads_hal.h"

static bool cads_scheduler_started;

/* Compile-time proof that the opaque storage in the public header is big
 * enough for the kernel's real objects. Getting this wrong would corrupt
 * adjacent memory, and a static assert costs nothing. */
_Static_assert(sizeof(StaticSemaphore_t) <= 80, "cads_mutex_t storage too small");
_Static_assert(sizeof(StaticQueue_t) <= 80, "cads_queue_t storage too small");
_Static_assert(sizeof(StaticTimer_t) <= 80, "cads_timer_t storage too small");
_Static_assert(sizeof(StaticEventGroup_t) <= 80, "cads_event_t storage too small");

void cads_kernel_init(void) {
    cads_scheduler_started = false;
}

/* See core/cads_hal.h's own comment on cads_hal_watchdog_init for the
 * reasoning; 2000 is the timeout in ms this passes down to it. */
#define CADS_WATCHDOG_TIMEOUT_MS 2000u

void cads_kernel_start(void) {
    cads_scheduler_started = true;

    /* Started here, immediately before the call that makes SysTick (and
     * therefore vApplicationTickHook below) start firing, so the gap
     * between "watchdog armed" and "watchdog being fed" is one tick
     * period, not an open window sized by whatever happens to run next. */
    cads_hal_watchdog_init(CADS_WATCHDOG_TIMEOUT_MS);

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

/* --- timer ------------------------------------------------------------------
 *
 * FreeRTOS's timer callback receives only the timer handle, not an arbitrary
 * context, so the caller's function pointer and context cannot travel through
 * FreeRTOS directly. xTimerCreateStatic's pvTimerID slot holds exactly one
 * pointer; that pointer is set to the owning cads_timer_t itself, which is
 * where the real callback and context already live (set by cads_timer_init()
 * before the timer can possibly fire), so the trampoline recovers both with
 * no allocation and no second lookup table.
 */

static void cads_timer_trampoline(TimerHandle_t handle) {
    cads_timer_t* timer = (cads_timer_t*)pvTimerGetTimerID(handle);
    timer->callback(timer->context);
}

void cads_timer_init(
    cads_timer_t* timer,
    const char* name,
    uint32_t period_ms,
    bool auto_reload,
    cads_timer_fn_t callback,
    void* context) {
    timer->callback = callback;
    timer->context = context;
    timer->handle = xTimerCreateStatic(
        name, pdMS_TO_TICKS(period_ms), auto_reload ? pdTRUE : pdFALSE, timer,
        cads_timer_trampoline, (StaticTimer_t*)timer->storage);
}

bool cads_timer_start(cads_timer_t* timer, uint32_t timeout_ms) {
    return xTimerStart((TimerHandle_t)timer->handle, pdMS_TO_TICKS(timeout_ms)) == pdPASS;
}

bool cads_timer_stop(cads_timer_t* timer, uint32_t timeout_ms) {
    return xTimerStop((TimerHandle_t)timer->handle, pdMS_TO_TICKS(timeout_ms)) == pdPASS;
}

bool cads_timer_reset(cads_timer_t* timer, uint32_t timeout_ms) {
    return xTimerReset((TimerHandle_t)timer->handle, pdMS_TO_TICKS(timeout_ms)) == pdPASS;
}

bool cads_timer_change_period(cads_timer_t* timer, uint32_t period_ms, uint32_t timeout_ms) {
    return xTimerChangePeriod(
               (TimerHandle_t)timer->handle, pdMS_TO_TICKS(period_ms), pdMS_TO_TICKS(timeout_ms)) ==
           pdPASS;
}

bool cads_timer_is_running(const cads_timer_t* timer) {
    return xTimerIsTimerActive((TimerHandle_t)timer->handle) != pdFALSE;
}

/* --- event ------------------------------------------------------------------ */

void cads_event_init(cads_event_t* event) {
    event->handle = xEventGroupCreateStatic((StaticEventGroup_t*)event->storage);
}

uint32_t cads_event_set(cads_event_t* event, uint32_t bits) {
    return (uint32_t)xEventGroupSetBits((EventGroupHandle_t)event->handle, (EventBits_t)bits);
}

uint32_t cads_event_clear(cads_event_t* event, uint32_t bits) {
    return (uint32_t)xEventGroupClearBits((EventGroupHandle_t)event->handle, (EventBits_t)bits);
}

uint32_t cads_event_get(const cads_event_t* event) {
    return (uint32_t)xEventGroupGetBits((EventGroupHandle_t)event->handle);
}

uint32_t cads_event_wait(
    cads_event_t* event,
    uint32_t bits,
    bool clear_on_exit,
    bool wait_for_all,
    uint32_t timeout_ms) {
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (uint32_t)xEventGroupWaitBits(
        (EventGroupHandle_t)event->handle, (EventBits_t)bits, clear_on_exit ? pdTRUE : pdFALSE,
        wait_for_all ? pdTRUE : pdFALSE, ticks);
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

/*
 * Feeds the independent watchdog once per SysTick (1 kHz, configTICK_RATE_HZ),
 * regardless of which task is running or what it is doing - deliberately
 * not tied to any application task's own progress. See core/cads_hal.h's
 * own comment on cads_hal_watchdog_init for the full reasoning: this
 * proves the interrupt subsystem is alive (catching a HardFault-recursion
 * lockup or an interrupts-disabled deadlock) with zero risk of a spurious
 * reset during any legitimate long operation, since none of them ever stop
 * the tick from firing.
 */
void vApplicationTickHook(void) {
    cads_hal_watchdog_feed();
}

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
