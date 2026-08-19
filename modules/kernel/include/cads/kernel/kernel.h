/*
 * CaDS Zero - kernel primitives.
 *
 * A thin, deliberately small layer over FreeRTOS. The point is not to hide the
 * kernel - it is to give the rest of the firmware one vocabulary that also
 * exists in the simulator, where there is no FreeRTOS at all.
 *
 * Everything here is statically allocated. The caller supplies the storage for
 * a thread's stack and for a queue's items, because a device with 192 KB of
 * RAM and no MMU is better served by a link-time failure than by a heap that
 * runs out at three in the morning.
 */

#ifndef CADS_KERNEL_H
#define CADS_KERNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- lifecycle ------------------------------------------------------------ */

void cads_kernel_init(void);

/** Start scheduling. Does not return. */
void cads_kernel_start(void);

/** True once cads_kernel_start() has been called. Drivers use this to decide
 *  whether to block on a semaphore or spin, since both run before the
 *  scheduler exists. */
bool cads_kernel_running(void);

/* --- time ----------------------------------------------------------------- */

uint32_t cads_kernel_ticks(void);
void cads_kernel_sleep_ms(uint32_t ms);

/** Sleep until `*last_wake + period_ms`, for drift-free periodic work. */
void cads_kernel_sleep_until(uint32_t* last_wake, uint32_t period_ms);

/* --- threads -------------------------------------------------------------- */

typedef enum {
    CadsPriorityIdle = 0,
    CadsPriorityLow = 1,
    CadsPriorityNormal = 3,
    CadsPriorityHigh = 5,
    CadsPriorityRealtime = 6,
} cads_priority_t;

typedef struct cads_thread cads_thread_t;
typedef void (*cads_thread_fn_t)(void* context);

/**
 * Storage for one thread. Declare it statically; the stack must be in memory
 * the CPU can reach, and on this target that means anywhere - but note that
 * placing it in CCM (the default for kernel allocations) means DMA cannot read
 * from a buffer that lives on it.
 */
struct cads_thread {
    void* handle;      /**< opaque kernel handle          */
    void* control;     /**< opaque task control block     */
    uint32_t* stack;   /**< caller supplied, in words     */
    uint32_t stack_words;
    const char* name;
};

/**
 * Create and start a thread.
 *
 * `stack` and `stack_words` are the caller's; nothing is allocated. Returns
 * false if the kernel refused, which at this point means the parameters were
 * wrong rather than that memory ran out.
 */
bool cads_thread_start(
    cads_thread_t* thread,
    const char* name,
    cads_thread_fn_t entry,
    void* context,
    uint32_t* stack,
    uint32_t stack_words,
    cads_priority_t priority);

/** Words of the thread's stack never used. Zero means it overflowed and the
 *  overflow hook should already have fired. */
uint32_t cads_thread_stack_free(const cads_thread_t* thread);

void cads_thread_yield(void);

/* --- mutex ---------------------------------------------------------------- */

typedef struct {
    void* handle;
    uint8_t storage[80]; /**< static kernel object storage */
} cads_mutex_t;

void cads_mutex_init(cads_mutex_t* mutex);
bool cads_mutex_lock(cads_mutex_t* mutex, uint32_t timeout_ms);
void cads_mutex_unlock(cads_mutex_t* mutex);

/* --- queue ---------------------------------------------------------------- */

typedef struct {
    void* handle;
    uint8_t storage[80];
} cads_queue_t;

/**
 * Initialise a queue over caller-supplied item storage.
 * `buffer` must be at least `capacity * item_size` bytes.
 */
void cads_queue_init(
    cads_queue_t* queue,
    void* buffer,
    uint32_t capacity,
    uint32_t item_size);

bool cads_queue_send(cads_queue_t* queue, const void* item, uint32_t timeout_ms);
bool cads_queue_receive(cads_queue_t* queue, void* item, uint32_t timeout_ms);

/** ISR-safe send. Only callable from an interrupt whose priority is
 *  numerically >= configMAX_SYSCALL_INTERRUPT_PRIORITY. */
