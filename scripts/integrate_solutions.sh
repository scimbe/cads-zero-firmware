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
#   - at the end: the largest TCP configuration the lab offers (L08/L09
#     measurement builds: TCP_MSS 1460, WND 32, SND_BUF 16) once more through
#     build + gates - lwIP's pools grow in CCM there, on top of every lesson's
#     own CCM buffers, so an overflow only shows with all lessons merged;
#   - per lesson, its static SRAM (every input section of its own objects
#     lNN_*.c that the linker map places in SRAM, CCM excluded) against the
#     guideline ("Richtmaß") table in apps/rnlab/README.md - a WARNING only,
#     since the guideline is a planning figure, not a gate. The per-merge
#     delta cannot stand in for this: praktikum/start already carries every
#     lesson's stub, so a delta only shows solution-over-stub;
#   - host build, full ctest - everything outside rnlab-L* green,
#     and rnlab-LNN green for every merged lesson. Lessons without a solution
#     branch are reported, not failed (their stubs are expected red).
#
# Usage: scripts/integrate_solutions.sh [--worktree DIR] [--quick] [--skip NN[,NN]] [--list]
#   --worktree DIR  worktree for the merge (default: <repo>/../cz-integ)
#   --quick         build + gates only once at the end (no per-lesson deltas)
#   --skip NN,...   leave these lessons out (e.g. until a conflict is fixed)
#   --list          only show which solution branches exist
# Exit 0 = everything green. Used by .github/workflows/integration.yml.
# Commands can be overridden (CI, tests): INTEG_FW_BUILD, INTEG_FW_BUILD_MAX
# (the max-TCP build; empty = skip it), INTEG_HOST_BUILD; INTEG_FW_REPO picks
# the repository. Self-test: python3 tests/scripts/test_integrate_solutions.py
# The lab's private docs repo has the same check plus a hardware smoke test.
set -euo pipefail

# The repo to integrate: this script's own, unless INTEG_FW_REPO names another
# (the lab's private wrapper runs a copy of this file from outside any clone;
# the self-test points it at a throwaway repo).
FW="${INTEG_FW_REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && git rev-parse --show-toplevel)}"
FW="$(cd "$FW" && pwd)"
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
MAX_ARGS="-DCADS_RNLAB_TCP_MSS=1460 -DCADS_RNLAB_TCP_WND_MSS=32 -DCADS_RNLAB_TCP_SND_BUF_MSS=16"
# Own build directory: CMake keeps cache options, so building the max config
# in build/itsboard would silently turn every later default build (the next
# run in this worktree) into a max build too.
FW_BUILD_MAX="${INTEG_FW_BUILD_MAX-cmake -S . -B build/itsboard-max -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake -DCMAKE_BUILD_TYPE=Release -DCADS_ARM_TOOLCHAIN_BIN=\$(dirname \$(command -v arm-none-eabi-gcc)) $MAX_ARGS && cmake --build build/itsboard-max}"
HOST_BUILD="${INTEG_HOST_BUILD:-cmake --preset host --fresh && cmake --build build/host}"
ELF="build/itsboard/cads-zero.elf"
ELF_MAX="build/itsboard-max/cads-zero.elf"

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

# Tools the builds and gates need (arm-none-eabi-nm, ninja, cmake). On a
# developer machine they may live only in the vcpkg tree Keil Studio manages
# (scripts/build.sh finds them via cads_env.sh). cads_env.sh itself is not
# sourced: it exits under `set -e` wherever that tree is missing (CI). Each
# tool is looked up on its own - having one on PATH says nothing about the
# others. Globs that match nothing stay literal and fail the -d test.
tool_dir() { # $1 = tool, $2 = glob of candidate bin dirs
    command -v "$1" >/dev/null && return 0
    local d
    for d in $2; do
        [ -d "$d" ] && [ -x "$d/$1" ] && { PATH="$d:$PATH"; return 0; }
    done
    return 0
}
tool_dir arm-none-eabi-nm "$HOME/.vcpkg/artifacts/*/compilers.arm.arm.none.eabi.gcc/*/bin"
tool_dir ninja "$HOME/.vcpkg/artifacts/*/tools.ninja.build.ninja/*"
tool_dir cmake "$HOME/.vcpkg/artifacts/*/tools.kitware.cmake/*/bin"
export PATH
NM_ARGS=()
command -v arm-none-eabi-nm >/dev/null && NM_ARGS=(--nm arm-none-eabi-nm)

LOGDIR="$WT/build/integ-logs"
mkdir -p "$LOGDIR"

fw_build() { (eval "${2:-$FW_BUILD}") >"$LOGDIR/$1.log" 2>&1; } # $1 log name, $2 command
ram_margin() { python3 scripts/check_ram_budget.py ${NM_ARGS[@]+"${NM_ARGS[@]}"} "${1:-$ELF}" 2>/dev/null |
    sed -nE 's/^margin *= *([0-9]+) B.*/\1/p'; }
flash_used() { python3 scripts/check_flash_budget.py "$ELF" 2>/dev/null |
    sed -nE 's/^flash image \(text\+data\) *= *([0-9]+) B.*/\1/p'; }
