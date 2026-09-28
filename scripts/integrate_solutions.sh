#!/usr/bin/env bash
# CaDS Zero - integration check of all lab solutions (apps/rnlab).
#
# A student fork ends the semester with every lesson in it at once, but each
# solution branch loesung/lNN-<slug> is developed and tested alone against
# praktikum/start. This merges origin/praktikum/start + origin/loesung/l01..l11
# in lesson order on a detached HEAD in its own worktree (nothing is pushed)
# and checks what only breaks in combination:
#
#   - merge conflict: stop, naming the files and the solution branches that
#     changed them since they forked (three-dot diff); if none did, the branch
#     collides with newer praktikum/start commits and needs rebasing/merging;
#   - after EVERY merge: firmware build + RAM/flash/FS gates, so a break or a
#     budget overrun is pinned to the lesson that caused it (and each lesson's
#     RAM/flash delta falls out of it);
#   - at the end: host build, full ctest - everything outside rnlab-L* green,
#     and rnlab-LNN green for every merged lesson. Lessons without a solution
#     branch are reported, not failed (their stubs are expected red).
#
# Usage: scripts/integrate_solutions.sh [--worktree DIR] [--quick] [--skip NN[,NN]] [--list]
#   --worktree DIR  worktree for the merge (default: <repo>/../cz-integ)
#   --quick         build + gates only once at the end (no per-lesson deltas)
#   --skip NN,...   leave these lessons out (e.g. until a conflict is fixed)
#   --list          only show which solution branches exist
# Exit 0 = everything green. Used by .github/workflows/integration.yml.
# Commands can be overridden (CI, tests): INTEG_FW_BUILD, INTEG_HOST_BUILD.
# The lab's private docs repo has the same check plus a hardware smoke test.
set -euo pipefail

FW="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && git rev-parse --show-toplevel)"
WT=""
QUICK=0
LIST=0
SKIP=""
LESSONS="01 02 03 04 05 06 07 08 09 10 11"

while [ $# -gt 0 ]; do
    case "$1" in
        --worktree) WT="$2"; shift 2 ;;
        --quick) QUICK=1; shift ;;
        --skip) SKIP=",$2,"; shift 2 ;;
        --list) LIST=1; shift ;;
        -h|--help) sed -n '2,27p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

[ -n "$WT" ] || WT="$(dirname "$FW")/cz-integ"
FW_BUILD="${INTEG_FW_BUILD:-bash scripts/build.sh Release}"
HOST_BUILD="${INTEG_HOST_BUILD:-cmake --preset host --fresh && cmake --build build/host}"
ELF="build/itsboard/cads-zero.elf"

say() { printf '%s\n' "$*"; }
fail() { say "ERROR: $*" >&2; exit 1; }

git -C "$FW" fetch -q origin
git -C "$FW" rev-parse -q --verify origin/praktikum/start >/dev/null ||
    fail "origin/praktikum/start missing in $FW"

branch_for() {
    git -C "$FW" for-each-ref --format='%(refname:short)' "refs/remotes/origin/loesung/l$1-*"
}

if [ "$LIST" = 1 ]; then
    for nn in $LESSONS; do
        b="$(branch_for "$nn")"
        say "L$nn  ${b:-(missing)}"
    done
    exit 0
fi

# --- worktree ----------------------------------------------------------------
if [ -d "$WT" ]; then
    # lib/lwip gets patched at configure time (modules/net) - expected, not dirt.
    if [ -n "$(git -C "$WT" status --porcelain --ignore-submodules=dirty)" ]; then
        fail "worktree $WT is not clean"
    fi
    git -C "$WT" checkout -q --detach origin/praktikum/start
else
    # Detached: no local branch that another worktree might already hold.
    git -C "$FW" worktree add -q --detach "$WT" origin/praktikum/start
fi
cd "$WT"
git submodule update -q --init --recursive
git config user.name >/dev/null || git config user.name "integrate-solutions"
git config user.email >/dev/null || git config user.email "integrate-solutions@localhost"
say "base: origin/praktikum/start $(git rev-parse --short HEAD), worktree $WT"

if [ -f scripts/cads_env.sh ]; then
    # shellcheck disable=SC1091
    set +u; source scripts/cads_env.sh; set -euo pipefail
fi
NM_ARGS=()
command -v arm-none-eabi-nm >/dev/null && NM_ARGS=(--nm arm-none-eabi-nm)

LOGDIR="$WT/build/integ-logs"
mkdir -p "$LOGDIR"

fw_build() { (eval "$FW_BUILD") >"$LOGDIR/$1.log" 2>&1; }
ram_margin() { python3 scripts/check_ram_budget.py ${NM_ARGS[@]+"${NM_ARGS[@]}"} "$ELF" 2>/dev/null |
    sed -nE 's/^margin *= *([0-9]+) B.*/\1/p'; }
flash_used() { python3 scripts/check_flash_budget.py "$ELF" 2>/dev/null |
    sed -nE 's/^flash image \(text\+data\) *= *([0-9]+) B.*/\1/p'; }
# The three CI gates; prints the failing one and returns 1.
gates() {
    local out
    out="$(python3 scripts/check_ram_budget.py ${NM_ARGS[@]+"${NM_ARGS[@]}"} "$ELF" 2>&1)" ||
        { say "  RAM gate: $(echo "$out" | tail -1)"; return 1; }
    out="$(python3 scripts/check_flash_budget.py "$ELF" 2>&1)" ||
        { say "  flash gate: $(echo "$out" | tail -1)"; return 1; }
    out="$(python3 scripts/check_fs_window.py "$ELF" 2>&1)" ||
        { say "  FS gate: $(echo "$out" | tail -1)"; return 1; }
}

