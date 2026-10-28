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

/* Hard mode (GitHub #69), also chosen on the new-game picker and kept with the
 * save: enemies have 1.5x HP and hit 1.5x as hard, and drop 1.5x the coins
 * (rounded up); Mario has action commands and guarding from the start. Its own
 * block in the tail, after keyitems.c's. */
#define HARD_MAGIC 0x53484844      /* "SHHD" */
#define HARD_BLOCK_OFS 0x74
static bool sHard, sNextHard;

void pm3ds_hard_choose(bool on) {
    sNextHard = on;
}

/* Skip intro (Discord suggestion), also from the new-game picker: the new file starts
 * where Mario lands after falling off the cliff by Goompa's house, Goompa about to join */
static bool sNextSkipIntro;

void pm3ds_skip_intro_choose(bool on) {
    sNextSkipIntro = on;
}

/* src/filemenu/filemenu_yesno.c, a new file, after the game set its first map */
void pm3ds_new_game_start(void) {
    if (!sNextSkipIntro) {
        return;
    }
    sNextSkipIntro = false;
    gGameStatusPtr->areaID = AREA_KMR;
    gGameStatusPtr->mapID = 2;   // kmr_03
    gGameStatusPtr->entryID = 2; // falling in from the cliff above
    evt_set_variable(nullptr, GB_StoryProgress, STORY_CH0_FELL_OFF_CLIFF);
    evt_set_variable(nullptr, GF_KMR02_Met_Goompa, true);
    evt_set_variable(nullptr, GF_KMR02_Met_Goombario, true);
}

bool pm3ds_hard_mode(void) {
    return sHard;
}

/* src/battle/190B20.c (enemy HP), 1A5830.c (enemy damage), src/enemy_ai.c (coins) */
s32 pm3ds_hard_scale(s32 v) {
    return sHard && v > 0 ? (v * 3 + 1) / 2 : v;
}

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
    sHard = false;
}

/* src/filemenu/filemenu_yesno.c, new file: the picker's choice */
void pm3ds_cheats_new_game(void) {
    sAllowed = sNextAllowed;
    sNextAllowed = false;   /* used once, so it can't carry over to another file */
    sOn = 0;
    sHard = sNextHard;
    sNextHard = false;
    if (sHard) {
        // the tutorial fights can't be won at 1.5x damage without them (GitHub #74): action
        // commands and guarding from the start, as the Lucky Star gives them later
        gPlayerData.hasActionCommands = true;
    }
}

/* src/fio.c */
void pm3ds_cheats_serialize(void* tail) {
    CheatSaveBlock b = { CHEAT_MAGIC, 1, sAllowed, (s32)sOn };
    s32 hard[2] = { HARD_MAGIC, sHard };
    memcpy((u8*)tail + CHEAT_BLOCK_OFS, &b, sizeof(b));
    memcpy((u8*)tail + HARD_BLOCK_OFS, hard, sizeof(hard));
}

void pm3ds_cheats_deserialize(const void* tail) {
    CheatSaveBlock b;
    memcpy(&b, (const u8*)tail + CHEAT_BLOCK_OFS, sizeof(b));
    s32 hard[2];
    memcpy(hard, (const u8*)tail + HARD_BLOCK_OFS, sizeof(hard));
    pm3ds_cheats_off();
    sHard = hard[0] == HARD_MAGIC && hard[1] != 0;
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