# The three CI gates; prints the failing one and returns 1.
gates() { # $1 = ELF (default: the default build)
    local out elf="${1:-$ELF}"
    out="$(python3 scripts/check_ram_budget.py ${NM_ARGS[@]+"${NM_ARGS[@]}"} "$elf" 2>&1)" ||
        { say "  RAM gate: $(echo "$out" | tail -1)"; return 1; }
    out="$(python3 scripts/check_flash_budget.py "$elf" 2>&1)" ||
        { say "  flash gate: $(echo "$out" | tail -1)"; return 1; }
    out="$(python3 scripts/check_fs_window.py "$elf" 2>&1)" ||
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

# --- per-lesson SRAM guideline (warning only) ------------------------------------------
# Parsed, not hard-coded: rows "| LNN <slug> | 0,5 KB | ..." below the README's
# "Richtmaß je Lektion" heading. Rows without "KB" (the timer table) never match.
guideline_bytes() { # $1 = NN -> bytes, empty if the README has no row
    LC_ALL=C awk -v nn="$1" '
        /Richtmaß je Lektion/ { on = 1 }
        on && $0 ~ "^\\| L" nn " " {
            if (match($0, /\| *[0-9]+(,[0-9]+)? KB *\|/)) {
                # "0,5" -> 512 without floats: the decimal separator
                # in number parsing follows the locale.
                v = substr($0, RSTART + 1, RLENGTH - 2); gsub(/ |KB/, "", v)
                n = split(v, part, ",")
                b = part[1] * 1024
                if (n > 1) b += int(part[2] * 1024 / (10 ^ length(part[2])))
                printf "%d\n", b; exit
            }
        }' apps/rnlab/README.md 2>/dev/null
}
# Static SRAM of lesson NN's own objects in the current build, from the map.
lesson_sram() { # $1 = NN -> bytes
    LC_ALL=C awk -v nn="$1" '
        function hex(h,   i, c, v) {
            v = 0; h = tolower(substr(h, 3))
            for (i = 1; i <= length(h); i++) {
                c = index("0123456789abcdef", substr(h, i, 1)) - 1
                v = v * 16 + c
            }
            return v
        }
        index($0, "(l" nn "_") && /\.c\.obj\)/ {
            n = 0
            for (f = 1; f <= NF; f++) if ($f ~ /^0x[0-9a-fA-F]+$/) val[++n] = $f
            if (n < 2) next
            a = hex(val[1])
            if (a >= 536870912 && a < 537067520) sum += hex(val[2])  # 0x20000000..0x2002ffff
        }
        END { printf "%d\n", sum }' build/itsboard/cads-zero.map 2>/dev/null
}
WARNINGS=(); SRAM=(); GUIDE=()
for nn in $LESSONS; do
    i=$((10#$nn))
    [ -n "${MERGED[$i]:-}" ] || continue
    SRAM[$i]="$(lesson_sram "$nn")"
    g="$(guideline_bytes "$nn")"
    GUIDE[$i]="$g"
    [ -n "$g" ] || continue
    if [ "${SRAM[$i]}" -gt "$g" ]; then
        w="L$nn has ${SRAM[$i]} B static SRAM, guideline $g B (apps/rnlab/README.md)"
        WARNINGS+=("$w")
        [ -z "${GITHUB_ACTIONS:-}" ] || echo "::warning title=RAM guideline L$nn::$w"
    fi
done

# --- table -------------------------------------------------------------------------
say
printf '%-4s %-36s %-14s %10s %12s %16s\n' "L" "solution branch" "rnlab-LNN" "RAM delta" "flash delta" "SRAM/guideline"
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
    sg="-"; [ -z "${SRAM[$i]:-}" ] || sg="${SRAM[$i]}/${GUIDE[$i]:-?} B"
    printf '%-4s %-36s %-14s %10s %12s %16s\n' "L$nn" "${b#origin/}" "$t" "${r:--}" "${f:--}" "$sg"
done
say "total: RAM margin $(ram_margin) B, flash $(flash_used) B"

if [ -n "$FW_BUILD_MAX" ]; then
    if ! fw_build max-tcp "$FW_BUILD_MAX"; then
        say "FAIL max-TCP build (MSS 1460, WND 32, SND_BUF 16) - $LOGDIR/max-tcp.log:"
        grep -E "overflow|error|Error" "$LOGDIR/max-tcp.log" | head -5 | sed 's/^/  /'
        STATUS=1
    elif gates "$ELF_MAX"; then
        say "max-TCP build (MSS 1460, WND 32, SND_BUF 16): gates ok, RAM margin $(ram_margin "$ELF_MAX") B"
    else
        say "FAIL max-TCP build: gate"; STATUS=1
    fi
fi
[ ${#MISSING[@]} -eq 0 ] || say "missing solutions: ${MISSING[*]}"
[ ${#SKIPPED[@]} -eq 0 ] || say "skipped (--skip): ${SKIPPED[*]}"
for w in ${WARNINGS[@]+"${WARNINGS[@]}"}; do say "WARNING $w"; done
say
[ "$STATUS" = 0 ] && say "integration: all green." || say "integration: FAILED (logs: $LOGDIR)"
exit "$STATUS"
