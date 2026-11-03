static unsigned wiizvEffectMask = 0;
static bool wiizvMaximumBonus = false;
static bool wiizvSkipMakerLogo = false;
static bool wiizvMixedAlignment = false;

extern "C" WIIZV_EXPORT bool wiizv_set_mixed_alignment(bool enabled)
{
    if (enabled && Memory.ROMCRC32 != 0x547cf7b9) return false;
    wiizvMixedAlignment = enabled;
    return true;
}

static bool wiizvTavernAllows(unsigned character)
{
    if (character >= 20 || Memory.RAM[0xdb] > 6) return false;
    const unsigned alignment = Memory.RAM[0x100a + character * 0x80] & 3;
    if (alignment > 2) return false;
    for (unsigned member = 0; member < Memory.RAM[0xdb]; ++member)
    {
        const unsigned other = Memory.RAM[0xdc + member];
        if (other >= 20) return false;
        const unsigned otherAlignment = Memory.RAM[0x100a + other * 0x80] & 3;
        if ((alignment == 0 && otherAlignment == 2) || (alignment == 2 && otherAlignment == 0)) return false;
    }
    return true;
}

static inline void wiizvTavernHook()
{
    if (Memory.ROMCRC32 != 0x547cf7b9) return;
    if (Registers.PBPC == 0x048341)
    {
        const unsigned stack = Registers.S.W;
        // 候補抽出は訓練場でも共用するため、酒場からのJSRだけを対象にする。
        if (stack < 0x1ffe && Memory.RAM[stack + 2] == 0x80 &&
            (Memory.RAM[stack + 1] == 0x62 || Memory.RAM[stack + 1] == 0xc2))
        {
            // 編集で変わった属性を使い、一覧作成時の古い属性マスクを補正する。
            const bool allowed = wiizvMixedAlignment || wiizvTavernAllows((Registers.X.W - 0x1000) / 0x80);
            if (wiizvMixedAlignment || allowed != (Registers.AL == 0))
                S9xSetPCBase(allowed ? 0x048343 : 0x048346);
        }
    }
    else if (!wiizvMixedAlignment && Registers.PBPC == 0x048095 && Registers.AL < 0x80)
    {
        // ONで開いた候補一覧からOFFにした場合も、属性制限に反する加入を防ぐ。
        const unsigned character = Memory.RAM[0x300 + Registers.AL];
        if (!wiizvTavernAllows(character))
            S9xSetPCBase(0x0480c6);
    }
}

extern "C" WIIZV_EXPORT bool wiizv_set_skip_logo(bool enabled)
{
    if (enabled && Memory.ROMCRC32 != 0x547cf7b9) return false;
    wiizvSkipMakerLogo = enabled;
    return true;
}

extern "C" WIIZV_EXPORT bool wiizv_set_max_bonus(bool enabled)
{
    if (enabled && Memory.ROMCRC32 != 0x547cf7b9) return false;
    wiizvMaximumBonus = enabled;
    return true;
}

extern "C" WIIZV_EXPORT void wiizv_normalize_mp()
{
    if (Memory.ROMCRC32 != 0x547cf7b9) return;
    // 最大回数は変更せず、ON中の9回が上限を超えていれば元の範囲に戻す。
    for (unsigned character = 0; character < 20; ++character)
    {
        if (Memory.RAM[0x1000 + character * 0x80] == 0) continue;
        for (unsigned level = 0; level < 14; ++level)
        {
            auto& mp = Memory.RAM[0x1027 + character * 0x80 + level];
            const unsigned maximum = mp & 15;
            if ((mp >> 4) > maximum) mp = static_cast<uint8>((maximum << 4) | maximum);
        }
    }
}

