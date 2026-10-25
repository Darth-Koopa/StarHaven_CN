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

for name in ("ui_native_glyph_text_fit", "ui_native_glyph_text_center_fit"):
    body = function_body(name)
    if "text_width(" not in body or "ui_native_glyph_text(" not in body:
        raise SystemExit(f"{name} must measure text and draw through the native glyph renderer")
    if "float render_scale = scale;" not in body or "scale * 0.625f" in body:
        raise SystemExit(f"{name} must use native game-font scale without TTF conversion")

if "#define UI_NATIVE_GLYPH_INK_SCALE 1.08f" not in source:
    raise SystemExit("expected a slight 1.08x glyph-ink enlargement without changing advances")
for label in (
    "ui_native_glyph_text_center_fit(cx, y0 + 22, tabs[i].name, label_scale",
    'ui_native_glyph_text_center_fit(vx0 + 58, vy1 - 20, "Drag to look", 0.7f',
    "ui_native_glyph_text_fit(x0 + 7, y0 + 1, s_log.name[i], 0.8f",
    "ui_native_glyph_text_fit(x, y0 + 1, a->name, 0.75f",
    "ui_native_glyph_text_fit(CX0 + 7, y + 1, label, 0.82f",
    "ui_native_glyph_text_center_fit((bx0 + bx1) / 2, (by0 + by1) / 2 - 6.5f, value, 0.75f",
):
    if label not in source:
        raise SystemExit(f"expected legibility call site missing: {label}")

print("Native iQue glyph renderer and scale-preserving ink enlargement checks passed")
