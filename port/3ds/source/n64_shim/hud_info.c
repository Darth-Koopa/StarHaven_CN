/*
 * hud_info.c — game-side half of the bottom-screen dashboard.
 *
 * Reads the decomp's game state into a plain struct (pm3ds_hud_info.h) so the
 * 3DS-side renderer (platform/bottom_screen.c) never needs the game headers.
 */
#include "common.h"
#include "map.h"
#include "game_modes.h"
#include "pm3ds_hud_info.h"

extern AreaConfig gAreas[];
#define PAUSE_MAP_SPACES 34   /* entries in PauseMapSpaces (include/world_map.inc.c) */
extern PauseMapSpace PauseMapSpaces[];
MapConfig* get_current_map_config(void);

static const struct { const char* area; const char* name; } sAreaNames[] = {
    { "area_kmr", "Goomba Region" },      { "area_mac", "Toad Town" },
    { "area_tik", "Toad Town Tunnels" },  { "area_kgr", "Inside the Whale" },
    { "area_kkj", "Peach's Castle" },     { "area_hos", "Star Region" },
    { "area_nok", "Koopa Region" },       { "area_trd", "Koopa Bros. Fortress" },
    { "area_iwa", "Mt. Rugged" },         { "area_dro", "Dry Dry Outpost" },
    { "area_sbk", "Dry Dry Desert" },     { "area_isk", "Dry Dry Ruins" },
    { "area_mim", "Forever Forest" },     { "area_obk", "Boo's Mansion" },
    { "area_arn", "Gusty Gulch" },        { "area_dgb", "Tubba Blubba's Castle" },
    { "area_omo", "Shy Guy's Toy Box" },  { "area_jan", "Lavalava Island" },
    { "area_kzn", "Mt. Lavalava" },       { "area_flo", "Flower Fields" },
    { "area_sam", "Shiver Region" },      { "area_pra", "Crystal Palace" },
    { "area_kpa", "Bowser's Castle" },    { "area_osr", "Peach's Castle Grounds" },
    { "area_end", "The End" },            { "area_mgm", "Playroom" },
    { "area_gv",  "Game Over" },          { "area_tst", "Test Area" },
};

static const char* sPartnerNames[] = {
    "-", "Goombario", "Kooper", "Bombette", "Parakarry", "Goompa", "Watt",
    "Sushie", "Lakilester", "Bow", "Goombaria", "Twink",
};

static const char* location_name(void) {
    MapConfig* mc = get_current_map_config();
    if (mc != nullptr && strcmp(mc->id, "kmr_20") == 0) {
        return "Mario's House";
    }
    if (gGameStatusPtr->areaID < 0) {
        return "";
    }
    const char* id = gAreas[gGameStatusPtr->areaID].id;
    for (u32 i = 0; i < ARRAY_COUNT(sAreaNames); i++) {
        if (id != nullptr && strcmp(id, sAreaNames[i].area) == 0) {
            return sAreaNames[i].name;
        }
    }
    return "";
}

/* chapter from story progress (first value of each chapter in enums.h) */
static s32 chapter_of(s32 story) {
    static const s32 firstOf[] = { -102, -72, -51, -12, 8, 42, 61, 90, 96 };
    s32 ch = 0;
    while (ch < (s32)ARRAY_COUNT(firstOf) && story >= firstOf[ch]) {
        ch++;
    }
    return ch;   /* 0 prologue, 1..8 chapters, 9 epilogue */
}

