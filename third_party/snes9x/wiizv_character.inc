#include "wiizv_character_data.h"
#include <algorithm>

static unsigned wiizvGoldStatus = 0, wiizvGoldCharacter = 0, wiizvGoldResumeStack = 0;
static unsigned wiizvGoldWaitStack = 0;
static uint64 wiizvGoldTarget = 0;
enum class WiizvCharacterEdit { Gold, Experience, Alignment, Items };
static WiizvCharacterEdit wiizvCharacterEdit = WiizvCharacterEdit::Gold;

extern "C" WIIZV_EXPORT bool wiizv_can_edit_character()
{
    if (Memory.ROMCRC32 != 0x547cf7b9 || Registers.PB != 0 ||
        wiizvGoldStatus == 1 || wiizvGoldStatus == 3) return false;
    const unsigned waitStack = Memory.RAM[0x1a] | (Memory.RAM[0x1b] << 8);
    const bool waiting = (Registers.PCw >= 0x8215 && Registers.PCw <= 0x8218 && Registers.S.W == waitStack) ||
        (Registers.PCw == 0x8150 && Registers.S.W + 4u == waitStack);
    const bool castle = waitStack == 0x1fd3 && Memory.RAM[0x35] == 0xc5 && Memory.RAM[0x36] == 0 &&
        Memory.RAM[0x1fdd] == 0xb4 && Memory.RAM[0x1fde] == 0x85 && Memory.RAM[0x1fdf] == 0;
    // 城の施設・郊外・訓練場は城メニューのJSRを残したまま入力を待つ。
    const bool facility = Memory.RAM[0xb7] == 0 && waitStack >= 0x1f80 && waitStack <= 0x1fd8 &&
        Memory.RAM[0x1fde] == 0xbf && Memory.RAM[0x1fdf] == 0x85;
    const bool camp = Memory.RAM[0xb7] >= 1 && Memory.RAM[0xb7] <= 8 &&
        waitStack >= 0x1f80 && waitStack <= 0x1fd8 && Memory.RAM[0x1fdd] == 0x43 &&
        Memory.RAM[0x1fde] == 0xa8 && Memory.RAM[0x1fdf] == 4;
    return waiting && (castle || facility || camp || wiizv_can_move());
}

extern "C" WIIZV_EXPORT bool wiizv_can_edit_items()
{
    if (Memory.ROMCRC32 != 0x547cf7b9 || Registers.PB || wiizvGoldStatus == 1 || wiizvGoldStatus == 3) return false;
    const unsigned stack = Memory.RAM[0x1a] | (Memory.RAM[0x1b] << 8);
    if (stack < 0x1f00 || stack > 0x1fe0 ||
        !((Registers.PCw >= 0x8215 && Registers.PCw <= 0x8218 && Registers.S.W == stack) ||
        (Registers.PCw == 0x8150 && Registers.S.W + 4u == stack))) return false;
    if (!Memory.RAM[0xdb] || Memory.RAM[0xdb] > 6) return false;
    unsigned seen = 0;
    for (unsigned i = 0; i < Memory.RAM[0xdb]; ++i) {
        unsigned slot = Memory.RAM[0xdc+i];
        if (slot >= 20 || !Memory.RAM[0x1000+slot*0x80] || (seen & (1u<<slot))) return false;
        seen |= 1u<<slot;
    }
    // Exclude active JSL return frames from the combat engine, without a location allowlist.
    for (unsigned i = stack+1; i+2 <= 0x1fff; ++i) {
        unsigned ret = Memory.RAM[i] | (Memory.RAM[i+1]<<8), bank = Memory.RAM[i+2];
        if (ret < 0x8003 || bank > 0x1f) continue;
        unsigned off = bank*0x8000+(ret&0x7fff)-3;
        if (off+3 < Memory.CalculatedSize && Memory.ROM[off] == 0x22 && bank == 5 && ret >= 0xac1b) return false;
    }
    return true;
}

extern "C" WIIZV_EXPORT bool wiizv_request_item_recalc(unsigned character)
{
    if (!wiizv_can_edit_items() || character >= 20 || !Memory.RAM[0x1000+character*0x80]) return false;
    wiizvGoldCharacter = character;
    wiizvCharacterEdit = WiizvCharacterEdit::Items;
    wiizvGoldStatus = 1;
    wiizvGoldWaitStack = Memory.RAM[0x1a] | (Memory.RAM[0x1b]<<8);
    return true;
}

