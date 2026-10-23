#!/usr/bin/env python3
"""Verify that the iQue raw font assets exactly match the known ROM ranges.

Run after extract_assets.py and gen_include_assets.py. This is a data-integrity
check: it does not depend on an emulator or distribute any ROM data.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
ROM_PATH = ROOT / "ver/ique/baserom.z64"
ASSET_ROOT = ROOT / "port/3ds/build_assets_ique/raw"

RANGES = {
    "charset/standard.dat": (0x10E120, 0x15D120),
    "charset/standard/palette.dat": (0x15D120, 0x15D620),
    "charset/title.dat": (0x15D620, 0x15E580),
    "charset/subtitle.dat": (0x15E580, 0x15F108),
    "charset/subtitle/palette.dat": (0x15F120, 0x15F130),
    "charset/title/palette.dat": (0x15F130, 0x15F140),
}


def fail(message: str) -> None:
    print(f"FAIL: iQue font assets: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    if not ROM_PATH.is_file():
        fail(f"missing staged ROM: {ROM_PATH}")
    rom = ROM_PATH.read_bytes()
    if len(rom) < 0x2800000 or rom[:4] != b"\x80\x37\x12\x40":
        fail("baserom is not a canonical .z64 iQue ROM")
    for rel, (start, end) in RANGES.items():
        path = ASSET_ROOT / rel
        if not path.is_file():
            fail(f"missing asset {path}")
        actual = path.read_bytes()
        expected = rom[start:end]
        if len(actual) != end - start:
            fail(f"{rel}: size 0x{len(actual):X}, expected 0x{end-start:X}")
        if actual != expected:
            fail(f"{rel}: bytes differ from ROM range 0x{start:X}..0x{end:X}")
    standard = (ASSET_ROOT / "charset/standard.dat").read_bytes()
    # Basic Latin 'A' (font code 0x21), lowercase 'a' (0x41), and first Chinese
    # glyph (0xA6) must have raster data; each CI4 16x16 glyph is 0x80 bytes.
    for idx, label in ((0x21, "Latin A"), (0x41, "Latin a"), (0xA6, "first Chinese glyph")):
        glyph = standard[idx * 0x80:(idx + 1) * 0x80]
        if len(glyph) != 0x80 or not any(glyph):
            fail(f"{label} glyph 0x{idx:X} is empty or out of range")
    print("OK: iQue standard/title/subtitle font rasters and palettes exactly match the verified ROM ranges.")


if __name__ == "__main__":
    main()
