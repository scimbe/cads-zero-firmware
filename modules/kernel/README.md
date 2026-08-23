# `modules/kernel` — threads, mutexes, queues, timers, and events over FreeRTOS

## What is it?

A thin wrapper (`include/cads/kernel/kernel.h`, implemented in `src/kernel.c`)
around the vendored FreeRTOS-Kernel (`lib/FreeRTOS-Kernel`), built into a single
static library, `cads_kernel`, together with FreeRTOS's own `tasks.c`,
`queue.c`, `list.c`, `timers.c`, `event_groups.c`, `stream_buffer.c` and the
Cortex-M4F port (`modules/kernel/CMakeLists.txt`). It exposes five primitives
under the `cads_` prefix — `cads_thread_t`, `cads_mutex_t`, `cads_queue_t`,
`cads_timer_t`, `cads_event_t` — plus lifecycle and time functions
(`cads_kernel_init`/`_start`/`_running`, `cads_kernel_ticks`,
`cads_kernel_sleep_ms`, `cads_kernel_sleep_until`), instead of handing the rest
of the firmware `xTaskCreate`, `SemaphoreHandle_t` and FreeRTOS's own naming
directly. `apps/bringup/tasks.c` is the real caller: it starts the `input`,
`ui` and `console` tasks through this module and uses a `cads_mutex_t` to
enforce the single-flusher rule on the display. The explorer console's `x`
command (`apps/bringup/explorer_kernel_test.c`) is this module's own proof of
life on target — it starts a one-shot `cads_timer_t` and blocks on a
`cads_event_t`, which only passes if a timer callback really ran on a
different task and woke the caller through the kernel.

## Why is it shaped this way?

**There is no kernel heap.** `FreeRTOSConfig.h` sets
`configSUPPORT_DYNAMIC_ALLOCATION` to `0` and `configSUPPORT_STATIC_ALLOCATION`
to `1`. Every `cads_mutex_t`, `cads_queue_t`, `cads_timer_t` and `cads_event_t`
therefore carries its own `uint8_t storage[80]` and hands FreeRTOS's
`*CreateStatic` constructors a pointer into it, rather than asking a heap for
one. `kernel.c` backs that number with four `_Static_assert`s
(`sizeof(StaticSemaphore_t) <= 80`, and the same for `StaticQueue_t`,
`StaticTimer_t`, `StaticEventGroup_t`) so a future FreeRTOS upgrade that grows
one of those structs fails the build instead of quietly overrunning adjacent
memory. Thread creation is the same story one level up: `cads_thread_start()`
draws from a fixed pool, `static StaticTask_t cads_task_blocks[12]`, and
returns `false` once it is exhausted rather than reaching for more RAM.
`docs/reference/memory-map.md` explains why this matters here specifically —
192 KB of RAM with no MMU and no way to recover from a failed allocation at
three in the morning means the memory a running system needs should be a
link-time fact, not a runtime hope.

**Stacks belong in CCM, and this module will not put them there for you.**
CCM (`0x10000000`, 64 KB) is invisible to every DMA controller on this part —
a transfer sourced from it produces silently wrong output, not a fault — so
`docs/reference/memory-map.md` reserves it for CPU-only data, and task stacks
are exactly that. `kernel.c`'s own file comment says so directly: "this module
does not place memory on their behalf." The caller applies `CADS_CCM_SECTION`
(`core/cads_hal.h`) to its own stack arrays, as `apps/bringup/tasks.c` does for
all three of its task stacks; `cads_thread_start()` only checks that
`stack_words >= configMINIMAL_STACK_SIZE` and otherwise trusts what it is
given.

**Priority 6 is the ceiling for a reason.** `configMAX_PRIORITIES` is 8
(0–7), and `FreeRTOSConfig.h` pins FreeRTOS's own timer service task to
`configTIMER_TASK_PRIORITY = (configMAX_PRIORITIES - 1)`, i.e. 7 — one above
`CadsPriorityRealtime` (6), the highest level `cads_priority_t` offers an
application thread. That is not an accident of numbering: it guarantees a
timer callback always preempts every application task, including one running
at `CadsPriorityRealtime`, and that no application task can starve the timer
service in return.

