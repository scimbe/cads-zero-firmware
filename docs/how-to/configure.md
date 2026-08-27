# Configure the firmware

Two things you can configure, on two different timescales: what a running
board does (`/config.txt`), and what code is in the image at all (a build
profile). This page covers both as tasks. For the full format, see the
[configuration reference](../reference/config-file.md).

## Change a setting on a running board

The fast path, no host tools:

1. Open **Settings** from the menu.
2. Change **Brightness** or **SPI clock** directly, or open **Touch
   calibration** / **Test pattern**.
3. These take effect immediately and, for the ones stored in `/config.txt`,
   persist across reboots.

## Edit `/config.txt` from your computer

Use this when you want to change the network address, or several settings at
once, or keep a config under version control.

Prerequisites: `st-flash` on your PATH (`brew install stlink` on macOS,
`apt install stlink-tools` on Debian/Ubuntu), and a host build so the
`cads_fs` helper exists:

```bash
cmake -S . -B build/host && cmake --build build/host --target cads_fs
```

Then, with the board connected over SWD:

=== "Edit in place"

    ```bash
    scripts/cads_config.py edit
    ```

    Pulls the file, opens it in `$EDITOR`, and — only if you changed and
    saved it — writes it back.

=== "Pull, edit, push"

    ```bash
    scripts/cads_config.py pull            # -> ./config.txt
    $EDITOR config.txt
    scripts/cads_config.py push config.txt
    ```

Apply the change without a reboot:

1. On the board, open **Settings → Reload config**.
2. It reports `Loaded /config.txt and applied it.`

!!! note "Why not a USB drive"
    The board has no USB mass-storage interface; its filesystem is only
    reachable over the debug link. `cads_config.py` dumps the littlefs volume,
    edits one file inside the image with the exact littlefs code the firmware
    uses, and writes the volume back — the practical equivalent of mounting
    it. The firmware is never reflashed; only the filesystem region changes.

### Set a static IP

```ini
net.dhcp = 0
net.ip = 192.168.1.50
net.netmask = 255.255.255.0
net.gateway = 192.168.1.1
```

Push, then **Settings → Reload config**. The address is applied to the netif
immediately if the link is up, or the next time it comes up.

## Build an image with only the features you want

A build profile selects which optional apps compile in. Smaller images have
more RAM margin and less attack surface.

1. Pick or write a profile (`profiles/*.profile`):

    ```ini
    app.settings    = on
    app.netinfo     = on
    app.nettools    = on
    app.active      = off
    app.game        = off
    # ...one line per app; omitted apps keep their default (on)
    ```

2. **Validate it before building** — this catches a typo and a
   view-registry overflow in milliseconds instead of after a full build:

    ```bash
    scripts/check_profile.py profiles/minimal.profile
    ```

3. Configure and build with it:

    ```bash
    cmake -S . -B build/itsboard -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DCADS_PROFILE=profiles/minimal.profile
    cmake --build build/itsboard
    ```

    A `-DCADS_APP_X=ON/OFF` on the same line overrides the profile for that
    one app.

4. Confirm what actually built, and that it fits, in one step:

    ```bash
    scripts/check_profile.py profiles/minimal.profile --build
    ```

    This does a real toolchain build and runs the RAM-budget check against
    the result.

!!! warning "Rebuild directory"
    CMake caches option values. To change which profile a build directory
    uses, either pass the new `-DCADS_PROFILE=...` (profiles pre-seed cleanly)
    or delete the build directory for a clean slate. An explicit `-DCADS_APP_X`
    you set earlier stays in the cache until you clear it.

## See also

- [Configuration reference](../reference/config-file.md) — every key and tool flag.
- [Why they are two separate files](../explanation/config-design.md).
