#!/usr/bin/env python3
"""
Pack the decomp's extracted assets into the 3DS romfs layout.

Version-aware: detects which decomp region (us / pal / jp / ique) is
active by reading <repo>/assets/.extract.stamp (written by
extract_assets.py) and uses the matching ver/<region>/ baserom + splat
yaml. If the stamp file uses the legacy single-line format (just a
SHA1, no region), defaults to "us" for backwards compatibility.

Outputs:
  port/3ds/romfs/baserom.z64
  port/3ds/romfs/assets/<preserve splat paths>/...
  port/3ds/romfs/manifest.txt
  port/3ds/source/debug_dl_assets.h
"""

import hashlib
import os
import re
import shutil
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
ASSETS_DIR = REPO_ROOT / "assets"
STAMP      = ASSETS_DIR / ".extract.stamp"

ROMFS_DIR     = REPO_ROOT / "port" / "3ds" / "romfs"
ROMFS_BASEROM = ROMFS_DIR / "baserom.z64"
ROMFS_ASSETS  = ROMFS_DIR / "assets"
MANIFEST      = ROMFS_DIR / "manifest.txt"
PACK_STAMP    = ROMFS_DIR / ".pack.stamp"

DEBUG_DL_HEADER = REPO_ROOT / "port" / "3ds" / "source" / "debug_dl_assets.h"

ROMFS_BUDGET_BYTES = 256 * 1024 * 1024


def sha1_of(path: Path) -> str:
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_stamp():
    """Returns (region, input_sha1). Region defaults to 'us' for
    backward-compatibility with the milestone-3 single-line stamp."""
    if not STAMP.exists():
        return ("", "")
    parts = STAMP.read_text().strip().split()
    if len(parts) == 2:
        return (parts[0], parts[1])
    if len(parts) == 1:
        return ("us", parts[0])
    return ("", "")


def find_logo_segment(splat_yaml):
    """
    Pick a fixed-offset non-paletted texture from splat.yaml to drive
    the debug DL. We prefer RGBA over IA/I/CI because RGBA needs no
    LOADTLUT step. Strategy: walk every fixed-offset segment line and
    pick the first one matching a "good debug target" shortlist.
    Returns (rom_offset, width, height, fmt_code, asset_relpath).
    """
    if not splat_yaml.exists():
        return 0, 0, 0, 0, ""
    text = splat_yaml.read_text(errors="replace")

    # Capture: offset, fmt, name (may contain slashes), w, h.
    # Optional trailing palette index field is ignored.
    pattern = re.compile(
        r"-\s*\[\s*0x([0-9A-Fa-f]+)\s*,\s*"
        r"(rgba16|rgba32|i4|i8|ia4|ia8|ia16|ci4|ci8)\s*,\s*"
        r"([A-Za-z_][A-Za-z0-9_/]*)\s*,\s*"
        r"(\d+)\s*,\s*(\d+)",
        re.MULTILINE,
    )

    # Preference: RGBA32 / RGBA16 / IA16 / IA8 (no TLUT). Prefer larger
    # textures so the visual check is unmistakable.
    # fmt_code (G_IM_FMT_*) + siz_code (G_IM_SIZ_*) tracked together so
    # the SETTIMG word can reference both fields correctly.
    candidates = []
    for m in pattern.finditer(text):
        fmt = m.group(2).upper()
        if fmt in ("CI4", "CI8"):
            continue   # need TLUT; debug DL doesn't load one
        offset = int(m.group(1), 16)
        name   = m.group(3)
        width  = int(m.group(4))
        height = int(m.group(5))
        if width < 16 or height < 16:
            continue
        fmt_siz = {
            "RGBA16": (0, 2), "RGBA32": (0, 3),
            "IA4":    (3, 0), "IA8":    (3, 1), "IA16":   (3, 2),
            "I4":     (4, 0), "I8":     (4, 1),
        }.get(fmt, (0, 2))
        fmt_code, siz_code = fmt_siz
        rgba_bonus = 1_000_000 if fmt.startswith("RGBA") else 0
        score = rgba_bonus + width * height
        candidates.append((score, offset, width, height, fmt_code, siz_code, name))

    if not candidates:
        return 0, 0, 0, 0, 0, ""

    candidates.sort(reverse=True)
    _, offset, width, height, fmt_code, siz_code, name = candidates[0]
    return offset, width, height, fmt_code, siz_code, name + ".bin"


