/*
 * touch_menu.c — game-side half of the bottom screen's touch menus.
 *
 * Each list menu that waits for a choice calls pm3ds_tm_poll() from its
 * "choosing" state (_3DS hooks in src/battle/btl_states_menus.c and the
 * popup menu, src/8a860_len_3f30.c). That publishes the entries for the
 * bottom screen and hands back a tap made there: the hook moves its cursor to
 * pm3ds_tm_index() and treats pm3ds_tm_confirm() like an A press, so a tap
 * takes the same path as the buttons (sounds, errors for disabled entries).
 */
#include "common.h"
#include "battle/battle.h"
#include "hud_element.h"

#include <string.h>

#include "pm3ds_touch_menu.h"

void dma_load_msg(u32 msgID, void* dest);

/* src/battle/btl_states_menus.c */
extern s32 WheelOptionCount;
extern s32 WheelOptionName[6];
extern s32 WheelOptionEnabled[6];
extern s8 BattleMenu_CurPos;
extern s8 BattleMenu_HomePos;
extern s32 MovesOptionCount;
extern s32 MovesOptionIndexMap[24];
extern s32 MovesOptionNames[24];
extern s32 MovesOptionDisplayCosts[24];
extern s32 MovesOptionDiscounts[24];
extern s32 MovesOptionEnabled[24];
extern s8 MovesCursorPos;
extern s32 UsingSpiritsSubmenu;
extern s32 StratsOptionCount;
extern s32 StratsOptionNames[5];
extern s32 StratsOptionEnabled[];
extern s8 StratsCursorPos;
extern s32 PartnerNameMessages[];
/* src/8a860_len_3f30.c */
extern PopupMenu* gPopupMenu;
extern s32 PopupMenu_SelectedIndex;

uint16_t pm3ds_touch_pad;
int8_t pm3ds_touch_stick_x;

static pm3ds_touch_menu_t sMenu;
static u32 sStamp;               /* gGameStatus.frameCounter when last published */
static s32 sNameMsg[PM3DS_TM_MAX + 1];   /* what each name was decoded from ([MAX] = title) */
static s32 sPickKind, sPickIndex;
static u32 sPickStamp;
static bool sConfirm;

/* ---- message text -> font codes ------------------------------------------ */
/* arguments of each MSG_READ_FUNC_* (0xFF xx ...); -1 = stop decoding */
static const s8 sFuncArgs[0x30] = {
    1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 2, 0, 2,     /* 00 font .. 0F speed */
    2, 1, 1, 1, 1, 1, 3, 2, 7, 1, 2, 2, 1, 3, 1, 1,     /* 10 set x .. 1F end choice */
    1, 1, 0, 0, 0, 0, -1, 1, 1, 1, 1, 0, 4, -1, 1, 1,   /* 20 set cancel .. 2F voice */
};

static void decode_msg(s32 msgID, char* out, s32 size) {
    static u8 buf[1024];
    const u8* m;
    s32 n = 0;
    s32 i;

    out[0] = '\0';
    if (msgID == MSG_NONE || msgID == 0) {
        return;
    }
    if (MSG_IS_ID(msgID)) {
        dma_load_msg(msgID, buf);
        m = buf;
    } else {
        m = (const u8*)msgID;
    }
    for (i = 0; i < (s32)sizeof(buf) && n < size - 1; i++) {
        u8 c = m[i];
        if (c == MSG_CHAR_READ_END) {
            break;
        }
        if (c == MSG_CHAR_READ_ENDL || c == MSG_CHAR_READ_SPACE || c == MSG_CHAR_READ_FULL_SPACE ||
            c == MSG_CHAR_READ_HALF_SPACE || c == MSG_CHAR_READ_NEXT) {
            if (n > 0 && out[n - 1] != ' ') {
                out[n++] = ' ';
            }
        } else if (c == MSG_CHAR_READ_FUNCTION) {
            u8 f = m[++i];
            if (f >= ARRAY_COUNT(sFuncArgs) || sFuncArgs[f] < 0) {
                break;
            }
            i += sFuncArgs[f];
        } else if (c == MSG_CHAR_READ_STYLE) {
            break;   // only dialogue boxes use styles
        } else if (c < 0xF0 && c + 0x20 < 0x100) {
            out[n++] = (char)(c + 0x20);
        }
    }
    while (n > 0 && out[n - 1] == ' ') {
        n--;
    }
    out[n] = '\0';
}