**`cads_kernel_running()` exists because drivers run before the scheduler
does.** `apps/bringup` initialises hardware during `cads_kernel_init()` and
`cads_tasks_start()`, all before `cads_kernel_start()` ever calls
`vTaskStartScheduler()`. Before that call there is nothing to block on, so
`cads_kernel_sleep_ms()`/`cads_kernel_sleep_until()` fall back to
`cads_hal_delay_ms()` and `cads_mutex_lock()` returns `true` immediately
("single threaded before start" — `kernel.c`) instead of calling into a
scheduler that is not running yet.

**The timer trampoline exists because FreeRTOS's timer callback only carries
the timer handle, not an arbitrary context.** `xTimerCreateStatic()`'s one
`pvTimerID` slot is set to the owning `cads_timer_t` itself — which already
holds the real `callback` and `context`, set by `cads_timer_init()` before the
timer can possibly fire — so `cads_timer_trampoline()` recovers both with no
allocation and no lookup table. That callback runs on FreeRTOS's one timer
service task, shared by every timer in the system, which is why the header
warns it must not block for long and why a caller that needs mutexes or
queues from a timer (unlike from an ISR) is free to use them — the timer task
is an ordinary task, not interrupt context.

**`CADS_EVENT_BIT_COUNT` is 24, not 32,** because `cads_event_t` is built on
FreeRTOS's `EventBits_t` word and, per the header, "FreeRTOS reserves the top
8 bits of the word this is built on for internal bookkeeping." The limit is
inherited from the primitive underneath, not chosen here.

**The ISR-safe path is deliberately narrow.** `configMAX_SYSCALL_INTERRUPT_PRIORITY`
is 5 (`FreeRTOSConfig.h`), so any ISR that touches the kernel must sit at a
numerically *higher* priority value than 5 — the display DMA (6) and the
console UART (8) both comply; an ISR above that masking level (a lower
number) must never call in, or it corrupts kernel state. `cads_queue_send_from_isr()`
is the one function built for that path; it calls `portYIELD_FROM_ISR()`
internally so the ISR itself does not have to. There is no
`cads_mutex`/`cads_event` equivalent — only the queue got one, because that is
the one FreeRTOS ISR API this module currently has a caller for.

**`configASSERT` halts rather than returns.** `FreeRTOSConfig.h` wires it to
`cads_kernel_assert()`, which calls `cads_hal_panic()` — the same choice
`vApplicationStackOverflowHook()` makes in `kernel.c`: a task that has
overrun its stack has already written over whatever memory came next, so the
hook halts instead of letting a corrupted scheduler keep running.
`configCHECK_FOR_STACK_OVERFLOW` is 2 in every build, debug or not, because
silent corruption on a part with no MMU is not worth the cycles saved.

**FreeRTOS's own sources are not warning-clean under this project's flags,
and `modules/kernel/CMakeLists.txt` says so rather than fixing it**: it turns
off `-Wconversion`, `-Wsign-conversion`, `-Wshadow` and `-Wcast-qual` for
`tasks.c`, `queue.c`, `list.c`, `timers.c`, `event_groups.c`,
`stream_buffer.c` and `port.c` specifically — "it is not our code to fix."
`kernel.c` itself carries no such exemption; the boundary between "this
module's code" and "vendored code this module builds" is drawn at that file
list, not at the library target.

**This module is target-only.** The top-level `CMakeLists.txt` only links
`cads_kernel` into `cads_apps` and `cads-zero.elf` inside
`if(CADS_TARGET STREQUAL "itsboard")`. The host simulator runs
`apps/bringup/tasks_sim.c` instead, which has no scheduler and collapses
threading to its single-threaded equivalent, and `explorer_kernel_test_sim.c`
answers the console's `x` command with "not available in the simulator (no
FreeRTOS on the host)" rather than faking a pass. The header's own opening
comment — this is meant to be "one vocabulary that also exists in the
simulator" — refers to the *shape* of the API (threads, mutexes, timers,
events as concepts other portable code can reason about), not to this library
compiling there; on the host, nothing currently implements that vocabulary
against real concurrency.

## How do I use it?

