#pragma once

inline void runExperienceResumeChecks(Player& session)
{
    uint64_t experience = 0;
    require(session.loadedSavedRam && characterData::readExperience(session.gameRam() + 0x1000, experience) &&
        experience == 165243 && characterData::level(session.gameRam() + 0x1000) == 1,
        "通常セーブから経験値と未変更のレベルを復元できません。");
    const std::string report = "PASS: original game validates edited EXP from SRAM; level remains 1 after restart\n";
    writeFile(session.dataDirectory / L"experience-resume-report.txt", report.data(), report.size());
}

inline void runExperienceChecks(Player& session)
{
    require(session.core.request_experience, "経験値編集用コアがありません。");
    require(!session.core.request_experience(0, 1), "タイトル画面で編集が許可されました。");
    session.setSpeed(0); session.replayInput = true;
    auto tap = [&](uint16_t buttons, int after = 100)
    {
        session.replayButtons = buttons; runTestFrames(session, 10);
        session.replayButtons = 0; runTestFrames(session, after);
    };
    tap(8, 240); tap(256, 590);
    const auto baseline = encodeState(session.captureState(), session.romId, session.coreId);
    auto* ram = session.gameRam();
    auto* record = ram + 0x1000;
    session.toggleMenu(); session.openCharacterMenu(); session.editCharacter = 0;
    for (int slot = 1; slot <= 5; ++slot) session.saveState(slot);
    std::array<std::vector<uint8_t>, 5> slots;
    for (int slot = 1; slot <= 5; ++slot) slots[slot - 1] = readFile(session.slotPath(slot));

    // ROMのSFC版実値。侍・ロードのLv3は参考Web表の値とは異なる。
    constexpr uint64_t early[8][3] = {{800,1666,2776}, {1000,2083,3471}, {900,1833,3055}, {750,1583,2638},
        {1200,2416,4026}, {1100,2215,3579}, {1100,2215,3579}, {1100,2284,3865}};
    constexpr uint64_t level13[8] = {275405,344388,303083,261693,399466,407346,407346,439900};
    constexpr uint64_t level12[8] = {165243,206633,181850,157016,239680,240705,240705,259941};
    constexpr uint64_t increment[8] = {225405,294388,253083,211693,349466,357346,357346,379900};
    constexpr uint64_t twice[8][3] = {{1666,4626,12850}, {2083,5785,16068}, {1833,5091,14141}, {1583,4396,12210},
        {2416,6710,18638}, {2215,6056,17342}, {2215,6056,17342}, {2284,6540,18728}};
    for (unsigned kind = 0; kind < 8; ++kind)
        for (unsigned levels = 1; levels <= 3; ++levels)
        {
            session.applyStateFile(baseline);
            record[0xa] = static_cast<uint8_t>((record[0xa] & 0xe3) | (kind << 2));
            characterData::writeExperience(record, 500);
            session.experienceLevels = levels;
            require(session.targetExperience() == early[kind][levels - 1], "現在の経験値からの到達点が一致しません。");
            uint64_t gain = 0;
            require(characterData::experienceGain(record, session.rom.data(), session.rom.size(), levels, gain) &&
                gain == early[kind][levels - 1] - 500, "不足分だけの加算量になりません。");
            const std::vector<uint8_t> before(ram, ram + 0x20000);
            session.executeExperienceAdd();
            require(session.characterExperience() == early[kind][levels - 1] && characterData::level(record) == 1,
                "経験値の変更でレベルが変化しました。");
            for (size_t byte = 0; byte < before.size(); ++byte)
                if ((byte < 0x1017 || byte > 0x101c) && byte != 0x107e && byte != 0x107f)
                    require(before[byte] == ram[byte], "経験値以外のパラメータ・場面状態が変化しました。");
            session.executeExperienceAdd();
            require(session.characterExperience() == twice[kind][levels - 1] && characterData::level(record) == 1,
                "繰り返し加算がレベル相当の経験値で制限されました。");
            session.restoreEditBackup();
            require(session.characterExperience() == early[kind][levels - 1], "経験値の復元に失敗しました。");
            for (unsigned current : {12u,13u,99u,999u,9999u,16383u,65535u})
            {
                const auto start = current == 12 ? level12[kind] : level13[kind] + increment[kind] * (current - 13);
                characterData::writeExperience(record, start + 123);
                const auto expected = level13[kind] + increment[kind] * (current + levels - 13);
                require(session.targetExperience() == expected, "高レベル・13境界の必要経験値が一致しません。");
            }
        }

    for (int speed = 0; speed < 5; ++speed)
    {
        session.applyStateFile(baseline); session.setSpeed(speed); session.experienceLevels = 3;
        record[0x21] = 12;
        characterData::writeExperience(record, 200000);
        session.executeExperienceAdd();
        require(session.characterExperience() == 726215 && characterData::level(record) == 12, "倍速時の経験値加算に失敗しました。");
        session.restoreEditBackup();
    }
    session.applyStateFile(baseline); session.setSpeed(0); session.experienceLevels = 1;
    unsigned rejectionCase = 0;
    auto rejects = [&]
    {
        const auto before = memoryHash(session);
        bool rejected = false;
        try { session.executeExperienceAdd(); } catch (const std::exception&) { rejected = true; }
        require(rejected && memoryHash(session) == before, "無効データ・保存失敗で編集前の状態を維持できません: case " + std::to_string(++rejectionCase));
    };
    record[0x17] = 0xfa; rejects();
    session.applyStateFile(baseline); record[0x21] = 0; rejects();
    for (unsigned current : {16383u,65535u})
    {
        session.applyStateFile(baseline); record[0x21] = static_cast<uint8_t>(current); record[0x22] = static_cast<uint8_t>(current >> 8);
        session.executeExperienceAdd();
        require(session.characterExperience() == 800 && characterData::level(record) == current, "実レベルによって経験値の到達点が変化しました。");
    }
    session.applyStateFile(baseline); session.experienceLevels = 1;
    for (uint64_t experience : {799ULL,800ULL,801ULL})
    {
        characterData::writeExperience(record, experience);
        require(session.targetExperience() == (experience < 800 ? 800 : 1666), "到達点の直前・一致・直後の判定が不正です。");
    }
    session.applyStateFile(baseline); session.editCharacter = 6; rejects(); session.editCharacter = 0;
    session.applyStateFile(baseline); session.experienceLevels = 0; rejects(); session.experienceLevels = 4; rejects(); session.experienceLevels = 1;
    require(!session.core.request_experience(20, 1) && !session.core.request_experience(0, 0) && !session.core.request_experience(0, 4),
        "コアが無効な指定を受理しました。");
    characterData::writeExperience(record, characterData::MAX_EXPERIENCE - 5);
    session.executeExperienceAdd(); require(session.characterExperience() == characterData::MAX_EXPERIENCE, "経験値の保存形式上限で桁あふれが起きました。");
    rejects();
    session.applyStateFile(baseline);
    auto blocked = session.editBackupPath(); blocked += L".tmp";
    fs::create_directory(blocked); rejects(); fs::remove(blocked);
    const auto saved = readFile(session.savePath);
    session.experienceLevels = 2;
    blocked = session.savePath; blocked += L".tmp";
    fs::create_directory(blocked); rejects(); fs::remove(blocked);
    require(readFile(session.savePath) == saved, "保存失敗で通常セーブが変更されました。");

    session.menuRow = 3; session.experienceLevels = 1; session.focused = true;
    SDL_Event event{}; event.type = SDL_EVENT_KEY_DOWN; event.key.scancode = SDL_SCANCODE_LEFT;
    require(handleAction(session, event) && session.experienceLevels == 3, "キーボードで＋1から＋3へ切り替えられません。");
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN; event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    require(handleAction(session, event) && session.experienceLevels == 1, "パッドで＋3から＋1へ切り替えられません。");
    session.experienceLevels = 3; session.menuRow = 4; session.activateMenu();
    require(session.characterExperience() == 2776, "メニューから経験値変更を実行できません。");
    session.notice.clear(); session.uiScreenshot = session.dataDirectory / L"experience-editor.bmp"; session.draw();
    session.menuRow = 7; session.activateMenu();
    require(session.characterExperience() == 0, "メニューから編集前の状態へ戻せません。");
    session.backMenu(); session.toggleMenu();

    // 原作の宿屋で実際に成長し、EXP編集時には成長処理が走っていないことを確かめる。
    tap(256); tap(256); tap(256); tap(1); tap(1);
    require(ram[0x35] == 0xc5 && ram[0x36] == 0 && ram[0xdb] == 1, "宿屋検証の城へ戻れません。");
    const auto partyCastle = encodeState(session.captureState(), session.romId, session.coreId);
    auto checkInnGrowth = [&](unsigned current, unsigned kind)
    {
        session.applyStateFile(partyCastle);
        record[0x21] = static_cast<uint8_t>(current); record[0x22] = static_cast<uint8_t>(current >> 8);
        record[0xa] = static_cast<uint8_t>((record[0xa] & 0xe3) | (kind << 2));
        characterData::writeExperience(record, current == 13 ? level13[kind] : 0);
        session.toggleMenu(); session.openCharacterMenu(); session.experienceLevels = 3; session.executeExperienceAdd();
        session.backMenu(); session.toggleMenu();
        tap(32); tap(256); tap(256); tap(256, 300);
        // 成長メッセージの確認入力を含め、原作の操作だけで目標まで成長させる。
        for (int step = 0; step < 30 && characterData::level(record) < current + 3; ++step) tap(256, 200);
        require(characterData::level(record) == current + 3, "原作の宿屋で指定の＋3レベルまで成長できません。");
        for (int step = 0; step < 6; ++step) tap(256, 200);
        require(characterData::level(record) == current + 3, "原作の必要経験値と一致せず、余分に成長しました。");
    };
    checkInnGrowth(1, 0);
    checkInnGrowth(13, 7);

    session.applyStateFile(partyCastle); tap(256);
    auto checkScene = [&]
    {
        session.toggleMenu(); session.openCharacterMenu(); session.experienceLevels = 2;
        const auto previous = session.characterExperience();
        session.executeExperienceAdd(); require(characterData::level(record) == 1 && session.characterExperience() == 1666, "施設・迷宮で経験値を変更できません。");
        session.restoreEditBackup(); require(session.characterExperience() == previous, "施設・迷宮で経験値を復元できません。");
        session.backMenu(); session.toggleMenu();
    };
    checkScene(); // 酒場
    session.applyStateFile(partyCastle); tap(16); tap(256); tap(32); tap(256); checkScene(); // 訓練場
    session.applyStateFile(partyCastle); tap(16); tap(256); tap(256, 300); tap(1); tap(32, 20); tap(256, 60); tap(16, 60);
    require(session.core.can_move(), "探索待ちに入りません。"); checkScene();

    session.applyStateFile(baseline); session.toggleMenu(); session.openCharacterMenu(); session.experienceLevels = 3;
    characterData::writeExperience(record, 12345);
    session.executeExperienceAdd(); session.executeExperienceAdd();
    require(session.characterExperience() == 165243 && characterData::level(record) == 1, "既存の経験値から次の到達点へ連続加算できません。");
    for (int slot = 1; slot <= 5; ++slot) require(readFile(session.slotPath(slot)) == slots[slot - 1], "経験値編集で5スロットが変更されました。");
    const std::string report = "PASS: eight classes; next +1/+2/+3 EXP thresholds from current EXP; repeated execution; exact/below/above threshold; actual level independent; high EXP; all speeds; parameter/scene isolation; UI; backup/undo; invalid data/storage overflow; write-failure rollback; tavern/training/exploration; original inn growth; quick slots preserved\n";
    writeFile(session.dataDirectory / L"experience-check-report.txt", report.data(), report.size());
}
