#!/usr/bin/env python3
"""Static regression checks for iQue texture/icon address plumbing."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]

def require(path: str, snippet: str, purpose: str) -> None:
    text = (ROOT / path).read_text(errors="replace")
    if snippet not in text:
        print(f"FAIL: {path} missing {purpose} ({snippet!r})", file=sys.stderr)
        raise SystemExit(1)

layout = "port/3ds/include/pm3ds_rom_layout.h"
require(layout, "#define PM3DS_ICON_ROM_ADDR(offset) (PM3DS_CART(PM3DS_ROM_ICON) + (offset))",
        "an explicit version-specific icon ROM address helper")
require("port/3ds/include/ld_addrs.h", "#define icon_ROM_START ((unsigned char*)PM3DS_CART(PM3DS_ROM_ICON))",
        "the general 3DS ROM-address override")
require("port/3ds/tools/gen_version_headers.sh", '#include "../../include/ld_addrs.h"',
        "the generated ld_addrs.h forwarding adapter")
for path, snippet, purpose in (
    ("src/hud_element.c", "PM3DS_ICON_ROM_ADDR(raster)", "direct icon raster ROM address"),
    ("src/hud_element.c", "PM3DS_ICON_ROM_ADDR(palette)", "direct icon palette ROM address"),
    ("src/item_entity.c", "PM3DS_ICON_ROM_ADDR(raster)", "item entity icon raster address"),
    ("src/item_entity.c", "PM3DS_ICON_ROM_ADDR(palette)", "item entity icon palette address"),
    ("src/msg.c", "PM3DS_ICON_ROM_ADDR(gItemIconRasterOffsets[offset])", "message item-icon raster address"),
    ("src/msg.c", "PM3DS_ICON_ROM_ADDR(gItemIconPaletteOffsets[offset])", "message item-icon palette address"),
    ("port/3ds/source/n64_shim/touch_menu.c", "PM3DS_ICON_ROM_ADDR(pos[1])", "bottom-screen touch-menu raster address"),
    ("port/3ds/source/n64_shim/touch_menu.c", "PM3DS_ICON_ROM_ADDR(pos[2])", "bottom-screen touch-menu palette address"),
    ("src/world/area_kmr/kmr_06/sticker_sign.c", "PM3DS_ICON_ROM_ADDR(0)", "sticker sign icon base"),
    ("src/world/area_osr/osr_00/scenes.c", "memcpy(N(PeachLetterImg), charset_peach_letter_OFFSET, sizeof(N(PeachLetterImg)));", "direct Peach Letter raster copy"),
    ("src/world/area_osr/osr_00/scenes.c", "gfx_tex_invalidate_range(N(PeachLetterPal), sizeof(N(PeachLetterPal)));", "Peach Letter palette cache invalidation"),
    ("port/3ds/source/n64_shim/gfx_translate.c", "pm3ds_addr_is_texture_source(st->timg_addr)", "TLUT source-address validation"),
):
    require(path, snippet, purpose)

workflow = (ROOT / ".github/workflows/ique-3ds.yml").read_text()
if "check_ique_texture_assets.py" not in workflow:
    print("FAIL: workflow does not validate iQue icon and Peach Letter asset bytes", file=sys.stderr)
    raise SystemExit(1)
print("iQue texture/icon wiring checks passed")
