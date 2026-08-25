# The toolchain, and why it comes from vcpkg

The mechanical version of this — where the compiler is, how to override it, what
the size report means — is [Build the firmware](../how-to/build.md). This page
is the part that is not in the build script: *why* the toolchain is what it is,
what is actually pinned and what only looks pinned, and the one honest gap in
the reasoning.

## What the build uses

One compiler, one build system, one target trunk:

- **`arm-none-eabi-gcc`** — the Arm GNU bare-metal toolchain (GCC, `binutils`,
  `newlib`), targeting `arm-none-eabi`. It is the only toolchain the ITSboard
  target builds with. There is no Arm Compiler 6 / `armclang` project, no IAR
  project, no CMSIS-Toolbox `csolution` in this repository — `cmake/arm-none-eabi-gcc.cmake`
  is the single toolchain file, and every firmware `CMakeLists.txt` is written
  against GCC and its linker.
- **CMake (≥ 3.20) driving Ninja** — `scripts/build.sh` configures with
  `-G Ninja` and points `CMAKE_TOOLCHAIN_FILE` at that one toolchain file.
- **The `st-link` host tools** (`st-flash`, `st-util`) for writing and debugging
  the board — see [Flash the board](../how-to/flash.md) and [Debug with GDB](../how-to/debug.md).
  These are a separate concern from the compiler and are *not* provisioned the
  way it is; more on that below.

The architecture flags are fixed in the toolchain file, not scattered across
targets (`cmake/arm-none-eabi-gcc.cmake`):

```
-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
--specs=nano.specs --specs=nosys.specs
```

That is the STM32F429ZI's core stated exactly: a Cortex-M4 with the
single-precision `FPv4-SP-D16` FPU, built for the hard-float ABI so floating
point goes through the FPU registers rather than a soft-float library. `nano.specs`
selects the size-optimised `newlib-nano` C library; `nosys.specs` supplies stub
syscalls for a system with no OS underneath libc. The language is C11, and it is
required rather than merely requested (`CMAKE_C_STANDARD 11` /
`CMAKE_C_STANDARD_REQUIRED ON` in the root `CMakeLists.txt`). The no-heap policy
that the rest of the firmware depends on is enforced by the linker script, not by
these flags — that is its own decision, documented where it bites.

## Where the compiler actually comes from

The build does not assume `arm-none-eabi-gcc` is on your `PATH`. It resolves it
out of a specific directory tree:

```
~/.vcpkg/artifacts/*/compilers.arm.arm.none.eabi.gcc/*/bin
```

That path is not arbitrary, and this is the crux of the page. It is the
**vcpkg artifact tree that the Keil Studio VS Code extension manages**
(`scripts/cads_env.sh`, `cmake/arm-none-eabi-gcc.cmake`). Keil Studio — Arm's
VS Code-based embedded environment — provisions its Arm GNU toolchain, its CMake,
and its Ninja through vcpkg's *artifact* mechanism (tool acquisition, distinct
from vcpkg's better-known C/C++ library ports), and drops them under
`~/.vcpkg/artifacts/…`. `scripts/cads_env.sh` reaches into that same tree for all
three:

```
compilers.arm.arm.none.eabi.gcc   → the compiler
tools.kitware.cmake               → CMake
tools.ninja.build.ninja           → Ninja
```

So the honest one-sentence answer to "why vcpkg?" is: **because that is where the
toolchain already is.** The board is developed inside Keil Studio; Keil Studio
installs the Arm toolchain via vcpkg; the build scripts consume what is already
provisioned rather than asking the developer to install a second copy by another
route. It is a single-source-of-truth convenience, not a package-management
philosophy.

### What is *not* recorded

The build reason above is real and discoverable in the repository. What is **not**
anywhere in the repository is a deliberation that weighed vcpkg against the
alternatives — a system package (`brew install arm-none-eabi-gcc`, a distro
package), Arm's own standalone installer, or a container image with the toolchain
baked in. No commit message, no design note, and no roadmap entry compares them.
The roadmap task that asked for this very page frames the toolchain choice as
being "over alternatives" without saying which alternatives were rejected or on
what grounds (`docs/ROADMAP.md`, milestone M8).

This page will not invent that comparison. Absent a stated reason, the vcpkg
provenance is best read as *the default that fell out of the development
environment* rather than a decision taken against a field of contenders. If a
reason beyond "it is what Keil Studio installs" ever existed, it was not written
down, and a future reader should treat the choice as open to revisiting rather
than as load-bearing.

