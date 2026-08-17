# Tutorials

Learning-oriented walkthroughs. Each one is a single happy path with nothing
optional in it. If you want to accomplish a specific task rather than learn the
system, the [how-to guides](../how-to/index.md) are terser.

1. **[Build and flash your first image](first-build.md)** — from a fresh clone to
   a test pattern on the panel.
2. **[Read the on-target test](first-gate.md)** — what the hardware gate checks,
   what the numbers mean, and how to tell a real failure from a flaky port.

**Prerequisites for both:** a NUCLEO-F429ZI with the ITS adapter and the
Waveshare shield, connected by USB. macOS or Linux. Nothing else — the Arm
toolchain comes from the vcpkg artifact tree the Keil Studio extension manages,
and the test scripts have no Python dependencies.
