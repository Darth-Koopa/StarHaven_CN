#ifndef _COMMON_GIVE_REWARD_H_
#define _COMMON_GIVE_REWARD_H_

#include "common.h"

static s32** N(varStash) = nullptr;

// TODO extracted from world/common/todo/StashVars to reduce warnings (for now)
API_CALLABLE(N(StashVars)) {
    //static s32** varTable = nullptr;
    s32 i;

    if (N(varStash) == nullptr) {
        N(varStash) = heap_malloc(sizeof(script->varTable));

        for (i = 0; i < ARRAY_COUNT(script->varTable); i++) {
            N(varStash)[i] = (s32*) script->varTable[i];
        }
    } else {
        for (i = 0; i < ARRAY_COUNT(script->varTable); i++) {
            script->varTable[i] = (s32) N(varStash)[i];
        }

        heap_free(N(varStash));
        N(varStash) = nullptr;
    }

    return ApiStatus_DONE2;
}

#include "world/common/todo/GetItemName.inc.c"

EvtScript N(GiveItemReward) = {
    Call(ShowGotItem, LVar0, ITEM_TYPE_KEY, 0)
    Return
    Return
    End
};

EvtScript N(GiveCoinReward) = {
    Call(ShowGotItem, LVar0, ITEM_TYPE_KEY, ITEM_PICKUP_FLAG_1_COIN)
    Return
    Return
    End
};

#ifdef _3DS
// The randomizer may have put another item in this gift (port/3ds/source/
// n64_shim/rando.c): ask it, show that, and add it whatever its kind.
// The out-index variables are the ones the original adds wrote.
ApiStatus pm3ds_RandoGift(Evt* script, bool isInitialCall);
ApiStatus pm3ds_RandoAddGift(Evt* script, bool isInitialCall);
#define PM3DS_GIVE_REWARD(itemID, type, outIdx) \
    Set(LVar0, itemID) \
    Set(LVar1, type) \
    Call(pm3ds_RandoGift) \
    ExecWait(N(GiveItemReward)) \
    Call(pm3ds_RandoAddGift, LVar0, LVar1, outIdx)

#define EVT_GIVE_KEY_REWARD(itemID) PM3DS_GIVE_REWARD(itemID, ITEM_TYPE_KEY, 0)   // 0: no output
#define EVT_GIVE_BADGE_REWARD(itemID) PM3DS_GIVE_REWARD(itemID, ITEM_TYPE_BADGE, LVar0)
#define EVT_GIVE_BADGE_REWARD_ALT(itemID, outIdx) PM3DS_GIVE_REWARD(itemID, ITEM_TYPE_BADGE, outIdx)
#define EVT_GIVE_CONSUMABLE_REWARD(itemID) PM3DS_GIVE_REWARD(itemID, ITEM_TYPE_CONSUMABLE, LVar1)
#define EVT_GIVE_CONSUMABLE_REWARD_ALT(itemID) PM3DS_GIVE_REWARD(itemID, ITEM_TYPE_CONSUMABLE, LVar0)
#else
#define EVT_GIVE_KEY_REWARD(itemID) \
    Set(LVar0, itemID) \
    Set(LVar1, ITEM_TYPE_KEY) \
    ExecWait(N(GiveItemReward)) \
    Call(AddKeyItem, itemID)

#define EVT_GIVE_BADGE_REWARD(itemID) \
    Set(LVar0, itemID) \
    Set(LVar1, ITEM_TYPE_BADGE) \
    ExecWait(N(GiveItemReward)) \
    Call(AddBadge, itemID, LVar0)

#define EVT_GIVE_BADGE_REWARD_ALT(itemID, outIdx) \
    Set(LVar0, itemID) \
    Set(LVar1, ITEM_TYPE_BADGE) \
    ExecWait(N(GiveItemReward)) \
    Call(AddBadge, LVar0, outIdx)

#define EVT_GIVE_CONSUMABLE_REWARD(itemID) \
    Set(LVar0, itemID) \
    Set(LVar1, ITEM_TYPE_CONSUMABLE) \
    ExecWait(N(GiveItemReward)) \
    Call(AddItem, LVar0, LVar1)

#define EVT_GIVE_CONSUMABLE_REWARD_ALT(itemID) \
    Set(LVar0, itemID) \
    Set(LVar1, ITEM_TYPE_CONSUMABLE) \
    ExecWait(N(GiveItemReward)) \
    Call(AddItem, itemID, LVar0)

#endif

#define EVT_GIVE_STAR_PIECE() \
    Set(LVar0, ITEM_STAR_PIECE) \
    Set(LVar1, ITEM_TYPE_STAR_PIECE) \
    ExecWait(N(GiveItemReward)) \
    Call(AddStarPieces, 1)

#define EVT_GIVE_COIN() \
    Set(LVar0, ITEM_COIN) \
    Set(LVar1, 0) \
    ExecWait(N(GiveCoinReward)) \
    Call(AddCoin, 1)

#endif
