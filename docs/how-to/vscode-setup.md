# Set up VS Code

Everything in this page is free — no paid extension, no account, no trial.
The repo ships working `.vscode/` config (`extensions.json`, `settings.json`,
`tasks.json`, `launch.json`) and a `CMakePresets.json` at the root, so most
of this is "open the folder and say yes" rather than manual setup.

## 1. Get a toolchain on `PATH`

`scripts/build.sh` (and the CMake presets below) look for `arm-none-eabi-gcc`
via `CADS_ARM_TOOLCHAIN_BIN`, then fall back to plain `PATH` — see
[Build the firmware](build.md#toolchain). For VS Code specifically, `PATH` is
the simpler option: the debugger extension resolves `arm-none-eabi-gdb` the
same way, with no environment variable to keep in sync between your shell and
however VS Code itself gets launched.

```bash
# macOS
brew install --cask gcc-arm-embedded

# Debian/Ubuntu
sudo apt install gcc-arm-none-eabi

# then, either shell:
arm-none-eabi-gcc --version   # confirm it's on PATH
```

You'll also want `cmake` (≥ 3.21 for the presets file, though the project's
own floor is 3.20), `ninja`, and `st-link` (for `st-flash`/`st-util`) —
`brew install cmake ninja stlink` / `apt install cmake ninja-build stlink-tools`.

If you'd rather use the exact toolchain version this project's own reference
machine has (installed via the vcpkg-artifacts mechanism the Arm Keil Studio
Pack extension manages), that still works — it's what `CADS_ARM_TOOLCHAIN_BIN`
is for — but it's the extra-step path, not the one this page assumes.

## 2. Install the extensions

Open the folder in VS Code. It will offer to install the four recommended
extensions from `.vscode/extensions.json` — accept, or install by hand:

