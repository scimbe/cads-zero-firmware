/*
 * CaDS Zero - the firmware's task set.
 *
 * Three tasks, chosen so that each of the three things that can starve the
 * others is represented:
 *
 *   ui        owns the display. A flush blocks for up to 448 ms, which is by
 *             far the longest single operation in the system, so it runs at
 *             normal priority and everything else must tolerate it.
 *   input     polls the buttons and the touch panel at 100 Hz. Higher priority
 *             than the UI, because an input sampled late feels like a bug even
 *             when nothing is lost.
 *   console   the hardware explorer and, later, the CLI. Lowest priority: it
 *             is a diagnostic channel and must never delay anything real.
 *
 * Stacks are static and sized from measurement - each task reports its unused
 * high-water mark, and the numbers are printed periodically so the sizes stay
 * honest as the code grows.
 */

#include "tasks.h"

#include "cads/kernel/kernel.h"
#include "cads_hal.h"
#include "canvas.h"
#include "explorer.h"
#include "input/cads_input.h"
#include "input_probe.h"

/* Stack sizes in words. The UI task carries the canvas call chain, which is
 * the deepest; the others are shallow.
 *
 * CADS_CONSOLE_STACK doubled 512->1024 (2026-08-28): a real, hardware-
 * confirmed stack overflow. This task's own app-tree loop
 * (explorer_app_demo.c) calls cads_net_poll() every tick, and with
 * net.dhcp=1 that runs lwIP's DHCP client state machine (dhcp_recv() /
 * dhcp_handle_state_machine() et al) - visibly deeper than the plain
 * static-IP path - on the SAME stack that same loop also uses for the
 * full app-tree tick chain (marauder_tick's PCAP/join call depth,
 * settings_service_config, gui_tick, ...). Caught live on the real board:
 * vApplicationIdleHook() (this file's own stack-guard sentinel check,
 * immediately below) faulted with a garbage PC
 * (0xF7FF0FF0 - an instruction-fetch violation, CFSR IACCVIOL) reached via
 * a corrupted return address - the textbook signature of a stack
 * overflow severe enough to corrupt the very code trying to detect it.
 * Reproduced with net.dhcp=1 and a real DHCP server present; net.dhcp=0
 * (the static-IP path) never hit it. Cheap to fix generously: unlike the
 * SRAM heap this scripts/check_ram_budget.py's 256 B floor actually
 * guards, task stacks live in CCM (CADS_CCM_SECTION below), which had
 * ~59 KB free out of 64 KB before this change - doubling costs 2 KB of
 * that, not a single byte of the tight SRAM margin. */
#define CADS_UI_STACK      512
#define CADS_INPUT_STACK   256
#define CADS_CONSOLE_STACK 1024

CADS_CCM_SECTION __attribute__((aligned(8))) static uint32_t cads_ui_stack[CADS_UI_STACK];
CADS_CCM_SECTION __attribute__((aligned(8))) static uint32_t cads_input_stack[CADS_INPUT_STACK];
CADS_CCM_SECTION __attribute__((aligned(8))) static uint32_t cads_console_stack[CADS_CONSOLE_STACK];

static cads_thread_t cads_ui_thread;
static cads_thread_t cads_input_thread;
static cads_thread_t cads_console_thread;

/* --- stack-guard sentinels ------------------------------------------------
 *
 * configCHECK_FOR_STACK_OVERFLOW (method 2, modules/kernel/src/FreeRTOSConfig.h)
 * watches the task stacks but is blind to the MSP - the shared ISR/handler
 * stack, 4K at the top of CCM - and only samples at a context switch. This
 * puts one sentinel word at the overflow end (lowest address) of each stack
 * that matters and rechecks them from the idle hook, so an MSP overflow, or a
 * task stack driven to its absolute limit, surfaces as a named panic with a
 * forensic record instead of silent corruption that faults elsewhere later.
 *
 * The canary value is 0xA5A5A5A5 on purpose: it is exactly FreeRTOS's own
 * tskSTACK_FILL_BYTE pattern, so the task stacks already carry it from
 * xTaskCreateStatic and this code never writes into them - a different value
 * would fight method 2's own check of those same bytes. Only the MSP word,
 * which FreeRTOS does not own, is painted (cads_stackguard_arm).
 *
 * The table is static const, so it lives in flash and costs nothing from the
 * RAM budget (scripts/check_ram_budget.py). It lives here, with the stacks it
 * guards, rather than in modules/kernel with the other vApplication* hooks:
 * cads_kernel must not depend on cads_apps (the reverse already holds), and a
 * const-in-flash table needs the task-stack symbols, file-static to this unit. */
#define CADS_STACKGUARD_CANARY 0xA5A5A5A5u

extern uint32_t __cads_stack_bottom; /* linker: lowest word of the 4K MSP region in CCM */

typedef struct {
    const char* name;
    volatile const uint32_t* sentinel; /* lowest word of the stack; last hit on overflow */
} cads_stackguard_t;

static const cads_stackguard_t cads_stackguards[] = {
    {"msp", &__cads_stack_bottom},
    {"ui", cads_ui_stack},
    {"input", cads_input_stack},
    {"console", cads_console_stack},
};

