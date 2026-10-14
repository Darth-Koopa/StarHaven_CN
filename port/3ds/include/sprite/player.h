// M4: forward to the real generated header in repo-root include/sprite/.
// The previous hand-stubbed enum here only listed a dozen SPR_* IDs and was
// missing ANIM_* / SPR_IMG_* / SPR_PAL_* defines that decomp src/evt needs.
// The real header is produced by port/3ds/tools/gen_player_sprite_header.py
// (a port-side standalone subset of tools/build/sprite/sprites.py that only
// emits the macros, skipping the heavy sprite-blob build).
//
// IMPORTANT: do not add an include guard in this file — the target uses the
// same _SPRITE_PLAYER_H_ guard, so guarding here would cause the target's
// contents to be skipped on first inclusion.
#include "../../../../include/sprite/player.h"
