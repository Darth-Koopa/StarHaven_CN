#!/usr/bin/env python3
"""Host-side regression checks for the proportional iQue small-font generator."""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "port/3ds/tools"))
import generate_small_text_font as small_font
from PIL import ImageFont

font_path = ROOT / "port/3ds/fonts/FusionPixel-10px-Prop-zh-Hans.ttf"
if not font_path.is_file():
    raise SystemExit(f"selected proportional TTF missing: {font_path}")
font = ImageFont.truetype(str(font_path), 10)
_, _, _, advance_i, _ = small_font.render_glyph(font, "i", 10)
_, _, _, advance_w, _ = small_font.render_glyph(font, "W", 10)
if advance_i >= advance_w:
    raise SystemExit(f"font metrics are not proportional: i={advance_i}, W={advance_w}")
for ch in "设置按键调试状态地图同伴扫描收集拖动查看速率栗子区域":
    pixels, width, height, advance, top = small_font.render_glyph(font, ch, 10)
    if not any(pixels) or width <= 0 or height <= 0 or advance <= 0:
        raise SystemExit(f"missing/blank glyph metrics for {ch!r}")

canonical_csv = ROOT / "port/3ds/localization/ui_translations_zh_CN.csv"
# Every translated glyph must fit the generated texture atlas. The menu merge
# added recipe/cheat labels and grew the subset beyond the former 512-slot atlas.
import csv
with canonical_csv.open("r", encoding="utf-8-sig", newline="") as stream:
    translation_rows = list(csv.DictReader(stream))
glyph_map = small_font.load_glyph_map()
needed_glyphs = {
    glyph_map[ch]
    for row in translation_rows
    for ch in row["proposed_simplified_chinese"]
    if ord(ch) > 127 and ch in glyph_map
}
needed_slots = small_font.CJK_FIRST_SLOT + len(needed_glyphs)
if needed_slots > small_font.SLOTS:
    raise SystemExit(
        f"current translation rows exceed the font atlas capacity: "
        f"need {needed_slots} slots, have {small_font.SLOTS}"
    )
review_csv = ROOT / "port/3ds/docs/ique-bottom-screen-translation-review.csv"
if not canonical_csv.is_file() or review_csv.exists():
    raise SystemExit("ui_translations_zh_CN.csv must be the only CSV translation source")
renderer = (ROOT / "port/3ds/source/platform/bottom_screen.c").read_text(encoding="utf-8")
if "C3D_TexSetFilter(&s_ui_small, GPU_LINEAR, GPU_LINEAR);" not in renderer:
    raise SystemExit("custom small-font atlas must retain smooth linear filtering, not nearest-neighbor")
if "s_pm3ds_small_font_advance[slot]" not in renderer:
    raise SystemExit("runtime renderer does not consume proportional advance metrics")
print("Proportional small-font metrics and smooth sampling checks passed")
