/*
 * rando.c — Star Haven's item randomizer (normal story order).
 *
 * Locations (tools/rando_locations.py -> rando_locations*.inc): items lying
 * around, item blocks, tree/bush drops and hidden panels (each named by the
 * save flag that marks it taken), chests (their flag too), and NPC gifts
 * (named by map and vanilla item: they have no flag of their own).
 *
 * Settings (pm3ds_rando.h flags, picked per new game):
 *   on              consumable items (not the Flower Fields berries) and badges
 *   coins           + coins and star pieces
 *   keys            + key items, placed by the logic below
 *   shops           shops trade their stock with the world (below)
 *   gifts           NPC gifts join the pool
 *   hidden          key items may also go to hidden panels and hidden blocks
 * Gear, partners and star spirits always keep their places, and so do the
 * plain items the story asks for: the Flower Fields berries, Tayce T.'s
 * Cake (chapter 4) and Dry Dry Outpost's password goods (chapter 2).
 *
 * Shops (rando_shops*.inc): each coin shop slot trades its item with a
 * random world spot of the same kind -- consumables in the restocking shops,
 * badges in Rowf's one-time stock -- so shops sell what the world held and
 * the world holds the shops' stock. Prices: the item's highest vanilla shop
 * price (Rowf: the badge's own price), else twice its sell value.
 *
 * Key items: the story still runs in its normal order, so a key item found in
 * an area of chapter N (the chapter that needs it, at the latest) may only go
 * to a location a normal-story player can reach before chapter N: the table's
 * kav (computed from the PMR world graph by tools/rando_logic.py) must be <= N.
 * Never to a gift (those depend on the NPC's own story). Key items placed
 * first, most constrained first; one with no room left stays where it was.
 *
 * The new-game picker (platform/bottom_screen.c) sets the flags and maybe a
 * seed; a seed is rolled otherwise. Both are kept in the save file's unused
 * tail (SaveData.unk_1304, inside the CRC), so copied saves keep them. The placement is rebuilt from the seed
 * on load; a spoiler log goes to <SD folder>/spoiler_<seed>.txt.
 */
#include "common.h"
#include "message_ids.h"

#include <stdio.h>
#include <string.h>

#include "pm3ds_brand.h"
#include "pm3ds_log.h"
#include "pm3ds_rando.h"

enum { SRC_ITEM, SRC_BLOCK, SRC_TREE, SRC_BUSH, SRC_PANEL, SRC_CHEST, SRC_GIFT, SRC_HIDDEN_BLOCK };
static const char *const s_src_names[] = { "item", "block", "tree", "bush", "hidden panel", "chest", "gift",
                                           "hidden block" };

typedef struct {
    s32 flag;          /* GF_* as the scripts write it; 0 for a gift */
    s16 item;          /* vanilla */
    u8 src;
    u8 kav;            /* first chapter whose key items may go here (99: none) */
    u8 chapter;        /* chapter of the area (99: none) */
    const char *itemName;
    const char *map;
} RandoLoc;

#define LOC(flag, item, src, map, kav, chapter) { flag, item, src, kav, chapter, #item, map },
static const RandoLoc sLocs[] = {
#if VERSION_US
#include "rando_locations_us.inc"
#else
#include "rando_locations.inc"
#endif
};
#undef LOC
#define NUM_LOCS ARRAY_COUNT(sLocs)

typedef struct {
    s16 item;          /* vanilla */
    s16 price;
    s32 descMsg;
    const char *map;
    u8 rowf;           /* Rowf's one-time badges */
    const char *itemName;
} RandoShop;

#define SHOP(item, price, desc, map, rowf) { item, price, desc, map, rowf, #item },
static const RandoShop sShops[] = {
#if VERSION_US
#include "rando_shops_us.inc"
#else
#include "rando_shops.inc"
#endif
};
#undef SHOP
#define NUM_SHOPS ARRAY_COUNT(sShops)

#define RANDO_MAGIC 0x53485244   /* "SHRD" */

typedef struct {
    s32 magic;
    s32 version;   /* 1: a mode (below); 2, 3: PM3DS_RANDO_* flags (2: before HIDDEN) */
    s32 flags;
    u32 seed;
} RandoSaveBlock;

