/*
 * keyitems.c: key item slots past PlayerData.keyItems (include/pm3ds_keyitems.h).
 * Kept in the save's unused tail (SaveData.unk_1304 + 0x38, after rando.c's
 * taken block), inside the CRC; a save without the block has none.
 */
#include "common.h"
#include "pm3ds_keyitems.h"

#include <string.h>

#define KEYS_MAGIC 0x53484B49     /* "SHKI" */
#define KEYS_BLOCK_OFS 0x38

typedef struct {
    s32 magic;
    s16 items[PM3DS_KEY_EXTRA];
} KeyItemsBlock;

_Static_assert(KEYS_BLOCK_OFS + sizeof(KeyItemsBlock) <= 0x7C, "key item block past the save's tail");

static s16 sExtra[PM3DS_KEY_EXTRA];
static s16 sBad;   /* what an index out of range reads and writes */

s16* pm3ds_key_slot(s32 i) {
    if (i >= 0 && i < ARRAY_COUNT(gPlayerData.keyItems)) {
        return &gPlayerData.keyItems[i];
    }
    i -= ARRAY_COUNT(gPlayerData.keyItems);
    if (i >= 0 && i < PM3DS_KEY_EXTRA) {
        return &sExtra[i];
    }
    sBad = ITEM_NONE;
    return &sBad;
}

/* src/inventory.c, clear_player_data */
void pm3ds_keys_clear(void) {
    memset(sExtra, 0, sizeof(sExtra));
}

/* src/fio.c */
void pm3ds_keys_serialize(void* tail) {
    KeyItemsBlock b;
    b.magic = KEYS_MAGIC;
    memcpy(b.items, sExtra, sizeof(sExtra));
    memcpy((u8*)tail + KEYS_BLOCK_OFS, &b, sizeof(b));
}

void pm3ds_keys_deserialize(const void* tail) {
    KeyItemsBlock b;
    memcpy(&b, (const u8*)tail + KEYS_BLOCK_OFS, sizeof(b));
    if (b.magic == KEYS_MAGIC) {
        memcpy(sExtra, b.items, sizeof(sExtra));
    } else {
        pm3ds_keys_clear();
    }
}
