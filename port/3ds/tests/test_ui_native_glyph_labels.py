#!/usr/bin/env python3
"""Regression test: small bottom-screen labels keep the native iQue glyph atlas."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / "port/3ds/source/platform/bottom_screen.c").read_text(encoding="utf-8")

def function_body(name: str) -> str:
    match = re.search(rf"static\s+(?:float|void)\s+{re.escape(name)}\s*\([^;]*?\)\s*\{{", source)
    if not match:
        raise SystemExit(f"function not found: {name}")
    start = match.end()
    depth = 1
    i = start
    while i < len(source) and depth:
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
        i += 1
    if depth:
        raise SystemExit(f"unclosed function body: {name}")
    return source[start:i - 1]

native_draw = function_body("ui_native_glyph_text")
if "quad2(&s_font" not in native_draw or "UI_NATIVE_GLYPH_INK_SCALE" not in native_draw:
    raise SystemExit("label renderer must draw enlarged glyphs from the original game-font texture")
if "x += unit_width * scale;" not in native_draw:
    raise SystemExit("glyph enlargement must preserve the original character advances")
if "ui_small_text_draw_raw" in native_draw or "s_ui_small" in native_draw:
    raise SystemExit("label renderer must not use the Fusion Pixel small-font atlas")

for name in ("ui_bitmap_text", "ui_bitmap_text_center"):
    body = function_body(name)
    if "text_width(" not in body or "ui_native_glyph_text(" not in body:
        raise SystemExit(f"{name} must measure text and draw through the native glyph renderer")
    if "scale * 0.625f" not in body:
        raise SystemExit(f"{name} must preserve the previous nominal text-scale mapping")

if "#define UI_NATIVE_GLYPH_INK_SCALE 1.08f" not in source:
    raise SystemExit("expected a slight 1.08x glyph-ink enlargement without changing advances")
for label in (
    "ui_bitmap_text_center(cx, y0 + 22, tabs[i].name",
    'ui_bitmap_text_center(vx0 + 58, vy1 - 20, "Drag to look"',
    "ui_bitmap_text(x0 + 7, y0 + 2, s_log.name[i]",
    "ui_bitmap_text(x, y0 + 2, a->name",
    "ui_bitmap_text(CX0 + 7, y + 3, label",
    "ui_bitmap_text_center((bx0 + bx1) / 2",
):
    if label not in source:
        raise SystemExit(f"expected legibility call site missing: {label}")

print("Native iQue glyph renderer and larger draw-scale checks passed")
