# Cable diagnostics: how the "cable tester" feature works

The LAN8742A can find a fault on an Ethernet cable and estimate the distance
to it, entirely over MDIO. No RMII data path, no PA7, no dependency on
[the solder-bridge decision](pa7-conflict.md) — this works on the board today.

## What it actually measures

The PHY sends a pulse down one twisted pair and times the echo. An open end,
a short, or a properly terminated far end each reflect differently, so the
PHY reports which one it found and — for the fault cases — how long the
electrical path was. `docs/reference/measurements.md` will carry hardware
numbers once a deliberately faulted cable is available to test against;
today's verification confirms the mechanism works and recovers cleanly, not
yet the distance accuracy on a real fault.

Two distinct modes, because they measure different things:

**TDR (open/short), `cads_hal_eth_tdr_run()`.** Forces the PHY to 100 Mb full
duplex with auto-negotiation and Auto-MDIX off, fires a pulse on the selected
pair (MDI or MDIX — test both; a fault can sit on either), and reads back a
condition plus a raw electrical length. Converting that length to metres
needs a propagation constant that depends on the cable type — CAT5, CAT5e,
CAT6, or an "unknown" fallback with a wider error bound — straight out of the
datasheet's Table 3-8:

```
distance_m ≈ raw_length × P_open   (open fault)
distance_m ≈ raw_length × P_short  (shorted fault)
```

**This is disruptive.** Forcing the PHY out of auto-negotiation drops any
active link on the pair being tested for the duration of the test. The driver
saves the PHY's register state first and restores it unconditionally —
success, timeout, or a failed MDIO write all take the same restore path —
so the link renegotiates on its own afterwards. Verified on hardware:
`link=UP, autoneg=done, 100M full` before and after.

**Matched length, `cads_hal_eth_cable_length_matched()`.** Non-disruptive.
Only meaningful while a 100 Mb link is already up: it reads the far end's
distance from the Cable Length Register and looks it up in the datasheet's
Table 3-11 (0–123 m, ±20 m). Nothing is forced, nothing drops.

## Why unknown-cable-type is the safe default

Every propagation constant assumes you know what is actually in the wall.
Guess wrong and the distance is wrong, quietly. `CadsEthCableUnknown` — the
default the explorer's `c` command uses — trades precision for honesty: the
datasheet's own error tables (3-9, 3-10) show it costs roughly 2–3× the error
of picking the right constant, which is a fair price for not asserting a
cable type nobody confirmed.

## First hardware result

```
# link was up before the test: matched length ~6m
# running TDR - this will drop the link briefly
# TDR MDI  MATCHED (terminated / active far end, or no fault on this pair)
# TDR MDIX MATCHED (terminated / active far end, or no fault on this pair)
# cable test done, link will renegotiate
```

Both channels report `MATCHED`, consistent with a good cable into a live
switch — exactly what the bench setup is. The 6 m estimate is plausible for a
short patch cable. What this run proves: the register sequence is correct,
the PHY answers as documented, and the save/restore leaves the link exactly
as it was. What it does not yet prove: distance accuracy against a cable with
a real, known fault at a known length — that needs a cable someone is willing
to deliberately damage, which is a bench task for a human, not software.

## Try it

```
scripts/board_photo.py --pattern 0   # or just open a serial console
```

then, on the console:

```
c                 # run the full cable test
e                 # confirm the link recovered afterwards
```
