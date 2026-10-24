#!/usr/bin/env python3
"""Static consistency checks for the official iQue build configuration.

This test intentionally does not require or distribute a baserom. It protects
against accidentally desynchronizing the region's ROM identity, build target,
font ranges and universal launcher.
"""
from pathlib import Path
import re
import sys
import yaml

ROOT = Path(__file__).resolve().parents[3]

def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)

layout = (ROOT / "port/3ds/include/pm3ds_rom_layout.h").read_text()
makefile = (ROOT / "port/3ds/Makefile").read_text()
starter = (ROOT / "port/3ds/universal/starter.c").read_text()
installer = (ROOT / "install_compilers.sh").read_text()
asset_tool = (ROOT / "port/3ds/tools/gen_include_assets.py").read_text()
extract_assets = (ROOT / "port/3ds/tools/extract_assets.py").read_text()
splat_extensions = {
    name: (ROOT / "tools/splat_ext" / name).read_text()
    for name in ("pm_icons.py", "tex_archives.py", "pm_map_data.py", "pm_sprites.py")
}
splat = (ROOT / "ver/ique/splat.yaml").read_text()
splat_doc = yaml.safe_load(splat)
stager = (ROOT / "port/3ds/tools/extract_baserom.py").read_text()
romdata = (ROOT / "port/3ds/tools/romdata.py").read_text()

block_match = re.search(r"#elif VERSION_IQUE\s*(.*?)\n#else", layout, re.S)
if not block_match:
    fail("pm3ds_rom_layout.h has no VERSION_IQUE block")
ique = block_match.group(1)
expected_layout = {
    "PM3DS_ROM_ICON": "0x215390",
    "PM3DS_ROM_LOGOS": "0x247230",
    "PM3DS_ROM_TITLE_BG_1": "0x262230",
    "PM3DS_ROM_IMGFX_DATA": "0x294870",
    "PM3DS_ROM_SHADING": "0x35EC00",
    "PM3DS_ROM_AUDIO": "0xF00000",
    "PM3DS_ROM_AUDIO_END": "0x1942C40",
    "PM3DS_ROM_SPRITES": "0x1943000",
    "PM3DS_ROM_MAPFS": "0x1E40000",
    "PM3DS_ROM_MAPFS_END": "0x27FEE1E",
    "PM3DS_ROM_MSG": "0x1B83000",
    "PM3DS_ROM_DATA_SIZE": "0x2800000",
    "PM3DS_ROM_CRC32": "0x18C02AD5",
}
for name, value in expected_layout.items():
    if not re.search(rf"#define\s+{name}\s+{value}\b", ique, re.I):
        fail(f"iQue {name} must be {value}")
if not re.search(r'#define\s+PM3DS_ROM_GAME_ID\s+"\\0\\0\\0\\0"', ique):
    fail("iQue must match the original blank game-code field; do not stamp a fake ID")
if "VERSION_IQUE" not in makefile or not re.search(r"PM3DS_VERSION\s*\?=\s*pal", makefile) or "PM3DS_VSUFFIX	:=	_ique" not in makefile:
    fail("Makefile does not expose the iQue target")
# The iQue ROM does not contain the anti-obfuscation entry-point segments
# represented by src/obfuscation_shims.c. The 3DS build uses native function
# aliases instead, so compiling that N64-only DMA wrapper source is invalid.
if not re.search(r"(?m)^M4_EXCLUDE\s*:=.*\bobfuscation_shims\.c\b", makefile):
    fail("3DS Makefile must exclude the N64-only obfuscation_shims.c wrapper")
for rel, alias in (
    ("src/main.c", "#define shim_create_audio_system_obfuscated create_audio_system"),
    ("src/main.c", "#define shim_load_engine_data_obfuscated load_engine_data"),
    ("src/world/world.c", "#define shim_general_heap_create_obfuscated general_heap_create"),
    ("src/state_battle.c", "#define shim_battle_heap_create_obfuscated battle_heap_create"),
):
    if alias not in (ROOT / rel).read_text():
        fail(f"missing native 3DS bypass for obfuscated shim: {alias}")