static void cads_stackguard_arm(void) {
    /* Task stacks are already 0xA5-filled by xTaskCreateStatic; only the MSP
     * sentinel needs painting, and only from here - before the scheduler
     * starts, with the MSP shallow, so its lowest word is safe to write. */
    __cads_stack_bottom = CADS_STACKGUARD_CANARY;
}

static const char* cads_stackguard_breached(void) {
    for(uint32_t i = 0u; i < (uint32_t)(sizeof(cads_stackguards) / sizeof(cads_stackguards[0])); i++) {
        if(*cads_stackguards[i].sentinel != CADS_STACKGUARD_CANARY) {
            return cads_stackguards[i].name;
        }
    }
    return NULL;
}

void vApplicationIdleHook(void) {
    const char* breached = cads_stackguard_breached();
    if(breached != NULL) {
        cads_hal_panic(breached); /* names the overflowed stack; does not return */
    }
}

/*
 * THE DISPLAY HAS EXACTLY ONE FLUSHER.
 *
 * Any task may draw into the canvas - drawing is cheap and the buffer is
 * private to the CPU - but only the ui task calls cads_canvas_flush(). A flush
 * holds the SPI bus for up to 448 ms, reconfigures the Ethernet MAC around it,
 * and drives a DMA transfer; two of them overlapping would interleave pixel
 * data into the panel and corrupt the frame.
 *
 * The mutex below exists for the second flusher that does not exist yet. The
 * rule is the real protection: everything else marks the canvas dirty and
 * waits, via cads_tasks_redraw_sync().
 */
static cads_mutex_t cads_display_mutex;

static volatile uint32_t cads_input_events;
static volatile uint32_t cads_last_key;

static void cads_on_input(const cads_input_event_t* event, void* context) {
    (void)context;
    cads_input_events++;
    if(event->type == CadsInputPress) {
        cads_last_key = (uint32_t)event->key + 1u;
    }
}

static void cads_input_task(void* context) {
    (void)context;
    cads_input_init();
    cads_input_set_callback(cads_on_input, NULL);

    uint32_t wake = cads_kernel_ticks();
    for(;;) {
        cads_input_tick();
        cads_kernel_sleep_until(&wake, 10u); /* 100 Hz */
    }
}

static void cads_ui_task(void* context) {
    (void)context;
    uint32_t wake = cads_kernel_ticks();

    for(;;) {
        if(cads_canvas_is_dirty()) {
            if(cads_mutex_lock(&cads_display_mutex, 1000u)) {
                cads_canvas_flush();
                cads_mutex_unlock(&cads_display_mutex);
            }
        }
        cads_kernel_sleep_until(&wake, 50u); /* 20 Hz ceiling */
    }
}

static void cads_console_task(void* context) {
    (void)context;
    cads_explorer_run(); /* never returns */
}

void cads_tasks_start(void) {
    cads_kernel_init();
    cads_mutex_init(&cads_display_mutex);

    cads_thread_start(
        &cads_input_thread, "input", cads_input_task, NULL, cads_input_stack,
        CADS_INPUT_STACK, CadsPriorityHigh);

    cads_thread_start(
        &cads_ui_thread, "ui", cads_ui_task, NULL, cads_ui_stack, CADS_UI_STACK,
        CadsPriorityNormal);

    cads_thread_start(
        &cads_console_thread, "console", cads_console_task, NULL, cads_console_stack,
        CADS_CONSOLE_STACK, CadsPriorityLow);

    /* Paint the MSP sentinel now that the task stacks exist (FreeRTOS filled
     * theirs at creation above) and before the scheduler starts feeding the
     * idle hook that rechecks them. */
    cads_stackguard_arm();

    cads_kernel_start(); /* does not return */
}

void cads_tasks_sleep_ms(uint32_t ms) {
    cads_kernel_sleep_ms(ms);
}

bool cads_tasks_redraw_sync(uint32_t timeout_ms) {
    /* The caller has already drawn; the ui task owns the transfer. Wait for it
     * to pick the work up rather than flushing here, which is what keeps the
     * single-flusher rule true instead of merely intended. */
    uint32_t deadline = cads_kernel_ticks() + timeout_ms;
    while(cads_canvas_is_dirty()) {
        if((int32_t)(cads_kernel_ticks() - deadline) >= 0) return false;
        cads_kernel_sleep_ms(5u);
    }
    return true;
}

void cads_tasks_report(void) {
    cads_probe_puts("# tasks  ui_free=");
    cads_probe_put_uint(cads_thread_stack_free(&cads_ui_thread));
    cads_probe_puts(" input_free=");
    cads_probe_put_uint(cads_thread_stack_free(&cads_input_thread));
    cads_probe_puts(" console_free=");
    cads_probe_put_uint(cads_thread_stack_free(&cads_console_thread));
    cads_probe_puts(" tasks=");
    cads_probe_put_uint(cads_kernel_task_count());
    cads_probe_puts(" events=");
    cads_probe_put_uint(cads_input_events);
    cads_probe_puts(" last_key=");
    cads_probe_put_uint(cads_last_key);
    cads_probe_puts("\r\n");
}