/* version 1 saves: Items, Everything, Everything + keys (shops, gifts, hidden spots always) */
static const u32 sV1Flags[4] = {
    0,
    PM3DS_RANDO_ON | PM3DS_RANDO_SHOPS | PM3DS_RANDO_GIFTS,
    PM3DS_RANDO_ON | PM3DS_RANDO_COINS | PM3DS_RANDO_SHOPS | PM3DS_RANDO_GIFTS,
    PM3DS_RANDO_ALL,
};

static u32 sFlags;             /* 0: off */
static u32 sSeed;
static u32 sNextFlags, sNextSeed;   /* the picker's choice for the next new game */
static s16 sItem[NUM_LOCS];    /* what location i holds now */
static const char* current_map(void);
static s16 sShopItem[NUM_SHOPS];

/* ---- seeded shuffle ------------------------------------------------------- */
static u32 next_rand(u32* state) {   /* splitmix32 */
    u32 z = (*state += 0x9E3779B9u);
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}

static bool is_key(s32 item) {
    return item > ITEM_NONE && item < ARRAY_COUNT(gItemTable) && (gItemTable[item].typeFlags & ITEM_TYPE_FLAG_KEY) &&
           !(gItemTable[item].typeFlags & ITEM_TYPE_FLAG_GEAR);
}

/* does location i's vanilla item move with these flags? */
static bool shuffleable(s32 i, u32 flags) {
    s32 item = sLocs[i].item;
    if (!(flags & PM3DS_RANDO_ON)) {
        return false;
    }
    if (sLocs[i].src == SRC_GIFT && !(flags & PM3DS_RANDO_GIFTS)) {
        return false;
    }
    if (item == ITEM_COIN || item == ITEM_STAR_PIECE) {
        return (flags & PM3DS_RANDO_COINS) != 0;
    }
    if (item <= ITEM_NONE || item >= ARRAY_COUNT(gItemTable)) {
        return false;
    }
    switch (item) {
        case ITEM_RED_BERRY:
        case ITEM_YELLOW_BERRY:
        case ITEM_BLUE_BERRY:
        case ITEM_BUBBLE_BERRY:
            return false;   // Flower Fields needs them where they grow
        case ITEM_CAKE:
            return false;   // Tayce T.'s cake: chapter 4's Gourmet Guy wants it
    }
    if (gItemTable[item].typeFlags & ITEM_TYPE_FLAG_GEAR) {
        return false;
    }
    if (gItemTable[item].typeFlags & ITEM_TYPE_FLAG_KEY) {
        // a prologue key item has nowhere earlier to go
        return (flags & PM3DS_RANDO_KEYS) && sLocs[i].chapter >= 1 && sLocs[i].chapter <= 8;
    }
    return (gItemTable[item].typeFlags & (ITEM_TYPE_FLAG_BADGE | ITEM_TYPE_FLAG_CONSUMABLE)) != 0;
}

/* a slot the story buys from: Dry Dry Outpost's password (Dried Shroom, then
 * Dusty Hammer) leads to Moustafa in chapter 2; Dried Pasta is in the Red
 * Jar's sequence */
static bool shop_story_slot(s32 i) {
    s32 item = sShops[i].item;
    return strcmp(sShops[i].map, "dro_01") == 0 &&
           (item == ITEM_DRIED_SHROOM || item == ITEM_DUSTY_HAMMER || item == ITEM_DRIED_PASTA);
}

/* may this item sit in that kind of shop slot (and come out of it)? */
static bool shop_kind_ok(s32 item, bool rowf) {
    if (item <= ITEM_NONE || item >= ARRAY_COUNT(gItemTable) || item == ITEM_COIN || item == ITEM_STAR_PIECE ||
        item == ITEM_CAKE_MIX) {   // chapter 4's cake: keep it buyable
        return false;
    }
    if (gItemTable[item].typeFlags & (ITEM_TYPE_FLAG_KEY | ITEM_TYPE_FLAG_GEAR)) {
        return false;
    }
    if (rowf) {
        return (gItemTable[item].typeFlags & ITEM_TYPE_FLAG_BADGE) != 0;
    }
    return (gItemTable[item].typeFlags & ITEM_TYPE_FLAG_CONSUMABLE) && !(gItemTable[item].typeFlags & ITEM_TYPE_FLAG_BADGE);
}

