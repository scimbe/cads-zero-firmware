# Flash the board

```bash
scripts/flash.sh                              # build/itsboard/cads-zero.bin
scripts/flash.sh path/to/other.bin
```

## What it does, and what it refuses to do

```
st-flash --serial <probe> --reset write <bin> 0x08000000
```

`st-flash write` sector-erases only the range it is writing. A 13 KB image
touches sector 0 and nothing else:

```
EraseFlash - Sector:0x0 Size:0x4000 -> Flash page at 0x8000000 erased
```

Three guards, in order of how badly you would miss them:

- **No mass erase, ever.** A chip erase would take the littlefs volume in bank 2
  with it, and on a board configured for it could touch option bytes.
- **Image size is checked against 1 MB.** A larger image would run past flash
  bank 1 into the filesystem window at `0x08120000`, so the script refuses
  rather than corrupting it.
- **Option bytes are never written.** Setting read protection is either annoying
  or permanent.

## Picking the probe

`CADS_STLINK_SERIAL` selects the ST-Link when more than one is attached. The
default is the board this repository is developed against:

```bash
CADS_STLINK_SERIAL=066FFF565282494867161033 scripts/flash.sh
```

`st-info --probe` lists what is connected.

## If flashing fails

A stale GDB server holds the probe and produces confusing errors:

```bash
pkill -f st-util
```