def copy_tree(src, dst):
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)


def write_manifest(lines):
    MANIFEST.write_text(
        "# file path                                     bytes      sha1\n"
        + "".join(lines)
    )


def main():
    region, input_sha = read_stamp()
    if not region:
        print("FAIL: <repo>/assets/.extract.stamp missing or unreadable — "
              "run extract_assets.py first", file=sys.stderr)
        return 1

    ver_baserom = REPO_ROOT / "ver" / region / "baserom.z64"
    splat_yaml  = REPO_ROOT / "ver" / region / "splat.yaml"
    if not ver_baserom.exists():
        print(f"FAIL: {ver_baserom} missing", file=sys.stderr)
        return 1

    region_assets = ASSETS_DIR / region
    src_assets = region_assets if region_assets.is_dir() else ASSETS_DIR

    base_sha = sha1_of(ver_baserom)

    if (PACK_STAMP.exists()
            and PACK_STAMP.read_text().strip() == base_sha
            and ROMFS_BASEROM.exists()
            and MANIFEST.exists()):
        print(f"OK: romfs/ already packed for {region.upper()} baserom "
              f"{base_sha[:8]}…")
        return 0

    ROMFS_DIR.mkdir(parents=True, exist_ok=True)

    print(f"copying baserom.z64 ({base_sha[:8]}…, "
          f"{ver_baserom.stat().st_size} bytes)…", file=sys.stderr)
    shutil.copyfile(ver_baserom, ROMFS_BASEROM)
    print(f"copying assets/ from {src_assets.relative_to(REPO_ROOT)} …",
          file=sys.stderr)
    copy_tree(src_assets, ROMFS_ASSETS)

    lines = []
    total = 0
    for root, _, files in os.walk(ROMFS_DIR):
        for f in files:
            if f.startswith("."):
                continue
            if f == "manifest.txt":
                continue
            p = Path(root) / f
            rel = p.relative_to(ROMFS_DIR).as_posix()
            size = p.stat().st_size
            sha = sha1_of(p)
            lines.append(f"{rel:50s} {size:10d}  {sha}\n")
            total += size

    if total > ROMFS_BUDGET_BYTES:
        print(f"FAIL: romfs payload {total} bytes exceeds budget "
              f"{ROMFS_BUDGET_BYTES}", file=sys.stderr)
        return 1

    lines.sort()
    write_manifest(lines)

    off, w, h, fmt, siz, relpath = find_logo_segment(splat_yaml)
    DEBUG_DL_HEADER.write_text(
        "/* Generated by port/3ds/tools/pack_romfs.py — do not edit. */\n"
        "#ifndef PM3DS_DEBUG_DL_ASSETS_H\n"
        "#define PM3DS_DEBUG_DL_ASSETS_H\n"
        f"#define PM3DS_LOGO_OFFSET 0x{off:08X}u\n"
        f"#define PM3DS_LOGO_WIDTH  {w}u\n"
        f"#define PM3DS_LOGO_HEIGHT {h}u\n"
        f"#define PM3DS_LOGO_FMT    {fmt}u  /* 0=RGBA,1=YUV,2=CI,3=IA,4=I */\n"
        f"#define PM3DS_LOGO_SIZ    {siz}u  /* 0=4b,1=8b,2=16b,3=32b */\n"
        f"#define PM3DS_LOGO_RELPATH \"{relpath}\"\n"
        f"#define PM3DS_LOGO_REGION \"{region}\"\n"
        "#endif\n"
    )

    PACK_STAMP.write_text(base_sha + "\n")
    print(f"OK: romfs/ packed for {region.upper()}, {total} bytes "
          f"across {len(lines)} files")
    if off == 0 or w == 0:
        print(f"  WARN: no title/logo segment found in {splat_yaml.relative_to(REPO_ROOT)};",
              file=sys.stderr)
        print(f"        debug_dl variant 2 will fall back to variant 1.",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
