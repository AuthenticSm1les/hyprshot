#!/usr/bin/env bash
#
# End-to-end tests for the shot binary.
#
# Usage: tests/cli_test.sh [path/to/shot]
#
# Argument handling is tested unconditionally. The capture tests need a live
# Wayland session with Hyprland; when there is none they are skipped.

set -uo pipefail

BIN="${1:-build/shot}"
if [[ "$BIN" != /* ]]; then
    BIN="$PWD/$BIN"
fi

export SHOT_NO_NOTIFY=1  # keep the desktop quiet while the suite runs

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTDIR="$ROOT/build/test-output"
ERRFILE="$ROOT/build/test-stderr.txt"

pass=0
fail=0
skipped=0

ok()   { pass=$((pass + 1)); printf '  ok    %s\n' "$1"; }
bad()  { fail=$((fail + 1)); printf '  FAIL  %s\n' "$1"; [[ -n "${2:-}" ]] && printf '        %s\n' "$2"; }
skip() { skipped=$((skipped + 1)); printf '  skip  %s\n' "$1"; }

# run <args...>: sets STATUS (exit code), OUT (stdout), ERR (stderr)
run() {
    OUT="$("$BIN" "$@" 2>"$ERRFILE")"
    STATUS=$?
    ERR="$(cat "$ERRFILE")"
}

expect_status() { # description expected
    if [[ "$STATUS" == "$2" ]]; then
        ok "$1"
    else
        bad "$1" "expected exit $2, got $STATUS; stderr: ${ERR:-<empty>}"
    fi
}

expect_contains() { # description haystack needle
    if [[ "$2" == *"$3"* ]]; then
        ok "$1"
    else
        bad "$1" "expected to contain '$3', got: ${2:-<empty>}"
    fi
}

is_png() {
    [[ -f "$1" && "$(head -c 8 "$1" | od -An -tx1 | tr -d ' \n')" == "89504e470d0a1a0a" ]]
}

if [[ ! -x "$BIN" ]]; then
    echo "cli_test: $BIN is not executable" >&2
    exit 1
fi

mkdir -p "$ROOT/build"

# ---------------------------------------------------------------------------
echo "cli_test: argument handling"
# ---------------------------------------------------------------------------

run --help
expect_status "--help exits 0" 0
expect_contains "--help prints usage" "$OUT" "Usage:"
if [[ -z "$ERR" ]]; then ok "--help writes to stdout, not stderr"; else bad "--help writes to stdout" "stderr: $ERR"; fi

run -h
expect_status "-h exits 0" 0

run --version
expect_status "--version exits 0" 0
expect_contains "--version prints a version" "$OUT" "shot"

run bogus
expect_status "unknown mode exits 2" 2
expect_contains "unknown mode is explained" "$ERR" "unknown mode"

run focused
expect_status "the removed 'focused' mode exits 2" 2
expect_contains "the removed 'focused' mode is explained" "$ERR" "unknown mode"

run --nope screen
expect_status "unknown option exits 2" 2

run --copy
expect_status "missing mode exits 2" 2

run screen --delay abc
expect_status "junk --delay exits 2 (no crash)" 2
expect_contains "junk --delay is explained" "$ERR" "--delay"

run screen --delay -1
expect_status "negative --delay exits 2" 2

run screen --dir
expect_status "missing option value exits 2" 2

run screen --copy --copy-only
expect_status "--copy with --copy-only exits 2" 2

run screen --copy-only --open
expect_status "--copy-only with --open exits 2" 2

run window --monitor DP-1
expect_status "--monitor outside screen mode exits 2" 2

# ---------------------------------------------------------------------------
echo "cli_test: captures"
# ---------------------------------------------------------------------------

if [[ -z "${WAYLAND_DISPLAY:-}" || -z "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]] || ! command -v hyprctl >/dev/null; then
    skip "no live Hyprland session; capture tests skipped"
else
    rm -rf "$OUTDIR"
    mkdir -p "$OUTDIR"

    # A plain screen capture writes a PNG and reports where it went.
    run screen --dir "$OUTDIR"
    expect_status "screen capture exits 0" 0
    if [[ -n "$OUT" && -f "$OUT" ]] && is_png "$OUT"; then
        ok "screen capture wrote a PNG and printed its path"
    else
        bad "screen capture wrote a PNG" "stdout: ${OUT:-<empty>}"
    fi

    # Back-to-back captures must not overwrite each other.
    first="$(SHOT_NO_NOTIFY=1 "$BIN" screen --dir "$OUTDIR" 2>/dev/null)"
    second="$(SHOT_NO_NOTIFY=1 "$BIN" screen --dir "$OUTDIR" 2>/dev/null)"
    if [[ -n "$first" && -n "$second" && "$first" != "$second" && -f "$first" && -f "$second" ]]; then
        ok "rapid captures get distinct filenames"
    else
        bad "rapid captures get distinct filenames" "'$first' vs '$second'"
    fi

    # An explicit filename is honoured exactly.
    run screen --dir "$OUTDIR" --file custom-name.png
    if [[ -f "$OUTDIR/custom-name.png" ]]; then
        ok "--file names the output"
    else
        bad "--file names the output" "missing $OUTDIR/custom-name.png"
    fi

    # Parent directories implied by --file are created.
    run screen --dir "$OUTDIR" --file nested/deeper/shot.png
    if [[ -f "$OUTDIR/nested/deeper/shot.png" ]]; then
        ok "--file creates parent directories"
    else
        bad "--file creates parent directories" "missing $OUTDIR/nested/deeper/shot.png"
    fi

    # An unknown output makes grim fail, and shot reports it.
    run screen --dir "$OUTDIR" --monitor NOT-AN-OUTPUT
    expect_status "capture failure exits 4" 4

    # Save-and-copy.
    run screen --dir "$OUTDIR" --copy
    expect_status "--copy exits 0" 0
    expect_contains "--copy mentions the clipboard" "$ERR" "copied to clipboard"

    # Copy-only must not leave a file behind.
    before="$(find "$OUTDIR" -type f | wc -l)"
    "$BIN" screen --copy-only >"$ERRFILE.out" 2>"$ERRFILE"
    status=$?
    after="$(find "$OUTDIR" -type f | wc -l)"
    if [[ "$status" == 0 ]]; then
        ok "--copy-only exits 0"
    else
        bad "--copy-only exits 0" "exit $status; $(cat "$ERRFILE")"
    fi
    expect_contains "--copy-only mentions the clipboard" "$(cat "$ERRFILE")" "copied to clipboard"
    if [[ -z "$(cat "$ERRFILE.out")" ]]; then
        ok "--copy-only prints no path"
    else
        bad "--copy-only prints no path" "stdout: $(cat "$ERRFILE.out")"
    fi
    if [[ "$before" == "$after" ]]; then
        ok "--copy-only writes no file"
    else
        bad "--copy-only writes no file" "$before -> $after files"
    fi
    rm -f "$ERRFILE.out"
fi

# ---------------------------------------------------------------------------
printf '\ncli_test: %d passed, %d failed, %d skipped\n' "$pass" "$fail" "$skipped"

if [[ "$fail" -gt 0 ]]; then
    [[ -d "$OUTDIR" ]] && echo "artefacts kept in $OUTDIR"
    exit 1
fi
rm -rf "$OUTDIR" "$ERRFILE"
exit 0
