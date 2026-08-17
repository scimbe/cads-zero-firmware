#!/usr/bin/env python3
"""Generate the CaDS Zero interrupt vector table from the CMSIS device header.

Deriving the table mechanically from ``IRQn_Type`` removes a whole class of
transcription bugs: a handler wired to the wrong slot is the kind of defect that
only shows up as a hard fault weeks later.

Usage:
    scripts/gen_vectors.py lib/cmsis_device_f4/Include/stm32f429xx.h \
        targets/itsboard/startup/vectors_stm32f429.c
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

HEADER = """\
/* GENERATED FILE - do not edit.
 * Regenerate with: scripts/gen_vectors.py {src} {dst}
 *
 * Cortex-M4 system exceptions plus the {n} device interrupts of the STM32F429,
 * derived from IRQn_Type in the CMSIS device header.
 */

#include <stdint.h>

extern uint32_t __cads_stack_top;

void Reset_Handler(void);

/*
 * GCC requires the target of an alias to be defined in the same translation
 * unit, so Default_Handler lives here rather than next to Reset_Handler.
 *
 * Halting rather than resetting is deliberate: an unexpected interrupt leaves
 * the machine intact for the attached ST-Link, and IPSR still names the vector
 * that fired.
 */
__attribute__((noreturn)) void Default_Handler(void) {{
    __asm volatile("bkpt #0" ::: "memory");
    for(;;) {{
    }}
}}

"""


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])

    text = src.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"typedef enum\s*\{(.*?)\}\s*IRQn_Type;", text, re.S)
    if not match:
        print(f"error: no IRQn_Type enum found in {src}", file=sys.stderr)
        return 1

    irqs: dict[int, str] = {}
    for line in match.group(1).splitlines():
        line = line.split("/*")[0].strip()
        entry = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(-?\d+)", line)
        if not entry:
            continue
        number = int(entry.group(2))
        if number < 0:
            continue  # system exceptions are spelled out explicitly below
        name = entry.group(1)
        irqs[number] = name.removesuffix("_IRQn")

    highest = max(irqs)
    device_handlers = [irqs.get(i) for i in range(highest + 1)]

    system = [
        "NMI_Handler",
        "HardFault_Handler",
        "MemManage_Handler",
        "BusFault_Handler",
        "UsageFault_Handler",
        None,
        None,
        None,
        None,
        "SVC_Handler",
        "DebugMon_Handler",
        None,
        "PendSV_Handler",
        "SysTick_Handler",
    ]

    out = [HEADER.format(src=src, dst=dst, n=len(irqs))]

    out.append("/* Every handler is a weak alias of Default_Handler; a driver that\n"
               " * defines the real symbol overrides it at link time. */\n")
    for name in system:
        if name:
            out.append(f'void {name}(void) __attribute__((weak, alias("Default_Handler")));\n')
    out.append("\n")
    for name in device_handlers:
        if name:
            out.append(
                f'void {name}_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));\n'
            )
    out.append("\n")

    out.append("typedef void (*cads_vector_t)(void);\n\n")
    out.append('__attribute__((section(".isr_vector"), used))\n')
    out.append("const cads_vector_t cads_vector_table[] = {\n")
    out.append("    (cads_vector_t)(&__cads_stack_top),\n")
    out.append("    Reset_Handler,\n")
    for index, name in enumerate(system):
        comment = f"  /* {index + 2:>3} */"
        out.append(f"    {name or '0'},{comment}\n")
    for index, name in enumerate(device_handlers):
        slot = index + 16
        handler = f"{name}_IRQHandler" if name else "0"
        label = name or "reserved"
        out.append(f"    {handler},  /* {slot:>3}  IRQ {index:>2}  {label} */\n")
    out.append("};\n")

    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text("".join(out), encoding="utf-8")
    print(f"{dst}: {len(irqs)} device interrupts, {len(device_handlers) + 16} vectors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
