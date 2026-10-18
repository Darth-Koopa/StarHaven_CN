#include "functions.h"
#include "npc.h"
#include "game_modes.h"
#ifdef _3DS
#include "gfx_prof.h"   // 3DS port: sampling profiler phases (empty unless PM3DS_STEP_PROF)
#endif

void state_world_draw_aux_ui(void);

void state_init_world(void) {
    set_game_mode_render_frontUI(0, state_world_draw_aux_ui);
}

void state_step_world(void) {
#ifdef _3DS
    PROF_PHASE(PH_W_ENC);
    update_encounters();
    PROF_PHASE(PH_W_NPCS);
    update_npcs();
    PROF_PHASE(PH_W_PLAYER);
    update_player();
    PROF_PHASE(PH_W_ITEMS);
    update_item_entities();
    PROF_PHASE(PH_W_FX);
    update_effects();
    PROF_PHASE(PH_W_MODELS);
    iterate_models();
    PROF_PHASE(PH_W_CAM);
    update_cameras();
    PROF_PHASE(PH_G_MODE);
#else
    update_encounters();
    update_npcs();
    update_player();
    update_item_entities();
    update_effects();
    iterate_models();
    update_cameras();
#endif
}

void state_drawUI_world(void) {
    draw_status_ui();
    draw_encounter_ui();
    imgfx_update_cache();
}

void state_world_draw_aux_ui(void) {
    draw_first_strike_ui();
}