### It is a default, not a requirement

The vcpkg tree is where the build *looks first*, but nothing forces it. Two
overrides exist, both grounded in `cmake/arm-none-eabi-gcc.cmake` and
`scripts/build.sh`:

- Set **`CADS_ARM_TOOLCHAIN_BIN`** to the `bin` directory of any
  `arm-none-eabi-gcc` you like. This wins over the vcpkg glob unconditionally.
- Put an `arm-none-eabi-gcc` on `PATH` and supply neither the variable nor a
  vcpkg tree; the toolchain file falls back to the bare `arm-none-eabi-` prefix
  and lets `PATH` resolve it.

This is precisely the escape hatch a contributor who does not use Keil Studio
needs. A system package or Arm's installer works identically once its `bin`
directory is named — the build does not care *how* the compiler was installed,
only that it can find one. Which is also why the absence of a recorded
alternatives comparison costs little in practice: the decision is cheap to
reverse per-developer.

## What "13.3.1" is, and what it is not

The current toolchain is `arm-none-eabi-gcc` **13.3.1**. That number is worth
stating carefully, because it is easy to mistake for a pin, and it is not one.

Nothing in the repository pins a toolchain version. The resolver takes the
*newest* toolchain it finds:

- the CMake file globs every `…/compilers.arm.arm.none.eabi.gcc/*/bin`, sorts the
  matches, and takes the last (`cmake/arm-none-eabi-gcc.cmake`);
- `scripts/cads_env.sh` does the same with `sort -V | tail -1`.

There is no version string in any `.cmake`, `.sh`, or manifest file in the tree
(the only `vcpkg-configuration.json` files present belong to the vendored
`lib/CMSIS_6` submodule and have nothing to do with selecting *this* build's
compiler). So **13.3.1 is the version installed today, observed, not the version
required.** Install a newer Arm GNU toolchain through Keil Studio and the next
configure will pick it up with no change to the repository; that is the resolver
working as designed, not drift.

This is a deliberate trade — zero maintenance to track Keil Studio's updates, in
exchange for a build whose exact compiler is defined by the developer's machine
rather than the repository. For a single-maintainer board that is a reasonable
place to land. It also means the version this documentation names will go stale;
trust `arm-none-eabi-gcc --version` on the machine in front of you over this
sentence. CI installs its own toolchain and is the closest thing to a canonical
version — see the `firmware` job in `.github/workflows/ci.yml`.

## Reproducing a working build from nothing

A from-scratch environment needs four things beyond a checkout. The mechanics are
in [Build the firmware](../how-to/build.md); the point here is *why each is on
the list*.

1. **The submodules.** The vendored dependencies — CMSIS, the STM32F4 CMSIS
   device headers, the FreeRTOS kernel, littlefs, Unity, and lwIP — live as git
   submodules (`.gitmodules`). Clone with `--recurse-submodules`; a plain clone
   configures and then fails to compile against headers that are not there.
2. **An `arm-none-eabi-gcc`.** From Keil Studio's vcpkg tree if you use it
   (nothing further to do), or from anywhere else with `CADS_ARM_TOOLCHAIN_BIN`
   or `PATH` pointed at it. Cortex-M4F hard-float support is the only real
   requirement, which any current Arm GNU toolchain has.
3. **CMake ≥ 3.20 and Ninja.** Also supplied by the vcpkg tree; a system CMake
   and Ninja are fine if you are not using Keil Studio.
4. **A Python 3 interpreter.** The build itself calls it —
   `find_package(Python3 … REQUIRED)` in the root `CMakeLists.txt` — for the
   code-generation steps (the vector table among them), and the entire
   hardware-facing tool suite under `scripts/` is dependency-free Python 3 by
   design.

The `st-link` tools are the one thing on the critical path that the vcpkg tree
does *not* provide. `scripts/flash.sh` and the debug flow call `st-flash` and
`st-util` as bare commands, so they must be independently installed and on
`PATH`. They are needed only to move the image onto the board and to debug it —
not to build it — which is why they sit outside the compiler-provisioning story
entirely.

A build that has compiled but never met the board does not count here; the
reasoning for that, and the on-target gate that enforces it, is
[Run the hardware gate](../how-to/board-test.md).
