# Debug with GDB

```bash
st-util --serial 066FFF565282494867161033 -p 4242 &
arm-none-eabi-gdb -q build/itsboard/cads-zero.elf \
    -ex "target extended-remote :4242"
```

Always clean up afterwards — a stale server holds the probe:

```bash
pkill -f st-util
```

## Where am I?

```
(gdb) monitor reset halt
(gdb) break cads_bringup_run
(gdb) continue
(gdb) bt
```

Note that attaching resets the target, so a backtrace taken immediately after
connecting shows the board a few milliseconds into boot, not where it was when
you decided to look. This is easy to misread as a hang.

## Useful register reads

```
# Fault status - all zero means no fault has occurred
printf "HFSR=%08x CFSR=%08x MMAR=%08x\n", *(unsigned int*)0xE000ED2C, \
    *(unsigned int*)0xE000ED28, *(unsigned int*)0xE000ED34

# Clock tree: SWS must read 0b10 (PLL) in bits 3:2
printf "RCC_CFGR=%08x RCC_CR=%08x FLASH_ACR=%08x\n", \
    *(unsigned int*)0x40023808, *(unsigned int*)0x40023800, *(unsigned int*)0x40023C00

# USART3
printf "SR=%08x BRR=%08x CR1=%08x\n", *(unsigned int*)0x40004800, \
    *(unsigned int*)0x40004808, *(unsigned int*)0x4000480C
```

Expected on a healthy board at 180 MHz: `RCC_CFGR` bits 3:2 = `10`,
`FLASH_ACR` = `0x705` (5 wait states, caches and prefetch on), `SystemCoreClock`
= 180000000.

## Breakpoints and the panic path

`Default_Handler` and `cads_hal_panic()` execute `bkpt #0`. With a debugger
attached this halts usefully and `IPSR` names the vector that fired. **Without**
a debugger a `bkpt` escalates to a HardFault, so a panic on an untethered board
presents as a lock-up with the red LED lit — which is the intended failure mode,
not a bug.

## Time is frozen while halted

`DWT->CYCCNT` does not advance when the core is halted, so reading it twice from
GDB always gives the same value. That is not evidence the counter is broken.
