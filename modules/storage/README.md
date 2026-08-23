# `modules/storage` — littlefs volume, flash driver, key/value store

## What is it?

Three layers, each depending only on the one below it. `cads/storage/flash.h`
is the narrow, medium-specific interface — read/program/erase against a
window of erasable blocks, addressed relative to the window rather than
absolutely. `cads/storage/storage.h` wires that into littlefs and exposes a
POSIX-ish path API (`open`/`read`/`write`/`seek`/`stat`/`rename`/`remove`/
`mkdir`/`dir_*`) with opaque handles drawn from fixed pools; it is the only
file in the module that includes `lfs.h`. `cads/storage/kv.h` is a client of
`storage.h` like any other caller — a flat, typed key/value table for the
handful of named settings a screen like `apps/settings` actually needs,
loaded whole into RAM and rewritten whole on save. `flash.h` has two
implementations selected by `CMakeLists.txt` on `CMAKE_SYSTEM_NAME`:
`src/cads_flash_stm32f4.c` (register-level driver, bank 2 of the STM32F429's
internal flash) when the ARM cross toolchain file is active, `src/cads_flash_host.c`
(a bounds-checked RAM array) otherwise. Everything above `flash.h` — the
littlefs glue and the key/value layer — is target-neutral C11 that builds
unchanged against either one.

## Why is it shaped this way?

**`flash.h` is the only place that knows what the medium is, on purpose.**
Its own header says so: the rest of the module is target-neutral C11 against
this one interface, so swapping the filesystem, or the medium, is a change to
one file rather than a change to every caller. `storage.h` extends the same
discipline upward — it never leaks `lfs_t`/`lfs_file_t` past its own `.c`
file, so nothing above this module includes `lfs.h` either.

**Addressing is window-relative, never absolute, everywhere in `flash.h`.**
A caller who gets an offset wrong lands somewhere else inside the 896 KB
window; a caller who gets an absolute address wrong lands in the firmware.
`cads_flash_geometry()->base` exists only so a diagnostic screen can report
where the window physically starts.

**Two independent bounds checks per flash operation, not one.**
`cads_flash_read()`/`program()` check the caller's offset+size against the
window before computing an address, then `cads_flash_addr_is_safe()` checks
the resulting absolute address again immediately before the register write;
`cads_flash_erase_block()` does the analogous pair against the block index
and the sector number. Per `docs/SAFETY.md` §4 this is deliberate belt and
suspenders: a bug in one check cannot silently become a write below
`0x08120000` while the other agrees. The compiled-in window itself is pinned
by `_Static_assert`s against `board.h`'s `CADS_FS_*` constants — it must
start at or after bank 2 sector 17, fit within 2 MB of flash, and equal
`block_size * block_count` exactly — checked once at compile time because
there is no register that answers "how much flash do I have" to probe
instead.