| Extension | ID | What it's for |
|---|---|---|
| CMake Tools | `ms-vscode.cmake-tools` | Reads `CMakePresets.json`, gives you a status-bar picker for the `itsboard`/`host` presets, drives configure/build/test without typing CLI commands. |
| C/C++ | `ms-vscode.cpptools` | IntelliSense, go-to-definition, and inline diagnostics — fed the *real* compiler flags from whichever preset is active via `compile_commands.json` (already enabled project-wide, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`), not a guess at your setup. |
| Cortex-Debug | `marus25.cortex-debug` | Breakpoints, variable inspection, and register views on the real board, driving `st-util` for you instead of typing the raw GDB session from [Debug with GDB](debug.md) by hand every time. |
| Python | `ms-python.python` | Editing/running `scripts/*.py` (`board_cmd.py`, `cads_config.py`, `board_test.py`, ...) — all dependency-free stdlib Python, so this is the only Python tooling you need. |

Nothing ESP32/Arduino-specific is on this list on purpose — see §5.

## 3. Build both targets

The status bar (bottom of the window, once CMake Tools is active) shows the
current preset. Pick **itsboard** or **host**, then either use CMake Tools'
own build button or **Terminal → Run Task**:

- **Build: ITSboard firmware** — the real board image, via `scripts/build.sh`.
- **Build: Host (sim + unit tests)** — the SDL2 simulator and the full test
  suite, native compiler, no ARM toolchain needed for this one.
- **Test: Host unit tests** — `ctest --preset host`, all 30+ unit and
  golden-image tests.

These call the exact same scripts and CMake presets the command line uses
(`scripts/build.sh`, `cmake --preset host`) — nothing VS-Code-specific about
what actually runs, only how you trigger it.

## 4. Flash and debug on real hardware

**Flash: ITSboard over ST-Link** (Run Task) calls `scripts/flash.sh` — see
[Flash the board](flash.md) for what it does and the safety constraints it
respects.

For interactive debugging, use the **Run and Debug** panel:

- **Debug on ITSboard (st-util)** — builds, flashes, and stops at `main()`
  with breakpoints available. This is [Debug with GDB](debug.md)'s
  `st-util` + `arm-none-eabi-gdb` session, wired into VS Code's UI instead of
  two terminal windows.
- **Attach to ITSboard (already running)** — no reset, no reflash: attaches
  to the board exactly as it is. This is the technique
  [Debug with GDB](debug.md#when-the-firmware-faults-read-the-console-first)
  describes for reading a halted fault handler's evidence — resetting first
  would destroy exactly what you're trying to inspect.

Both assume one ST-Link attached. With two on the same Mac, add
`"serialNumber": "<id>"` to the relevant config in `.vscode/launch.json`
(find the id with `st-info --probe`).

## 5. The ESP32/Marauder side

No dedicated Arduino/ESP-IDF extension is on the recommended list — the
WiFi co-processor's firmware is built entirely by
`tools/marauder-build/build_and_flash.sh` (wired up as the **Build:
ESP32Marauder co-processor** task), which drives `arduino-cli` directly. See
[wifi-coprocessor.md](../reference/wifi-coprocessor.md) for the wiring and
[marauder-coprocessor.md](../reference/marauder-coprocessor.md) for the build
itself. If you want editor support while reading or patching the vendored
Marauder C++ source, `ms-vscode.cpptools` (already installed for the STM32
side) handles plain C++ syntax/navigation fine without a second, heavier
Arduino-specific extension — this codebase's own C/C++ files and Marauder's
are different languages of the same family, not different tooling.

## How this differs from ITS-BRD-VSC

[Transport-Protocol/ITS-BRD-VSC](https://github.com/Transport-Protocol/ITS-BRD-VSC)
is HAW Hamburg's other ITSboard example (an lwIP teaching project) — worth
naming directly since the two setups make different, deliberate trade-offs:

| | ITS-BRD-VSC | CaDS Zero |
|---|---|---|
| Project format | CMSIS-Solution (`.cproject.yml`/`.csolution.yml`) | Plain CMake (`CMakeLists.txt` + `CMakePresets.json`) |
| Build UI | Keil Studio Pack's CMSIS panel | Any CMake-aware tool — VS Code's CMake Tools here, but equally `cmake --preset` from a terminal, CLion, or CI with no VS Code involved at all |
| Toolchain | Arm Keil Studio Pack (MDK 6, Community licence) | Homebrew/apt `arm-none-eabi-gcc`, or the same vcpkg-artifacts mechanism as an alternative |
| Extensions needed | Keil Studio Pack (bundles ~7 Arm-specific extensions), clangd, Memory/Peripheral Inspector | 4 mainstream, single-purpose extensions |
| Build without hardware | Not covered — the project targets the real board only | `build/host`: the full GUI runs in an SDL2 window, and the entire unit/golden-image suite runs, with no board attached |
| Second co-processor toolchain | N/A | ESP32/Arduino side integrated in the same repo (`tools/marauder-build/`) |

Neither approach is "wrong" — CMSIS-Solution is the vendor-blessed path and
Keil Studio Pack's Memory/Peripheral Inspector views are genuinely nice for
close register-level work. The trade-off CaDS Zero makes instead: plain CMake
means what you learn here (presets, `compile_commands.json`-driven
IntelliSense, `cortex-debug` against `st-util`) transfers directly to any
other embedded CMake project, not just ones opened through Keil's own project
format — and the host simulator means a student without a board yet, or
whose board is on someone else's desk this week, can still build, run, and
test almost everything.

## Troubleshooting

- **CMake Tools can't find a compiler for the `itsboard` preset** — the
  toolchain file couldn't resolve `arm-none-eabi-gcc`. Run
  `arm-none-eabi-gcc --version` in the same terminal VS Code was launched
  from; if that fails, revisit §1 (a `brew`/`apt` install needs a fresh
  terminal, and possibly VS Code itself, to pick up the updated `PATH`).
- **`cortex-debug` can't find `arm-none-eabi-gdb`** — same root cause as
  above; this extension resolves it via the same `PATH`.
- **Flash/debug fails right after a firmware update to `st-link`, or the
  probe seems to vanish** — see
  [Flash the board](flash.md) and [Debug with GDB](debug.md) for the
  probe-sharing and stale-server pitfalls; both apply the same way whether
  you're driving `st-flash`/`st-util` from a terminal or through this page's
  tasks/launch configs.
