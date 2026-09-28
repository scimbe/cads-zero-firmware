# `apps/wetter` — weather station (lab L11)

## What is it?

One view (`CADS_VIEW_ID_WETTER`, `0x0D00`) in the apps menu ("Wetter") that
shows the current weather from open-meteo — temperature, humidity, wind,
WMO weather code as text and icon — refreshed every 10 minutes while the
view is open, with a status line for every failure mode (no link, no
address, DNS/HTTP errors, stale data with its age). `OK` fetches now.

`CADS_APP_WETTER` (default `ON`) builds it; it needs `CADS_APP_RNLAB` and
switches itself off without it. `apps/menu` registers the view,
`apps/bringup/explorer_app_demo.c` calls `cads_wetter_tick()` every loop pass.

## Why is it shaped this way?

**The app is glue; the decisions are host-tested logic.** When to fetch
(interval, retries 5 s → 5 min backoff, immediate fetch when the link
returns) and every string on the panel come from
`apps/rnlab/src/l11_wetter_app_logic.c` (`rnlab_wx_*`, tests in
`tests/unit/test_rnlab_l11.c`). The HTTP client is lab L10's
(`l10_http_wetter_1.h`), asynchronous, bounded buffers, 10 s timeout.
`lab 11 status|server|interval|refresh` works on the same controller
instance, so the console can point the display at a local test server and
read the app's redraw measurement.

**Every blit stops the Ethernet receiver** (PA7,
`docs/explanation/pa7-conflict.md`). The view's damage is one bounding box,
so the tick hands the compositor one changed field per frame instead of the
union of all changed fields, and nothing while nothing changes; the status
line changes at most once a minute. `tests/unit/test_wetter_app.c` renders
the view through the real compositor on the fake HAL and fails if any blit
leaves the changed field's rectangle.

## What are the limits?

- It only fetches while the view is shown (a fetch nobody looks at would
  also compete with `lab 10 get` for the one HTTP client).
- Plain HTTP only; the board has no TLS.
- In the simulator there is no network, so the view shows "Kein Link".
