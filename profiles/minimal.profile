# CaDS Zero build profile - minimal
#
# The smallest useful image: desktop, menu and Settings only (Settings stays
# on deliberately - it is the only way to reach touch calibration and the
# config-reload entry, both of which matter even on a stripped-down build).
# Everything else off: no games, no network apps, no M9 offensive suite, no
# file browser. Meant as a starting point for a size- or attack-surface-
# conscious build, or as a known-good baseline to diff a custom profile
# against.
#
#   cmake -S . -B build/itsboard -DCADS_PROFILE=profiles/minimal.profile ...

app.settings    = on
app.about       = off
app.gpio        = off
app.netinfo     = off
app.filebrowser = off
app.game        = off
app.netiperf    = off
app.nettools    = off
app.active      = off
