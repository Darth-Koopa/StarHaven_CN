#!/usr/bin/env python3
"""Generate a compact proportional bitmap font atlas for the iQue 3DS UI.

The readable UTF-8 translation CSV remains the source of localized strings.
Non-ASCII translated characters are encoded as private-use iQue glyph tokens;
this generator maps those tokens to a compact, proportional TTF-derived bitmap
font. Pillow is needed only on the host at build time; the 3DS runtime does not
load or parse a TTF.
"""
from __future__ import annotations

import argparse
import csv
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_FONT = ROOT / "port/3ds/fonts/FusionPixel-10px-Prop-zh-Hans.ttf"
DEFAULT_CSV = ROOT / "port/3ds/localization/ui_translations_zh_CN.csv"
DEFAULT_OUT = ROOT / "port/3ds/gen/ui_small_font.h"
GLYPH_COUNT = 0x4F000 // 0x80
CELL = 16
COLS = 32
ROWS = 32
SLOTS = COLS * ROWS
ASCII_FIRST = 32
ASCII_LAST = 126
CJK_FIRST_SLOT = 128


def load_glyph_map() -> dict[str, int]:
    sys.path.insert(0, str(ROOT / "port/3ds/tools"))
    import generate_bottom_screen_translations as translations
    return translations.load_ique_character_map()


def c_array(name: str, ctype: str, values: list[int], per_line: int = 16) -> list[str]:
    lines = [f"static const {ctype} {name}[{len(values)}] = {{"]
    for start in range(0, len(values), per_line):
        chunk = values[start:start + per_line]
        lines.append("    " + ", ".join(str(v) for v in chunk) + ",")
    lines.append("};")
    return lines


