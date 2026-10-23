/*
 * journal.c — game-side half of the bottom screen's Tattle log and item tracker.
 *
 * Tattle log: every enemy Goombario has tattled, from the save's tattle bits
 * (GB_Tattles_00.., one per actor type, as battles set them) with the name and
 * tattle text the game shows. Item tracker: per area, how many of the item
 * spots the randomizer knows (rando_locations.inc: field items, blocks, hidden
 * blocks, trees, bushes, hidden panels, chests) have been collected, from the
 * same flags the scripts set. Gifts have no flag and are left out.
 *
 * Plain structs out (pm3ds_hud_info.h), so platform/bottom_screen.c needs no
 * decomp headers.
 */
#include "common.h"
#include "battle/battle.h"
#include "map.h"

#include <string.h>

#include "pm3ds_hud_info.h"
#include "pm3ds_rando.h"

extern s32 bActorTattles[];
void pm3ds_decode_msg(s32 msgID, char* out, s32 size, bool dialogue);   /* touch_menu.c */

/* ---- tattle log ------------------------------------------------------------ */
#define NUM_ACTOR_TYPES (ACTOR_TYPE_SLOT_MACHINE_START_DUP3 + 1)

/* one entry per tattle message (types that share one, share the entry), in
 * actor-type order */
static u8 sEntryType[NUM_ACTOR_TYPES];
static s32 sNumEntries = -1;
static char sEntryName[NUM_ACTOR_TYPES][PM3DS_TATTLE_NAME];

static void list_entries(void) {
    sNumEntries = 0;
    for (s32 t = 0; t < NUM_ACTOR_TYPES; t++) {
        s32 msg = bActorTattles[t];
        bool dup = false;
        if (msg == MSG_NONE || msg == 0) {
            continue;
        }
        for (s32 e = 0; e < sNumEntries && !dup; e++) {
            dup = bActorTattles[sEntryType[e]] == msg;
        }
        if (!dup) {
            sEntryType[sNumEntries++] = t;
        }
    }
}

static bool type_tattled(s32 t) {
    s32 bits = get_global_byte(EVT_INDEX_OF_GAME_BYTE(GB_Tattles_00) + t / 8);
    return (bits >> (t % 8)) & 1;
}

/* every type with this entry's message counts */
static bool entry_known(s32 e) {
    s32 msg = bActorTattles[sEntryType[e]];
    for (s32 t = 0; t < NUM_ACTOR_TYPES; t++) {
        if (bActorTattles[t] == msg && type_tattled(t)) {
            return true;
        }
    }
    return false;
}

int pm3ds_tattle_query(pm3ds_tattle_list_t* out) {
    out->known = 0;
    out->total = 0;
    if (gGameStatusPtr == nullptr) {
        return 0;
    }
    if (sNumEntries < 0) {
        list_entries();
    }
    out->total = sNumEntries;
    for (s32 e = 0; e < sNumEntries && out->known < PM3DS_TATTLE_MAX; e++) {
        if (!entry_known(e)) {
            continue;
        }
        if (sEntryName[e][0] == '\0') {
            pm3ds_decode_msg(bActorNames[sEntryType[e]], sEntryName[e], PM3DS_TATTLE_NAME, false);
        }
        out->entry[out->known] = e;
        memcpy(out->name[out->known], sEntryName[e], PM3DS_TATTLE_NAME);
        out->known++;
    }
    return out->known;
}

void pm3ds_tattle_text(int entry, char* out, int size) {
    out[0] = '\0';
    if (entry >= 0 && entry < sNumEntries) {
        pm3ds_decode_msg(bActorTattles[sEntryType[entry]], out, size, true);
    }
}

/* ---- item tracker ---------------------------------------------------------- */
enum { SRC_ITEM, SRC_BLOCK, SRC_TREE, SRC_BUSH, SRC_PANEL, SRC_CHEST, SRC_GIFT, SRC_HIDDEN_BLOCK };   /* rando.c */

typedef struct {
    s32 flag;
    s16 item;
    u8 src;
    const char* map;
} TrackedSpot;

#define LOC(flag, item, src, map, kav, chapter) { flag, item, src, map },
static const TrackedSpot sSpots[] = {
#if VERSION_US || VERSION_IQUE
#include "rando_locations_us.inc"
#else
#include "rando_locations.inc"
#endif
};
#undef LOC

