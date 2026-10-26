# Generate the complete version-specific include tree for the 3DS port.
# The decomp's configure step emits more than message_ids.h and item_data.inc.c:
# enums.h also needs sprite/sprite_shading_profiles.h, the full-game sources
# include recipes.inc.c, and Splat-backed headers (icon_offsets.h and image
# metadata) are implicit outputs of generated segment rules. Building/copying
# only a hand-picked subset allowed the port to reach C compilation with a
# silently incomplete include tree.
#
# The generated tree lives under ver/<version>/build/include. Mirror all of it
# into port/3ds/gen/<version>/, which is first in the 3DS compiler include path.
# Then run the port-specific player/NPC generators last so their headers take
# precedence over upstream decomp versions where the port uses a different ABI.
#
# Needs ver/<version>/baserom.z64 and Splat-extracted assets first.
# usage: gen_version_headers.sh us|ique|jp
set -euo pipefail
VER=${1:?version (us|ique|jp)}
case "$VER" in
  us|ique|jp) ;;
  *) echo "unsupported version '$VER' (expected us, ique or jp)" >&2; exit 2 ;;
esac
PORT=$(realpath "$(dirname "$0")/..")
ROOT=$(realpath "$PORT/../..")
OUT=$PORT/gen/$VER
GEN_INCLUDE=$ROOT/ver/$VER/build/include
export PATH="$ROOT/.venv/bin:$ROOT/venv/bin:$PATH" PYTHONPATH="$ROOT/tools/build:$ROOT/tools"

cd "$ROOT"
bash "$ROOT/configure" "$VER" --no-split-assets > /dev/null
# This phony target enumerates all generated C/header outputs for the selected
# version. It includes implicit outputs such as icon_offsets.h and
# sprite_shading_profiles.h, plus recipes, item/move tables, NPC image headers,
# and generated INCLUDE_IMG/PAL headers. Don't reduce this to a few explicit
# targets: they share the same Ninja graph and may otherwise be omitted.
command ninja "generated_code_$VER" > /dev/null

if [[ ! -d "$GEN_INCLUDE" ]]; then
  echo "configure did not create $GEN_INCLUDE" >&2
  exit 1
fi
mkdir -p "$OUT"
cp -a "$GEN_INCLUDE/." "$OUT/"

# IMPORTANT: gen/$(VER) is the first include directory in the 3DS Makefile.
# Without this adapter, C files include the raw generated ld_addrs.h before
# port/3ds/include/ld_addrs.h, silently bypassing the 3DS overrides for
# icon_ROM_START (cart-tagged iQue icon block), charset_ROM_START (embedded
# INCLUDE_IMG/PAL pointers), and title_bg_1_ROM_START. That makes valid icon
# offsets point into the weak RAM stubs and turns embedded letter art into
# bogus ROM reads. Keep the version-generated declarations as the source of
# truth, but route every ordinary #include "ld_addrs.h" through the port
# wrapper which includes the generated declarations and then applies port
# address overrides.
cat > "$OUT/ld_addrs.h" <<'EOF_LD_ADDRS_ADAPTER'
#ifndef PM3DS_GENERATED_LD_ADDRS_ADAPTER_H
#define PM3DS_GENERATED_LD_ADDRS_ADAPTER_H
#include "../../include/ld_addrs.h"
#endif
EOF_LD_ADDRS_ADAPTER

# These 3DS-specific headers use the port's compact sprite tables rather than
# the decomp's N64 runtime tables, so generate them after the full-tree copy.
mkdir -p "$OUT/sprite/npc"
python3 "$PORT/tools/gen_player_sprite_header.py" "assets/$VER/sprite/player.xml" "$OUT/sprite/player.h"
python3 "$PORT/tools/gen_npc_sprite_headers.py" "assets/$VER/sprite/npc.xml" "$OUT/sprite/npc"

# Fail here, before Docker/C compilation, if the generated include set is not
# complete. The check also verifies that every header/C include emitted by
# configure was copied into the port's highest-priority include directory.
python3 "$PORT/tools/check_generated_headers.py" "$VER"
echo "headers for $VER in $OUT: $(find "$OUT" -type f | wc -l) files"
