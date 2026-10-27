#ifndef PM3DS_CHEATS_H
#define PM3DS_CHEATS_H
/* Cheats (n64_shim/cheats.c): allowed per save, chosen on the new-game picker
 * (platform/bottom_screen.c), then switched one by one on the Options rail's
 * Cheats page. No decomp headers here. */
#include <stdbool.h>
#include <stdint.h>

enum {
    PM3DS_CHEAT_HP,       /* HP stays full, Mario can't be knocked out */
    PM3DS_CHEAT_FP,       /* FP stays full */
    PM3DS_CHEAT_SP,       /* star power stays full */
    PM3DS_CHEAT_COINS,    /* 999 coins */
    PM3DS_CHEAT_BP,       /* equip any badges, whatever their BP */
    PM3DS_CHEAT_COUNT,
};

void pm3ds_cheats_choose(bool allowed);   /* the next new game's choice */
bool pm3ds_cheats_allowed(void);          /* the running game's save allows cheats */
bool pm3ds_cheat_on(int cheat);
void pm3ds_cheat_toggle(int cheat);

/* Hard mode (n64_shim/cheats.c): per save, chosen on the new-game picker */
void pm3ds_hard_choose(bool on);
bool pm3ds_hard_mode(void);
int32_t pm3ds_hard_scale(int32_t v);   /* x1.5 rounded up in hard mode */

#endif
