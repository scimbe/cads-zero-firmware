# Build the firmware

```bash
scripts/build.sh              # Debug, the default
scripts/build.sh Release
```

Artifacts land in `build/itsboard/`: `cads-zero.elf`, `.bin`, `.hex`, and
`cads-zero.map`.

## Toolchain

The script resolves `arm-none-eabi-gcc` from the vcpkg artifact tree that the
Keil Studio VS Code extension manages, picking the newest version it finds:

```
~/.vcpkg/artifacts/*/compilers.arm.arm.none.eabi.gcc/*/bin
```

Override with `CADS_ARM_TOOLCHAIN_BIN`, or put a toolchain on `PATH` and the
CMake toolchain file will fall back to it.

## Build options

```bash
# After swapping SB121/SB122 on the Nucleo - see docs/explanation/pa7-conflict.md
scripts/build.sh Debug -DCADS_SPI_MOSI_ON_PB5=1
```

## Reading the size report

Every link prints one:

```
Memory region         Used Size  Region Size  %age Used
       FLASH_APP:       13412 B         1 MB      1.28%
        FLASH_FS:           0 B       896 KB      0.00%
             RAM:      107712 B       192 KB     54.79%
             CCM:           0 B        64 KB      0.00%
```

`FLASH_FS` must stay at 0 — anything landing there would collide with the
littlefs volume. The linker also asserts that at least 48 KB of heap survives,
because lwIP and the GUI do not fit below that.

## Host build

Without the toolchain file, CMake configures for the host: the simulator and the
unit tests.

```bash
cmake -S . -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host
```
