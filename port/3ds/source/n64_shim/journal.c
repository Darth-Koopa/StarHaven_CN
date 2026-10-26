/*
 * journal.c — game-side half of the bottom screen's Tattle log and item tracker.
 *
 * Tattle log: every enemy Goombario has tattled, from the save's tattle bits
 * (GB_Tattles_00.., one per actor type, as battles set them) with the name and
 * tattle text the game shows. Item tracker: per area, how many of the item
 * spots the randomizer knows (rando_locations.inc: field items, blocks, hidden
 * blocks, trees, bushes, hidden panels, chests) have been collected, from the
 * same flags the scripts set. Gifts have no flag and are left out. Recipes:
 * every dish Tayce T. can cook, every way to cook it, and which ones she has
 * cooked (her own discovered flags).
 *
 * Plain structs out (pm3ds_hud_info.h), so platform/bottom_screen.c needs no
 * decomp headers.
 */
#include "common.h"
#include "battle/battle.h"
#include "hud_element.h"
#include "map.h"

#include <string.h>

#include "pm3ds_hud_info.h"
#include "pm3ds_rando.h"

extern s32 bActorTattles[];
void pm3ds_decode_msg(s32 msgID, char* out, s32 size, bool dialogue);   /* touch_menu.c */

/* ---- tattle log ------------------------------------------------------------ */
#define NUM_ACTOR_TYPES (ACTOR_TYPE_SLOT_MACHINE_START_DUP3 + 1)

/* types whose tattle is its own message but with the same name and (up to
 * spaces and line breaks) the same text as another type: the flying
 * Magikoopas and the boss's copies. They log under the base type (GitHub #55). */
static const u8 sTattleAlias[][2] = {
    { ACTOR_TYPE_FLYING_MAGIKOOPA,        ACTOR_TYPE_MAGIKOOPA },
    { ACTOR_TYPE_FLYING_MAGICLONE,        ACTOR_TYPE_MAGICLONE },
    { ACTOR_TYPE_FLYING_RED_MAGIKOOPA,    ACTOR_TYPE_RED_MAGIKOOPA },
    { ACTOR_TYPE_FLYING_GREEN_MAGIKOOPA,  ACTOR_TYPE_GREEN_MAGIKOOPA },
    { ACTOR_TYPE_FLYING_YELLOW_MAGIKOOPA, ACTOR_TYPE_YELLOW_MAGIKOOPA },
    { ACTOR_TYPE_FLYING_GRAY_MAGIKOOPA,   ACTOR_TYPE_GRAY_MAGIKOOPA },
    { ACTOR_TYPE_FLYING_WHITE_MAGIKOOPA,  ACTOR_TYPE_WHITE_MAGIKOOPA },
    { ACTOR_TYPE_FLYING_MAGIKOOPA_BOSS,   ACTOR_TYPE_MAGIKOOPA_BOSS },
    { ACTOR_TYPE_MAGIKOOPA_DUP2,          ACTOR_TYPE_MAGIKOOPA_BOSS },
};

static s32 tattle_base_type(s32 t) {
    for (u32 i = 0; i < ARRAY_COUNT(sTattleAlias); i++) {
        if (sTattleAlias[i][0] == t) {
            return sTattleAlias[i][1];
        }
    }
    return t;
}

/* the message a type is logged under */
static s32 tattle_msg(s32 t) {
    return bActorTattles[tattle_base_type(t)];
}

/* one entry per tattle message (types that share one, share the entry), in
 * actor-type order */
static u8 sEntryType[NUM_ACTOR_TYPES];
static s32 sNumEntries = -1;
static char sEntryName[NUM_ACTOR_TYPES][PM3DS_TATTLE_NAME];

static void list_entries(void) {
    sNumEntries = 0;
    for (s32 t = 0; t < NUM_ACTOR_TYPES; t++) {
        s32 msg = tattle_msg(t);
        bool dup = false;
        if (msg == MSG_NONE || msg == 0) {
            continue;
        }
        for (s32 e = 0; e < sNumEntries && !dup; e++) {
            dup = tattle_msg(sEntryType[e]) == msg;
        }
        if (!dup) {
            sEntryType[sNumEntries++] = tattle_base_type(t);
        }
    }
}

