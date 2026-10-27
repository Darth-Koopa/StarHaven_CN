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

native_draw = function_body("ui_native_glyph_text_ink")
if "quad2(&s_font" not in native_draw or "ink_scale" not in native_draw:
    raise SystemExit("native renderer must draw from the original game-font texture with an explicit ink scale")
if "x += unit_width * scale;" not in native_draw:
    raise SystemExit("ink scaling must preserve the original character advances")
if "ui_small_text_draw_raw" in native_draw or "s_ui_small" in native_draw:
    raise SystemExit("native label renderer must not use the Fusion Pixel small-font atlas")
if "return ui_native_glyph_text_ink(x, y, str, scale, 1.0f, c);" not in function_body("ui_native_glyph_text_original"):
    raise SystemExit("original-size renderer must not apply the 1.20x ink enlargement")

for name in ("ui_native_glyph_text_fit_ink", "ui_native_glyph_text_center_fit_ink"):
    body = function_body(name)
    if "text_width(" not in body or "ui_native_glyph_text_ink(" not in body:
        raise SystemExit(f"{name} must measure text and draw through the native glyph renderer")
    if "float render_scale = scale;" not in body or "scale * 0.625f" in body:
        raise SystemExit(f"{name} must use native game-font scale without TTF conversion")

if "#define UI_NATIVE_GLYPH_INK_SCALE 1.20f" not in source:
    raise SystemExit("default glyph-ink enlargement must remain available for UI elements that use it")
for snippet in (
    'ui_native_glyph_text_fit_original(x0 + 7, y0 + 2, s_log.name[i], 1.0f',
    'ui_native_glyph_text_fit_original(x, y0 + 1, a->name, 1.0f',
    'ui_native_glyph_text_original(x0 + 21, y0 + 2, name,',
    'ui_native_glyph_text_fit_original(CX0 + 7, y + 1, label, 0.90f',
    'ui_native_glyph_text_original(CX0 + 7, y + 2, s_cheat_names[i],',
):
    if snippet not in source:
        raise SystemExit(f"requested name/label must bypass extra 1.20x ink scale: {snippet}")

if 'fit(h->location, 188, 1.0f, 0.7f)' not in source:
    raise SystemExit("status-page current-area name must use a 1.00x base scale")
if 's_cook_all ? COL_SEL : COL_OFF, 0.75f);' not in source:
    raise SystemExit("recipe Hide new / Show all labels must be slightly larger")
if "const float label_scale = 0.62f;" not in source:
    raise SystemExit("all left-rail labels must share one nominal native-glyph scale")
if "ui_native_glyph_text_fit_original(CX0 + 7, y + 1, label, 0.90f, 150, COL_WHITE);" not in source:
    raise SystemExit("settings left labels must be shifted up one pixel while preserving scale")
if "fit(title, 160, 1.0f, 0.7f)" not in source:
    raise SystemExit("page titles must use a 1.00x native scale before fit-to-width")
if "#define LOG_TEXT_SCALE 0.85f" not in source or "#define DISH_DESC_SCALE 0.85f" not in source:
    raise SystemExit("Tattle and recipe-description bodies must use 0.85x")
if 'fit(nm, 52, 1.0f, 0.55f)' not in source:
    raise SystemExit("partner button names must use a larger 1.00x base scale")
if 's_tm.kind == PM3DS_TM_POPUP ? 0.85f : 0.9f' not in source:
    raise SystemExit("item/partner popup names must use 0.85x native scale")
if 'fit(name, x1 - x0 - 26, 1.0f, 0.5f)' not in source:
    raise SystemExit("recipe names must use a 1.00x native scale before fit-to-width")
if 'fit(s_cheat_names[i], 112, 1.0f, 0.6f)' not in source:
    raise SystemExit("cheat option names must use a 1.00x native scale before fit-to-width")
if 'text_center(160, 124, s_info.demo ? "- Demo -" : "Press START to play", 1.0f' not in source:
    raise SystemExit("startup Press START text must stay at 1.00x")
if "float label_scale = strlen(tabs[i].name)" in source:
    raise SystemExit("left-rail labels must not vary in scale with English source length")
for label in (
    "ui_native_glyph_text_center_fit(cx, y0 + 19, tabs[i].name, label_scale",
    'ui_native_glyph_text_center_fit(vx0 + 58, vy1 - 20, "Drag to look", 0.7f',
    "ui_native_glyph_text_fit_original(x0 + 7, y0 + 2, s_log.name[i], 1.0f",
    "ui_native_glyph_text_fit_original(x, y0 + 1, a->name, 1.0f",
    "ui_native_glyph_text_fit_original(CX0 + 7, y + 1, label, 0.90f",
    "ui_native_glyph_text_center_fit((bx0 + bx1) / 2, (by0 + by1) / 2 - 6.5f, value, 0.75f",
):
    if label not in source:
        raise SystemExit(f"expected legibility call site missing: {label}")

print("Native iQue glyph renderer and scale-preserving ink enlargement checks passed")

# Recipe and cheat labels should share the native iQue glyph renderer and row baseline.
for expected in (
    'ui_native_glyph_text_original(x0 + 21, y0 + 2, name,',
    'ui_native_glyph_text_original(CX0 + 7, y + 2, s_cheat_names[i],',
    'ui_native_glyph_text(CX0 + 124, y + 4, s_cheat_help[i],',
):
    if expected not in source:
        raise SystemExit(f"expected native-glyph row renderer missing: {expected}")

import csv
translation_csv = ROOT / "port/3ds/localization/ui_translations_zh_CN.csv"
with translation_csv.open("r", encoding="utf-8-sig", newline="") as stream:
    rows = list(csv.DictReader(stream))
if not any(r["source_file"] == "bottom_screen.c" and r["english_source"] == "Checked" and r["proposed_simplified_chinese"] == "已完成" for r in rows):
    raise SystemExit("Checked translation must be 已完成")
for source_text, expected_text in (("Heart & flower drops", "心 & 鲜花掉落"), ("Low HP beep", "低HP哔哔")):
    if not any(r["source_file"] == "bottom_screen.c" and r["english_source"] == source_text and r["proposed_simplified_chinese"] == expected_text for r in rows):
        raise SystemExit(f"missing settings translation: {source_text} -> {expected_text}")
if not any(r["source_file"] == "touch_menu.c" and r["english_source"] == "Peach" and r["proposed_simplified_chinese"] == "桃花公主" for r in rows):
    raise SystemExit("Peach battle-menu title must be translated as 桃花公主")
print("Unified row renderers, scale targets, and settings translations checks passed")
