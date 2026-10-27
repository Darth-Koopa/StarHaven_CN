#ifndef PM3DS_KEYITEMS_H
#define PM3DS_KEYITEMS_H
/*
 * The key item list, longer than PlayerData's 32 slots (n64_shim/keyitems.c).
 * A randomized game can hand out most key items early, and a full list
 * dropped the next one for good (Kooper's shell, so a softlock). Slots 32 and
 * up live outside PlayerData and are saved in the save's unused tail, so the
 * save layout stays the cartridge's. The game's key item loops go through
 * KEY_SLOTS / KEY_ITEM(i); an index the scripts keep (FindKeyItem,
 * ShowKeyChoicePopup, RemoveKeyItemAt) means the same slot either way.
 */
#include "common.h"

#define PM3DS_KEY_EXTRA 28
#define KEY_SLOTS (s32)(ARRAY_COUNT(gPlayerData.keyItems) + PM3DS_KEY_EXTRA)
#define KEY_ITEM(i) (*pm3ds_key_slot(i))

s16* pm3ds_key_slot(s32 i);
void pm3ds_keys_clear(void);
void pm3ds_keys_serialize(void* tail);
void pm3ds_keys_deserialize(const void* tail);

#endif