static bool type_tattled(s32 t) {
    s32 bits = get_global_byte(EVT_INDEX_OF_GAME_BYTE(GB_Tattles_00) + t / 8);
    return (bits >> (t % 8)) & 1;
}

/* every type with this entry's message counts */
static bool entry_known(s32 e) {
    s32 msg = tattle_msg(sEntryType[e]);
    for (s32 t = 0; t < NUM_ACTOR_TYPES; t++) {
        if (tattle_msg(t) == msg && type_tattled(t)) {
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
enum { SRC_ITEM, SRC_BLOCK, SRC_TREE, SRC_BUSH, SRC_PANEL, SRC_CHEST, SRC_GIFT, SRC_HIDDEN_BLOCK,   /* rando.c */
       SRC_CRATE, SRC_SUPER_BLOCK };                                                                  /* tracker only */

typedef struct {
    s32 flag;
    s16 item;
    u8 src;
    const char* map;
} TrackedSpot;

#define LOC(flag, item, src, map, kav, chapter) { flag, item, src, map },
static const TrackedSpot sSpots[] = {
#if VERSION_US
#include "rando_locations_us.inc"
#elif VERSION_JP
#include "rando_locations_jp.inc"
#elif VERSION_IQUE
#include "rando_locations_ique.inc"
#else
#include "rando_locations.inc"
#endif
    /* not randomizer spots, but things to find all the same: items in crates
     * (GitHub #44; the obk_05 apples have no flag and come back) and the Super
     * Blocks (GitHub #43) */
    { GF_KPA17_Crate_TastyTonic, ITEM_TASTY_TONIC, SRC_CRATE, "kpa_17" },
    { GF_KPA17_Crate_LifeShroom, ITEM_LIFE_SHROOM, SRC_CRATE, "kpa_17" },
    { GF_OBK03_Crate_SuperShroom, ITEM_SUPER_SHROOM, SRC_CRATE, "obk_03" },
    { GF_OBK04_Crate_MapleSyrup, ITEM_MAPLE_SYRUP, SRC_CRATE, "obk_04" },
    { GF_OBK06_Crate_StarPiece, ITEM_STAR_PIECE, SRC_CRATE, "obk_06" },
    { GF_TIK07_SuperBlock, ITEM_NONE, SRC_SUPER_BLOCK, "tik_07" },
    { GF_TIK10_SuperBlock, ITEM_NONE, SRC_SUPER_BLOCK, "tik_10" },
    { GF_TIK02_SuperBlock, ITEM_NONE, SRC_SUPER_BLOCK, "tik_12" },
    { GF_TIK17_SuperBlock, ITEM_NONE, SRC_SUPER_BLOCK, "tik_17" },
    { GF_TIK19_SuperBlock, ITEM_NONE, SRC_SUPER_BLOCK, "tik_19" },
    { GF_IWA10_SuperBlock, ITEM_NONE, SRC_SUPER_BLOCK, "iwa_10" },
    { GF_SBK56_SuperBlock, ITEM_NONE, SRC_SUPER_BLOCK, "sbk_56" },
    { GF_ISK10_SuperBlock, ITEM_NONE, SRC_SUPER_BLOCK, "isk_10" },
    { GF_DGB04_SuperBlock, ITEM_NONE, SRC_SUPER_BLOCK, "dgb_04" },
    { GF_OMO11_SuperBlock, ITEM_NONE, SRC_SUPER_BLOCK, "omo_11" },
    { GF_JAN08_SuperBlock, ITEM_NONE, SRC_SUPER_BLOCK, "jan_08" },
    { GF_KZN04_SuperBlock, ITEM_NONE, SRC_SUPER_BLOCK, "kzn_04" },
    { GF_KZN09_SuperBlock, ITEM_NONE, SRC_SUPER_BLOCK, "kzn_09" },
    { GF_FLO08_SuperBlock, ITEM_NONE, SRC_SUPER_BLOCK, "flo_08" },
    { GF_FLO16_SuperBlock, ITEM_NONE, SRC_SUPER_BLOCK, "flo_16" },
    { GF_SAM08_SuperBlock, ITEM_NONE, SRC_SUPER_BLOCK, "sam_08" },
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

/* spots whose map clears their flag again, so the item comes back (GitHub #41):
 * they never stay found, so they don't count */
static const s32 sRegrowing[] = {
    GF_KMR02_Tree1_Goomnut, GF_NOK01_Bush4_KoopaLeaf, GF_NOK02_Bush1_KoopaLeaf,
    GF_SAM02_Item_IcedPotato, GF_SBK56_Tree1_Lemon, GF_SBK56_Tree2_Lime,
};

static bool regrows(s32 flag) {
    for (u32 i = 0; i < ARRAY_COUNT(sRegrowing); i++) {
        if (sRegrowing[i] == flag) {
            return true;
        }
    }
    return false;
}

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
            if (s->flag == 0 || strncmp(s->map, sTrackedAreas[a].prefix, 3) != 0 || regrows(s->flag)) {
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
            } else if (s->item != ITEM_NONE && (gItemTable[s->item].typeFlags & ITEM_TYPE_FLAG_BADGE)) {
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

/* ---- recipes --------------------------------------------------------------- */
/* Tayce T.'s tables (src/world/area_mac/mac_02/npc/tayce_t.inc.c, recipes.inc.c);
 * the game sets one discovered flag per dish it hands out */
typedef struct { s32 quality, itemID; } RecipeResult;
typedef struct { s32 inputA, inputB, output; } RecipePair;
extern s32 CookingIngredients[];
extern RecipeResult CookingResults[];
extern s8 SingleRecipesWithoutCookbook[], SingleRecipesWithCookbook[], DoubleRecipesMatrix[];
extern RecipePair ExtraDoubleRecipes[];
extern s32 MysteryResultOptions[];
extern const s32 CookableItemIDs[], CookableDiscoveredFlags[];
extern const s32 pm3ds_RecipeTableSizes[];
enum { RT_INGREDIENTS, RT_RESULTS, RT_EXTRAS, RT_MYSTERY, RT_COOKABLE };

static bool recipe_mystery(s32 item) {
    for (s32 i = 0; i < pm3ds_RecipeTableSizes[RT_MYSTERY]; i++) {
        if (MysteryResultOptions[i] == item) {
            return true;
        }
    }
    return false;
}

/* calls add(a, b, cookbook, ctx) for every way to cook item; b = ITEM_NONE: on its own */
static s32 recipe_ways(s32 item, void (*add)(s32 a, s32 b, bool cookbook, void* ctx), void* ctx) {
    s32 n = pm3ds_RecipeTableSizes[RT_INGREDIENTS], ways = 0;
    for (s32 i = 0; i < n; i++) {
        bool plain = CookingResults[SingleRecipesWithoutCookbook[i]].itemID == item && SingleRecipesWithoutCookbook[i] != 0;
        bool book = CookingResults[SingleRecipesWithCookbook[i]].itemID == item && SingleRecipesWithCookbook[i] != 0;
        if (plain || book) {
            if (add) add(CookingIngredients[i], ITEM_NONE, !plain, ctx);
            ways++;
        }
    }
    for (s32 a = 0; a < n; a++) {
        for (s32 b = a; b < n; b++) {
            s32 r = DoubleRecipesMatrix[b + a * n];
            if (r > 0 && CookingResults[r].itemID == item) {
                if (add) add(CookingIngredients[a], CookingIngredients[b], true, ctx);
                ways++;
            }
        }
    }
    for (s32 i = 0; i < pm3ds_RecipeTableSizes[RT_EXTRAS]; i++) {
        if (CookingResults[ExtraDoubleRecipes[i].output].itemID == item) {
            if (add) add(ExtraDoubleRecipes[i].inputA, ExtraDoubleRecipes[i].inputB, true, ctx);
            ways++;
        }
    }
    return ways;
}

static const void* item_hud_script(s32 item) {
    extern IconHudScriptPair gItemHudScripts[];
    return gItemHudScripts[gItemTable[item].hudElemID].enabled;
}

static s32 sRecipeItem[PM3DS_RECIPE_MAX], sRecipeFlag[PM3DS_RECIPE_MAX];
static char sRecipeName[PM3DS_RECIPE_MAX][PM3DS_RECIPE_NAME];
static s32 sNumRecipes = -1;

/* the dishes that can be cooked at all (a few discovered flags never get set),
 * sorted by name */
static void list_recipes(void) {
    sNumRecipes = 0;
    for (s32 i = 0; i < pm3ds_RecipeTableSizes[RT_COOKABLE] && sNumRecipes < PM3DS_RECIPE_MAX; i++) {
        s32 item = CookableItemIDs[i];
        if (item != ITEM_MISTAKE && recipe_ways(item, nullptr, nullptr) == 0 && !recipe_mystery(item)) {
            continue;
        }
        char name[PM3DS_RECIPE_NAME];
        pm3ds_decode_msg(gItemTable[item].nameMsg, name, sizeof(name), false);
        s32 k = sNumRecipes++;
        while (k > 0 && strcmp(sRecipeName[k - 1], name) > 0) {
            sRecipeItem[k] = sRecipeItem[k - 1];
            sRecipeFlag[k] = sRecipeFlag[k - 1];
            memcpy(sRecipeName[k], sRecipeName[k - 1], PM3DS_RECIPE_NAME);
            k--;
        }
        sRecipeItem[k] = item;
        sRecipeFlag[k] = CookableDiscoveredFlags[i];
        memcpy(sRecipeName[k], name, PM3DS_RECIPE_NAME);
    }
}

int pm3ds_recipe_query(pm3ds_recipe_list_t* out) {
    out->count = 0;
    out->cooked = 0;
    if (gGameStatusPtr == nullptr) {
        return 0;
    }
    if (sNumRecipes < 0) {
        list_recipes();
    }
    for (s32 i = 0; i < sNumRecipes; i++) {
        out->item[i] = sRecipeItem[i];
        out->done[i] = evt_get_variable(nullptr, sRecipeFlag[i]) != 0;
        out->icon[i] = item_hud_script(sRecipeItem[i]);
        memcpy(out->name[i], sRecipeName[i], PM3DS_RECIPE_NAME);
        out->cooked += out->done[i];
    }
    out->count = sNumRecipes;
    return out->count;
}

/* the detail's ingredient slot for item, added on first use */
static s16 sIngrItem[PM3DS_RECIPE_INGR];

static s32 recipe_ingr(pm3ds_recipe_detail_t* d, s32 item) {
    for (s32 i = 0; i < d->ingr_count; i++) {
        if (sIngrItem[i] == item) {
            return i;
        }
    }
    if (d->ingr_count >= PM3DS_RECIPE_INGR) {
        return -1;
    }
    s32 i = d->ingr_count++;
    sIngrItem[i] = item;
    pm3ds_decode_msg(gItemTable[item].nameMsg, d->ingr_name[i], PM3DS_RECIPE_NAME, false);
    d->ingr_icon[i] = item_hud_script(item);
    return i;
}

static void recipe_add_way(s32 a, s32 b, bool cookbook, void* ctx) {
    pm3ds_recipe_detail_t* d = ctx;
    if (d->ways >= PM3DS_RECIPE_WAYS) {
        return;
    }
    s32 ia = recipe_ingr(d, a), ib = b == ITEM_NONE ? -1 : recipe_ingr(d, b);
    if (ia < 0 || (b != ITEM_NONE && ib < 0)) {
        return;
    }
    d->way[d->ways].a = ia;
    d->way[d->ways].b = ib;
    d->way[d->ways].cookbook = cookbook;
    d->way[d->ways].luck = a == ITEM_MYSTERY;
    d->ways++;
}

void pm3ds_recipe_detail(int item, pm3ds_recipe_detail_t* out) {
    memset(out, 0, sizeof(*out));
    if (gGameStatusPtr == nullptr || item < ITEM_FIRST_CONSUMABLE || item > ITEM_LAST_CONSUMABLE) {
        return;
    }
    out->hp = gItemTable[item].potencyA;
    out->fp = gItemTable[item].potencyB;
    pm3ds_decode_msg(gItemTable[item].fullDescMsg, out->desc, sizeof(out->desc), false);
    recipe_ways(item, recipe_add_way, out);
    if (recipe_mystery(item)) {
        recipe_add_way(ITEM_MYSTERY, ITEM_NONE, false, out);
    }
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
    for (s32 i = 0; i < pm3ds_RecipeTableSizes[RT_COOKABLE]; i += 3) {   /* every third dish cooked */
        evt_set_variable(nullptr, CookableDiscoveredFlags[i], true);
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
