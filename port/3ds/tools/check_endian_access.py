#!/usr/bin/env python3
"""
Scans port/3ds/source/ for direct casts of byte-array baked blobs
(*.bin.inc.c-derived data) to wide-integer pointers without routing
through pm_endian.h's BE_*/LE_READ_BE_U* helpers.

This is a conservative regex lint — false positives are OK (engineer
adds an explicit BE_OK comment to silence). False negatives matter
more: we want to catch unguarded casts before they corrupt save data.
"""

import re
import sys
from pathlib import Path

BAD_RE = re.compile(
    r"\([a-zA-Z_][a-zA-Z0-9_]*\s*\*\s*\)\s*[a-zA-Z_][a-zA-Z0-9_]*\.(bin|inc|inc_c)"
    r"|\(u?int(16|32|64)_t\s*\*\s*\)"
)

ALLOW_COMMENT = "/* BE_OK */"

ROOT = Path(__file__).resolve().parent.parent / "source"

def main():
    violations = []
    for p in ROOT.rglob("*.c"):
        for i, line in enumerate(p.read_text(errors="replace").splitlines(), 1):
            if "(u" not in line and "(int" not in line:
                continue
            if not BAD_RE.search(line):
                continue
            if ALLOW_COMMENT in line:
                continue
            violations.append(f"{p}:{i}: {line.strip()}")
    if violations:
        print("Endian access violations (use BE_*/LE_READ_BE_U*):", file=sys.stderr)
        for v in violations:
            print("  " + v, file=sys.stderr)
        return 1
    print("OK: no unguarded baked-data casts")
    return 0

if __name__ == "__main__":
    sys.exit(main())
