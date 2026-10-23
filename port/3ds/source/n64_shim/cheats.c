/*
 * cheats.c — Star Haven's cheats.
 *
 * A save allows them or not, chosen once on the new-game picker
 * (platform/bottom_screen.c) and never changed after, so a normal save can
 * never turn them on. A save that
 * allows them gets a Cheats page on the Options rail with a switch per cheat
 * (pm3ds_cheats.h). Both live in the save file's unused tail next to the
 * randomizer's block (SaveData.unk_1304 + 0x10, inside the CRC), so they
 * stay with the save.
 *
 * Applied once per game step (src/main_loop.c): HP, FP, star power and coins
 * are topped up. Damage that would knock Mario out in the same step is caught
 * where the battle subtracts it (src/battle/1A5830.c, dmg_player.c), and the
 * badge menu skips its BP check (src/pause/pause_badges.c).
 */
#include "common.h"

#include <string.h>

#include "pm3ds_cheats.h"

#define CHEAT_MAGIC 0x53484348   /* "SHCH" */
#define CHEAT_BLOCK_OFS 0x10     /* after rando.c's block */

typedef struct {
    s32 magic;
    s32 version;
    s32 allowed;
    s32 on;        /* bit per PM3DS_CHEAT_* */
} CheatSaveBlock;

static bool sAllowed, sNextAllowed;
static u32 sOn;

void pm3ds_cheats_choose(bool allowed) {
    sNextAllowed = allowed;
}

bool pm3ds_cheats_allowed(void) {
    return sAllowed;
}

bool pm3ds_cheat_on(int cheat) {
    return sAllowed && (sOn >> cheat) & 1;
}

void pm3ds_cheat_toggle(int cheat) {
    if (sAllowed && cheat >= 0 && cheat < PM3DS_CHEAT_COUNT) {
        sOn ^= 1u << cheat;
    }
}

/* src/vars_access.c, clear_saved_variables: a fresh state has none */
void pm3ds_cheats_off(void) {
    sAllowed = false;
    sOn = 0;
}

/* src/filemenu/filemenu_yesno.c, new file: the picker's choice */
void pm3ds_cheats_new_game(void) {
    sAllowed = sNextAllowed;
    sNextAllowed = false;   /* used once, so it can't carry over to another file */
    sOn = 0;
}

/* src/fio.c */
void pm3ds_cheats_serialize(void* tail) {
    CheatSaveBlock b = { CHEAT_MAGIC, 1, sAllowed, (s32)sOn };
    memcpy((u8*)tail + CHEAT_BLOCK_OFS, &b, sizeof(b));
}

void pm3ds_cheats_deserialize(const void* tail) {
    CheatSaveBlock b;
    memcpy(&b, (const u8*)tail + CHEAT_BLOCK_OFS, sizeof(b));
    pm3ds_cheats_off();
    if (b.magic == CHEAT_MAGIC && b.version == 1 && b.allowed) {
        sAllowed = true;
        sOn = (u32)b.on & ((1u << PM3DS_CHEAT_COUNT) - 1);
    }
}

/* src/main_loop.c, every game step */
void pm3ds_cheats_step(void) {
    PlayerData* playerData = &gPlayerData;
    if (!sAllowed || sOn == 0) {
        return;
    }
    if (pm3ds_cheat_on(PM3DS_CHEAT_HP) && playerData->curHP < playerData->curMaxHP) {
        playerData->curHP = playerData->curMaxHP;
    }
    if (pm3ds_cheat_on(PM3DS_CHEAT_FP) && playerData->curFP < playerData->curMaxFP) {
        playerData->curFP = playerData->curMaxFP;
    }
    if (pm3ds_cheat_on(PM3DS_CHEAT_SP) && playerData->starPower < playerData->maxStarPower * SP_PER_BAR) {
        playerData->starPower = playerData->maxStarPower * SP_PER_BAR;
    }
    if (pm3ds_cheat_on(PM3DS_CHEAT_COINS) && playerData->coins < 999) {
        playerData->coins = 999;
    }
}

/* src/pause/pause_badges.c: BP a badge may use */
s32 pm3ds_cheats_badge_bp(s32 bp) {
    return pm3ds_cheat_on(PM3DS_CHEAT_BP) ? 999 : bp;
}

/* src/battle: the player's HP after a hit, kept full (never 0, so no KO) */
s32 pm3ds_cheats_player_hp(s32 hp) {
    return pm3ds_cheat_on(PM3DS_CHEAT_HP) ? gPlayerData.curMaxHP : hp;
}
