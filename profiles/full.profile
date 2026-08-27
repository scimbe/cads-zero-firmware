# CaDS Zero build profile - full
#
# Everything the firmware has. Matches the built-in defaults (every
# CADS_APP_* option is ON) - this file exists so "full" is a name you can
# point CADS_PROFILE at rather than remembering that omitting a profile
# means the same thing.
#
#   cmake -S . -B build/itsboard -DCADS_PROFILE=profiles/full.profile ...

app.settings    = on
app.about       = on
app.gpio        = on
app.netinfo     = on
app.filebrowser = on
app.game        = on
app.netiperf    = on
app.nettools    = on
app.active      = on
