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
