#!/usr/bin/env python3
"""Host-side smoke test for the iQue UI translation generator and C lookup."""
from __future__ import annotations

import importlib.util
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
GENERATOR = ROOT / "port/3ds/tools/generate_bottom_screen_translations.py"
CSV = ROOT / "port/3ds/localization/ui_translations_zh_CN.csv"


def main() -> int:
    spec = importlib.util.spec_from_file_location("ui_translation_generator", GENERATOR)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load UI translation generator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    glyphs = module.load_ique_character_map()
    expected_glyph = glyphs["状"]

    with tempfile.TemporaryDirectory(prefix="pm3ds-ui-translation-test-") as temp:
        tempdir = Path(temp)
        header = tempdir / "ui_translations_zh_CN.h"
        subprocess.run([
            "python3", str(GENERATOR), "--csv", str(CSV), "--out", str(header), "--strict"
        ], check=True, capture_output=True, text=True)
        c_file = tempdir / "test.c"
        exe = tempdir / "test"
        c_file.write_text(f'''#include <stdio.h>
#include <string.h>
#include "{header}"
int main(void) {{
    const unsigned char *p = (const unsigned char *)pm3ds_ui_translate("Stats");
    unsigned int cp;
    if (p[0] != 0xEE || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return 1;
    cp = ((unsigned int)(p[0] & 0x0F) << 12) | ((unsigned int)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    if (cp != 0xE000u + {expected_glyph}u) return 2;
    if (strcmp(pm3ds_ui_translate("not a translation key"), "not a translation key") != 0) return 3;
    if (strstr(pm3ds_ui_translate("%d of %d enemies"), "%d") == NULL) return 4;
    return 0;
}}
''', encoding="utf-8")
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(c_file), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    print("UI translation generator/C lookup tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
