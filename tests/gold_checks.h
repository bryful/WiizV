#pragma once

inline void runGoldResumeChecks(Player& session)
{
    uint64_t gold = 0;
    require(characterData::readGold(session.gameRam() + 0x1000, gold) && gold == 10000 && session.loadedSavedRam,
        "通常セーブから加算後のGOLDを復元できません。");
    require(characterData::allSpellsLearned(session.gameRam()+0x1000),"全呪文の習得を通常セーブから復元できません。");
    const std::string report = "PASS: original game validates SRAM checksums and restores added GOLD after restart\n";
    writeFile(session.dataDirectory / L"gold-resume-report.txt", report.data(), report.size());
}

inline void runGoldChecks(Player& session)
{
    require(session.core.can_edit_character && session.core.request_gold && session.core.cancel_gold, "編集用のコア拡張がありません。");
    require(!session.core.can_edit_character(), "タイトル画面で編集が許可されました。");
    session.setSpeed(0); session.replayInput = true;
    auto tap = [&](uint16_t buttons, int after = 100)
    {
        session.replayButtons = buttons; runTestFrames(session, 10);
        session.replayButtons = 0; runTestFrames(session, after);
    };
    tap(8, 240); tap(256, 590);
    require(session.core.can_edit_character(), "城のメイン画面を検出できません。");
    const auto baseline = encodeState(session.captureState(), session.romId, session.coreId);
    auto* ram = session.gameRam();
    for (int slot = 1; slot <= 5; ++slot) session.saveState(slot);
    std::array<std::vector<uint8_t>, 5> slots;
    for (int slot = 1; slot <= 5; ++slot) slots[slot - 1] = readFile(session.slotPath(slot));
    session.toggleMenu(); session.menuRow = 12; session.activateMenu();
    require(session.characterMenu, "GOLD編集画面を開けません。");
    session.notice.clear(); session.uiScreenshot = session.dataDirectory / L"gold-editor.bmp"; session.draw();
    for (int speed = 0; speed < 5; ++speed)
    {
        session.setSpeed(speed); session.applyStateFile(baseline);
        for (unsigned character = 0; character < 6; ++character)
        {
            session.editCharacter = character; session.goldStep = 4;
            const std::vector<uint8_t> records(ram + 0x1000, ram + 0x1a00);
            session.executeGoldAdd();
            require(session.characterGold() == 10000, "GOLDが加算されません。");
            const auto* sram = static_cast<const uint8_t*>(session.core.get_memory_data(RETRO_MEMORY_SAVE_RAM));
            for (size_t byte = 0; byte < records.size(); ++byte)
            {
                const auto offset = byte % 0x80;
                if (offset >= 0x7e || (byte / 0x80 == character && offset >= 0x11 && offset <= 0x16)) continue;
                require(records[byte] == ram[0x1000 + byte], "GOLD以外のキャラクターデータが変化しました。");
            }
            require(std::equal(ram + 0x1000, ram + 0x1a00, sram) && std::equal(ram + 0x1000, ram + 0x1a00, sram + 0xa00),
                "原作の二重SRAM保存に反映されません。");
        }
    }
    session.restoreEditBackup();
    require(session.characterGold() == 0, "編集前の状態へ復元できません。");

    session.applyStateFile(baseline); session.editCharacter = 19;
    std::copy(ram + 0x1000, ram + 0x1080, ram + 0x1980);
    session.executeGoldAdd(); require(session.characterGold() == 10000, "末尾の登録枠を編集できません。");
    session.applyStateFile(baseline); session.editCharacter = 0;
    characterData::writeGold(ram + 0x1000, characterData::MAX_GOLD - 5);
    session.executeGoldAdd(); require(session.characterGold() == characterData::MAX_GOLD, "GOLDの上限で正しく止まりません。");
    auto rejects = [&]
    {
        const auto before = memoryHash(session);
        bool rejected = false;
        try { session.executeGoldAdd(); } catch (const std::exception&) { rejected = true; }
        require(rejected && memoryHash(session) == before, "無効な編集を安全に拒否できません。");
    };
    rejects();
    session.applyStateFile(baseline); ram[0x1011] = 0xa0; rejects();
    session.applyStateFile(baseline); session.editCharacter = 6; rejects();
    session.editCharacter = 0;

    auto blocked = session.editBackupPath(); blocked += L".tmp";
    fs::create_directory(blocked); rejects(); fs::remove(blocked);
    const auto saveBefore = readFile(session.savePath);
    blocked = session.savePath; blocked += L".tmp";
    fs::create_directory(blocked); rejects(); fs::remove(blocked);
    require(readFile(session.savePath) == saveBefore, "保存失敗で通常セーブが変更されました。");
    session.executeGoldAdd();
    const auto backup = readFile(session.editBackupPath());
    const auto beforeInvalidBackup = memoryHash(session);
    auto invalid = backup; invalid.back() ^= 1;
    writeFile(session.editBackupPath(), invalid.data(), invalid.size());
    bool rejected = false;
    try { session.restoreEditBackup(); } catch (const std::exception&) { rejected = true; }
    require(rejected && memoryHash(session) == beforeInvalidBackup, "破損バックアップの復元で状態が変化しました。");
    writeFile(session.editBackupPath(), backup.data(), backup.size());
    session.restoreEditBackup(); require(session.characterGold() == 0, "バックアップ復元でGOLDが戻りません。");

    session.menuRow = 0;
    session.focused = true;
    SDL_Event event{}; event.type = SDL_EVENT_KEY_DOWN; event.key.scancode = SDL_SCANCODE_RIGHT;
    require(handleAction(session, event) && session.editCharacter == 1, "キーボードでキャラクターを選択できません。");
    session.menuRow = 1;
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN; event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    require(handleAction(session, event) && session.goldStep == 5, "パッドで加算額を変更できません。");
    session.backMenu(); session.toggleMenu();

    unsigned sceneIndex = 0;
    auto checkScene = [&](const char* name)
    {
        require(session.core.can_edit_character(), std::string(name) + "で編集を開けません。");
        const auto scene = encodeState(session.captureState(), session.romId, session.coreId);
        for (int speed = 0; speed < 5; ++speed)
        {
            session.setSpeed(speed); session.applyStateFile(scene);
            session.toggleMenu(); session.openCharacterMenu(); session.editCharacter = 0; session.goldStep = 4;
            const std::vector<uint8_t> beforeRam(ram, ram + 0x20000);
            const auto* sram = static_cast<const uint8_t*>(session.core.get_memory_data(RETRO_MEMORY_SAVE_RAM));
            const auto sramSize = session.core.get_memory_size(RETRO_MEMORY_SAVE_RAM);
            const std::vector<uint8_t> beforeSave(sram, sram + sramSize);
            const auto beforePixels = session.pixels;
            const auto previous = session.characterGold();
            session.executeGoldAdd();
            require(session.characterGold() == previous + 10000 && session.core.can_edit_character(),
                std::string(name) + "で編集または場面復帰に失敗しました。");
            for (size_t byte = 0; byte < beforeRam.size(); ++byte)
                if (byte < 0x1000 || byte >= 0x1080)
                    require(ram[byte] == beforeRam[byte], std::string(name) + "で場面の作業領域が変化しました。");
            for (size_t byte = 0; byte < beforeSave.size(); ++byte)
                if (byte >= 0x80 && (byte < 0xa00 || byte >= 0xa80))
                    require(sram[byte] == beforeSave[byte], std::string(name) + "で対象外の通常セーブが変化しました。");
            require(session.pixels == beforePixels, "編集中に元の画面が変化しました。");
            if (speed == 0)
            {
                session.notice.clear();
                session.uiScreenshot = session.dataDirectory / ("scene-" + std::to_string(sceneIndex++) + ".bmp");
                session.draw();
            }
            runTestFrames(session, 20);
            require(session.characterGold() == previous + 10000 && session.core.can_edit_character(),
                std::string(name) + "の再開後に編集結果が失われました。");
            session.restoreEditBackup();
            require(session.characterGold() == previous && session.core.can_edit_character(), "元の場面でバックアップ復元ができません。");
            session.backMenu(); session.toggleMenu();
        }
        session.setSpeed(0);
        // 編集後も元の場面が入力を受け付けられる状態で進行する。
        runTestFrames(session, 20);
    };
    session.applyStateFile(baseline);
    tap(256);
    checkScene("酒場");
    tap(256); checkScene("酒場の加入一覧");
    for (int member = 0; member < 6; ++member) tap(256, 50);
    tap(1); tap(16, 40); tap(256, 80); tap(256, 300);
    checkScene("キャンプ");
    tap(1); checkScene("階段の確認画面");
    tap(32, 20); tap(256, 60); tap(16, 60);
    checkScene("迷宮の移動中");
    const auto dungeon = encodeState(session.captureState(), session.romId, session.coreId);
    tap(128); require(session.core.can_move(), "編集後に迷宮の移動操作へ戻れません。");
    session.applyStateFile(dungeon);
    ram[0xee] = 1; tap(16, 240);
    require(!session.core.can_edit_character() && !session.core.request_gold(0, 1), "戦闘中に編集が許可されました。");

    session.applyStateFile(baseline); tap(16); tap(256); checkScene("郊外");
    tap(32); tap(256); checkScene("訓練場");
    const auto training = encodeState(session.captureState(), session.romId, session.coreId);
    tap(32); tap(32); tap(256); checkScene("訓練場のキャラクター一覧");
    tap(1); require(session.core.can_edit_character(), "編集後に訓練場の操作へ戻れません。");
    session.applyStateFile(training); tap(256); checkScene("訓練場の名前入力");
    tap(256); tap(8); tap(256); checkScene("訓練場の種族選択");

    session.applyStateFile(baseline); session.setSpeed(0);
    session.toggleMenu(); session.openCharacterMenu(); session.editCharacter = 0; session.goldStep = 4;
    const auto silentBaseline=encodeState(session.captureState(),session.romId,session.coreId);
    for(unsigned repeat=0;repeat<12;++repeat){
        session.applyStateFile(silentBaseline);session.editCharacter=repeat%6;session.goldStep=0;
        const auto videoBefore=session.videoFrames,audioBefore=session.audioFrames;
        session.executeGoldAdd();session.executeExperienceAdd();session.editAlignment=2;session.executeAlignmentChange();
        const std::vector<uint8_t> before(ram,ram+0x20000);
        const auto base=0x1000+session.editCharacter*0x80;
        session.executeLearnAllSpells();
        require(characterData::allSpellsLearned(ram+base),"全呪文を習得できません。");
        for(size_t offset=0;offset<before.size();++offset){
            if(offset>=base+0x35&&offset<base+0x43)continue;
            if(offset==base+0x7e||offset==base+0x7f)continue;
            require(before[offset]==ram[offset],"呪文習得で他のパラメータ・MP・場面が変化しました。");
        }
        require(session.videoFrames==videoBefore&&session.audioFrames==audioBefore,"編集中にエミュレーション・映像・音声が進行しました。");
        session.executeLearnAllSpells();session.restoreEditBackup();
        require(!characterData::allSpellsLearned(ram+base),"呪文習得前へ戻せません。");
    }
    session.applyStateFile(silentBaseline);session.editCharacter=0;session.goldStep=4;
    session.executeGoldAdd(); session.replayInput = false;
    session.executeLearnAllSpells();
    auto blockedSpells=session.savePath;blockedSpells+=L".tmp";
    session.restoreEditBackup();fs::create_directory(blockedSpells);
    const auto beforeFailure=memoryHash(session);bool spellRejected=false;
    try{session.executeLearnAllSpells();}catch(const std::exception&){spellRejected=true;}
    fs::remove(blockedSpells);
    require(spellRejected&&memoryHash(session)==beforeFailure,"呪文習得の保存失敗で状態が変化しました。");
    session.executeLearnAllSpells();
    session.notice.clear(); session.uiScreenshot = session.dataDirectory / L"gold-added.bmp"; session.draw();
    for (int slot = 1; slot <= 5; ++slot) require(readFile(session.slotPath(slot)) == slots[slot - 1], "GOLD編集でステートスロットが上書きされました。");
    const std::string report = "PASS: all speeds and six characters; tavern/list, training/list/name/race, outskirts, camp and exploration editing/undo; scene WRAM/SRAM/video isolation; original checksum/mirrored SRAM save; cap/invalid data; write-failure rollback; title/battle rejection; keyboard/pad; five slots preserved\n";
    writeFile(session.dataDirectory / L"gold-check-report.txt", report.data(), report.size());
}
