#pragma once

inline void runBonusResumeChecks(Player& session)
{
    require(session.maximumBonus && session.gameRam()[0x9d] == 59, "再起動後のボーナス設定・配分状態が不正です。");
    const std::string report = "PASS: maximum bonus setting restored; allocated state remains at 59 points\n";
    writeFile(session.dataDirectory / L"bonus-resume-report.txt", report.data(), report.size());
}

inline void runBonusChecks(Player& session)
{
    require(session.core.set_max_bonus && !session.maximumBonus, "ボーナス検証開始状態が不正です。");
    session.setSpeed(0);
    session.replayInput = true;
    auto tap = [&](uint16_t buttons, int after = 100)
    {
        session.replayButtons = buttons; runTestFrames(session, 10);
        session.replayButtons = 0; runTestFrames(session, after);
    };
    tap(8, 240); tap(256, 590); tap(16); tap(256); tap(32); tap(256);
    tap(256); tap(256); tap(8); tap(256);
    auto* ram = session.gameRam();
    auto registeredCount = [&]
    {
        int count = 0;
        for (int character = 0; character < 20; ++character)
            if (ram[0x1000 + character * 0x80] != 0) ++count;
        return count;
    };
    const int registeredBefore = registeredCount();
    const auto beforeRace = encodeState(session.captureState(), session.romId, session.coreId);
    tap(256); tap(256);
    require(ram[0x9d] >= 7 && ram[0x9d] < 60, "OFF時の通常抽選を確認できません。");
    const auto normal = ram[0x9d];
    session.toggleMaximumBonus();
    require(ram[0x9d] == normal, "ONにしただけで既存の配分ポイントが変化しました。");

    for (int speed = 0; speed < 5; ++speed)
    {
        session.setSpeed(speed);
        for (int race = 0; race < 5; ++race)
        {
            session.applyStateFile(beforeRace);
            for (int row = 0; row < race; ++row) tap(32);
            tap(256); tap(256);
            require(ram[0x9d] == 60, "新規キャラクターのボーナスが60になりません。");
            const auto strength = ram[0x94];
            tap(128);
            require(ram[0x9d] == 59 && ram[0x94] == strength + 1, "ポイントを正常に配分できません。");
            tap(64);
            require(ram[0x9d] == 60 && ram[0x94] == strength, "配分を正常に戻せません。");
            if (race == 0 && speed == 0)
            {
                session.uiScreenshot = session.dataDirectory / L"bonus-allocation.bmp"; session.draw();
            }
        }
    }
    // 全60ポイントを使い切り、原作の職業選択と登録まで進める。
    for (int stat = 0; stat < 6; ++stat)
    {
        for (int point = 0; point < 10; ++point) tap(128, 30);
        tap(32, 30);
    }
    require(ram[0x9d] == 0, "60ポイントを使い切れません。");
    tap(256); tap(256); tap(256, 300);
    session.uiScreenshot = session.dataDirectory / L"bonus-created.bmp"; session.draw();
    require(registeredCount() == registeredBefore + 1, "作成したキャラクターを登録できません。");

    session.applyStateFile(beforeRace);
    tap(256); tap(256); tap(128);
    const auto allocated = encodeState(session.captureState(), session.romId, session.coreId);
    session.toggleMaximumBonus();
    require(ram[0x9d] == 59, "OFFで配分中のポイントが変化しました。");
    session.applyStateFile(beforeRace); tap(256); tap(256);
    require(ram[0x9d] >= 7 && ram[0x9d] < 60, "OFF後も最大化が残りました。");
    session.applyStateFile(allocated);
    session.replayInput = false;
    session.toggleMenu(); session.menuRow = 8; session.activateMenu();
    require(session.maximumBonus && ram[0x9d] == 59, "メニュー切替で配分状態が変化しました。");
    session.notice.clear(); session.uiScreenshot = session.dataDirectory / L"bonus-menu.bmp"; session.draw();
    session.toggleMenu();
    const auto beforeFailure = memoryHash(session);
    const auto temporary = session.dataDirectory / L"settings.ini.tmp";
    fs::create_directory(temporary);
    bool rejected = false;
    try { session.toggleMaximumBonus(); } catch (const std::exception&) { rejected = true; }
    fs::remove(temporary);
    require(rejected && session.maximumBonus && memoryHash(session) == beforeFailure, "設定保存失敗時に状態が変化しました。");
    session.saveState(1);
    const std::string report = "PASS: real creation at every speed and all five races; 60 points; allocation/refund; normal rolls when OFF; state isolation; menu; write failure\n";
    writeFile(session.dataDirectory / L"bonus-check-report.txt", report.data(), report.size());
}