# Indexed arrays, index = lesson number (bash 3.2 on macOS has no -A).
MERGED=(); RAMD=(); FLASHD=(); RESULT=(); MISSING=(); SKIPPED=()
PREV_RAM=""; PREV_FLASH=""
if [ "$QUICK" = 0 ]; then
    fw_build base || fail "firmware build of the base fails ($LOGDIR/base.log)"
    gates || fail "the base itself fails a gate"
    PREV_RAM="$(ram_margin)"; PREV_FLASH="$(flash_used)"
    say "base: RAM margin $PREV_RAM B, flash $PREV_FLASH B"
fi

# --- merges --------------------------------------------------------------------
for nn in $LESSONS; do
    i=$((10#$nn))
    b="$(branch_for "$nn")"
    if [ -n "$b" ] && [ "${SKIP#*,$nn,}" != "$SKIP" ]; then
        SKIPPED+=("L$nn"); say "L$nn: skipped (--skip) $b"; continue
    fi
    if [ -z "$b" ]; then
        MISSING+=("L$nn"); say "L$nn: no solution branch - skipped"; continue
    fi
    if ! git merge -q --no-ff --no-edit -m "Merge $b (integration check)" "$b" >"$LOGDIR/merge-$nn.log" 2>&1; then
        files="$(git diff --name-only --diff-filter=U)"
        culprits=""
        for other in ${MERGED[@]+"${!MERGED[@]}"}; do
            ob="${MERGED[$other]}"
            for f in $files; do
                # Three dots: only what $ob changed since it forked, not what
                # praktikum/start gained in the meantime.
                if git diff --name-only "origin/praktikum/start...$ob" -- "$f" | grep -q .; then
                    culprits="$culprits $ob"; break
                fi
            done
        done
        git merge --abort
        say "CONFLICT merging $b"
        say "  files:          $(echo $files)"
        if [ -n "$culprits" ]; then
            say "  collides with:$culprits"
        else
            say "  collides with: praktikum/start itself - $b is"
            say "                 $(git rev-list --count "$b..origin/praktikum/start") commits behind; merge/rebase origin/praktikum/start"
        fi
        exit 1
    fi
    MERGED[$i]="$b"
    say "L$nn: merged $b"
    if [ "$QUICK" = 0 ]; then
        if ! fw_build "l$nn"; then
            say "BUILD BREAK only after merging $b ($LOGDIR/l$nn.log):"
            grep -E "error|Error" "$LOGDIR/l$nn.log" | head -5 | sed 's/^/  /'
            exit 1
        fi
        gates || fail "gate fails only after merging $b"
        ram="$(ram_margin)"; flash="$(flash_used)"
        RAMD[$i]=$(( PREV_RAM - ram )); FLASHD[$i]=$(( flash - PREV_FLASH ))
        PREV_RAM="$ram"; PREV_FLASH="$flash"
    fi
done

# --- final build, gates, tests ------------------------------------------------------
STATUS=0
if [ "$QUICK" = 1 ]; then
    fw_build final || fail "firmware build fails ($LOGDIR/final.log)"
fi
say
gates && say "gates: RAM/flash/FS ok" || STATUS=1

(eval "$HOST_BUILD") >"$LOGDIR/host.log" 2>&1 || fail "host build fails ($LOGDIR/host.log)"
if ctest --test-dir build/host -LE '^rnlab-L' >"$LOGDIR/ctest-rest.log" 2>&1; then
    say "ctest without rnlab-L*: $(grep -E 'tests passed' "$LOGDIR/ctest-rest.log")"
else
    say "FAIL ctest without rnlab-L*:"; grep -E '\(Failed\)|\*\*\*' "$LOGDIR/ctest-rest.log" | sed 's/^/  /'
    STATUS=1
fi
for nn in $LESSONS; do
    i=$((10#$nn))
    if ctest --test-dir build/host --no-tests=error -L "^rnlab-L$nn\$" >"$LOGDIR/ctest-$nn.log" 2>&1; then
        RESULT[$i]="green"
    else
        RESULT[$i]="RED"
        [ -z "${MERGED[$i]:-}" ] || STATUS=1
    fi
done

# --- table -------------------------------------------------------------------------
say
printf '%-4s %-36s %-14s %10s %12s\n' "L" "solution branch" "rnlab-LNN" "RAM delta" "flash delta"
for nn in $LESSONS; do
    i=$((10#$nn))
    b="${MERGED[$i]:-}"
    if [ -n "$b" ]; then
        t="${RESULT[$i]}"; r="${RAMD[$i]:-}"; f="${FLASHD[$i]:-}"
        r="${r:+$r B}"; f="${f:+$f B}"
    else
        b="(missing)"; [ "${SKIP#*,$nn,}" != "$SKIP" ] && b="(--skip)"
        t="${RESULT[$i]} (stub)"; r=""; f=""
    fi
    printf '%-4s %-36s %-14s %10s %12s\n' "L$nn" "${b#origin/}" "$t" "${r:--}" "${f:--}"
done
say "total: RAM margin $(ram_margin) B, flash $(flash_used) B"
[ ${#MISSING[@]} -eq 0 ] || say "missing solutions: ${MISSING[*]}"
[ ${#SKIPPED[@]} -eq 0 ] || say "skipped (--skip): ${SKIPPED[*]}"
say
[ "$STATUS" = 0 ] && say "integration: all green." || say "integration: FAILED (logs: $LOGDIR)"
exit "$STATUS"
