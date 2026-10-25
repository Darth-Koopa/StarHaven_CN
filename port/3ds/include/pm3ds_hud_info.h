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
    int ui_ready;         /* past the boot and logos: the ROM and the HUD graphics are there (page icons) */
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

/* Tattle log (n64_shim/journal.c): enemies Goombario has tattled, in actor order */
#define PM3DS_TATTLE_MAX  160
#define PM3DS_TATTLE_NAME 24
typedef struct {
    int known, total;                     /* tattled / enemies with a tattle */
    short entry[PM3DS_TATTLE_MAX];        /* for pm3ds_tattle_text */
    char name[PM3DS_TATTLE_MAX][PM3DS_TATTLE_NAME];
} pm3ds_tattle_list_t;
int pm3ds_tattle_query(pm3ds_tattle_list_t *out);
void pm3ds_tattle_text(int entry, char *out, int size);   /* whole tattle as one paragraph */

/* Item tracker (n64_shim/journal.c): collected item spots per area, story order */
#define PM3DS_TRACKER_AREAS 24
typedef struct {
    char name[24];
    unsigned char found, total;               /* every tracked spot */
    unsigned char sp_found, sp_total;         /* star pieces */
    unsigned char badge_found, badge_total;   /* badges */
    unsigned char hidden_found, hidden_total; /* hidden blocks and panels */
} pm3ds_tracker_area_t;
typedef struct {
    int count, found, total;
    int rando;                                /* spots hold shuffled items: only the totals mean anything */
    int here;                                 /* area[] index of Mario's area, -1 if it has no spots */
    int room_found, room_total;               /* spots in the map Mario is in */
    pm3ds_tracker_area_t area[PM3DS_TRACKER_AREAS];
} pm3ds_tracker_t;
int pm3ds_tracker_query(pm3ds_tracker_t *out);

/* Recipes (n64_shim/journal.c): every dish Tayce T. can cook, by name */
#define PM3DS_RECIPE_MAX  56
#define PM3DS_RECIPE_NAME 24
typedef struct {
    int count, cooked;
    short item[PM3DS_RECIPE_MAX];                 /* for pm3ds_recipe_detail */
    unsigned char done[PM3DS_RECIPE_MAX];         /* cooked at least once */
    const void *icon[PM3DS_RECIPE_MAX];           /* HudScript */
    char name[PM3DS_RECIPE_MAX][PM3DS_RECIPE_NAME];
} pm3ds_recipe_list_t;
int pm3ds_recipe_query(pm3ds_recipe_list_t *out);
/* one dish: what it does and every way to cook it */
#define PM3DS_RECIPE_WAYS  48
#define PM3DS_RECIPE_INGR  36
typedef struct {
    int hp, fp;                                   /* eating it restores */
    char desc[192];
    int ways;
    struct {
        short a, b;                               /* ingr[] indices; b -1: on its own */
        unsigned char cookbook;                   /* only once Tayce T. has the Cookbook */
        unsigned char luck;                       /* a cooked Mystery? (a) turns into it by luck */
    } way[PM3DS_RECIPE_WAYS];
    int ingr_count;
    char ingr_name[PM3DS_RECIPE_INGR][PM3DS_RECIPE_NAME];
    const void *ingr_icon[PM3DS_RECIPE_INGR];
} pm3ds_recipe_detail_t;
void pm3ds_recipe_detail(int item, pm3ds_recipe_detail_t *out);

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

/* icons the pages draw, as the game's HudScripts (decoded like the menus'
 * icons, so they need the ROM: only ask once a game is running) */
enum {
    UI_ICON_STAR_PIECE, UI_ICON_COMPASS, UI_ICON_SETTINGS, UI_ICON_CONTROLS,
    UI_ICON_DEBUG, UI_ICON_BOOTS, UI_ICON_HAMMER, UI_ICON_COOK, UI_ICON_CHEATS,
    UI_ICON_COUNT
};
const void *pm3ds_ui_icon_script(int id);

/* the game's dialogue font: 16x16 CI4 glyphs, code = ASCII - 0x20 */
typedef struct {
    const uint8_t  *rasters;   /* 128 bytes per glyph */
    const uint8_t  *widths;    /* advance per glyph */
    const uint16_t *palettes;  /* 16-entry palettes, 8 entries apart */
    int glyph_count;
} pm3ds_ui_font_t;

int pm3ds_ui_get_font(pm3ds_ui_font_t *out);   /* 0 until the game has loaded it */

#endif