static void build(void) {
    static s16 pool[NUM_LOCS], keys[NUM_LOCS], rest[NUM_LOCS];
    static bool taken[NUM_LOCS];
    s32 n = 0, nk = 0, nr = 0, kept = 0;
    s32 i, j;
    u32 state = sSeed;

    for (i = 0; i < NUM_LOCS; i++) {
        sItem[i] = sLocs[i].item;
        taken[i] = false;
        if (shuffleable(i, sFlags)) {
            pool[n++] = i;
            if (is_key(sLocs[i].item)) {
                keys[nk++] = i;
            } else {
                rest[nr++] = i;
            }
        }
    }
    for (i = 0; i < NUM_SHOPS; i++) {
        sShopItem[i] = sShops[i].item;
    }
    if (!(sFlags & PM3DS_RANDO_ON)) {
        return;
    }
    // key items: random order, then the earliest-needed first (stable)
    for (i = nk - 1; i > 0; i--) {
        s32 t = next_rand(&state) % (u32)(i + 1);
        s16 x = keys[i];
        keys[i] = keys[t];
        keys[t] = x;
    }
    for (i = 1; i < nk; i++) {
        s16 x = keys[i];
        for (j = i; j > 0 && sLocs[keys[j - 1]].chapter > sLocs[x].chapter; j--) {
            keys[j] = keys[j - 1];
        }
        keys[j] = x;
    }
    // (only to spots that held no key item, so a key's own spot stays free for it)
#define KEY_SPOT(l, need)                                                                                      \
    (!taken[l] && !is_key(sLocs[l].item) && sLocs[l].src != SRC_GIFT && sLocs[l].kav <= (need) &&            \
     ((sFlags & PM3DS_RANDO_HIDDEN) || (sLocs[l].src != SRC_PANEL && sLocs[l].src != SRC_HIDDEN_BLOCK)))
    for (i = 0; i < nk; i++) {
        s32 need = sLocs[keys[i]].chapter;
        s32 cands = 0, pick;
        for (j = 0; j < n; j++) {
            cands += KEY_SPOT(pool[j], need);
        }
        if (cands == 0) {
            pick = keys[i];   // no room before it is needed: where it was
            kept++;
        } else {
            s32 k = next_rand(&state) % (u32)cands;
            for (j = 0; j < n; j++) {
                if (KEY_SPOT(pool[j], need) && k-- == 0) {
                    break;
                }
            }
            pick = pool[j];
        }
#undef KEY_SPOT
        taken[pick] = true;
        sItem[pick] = sLocs[keys[i]].item;
    }
    // everything else: Fisher-Yates into the spots left
    for (i = nr - 1; i > 0; i--) {
        s32 t = next_rand(&state) % (u32)(i + 1);
        s16 x = rest[i];
        rest[i] = rest[t];
        rest[t] = x;
    }
    j = 0;
    for (i = 0; i < n; i++) {
        if (!taken[pool[i]] && j < nr) {
            sItem[pool[i]] = sLocs[rest[j++]].item;
        }
    }
    // shops: each slot trades with a world spot holding the same kind of item
    for (i = 0; i < NUM_SHOPS && (sFlags & PM3DS_RANDO_SHOPS); i++) {
        s32 cands = 0, k;
        if (!shop_kind_ok(sShopItem[i], sShops[i].rowf) || shop_story_slot(i)) {
            continue;
        }
        for (j = 0; j < n; j++) {
            cands += sLocs[pool[j]].src != SRC_GIFT && shop_kind_ok(sItem[pool[j]], sShops[i].rowf);
        }
        if (cands == 0) {
            continue;
        }
        k = next_rand(&state) % (u32)cands;
        for (j = 0; j < n; j++) {
            s32 l = pool[j];
            if (sLocs[l].src != SRC_GIFT && shop_kind_ok(sItem[l], sShops[i].rowf) && k-- == 0) {
                s16 x = sItem[l];
                sItem[l] = sShopItem[i];
                sShopItem[i] = x;
                break;
            }
        }
    }
    pm3ds_log(PM3DS_LOG_INFO, "rando: seed %08lX flags %02lX, %d of %d locations shuffled, %d key items (%d kept)",
              (unsigned long)sSeed, (unsigned long)sFlags, (int)n, (int)NUM_LOCS, (int)nk, (int)kept);
}

