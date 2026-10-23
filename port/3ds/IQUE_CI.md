# iQue GitHub Actions build notes

The iQue job intentionally performs the upstream `generated_code_ique` Ninja
 target before cross-compiling the 3DS port. Do not replace this with a short
list of selected generated files. The complete decomp include tree contains
version-sensitive and Splat-generated outputs including:

- `message_ids.h`, `item_data.inc.c`, `item_enum.h`, `move_data.inc.c`, and
  `move_enum.h`;
- `icon_offsets.h` and `sprite/sprite_shading_profiles.h`, emitted as implicit
  outputs of Splat segment rules;
- `recipes.inc.c`, `world_map.inc.c`, and `battle/actor_types.*`;
- image/sprite include metadata such as `pause/world_map.png.h` and NPC headers.

The generator mirrors `ver/<version>/build/include/` into
`port/3ds/gen/<version>/`, which is first in the Makefile include search path.
The 3DS-specific player and NPC sprite headers are generated afterward so the
port's versions override upstream decomp outputs where their ABI differs.

`check_generated_headers.py` is a preflight guard. It checks known generated
includes referenced by compiled sources and verifies that every `.h`/`.c`
include output emitted under the configured include directory is mirrored into
the port include directory. A missing generated include should fail in the
asset/codegen step, before the longer ARM cross-build starts.

The CI job still requires the private `IQUE_ROM_URL` secret. This project does
not include or publish the ROM. A successful codegen/header preflight is not a
substitute for testing the final `.3dsx` on an emulator or a New 3DS.
