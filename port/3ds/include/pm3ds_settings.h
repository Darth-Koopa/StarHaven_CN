#ifndef PM3DS_SETTINGS_H
#define PM3DS_SETTINGS_H
/* Player options, kept in <SD folder>/port.cfg (platform/pm3ds_settings.c). */
#include <stdbool.h>
#include <stdint.h>

/* what a 3DS button does */
enum {
    ACT_NONE,
    ACT_A, ACT_B, ACT_Z, ACT_R, ACT_L, ACT_START,
    ACT_C_UP, ACT_C_DOWN, ACT_C_LEFT, ACT_C_RIGHT,
    ACT_FAST_FORWARD,
    ACT_COUNT
};

/* the 3DS buttons that can be mapped, in the order the remap page lists them */
enum {
    KEY_IDX_A, KEY_IDX_B, KEY_IDX_X, KEY_IDX_Y, KEY_IDX_L, KEY_IDX_R, KEY_IDX_ZL, KEY_IDX_ZR,
    KEY_IDX_START, KEY_IDX_SELECT, KEY_IDX_DUP, KEY_IDX_DDOWN, KEY_IDX_DLEFT, KEY_IDX_DRIGHT,
    KEY_IDX_CUP, KEY_IDX_CDOWN, KEY_IDX_CLEFT, KEY_IDX_CRIGHT,
    KEY_IDX_COUNT
};

enum { TEXT_NORMAL, TEXT_FAST, TEXT_INSTANT, TEXT_SPEEDS };

/* 3D depth: how far the 3D slider at its top pushes the background back */
#define DEPTH3D_LEVELS 5
#define DEPTH3D_DEFAULT 1   /* the depth of 1.0.0 to 1.0.4 */
extern const float pm3ds_depth3d_shift[DEPTH3D_LEVELS];   /* pixels per eye at infinity */
extern const char *const pm3ds_depth3d_names[DEPTH3D_LEVELS];

typedef struct {
    int view_mode;              /* 0 Original, 1 Fill, 2 Wide */
    bool fps60;                 /* in-between frames (n64_shim/gfx_interp.c) */
    int depth3d;                /* 0..DEPTH3D_LEVELS-1 */
    int text_speed;             /* TEXT_* */
    int ff_speed;               /* game steps per frame while fast-forwarding: 2..4 */
    bool ff_toggle;             /* the fast-forward button toggles instead of being held */
    unsigned rando_flags;       /* the new-game picker's last choice (pm3ds_rando.h; ON clear: classic) */
    uint8_t keymap[KEY_IDX_COUNT];   /* ACT_* per 3DS button */
    bool update_check;          /* ask GitHub for a newer release at launch (pm3ds_update.c) */
    int update_skip;            /* a release the player said not to mention again (version code) */
    bool map_dragged;           /* the player has dragged the bottom-screen map, so "Drag to look" is not shown */
} pm3ds_settings_t;

extern pm3ds_settings_t pm3ds_cfg;

void pm3ds_settings_load(void);
void pm3ds_settings_save(void);
void pm3ds_settings_default_keymap(uint8_t *map);
uint32_t pm3ds_settings_key_bit(int key_idx);       /* KEY_* of libctru */
const char *pm3ds_settings_key_name(int key_idx);
const char *pm3ds_settings_action_name(int act);

/* fast-forward: held (or toggled) via a mapped button or the bottom-screen button */
extern bool pm3ds_ff_button_on;     /* touch toggle */
void pm3ds_ff_poll(void);           /* once per game step */
bool pm3ds_fast_forward(void);

#endif