/* ITEM_ name without the prefix, from the location tables */
static const char* item_name(s32 item) {
    static char buf[12];
    s32 i;
    for (i = 0; i < NUM_LOCS; i++) {
        if (sLocs[i].item == item) {
            return sLocs[i].itemName + 5;
        }
    }
    for (i = 0; i < NUM_SHOPS; i++) {
        if (sShops[i].item == item) {
            return sShops[i].itemName + 5;
        }
    }
    snprintf(buf, sizeof(buf), "item %d", (int)item);
    return buf;
}

static void write_spoiler(void) {
    char path[96];
    FILE* f;
    s32 i;

    snprintf(path, sizeof(path), PM3DS_SD_DIR "/spoiler_%08lX.txt", (unsigned long)sSeed);
    f = fopen(path, "w");
    if (f == nullptr) {
        return;
    }
    fprintf(f, "%s randomizer, seed %08lX\nShuffled: items, badges%s%s%s%s%s\n\n", PM3DS_APP_NAME,
            (unsigned long)sSeed, (sFlags & PM3DS_RANDO_COINS) ? ", coins, star pieces" : "",
            (sFlags & PM3DS_RANDO_KEYS) ? ", key items" : "",
            (sFlags & PM3DS_RANDO_KEYS) && (sFlags & PM3DS_RANDO_HIDDEN) ? " (hidden spots too)" : "",
            (sFlags & PM3DS_RANDO_SHOPS) ? ", shops" : "", (sFlags & PM3DS_RANDO_GIFTS) ? ", NPC gifts" : "");
    if (sFlags & PM3DS_RANDO_KEYS) {
        fprintf(f, "Key items:\n");
        for (i = 0; i < NUM_LOCS; i++) {
            if (sItem[i] != sLocs[i].item && is_key(sItem[i])) {
                fprintf(f, "  %-26s %-8s %s\n", item_name(sItem[i]), sLocs[i].map, s_src_names[sLocs[i].src]);
            }
        }
        fprintf(f, "\nEverything:\n");
    }
    for (i = 0; i < NUM_LOCS; i++) {
        if (sItem[i] != sLocs[i].item) {
            fprintf(f, "%-8s %-13s %-26s -> %s\n", sLocs[i].map, s_src_names[sLocs[i].src], sLocs[i].itemName + 5,
                    item_name(sItem[i]));
        }
    }
    fprintf(f, "\nShops:\n");
    for (i = 0; i < NUM_SHOPS; i++) {
        if (sShopItem[i] != sShops[i].item) {
            fprintf(f, "%-8s %-13s %-26s -> %s\n", sShops[i].map, sShops[i].rowf ? "Rowf" : "shop",
                    item_name(sShops[i].item), item_name(sShopItem[i]));
        }
    }
    fclose(f);
}

static void log_swap(s32 i) {
    pm3ds_log(PM3DS_LOG_INFO, "rando: %s %s %s -> %s", sLocs[i].map, s_src_names[sLocs[i].src], sLocs[i].itemName + 5,
              item_name(sItem[i]));
}

static s32 item_type(s32 item) {
    if (item == ITEM_STAR_PIECE) {
        return ITEM_TYPE_STAR_PIECE;
    }
    if (item > ITEM_NONE && item < ARRAY_COUNT(gItemTable)) {
        if (gItemTable[item].typeFlags & ITEM_TYPE_FLAG_KEY) {
            return ITEM_TYPE_KEY;
        }
        if (gItemTable[item].typeFlags & ITEM_TYPE_FLAG_BADGE) {
            return ITEM_TYPE_BADGE;
        }
    }
    return ITEM_TYPE_CONSUMABLE;
}

/* ---- game hooks ------------------------------------------------------------- */
/* src/item_entity.c, make_item_entity: the item this flagged spot holds now */
s32 pm3ds_rando_item(s32 itemID, s32 flagIndex, s32* spawnMode) {
    s32 i;
    s32 newItem;

    if (sFlags == 0 || flagIndex <= 0 || flagIndex == 0xFFFF) {
        return itemID;
    }
    for (i = 0; i < NUM_LOCS; i++) {
        if (sLocs[i].flag != 0 && EVT_INDEX_OF_GAME_FLAG(sLocs[i].flag) == flagIndex && sLocs[i].item == itemID) {
            break;
        }
    }
    if (i == NUM_LOCS || sItem[i] == sLocs[i].item) {
        return itemID;
    }
    newItem = sItem[i];
    log_swap(i);
    // an item block pops coins and items differently
    if (*spawnMode == ITEM_SPAWN_MODE_ITEM_BLOCK_COIN || *spawnMode == ITEM_SPAWN_MODE_ITEM_BLOCK_ITEM ||
        *spawnMode == ITEM_SPAWN_MODE_ITEM_BLOCK_BADGE) {
        if (newItem == ITEM_COIN) {
            *spawnMode = ITEM_SPAWN_MODE_ITEM_BLOCK_COIN;
        } else if (gItemTable[newItem].typeFlags & ITEM_TYPE_FLAG_BADGE) {
            *spawnMode = ITEM_SPAWN_MODE_ITEM_BLOCK_BADGE;
        } else {
            *spawnMode = ITEM_SPAWN_MODE_ITEM_BLOCK_ITEM;
        }
    }
    return newItem;
}

