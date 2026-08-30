# Board <-> Mac secure link (primitive layer)

2026-08-28: the crypto primitive and entropy source for an eventual
encrypted board<->Mac link, built and hardware-verified. 2026-08-30: the
wire framing on top of it. **Not yet wired into any live transport** -
this page documents what exists today, not an aspirational full feature.

## Why

The board's own console/network interfaces (see
[the explorer console reference](explorer-console.md) and
[Marauder co-processor](marauder-coprocessor.md)) are plaintext today - the
serial console, and the `j`-command TCP `cads_cli` on port 4242. For a
scenario where the Mac (or a companion device) relays board data further
out - e.g. to an LLM-driven demo bridge over a real tunnel - the local
board<->Mac leg was identified as worth encrypting too, even though it
never leaves a local LAN, given this device's realistic physical-capture
threat model as a field pentesting tool.

## What's built

- **`modules/security`** (`cads/security/secure_link.h`): a thin wrapper
  over vendored [Monocypher](https://monocypher.org) 4.0.3
  (`lib/monocypher`, dual BSD-2-Clause/CC0-1.0, two files, no dependencies,
  no dynamic allocation) providing `cads_secure_link_seal()` /
  `cads_secure_link_open()` - XChaCha20-Poly1305 AEAD, 32-byte key, 24-byte
  nonce, 16-byte tag. Zero HAL dependency, fully host-testable
  (`tests/unit/test_secure_link.c`), including a known-answer test
  generated via an independent reference implementation
  (PyNaCl/libsodium's `crypto_aead_xchacha20poly1305_ietf`) - a real
  cross-implementation check, not just internal round-trip consistency,
  which is what actually matters for interoperating with a Mac-side
  implementation.
- **`cads_hal_rng_bytes()`** (`core/cads_hal.h`, `targets/itsboard/hal/hal_rng.c`):
  a real driver for the STM32F429's hardware RNG (RM0090 ch. 24), the
  entropy source for a random nonce per message. Implements the manual's
  documented procedure exactly, including the FIPS 140-2 continuous-RNG
  self-test on every word (not just at boot) and live SECS/CECS error
  checking. Board-only - the simulator has no RNG peripheral.
- **`J <n>`** (explorer console, board-only): dumps `n` random bytes
  (default 16, max 64) as hex - the live, on-hardware proof this driver
  actually works, since the host build can never exercise a real RNG
  peripheral. `board_cmd.py J 16`.

## Design decisions, stated explicitly

- **Fixed pre-shared key, no handshake.** The same class of design as
  WireGuard's or IPsec-ESP's static-SA mode: one long-term symmetric key,
  per-message nonces, no per-message key agreement. A full TLS 1.3 (what
  DTLS/CoAPS would need) handshake needs low-to-mid *kilobytes* of working
  memory even in the leanest embedded implementations - this firmware's
  RAM margin (currently ~670 B) does not afford that, and would not even
  if hand-rolled from scratch in `no_std` C, since the memory requirement
  is inherent to the TLS 1.3 handshake protocol itself, not an artifact of
  any particular library.
- **Random nonce per message, not a persisted counter.** A monotonic
  counter nonce is only safe if it never repeats for a given key across
  the device's *entire life*, including reboots - resetting to 0 on boot
  with a fixed key causes nonce reuse on the very first post-boot message,
  which is catastrophic for both AES-GCM and ChaCha20-Poly1305 (the
  authentication key becomes recoverable, not just the message). A random
  24-byte XChaCha nonce sidesteps this entirely: collision probability at
  that length is negligible even at very high message volumes, and it
  needs no persisted state across reboots at all - exactly why the
  hardware RNG driver above exists.
- **No forward secrecy at the PSK level - an accepted risk, not an
  oversight.** If the key is ever extracted from a captured board, every
  message ever sealed under it becomes decryptable. Real TLS/DTLS with
  ephemeral ECDHE would not have this property. Given this device's
  purpose (a field pentesting tool that could plausibly be physically
  seized), this is a real trade-off being made consciously, not a detail
  to silently absorb - see `modules/security/include/cads/security/secure_link.h`'s
  own header comment for the same statement kept next to the code.
- **No hardware crypto acceleration available.** The STM32F429 is F42xxx,
  not F43xxx - RM0090's own CRYP (cryptographic processor) chapter states
  it "applies to STM32F415/417xx and STM32F43xxx devices" only. Software
  ChaCha20 was the right choice regardless: it was explicitly designed
  (RFC 7905) to be fast and constant-time in plain software with no
  lookup tables, unlike table-driven AES without hardware acceleration.

## Wire framing (2026-08-30)

`modules/security/include/cads/security/secure_frame.h` -
`cads_secure_frame_encode()`/`cads_secure_frame_decode()`, a self-describing
frame format over `cads_secure_link`'s seal/open, built exactly to ct-agent's
2026-08-28 review notes: a 4-byte magic whose first byte is `>= 0x80` (never
confusable with cads_cli's plaintext ASCII, the same disambiguation
convention this project already uses for the touchscreen's headless
key-injection bytes), a little-endian length field, the 24-byte nonce in the
clear (not secret - standard AEAD practice), then ciphertext + a 16-byte mac.
`cads_secure_frame_decode()` is streaming-safe: fed a partial frame (a real
TCP segment boundary landing mid-frame) it reports `INCOMPLETE` and consumes
nothing, so a caller can call it again once more bytes arrive, and a bad
magic byte reports `BAD_MAGIC` with `consumed = 1` so a caller resyncing a
corrupted stream can just retry one byte at a time. 11 host tests
(`tests/unit/test_secure_frame.c`): round-trip, the documented header layout
byte-for-byte, both flavors of incomplete-frame, magic mismatch, tampered
ciphertext (and that the output buffer is still wiped through this layer,
not just at the `cads_secure_link` layer directly), wrong AD, an
undersized decode buffer, an undersized encode buffer, two frames
back-to-back on one stream, and the zero-length-plaintext edge case.
Zero RAM/flash cost on the shipped firmware - nothing calls it yet, so the
linker drops it entirely (confirmed: RAM margin unchanged, 928 B, across
this addition).

## Not yet built

- Any caller that actually invokes this framing (or `cads_secure_link`
  directly) against a real transport - the natural fit is `cads_cli`'s
  existing read/write path (port 4242, the `j` console command), but that
  is a live-transport change (touches lwIP/scheduler-adjacent code, a
  different risk class than this session's other additions) and is
  deliberately not folded into the framing work itself.
- Key provisioning - how a PSK actually gets onto the board (a `security.psk`
  config key via `modules/config` is the natural fit, not yet added).

See `docs/ROADMAP.md`'s 2026-08-28 Log for the fuller cross-session design
discussion that led here.
