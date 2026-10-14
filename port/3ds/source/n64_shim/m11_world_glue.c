/*
 * m11_world_glue.c — small link-level glue for the natively compiled world.
 */
#include "common.h"
#include "effects.h"
#include "audio.h"

// flo_* maps call this with an int-typed local prototype; upstream links it
// as an alias of fx_sun (ver/pal/undefined_syms.txt). An alias would hand
// fx_sun's float params whatever is in s0-s3 on ARM hard-float, so convert.
void fx_sun_undeclared(s32 type, s32 x, s32 y, s32 z, s32 w, s32 duration) {
    fx_sun(type, x, y, z, w, duration);
}


// Target of every PROVIDE()d overlay address symbol in m11_syms.ld
// (START == END == here, so dma_copy() from one is a zero-length no-op).
const u32 pm3ds_null_segment[1] = { 0 };

/* M14: the gGameStatusPtr field writes from the real load_engine_data, which
 * the port's __wrap_load_engine_data skipped (musicEnabled = false meant
 * bgm_set_song never requested a song). */
void pm3ds_load_engine_status_fields(void) {
    s32 i;

    gGameStatusPtr->unk_79 = 0;
    gGameStatusPtr->backgroundFlags = 0;
    gGameStatusPtr->musicEnabled = true;
    gGameStatusPtr->healthBarsEnabled = true;
    gGameStatusPtr->introPart = INTRO_PART_NONE;
    gGameStatusPtr->demoBattleFlags = 0;
    gGameStatusPtr->multiplayerEnabled = false;
    gGameStatusPtr->altViewportOffset.x = -8;
    gGameStatusPtr->altViewportOffset.y = 4;
    gGameStatusPtr->debugQuizmo = gGameStatusPtr->unk_13C = 0;
    gGameStatusPtr->saveCount = 0;
    for (i = 0; i < ARRAY_COUNT(gGameStatusPtr->holdRepeatInterval); i++) {
        gGameStatusPtr->holdRepeatInterval[i] = 3;
        gGameStatusPtr->holdDelayTime[i] = 12;
    }
}
