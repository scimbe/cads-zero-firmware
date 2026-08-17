# The input scheme

How applications are driven. Designed around what this board actually has,
rather than around what the device it takes its cues from has.

!!! warning "Pin mapping pending measurement"
    The **scheme** below is settled. The **pin mapping** is not: a probe of
    PF0..PF7 and PG0..PG5 recorded zero edges while the buttons were pressed,
    which disproved the obvious assumption. The hardware explorer (`w` command)
    watches all 176 pins and will settle it. Nothing is committed to a mapping
    until that measurement exists.

## The starting point

A Flipper Zero has six buttons: up, down, left, right, OK, back. Every app is
written against that vocabulary, and it works because six is enough to express
"move, choose, retreat" without a manual.

This board has, in principle:

- **8 buttons** on the ITS adapter, in a row
- a **480×320 resistive touchscreen**
- the Nucleo's **USER** button
- 8 further input lines and 6 interrupt lines on the adapter

Eight buttons in a row is not a D-pad. Pretending otherwise — mapping buttons
1..4 to a cross — produces something that needs explaining every time. So the
scheme takes the row seriously as a row.

## The scheme: two rails and a context row

### Rail 1 — the row is a soft-key strip

The eight buttons sit in a line under the display. So the display labels them:
a strip along the bottom edge, eight cells wide, each cell showing what the
button below it does *right now*.

```
┌──────────────────────────────────────────────────────────┐
│                                                          │
│                   application content                    │
│                                                          │
├────┬────┬────┬────┬────┬────┬────┬────┬──────────────────┤
│ ▲  │ ▼  │ ◀  │ ▶  │ OK │ ⤺  │ F1 │ F2 │  ← soft-key strip│
└────┴────┴────┴────┴────┴────┴────┴────┴──────────────────┘
   1    2    3    4    5    6    7    8
```

This is the ATM/oscilloscope idiom, and it is the right one here because it
inherits the property that makes it work on those devices: **the button's
meaning is written directly above the button**. Nothing has to be memorised, and
an app with unusual actions does not have to bend them into a D-pad shape.

The default assignment gives the familiar six and keeps two free:

| Button | Default | Rationale |
|---|---|---|
| 1 | **Up** | The six navigation actions come first, in reading order, so |
| 2 | **Down** | muscle memory transfers between apps. |
| 3 | **Left** | |
| 4 | **Right** | |
| 5 | **OK** | |
| 6 | **Back** | |
| 7 | **F1** | App-defined. Labelled by the app. |
| 8 | **F2** | App-defined. |

An app may relabel any of the eight. A list view uses 1/2/5/6 and leaves 3/4
blank; a paint app might use all eight. The strip always shows the truth.

### Rail 2 — touch is direct manipulation

The touchscreen is not a second-class input and is not merely an on-screen
D-pad. Where an element is visible, touching it acts on it: tap a menu row to
choose it, drag a list to scroll, tap the soft-key strip to press that key
without reaching for the button.

The rule that keeps this coherent:

> **Every action reachable by touch is also reachable by button, and vice
> versa.** Neither input is required.

That matters for a lab device. A resistive panel needs a firm press, which is
awkward while probing something with the other hand; conversely, a precise
target is faster to hit with a finger than to walk to with a cursor. Supporting
both is not indulgence, it is the difference between usable and not.

### The context row — modifiers, not actions

The adapter's remaining input lines (switches rather than momentary buttons, if
that is what they turn out to be) are **modes, not commands**. A switch that
stays where it is put is a terrible "OK" and an excellent "hold this setting":

- lock the screen orientation
- freeze a live display
- select which of several profiles is active
- enable a verbose overlay

They are read as state, never as events, and an app that ignores them behaves
sensibly.

### The USER button — the escape hatch

The Nucleo's own button is deliberately **not** an application input. It is the
system-level escape:

- short press → return to the desktop
- long press (2 s) → force-terminate the current app

Every handheld needs one control that always does the same thing regardless of
what the software is doing. Keeping it out of the app vocabulary is what makes
that guarantee possible.

## Event model

Buttons produce four event types, which is what a menu, a list and a game each
need without any of them needing more:

| Event | When | Typical use |
|---|---|---|
| `Press` | on the debounced edge | immediate feedback, key highlight |
| `Release` | on the release edge | click semantics |
| `Repeat` | held past 400 ms, every 120 ms | scrolling a long list |
| `Long` | held past 800 ms, once | secondary action, context menu |

Debounce comes from measurement rather than a folk constant: the probe records
the shortest interval between consecutive edges on each line, which *is* the
bounce duration for that switch. Until that measurement lands the interval is
20 ms, which is conservative for a tactile switch.

Touch produces `Down`, `Move`, `Up`, plus `Tap` and `Drag` synthesised from
them. Gesture recognition stays deliberately thin — a resistive panel is
single-touch and noisy, and pinch or multi-finger gestures would be inventing
precision that the hardware does not have.

## Why not an on-screen D-pad

It was considered and rejected. It spends a fifth of a 480×320 display
reproducing a control the board already has eight of, physically, with tactile
feedback. The soft-key strip costs a 40-pixel band and makes the physical
buttons *more* useful rather than competing with them.

## Consequences for app authors

An app declares its key labels and receives events:

```c
static const cads_softkey_t keys[] = {
    {CadsKeyUp,    "Up"},
    {CadsKeyDown,  "Down"},
    {CadsKeyOk,    "Open"},     /* relabelled, still the OK slot */
    {CadsKeyBack,  "Exit"},
    {CadsKeyF1,    "Sort"},
};
```

The framework draws the strip, routes touches on it as key presses, and delivers
one event stream whether the input came from a finger or a switch. An app that
handles the six navigation keys works without knowing this board has eight
buttons and a touchscreen.