```c
#include "cads/kernel/kernel.h"
#include "cads_hal.h"

#define WORKER_STACK_WORDS 256u
#define HEARTBEAT_BIT (1u << 0)

/* Stacks are caller-owned and belong in CCM - no DMA controller can reach it,
 * which is exactly right for a stack. */
CADS_CCM_SECTION __attribute__((aligned(8)))
static uint32_t worker_stack[WORKER_STACK_WORDS];

static cads_thread_t worker_thread;
static cads_mutex_t resource_mutex;
static cads_timer_t heartbeat_timer;
static cads_event_t heartbeat_event;

static void heartbeat_fired(void* context) {
    (void)context;
    /* Runs on FreeRTOS's timer service task, not on worker_task. */
    cads_event_set(&heartbeat_event, HEARTBEAT_BIT);
}

static void worker_task(void* context) {
    (void)context;
    uint32_t wake = cads_kernel_ticks();
    for(;;) {
        uint32_t bits = cads_event_wait(&heartbeat_event, HEARTBEAT_BIT,
                                         true /* clear on exit */, true, 500u);
        if((bits & HEARTBEAT_BIT) != 0u && cads_mutex_lock(&resource_mutex, 100u)) {
            /* ... touch the shared resource ... */
            cads_mutex_unlock(&resource_mutex);
        }
        cads_kernel_sleep_until(&wake, 50u); /* 20 Hz ceiling */
    }
}

void app_start(void) {
    cads_kernel_init();
    cads_mutex_init(&resource_mutex);
    cads_event_init(&heartbeat_event);
    cads_timer_init(&heartbeat_timer, "heartbeat", 200u, true /* auto_reload */,
                     heartbeat_fired, NULL);
    cads_timer_start(&heartbeat_timer, 100u);

    if(!cads_thread_start(&worker_thread, "worker", worker_task, NULL,
                           worker_stack, WORKER_STACK_WORDS, CadsPriorityNormal)) {
        cads_hal_panic("worker thread failed to start");
    }

    cads_kernel_start(); /* does not return */
}
```

Link with `target_link_libraries(<your target> PUBLIC cads_kernel)`; the
library and its `include/` are only present when `CADS_TARGET` is
`itsboard`. `FreeRTOS.h`, `task.h`, `queue.h`, `semphr.h`, `timers.h`,
`event_groups.h` and `FreeRTOSConfig.h` are also on the public include path —
reachable deliberately by a caller that wants FreeRTOS's own API directly,
never included by accident.

## What are the limits?

- **Thread count is capped at 12** — `cads_thread_start()`'s internal task
  block pool (`cads_task_blocks[12]` in `kernel.c`) is fixed size; the 13th
  call returns `false` rather than growing the pool.
- **No stop, suspend, delete or priority-change wrappers**, even though
  `FreeRTOSConfig.h` enables `INCLUDE_vTaskDelete`, `INCLUDE_vTaskSuspend` and
  `INCLUDE_vTaskPrioritySet` for FreeRTOS itself. Only `cads_thread_start()`
  and `cads_thread_yield()` exist; a caller that needs the rest reaches
  FreeRTOS's own `task.h` directly, which the public include path allows on
  purpose.
- **`cads_mutex_t` is a plain mutex, not recursive.** `cads_mutex_init()`
  calls `xSemaphoreCreateMutexStatic()`, not the recursive constructor,
  despite `configUSE_RECURSIVE_MUTEXES` being on in `FreeRTOSConfig.h` for
  FreeRTOS's own use. Locking it twice from the same thread deadlocks.
  Priority inheritance is whatever FreeRTOS's plain mutex gives you; this
  wrapper adds nothing on top.
- **Only `cads_queue_send_from_isr()` has an ISR-safe form.** There is no
  `_from_isr` variant for `cads_mutex_t` or `cads_event_t`, even though
  FreeRTOS provides both; add one if a driver ever needs it.
- **No tickless idle.** `configUSE_TICKLESS_IDLE` is `0`; the tick fires at a
  constant 1 kHz regardless of load. This module does not manage power.
- **No runtime CPU-usage stats.** `configGENERATE_RUN_TIME_STATS` is `0`; the
  only per-task diagnostic is `cads_thread_stack_free()` (stack high-water
  mark) and the aggregate `cads_kernel_task_count()`. There is no heap to
  report on, by design — see `docs/reference/memory-map.md`.
- **Queues carry no framing or type safety.** `cads_queue_init()` takes a raw
  `item_size`; the caller's `buffer` must be at least `capacity * item_size`
  bytes and nothing here checks that at the call site.
- **Does not build for the host.** The top-level `CMakeLists.txt` links
  `cads_kernel` only when `CADS_TARGET STREQUAL "itsboard"`. The simulator
  uses `apps/bringup/tasks_sim.c` (no scheduler, single-threaded) instead, and
  `apps/bringup/explorer_kernel_test_sim.c` reports the `x` console command as
  unavailable there rather than faking a result.
