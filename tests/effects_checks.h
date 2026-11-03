#pragma once

inline void checkEffectValues(Player& session, unsigned mask)
{
    const uint8_t expected[5]{static_cast<uint8_t>((mask & 1) ? 250 : 0),
        static_cast<uint8_t>((mask & 2) ? 125 : 0), static_cast<uint8_t>((mask & 2) ? 2 : 0),
        static_cast<uint8_t>((mask & 4) ? 200 : 0), static_cast<uint8_t>((mask & 8) ? 100 : 0)};
    require(std::equal(std::begin(expected), std::end(expected), session.gameRam() + 0xc9), "常時効果の値が一致しません。");
}

inline void runEffectsResumeChecks(Player& session)
{
    require(session.effectMask == 15, "再起動後に常時効果の設定が維持されません。");
    checkEffectValues(session, 15);
    const std::string result = "PASS: all four effect settings restored after restart and state load\n";
    writeFile(session.dataDirectory / L"effects-resume-report.txt", result.data(), result.size());
}

inline void runEffectsChecks(Player& session)
{
    require(session.supportsEffects() && session.effectMask == 0, "常時効果の検証開始状態が不正です。");
    session.setSpeed(0);
    session.replayInput = true;
    auto tap = [&](uint16_t buttons, int after = 100)
    {
        session.replayButtons = buttons; runTestFrames(session, 10);
        session.replayButtons = 0; runTestFrames(session, after);
    };
    tap(8, 240); tap(256, 590); tap(256); tap(256); tap(256);
    for (int i = 0; i < 5; ++i) tap(256, 50);
    tap(1); tap(16, 40); tap(256, 80); tap(256, 300);
    tap(1); tap(32, 20); tap(256, 60); tap(16, 60);
    auto* ram = session.gameRam();
    require(session.core.can_move() && ram[0xb5] == 0 && ram[0xb6] == 1, "効果検証用の探索状態に入れません。");
    const auto baseline = encodeState(session.captureState(), session.romId, session.coreId);

    // OFFのままなら原作の通常呪文がそのまま減少する。
    const uint8_t normal[5]{42, 17, 2, 33, 44};
    std::copy(std::begin(normal), std::end(normal), ram + 0xc9);
    ram[0xc7] = 9;
    tap(16, 60);
    const uint8_t decremented[5]{41, 16, 2, 32, 43};
    require(std::equal(std::begin(decremented), std::end(decremented), ram + 0xc9), "常時OFFで通常の呪文効果を変更しました。");

    for (unsigned effect = 0; effect < 4; ++effect)
    {
        session.applyStateFile(baseline);
        session.toggleEffect(effect);
        checkEffectValues(session, 1u << effect);
        ram[0xc7] = 9;
        tap(16, 60);
        checkEffectValues(session, 1u << effect);
        require(std::equal(ram + 0xc9, ram + 0xce, ram + 0x105b), "パーティへの効果同期が一致しません。");
        session.toggleEffect(effect);
        checkEffectValues(session, 0);
        for (unsigned character = 0; character < 20; ++character)
        {
            const auto base = 0x105b + character * 0x80;
            const unsigned first = effect == 0 ? 0 : effect == 1 ? 1 : effect + 1;
            require(ram[base + first] == 0 && (effect != 1 || ram[base + 2] == 0), "OFFの効果がキャラクターに残りました。");
        }
    }
    session.applyStateFile(baseline);
    for (unsigned effect = 0; effect < 4; ++effect) session.toggleEffect(effect);
    for (int speed = 0; speed < static_cast<int>(GAME_SPEEDS.size()); ++speed)
    {
        session.setSpeed(speed);
        session.applyStateFile(baseline);
        checkEffectValues(session, 15);
        ram[0xc7] = 9;
        tap(16, 60);
        checkEffectValues(session, 15);
    }
    session.setSpeed(0);
    session.applyStateFile(baseline);
    ram[0xee] = 1;
    tap(16, 240);
    require(!session.core.can_move(), "効果検証用の戦闘に入れません。");
    checkEffectValues(session, 15);
    session.applyStateFile(baseline);
    session.replayInput = false;
    session.inputBlocked = true;

    // 暗闇で原作が灯りを消す命令を実行した後も、ONの効果を維持する。
    const auto dark = std::find_if(session.dungeonMaps[0].begin(), session.dungeonMaps[0].end(), [](const auto& cell)
    {
        return (cell.flags & 0x40) && cell.event == 0;
    });
    require(dark != session.dungeonMaps[0].end(), "暗闇の検証先がありません。");
    session.openMap(); session.mapX = dark->x; session.mapY = dark->y;
    session.executeMapMove(); runTestFrames(session, 60);
    checkEffectValues(session, 15);

    // UIから独立した切替、設定書込失敗時の維持、読込後の再適用を確認する。
    session.applyStateFile(baseline);
    session.toggleMenu(); session.menuRow = 7; session.activateMenu();
    require(session.effectsMenu && session.menuRows() == 6, "常時効果メニューを開けません。");
    session.menuRow = 0; session.activateMenu();
    require(session.effectMask == 14, "常時効果メニューで切り替わりません。");
    session.activateMenu(); checkEffectValues(session, 15);
    session.notice.clear();
    session.uiScreenshot = session.dataDirectory / L"effects-menu.bmp";
    session.draw();
    SDL_Event back{};
    back.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN; back.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    handleAction(session, back);
    require(!session.effectsMenu && session.menuOpen && session.menuRow == 7, "パッドで機能メニューへ戻れません。");
    session.toggleMenu();
    auto temporary = session.dataDirectory / L"settings.ini.tmp";
    fs::create_directory(temporary);
    bool rejected = false;
    const auto before = memoryHash(session);
    try { session.toggleEffect(0); } catch (const std::exception&) { rejected = true; }
    fs::remove(temporary);
    require(rejected && session.effectMask == 15 && memoryHash(session) == before, "設定保存に失敗しても常時効果が変更されました。");

    session.saveState(1);
    session.applyStateFile(baseline);
    checkEffectValues(session, 15);
    const auto identity = session.romId;
    session.romId = "unsupported";
    rejected = false;
    try { session.toggleEffect(0); } catch (const std::exception&) { rejected = true; }
    session.romId = identity;
    require(rejected && session.effectMask == 15, "未対応ROMで専用効果を有効にできました。");
    const std::string result = "PASS: four independent effects; normal spells unchanged when disabled; walking timer; party sync; OFF clearing; all speeds; battle; darkness; state load reapplication; menu and pad; settings write failure; unsupported ROM\n";
    writeFile(session.dataDirectory / L"effects-check-report.txt", result.data(), result.size());
}
