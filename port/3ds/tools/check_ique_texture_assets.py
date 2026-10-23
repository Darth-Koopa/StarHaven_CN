#!/usr/bin/env python3
"""Validate iQue icon offsets and embedded Peach Letter bytes before 3DS build.

The official iQue ROM uses the same packed icon format described by
 tools/splat_ext/icon.yaml. This check independently walks that table, verifies
 its end against the next Splat segment, checks representative gameplay icons,
 and asserts the .bin blobs embedded by the 3DS build are exact ROM slices.
"""
from __future__ import annotations

import hashlib
import re
import sys
import zlib
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[3]
EXPECTED_SHA1 = "5c724685085eba796537573dd6f84aaddedc8582"
ROM_SIZE = 0x2800000
ICON_BASE = 0x215390
ICON_END = 0x247230
LETTER_IMAGE_OFFSET = 0x15F140
LETTER_IMAGE_SIZE = 150 * 105  # CI8
LETTER_PALETTE_OFFSET = 0x162EC8
LETTER_PALETTE_SIZE = 0x200  # 256 RGBA5551 entries


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def parse_icon_layout() -> tuple[dict[str, dict[str, int | None]], int]:
    try:
        rows = yaml.safe_load((ROOT / "tools/splat_ext/icon.yaml").read_text())
    except Exception as exc:
        fail(f"cannot parse tools/splat_ext/icon.yaml: {exc}")
    if not isinstance(rows, list):
        fail("tools/splat_ext/icon.yaml is not a list")
    pos = 0
    layout: dict[str, dict[str, int | None]] = {}
    for idx, row in enumerate(rows):
        if not isinstance(row, list) or len(row) != 4:
            fail(f"unexpected icon entry at index {idx}: {row!r}")
        fmt, name, width, height = row
        if not isinstance(name, str) or not isinstance(width, int) or not isinstance(height, int):
            fail(f"invalid icon metadata at index {idx}: {row!r}")
        if width <= 0 or height <= 0:
            fail(f"invalid dimensions for icon {name}: {width}x{height}")
        if fmt in ("solo", "pair"):
            size = width * height // 2
            raster = pos
            pos += size
            palette = pos
            pos += 32
            disabled_palette = None
            if fmt == "pair":
                disabled_palette = pos
                pos += 32
            layout[name] = {
                "raster": raster,
                "palette": palette,
                "disabled_palette": disabled_palette,
                "raster_size": size,
            }
        elif fmt == "rgba16":
            size = width * height * 2
            raster = pos
            pos += size
            layout[name] = {
                "raster": raster,
                "palette": None,
                "disabled_palette": None,
                "raster_size": size,
            }
        else:
            fail(f"unsupported icon format {fmt!r} for {name}")
    return layout, pos


def expect_macro(header: str, symbol: str, expected: int) -> None:
    match = re.search(rf"(?m)^\s*#define\s+{re.escape(symbol)}\s+(0x[0-9A-Fa-f]+)\b", header)
    if not match:
        fail(f"icon offset header is missing {symbol}")
    got = int(match.group(1), 16)
    if got != expected:
        fail(f"{symbol} is 0x{got:X}, but icon.yaml requires 0x{expected:X}")


def check_blob(path: Path, expected: bytes, description: str) -> None:
    if not path.is_file():
        fail(f"missing extracted embedded texture {path.relative_to(ROOT)}")
    actual = path.read_bytes()
    if len(actual) != len(expected):
        fail(f"{description} length is {len(actual)}, expected {len(expected)}")
    if actual != expected:
        fail(f"{description} differs from the official iQue ROM bytes")
    if not any(actual):
        fail(f"{description} is all zero bytes")


