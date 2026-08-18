# Module layout

CaDS Zero is built as a set of independent libraries rather than one firmware
blob. Each module is a CMake target with a declared public surface, and the
build enforces the boundaries — a module that reaches into another's internals
does not compile.

## Why

Three reasons, in order of how much they cost when ignored:

1. **The simulator.** Everything above the HAL has to build for the host as
   well as for the board. That only stays true if the dependency on hardware is
   confined to one module with a declared interface, rather than diffused
   through `#include "stm32f4xx.h"` in whatever file needed a register.
2. **Parallel work.** Work packages are handed to independent agents. A module
   with a narrow public header can be implemented, reviewed and merged without
   reading the rest of the tree — which is also what keeps a package inside a
   200k-token context window.
3. **Reuse.** The canvas, the font renderer and the toolbox are not specific to
   this firmware. Anything that is generic should be usable by the next project
   without carrying this one along.

## Anatomy

```
modules/<name>/
  include/cads/<name>/*.h    public API, the only thing dependents may include
  src/*.c *.h                implementation, private headers live here
  tests/*.c                  host unit tests, run by ctest
  README.md                  what it is, what it depends on, how to use it
  CMakeLists.txt
```

The include path is `cads/<name>/…` rather than a bare filename, so a
`#include` says where a type comes from:

```c
#include "cads/canvas/canvas.h"     /* obvious */
#include "canvas.h"                 /* which canvas? */
```

## CMake contract

```cmake
add_library(cads_canvas STATIC src/canvas.c src/text.c)

# Public: dependents get this. Private: only this module's sources.
target_include_directories(cads_canvas
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(cads_canvas PUBLIC cads_hal_api cads_font PRIVATE cads_flags)
```

`PRIVATE` on the include directory is the part that does the work: it makes a
module's own headers unreachable from outside, so the public header is the only
way in and stays honest.

## The dependency rule

Dependencies point **downwards only**, and the graph is acyclic:

```
        apps/            desktop, menu, tools
          │
        gui/             views, widgets, compositor
          │
   canvas ─┴─ input ─ storage ─ net        feature modules
     │         │         │        │
     └─────────┴────┬────┴────────┘
                    │
                 hal_api            the interface, no implementation
                    │
        ┌───────────┴───────────┐
   targets/itsboard        targets/sim     the two implementations
```

A feature module never includes a target header. `hal_api` is headers only,
which is what lets the same object files link against either backend.

## Module inventory

| Module | Reusable beyond this project | Depends on |
|---|---|---|
| `toolbox` | yes, entirely generic | — |
| `hal_api` | interface only | — |
| `font` | yes, any 1 bpp glyph renderer | toolbox |
| `canvas` | yes, any indexed framebuffer | hal_api, font |
| `assets` | no, CaDS branding | canvas |
| `input` | partly, the debounce logic is generic | hal_api, toolbox |
| `gui` | yes | canvas, input |
| `storage` | partly, littlefs glue is generic | hal_api |
| `net` | no, board specific | hal_api |

## Documentation standard

Every module carries a `README.md` that answers four questions, in this order:

1. **What is it?** One paragraph.
2. **Why is it shaped this way?** The constraints that forced the design. This
   is the part that stops someone "simplifying" a decision that was load
   bearing — the canvas being 4 bpp looks arbitrary until you know a truecolour
   buffer is 300 KB and there are 192 KB.
3. **How do I use it?** A short, complete example that compiles.
4. **What are the limits?** What it deliberately does not do.

Public headers document *contracts* — ownership, validity, threading, units.
Implementation comments explain *why*, never *what*: the code already says what.
