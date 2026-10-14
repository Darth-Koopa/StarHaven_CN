#!/usr/bin/env python3
"""
Placeholder for the asset-milestone byteswap pipeline. Currently a no-op
that copies its input to its output. The asset milestone (sub-project 3)
fills this in with per-blob word-size schema handling.

Usage:
    byteswap_blob.py <input.bin> <output.bin> [--word=2|4|8]
"""

import shutil
import sys

def main():
    if len(sys.argv) < 3:
        print("usage: byteswap_blob.py <in> <out> [--word=N]", file=sys.stderr)
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    shutil.copyfile(src, dst)
    print(f"NOTE: byteswap_blob.py is a no-op stub; copied {src} -> {dst}", file=sys.stderr)
    return 0

if __name__ == "__main__":
    sys.exit(main())
