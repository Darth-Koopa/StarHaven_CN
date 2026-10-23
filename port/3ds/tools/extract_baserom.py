#!/usr/bin/env python3
"""Validate and stage an original Paper Mario ROM for the 3DS port.

Usage:
    extract_baserom.py <input.z64> <output_dir> [us|ique]

When the optional version is omitted, the SHA-1 identifies the supported base
ROM. The copied file is called baserom.z64; patched translations are rejected
because the runtime offsets and ROM-data restoration table are version-specific.
"""

import hashlib
import shutil
import sys
from pathlib import Path

ROM_SHA1 = {
    "us": "3837f44cda784b466c9a2d99df70d77c322b97a0",
    "ique": "5c724685085eba796537573dd6f84aaddedc8582",
}


def main() -> int:
    if len(sys.argv) not in (3, 4):
        print("usage: extract_baserom.py <in.z64> <out_dir> [us|ique]", file=sys.stderr)
        return 2
    src = Path(sys.argv[1])
    out = Path(sys.argv[2])
    if not src.is_file():
        print(f"FAIL: ROM not found: {src}", file=sys.stderr)
        return 1
    data = src.read_bytes()
    sha = hashlib.sha1(data).hexdigest()
    found = next((version for version, expected in ROM_SHA1.items() if expected == sha), None)
    requested = sys.argv[3].lower() if len(sys.argv) == 4 else None
    if requested is not None and requested not in ROM_SHA1:
        print("FAIL: version must be 'us' or 'ique'", file=sys.stderr)
        return 2
    if found is None or (requested is not None and requested != found):
        expected = ROM_SHA1.get(requested) if requested else None
        detail = f" expected {expected}" if expected else ""
        print(f"FAIL: unrecognized/mismatched ROM SHA-1 {sha}.{detail}", file=sys.stderr)
        print("Supported originals:", file=sys.stderr)
        for version, digest in ROM_SHA1.items():
            print(f"  {version}: {digest}", file=sys.stderr)
        return 1

    out.mkdir(parents=True, exist_ok=True)
    dst = out / "baserom.z64"
    shutil.copyfile(src, dst)
    print(f"OK: staged original {found} ROM at {dst} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
