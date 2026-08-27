# Configuration, profiles, and the decisions behind them

Persistence arrived late in this firmware, and deliberately so. For a long
time the only durable state was the littlefs volume and a handful of raw
calibration values; everything else was rebuilt from defaults on every boot.
Adding a real configuration story — a file a person edits, a build that can be
shaped to a purpose — meant making several decisions that each had a plausible
alternative. This page is the argument for the ones that were taken.

## Two files, not one

The single most consequential decision was to keep **runtime configuration**
and **build-time feature selection** in separate files. They look similar —
both are `key = value` text, both are "configuration" — and folding them into
one file is the obvious economy. It is also a category error.

The two answer questions on different timescales, read by different actors:

| | `/config.txt` | `*.profile` |
|---|---|---|
| Read by | the firmware, at boot | CMake, at configure time |
| Lives on | the board's flash | the developer's disk / the repo |
| Changes | while the product is in the field | while the product is being built |
| Effect of a bad value | one setting reverts to default | a feature is absent from the image |

A field engineer editing a network address should not be able to accidentally
remove the network stack; a build server selecting a minimal image should not
need a running board. Merging the files would make each of those failure modes
reachable from the other's editing context. The separation is not tidiness —
it is a blast-radius decision. The cost is a second small parser and a second
small format to document, which is cheap next to the confusion a unified file
would invite the first time someone edited the wrong section.

## Text, not the key-value store that already existed

The firmware already had a persistence primitive: `cads/storage`'s `kv`, a
binary table used for touch calibration. Reusing it for configuration would
have saved a module. It was rejected for one reason that overrides the saving:
**the config file's entire purpose is that a human edits it.** A binary table
is not hand-editable, and building a host-side editor for a bespoke binary
format is more work than a text parser, for a worse result.

So the split runs along a real seam. Calibration stays in `kv`, because its
values — raw XPT2046 ADC counts — mean nothing to a human and are never edited
by hand; they are produced by a tap and consumed by the driver. Configuration
is text, because every value in it is something a person might reasonably want
to set: a brightness, an IP address, an SSID. The storage mechanism follows
the audience, not the other way round.

The text parser pays its way beyond editability. It tolerates a partial file:
an unknown key is skipped, an omitted key keeps its default, a malformed IP
leaves the previous value intact. A hand-truncated file — the normal outcome
of an interrupted edit over a debug link — degrades to "defaults plus whatever
parsed" rather than a struct full of zeros. A binary format would have to
carry a version and a checksum to reach the same robustness; the text format
gets it from the grammar.

## Editing over SWD, because there is no USB drive

The natural way to edit a file on a device is to mount it as a drive. This
board cannot do that: it exposes no USB mass-storage interface, only the SWD
debug link. Three options remained:

1. **Implement USB-MSC in the firmware.** Large, and it exposes the raw flash
   sectors to any host that enumerates the device — including the sectors the
   firmware itself runs from, unless carefully partitioned. A significant new
   attack and corruption surface for the convenience of a drive icon.
2. **`littlefs-fuse` on the host.** A real mount, but it is a per-developer
   dependency to install and keep version-matched to the firmware's littlefs,
   and it still needs the volume dumped over SWD first.
3. **Dump, edit the image with the firmware's own littlefs, write it back.**
   Chosen.

Option 3 wins on a property the other two lack: `cads_fs`, the host tool, is
built from the *same* `cads/storage` and littlefs sources the firmware links.
The on-disk format cannot drift between the board and the tool, because there
is one implementation of it. `scripts/cads_config.py` wraps the dump/edit/
restore into `pull`, `push`, and `edit`, and the result is the practical
equivalent of mounting the filesystem: you edit a file, and the change lands
on the board, with the firmware never reflashed.

The honest cost of option 3 is that it is not *live* — there is a dump and a
restore around each edit, and the board must be on the debug link. For a
developer-facing configuration workflow that is an acceptable trade; for an
end-user product it would not be, and that is the boundary at which USB-MSC
would become worth its risks.

The tool is plain C11 over `stdio` with no platform calls, so the same source
builds under gcc, clang, and MSVC — the cross-platform requirement is met by
writing to the standard library rather than to an OS.

## The profile mechanism: pre-seed, don't force

A build profile has to change which `CADS_APP_*` options are on, and it has to
lose to an explicit `-D` on the command line — a developer overriding one app
for a one-off build should not have to edit a file. CMake offers a few ways to
set a cached variable; the choice among them is the whole behaviour.

