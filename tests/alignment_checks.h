#pragma once

inline void runAlignmentResumeChecks(Player& session)
{
    const auto* ram = session.gameRam();
    require(session.mixedAlignment && ram[0xdb] == 2 && (ram[0x100a] & 3) == 0 && (ram[0x108a] & 3) == 2,
        "再起動後の善悪混合設定・パーティを復元できません。");
    const std::string report = "PASS: mixed-alignment setting and Good/Evil party restored after restart\n";
    writeFile(session.dataDirectory / L"alignment-resume-report.txt", report.data(), report.size());
}

inline void runAlignmentChecks(Player& session)
{
    require(session.core.set_mixed_alignment && !session.mixedAlignment, "善悪混合検証の開始状態が不正です。");
    session.setSpeed(0);
    session.replayInput = true;
    auto tap = [&](uint16_t buttons, int after = 70)
    {
        session.replayButtons = buttons; runTestFrames(session, 10);
        session.replayButtons = 0; runTestFrames(session, after);
    };
    tap(8, 240); tap(256, 590);
    auto* ram = session.gameRam();
    require(ram[0xdb] == 0, "開始時のパーティが空ではありません。");
    const auto castle = encodeState(session.captureState(), session.romId, session.coreId);
    auto candidates = [&]
    {
        require(ram[0x35] == 0x61 && ram[0x36] == 0 && ram[0x34] <= 20, "酒場の加入一覧に入りません。");
        return std::vector<uint8_t>(ram + 0x300, ram + 0x300 + ram[0x34]);
    };
    auto prepare = [&](int leader, int recruit)
    {
        session.applyStateFile(castle);
        ram[0x100a] = static_cast<uint8_t>((ram[0x100a] & 0xfc) | leader);
        ram[0x108a] = static_cast<uint8_t>((ram[0x108a] & 0xfc) | recruit);
        tap(256); tap(256); tap(256);
        require(ram[0xdb] == 1 && ram[0xdc] == 0, "酒場で先頭キャラクターを加入できません。");
    };
    for (int speed = 0; speed < 5; ++speed)
    {
        session.setSpeed(speed);
        for (int leader = 0; leader < 3; ++leader)
        {
            for (int recruit = 0; recruit < 3; ++recruit)
            {
                prepare(leader, recruit);
                const auto normal = candidates();
                const bool allowed = leader == recruit || leader == 1 || recruit == 1;
                require((std::find(normal.begin(), normal.end(), 1) != normal.end()) == allowed, "OFFの属性制限が原作と一致しません。");
                const auto leaderData = ram[0x100a], recruitData = ram[0x108a];
                session.toggleMixedAlignment();
                tap(1); tap(256); // 候補一覧を開き直す。
                const auto mixed = candidates();
                require(mixed == std::vector<uint8_t>({1, 2, 3, 4, 5}), "ONの候補に未登録・加入済みの枠が混入したか、属性制限が残りました。");
                tap(256);
                require(ram[0xdb] == 2 && ram[0xdd] == 1, "ONで選択した仲間が加入しません。");
                session.toggleMixedAlignment();
                require(ram[0xdb] == 2 && ram[0x100a] == leaderData && ram[0x108a] == recruitData,
                    "混合パーティの切替でメンバー・種族・職業・属性が変わりました。");
            }
        }
    }
    // 一覧表示中にOFFへ戻した場合も、表示済みの悪キャラクターを加入させない。
    prepare(0, 2);
    session.toggleMixedAlignment(); tap(1); tap(256);
    const auto cached = encodeState(session.captureState(), session.romId, session.coreId);
    session.toggleMixedAlignment(); tap(256);
    require(ram[0xdb] == 1, "OFFにした後もキャッシュ済み候補から善悪混合で加入できました。");
    tap(256);
    const auto restored = candidates();
    require(std::find(restored.begin(), restored.end(), 1) == restored.end(), "OFF後の一覧に悪の仲間が残りました。");

    // 共用されている訓練場の候補抽出は変更しない。
    tap(1); tap(1); tap(16); tap(256); tap(32); tap(256);
    const auto trainingMenu = encodeState(session.captureState(), session.romId, session.coreId);
    tap(32); tap(32); tap(256);
    require(ram[0x35] == 0xc1 && ram[0x36] == 0 && ram[0x34] > 0 && ram[0x34] <= 20, "訓練場の候補一覧に入りません。");
    const std::vector<uint8_t> trainingOff(ram + 0x300, ram + 0x300 + ram[0x34]);
    session.applyStateFile(trainingMenu);
    session.toggleMixedAlignment();
    tap(32); tap(32); tap(256);
    require(ram[0x35] == 0xc1 && ram[0x36] == 0, "ONで訓練場の候補一覧に入りません。");
    require(std::vector<uint8_t>(ram + 0x300, ram + 0x300 + ram[0x34]) == trainingOff, "酒場以外の候補抽出が変化しました。");

    session.applyStateFile(castle); tap(256); tap(256);
    for (int member = 0; member < 6; ++member) tap(256);
    require(ram[0xdb] == 6, "6人までパーティを編成できません。");
    tap(256);
    require(ram[0xdb] == 6, "6人の加入上限を超えました。");

    session.applyStateFile(cached); tap(256);
    require(ram[0xdb] == 2 && (ram[0x100a] & 3) == 0 && (ram[0x108a] & 3) == 2, "善悪混合パーティを復元・編成できません。");
    session.replayInput = false;
    session.toggleMenu(); session.menuRow = 10;
    const auto beforeToggle = memoryHash(session);
    SDL_Event event{}; event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
    require(handleAction(session, event) && !session.mixedAlignment, "パッドから善悪混合設定をOFFにできません。");
    event.type = SDL_EVENT_KEY_DOWN; event.key.scancode = SDL_SCANCODE_RETURN;
    require(handleAction(session, event) && session.mixedAlignment && memoryHash(session) == beforeToggle, "設定切替でゲーム状態が変化しました。");
    session.notice.clear(); session.uiScreenshot = session.dataDirectory / L"alignment-menu.bmp"; session.draw();
    session.toggleMenu();
    const auto temporary = session.dataDirectory / L"settings.ini.tmp";
    fs::create_directory(temporary);
    bool rejected = false;
    try { session.toggleMixedAlignment(); } catch (const std::exception&) { rejected = true; }
    fs::remove(temporary);
    require(rejected && session.mixedAlignment && memoryHash(session) == beforeToggle, "設定保存失敗時に善悪混合設定が変化しました。");
    session.saveState(1);
    const std::string report = "PASS: all nine alignment combinations at every speed; unchanged character attributes; OFF/cached-list rejection; training unchanged; six-member limit; menu; write failure\n";
    writeFile(session.dataDirectory / L"alignment-check-report.txt", report.data(), report.size());
}
