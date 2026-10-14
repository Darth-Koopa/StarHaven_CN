#include "common.h"

#include "world/common/todo/StashVars.inc.c"
#include "world/common/todo/GetItemName.inc.c"
#include "world/common/todo/SomeItemEntityFunc.inc.c"
#include "world/common/todo/IsItemBadge.inc.c"

s32** N(varStash) = nullptr;

#ifdef _3DS
// the randomizer's item for this chest (port/3ds/source/n64_shim/rando.c)
ApiStatus pm3ds_RandoChest(Evt* script, bool isInitialCall);
ApiStatus pm3ds_RandoAddGift(Evt* script, bool isInitialCall);
#define PM3DS_RANDO_CHEST(flag) Call(pm3ds_RandoChest, flag)
#else
#define PM3DS_RANDO_CHEST(flag)
#endif

EvtScript N(EVS_Chest_ShowGotItem) = {
    SetGroup(EVT_GROUP_NEVER_PAUSE)
    Call(SetTimeFreezeMode, TIME_FREEZE_FULL)
    Wait(40)
    Call(ShowGotItem, LVar0, false, 0)
    Call(SetTimeFreezeMode, TIME_FREEZE_NONE)
    Return
    Return
    End
};

EvtScript N(EVS_Chest_GetItem) = {
    Call(DisablePlayerInput, true)
    Set(LVar0, LVarA)
    IfNe(LVarA, ITEM_NONE)
        ExecWait(N(EVS_Chest_ShowGotItem))
    EndIf
#ifdef _3DS
    Call(pm3ds_RandoAddGift, LVarA, LVarB, LVar0)   // also coins and star pieces
#else
    Switch(LVarB)
        CaseEq(ITEM_TYPE_CONSUMABLE)
            Call(AddItem, LVarA, LVar0)
        CaseEq(ITEM_TYPE_KEY)
            Call(AddKeyItem, LVarA)
        CaseEq(ITEM_TYPE_BADGE)
            Call(AddBadge, LVarA, LVar0)
    EndSwitch
#endif
    Wait(15)
    Call(DisablePlayerInput, false)
    Return
    End
};

#define EVT_OPEN_CHEST_CONSUMABLE(item, flag) \
    { \
        Set(LVarA, item) \
        Set(LVarB, ITEM_TYPE_CONSUMABLE) \
        PM3DS_RANDO_CHEST(flag) \
        Set(flag, true) \
        ExecWait(N(EVS_Chest_GetItem)) \
        Return \
        End \
    }

#define EVT_OPEN_CHEST_KEY(item, flag) \
    { \
        Set(LVarA, item) \
        Set(LVarB, ITEM_TYPE_KEY) \
        PM3DS_RANDO_CHEST(flag) \
        Set(flag, true) \
        ExecWait(N(EVS_Chest_GetItem)) \
        Return \
        End \
    }

#define EVT_OPEN_CHEST_BADGE(item, flag) \
    { \
        Set(LVarA, item) \
        Set(LVarB, ITEM_TYPE_BADGE) \
        PM3DS_RANDO_CHEST(flag) \
        Set(flag, true) \
        ExecWait(N(EVS_Chest_GetItem)) \
        Return \
        End \
    }