if "$(OUTPUT).elf : $(WORLD_OFILES) $(TOPDIR)/m11_syms$(PM3DS_VSUFFIX).ld" not in makefile:
    fail("ELF target must depend on the generated linker script so symbol retries force a relink")
if "set -euo pipefail" not in installer or "curl --fail --location --retry 3 --show-error" not in installer:
    fail("install_compilers.sh must fail on a failed download or extraction")
if '"ique": {' not in asset_tool or '"ique": (b"Paper Mario (iQue China)", 0x2800000)' not in romdata:
    fail("font extraction or ROM-data packaging is missing iQue metadata")
if "IQUE_ROM_CRC32     0x18C02AD5u" not in starter or "return 'Q'" not in starter:
    fail("universal launcher does not identify the known official iQue ROM")

# Splat loads custom segment types from tools/splat_ext, but these modules
# import shared helpers from tools/build/common.py as `common`. Ensure clean
# CI runs establish this path explicitly and the affected extensions do too.
if '"tools" / "build"' not in extract_assets or 'env["PYTHONPATH"]' not in extract_assets:
    fail("extract_assets.py does not expose tools/build through PYTHONPATH")
for name, source in splat_extensions.items():
    if 'parent.parent / "build"' not in source:
        fail(f"{name} does not add tools/build before importing common")


# In a PM3DS_ASSET_ROOT build, charset_*_OFFSET is an in-image asset pointer.
# Copy it directly instead of passing it through dma_copy's mixed ROM/host
# address classifier; otherwise an out-of-range host pointer can be mistaken
# for a low cart-ROM offset, corrupting both dialogue and UI glyphs.
msg_source = (ROOT / "src/msg.c").read_text()
if "memcpy(dest, (const void *)(uintptr_t)offset, size);" not in msg_source:
    fail("3DS load_font_data must copy embedded charset blobs directly")
if "gfx_tex_invalidate_range(dest, size);" not in msg_source:
    fail("3DS load_font_data must invalidate texture cache after updating font buffers")
if "#if defined(_3DS) && defined(PM3DS_ASSET_ROOT)" not in msg_source:
    fail("direct charset copy must be restricted to the 3DS embedded-asset build")


# Texture resources: the icon segment is a packed ROM block and must use the
# version-specific cart base, while RDP TLUT data may be host pointers or tagged
# ROM pointers (including 2-byte-aligned addresses). Keep these paths explicit.
rom_layout = (ROOT / "port/3ds/include/pm3ds_rom_layout.h").read_text()
if "#define PM3DS_ICON_ROM_ADDR(offset) (PM3DS_CART(PM3DS_ROM_ICON) + (offset))" not in rom_layout:
    fail("3DS icon reads must use the explicit per-version icon ROM address")
texture_addr = (ROOT / "port/3ds/include/pm3ds_addr.h").read_text()
if "static inline int pm3ds_addr_is_texture_source(uintptr_t a)" not in texture_addr:
    fail("texture/TLUT address classification helper is missing")
if "if ((a & 3)" in texture_addr:
    fail("texture/TLUT source validation must not require 4-byte alignment")
for path, token in (
    ("src/hud_element.c", "PM3DS_ICON_ROM_ADDR(raster)"),
    ("src/hud_element.c", "PM3DS_ICON_ROM_ADDR(palette)"),
    ("src/item_entity.c", "PM3DS_ICON_ROM_ADDR(raster)"),
    ("src/item_entity.c", "PM3DS_ICON_ROM_ADDR(palette)"),
    ("src/msg.c", "PM3DS_ICON_ROM_ADDR(gItemIconRasterOffsets[offset])"),
    ("src/msg.c", "PM3DS_ICON_ROM_ADDR(gItemIconPaletteOffsets[offset])"),
    ("port/3ds/source/n64_shim/touch_menu.c", "PM3DS_ICON_ROM_ADDR(pos[1])"),
    ("port/3ds/source/n64_shim/touch_menu.c", "PM3DS_ICON_ROM_ADDR(pos[2])"),
    ("src/world/area_kmr/kmr_06/sticker_sign.c", "PM3DS_ICON_ROM_ADDR(0)"),
):
    if token not in (ROOT / path).read_text():
        fail(f"{path} must read iQue icon data via PM3DS_ICON_ROM_ADDR")
