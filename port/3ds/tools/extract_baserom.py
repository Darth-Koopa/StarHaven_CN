#!/usr/bin/env python3
"""
Validates a Paper Mario US baserom by SHA1 and stages it for romfs.

Usage:
    extract_baserom.py <input.z64> <output_dir>

Copies <input.z64> to <output_dir>/baserom.z64 after verifying its SHA1
matches the US release. Refuses to operate on non-US ROMs at this
milestone (target: -DVERSION_US=1 in the Makefile).
"""

import hashlib
import shutil
import sys
from pathlib import Path

US_SHA1 = "3837f44cda784b466c9a2d99df70d77c322b97a0"

def main():
    if len(sys.argv) != 3:
        print("usage: extract_baserom.py <in.z64> <out_dir>", file=sys.stderr)
        return 2
    src = Path(sys.argv[1])
    out = Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)

    data = src.read_bytes()
    sha = hashlib.sha1(data).hexdigest()
    if sha != US_SHA1:
        print(f"FAIL: SHA1 {sha} does not match US baserom {US_SHA1}", file=sys.stderr)
        return 1

    dst = out / "baserom.z64"
    shutil.copyfile(src, dst)
    print(f"OK: staged {dst} ({len(data)} bytes)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