/* world/common/entity/Chest.inc.c: Call(pm3ds_RandoChest, GF_x) with LVarA
 * the vanilla item: LVarA/LVarB become the item now in this chest and its type */
API_CALLABLE(pm3ds_RandoChest) {
    s32 flag = *script->ptrReadPos;
    s32 item = script->varTable[10];
    s32 i;

    if (sFlags == 0) {
        return ApiStatus_DONE2;
    }
    for (i = 0; i < NUM_LOCS; i++) {
        if (sLocs[i].src == SRC_CHEST && sLocs[i].flag == flag && sLocs[i].item == item) {
            break;
        }
    }
    if (i < NUM_LOCS && sItem[i] != sLocs[i].item) {
        log_swap(i);
        script->varTable[10] = sItem[i];
        script->varTable[11] = item_type(sItem[i]);
    }
    return ApiStatus_DONE2;
}

/* world/common/complete/GiveReward.inc.c: LVar0/LVar1 (item, type) of an NPC's
 * gift become the item it gives now */
API_CALLABLE(pm3ds_RandoGift) {
    s32 item = script->varTable[0];
    const char* map;
    s32 i;

    if (sFlags == 0 || gGameStatusPtr->areaID < 0) {
        return ApiStatus_DONE2;
    }
    map = current_map();
    for (i = 0; i < NUM_LOCS; i++) {
        if (sLocs[i].src == SRC_GIFT && sLocs[i].item == item && strcmp(sLocs[i].map, map) == 0) {
            break;
        }
    }
    if (i < NUM_LOCS && sItem[i] != sLocs[i].item) {
        log_swap(i);
        script->varTable[0] = sItem[i];
        script->varTable[1] = item_type(sItem[i]);
    }
    return ApiStatus_DONE2;
}

/* Call(pm3ds_RandoAddGift, item, type, outIndex): AddItem / AddKeyItem /
 * AddBadge / AddStarPieces / AddCoin, whichever the item is; outIndex 0 for
 * none */
API_CALLABLE(pm3ds_RandoAddGift) {
    Bytecode* args = script->ptrReadPos;
    s32 item = evt_get_variable(script, *args++);
    s32 type = evt_get_variable(script, *args++);
    Bytecode out = *args++;
    PlayerData* playerData = &gPlayerData;
    s32 idx = 0;
    s32 i;

    if (item == ITEM_COIN) {
        add_coins(1);
    } else if (type == ITEM_TYPE_STAR_PIECE || item == ITEM_STAR_PIECE) {
        add_star_pieces(1);
    } else if (type == ITEM_TYPE_KEY) {
        if (item == ITEM_KOOPA_FORTRESS_KEY) {
            playerData->fortressKeyCount++;
        } else {
            for (i = 0; i < ARRAY_COUNT(playerData->keyItems) && playerData->keyItems[i] != ITEM_NONE; i++) {
            }
            if (i < ARRAY_COUNT(playerData->keyItems)) {
                playerData->keyItems[i] = item;
            }
        }
    } else if (type == ITEM_TYPE_BADGE) {
        idx = add_badge(item);
    } else {
        idx = add_item(item);
    }
    if (out != 0) {   // 0: the caller wants no index (a key item reward)
        evt_set_variable(script, out, idx);
    }
    return ApiStatus_DONE2;
}

static const char* current_map(void) {
    if (gGameStatusPtr->areaID < 0) {
        return "";
    }
    return gAreas[gGameStatusPtr->areaID].maps[gGameStatusPtr->mapID].id;
}