**Programming runs from flash (bank 1), not `.ramfunc` — this was a real
hardware bug, not a style choice.** An earlier version placed
`cads_flash_erase_sector()`/`cads_flash_program_word()` in `.ramfunc`, on the
reasoning that executing from RAM would remove any doubt about the safety of
erasing/programming bank 2 while bank 1 keeps fetching instructions. On real
hardware it did the opposite: the identical sequence run from `.ramfunc`
produced an intermittent `BusFault` or a spurious `CADS_FLASH_ERR_IO`/
`CADS_FLASH_ERR_VERIFY` a handful of runs in (confirmed via the fault
handler's stacked PC landing inside the RAM-resident routine). The same
sequence run from flash — what the code does now — has been reliable every
time it has been tried; the part is documented dual-bank (RM0090), so
fetching from bank 1 while writing bank 2 is legal and is what actually
happens. `docs/SAFETY.md` §4 was updated to match. This is also *why* M4 had
a mandatory hardware gate rather than trusting host tests: the host's
RAM-backed emulation (`cads_flash_host.c`) has no RAM-vs-flash execution
distinction to catch a bug like this at all.

**Every erase and program resets the ART accelerator's data cache.** Found
the same way: `cads_flash_program()`'s own post-write `memcmp()` verify
intermittently failed on a location that had just been read once
(`cads_flash_read()` right after erase, to confirm `0xFF`), with no error
flag set by the controller itself. `FLASH_ACR.DCEN` caches flash reads and
has no way to know the underlying content changed under it, so a stale cache
line, not a bad write, was the actual cause. `cads_flash_reset_data_cache()`
disables, resets, and re-enables the cache — RM0090's documented sequence,
DCEN cleared before DCRST — after every erase and every program, not only
when this driver is about to verify, because any caller could read a
just-modified address next.

**The program unit is 4 bytes because this board runs at 3.3 V.** That is
`FLASH_VOLTAGE_RANGE_3` in ST's terms, which allows `x32` program
parallelism (`PSIZE`); writing a narrower unit than the configured `PSIZE`
is documented as undefined, so `read_size`/`prog_size` are both pinned to
`CADS_FLASH_UNIT` (4) rather than left to the caller to get right.

**Bank-2 sector numbers need `+4` before they reach `FLASH_CR.SNB`.** `SNB`
is 5 bits: `SNB[3:0]` selects a sector within a bank, `SNB[4]` selects bank
2. Sectors 0..11 map straight through; sectors 12..23 need `+4` added to land
with `SNB[4]` set — sector 17, the first sector of this volume, is `SNB 21`,
not `SNB 17`. This isn't a derivation, it's ST's own HAL doing exactly this
before writing `FLASH_CR`, reproduced in `cads_flash_sector_to_snb()` because
it is the single easiest constant in this file to get wrong in a way that
erases the wrong sector instead of failing loudly.

**No mass erase, ever, and format() pays for that in wall-clock time.**
`cads_storage_format()` erases the module's seven 128 KB sectors one at a
time — several seconds — and touches nothing outside the window: the
firmware in bank 1 is never reachable from here, and nothing in this module
writes `FLASH_OPTCR` (`docs/SAFETY.md` §4).

**littlefs is built as its own library, `cads_littlefs`, not folded into
`cads_storage`'s sources.** It is vendored, not ours, and its internal
`int err` reuse repeatedly trips `cads_flags`' `-Wshadow`; not worth
carrying a diff for upstream's code, the same call already made for
`lib/Unity`.
`LFS_NO_MALLOC` turns any path that would need a heap into a build failure
instead of a silent `malloc()` — there is no heap on this device
(`docs/reference/memory-map.md`: FreeRTOS is static-allocation-only, and the
linker leaves `_sbrk`'s `end` symbol undefined on purpose). `LFS_NO_ASSERT`
was hard-won: without it, littlefs's default `LFS_ASSERT(test)` expands to
the real libc `assert()`, which pulls in the fprintf/stdio chain and
therefore `_sbrk` — the board build failed to *link*, not compile, over
exactly that undefined symbol, invisible on the host because it links a real
libc with a real heap. `LFS_NO_{DEBUG,WARN,ERROR}` keep littlefs's optional
printf tracing out of a firmware that links no formatted output anywhere
else.

**`CADS_STORAGE_BLOCK_CYCLES` is 500, the mid-range of littlefs' suggested
100–1000, and this volume is only 7 blocks.** littlefs relocates a metadata
pair to a fresh block after it has been erased this many times, to spread
wear. With a 1000-block volume that policy barely matters — wear is already
spread thin. With 7 blocks, the same knob controls something sharper: a
small value relocates often, spreading wear evenly but paying relocation
overhead constantly; a large value defers relocation, but leaves whatever is
metadata-hot concentrated on one of only seven blocks for far longer before
it moves. 500 is a deliberate middle ground given how few blocks there are
to spread wear across, not a default nobody looked at.

**No blob values in `cads_kv` — one file, whole-table rewrite.** What a
settings screen needs is a handful of named values (backlight percent, the
SPI clock choice, a calibration offset) surviving a reboot; that does not
justify a database. `cads_kv_open()` reads the whole table into a static
array once; every getter after that is a RAM lookup, and `cads_kv_save()`
writes header-then-entries back in one `cads_storage_write()` — a screen
that changes eight values in a row costs one erase-cycle-friendly write
instead of eight. `CADS_KV_MAGIC`/`CADS_KV_VERSION` guard the header so a
truncated or foreign file reports `CADS_STORAGE_ERR_CORRUPT` and leaves the
table empty rather than partially populated; a missing file is not a fault
at all — it means nothing has been saved yet.

**No allocation anywhere in the module.** `storage.h`'s file and directory
handles come from fixed pools (`CADS_STORAGE_MAX_FILES` = 4,
`CADS_STORAGE_MAX_DIRS` = 2 by default); `kv.h`'s entries come from a fixed
table (`CADS_KV_MAX_ENTRIES` = 32). Every one of these returns
`CADS_STORAGE_ERR_NOSLOT` once exhausted rather than growing — consistent
with `LFS_NO_MALLOC` and with `modules/toolbox`'s own no-heap rule.

**Not thread safe, and deliberately not fixed here.** One task owns the
filesystem; there is no internal mutex. `storage.h`'s own header explains
why: the only lock worth having also has to cover the multi-second erase
inside `format()`, and that's an application-level policy decision, not
something this module should impose unasked.

**`cads_flash_host.c` emulates NOR semantics, not just a RAM array.** Program
only clears bits (`ram[i] &= src[i]`, never a plain assignment) and erase
sets a block back to all-`0xFF`, so "programmed over live data" misbehaves
identically on host and board, and the same readback-verify test catches it
in both places. What it explicitly does not emulate — stated in its own
header — is erase/program timing, wear, or power loss mid-operation; the
`.ramfunc`-vs-flash bug and the data-cache bug above were only ever
reachable on real silicon, which is why M4 required a hardware gate (write,
reset without reflashing, read back, firmware-region CRC32 identical before
and after) rather than accepting the 30 host tests alone as sufficient.

## How do I use it?

```c
#include "cads/storage/storage.h"
#include "cads/storage/kv.h"

void storage_bringup(void) {
    int status = cads_storage_mount();
    if (status == CADS_STORAGE_ERR_CORRUPT) {
        /* Expected on a board that has never been formatted. */
        status = cads_storage_format();
    }
    if (status != CADS_STORAGE_OK) {
        return; /* cads_storage_status_text(status) for a log line */
    }

    /* A handful of named settings: one file, loaded whole, rewritten whole. */
    cads_kv_open("/settings.kv");
    int32_t backlight = 80;
    if (cads_kv_get_i32("backlight", &backlight) != CADS_STORAGE_OK) {
        cads_kv_set_i32("backlight", backlight); /* first boot: seed the default */
        cads_kv_save();
    }

    /* Anything that isn't a handful of named values: a plain file. */
    cads_storage_file_t* file = NULL;
    if (cads_storage_open(&file, "/leo.txt",
                           CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT | CADS_STORAGE_TRUNC) ==
        CADS_STORAGE_OK) {
        const char text[] = "Leo is dozing";
        cads_storage_write(file, text, sizeof(text));
        cads_storage_close(file); /* must be called even on a failed write */
    }
}
```

Link against `cads_storage` (`target_link_libraries(<target> PRIVATE cads_storage)`);
it pulls in `cads_littlefs` publicly. Include as `cads/storage/storage.h`,
`cads/storage/kv.h`, or `cads/storage/flash.h`.

## What are the limits?

- **Fixed pools, exhausted not grown.** Four open files, two open
  directories, thirty-two key/value entries by default
  (`CADS_STORAGE_MAX_FILES`/`CADS_STORAGE_MAX_DIRS`/`CADS_KV_MAX_ENTRIES`) —
  a fifth open file or a thirty-third key returns `CADS_STORAGE_ERR_NOSLOT`,
  never reallocates.
- **Names and values are capped, not truncated silently.** File names 63
  bytes, paths 127 bytes (`CADS_STORAGE_NAME_MAX`/`PATH_MAX`); key/value keys
  23 bytes, string values 31 bytes (`CADS_KV_KEY_MAX`/`STR_MAX`). A key or
  path over the limit is refused with `CADS_STORAGE_ERR_NAMETOOLONG`, not
  cut short.
- **`cads_kv` has no iteration API.** `cads_kv_has()`, `cads_kv_type()`, and
  `cads_kv_count()` exist; there is no "list every key" call, so a caller
  that wants to enumerate what's stored has to already know the names it's
  looking for.
- **`cads_kv_save()` carries no journal of its own.** It opens the target
  file `WRONLY | CREAT | TRUNC` and writes the header, then the entries, in
  up to two `cads_storage_write()` calls; whatever protection a power loss
  mid-save gets is whatever littlefs itself provides underneath, not
  anything this layer adds.
- **One task, no internal locking, anywhere in the module.** Calling from
  two tasks at once corrupts the volume; nothing here detects or prevents
  that.
- **No mass erase, no option-byte writes, no addresses below the window.**
  `format()` only ever erases this module's seven sectors; nothing in this
  module can touch bank 1 or `FLASH_OPTCR`, by construction rather than by
  convention.
- **The host build cannot exercise real flash behaviour.**
  `cads_flash_host.c` proves geometry, bounds, alignment, and NOR
  program/erase semantics — nothing about real erase timing, wear, power
  loss mid-operation, the ART data cache, or execution-location safety, all
  of which are board-only and reviewed by reading rather than tested on the
  host.
