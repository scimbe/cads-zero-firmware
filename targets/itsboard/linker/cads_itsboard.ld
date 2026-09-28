/*
 * CaDS Zero - linker script for ITSboard (NUCLEO-F429ZI + ITS adapter + Waveshare 4" shield)
 *
 * STM32F429ZIT6 memory map as used by this firmware:
 *
 *   FLASH_APP   0x08000000  1024K   Flash bank 1 (sectors 0..11)   - firmware
 *   FLASH_FS    0x08120000   896K   Flash bank 2 (sectors 17..23)  - littlefs volume "/int"
 *               0x08100000   128K   Flash bank 2 (sectors 12..16)  - RESERVED, left erased
 *   RAM         0x20000000   192K   SRAM1+SRAM2+SRAM3, contiguous, DMA capable
 *   CCM         0x10000000    64K   Core coupled memory, NO DMA ACCESS
 *
 * Safety notes (see docs/SAFETY.md):
 *  - The firmware never erases or programs anything below 0x08120000.
 *  - Because the device is dual bank, erasing bank 2 while executing from
 *    bank 1 does not stall the CPU. Flash routines are still placed in RAM.
 *  - Anything handed to a DMA controller must live in RAM, never in CCM.
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH_APP (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    FLASH_FS  (r)   : ORIGIN = 0x08120000, LENGTH = 896K
    RAM       (rwx) : ORIGIN = 0x20000000, LENGTH = 192K
    CCM       (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
}

/* Exported so the storage driver can assert it stays inside its window. */
__cads_fs_start = ORIGIN(FLASH_FS);
__cads_fs_size  = LENGTH(FLASH_FS);

/* Main stack (MSP) lives at the top of CCM: it is never a DMA target and
 * keeping it out of RAM leaves the DMA-capable region for buffers. */
__cads_main_stack_size = 4K;

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(512);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH_APP

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
    } > FLASH_APP

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH_APP

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH_APP
    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH_APP

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH_APP
    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH_APP
    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH_APP

    /* --- initialised data, copied to RAM by the reset handler --- */
    __cads_data_load = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        __cads_data_start = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        __cads_data_end = .;
    } > RAM AT > FLASH_APP

    /* --- code that must not execute from flash (flash erase/program) --- */
    __cads_ramfunc_load = LOADADDR(.ramfunc);
    .ramfunc :
    {
        . = ALIGN(8);
        __cads_ramfunc_start = .;
        *(.ramfunc)
        *(.ramfunc*)
        . = ALIGN(8);
        __cads_ramfunc_end = .;
    } > RAM AT > FLASH_APP

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __cads_bss_start = .;
        __bss_start__ = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __cads_bss_end = .;
        __bss_end__ = .;
    } > RAM

    /* Framebuffer and DMA buffers: explicit section so their placement in
     * DMA-capable SRAM is guaranteed and visible in the map file. */
    .dmaram (NOLOAD) :
    {
        . = ALIGN(32);
        __cads_dmaram_start = .;
        *(.dmaram)
        *(.dmaram*)
        . = ALIGN(4);
        __cads_dmaram_end = .;
    } > RAM

    /* The heap is whatever is left of RAM. Deliberately NOT expressed as a
     * section that fills the region: doing that makes --print-memory-usage
     * report 100% forever and destroys its value as a regression signal. */
    .heap (NOLOAD) :
    {
        . = ALIGN(8);
        __cads_heap_start = .;
    } > RAM

    __cads_heap_end = ORIGIN(RAM) + LENGTH(RAM);
    __cads_heap_size = __cads_heap_end - __cads_heap_start;

    /* The floor is 48K unless the build defines __cads_heap_floor
     * (-Wl,--defsym, top-level CMakeLists.txt). Only praktikum/start's lab
     * build does, with 0: nothing allocates from this region at run time
     * (no malloc/_sbrk is linked, lwIP's memory is static pools, the GUI's
     * framebuffer is static .bss) and the main stack is in CCM, so the lab
     * may spend it on static lwIP pools. scripts/check_ram_budget.py reads
     * the same symbol, so its margin gate stays the real guard. */
    ASSERT(__cads_heap_size >= (DEFINED(__cads_heap_floor) ? __cads_heap_floor : 48K),
           "SRAM left below the heap floor - lwIP and the GUI will not fit")

    /* CCM: FreeRTOS task stacks and kernel objects. NEVER DMA. */
    .ccm (NOLOAD) :
    {
        . = ALIGN(8);
        __cads_ccm_start = .;
        *(.ccm)
        *(.ccm*)
        . = ALIGN(8);
        __cads_ccm_end = .;
    } > CCM

    __cads_stack_top    = ORIGIN(CCM) + LENGTH(CCM);
    __cads_stack_bottom = __cads_stack_top - __cads_main_stack_size;

    ASSERT(__cads_ccm_end <= __cads_stack_bottom,
           "CCM overflow: .ccm section collides with the main stack")

    /DISCARD/ : { *(.ARM.attributes) }
}