/* decode only when the message changed: these run every frame */
static void set_name(s32 slot, s32 msgID) {
    char* dst = slot == PM3DS_TM_MAX ? sMenu.title : sMenu.e[slot].name;
    s32 size = slot == PM3DS_TM_MAX ? sizeof(sMenu.title) : sizeof(sMenu.e[0].name);
    if (sNameMsg[slot] != msgID || dst[0] == '\0') {
        sNameMsg[slot] = msgID;
        decode_msg(msgID, dst, size);
    }
}

static void set_entry(s32 i, s32 msgID, bool enabled, s32 cost, s32 costKind) {
    set_name(i, msgID);
    sMenu.e[i].enabled = enabled;
    sMenu.e[i].cost = cost;
    sMenu.e[i].max_hp = 0;
    sMenu.e[i].cost_kind = costKind;
}

/* the battle's current wheel option names the open submenu */
static void wheel_title(void) {
    s32 pos = BattleMenu_CurPos + BattleMenu_HomePos;
    if (pos >= 0 && pos < WheelOptionCount) {
        set_name(PM3DS_TM_MAX, WheelOptionName[pos]);
    }
}

/* ---- publishing ------------------------------------------------------------ */
static void publish(s32 kind) {
    s32 i;
    s32 n = 0;

    sMenu.kind = kind;
    sMenu.all_targets = 0;
    sMenu.partner = (gBattleStatus.flags1 & BS_FLAGS1_PARTNER_ACTING) != 0;
    switch (kind) {
        case PM3DS_TM_WHEEL:
            n = WheelOptionCount;
            for (i = 0; i < n && i < PM3DS_TM_MAX; i++) {
                set_entry(i, WheelOptionName[i], WheelOptionEnabled[i] == true, 0, PM3DS_TM_COST_NONE);
            }
            sMenu.cursor = BattleMenu_CurPos + BattleMenu_HomePos;
            set_name(PM3DS_TM_MAX, sMenu.partner ? PartnerNameMessages[gPlayerData.curPartner] : MSG_NONE);
            if (!sMenu.partner) {
                strcpy(sMenu.title, "Mario");
                sNameMsg[PM3DS_TM_MAX] = -2;
            }
            break;
        case PM3DS_TM_MOVES:
            n = MovesOptionCount;
            for (i = 0; i < n && i < PM3DS_TM_MAX; i++) {
                s32 o = MovesOptionIndexMap[i];
                s32 shown = MovesOptionDisplayCosts[o];
                s32 cost = shown - MovesOptionDiscounts[o];
                if (shown != 0 && cost <= 0) {
                    cost = 1;
                }
                if (UsingSpiritsSubmenu) {
                    cost = shown;   // star power bars
                }
                set_entry(i, MovesOptionNames[o], MovesOptionEnabled[o] > 0, cost,
                          shown == 0 ? PM3DS_TM_COST_NONE
                                     : (UsingSpiritsSubmenu ? PM3DS_TM_COST_SP : PM3DS_TM_COST_FP));
            }
            sMenu.cursor = MovesCursorPos;
            wheel_title();
            break;
        case PM3DS_TM_STRATS:
            n = StratsOptionCount;
            for (i = 0; i < n && i < PM3DS_TM_MAX; i++) {
                set_entry(i, StratsOptionNames[i], StratsOptionEnabled[i] != 0, 0, PM3DS_TM_COST_NONE);
            }
            sMenu.cursor = StratsCursorPos;
            wheel_title();
            break;
        case PM3DS_TM_POPUP: {
            PopupMenu* p = gPopupMenu;
            s32 costKind = PM3DS_TM_COST_NONE;
            if (p == nullptr) {
                sMenu.kind = PM3DS_TM_NONE;
                return;
            }
            if (p->popupType == POPUP_MENU_SELL_ITEM) {
                costKind = PM3DS_TM_COST_COINS;
            } else if (p->popupType == POPUP_MENU_TRADE_FOR_BADGE) {
                costKind = PM3DS_TM_COST_STAR_PIECES;
            }
            n = p->numEntries;
            for (i = 0; i < n && i < PM3DS_TM_MAX; i++) {
                set_entry(i, p->nameMsg[i], p->enabled[i], costKind ? p->value[i] : 0, costKind);
            }
            sMenu.cursor = PopupMenu_SelectedIndex;
            if (gGameStatusPtr->context == CONTEXT_BATTLE) {
                wheel_title();
            } else {
                const char* t = p->popupType == POPUP_MENU_SWITCH_PARTNER ? "Partners"
                              : p->popupType == POPUP_MENU_SELL_ITEM      ? "Sell"
                              : p->popupType == POPUP_MENU_TRADE_FOR_BADGE ? "Trade"
                                                                           : "Items";
                strcpy(sMenu.title, t);
                sNameMsg[PM3DS_TM_MAX] = -2;
            }
            break;
        }
        case PM3DS_TM_TARGET: {
            Actor* actor = sMenu.partner ? gBattleStatus.partnerActor : gBattleStatus.playerActor;
            n = actor->targetListLength;
            for (i = 0; i < n && i < PM3DS_TM_MAX; i++) {
                SelectableTarget* target = &actor->targetData[actor->targetIndexList[i]];
                Actor* t = get_actor(target->actorID);
                s32 msgID;
                if (target->actorID == ACTOR_PLAYER) {
                    msgID = MSG_Menus_Battle_TargetMario;
                } else if (target->actorID == ACTOR_PARTNER) {
                    msgID = PartnerNameMessages[(gBattleStatus.flags2 & BS_FLAGS2_PEACH_BATTLE) ? PARTNER_TWINK
                                                                                                 : gPlayerData.curPartner];
                } else {
                    msgID = get_actor_part(t, target->partID)->staticData->overrideNameMsg;
                    if (msgID == MSG_NONE) {
                        msgID = bActorNames[t->actorType];
                    }
                }
                set_entry(i, msgID, true, -1, PM3DS_TM_COST_NONE);
                if (t != nullptr && (target->actorID & ACTOR_CLASS_MASK) == ACTOR_CLASS_ENEMY &&
                    is_actor_health_bar_visible(t)) {
                    sMenu.e[i].cost = t->curHP;
                    sMenu.e[i].max_hp = t->maxHP;
                    sMenu.e[i].cost_kind = PM3DS_TM_COST_HP;
                }
            }
            sMenu.cursor = actor->selectedTargetIndex;
            sMenu.all_targets = !(gBattleStatus.curTargetListFlags & TARGET_FLAG_SELECT_ONE);
            strcpy(sMenu.title, "Target");
            sNameMsg[PM3DS_TM_MAX] = -2;
            break;
        }
    }
    sMenu.count = n < PM3DS_TM_MAX ? n : PM3DS_TM_MAX;
    sStamp = gGameStatusPtr->frameCounter;
}