letter_scene = (ROOT / "src/world/area_osr/osr_00/scenes.c").read_text()
if "memcpy(N(PeachLetterImg), charset_peach_letter_OFFSET, sizeof(N(PeachLetterImg)));" not in letter_scene:
    fail("Princess Peach's Letter raster must copy the embedded CI8 blob directly")
if "gfx_tex_invalidate_range(N(PeachLetterPal), sizeof(N(PeachLetterPal)));" not in letter_scene:
    fail("Princess Peach's Letter palette must invalidate texture cache after copy")

# Check iQue's dedicated Chinese-font ranges match the upstream splat entries.
font_ranges = {
    '"charset/standard.dat": (0x10E120, 0x15D120)': "0x10E120",
    '"charset/standard/palette.dat": (0x15D120, 0x15D620)': "0x15D120",
    '"charset/title.dat": (0x15D620, 0x15E580)': "0x15D620",
    '"charset/subtitle.dat": (0x15E580, 0x15F108)': "0x15E580",
    '"charset/subtitle/palette.dat": (0x15F120, 0x15F130)': "0x15F120",
    '"charset/title/palette.dat": (0x15F130, 0x15F140)': "0x15F130",
}
for snippet, _ in font_ranges.items():
    if snippet not in asset_tool:
        fail(f"missing iQue font range {snippet}")
for address in ("0x10E120, pm_charset", "0x15D120, pm_charset_palettes",
                "0x15D620, pm_charset", "0x15E580, pm_charset",
                "0x15F120, pm_charset_palettes", "0x15F130, pm_charset_palettes"):
    if address not in splat:
        fail(f"font range {address} no longer matches ver/ique/splat.yaml")

# iQue file-menu glyphs must preserve the native byte-pair mapping and use
# immutable embedded standard.dat data on 3DS. A rotating scratch buffer is
# safe for memory bounds but corrupts earlier glyphs while queued display lists
# still reference its slots.
filemenu_msg = (ROOT / "src/filemenu/filemenu_msg.c").read_text()
touch_menu = (ROOT / "port/3ds/source/n64_shim/touch_menu.c").read_text()
bottom_screen = (ROOT / "port/3ds/source/platform/bottom_screen.c").read_text()
if "*offsetPtr = offset" in filemenu_msg or "filemenu_glyphBuffer[*offsetPtr]" in filemenu_msg:
    fail("iQue file-menu CJK glyph path can index outside its 20-slot glyph cache")
for token in ("glyphOffset = (((character >> 8) - 1) * 0x31) + (c & 0xff) + 0x47",
              "#if defined(_3DS) && defined(PM3DS_ASSET_ROOT)",
              "glyphRaster = (const void *)(charset_standard_OFFSET + glyphOffset * 0x80)",
              "gDPLoadTextureBlock_4b(gMainGfxPos++, glyphRaster"):
    if token not in filemenu_msg:
        fail(f"iQue file-menu CJK glyph path is missing native/stable raster access: {token}")
for token in ("MSG_CHAR_MULTIBYTE_FIRST", "0xE000u + (u32)glyph"):
    if token not in touch_menu:
        fail(f"iQue bottom-screen message decoder does not preserve multibyte glyph ids: {token}")
for token in ("charset_standard_OFFSET + (size_t)glyph * 0x80", "PM3DS_CJK_PRIVATE_BASE", "font_text_unit", "#define FONT_ATLAS_W 512"):
    if token not in bottom_screen:
        fail(f"iQue bottom-screen renderer is missing CJK glyph support: {token}")
if "5c724685085eba796537573dd6f84aaddedc8582" not in stager:
    fail("baserom staging helper does not accept the original iQue SHA-1")

gen_version_headers = (ROOT / "port/3ds/tools/gen_version_headers.sh").read_text()
if 'bash "$ROOT/configure" "$VER" --no-split-assets' not in gen_version_headers:
    fail("gen_version_headers.sh must invoke configure via bash (checkout permissions may be non-executable)")
