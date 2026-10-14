#!/usr/bin/env python3
"""Snapshot resolved/unresolved decomp symbol counts in the linked ELF.

Usage:
  check_symbol_count.py            # print snapshot
  check_symbol_count.py --baseline # write snapshot to .m4_symbol_baseline.txt
  check_symbol_count.py --check    # compare against baseline; fail if
                                   # unresolved went UP since last wave
                                   # (signals a SOURCES regression)
"""
import argparse
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
ELF  = REPO / "port" / "3ds" / "PaperMario3DS.elf"
SNAP = REPO / "port" / "3ds" / ".m4_symbol_baseline.txt"

NM   = "arm-none-eabi-nm"


def snapshot():
    if not ELF.exists():
        print(f"FAIL: {ELF} missing -- build first", file=sys.stderr)
        sys.exit(1)
    try:
        out = subprocess.check_output([NM, str(ELF)], text=True)
    except FileNotFoundError:
        # Fall back to Docker if arm-none-eabi-nm isn't on PATH.
        out = subprocess.check_output([
            "docker", "run", "--rm", "-v", f"{REPO}:/project",
            "papermario-3ds-builder",
            "arm-none-eabi-nm", f"/project/port/3ds/PaperMario3DS.elf",
        ], text=True)
    resolved = 0
    unresolved = 0
    for line in out.splitlines():
        parts = line.split()
        if not parts:
            continue
        # Undefined: " " <space> "U" <space> name      -> parts = ["U", name]
        # Defined:   "<hexaddr>" "<type>" "<name>"     -> parts = [addr, type, name]
        if len(parts) == 2 and parts[0] == "U":
            unresolved += 1
        elif len(parts) >= 3:
            resolved += 1
    return resolved, unresolved


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", action="store_true",
                    help="Write current snapshot to .m4_symbol_baseline.txt")
    ap.add_argument("--check", action="store_true",
                    help="Compare against baseline; fail if unresolved went UP")
    args = ap.parse_args()

    r, u = snapshot()
    print(f"resolved: {r}  unresolved: {u}")

    if args.baseline:
        SNAP.write_text(f"{r} {u}\n")
        print(f"OK: wrote baseline {SNAP.relative_to(REPO)}")
        return 0
    if args.check:
        if not SNAP.exists():
            print(f"FAIL: no baseline at {SNAP}", file=sys.stderr)
            return 1
        br, bu = map(int, SNAP.read_text().split())
        if u > bu:
            print(f"FAIL: unresolved went UP ({bu} -> {u})", file=sys.stderr)
            return 1
        print(f"OK: unresolved {bu} -> {u}, resolved {br} -> {r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
