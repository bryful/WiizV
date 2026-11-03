#pragma once

inline bool logoTitleReady(Player& session)
{
    const auto* ram = session.gameRam();
    return ram[0x1a] == 0xd5 && ram[0x1b] == 0x1f &&
        ram[0x1fda] == 0xe3 && ram[0x1fdb] == 0xdf && ram[0x1fdc] == 0 && ram[0xf6] == 0x3f;
}

inline void runLogoResumeChecks(Player& session)
{
    require(session.skipMakerLogo && logoTitleReady(session), "再起動後にロゴスキップとタイトル到達を確認できません。");
    const std::string report = "PASS: skip-logo setting restored; cold boot reaches title within 360 frames\n";
    writeFile(session.dataDirectory / L"logo-resume-report.txt", report.data(), report.size());
}

inline void runLogoChecks(Player& session)
{
    require(session.core.set_skip_logo && !session.skipMakerLogo, "ロゴ検証開始状態が不正です。");
    session.replayInput = true;
    session.replayButtons = 0;
    auto* ram = session.gameRam();
    const std::vector<uint8_t> marker(ram + 0x1ff0, ram + 0x2000);
    auto reachTitle = [&]
    {
        runTestFrames(session, 1);
        int frames = 1;
        while (!logoTitleReady(session) && frames < 3600) { runTestFrames(session, 1); ++frames; }
        require(logoTitleReady(session), "起動後にタイトルの入力待ちに入りません。");
        return frames;
    };
    std::ostringstream report;
    for (int speed = 0; speed < 5; ++speed)
    {
        session.setSpeed(speed);
        session.core.reset();
        std::fill(ram + 0x1ff0, ram + 0x2000, uint8_t{0});
        const int normalFrames = reachTitle();
        const std::vector<uint8_t> normalCharacters(ram + 0x1000, ram + 0x1a00);
        session.toggleSkipMakerLogo();
        session.core.reset();
        std::fill(ram + 0x1ff0, ram + 0x2000, uint8_t{0});
        const int skippedFrames = reachTitle();
        require(skippedFrames + 200 < normalFrames, "ロゴ表示の待機時間が省略されません: " + std::to_string(normalFrames) + "/" + std::to_string(skippedFrames));
        require(std::equal(normalCharacters.begin(), normalCharacters.end(), ram + 0x1000), "スキップ起動でキャラクターデータが変化しました。");
        require(std::equal(marker.begin(), marker.end(), ram + 0x1ff0), "起動済みマーカーが記録されません。");
        session.core.reset();
        std::copy(marker.begin(), marker.end(), ram + 0x1ff0);
        const int warmFrames = reachTitle();
        require(warmFrames < normalFrames, "起動済みマーカーがあるリセットで待機が残りました。");
        report << "speed=" << session.gameSpeed() << " normal=" << normalFrames << " skipped=" << skippedFrames << " warm=" << warmFrames << '\n';
        session.toggleSkipMakerLogo();
    }
    session.setSpeed(0);
    session.core.reset();
    std::fill(ram + 0x1ff0, ram + 0x2000, uint8_t{0});
    runTestFrames(session, 300);
    require(!logoTitleReady(session), "OFF後もロゴを省略しました。");
    const auto duringLogo = encodeState(session.captureState(), session.romId, session.coreId);
    reachTitle();
    // ロゴ中のステートも復元でき、設定変更は表示中の画面へ割り込まない。
    session.toggleSkipMakerLogo();
    session.applyStateFile(duringLogo);
    require(!logoTitleReady(session), "ロゴ中のステートを正しく復元できません。");
    reachTitle();
    const auto title = encodeState(session.captureState(), session.romId, session.coreId);
    session.replayButtons = 8; runTestFrames(session, 10);
    session.replayButtons = 0; runTestFrames(session, 240);
    session.replayButtons = 256; runTestFrames(session, 10);
    session.replayButtons = 0; runTestFrames(session, 590);
    require(ram[0x23] > 0 && !logoTitleReady(session), "スキップ起動後にゲームを開始できません。");
    session.applyStateFile(title);
    session.replayInput = false;
    session.toggleMenu();
    const auto beforeToggle = memoryHash(session);
    const auto box = session.menuBounds();
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN; event.button.button = SDL_BUTTON_LEFT;
    event.button.x = box.x + 40; event.button.y = box.y + 84 + 9 * session.menuRowHeight();
    require(handleAction(session, event) && !session.skipMakerLogo, "マウスからロゴ設定を切り替えられません。");
    event.type = SDL_EVENT_KEY_DOWN; event.key.scancode = SDL_SCANCODE_RETURN;
    require(handleAction(session, event) && session.skipMakerLogo && memoryHash(session) == beforeToggle, "メニュー切替でゲーム状態が変化しました。");
    session.notice.clear(); session.uiScreenshot = session.dataDirectory / L"logo-menu.bmp"; session.draw();
    session.toggleMenu();
    const auto temporary = session.dataDirectory / L"settings.ini.tmp";
    fs::create_directory(temporary);
    bool rejected = false;
    try { session.toggleSkipMakerLogo(); } catch (const std::exception&) { rejected = true; }
    fs::remove(temporary);
    require(rejected && session.skipMakerLogo && memoryHash(session) == beforeToggle, "保存失敗時にロゴ設定が変化しました。");
    report << "PASS: all speeds; cold/warm reset; original character data; OFF; logo-state restoration; game start; mouse/keyboard menu; write failure\n";
    const auto text = report.str();
    writeFile(session.dataDirectory / L"logo-check-report.txt", text.data(), text.size());
}
