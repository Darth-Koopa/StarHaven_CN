#!/usr/bin/env python3
"""
Walks the linked ELF and fails if any libultra-like symbol is still
undefined. Used by CI right after the link step.

Usage:
    check_undef_syms.py <path-to-elf>

Exits 0 on clean, 1 on any unresolved symbol matching the libultra prefixes.
"""

import re
import subprocess
import sys

PREFIX_RE = re.compile(r"^(os|gSP|gDP|nu|gu|alSeq|alSnd|n_al|__os)")

def main(argv):
    if len(argv) != 2:
        print("usage: check_undef_syms.py <elf>", file=sys.stderr)
        return 2
    elf = argv[1]
    try:
        nm = subprocess.run(
            ["arm-none-eabi-nm", "--undefined-only", elf],
            capture_output=True, text=True, check=True,
        )
    except FileNotFoundError:
        nm = subprocess.run(
            ["nm", "--undefined-only", elf],
            capture_output=True, text=True, check=True,
        )

    bad = []
    for line in nm.stdout.splitlines():
        parts = line.strip().split()
        if not parts:
            continue
        sym = parts[-1]
        if PREFIX_RE.match(sym):
            bad.append(sym)

    if bad:
        print(f"FAIL: {len(bad)} unresolved libultra-like symbols:", file=sys.stderr)
        for s in bad:
            print(f"  {s}", file=sys.stderr)
        return 1
    print(f"OK: no unresolved libultra symbols in {elf}")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
