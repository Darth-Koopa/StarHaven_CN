/* pm3ds_touch_menu.h — the game's open list menus, mirrored on the bottom
 * screen so they can be tapped (n64_shim/touch_menu.c fills it from the
 * battle menus and the popup menu; platform/bottom_screen.c draws it).
 * Plain C: the bottom screen needs no decomp headers. */
#ifndef PM3DS_TOUCH_MENU_H
#define PM3DS_TOUCH_MENU_H

#include <stdint.h>

#define PM3DS_TM_MAX 32

enum {
    PM3DS_TM_NONE,
    PM3DS_TM_WHEEL,    /* battle: the main command reel (Jump, Hammer, Items, ...) */
    PM3DS_TM_MOVES,    /* battle: a move list (jump/hammer moves, partner abilities, star spirits) */
    PM3DS_TM_STRATS,   /* battle: Strategies */
    PM3DS_TM_POPUP,    /* item / partner lists, in battle and in the world */
    PM3DS_TM_TARGET,   /* battle: choosing a target */
};

enum { PM3DS_TM_COST_NONE, PM3DS_TM_COST_FP, PM3DS_TM_COST_SP, PM3DS_TM_COST_COINS, PM3DS_TM_COST_STAR_PIECES,
       PM3DS_TM_COST_HP };

typedef struct {
    char name[28];      /* the game's font codes + 0x20: ASCII for plain text */
    int16_t cost;       /* move cost, price; HP for a target (-1 unknown) */
    int16_t max_hp;     /* targets */
    uint8_t cost_kind;  /* PM3DS_TM_COST_* */
    uint8_t enabled;
    uint8_t attack;     /* WHEEL: opens Jump or Hammer (the bottom row of the tiles) */
    const void *icon;   /* the game's HudScript for the entry's icon, or NULL */
} pm3ds_tm_entry_t;

typedef struct {
    int kind;           /* PM3DS_TM_*; NONE when no menu is waiting for a choice */
    int count;
    int cursor;
    int all_targets;    /* TARGET: the move hits every target, any tap confirms */
    int partner;        /* the partner's turn (battle menus) */
    char title[28];
    pm3ds_tm_entry_t e[PM3DS_TM_MAX];
} pm3ds_touch_menu_t;

/* the bottom screen's side */
int pm3ds_tm_get(pm3ds_touch_menu_t *out);   /* the menu waiting for a choice now, or PM3DS_TM_NONE */
void pm3ds_tm_pick(int kind, int index);     /* choose entry index (as if moved there and A pressed) */
/* the first image of an entry's icon as RGBA8, row 0 on top, into out (32x32
 * at most, larger ones are shrunk): 1 with its size, 0 if it has none */
int pm3ds_tm_icon_rgba(const void *script, uint8_t *out, int *w, int *h);

/* N64 buttons and stick held from the bottom screen's pad, OR'd into the
 * controller (n64_shim/os_cont.c) */
extern uint16_t pm3ds_touch_pad;
extern int8_t pm3ds_touch_stick_x;

#endif