/* ---- game hooks ------------------------------------------------------------ */
/* From a menu's choosing state, every frame: publish it; true when a tap on
 * it is waiting (then pm3ds_tm_index() is the entry and pm3ds_tm_confirm()
 * reports the "A press" once). */
bool pm3ds_tm_poll(s32 kind) {
    publish(kind);
    if (sPickKind != kind) {
        return false;
    }
    sPickKind = PM3DS_TM_NONE;
    if (gGameStatusPtr->frameCounter - sPickStamp > 20 || sPickIndex < 0 || sPickIndex >= sMenu.count) {
        return false;
    }
    sConfirm = true;
    return true;
}

s32 pm3ds_tm_index(void) {
    return sPickIndex;
}

bool pm3ds_tm_pending(void) {
    return sConfirm;
}

bool pm3ds_tm_confirm(void) {
    bool c = sConfirm;
    sConfirm = false;
    return c;
}

/* first visible row of a scrolling list so that row `sel` shows with one row
 * of context either side (the rule the game's lists follow) */
s32 pm3ds_tm_scroll(s32 sel, s32 first, s32 count, s32 visible) {
    if (sel < first + 1) {
        first = sel - 1;
    }
    if (sel >= first + visible - 1) {
        first = sel - visible + 2;
    }
    if (first > count - visible) {
        first = count - visible;
    }
    if (first < 0) {
        first = 0;
    }
    return first;
}

/* ---- bottom screen side ---------------------------------------------------- */
int pm3ds_tm_get(pm3ds_touch_menu_t* out) {
    if (gGameStatusPtr == nullptr || sMenu.kind == PM3DS_TM_NONE || gGameStatusPtr->frameCounter - sStamp > 4) {
        out->kind = PM3DS_TM_NONE;
        return PM3DS_TM_NONE;
    }
    *out = sMenu;
    return sMenu.kind;
}

void pm3ds_tm_pick(int kind, int index) {
    if (gGameStatusPtr == nullptr) {
        return;
    }
    sPickKind = kind;
    sPickIndex = index;
    sPickStamp = gGameStatusPtr->frameCounter;
}