def render_glyph(font: ImageFont.FreeTypeFont, ch: str, size: int) -> tuple[list[int], int, int, int, int]:
    """Render one glyph into a 16x16 cell and return proportional metrics.

    The glyphs share a baseline rather than being vertically centered one by
    one, so lowercase descenders and punctuation retain normal typography.
    Ink bounds are cropped at draw time; advance comes from the TTF metrics.
    """
    image = Image.new("L", (CELL, CELL), 0)
    draw = ImageDraw.Draw(image)
    left, top, right, bottom = draw.textbbox((0, 0), ch, font=font, anchor="ls")
    width = max(0, min(CELL, right - left))
    height = max(0, min(CELL, bottom - top))
    baseline_y = size - 1
    ink_top = max(0, min(CELL - height, baseline_y + top)) if height else baseline_y
    if width and height:
        cell_x = (CELL - width) // 2
        draw.text((cell_x - left, baseline_y), ch, font=font, fill=255, anchor="ls")
    try:
        advance = int(math.ceil(float(font.getlength(ch))))
    except (AttributeError, TypeError, ValueError):
        advance = width
    if ch == " ":
        width = height = 0
        ink_top = baseline_y
    advance = max(width, min(CELL, max(2, advance)))
    pixels = [image.getpixel((x, y)) for y in range(CELL) for x in range(CELL)]
    return pixels, width, height, advance, ink_top


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=Path, default=DEFAULT_FONT)
    parser.add_argument("--csv", type=Path, default=DEFAULT_CSV)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--size", type=int, choices=(10, 12), default=10)
    args = parser.parse_args()
    if not args.font.is_file():
        print(f"font file not found: {args.font}", file=sys.stderr)
        return 1

    with args.csv.open("r", encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        print(f"translation CSV is empty: {args.csv}", file=sys.stderr)
        return 1

    char_to_glyph = load_glyph_map()
    target_chars = {ch for row in rows for ch in row["proposed_simplified_chinese"]}
    cjk_glyphs: dict[int, str] = {}
    for ch in target_chars:
        if ord(ch) <= 127:
            continue
        if ch not in char_to_glyph:
            print(f"missing iQue glyph mapping for {ch!r} U+{ord(ch):04X}", file=sys.stderr)
            return 1
        glyph = char_to_glyph[ch]
        previous = cjk_glyphs.setdefault(glyph, ch)
        if previous != ch:
            print(f"ambiguous native glyph index {glyph}: {previous!r} vs {ch!r}", file=sys.stderr)
            return 1

    glyph_count = len(cjk_glyphs)
    slot_count = CJK_FIRST_SLOT + glyph_count
    if slot_count > SLOTS:
        print(f"too many glyphs: {slot_count} slots exceed atlas capacity {SLOTS}", file=sys.stderr)
        return 1

    try:
        font = ImageFont.truetype(str(args.font), args.size)
    except Exception as exc:
        print(f"cannot open TTF {args.font}: {exc}", file=sys.stderr)
        return 1

    pixels: list[list[int]] = [[0] * (CELL * CELL) for _ in range(slot_count)]
    widths = [0] * slot_count
    heights = [0] * slot_count
    advances = [args.size] * slot_count
    tops = [args.size - 1] * slot_count

    for cp in range(ASCII_FIRST, ASCII_LAST + 1):
        glyph_pixels, width, height, advance, top = render_glyph(font, chr(cp), args.size)
        pixels[cp] = glyph_pixels
        widths[cp], heights[cp], advances[cp], tops[cp] = width, height, advance, top

    glyph_slots = [0xFFFF] * GLYPH_COUNT
    for offset, glyph in enumerate(sorted(cjk_glyphs)):
        slot = CJK_FIRST_SLOT + offset
        glyph_slots[glyph] = slot
        ch = cjk_glyphs[glyph]
        glyph_pixels, width, height, advance, top = render_glyph(font, ch, args.size)
        pixels[slot] = glyph_pixels
        widths[slot], heights[slot], advances[slot], tops[slot] = width, height, advance, top

    out = [
        "/* Generated by tools/generate_small_text_font.py; do not edit by hand. */",
        "/* Fusion Pixel Font proportional build (SIL OFL 1.1); see fonts/LICENSE-OFL.txt. */",
        "#ifndef PM3DS_UI_SMALL_FONT_H",
        "#define PM3DS_UI_SMALL_FONT_H",
        f"#define PM3DS_SMALL_FONT_SIZE {args.size}",
        f"#define PM3DS_SMALL_FONT_CELL {CELL}",
        f"#define PM3DS_SMALL_FONT_ATLAS_COLS {COLS}",
        f"#define PM3DS_SMALL_FONT_ATLAS_ROWS {ROWS}",
        f"#define PM3DS_SMALL_FONT_ATLAS_W {CELL * COLS}",
        f"#define PM3DS_SMALL_FONT_ATLAS_H {CELL * ROWS}",
        f"#define PM3DS_SMALL_FONT_SLOT_COUNT {slot_count}",
        f"#define PM3DS_SMALL_FONT_GLYPH_PIXELS {CELL * CELL}",
        f"#define PM3DS_SMALL_FONT_IQUE_GLYPH_COUNT {GLYPH_COUNT}",
        "",
        f"static const unsigned char s_pm3ds_small_font_pixels[{slot_count}][{CELL * CELL}] = {{",
    ]
    for row in pixels:
        out.append("    {" + ", ".join(str(v) for v in row) + "},")
    out.append("};")
    out.extend(c_array("s_pm3ds_small_font_width", "unsigned char", widths))
    out.extend(c_array("s_pm3ds_small_font_height", "unsigned char", heights))
    out.extend(c_array("s_pm3ds_small_font_top", "unsigned char", tops))
    out.extend(c_array("s_pm3ds_small_font_advance", "unsigned char", advances))
    out.extend(c_array("s_pm3ds_ique_small_font_slots", "unsigned short", glyph_slots))
    out.extend(["#endif /* PM3DS_UI_SMALL_FONT_H */", ""])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(out), encoding="utf-8")
    print(f"Generated {args.out}: {glyph_count} CJK/native glyphs + printable ASCII; "
          f"{slot_count} slots, {args.size}px nominal height, proportional advances.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