/* a shop entry with the item its slot sells now (price, description to match) */
static void shop_entry(ShopItemData* e, bool rowf) {
    const char* map = current_map();
    s32 i, j, item;
    bool found;

    for (i = 0; i < NUM_SHOPS; i++) {
        if (sShops[i].rowf == rowf && sShops[i].item == (s32)e->itemID && strcmp(sShops[i].map, map) == 0) {
            break;
        }
    }
    if (i == NUM_SHOPS || sShopItem[i] == sShops[i].item) {
        return;
    }
    item = sShopItem[i];
    e->itemID = item;
    e->descMsg = gItemTable[item].shortDescMsg;
    e->price = rowf ? gItemTable[item].sellValue : gItemTable[item].sellValue * 2;
    for (j = 0, found = false; j < NUM_SHOPS; j++) {   // sold in vanilla: its shop blurb, its highest price
        if (sShops[j].item == item) {
            e->descMsg = sShops[j].descMsg;
            if (!rowf && (!found || sShops[j].price > e->price)) {
                e->price = sShops[j].price;
            }
            found = true;
        }
    }
    if (e->price < 1) {
        e->price = 1;
    }
    pm3ds_log(PM3DS_LOG_INFO, "rando: %s %s %s -> %s for %d", map, rowf ? "Rowf" : "shop", item_name(sShops[i].item),
              item_name(item), (int)e->price);
}

/* src/world/script_api/shops.c, MakeShop: the inventory to use */
ShopItemData* pm3ds_rando_shop(ShopItemData* inventory) {
    static ShopItemData copy[16];
    s32 n;

    if (sFlags == 0) {
        return inventory;
    }
    for (n = 0; n < ARRAY_COUNT(copy) - 1 && inventory[n].itemID != ITEM_NONE; n++) {
        copy[n] = inventory[n];
        shop_entry(&copy[n], false);
    }
    copy[n].itemID = ITEM_NONE;
    return copy;
}

/* src/world/area_mac/mac_01/shop.c: one of Rowf's badges as he stocks it */
void pm3ds_rando_rowf(ShopItemData* entry) {
    if (sFlags != 0) {
        shop_entry(entry, true);
    }
}

/* src/main_loop.c etc., clear_saved_variables: a fresh state is unrandomized */
void pm3ds_rando_off(void) {
    sFlags = 0;
    sSeed = 0;
    build();
}

/* platform/bottom_screen.c, new-game picker */
void pm3ds_rando_choose(unsigned flags, unsigned seed) {
    sNextFlags = (flags & PM3DS_RANDO_ON) ? (flags & PM3DS_RANDO_ALL) : 0;
    sNextSeed = seed;
}

/* src/filemenu/filemenu_yesno.c, new file: the picker's choice */
void pm3ds_rando_new_game(void) {
    extern unsigned int pm3ds_random_seed(void);
    if (sNextFlags == 0) {
        pm3ds_rando_off();
        return;
    }
    sFlags = sNextFlags;
    sSeed = sNextSeed != 0 ? sNextSeed : pm3ds_random_seed();
    if (sSeed == 0) {
        sSeed = 1;
    }
    build();
    write_spoiler();
}

/* src/fio.c */
void pm3ds_rando_serialize(void* dst) {
    RandoSaveBlock b = { RANDO_MAGIC, 3, (s32)sFlags, sSeed };
    memcpy(dst, &b, sizeof(b));
}

void pm3ds_rando_deserialize(const void* src) {
    RandoSaveBlock b;
    memcpy(&b, src, sizeof(b));
    if (b.magic == RANDO_MAGIC && b.version == 1 && b.flags > 0 && b.flags < ARRAY_COUNT(sV1Flags)) {
        sFlags = sV1Flags[b.flags];
        sSeed = b.seed;
        build();
    } else if (b.magic == RANDO_MAGIC && (b.version == 2 || b.version == 3) && (b.flags & PM3DS_RANDO_ON)) {
        sFlags = (u32)b.flags & PM3DS_RANDO_ALL;
        if (b.version == 2) {
            sFlags |= PM3DS_RANDO_HIDDEN;   // placed before the option existed
        }
        sSeed = b.seed;
        build();
    } else {
        pm3ds_rando_off();
    }
}

/* for the bottom screen */
unsigned pm3ds_rando_seed(void) {
    return sFlags == 0 ? 0 : sSeed;
}
