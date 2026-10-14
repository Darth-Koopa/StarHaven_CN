/* pm3ds_hud_info.h — game state and art for the bottom-screen menu, as plain
 * C so the 3DS-side renderer (platform/bottom_screen.c) needs no decomp
 * headers. Filled by n64_shim/hud_info.c. */
#ifndef PM3DS_HUD_INFO_H
#define PM3DS_HUD_INFO_H

#include <stdint.h>

#define PM3DS_HUD_MAX_SPACES 40
#define PM3DS_HUD_PARTNERS   8

typedef struct {
    int in_game;          /* a save is loaded and the player is in the world/battle/pause */
    int in_battle;
    int demo;             /* attract demo running */
    int hp, max_hp, fp, max_fp, bp, level;
    int star_points, coins, star_pieces;
    int star_power, max_star_power;   /* star power in 1/256 bars */
    int chapter;          /* 0 prologue, 1..8, 9 epilogue */
    unsigned play_seconds;
    int partner;          /* PartnerIDs, 0 = none */
    unsigned partners_owned;          /* bit n = partner n has joined */
    char partner_name[16];
    char location[32];
    /* world map (pause menu data) */
    int map_count;
    int map_current;      /* index into map_x/map_y of Mario's location, -1 unknown */
    int16_t map_x[PM3DS_HUD_MAX_SPACES], map_y[PM3DS_HUD_MAX_SPACES];
    uint8_t map_visited[PM3DS_HUD_MAX_SPACES];
} pm3ds_hud_info_t;

void pm3ds_hud_query(pm3ds_hud_info_t *out);

/* N64 images the menu draws, decoded once into the UI atlas */
enum {
    UI_IMG_HEART, UI_IMG_FLOWER, UI_IMG_COIN, UI_IMG_STAR_POINT, UI_IMG_STAR_PIECE,
    UI_IMG_BP, UI_IMG_MARIO, UI_IMG_TIME, UI_IMG_POW_STAR, UI_IMG_POW_EMPTY,
    UI_IMG_LABEL_STATS, UI_IMG_LABEL_MAP, UI_IMG_LABEL_PARTY,
    UI_IMG_MAP_WALK0, UI_IMG_MAP_WALK1, UI_IMG_MAP_LOCATION, UI_IMG_CORNERS,
    UI_IMG_PARTNER_FIRST,                                    /* 8 portraits, PARTNER ids 1..8 */
    UI_IMG_PARTNER_DISABLED_FIRST = UI_IMG_PARTNER_FIRST + PM3DS_HUD_PARTNERS,
    UI_IMG_WORLD_MAP = UI_IMG_PARTNER_DISABLED_FIRST + PM3DS_HUD_PARTNERS,
    UI_IMG_COUNT
};

enum { UI_FMT_RGBA16, UI_FMT_RGBA32, UI_FMT_CI4, UI_FMT_CI8, UI_FMT_IA8, UI_FMT_I4 };

typedef struct {
    const uint8_t  *img;   /* big-endian N64 texels */
    const uint16_t *pal;   /* big-endian RGBA5551 palette for CI formats */
    int w, h, fmt;
} pm3ds_ui_image_t;

int pm3ds_ui_get_image(int id, pm3ds_ui_image_t *out);   /* 0 if unavailable */

/* the game's dialogue font: 16x16 CI4 glyphs, code = ASCII - 0x20 */
typedef struct {
    const uint8_t  *rasters;   /* 128 bytes per glyph */
    const uint8_t  *widths;    /* advance per glyph */
    const uint16_t *palettes;  /* 16-entry palettes, 8 entries apart */
    int glyph_count;
} pm3ds_ui_font_t;

int pm3ds_ui_get_font(pm3ds_ui_font_t *out);   /* 0 until the game has loaded it */

#endif