if 'command ninja "generated_code_$VER"' not in gen_version_headers:
    fail("version header generation must build the complete generated_code target")
if 'cp -a "$GEN_INCLUDE/." "$OUT/"' not in gen_version_headers:
    fail("version header generation must mirror the full generated include tree")
if 'check_generated_headers.py' not in gen_version_headers:
    fail("version generated includes must be checked before C compilation")
required_codegen = (
    "sprite/sprite_shading_profiles.h",
    "icon_offsets.h",
    "recipes.inc.c",
    "pause/world_map.png.h",
)
header_checker = (ROOT / "port/3ds/tools/check_generated_headers.py").read_text()
for path in required_codegen:
    if path not in header_checker:
        fail(f"generated-header preflight does not check {path}")

workflow = (ROOT / ".github/workflows/ique-3ds.yml").read_text()
workflow_doc = yaml.safe_load(workflow)
if "check_generated_headers.py ique" not in workflow:
    fail("workflow must preflight all generated iQue headers before compiling")
if "check_ique_font_assets.py" not in workflow:
    fail("workflow must validate the extracted iQue font bytes against the ROM")
if "check_ique_texture_assets.py" not in workflow:
    fail("workflow must validate the packed iQue icon and Peach Letter bytes")
if "check_ique_texture_wiring.py" not in workflow or "test_pm3ds_addr" not in workflow:
    fail("workflow must run texture address wiring and TLUT host regression tests")
if not isinstance(workflow_doc, dict) or not isinstance(workflow_doc.get("jobs"), dict):
    fail("GitHub Actions workflow YAML does not define jobs")
for token in ("Install makerom for CIA packaging", "StarHaven_iQue.cia",
              "StarHaven_iQue_3DS.zip", "dist/StarHaven_iQue_3DS.zip"):
    if token not in workflow:
        fail(f"workflow is missing dual-format release packaging: {token}")
artifact_step = workflow.split("      - name: Upload iQue 3DS packages (ROM excluded)", 1)[-1]
if "path: dist/StarHaven_iQue_3DS.zip" not in artifact_step:
    fail("only the release ZIP should be uploaded as the build artifact")
if "romdata_ique.bin" in artifact_step:
    fail("the ROM-data mapping table must not be uploaded as a loose artifact")
dockerfile = (ROOT / "port/3ds/Dockerfile").read_text()
if "FROM devkitpro/devkitarm:20260610" not in dockerfile:
    fail("Dockerfile must use the pinned official devkitpro/devkitarm image")
if "dkp-pacman -Syyu" in dockerfile or "dkp-pacman -S --" in dockerfile:
    fail("Dockerfile must not install toolchain packages from the CI-blocked package mirror")
for token in ("$DEVKITARM/3ds_rules", "3dsxtool", "libctru/include/3ds.h"):
    if token not in dockerfile:
        fail(f"Dockerfile toolchain preflight missing: {token}")
for token in ("docker build --pull --progress=plain", "Validate the release package (no ROM, no loose romdata table)",
              "romdata_ique.bin", "actions/upload-artifact@v4"):
    if token not in workflow:
        fail(f"workflow is missing CI preflight/package validation: {token}")
if "for attempt in 1 2 3 4 5 6 7; do" not in workflow:
    fail("link-symbol retry loop must allow an initial build plus six retries")
if 'undefined reference to' not in workflow or 'not retrying symbol generation' not in workflow:
    fail("symbol-discovery retries must be gated on linker overlay-symbol errors, not ordinary compiler errors")
if not isinstance(splat_doc, dict) or not isinstance(splat_doc.get("segments"), list):
    fail("ver/ique/splat.yaml did not parse as a splat configuration")
for required in ("IQUE_ROM_URL", "5c724685085eba796537573dd6f84aaddedc8582",
                 "extract_assets.py", "gen_include_assets.py", "PM3DS_VERSION=ique",
                 "package.sh", "actions/upload-artifact@v4"):
    if required not in workflow:
        fail(f"workflow is missing step/configuration: {required}")

print("iQue configuration checks passed (ROM SHA-1, CRC32/layout constants and font ranges are pinned in source).")
