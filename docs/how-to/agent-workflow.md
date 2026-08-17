# Work an issue as an agent

How parallel contributors operate on this project.

## The rules

**1. The maintainer holds the hardware exclusively.**

Do not flash the board, reset it, run `scripts/flash.sh` or `scripts/board_test.py`,
or attach a debugger. There is one ST-Link and one board; two things driving it
concurrently produce failures that look like firmware bugs and are not.

Write the code and the tests. The maintainer runs the gate and merges.

**2. Only take `swarm-ready` issues.**

| Label | Meaning |
|---|---|
| `swarm-ready` | Self-contained and hardware-free. Take it. |
| `hardware-gate` | Needs the physical board. Maintainer only. |
| `needs-decision` | Blocked on a user decision. Do not start. |

**3. [Safety rules](../SAFETY.md) are binding**, including for code you cannot
run. A driver that reconfigures PF0-7 as outputs is wrong even if it never gets
flashed.

**4. Clean room.** No code from `flipperzero-firmware`. See
[Clean room](../explanation/clean-room.md) for where the line is.

**5. Both targets.** Everything above the HAL must build for the board *and* the
simulator. A feature that only builds for one is not finished.

## The loop

```bash
git checkout -b issue-<n>-<slug>

# implement

cmake -S . -B build/host -G Ninja && cmake --build build/host && ctest --test-dir build/host
scripts/build.sh                       # must link, and stay inside the size budget

# update docs/ROADMAP.md, then
gh pr create --title "[M<n>] ..." --body "Closes #<n>. ..."
```

## What a reviewable PR contains

- The change, and nothing unrelated to the issue.
- Tests where the logic allows it — unit tests for anything portable, golden
  images for anything that draws.
- `docs/ROADMAP.md` updated.
- If it changes a hardware path: a note saying what the maintainer should look
  for at the bench, because they will be the one running it.
- If it changes memory usage: the new size report.
