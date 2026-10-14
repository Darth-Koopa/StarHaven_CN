#!/usr/bin/env python3
"""
Regression test for the produced romfs manifest.

Pure host test — does not require a baserom. Two modes:
  - If port/3ds/romfs/manifest.txt exists, verify each committed
    signature line is present in it with matching size + (if given) sha1.
  - If port/3ds/romfs/manifest.txt does NOT exist, only verify the
    signature file parses cleanly.
"""

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
SIGNATURE = REPO_ROOT / "port" / "3ds" / "tools" / "manifest_signature.txt"
MANIFEST  = REPO_ROOT / "port" / "3ds" / "romfs" / "manifest.txt"


def parse_signature(text):
    out = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) == 3:
            rel, lo, hi = parts
            out.append((rel, int(lo), int(hi), None))
        elif len(parts) == 4:
            rel, lo, hi, sha = parts
            out.append((rel, int(lo), int(hi), sha))
        else:
            raise ValueError(f"bad signature line: {raw!r}")
    return out


def parse_manifest(text):
    out = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        # path  bytes  sha1
        m = re.match(r"(\S+)\s+(\d+)\s+([0-9a-f]{40})", line)
        if not m:
            raise ValueError(f"bad manifest line: {raw!r}")
        out[m.group(1)] = (int(m.group(2)), m.group(3))
    return out


def main() -> int:
    if not SIGNATURE.exists():
        print("FAIL: signature file missing", file=sys.stderr)
        return 1
    signature = parse_signature(SIGNATURE.read_text())
    if not signature:
        print("FAIL: signature file is empty", file=sys.stderr)
        return 1

    if not MANIFEST.exists():
        print(f"SKIP: {MANIFEST} not present — pack_romfs.py has not run yet")
        return 0

    manifest = parse_manifest(MANIFEST.read_text())

    fails = []
    for rel, lo, hi, sha in signature:
        if rel.endswith("/"):
            # directory marker — just verify some file under it exists.
            if not any(k.startswith(rel) for k in manifest):
                fails.append(f"no files under {rel}")
            continue
        if rel not in manifest:
            fails.append(f"missing {rel}")
            continue
        size, got_sha = manifest[rel]
        if not (lo <= size <= hi):
            fails.append(f"{rel}: size {size} outside [{lo}, {hi}]")
        if sha and sha != got_sha:
            fails.append(f"{rel}: sha1 {got_sha} != {sha}")

    if fails:
        for f in fails:
            print(f"FAIL: {f}", file=sys.stderr)
        return 1
    print(f"OK: {len(signature)} signature entries match manifest")
    return 0


if __name__ == "__main__":
    sys.exit(main())