extern "C" WIIZV_EXPORT bool wiizv_request_gold(unsigned character, uint64 amount)
{
    if (!wiizv_can_edit_character() || character >= 20 || amount == 0 || amount > characterData::MAX_GOLD) return false;
    auto* record = Memory.RAM + 0x1000 + character * 0x80;
    uint64 gold = 0;
    if (!record[0] || !characterData::readGold(record, gold) || gold == characterData::MAX_GOLD) return false;
    wiizvGoldCharacter = character;
    wiizvCharacterEdit = WiizvCharacterEdit::Gold;
    wiizvGoldTarget = gold + std::min(amount, characterData::MAX_GOLD - gold);
    wiizvGoldStatus = 1;
    wiizvGoldWaitStack = Memory.RAM[0x1a] | (Memory.RAM[0x1b] << 8);
    return true;
}

extern "C" WIIZV_EXPORT bool wiizv_request_experience(unsigned character, unsigned levels)
{
    if (!wiizv_can_edit_character() || character >= 20) return false;
    const auto* record = Memory.RAM + 0x1000 + character * 0x80;
    uint64 current = 0, target = 0;
    if (!characterData::readExperience(record, current) ||
        !characterData::experienceTarget(record, Memory.ROM, Memory.CalculatedSize, levels, target) || current == target) return false;
    wiizvGoldCharacter = character;
    wiizvGoldTarget = target;
    wiizvCharacterEdit = WiizvCharacterEdit::Experience;
    wiizvGoldStatus = 1;
    wiizvGoldWaitStack = Memory.RAM[0x1a] | (Memory.RAM[0x1b] << 8);
    return true;
}

extern "C" WIIZV_EXPORT bool wiizv_request_alignment(unsigned character, unsigned alignment)
{
    if (!wiizv_can_edit_character() || character >= 20 || alignment > 2) return false;
    const auto* record = Memory.RAM + 0x1000 + character * 0x80;
    if (!record[0] || (record[0xa] & 3) == alignment) return false;
    wiizvGoldCharacter = character;
    wiizvGoldTarget = alignment;
    wiizvCharacterEdit = WiizvCharacterEdit::Alignment;
    wiizvGoldStatus = 1;
    wiizvGoldWaitStack = Memory.RAM[0x1a] | (Memory.RAM[0x1b] << 8);
    return true;
}

extern "C" WIIZV_EXPORT unsigned wiizv_gold_status() { return wiizvGoldStatus; }
extern "C" WIIZV_EXPORT void wiizv_cancel_gold() { wiizvGoldStatus = 0; }

static inline void wiizvCharacterHook()
{
    if (Memory.ROMCRC32 != 0x547cf7b9 || Registers.PBPC != 0x0081f5) return;
    if (wiizvGoldStatus == 3 && Registers.S.W == wiizvGoldResumeStack)
    {
        wiizvGoldStatus = 2;
        return;
    }
    if (wiizvGoldStatus != 1 || Registers.S.W != wiizvGoldWaitStack + 4 || Registers.D.W || Registers.DB ||
        !CheckMemory() || CheckIndex()) return;
    auto* record = Memory.RAM + 0x1000 + wiizvGoldCharacter * 0x80;
    if (wiizvCharacterEdit == WiizvCharacterEdit::Items) {
        unsigned address = 0x1000 + wiizvGoldCharacter * 0x80;
        Memory.RAM[0x30] = static_cast<uint8>(address); Memory.RAM[0x31] = static_cast<uint8>(address >> 8);
        Registers.X.W = address;
    }
    else if (wiizvCharacterEdit == WiizvCharacterEdit::Alignment)
        record[0xa] = static_cast<uint8>((record[0xa] & 0xfc) | wiizvGoldTarget);
    else if (wiizvCharacterEdit == WiizvCharacterEdit::Experience) characterData::writeExperience(record, wiizvGoldTarget);
    else characterData::writeGold(record, wiizvGoldTarget);
    wiizvGoldResumeStack = Registers.S.W;
    wiizvGoldStatus = 3;
    // 原作のチェックサム更新・二重SRAM保存を実行する。呼出元は編集結果だけを残して場面を復元する。
    Memory.RAM[Registers.S.W--] = 0;
    Memory.RAM[Registers.S.W--] = 0x81;
    Memory.RAM[Registers.S.W--] = 0xf4;
    S9xSetPCBase(wiizvCharacterEdit == WiizvCharacterEdit::Items ? 0x009fa2 : 0x00b8c4);
}