`set(... CACHE BOOL ... FORCE)` would make the profile win over everything,
including the command line — wrong. A plain `set(... CACHE BOOL ...)` without
`FORCE` only takes effect when the variable is *not already in the cache*.
Because an explicit `-D` populates the cache before any of the project's own
CMake runs, and the profile block runs before the `option()` calls, the
precedence falls out for free: **command-line `-D` → profile → built-in
default**. No conditional logic, no ordering hazard beyond placing the profile
block ahead of the options — the cache semantics do the arbitration.

This mechanism has one genuine liability: there are now **two parsers** for the
profile format, CMake's and `check_profile.py`'s, and two parsers of one format
can disagree. The mitigation is scope discipline. CMake is the source of truth
— it is what actually configures the build. `check_profile.py` exists only to
fail *faster* and with a clearer message, and to run the checks CMake cannot
(a real RAM-fit build, and the view-capacity analysis below). It is a
pre-flight, not a second authority; when the two would differ, CMake's result
is the one that ships. That framing keeps the duplication honest.

## The check that matters: view-registry capacity

`check_profile.py` could have stopped at syntax. The check that earns its
place is a dependency one: does the set of apps a profile enables register
more views than the fixed-size dispatcher table can hold?

This is worth automating because the failure is *silent*.
`cads_view_dispatcher_add()` returns `false` past capacity, every caller
discards the return with `(void)`, and the excess views simply never exist —
a dead menu row, a navigation target that does nothing, with no error anywhere.
That exact bug shipped twice in this project before it was understood. A
feature-selection mechanism that lets a profile quietly reintroduce it would
be worse than no mechanism, so the checker computes the view count from the
enabled apps and compares it against the constant read straight out of the
source. A profile that would overflow fails validation on a laptop instead of
disappointing someone in front of a panel.

The checker's per-app view table is maintained by hand, which is a drift risk.
It is an acceptable one because it is backstopped: the host-side app-tree test
asserts the real registration count against the real capacity, so a table that
falls out of step with the code fails a build regardless of the profile. The
hand table buys speed for the common case; the test guarantees correctness for
all of them.

## What made all of this affordable

None of this would have been comfortable a day earlier. This firmware spent
its whole life against a 48 KB heap floor with a margin measured in hundreds of
bytes; a new module plus a host of static config state would have meant
another anxious round of trimming lwIP pools. The reason the config work
proceeded without that fight is a single change made just before it: reclaiming
15 KB of RAM that a double-buffered display staging bank was holding for an
overlap that never happened.

That is worth stating as a decision in its own right, because it reframed every
decision after it. With the margin at ~10 KB instead of ~256 B, "carry the WiFi
fields now even though the driver does not exist yet" became obviously correct
rather than a luxury; deepening the Ethernet RX ring became a measurement to
act on rather than a wish. A structural RAM win does not just add headroom — it
changes which designs are on the table. The config file is, in part, a
dividend of that reclamation.

## Reserving what does not exist yet

The config file carries `wifi.*` keys for an ESP32 dev board whose UART driver
is not written and whose hardware the project lead is still building. Storing
fields a running firmware ignores looks like dead weight. It is a deliberate
bet on where the cost is cheapest.

A configuration file is a compatibility surface. The day the WiFi driver
lands, a config format that already has a place for an SSID needs no migration
— old files stay valid, new files gain meaning. Adding the fields later would
mean either a format version bump or a silent extension that older tooling
mishandles. The fields cost a few bytes of struct and a few lines of parser
now; adding them later costs a compatibility event. Reserving them is the
smaller bill, paid at the cheaper time.

## Features as tests for infrastructure

One decision was not planned: the config feature became a test for the flash
driver, and found a real defect. Writing `/config.txt` on a fresh board is the
first time a normal boot performs a genuine flash *erase and program* — every
prior write came from an explicit storage-test command, never the boot path.
That cold path ran straight into an unbounded `while (FLASH->SR & BSY) {}`
wait that no test had ever exercised from that context, and the board hung
silently.

The lesson generalises past the fix. A new feature that touches a
long-dormant code path is a better test of that path than any unit test aimed
at it deliberately, precisely because it arrives without the assumptions the
path's author held. The value was not only bounding the wait; it was learning
that the wait had never been bounded in anger, and that the firmware's own
watchdog had been quietly masking the gap. The feature paid for itself twice:
once as a feature, once as a probe into infrastructure everyone had assumed was
finished.

## See also

- [Configuration reference](../reference/config-file.md)
- [How to configure the firmware](../how-to/configure.md)
- [Why dirty rectangles are mandatory](dirty-rectangles.md) — the display bus economics behind the 15 KB reclamation.
- [The PA7 conflict](pa7-conflict.md) — the shared-pin arbitration these decisions live alongside.
