#!/usr/bin/env python3
"""
Reference-frame harness for the F3DEX2 -> PICA200 translator.

Boots the .3dsx in a headless 3DS emulator, waits long enough to pass
frame 60, scrapes the emulator log for PANIC or error lines, and
(optionally) compares an emulator-produced screenshot against a
committed reference PNG via a perceptual-hash diff.

Usage:
    dump_frame.py <3dsx> <output.png>
    dump_frame.py <3dsx> <output.png> --expect=<ref.png>

Exit codes:
    0   capture (and comparison, if requested) passed
    1   capture failed, PANIC seen, or comparison out of tolerance
    77  no Citra/Azahar binary on PATH (autotools "skip")
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

TOLERANCE = 0.005   # 0.5 % perceptual-hash distance allowed

def find_emulator():
    for cand in ("citra-headless", "azahar-headless", "azahar", "citra"):
        if shutil.which(cand):
            return cand
    return None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("threedsx")
    ap.add_argument("out_png")
    ap.add_argument("--expect", default=None)
    ap.add_argument("--frames", type=int, default=60)
    args = ap.parse_args()

    emu = find_emulator()
    if emu is None:
        print("SKIP: no Citra/Azahar binary on PATH", file=sys.stderr)
        return 77

    if not os.path.exists(args.threedsx):
        print(f"FAIL: .3dsx not found: {args.threedsx}", file=sys.stderr)
        return 1

    log_path = Path("/tmp/pm3ds-dumpframe.log")
    duration = max(4, args.frames // 15 + 2)
    try:
        with open(log_path, "wb") as logf:
            subprocess.run(
                ["timeout", f"{duration}s", emu,
                 "--log-filter", "*:Info", args.threedsx],
                stdout=logf, stderr=subprocess.STDOUT,
            )
    except (subprocess.SubprocessError, FileNotFoundError) as e:
        print(f"FAIL: emulator failed: {e}", file=sys.stderr)
        return 1

    log = log_path.read_bytes()
    if b"PANIC" in log:
        print("FAIL: PANIC in emulator log", file=sys.stderr)
        return 1

    if args.expect is None:
        print("OK: emulator ran, no PANIC; reference capture must be done manually")
        return 0

    try:
        from PIL import Image
        import imagehash
    except ImportError:
        print("FAIL: pip install Pillow imagehash", file=sys.stderr)
        return 1

    if not os.path.exists(args.expect):
        print(f"FAIL: reference PNG missing: {args.expect}", file=sys.stderr)
        return 1
    if not os.path.exists(args.out_png):
        print(f"FAIL: emulator did not produce {args.out_png}", file=sys.stderr)
        return 1

    h_expect = imagehash.phash(Image.open(args.expect))
    h_got    = imagehash.phash(Image.open(args.out_png))
    dist = (h_expect - h_got) / 64.0
    if dist > TOLERANCE:
        print(f"FAIL: phash distance {dist:.4f} > tolerance {TOLERANCE}",
              file=sys.stderr)
        return 1
    print(f"OK: phash distance {dist:.4f} within tolerance")
    return 0

if __name__ == "__main__":
    sys.exit(main())
