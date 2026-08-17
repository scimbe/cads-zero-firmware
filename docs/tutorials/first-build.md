# Build and flash your first image

From a fresh clone to a test pattern on the panel. Ten minutes.

## 1. Clone with submodules

```bash
git clone --recurse-submodules https://github.com/scimbe/cads-zero
cd cads-zero
```

If you already cloned without them:

```bash
git submodule update --init --recursive
```

Six submodules: CMSIS, the STM32F4 device headers, FreeRTOS, lwIP, littlefs and
Unity. All permissive licences; see [Clean room](../explanation/clean-room.md).

## 2. Build

```bash
scripts/build.sh
```

The Arm toolchain is found automatically in the vcpkg artifact tree. You should
end with:

```
Memory region         Used Size  Region Size  %age Used
       FLASH_APP:       13412 B         1 MB      1.28%
             RAM:      107712 B       192 KB     54.79%
```

Most of that RAM is the framebuffer — 75 KB of it — and the staging buffers.
[Why 4 bpp](../explanation/why-4bpp.md) explains the arithmetic.

## 3. Check the board is there

```bash
st-info --probe
```

```
Found 1 stlink programmers
  serial:     066FFF565282494867161033
  chipid:     0x419
  dev-type:   STM32F42x_F43x
```

## 4. Flash and test in one step

```bash
scripts/board_test.py
```

This builds, flashes, resets, and reads the self test back over the ST-Link's
serial port. It ends with:

```
PASS: 9/9 assertions on real hardware
```

## 5. Look at the panel

The software cannot check this part. The display bus is write-only — there is no
readback path — so a human has to confirm the picture. You should see:

- a **CaDS blue** bar across the top with a **green** line under it
- sixteen palette swatches below it, the whole 16-colour palette
- four corner markers: red, green, amber, teal
- two white diagonals crossing the lower half
- a grey frame around the drawing area

Then touch the panel. Green squares should follow your finger, and the console
prints the coordinates.

**If the header bar is at the bottom**, the scan direction is inverted.
**If the colours look wrong**, the RGB565 byte order is. Both are exactly what
the pattern is designed to expose.

## Next

[Read the on-target test](first-gate.md) — what those nine assertions actually
check, and what the numbers mean.