/* story order */
static const struct { const char* prefix; const char* name; } sTrackedAreas[] = {
    { "kmr", "Goomba Region" },   { "mac", "Toad Town" },        { "tik", "Toad Town Tunnels" },
    { "nok", "Koopa Region" },    { "trd", "Koopa Bros. Fortress" }, { "iwa", "Mt. Rugged" },
    { "dro", "Dry Dry Outpost" }, { "sbk", "Dry Dry Desert" },   { "isk", "Dry Dry Ruins" },
    { "mim", "Forever Forest" },  { "obk", "Boo's Mansion" },    { "arn", "Gusty Gulch" },
    { "dgb", "Tubba's Castle" },  { "omo", "Shy Guy's Toy Box" }, { "jan", "Lavalava Island" },
    { "kzn", "Mt. Lavalava" },    { "flo", "Flower Fields" },    { "sam", "Shiver Region" },
    { "pra", "Crystal Palace" },  { "kpa", "Bowser's Castle" },  { "kkj", "Peach's Castle" },
    { "hos", "Star Region" },     { "osr", "Castle Grounds" },
};

MapConfig* get_current_map_config(void);

int pm3ds_tracker_query(pm3ds_tracker_t* out) {
    memset(out, 0, sizeof(*out));
    out->here = -1;
    if (gGameStatusPtr == nullptr) {
        return 0;
    }
    out->rando = pm3ds_rando_seed() != 0;
    MapConfig* mc = gGameStatusPtr->areaID >= 0 ? get_current_map_config() : nullptr;
    const char* mapID = mc != nullptr ? mc->id : nullptr;
    for (u32 a = 0; a < ARRAY_COUNT(sTrackedAreas) && out->count < PM3DS_TRACKER_AREAS; a++) {
        pm3ds_tracker_area_t* r = &out->area[out->count];
        snprintf(r->name, sizeof(r->name), "%s", sTrackedAreas[a].name);
        for (u32 i = 0; i < ARRAY_COUNT(sSpots); i++) {
            const TrackedSpot* s = &sSpots[i];
            if (s->flag == 0 || strncmp(s->map, sTrackedAreas[a].prefix, 3) != 0) {
                continue;
            }
            bool got = evt_get_variable(nullptr, s->flag) != 0;
            if (mapID != nullptr && strcmp(s->map, mapID) == 0) {
                out->room_total++;
                out->room_found += got;
            }
            r->total++;
            r->found += got;
            if (s->item == ITEM_STAR_PIECE) {
                r->sp_total++;
                r->sp_found += got;
            } else if (gItemTable[s->item].typeFlags & ITEM_TYPE_FLAG_BADGE) {
                r->badge_total++;
                r->badge_found += got;
            }
            if (s->src == SRC_HIDDEN_BLOCK || s->src == SRC_PANEL) {
                r->hidden_total++;
                r->hidden_found += got;
            }
        }
        if (r->total == 0) {
            continue;
        }
        if (mapID != nullptr && strncmp(mapID, sTrackedAreas[a].prefix, 3) == 0) {
            out->here = out->count;
        }
        out->found += r->found;
        out->total += r->total;
        out->count++;
    }
    return out->count;
}

#ifdef PM3DS_SHOT_JOURNAL
/* debug builds only (README screenshots): a chapter-2 file's worth of tattles and
 * found item spots, so the Tattle and Found tabs have something to show */
void pm3ds_journal_debug_fill(void) {
    static const s32 types[] = { ACTOR_TYPE_GOOMBA, ACTOR_TYPE_PARAGOOMBA, ACTOR_TYPE_SPIKED_GOOMBA,
        ACTOR_TYPE_RED_GOOMBA, ACTOR_TYPE_FUZZY, ACTOR_TYPE_KOOPA_TROOPA, ACTOR_TYPE_PARATROOPA,
        ACTOR_TYPE_BOB_OMB, ACTOR_TYPE_BULLET_BILL, ACTOR_TYPE_BILL_BLASTER, ACTOR_TYPE_CLEFT,
        ACTOR_TYPE_MONTY_MOLE, ACTOR_TYPE_BANDIT, ACTOR_TYPE_POKEY, ACTOR_TYPE_SWOOPER,
        ACTOR_TYPE_BUZZY_BEETLE };
    static const char* areas[] = { "kmr", "mac", "nok", "trd", "iwa", "dro", "sbk" };
    for (u32 k = 0; k < ARRAY_COUNT(types); k++) {
        s32 i = EVT_INDEX_OF_GAME_BYTE(GB_Tattles_00) + types[k] / 8;
        set_global_byte(i, get_global_byte(i) | (1 << (types[k] % 8)));
    }
    for (u32 i = 0, n = 0; i < ARRAY_COUNT(sSpots); i++) {
        for (u32 a = 0; a < ARRAY_COUNT(areas); a++) {
            if (sSpots[i].flag != 0 && strncmp(sSpots[i].map, areas[a], 3) == 0 && (n++ % 4) != 3) {
                evt_set_variable(nullptr, sSpots[i].flag, true);
            }
        }
    }
}
#endif
