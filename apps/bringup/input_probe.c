/*
 * CaDS Zero - input discovery probe.
 *
 * The ITS adapter brings out 8 inputs (PF0..PF7) and 6 interrupt-capable lines
 * (PG0..PG5), but nothing in the sources says what is physically wired to them
 * on this particular board - switches, buttons, a keypad matrix, or nothing at
 * all. Guessing a button map and discovering later that INT3 is a DIP switch
 * would poison every layer built on top of it.
 *
 * So this measures instead. It reports every edge on every line with a
 * timestamp, and the operator presses things one at a time. The output is
 * machine-readable so scripts/input_map.py can turn a press session directly
 * into a mapping.
 *
 * It also measures bounce duration, because the debounce interval in the input
 * service should come from this hardware rather than from a folk number.
 */

#include "input_probe.h"

#include <stdbool.h>
#include <stdint.h>

#include "cads_hal.h"

typedef struct {
    const char* name;
    uint8_t previous;
    uint32_t last_change_ms;
    uint32_t edges;
    uint32_t shortest_stable_ms;
} cads_line_group_t;

static void cads_probe_report(
    const char* group,
    uint8_t index,
    bool pressed,
    uint32_t timestamp,
    uint32_t since) {
    /* One line per edge, fixed field order:
     *   EDGE <group> <index> <down|up> <ms> <since_previous_ms>       */
    cads_probe_puts("EDGE ");
    cads_probe_puts(group);
    cads_probe_puts(" ");
    cads_probe_put_uint(index);
    cads_probe_puts(pressed ? " down " : " up   ");
    cads_probe_put_uint(timestamp);
    cads_probe_puts(" ");
    cads_probe_put_uint(since);
    cads_probe_puts("\r\n");
}

static void cads_probe_scan(
    cads_line_group_t* group,
    uint8_t current,
    uint8_t width,
    uint32_t now) {
    uint8_t changed = (uint8_t)(current ^ group->previous);
    if(!changed) return;

    for(uint8_t bit = 0; bit < width; bit++) {
        if(!(changed & (1u << bit))) continue;

        uint32_t since = now - group->last_change_ms;
        cads_probe_report(group->name, bit, (current & (1u << bit)) != 0u, now, since);

        group->edges++;
        /* The shortest interval between consecutive edges is a direct read on
         * how long this switch bounces for. */
        if(since > 0u && since < group->shortest_stable_ms) {
            group->shortest_stable_ms = since;
        }
    }

    group->previous = current;
    group->last_change_ms = now;
}

void cads_input_probe_run(uint32_t duration_ms) {
    cads_probe_puts("\r\n# INPUT PROBE\r\n");
    cads_probe_puts("# Press each button once, in this order, pausing between:\r\n");
    cads_probe_puts("#   up, down, left, right, ok/enter, back/escape\r\n");
    cads_probe_puts("# Then flip any switches. Format:\r\n");
    cads_probe_puts("#   EDGE <group> <index> <down|up> <ms> <since_prev_ms>\r\n");
    cads_probe_puts("# groups: IN = PF0..PF7, INT = PG0..PG5, USR = Nucleo button\r\n");

    cads_line_group_t in_group = {"IN", 0u, 0u, 0u, 0xFFFFFFFFu};
    cads_line_group_t int_group = {"INT", 0u, 0u, 0u, 0xFFFFFFFFu};
    cads_line_group_t user_group = {"USR", 0u, 0u, 0u, 0xFFFFFFFFu};

    /* Seed from the resting state so the initial level is not reported as an
     * edge - otherwise a switch left closed looks like a press at t=0. */
    in_group.previous = cads_hal_adapter_inputs();
    int_group.previous = cads_hal_adapter_interrupts();
    user_group.previous = cads_hal_user_button() ? 1u : 0u;

    cads_probe_puts("# resting state: IN=");
    cads_probe_put_uint(in_group.previous);
    cads_probe_puts(" INT=");
    cads_probe_put_uint(int_group.previous);
    cads_probe_puts(" USR=");
    cads_probe_put_uint(user_group.previous);
    cads_probe_puts("\r\n");

    uint32_t start = cads_hal_ticks_ms();
    uint32_t next_beat = start;

    while(cads_hal_ticks_ms() - start < duration_ms) {
        uint32_t now = cads_hal_ticks_ms();

        /* Polled at ~1 kHz. Fast enough to see bounce, which is the point:
         * an interrupt-driven reader with a debounce filter would hide exactly
         * the measurement being taken here. */
        cads_probe_scan(&in_group, cads_hal_adapter_inputs(), 8u, now);
        cads_probe_scan(&int_group, cads_hal_adapter_interrupts(), 6u, now);
        cads_probe_scan(&user_group, cads_hal_user_button() ? 1u : 0u, 1u, now);

        if(now >= next_beat) {
            next_beat = now + 5000u;
            cads_hal_led_toggle(CadsLedBlue);
        }

        cads_hal_delay_us(1000u);
    }

    cads_probe_puts("# PROBE SUMMARY\r\n");
    const cads_line_group_t* groups[] = {&in_group, &int_group, &user_group};
    for(uint32_t i = 0; i < 3u; i++) {
        cads_probe_puts("# ");
        cads_probe_puts(groups[i]->name);
        cads_probe_puts(" edges=");
        cads_probe_put_uint(groups[i]->edges);
        cads_probe_puts(" min_gap_ms=");
        cads_probe_put_uint(
            groups[i]->shortest_stable_ms == 0xFFFFFFFFu ? 0u : groups[i]->shortest_stable_ms);
        cads_probe_puts("\r\n");
    }
    cads_probe_puts("# PROBE END\r\n");
}