def main() -> int:
    if len(sys.argv) != 2:
        fail("usage: check_ique_texture_assets.py /path/to/original-iQue.z64")
    rom_path = Path(sys.argv[1])
    if not rom_path.is_file():
        fail(f"iQue ROM is missing: {rom_path}")
    rom = rom_path.read_bytes()
    sha1 = hashlib.sha1(rom).hexdigest()
    if len(rom) != ROM_SIZE or sha1 != EXPECTED_SHA1:
        fail(f"expected original 40 MiB iQue ROM; got size=0x{len(rom):X}, SHA-1={sha1}")
    crc = zlib.crc32(rom) & 0xFFFFFFFF
    if crc != 0x18C02AD5:
        fail(f"iQue ROM CRC32 changed: got {crc:08X}, expected 18C02AD5")

    icon_layout, span = parse_icon_layout()
    if span != ICON_END - ICON_BASE:
        fail(f"icon.yaml spans 0x{span:X} bytes, expected 0x{ICON_END - ICON_BASE:X}")
    if rom[ICON_BASE:ICON_BASE + span] == bytes(span):
        fail("iQue icon block is unexpectedly all zeros")
    if ICON_BASE + span != ICON_END:
        fail(f"icon block end 0x{ICON_BASE + span:X} does not match the logos segment at 0x{ICON_END:X}")

    # Representative assets cover key items, equipment settings/debug symbols,
    # badges, battle/animation sprites and the star-piece/star-point counters.
    representative = (
        "key/Gift",
        "key/calculator",
        "key/screwdriver",
        "badge/SpinSmash",
        "anim/star_piece_0",
        "anim/sp_0",
        "hud/star_point",
    )
    for name in representative:
        if name not in icon_layout:
            fail(f"representative icon {name!r} is absent from icon.yaml")
        entry = icon_layout[name]
        raster_size = int(entry["raster_size"])
        raster_start = ICON_BASE + int(entry["raster"])
        raster = rom[raster_start:raster_start + raster_size]
        if len(raster) != raster_size or not any(raster):
            fail(f"icon raster {name} is missing or all zeros at ROM 0x{raster_start:X}")
        if entry["palette"] is not None:
            palette_start = ICON_BASE + int(entry["palette"])
            palette = rom[palette_start:palette_start + 32]
            if len(palette) != 32 or not any(palette):
                fail(f"icon palette {name} is missing or all zeros at ROM 0x{palette_start:X}")

    # Verify the generated relative-offset header agrees with this walk of
    # icon.yaml. The version-generated header is preferred; local checkouts may
    # use the tracked shared header before the code-generation step has run.
    header_candidates = (
        ROOT / "port/3ds/gen/ique/icon_offsets.h",
        ROOT / "ver/ique/build/include/icon_offsets.h",
        ROOT / "include/icon_offsets.h",
    )
    header_path = next((p for p in header_candidates if p.is_file()), None)
    if header_path is None:
        fail("no generated/tracked icon_offsets.h exists to validate")
    header = header_path.read_text(errors="replace")
    # Verify every generated offset, not just examples. An icon can appear
    # correctly in one screen while a stale/mismatched macro breaks other
    # items, badges, battle moves, or HUD counters.
    for name, entry in icon_layout.items():
        symbol = "ICON_" + re.sub(r"\W", "_", name)
        raster_offset = int(entry["raster"])
        expect_macro(header, symbol + "_raster", raster_offset)
        if entry["palette"] is not None:
            expect_macro(header, symbol + "_palette", int(entry["palette"]))
        if entry["disabled_palette"] is not None:
            expect_macro(header, symbol + "_disabled_raster", raster_offset)
            expect_macro(header, symbol + "_disabled_palette", int(entry["disabled_palette"]))

    asset_root = ROOT / "port/3ds/build_assets_ique"
    check_blob(
        asset_root / "charset/peach_letter.png.bin",
        rom[LETTER_IMAGE_OFFSET:LETTER_IMAGE_OFFSET + LETTER_IMAGE_SIZE],
        "Princess Peach's Letter CI8 raster",
    )
    check_blob(
        asset_root / "charset/peach_letter.pal.bin",
        rom[LETTER_PALETTE_OFFSET:LETTER_PALETTE_OFFSET + LETTER_PALETTE_SIZE],
        "Princess Peach's Letter CI8 palette",
    )

    print(
        "iQue texture assets passed: "
        f"{len(icon_layout)} icon entries span 0x{span:X} bytes; "
        f"Peach Letter raster/palette match ROM SHA-1 {sha1[:8]}…; "
        f"checked {header_path.relative_to(ROOT)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