bool cads_queue_send_from_isr(cads_queue_t* queue, const void* item);

uint32_t cads_queue_count(const cads_queue_t* queue);

/* --- timer ------------------------------------------------------------------
 *
 * A software timer: its callback runs on FreeRTOS's timer service task, not
 * on the caller's thread and not in interrupt context. That matters for two
 * things a caller must get right - the callback must not block for long (it
 * shares the one timer task with every other timer in the system, so a slow
 * callback delays all of them), and it is free to call other kernel APIs
 * (mutexes, queues) that an ISR could not.
 */

typedef void (*cads_timer_fn_t)(void* context);

typedef struct {
    void* handle;
    /* FreeRTOS's timer callback receives only the timer handle, not an
     * arbitrary context - its one pvTimerID slot is used to point back at
     * this struct so the trampoline in kernel.c can recover both of these. */
    cads_timer_fn_t callback;
    void* context;
    uint8_t storage[80];
} cads_timer_t;

/**
 * Create a timer. It does not start running until cads_timer_start().
 *
 * `auto_reload`: true restarts the timer after every expiry (a periodic
 * tick); false fires once and stops (a timeout).
 */
void cads_timer_init(
    cads_timer_t* timer,
    const char* name,
    uint32_t period_ms,
    bool auto_reload,
    cads_timer_fn_t callback,
    void* context);

bool cads_timer_start(cads_timer_t* timer, uint32_t timeout_ms);
bool cads_timer_stop(cads_timer_t* timer, uint32_t timeout_ms);

/** Restart the period from now, without changing it. Starts the timer if it
 *  was not already running. */
bool cads_timer_reset(cads_timer_t* timer, uint32_t timeout_ms);

bool cads_timer_change_period(cads_timer_t* timer, uint32_t period_ms, uint32_t timeout_ms);
bool cads_timer_is_running(const cads_timer_t* timer);

/* --- event ------------------------------------------------------------------
 *
 * A rendezvous point for "wait until some combination of things has
 * happened" - the case a queue or a mutex does not fit, because there is no
 * single message and no single resource, just a set of independent
 * conditions a caller wants to wait on together. 24 flags: FreeRTOS reserves
 * the top 8 bits of the word this is built on for internal bookkeeping.
 */

#define CADS_EVENT_BIT_COUNT 24u

typedef struct {
    void* handle;
    uint8_t storage[80];
} cads_event_t;

void cads_event_init(cads_event_t* event);

/** Set bits and return the value immediately after the set (which may already
 *  reflect another thread's concurrent clear - only useful as a hint). */
uint32_t cads_event_set(cads_event_t* event, uint32_t bits);
uint32_t cads_event_clear(cads_event_t* event, uint32_t bits);
uint32_t cads_event_get(const cads_event_t* event);

/**
 * Block until `bits` are satisfied or `timeout_ms` elapses.
 *
 * `wait_for_all`: true waits for every bit in `bits` to be set, false for any
 * one of them. `clear_on_exit`: true atomically clears the bits that were
 * waited for before returning, which is what turns them into one-shot events
 * rather than persistent state - the usual choice unless another waiter also
 * needs to observe them.
 *
 * Returns the bits actually set at the moment of return (which can include
 * bits beyond the ones waited for); check `(result & bits) == bits` for
 * wait_for_all or `(result & bits) != 0` for wait-for-any to tell a real
 * satisfaction from a timeout.
 */
uint32_t cads_event_wait(
    cads_event_t* event,
    uint32_t bits,
    bool clear_on_exit,
    bool wait_for_all,
    uint32_t timeout_ms);

/* --- diagnostics ---------------------------------------------------------- */

/**
 * Number of tasks the scheduler knows about.
 *
 * There is no heap to report on: the kernel is configured for static
 * allocation only, so every task stack and kernel object is storage its owner
 * declared and the total is visible in the linker's memory report rather than
 * at run time. See modules/kernel/src/FreeRTOSConfig.h.
 */
uint32_t cads_kernel_task_count(void);

__attribute__((noreturn)) void cads_kernel_assert(const char* file, int line);

#endif /* CADS_KERNEL_H */