void pm3ds_hud_query(pm3ds_hud_info_t* out) {
    memset(out, 0, sizeof(*out));
    if (gGameStatusPtr == nullptr) {
        return;
    }
    s32 mode = get_game_mode();
    out->in_game = (mode == GAME_MODE_WORLD || mode == GAME_MODE_CHANGE_MAP || mode == GAME_MODE_ENTER_WORLD ||
                    mode == GAME_MODE_BATTLE || mode == GAME_MODE_END_BATTLE ||
                    mode == GAME_MODE_PAUSE || mode == GAME_MODE_UNPAUSE) &&
                   gGameStatusPtr->demoState == 0 && gGameStatusPtr->introPart < 0;
    out->in_battle = mode == GAME_MODE_BATTLE;
    out->demo = gGameStatusPtr->demoState != 0;
    if (!out->in_game) {
        return;
    }
    PlayerData* pd = &gPlayerData;
    out->hp = pd->curHP;          out->max_hp = pd->curMaxHP;
    out->fp = pd->curFP;          out->max_fp = pd->curMaxFP;
    out->bp = pd->maxBP;          out->level = pd->level;
    out->star_points = pd->starPoints;
    out->coins = pd->coins;
    out->star_pieces = pd->starPieces;
    out->star_power = pd->starPower;
    out->max_star_power = pd->maxStarPower;
    out->play_seconds = pd->frameCounter / 60;
    out->chapter = chapter_of(gCurrentSaveFile.globalBytes[0]);
    out->partner = pd->curPartner;
    snprintf(out->partner_name, sizeof(out->partner_name), "%s",
             (u32)pd->curPartner < ARRAY_COUNT(sPartnerNames) ? sPartnerNames[(u32)pd->curPartner] : "?");
    for (u32 i = 0; i < ARRAY_COUNT(pd->partners) && i < 32; i++) {
        if (pd->partners[i].enabled) {
            out->partners_owned |= 1u << i;
        }
    }
    snprintf(out->location, sizeof(out->location), "%s", location_name());

    /* world map: the pause menu's spaces, visited flags and Mario's location */
    s32 here = evt_get_variable(nullptr, GB_WorldLocation);
    out->map_current = -1;
    out->map_count = PAUSE_MAP_SPACES < PM3DS_HUD_MAX_SPACES ? PAUSE_MAP_SPACES : PM3DS_HUD_MAX_SPACES;
    for (s32 i = 0; i < out->map_count; i++) {
        out->map_x[i] = PauseMapSpaces[i].pos.x;
        out->map_y[i] = PauseMapSpaces[i].pos.y;
        out->map_visited[i] = evt_get_variable(nullptr, GF_MAP_ToadTown + i) != 0;
        if (PauseMapSpaces[i].id == here) {
            out->map_current = i;
        }
    }
}

/* ---- art for the bottom-screen menu ----------------------------------------- */

#define IMG(name) extern u8 name[]
#define PAL(name) extern u16 name[]
IMG(ui_stat_heart_png); IMG(ui_stat_flower_png);
IMG(ui_status_coin_0_png); PAL(ui_status_coin_0_pal);
IMG(ui_status_star_point_0_png); PAL(ui_status_star_point_0_pal);
IMG(ui_pause_stat_star_piece0_png); PAL(ui_pause_stat_star_piece0_pal);
IMG(ui_pause_stat_bp_png); PAL(ui_pause_stat_bp_pal);
IMG(ui_pause_stat_mario_png); PAL(ui_pause_stat_mario_pal);
IMG(ui_pause_stat_time_png); PAL(ui_pause_stat_time_pal);
IMG(ui_status_pow_star_1_png); PAL(ui_status_pow_star_1_pal);
IMG(ui_status_pow_star_empty_png); PAL(ui_status_pow_star_empty_pal);
IMG(ui_pause_label_stats_png); PAL(ui_pause_label_stats_pal);
IMG(ui_pause_label_map_png); PAL(ui_pause_label_map_pal);
IMG(ui_pause_label_party_png); PAL(ui_pause_label_party_pal);
IMG(ui_pause_map_walk0_png); PAL(ui_pause_map_walk0_pal);
IMG(ui_pause_map_walk1_png); PAL(ui_pause_map_walk1_pal);
IMG(pause_map_location_png);
IMG(ui_box_corners1_png);
IMG(pause_world_map_png); PAL(pause_world_map_pal);
#define PARTNER_ART(n) IMG(ui_##n##_png); PAL(ui_##n##_pal); PAL(ui_##n##_disabled_pal)
PARTNER_ART(goombario); PARTNER_ART(kooper); PARTNER_ART(bombette); PARTNER_ART(parakarry);
PARTNER_ART(bow); PARTNER_ART(watt); PARTNER_ART(sushie); PARTNER_ART(lakilester);

/* pause-menu order; partner ids 1 2 3 4 9 6 7 8 */
static const struct { const u8* img; const u16* pal; const u16* palDisabled; } sPartnerArt[PM3DS_HUD_PARTNERS] = {
    { ui_goombario_png, ui_goombario_pal, ui_goombario_disabled_pal },
    { ui_kooper_png, ui_kooper_pal, ui_kooper_disabled_pal },
    { ui_bombette_png, ui_bombette_pal, ui_bombette_disabled_pal },
    { ui_parakarry_png, ui_parakarry_pal, ui_parakarry_disabled_pal },
    { ui_bow_png, ui_bow_pal, ui_bow_disabled_pal },
    { ui_watt_png, ui_watt_pal, ui_watt_disabled_pal },
    { ui_sushie_png, ui_sushie_pal, ui_sushie_disabled_pal },
    { ui_lakilester_png, ui_lakilester_pal, ui_lakilester_disabled_pal },
};