static void wiizvApplyMp()
{
    if (!(wiizvEffectMask & 16) || Memory.RAM[0xdb] == 0 || Memory.RAM[0xdb] > 6 || Memory.RAM[0xb7] > 8) return;
    for (unsigned member = 0; member < Memory.RAM[0xdb]; ++member)
    {
        const unsigned character = Memory.RAM[0xdc + member];
        if (character >= 20 || Memory.RAM[0x1000 + character * 0x80] == 0) continue;
        for (unsigned level = 0; level < 14; ++level)
        {
            auto& mp = Memory.RAM[0x1027 + character * 0x80 + level];
            mp = (mp & 15) | 0x90;
        }
    }
}

static bool wiizvEffectParty()
{
    return Memory.RAM[0xdb] > 0 && Memory.RAM[0xdb] <= 6 &&
        Memory.RAM[0xb7] >= 1 && Memory.RAM[0xb7] <= 8;
}

static void wiizvApplyEffects()
{
    if (!wiizvEffectMask || !wiizvEffectParty()) return;
    if (wiizvEffectMask & 1) Memory.RAM[0xc9] = 250;
    if (wiizvEffectMask & 2) { Memory.RAM[0xca] = 125; Memory.RAM[0xcb] = 2; }
    if (wiizvEffectMask & 4) Memory.RAM[0xcc] = 200;
    if (wiizvEffectMask & 8) Memory.RAM[0xcd] = 100;
}

extern "C" WIIZV_EXPORT bool wiizv_set_effects(unsigned mask)
{
    if (mask > 31) return false;
    const unsigned removed = wiizvEffectMask & ~mask;
    if (removed & 16) wiizv_normalize_mp();
    const unsigned fields[5]{0xc9, 0xca, 0xcb, 0xcc, 0xcd};
    const unsigned bits[5]{1, 2, 2, 4, 8};
    for (unsigned field = 0; field < 5; ++field)
    {
        if (!(removed & bits[field])) continue;
        Memory.RAM[fields[field]] = 0;
        // OFFにした効果は、登録キャラクターに保存された残り時間も解除する。
        for (unsigned character = 0; character < 20; ++character)
            Memory.RAM[0x1000 + character * 0x80 + 0x5b + field] = 0;
    }
    wiizvEffectMask = mask;
    wiizvApplyEffects();
    wiizvApplyMp();
    return true;
}

static inline void wiizvEffectsHook()
{
    wiizvTavernHook();
    // 初期化と起動済みマーカーの記録は残し、ロゴ表示とその待機だけを省く。
    if (wiizvSkipMakerLogo && Memory.ROMCRC32 == 0x547cf7b9)
    {
        if (Registers.PBPC == 0x008491) S9xSetPCBase(0x0084ae);
        else if (Registers.PBPC == 0x008462) S9xSetPCBase(0x008485);
    }
    // 新規作成の抽選結果だけを原作上限60にする。配分中の残りポイントには触れない。
    if (wiizvMaximumBonus && Memory.ROMCRC32 == 0x547cf7b9 && Registers.PBPC == 0x04a4a4)
        Registers.AL = 60;
    // 通常セーブから迷宮に入る際も、OFFなら保存済みの9回を本来の範囲へ戻す。
    if ((Registers.PBPC == 0x04a840 || Registers.PBPC == 0x04c082) && !(wiizvEffectMask & 16)) wiizv_normalize_mp();
    if (!wiizvEffectMask) return;
    if (wiizvEffectMask & 16)
    {
        // 呪文回数の確認・消費、およびMP吸収処理の境界で9回を維持する。
        switch (Registers.PBPC)
        {
        case 0x04a840: case 0x04aa2f: case 0x04aa50:
        case 0x05b70d: case 0x05b718:
        case 0x05dec8: case 0x05dee0:
            wiizvApplyMp();
            break;
        }
    }
    // 効果の読込・減少・解除と描画・パーティ同期の境界で再適用する。
    switch (Registers.PBPC)
    {
    case 0x04adac: case 0x04c082:
    case 0x0582ef: case 0x059da0: case 0x05a622: case 0x05a64e:
    case 0x05c6cb: case 0x05c78e: case 0x05c828:
        wiizvApplyEffects();
        break;
    }
}
