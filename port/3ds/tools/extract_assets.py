#!/usr/bin/env python3
"""
Wraps the decomp's splat pipeline.

Steps performed:
  1. Compute SHA1 of the input ROM.
  2. Match against the No-Intro / decomp-input SHA1 table to identify
     the region (us / pal / jp / ique) and any required transform.
  3. Apply the transform (PAL needs a 16 MB tail-duplicate to reach
     64 MB; other regions are pass-through).
  4. Verify the transformed SHA1 matches the decomp's expected input
     SHA1 for that region (from ver/<region>/splat.yaml).
  5. Stage as ver/<region>/baserom.z64.
  6. Run splat split ver/<region>/splat.yaml.
  7. Write a stamp file <repo>/assets/.extract.stamp containing
     "<region> <input-sha1>".

Idempotent: re-running with the same ROM is a no-op.

Usage:
    extract_assets.py <path-to-baserom>

Exit codes:
    0   assets/ tree ready (or already up to date).
    1   unrecognised SHA1, splat missing, or splat failed.
    2   missing baserom argument.
"""

import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]

# Input-side SHA1 → (region, transform-name).
# - "none" means pass through unmodified.
# - "pal-dup-tail" means append a 16 MB duplicate of bytes [0x2000000..0x3000000]
#   to reach the 64 MB the decomp's PAL splat.yaml expects.
NO_INTRO_INPUT_TABLE = {
    "3837f44cda784b466c9a2d99df70d77c322b97a0": ("us",   "none"),
    "1273275142b79f7443f55bd35679a19670adfe3a": ("pal",  "pal-dup-tail"),
    "b9cca3ff260b9ff427d981626b82f96de73586d3": ("jp",   "none"),
    "5c724685085eba796537573dd6f84aaddedc8582": ("ique", "none"),
}

# Decomp-side expected baserom SHA1 (matches the `sha1:` field in
# ver/<region>/splat.yaml). After our transform, the staged ROM must
# hash to this exact value.
DECOMP_INPUT_SHA1 = {
    "us":   "3837f44cda784b466c9a2d99df70d77c322b97a0",
    "pal":  "2111d39265a317414d359e35a7d971c4dfa5f9e1",
    "jp":   "b9cca3ff260b9ff427d981626b82f96de73586d3",
    "ique": "5c724685085eba796537573dd6f84aaddedc8582",
}


def sha1_of_bytes(data: bytes) -> str:
    return hashlib.sha1(data).hexdigest()


def sha1_of(path: Path) -> str:
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def apply_transform(src_bytes: bytes, transform: str) -> bytes:
    if transform == "none":
        return src_bytes
    if transform == "pal-dup-tail":
        # Append the 16 MB chunk at offset 0x2000000..0x3000000 to the end.
        dup_start = 0x2000000
        dup_end   = 0x3000000
        return src_bytes + src_bytes[dup_start:dup_end]
    raise ValueError(f"unknown transform: {transform}")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: extract_assets.py <baserom>", file=sys.stderr)
        return 2

    user_rom = Path(sys.argv[1])
    if not user_rom.is_file():
        print(f"FAIL: baserom not found at {user_rom}", file=sys.stderr)
        return 1

    input_sha = sha1_of(user_rom)
    if input_sha not in NO_INTRO_INPUT_TABLE:
        print(f"FAIL: unrecognised baserom SHA1 {input_sha}", file=sys.stderr)
        print("Known No-Intro hashes:", file=sys.stderr)
        for k, (region, _) in NO_INTRO_INPUT_TABLE.items():
            print(f"  {region.upper():4s}  {k}", file=sys.stderr)
        return 1

    region, transform = NO_INTRO_INPUT_TABLE[input_sha]
    print(f"input: {region.upper()} (No-Intro SHA1 {input_sha[:8]}…, transform={transform})",
          file=sys.stderr)

    ver_baserom = REPO_ROOT / "ver" / region / "baserom.z64"
    splat_yaml  = REPO_ROOT / "ver" / region / "splat.yaml"
    assets_dir  = REPO_ROOT / "assets"
    stamp       = assets_dir / ".extract.stamp"

    if not splat_yaml.exists():
        print(f"FAIL: {splat_yaml} missing — run `./configure` first",
              file=sys.stderr)
        return 1

    # Idempotence: if stamp matches the input SHA1, skip.
    if stamp.exists():
        prev = stamp.read_text().strip().split()
        if len(prev) == 2 and prev[1] == input_sha:
            print(f"OK: assets/ already extracted from {region.upper()} "
                  f"baserom {input_sha[:8]}…")
            return 0

    # Apply transform + stage as ver/<region>/baserom.z64.
    print(f"applying {transform!r} transform, staging at "
          f"ver/{region}/baserom.z64 …", file=sys.stderr)
    src_bytes = user_rom.read_bytes()
    out_bytes = apply_transform(src_bytes, transform)

    expected = DECOMP_INPUT_SHA1[region]
    got = sha1_of_bytes(out_bytes)
    if got != expected:
        print(f"FAIL: post-transform SHA1 {got} != expected {expected}",
              file=sys.stderr)
        return 1
    print(f"  post-transform SHA1 {got[:8]}… matches decomp expectation",
          file=sys.stderr)

    ver_baserom.parent.mkdir(parents=True, exist_ok=True)
    ver_baserom.write_bytes(out_bytes)

    print(f"running splat on ver/{region}/splat.yaml …", file=sys.stderr)
    # The decomp's custom Splat segment modules import helpers from
    # tools/build/common.py and sibling modules from tools/splat_ext. Those
    # paths are added by some developer shells, but not by a clean GitHub
    # Actions runner. Set them explicitly for a reproducible extraction.
    env = os.environ.copy()
    python_paths = [
        str(REPO_ROOT / "tools" / "build"),
        str(REPO_ROOT / "tools" / "splat_ext"),
    ]
    if env.get("PYTHONPATH"):
        python_paths.append(env["PYTHONPATH"])
    env["PYTHONPATH"] = os.pathsep.join(python_paths)

    cp = subprocess.run(
        [sys.executable, "-m", "splat", "split", str(splat_yaml)],
        cwd=REPO_ROOT,
        env=env,
    )
    if cp.returncode != 0:
        print("FAIL: splat exited non-zero", file=sys.stderr)
        return 1

    if not assets_dir.exists():
        print(f"FAIL: splat finished but {assets_dir} does not exist",
              file=sys.stderr)
        return 1

    stamp.write_text(f"{region} {input_sha}\n")
    print(f"OK: assets/ produced for {region.upper()}; stamp = "
          f"'{region} {input_sha[:8]}…'")
    return 0


if __name__ == "__main__":
    sys.exit(main())
