#!/usr/bin/env python3
"""
Port-side standalone generator for `include/sprite/npc/<Name>.h`.

Upstream tools/build/sprite/header.py uses splat_ext.pm_sprites's
`from_dir(...)` which transitively depends on splat + sprite-blob plumbing.
For M4 we only need the SPR_/SPR_IMG_/SPR_PAL_/ANIM_ macros so that
references like `#include "sprite/npc/WorldClubba.h"` resolve. The
compressed sprite blob and runtime decode are M5+.

Reads assets/<ver>/sprite/npc.xml (the manifest of NPC sprite names) and
for each entry parses assets/<ver>/sprite/npc/<Name>/SpriteSheet.xml,
then emits the corresponding header in <out_dir>/.

The numeric encoding matches upstream header.py:
  SPR_<name>                      = <s>
  SPR_PAL_<name>[_<palette>]      = <p>
  SPR_IMG_<name>_<raster>         = <i>
  ANIM_<name>[_<palette>]_<anim>  = (s << 16) | (p << 8) | a
"""

import argparse
import xml.etree.ElementTree as ET
from pathlib import Path


def emit_header(out_path: Path, sprite_name: str, sprite_id: int, sheet_xml_path: Path) -> None:
    sprite = ET.parse(sheet_xml_path).getroot()
    palettes = sprite.findall("./PaletteList/Palette")
    rasters = sprite.findall("./RasterList/Raster")
    anims = sprite.findall("./AnimationList/Animation")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w") as f:
        guard = f"_NPC_SPRITE_{sprite_name.upper()}_H_"
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write('#include "types.h"\n\n')
        f.write(f"#define SPR_{sprite_name} 0x{sprite_id:02X}\n\n")

        for i, raster in enumerate(rasters):
            raster_id = int(raster.attrib.get("id", str(i)), 16)
            # Upstream npc_sprite.py derives raster name from src filename
            # (strip .png suffix); explicit `name` attribute is optional.
            raster_name = raster.attrib.get("name") or raster.attrib["src"].split(".png")[0]
            f.write(f"#define SPR_IMG_{sprite_name}_{raster_name} 0x{raster_id:X}\n")
        f.write("\n")

        for p, palette in enumerate(palettes):
            palette_name = palette.attrib.get("name") or palette.attrib["src"].split(".png")[0]
            palette_id = int(palette.attrib.get("id", str(p)), 16)
            if palette_name == "Default":
                f.write(f"#define SPR_PAL_{sprite_name} 0x{palette_id:X}\n")
            else:
                f.write(f"#define SPR_PAL_{sprite_name}_{palette_name} 0x{palette_id:X}\n")
        f.write("\n")

        for p, palette in enumerate(palettes):
            palette_name = palette.attrib.get("name") or palette.attrib["src"].split(".png")[0]
            palette_id = int(palette.attrib.get("id", str(p)), 16)
            for a, anim in enumerate(anims):
                anim_name = anim.attrib["name"]
                if palette_name == "Default":
                    f.write(f"#define ANIM_{sprite_name}_{anim_name} 0x{sprite_id:02X}{palette_id:02X}{a:02X}\n")
                else:
                    f.write(f"#define ANIM_{sprite_name}_{palette_name}_{anim_name} 0x{sprite_id:02X}{palette_id:02X}{a:02X}\n")
            f.write("\n")

        f.write(f"#endif // {guard}\n")


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("manifest", type=Path, help="assets/<ver>/sprite/npc.xml")
    p.add_argument("out_dir", type=Path, help="include/sprite/npc")
    args = p.parse_args()

    manifest_root = ET.parse(args.manifest).getroot()
    sprite_dir_root = args.manifest.parent / "npc"

    sprite_id = 1
    skipped = 0
    written = 0
    for sprite_elem in manifest_root.findall(".//Sprite"):
        sprite_name = sprite_elem.attrib["name"]
        sheet_xml = sprite_dir_root / sprite_name / "SpriteSheet.xml"
        if not sheet_xml.exists():
            skipped += 1
            sprite_id += 1
            continue
        out_path = args.out_dir / f"{sprite_name}.h"
        emit_header(out_path, sprite_name, sprite_id, sheet_xml)
        written += 1
        sprite_id += 1

    print(f"wrote {written} headers, skipped {skipped} missing")


if __name__ == "__main__":
    main()