int pm3ds_ui_get_image(int id, pm3ds_ui_image_t* out) {
#define SET(i, p, w_, h_, f) do { out->img = (i); out->pal = (p); out->w = (w_); out->h = (h_); out->fmt = (f); } while (0)
    switch (id) {
        case UI_IMG_HEART:        SET(ui_stat_heart_png, nullptr, 16, 16, UI_FMT_RGBA32); break;
        case UI_IMG_FLOWER:       SET(ui_stat_flower_png, nullptr, 16, 16, UI_FMT_RGBA32); break;
        case UI_IMG_COIN:         SET(ui_status_coin_0_png, ui_status_coin_0_pal, 16, 16, UI_FMT_CI4); break;
        case UI_IMG_STAR_POINT:   SET(ui_status_star_point_0_png, ui_status_star_point_0_pal, 16, 16, UI_FMT_CI4); break;
        case UI_IMG_STAR_PIECE:   SET(ui_pause_stat_star_piece0_png, ui_pause_stat_star_piece0_pal, 16, 16, UI_FMT_CI4); break;
        case UI_IMG_BP:           SET(ui_pause_stat_bp_png, ui_pause_stat_bp_pal, 16, 16, UI_FMT_CI4); break;
        case UI_IMG_MARIO:        SET(ui_pause_stat_mario_png, ui_pause_stat_mario_pal, 16, 16, UI_FMT_CI4); break;
        case UI_IMG_TIME:         SET(ui_pause_stat_time_png, ui_pause_stat_time_pal, 16, 16, UI_FMT_CI4); break;
        case UI_IMG_POW_STAR:     SET(ui_status_pow_star_1_png, ui_status_pow_star_1_pal, 8, 8, UI_FMT_CI4); break;
        case UI_IMG_POW_EMPTY:    SET(ui_status_pow_star_empty_png, ui_status_pow_star_empty_pal, 8, 8, UI_FMT_CI4); break;
        case UI_IMG_LABEL_STATS:  SET(ui_pause_label_stats_png, ui_pause_label_stats_pal, 48, 16, UI_FMT_CI4); break;
        case UI_IMG_LABEL_MAP:    SET(ui_pause_label_map_png, ui_pause_label_map_pal, 48, 16, UI_FMT_CI4); break;
        case UI_IMG_LABEL_PARTY:  SET(ui_pause_label_party_png, ui_pause_label_party_pal, 48, 16, UI_FMT_CI4); break;
        case UI_IMG_MAP_WALK0:    SET(ui_pause_map_walk0_png, ui_pause_map_walk0_pal, 24, 24, UI_FMT_CI4); break;
        case UI_IMG_MAP_WALK1:    SET(ui_pause_map_walk1_png, ui_pause_map_walk1_pal, 24, 24, UI_FMT_CI4); break;
        case UI_IMG_MAP_LOCATION: SET(pause_map_location_png, nullptr, 16, 16, UI_FMT_IA8); break;
        case UI_IMG_CORNERS:      SET(ui_box_corners1_png, nullptr, 16, 64, UI_FMT_IA8); break;
        case UI_IMG_WORLD_MAP:    SET(pause_world_map_png, pause_world_map_pal, 320, 320, UI_FMT_CI8); break;
        default:
            if (id >= UI_IMG_PARTNER_FIRST && id < UI_IMG_PARTNER_FIRST + PM3DS_HUD_PARTNERS) {
                s32 k = id - UI_IMG_PARTNER_FIRST;
                SET(sPartnerArt[k].img, sPartnerArt[k].pal, 32, 32, UI_FMT_CI4);
            } else if (id >= UI_IMG_PARTNER_DISABLED_FIRST && id < UI_IMG_PARTNER_DISABLED_FIRST + PM3DS_HUD_PARTNERS) {
                s32 k = id - UI_IMG_PARTNER_DISABLED_FIRST;
                SET(sPartnerArt[k].img, sPartnerArt[k].palDisabled, 32, 32, UI_FMT_CI4);
            } else {
                return 0;
            }
            break;
    }
#undef SET
    return 1;
}

extern u8 MsgCharImgNormal[];
extern u8 MsgCharWidthNormal[];
extern u16 D_802F4560[80][8];

int pm3ds_ui_get_font(pm3ds_ui_font_t* out) {
    /* loaded from ROM by load_font(); 'A' (code 0x21) is blank until then */
    const u8* a = MsgCharImgNormal + 0x21 * 128;
    s32 ink = 0;
    for (s32 i = 0; i < 128; i++) {
        ink |= a[i];
    }
    if (!ink) {
        return 0;
    }
    out->rasters = MsgCharImgNormal;
    out->widths = MsgCharWidthNormal;
    out->palettes = &D_802F4560[0][0];
    out->glyph_count = 0x5100 / 128;
    return 1;
}
