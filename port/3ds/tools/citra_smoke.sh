#!/usr/bin/env bash
# port/3ds/tools/citra_smoke.sh
#
# Boots the built .3dsx in a headless 3DS emulator for 30 wall-seconds,
# scrapes the debug log, checks for ERROR-level lines, and reports the
# final frame counter via the bottom-screen console scrape.
#
# Supports either citra-headless (legacy) or Azahar (Citra successor).
# Requirements: one of {citra-headless, azahar-headless, azahar, citra}
# on PATH, plus GNU `timeout` for wall-clock cap.
# CI-friendly — exits non-zero on any failure, 77 (autotools "skip") if
# no emulator binary is installed.

set -euo pipefail

THREEDSX="${1:-port/3ds/PaperMario3DS.3dsx}"
DURATION="${PM3DS_SMOKE_SECONDS:-30}"
LOG="$(mktemp /tmp/pm3ds-smoke.XXXX.log)"

if [ ! -f "$THREEDSX" ]; then
    echo "FAIL: .3dsx not found at $THREEDSX" >&2
    exit 1
fi

# Prefer headless variants when available; fall back to the GUI binary
# which most builds still run when invoked with a .3dsx arg.
EMULATOR=""
for candidate in citra-headless azahar-headless azahar citra; do
    if command -v "$candidate" >/dev/null 2>&1; then
        EMULATOR="$candidate"
        break
    fi
done

if [ -z "$EMULATOR" ]; then
    echo "SKIP: no Citra/Azahar binary found on PATH" >&2
    exit 77   # autotools "skip" code
fi

echo "running $EMULATOR on $THREEDSX for ${DURATION}s"
timeout --signal=TERM "${DURATION}s" "$EMULATOR" --log-filter "*:Info" "$THREEDSX" \
    > "$LOG" 2>&1 || true

if grep -qE '\[ ?Error ?\]' "$LOG"; then
    echo "FAIL: ERROR-level log lines found:" >&2
    grep -E '\[ ?Error ?\]' "$LOG" | head -20 >&2
    exit 1
fi

if grep -q 'PANIC' "$LOG"; then
    echo "FAIL: pm3ds_panic fired:" >&2
    grep -A 5 'PANIC' "$LOG" | head -30 >&2
    exit 1
fi

# Extract the highest Frame: N seen.
last_frame=$(grep -oE 'Frame: [0-9]+' "$LOG" | tail -1 | awk '{print $2}' || echo "0")
echo "last frame seen: ${last_frame:-0}"

min_frames=$((DURATION * 30))   # very generous floor (game runs at 30+ FPS)
if [ "${last_frame:-0}" -lt "$min_frames" ]; then
    echo "FAIL: only ${last_frame:-0} frames in ${DURATION}s (need ≥ $min_frames)" >&2
    exit 1
fi

echo "OK: smoke passed (frames=$last_frame, duration=${DURATION}s, emulator=$EMULATOR)"
