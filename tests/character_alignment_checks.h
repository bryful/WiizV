#pragma once

inline void runCharacterAlignmentResumeChecks(Player& session)
{
    require(session.loadedSavedRam && (session.gameRam()[0x100a] & 3) == 2 && (session.gameRam()[0x108a] & 3) == 1,
        "通常セーブから変更した属性を復元できません。");
    const std::string report = "PASS: original game validates SRAM checksums and restores Evil/Neutral characters after restart\n";
    writeFile(session.dataDirectory / L"character-alignment-resume-report.txt", report.data(), report.size());
}

inline void runCharacterAlignmentChecks(Player& session)
{
    require(session.core.request_alignment && !session.core.request_alignment(0, 2), "タイトルで属性編集が許可されました。");
    session.setSpeed(0); session.replayInput = true;
    auto tap = [&](uint16_t buttons, int after = 100)
    {
        session.replayButtons = buttons; runTestFrames(session, 10);
        session.replayButtons = 0; runTestFrames(session, after);
    };
    tap(8, 240); tap(256, 590);
    const auto baseline = encodeState(session.captureState(), session.romId, session.coreId);
    auto* ram = session.gameRam();
    session.toggleMenu(); session.openCharacterMenu(); session.editCharacter = 0;
    for (int slot = 1; slot <= 5; ++slot) session.saveState(slot);
    std::array<std::vector<uint8_t>, 5> slots;
    for (int slot = 1; slot <= 5; ++slot) slots[slot - 1] = readFile(session.slotPath(slot));
    for (int speed = 0; speed < 5; ++speed)
        for (unsigned from = 0; from < 3; ++from)
            for (unsigned to = 0; to < 3; ++to)
            {
                if (from == to) continue;
                session.applyStateFile(baseline); session.setSpeed(speed);
                ram[0x100a] = static_cast<uint8_t>((ram[0x100a] & 0xfc) | from);
                session.editAlignment = to;
                const std::vector<uint8_t> before(ram, ram + 0x20000);
                session.executeAlignmentChange();
                require(session.characterAlignment() == to, "属性が指定した値に変わりません。");
                for (size_t byte = 0; byte < before.size(); ++byte)
                    if (byte == 0x100a) require((before[byte] & 0xfc) == (ram[byte] & 0xfc), "種族・職業が変化しました。");
                    else if (byte != 0x107e && byte != 0x107f) require(before[byte] == ram[byte], "属性以外のデータや場面が変化しました。");
                const auto* sram = static_cast<const uint8_t*>(session.core.get_memory_data(RETRO_MEMORY_SAVE_RAM));
                require(std::equal(ram + 0x1000, ram + 0x1080, sram) && std::equal(ram + 0x1000, ram + 0x1080, sram + 0xa00),
                    "属性が二重SRAM保存へ反映されません。");
                session.restoreEditBackup(); require(session.characterAlignment() == from, "属性を復元できません。");
            }
    session.setSpeed(0);
    for (unsigned upper = 0; upper < 64; ++upper)
    {
        session.applyStateFile(baseline); ram[0x100a] = static_cast<uint8_t>(upper << 2); session.editAlignment = 2;
        session.executeAlignmentChange();
        require(ram[0x100a] == static_cast<uint8_t>((upper << 2) | 2), "種族・職業ビットの組合せを維持できません。");
    }
    session.applyStateFile(baseline);
    auto rejects = [&]
    {
        const auto before = memoryHash(session); bool rejected = false;
        try { session.executeAlignmentChange(); } catch (const std::exception&) { rejected = true; }
        require(rejected && memoryHash(session) == before, "不正指定・保存失敗で元の状態を維持できません。");
    };
    session.editAlignment = 0; rejects(); session.editAlignment = 3; rejects(); session.editAlignment = 2;
    session.editCharacter = 6; rejects(); session.editCharacter = 0;
    require(!session.core.request_alignment(20, 2) && !session.core.request_alignment(0, 3) && !session.core.request_alignment(6, 2),
        "コアが不正な属性編集を受理しました。");
    auto blocked = session.editBackupPath(); blocked += L".tmp";
    fs::create_directory(blocked); rejects(); fs::remove(blocked);
    session.editAlignment = 1; // 直前に保存したEvilとは違う値にして実際のファイル書込みを試す。
    const auto saved = readFile(session.savePath); blocked = session.savePath; blocked += L".tmp";
    fs::create_directory(blocked); rejects(); fs::remove(blocked);
    require(readFile(session.savePath) == saved, "保存失敗で通常セーブが変更されました。");

    session.menuRow = 5; session.editAlignment = 0; session.focused = true;
    SDL_Event event{}; event.type = SDL_EVENT_KEY_DOWN; event.key.scancode = SDL_SCANCODE_LEFT;
    require(handleAction(session, event) && session.editAlignment == 2, "キーボードで属性を選択できません。");
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN; event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    require(handleAction(session, event) && session.editAlignment == 0, "パッドで属性を選択できません。");
    session.editAlignment = 2; session.menuRow = 6; session.activateMenu();
    session.notice.clear(); session.uiScreenshot = session.dataDirectory / L"character-alignment-editor.bmp"; session.draw();
    require(session.characterAlignment() == 2, "メニューから属性を変更できません。");
    session.menuRow = 7; session.activateMenu(); require(session.characterAlignment() == 0, "メニューから属性を復元できません。");
    session.menuRow = 0; session.editAlignment = 2;
    event.type = SDL_EVENT_KEY_DOWN; event.key.scancode = SDL_SCANCODE_RIGHT;
    require(handleAction(session, event) && session.editCharacter == 1 && session.editAlignment == 0, "対象を変えても属性の選択が前のキャラクターのままです。");
    session.backMenu(); session.toggleMenu();

    auto editHere = [&](unsigned character, unsigned alignment)
    {
        session.toggleMenu(); session.openCharacterMenu(); session.editCharacter = character; session.editAlignment = alignment;
        session.executeAlignmentChange(); session.backMenu(); session.toggleMenu();
    };
    tap(256); // 酒場
    editHere(0, 2); editHere(0, 0);
    tap(256); tap(256); // Goodの先頭を加入し、加入候補一覧を表示。
    require(ram[0xdb] == 1, "加入検証の先頭キャラクターがいません。");
    const auto joinList = encodeState(session.captureState(), session.romId, session.coreId);
    editHere(1, 2); tap(256);
    require(ram[0xdb] == 1, "一覧を開いた後にEvilへ変えた候補がGoodパーティへ加入しました。");
    tap(256); // 加入一覧を開き直す。
    require(std::find(ram + 0x300, ram + 0x300 + ram[0x34], 1) == ram + 0x300 + ram[0x34], "新しい一覧に加入不可の候補が残っています。");
    session.applyStateFile(joinList); editHere(0, 2); // 先頭だけEvilに変える。
    tap(256); require(ram[0xdb] == 1, "古いパーティ属性マスクでGoodの候補を加入させました。");
    tap(256); // 全候補がGoodのため、候補なしのメッセージになる。
    require(ram[0xdb] == 1, "属性編集でメンバー数が変化しました。");

    session.applyStateFile(joinList); editHere(1, 2); editHere(0, 1); // Neutralの先頭はEvilを加入できる。
    tap(1); tap(256);
    require(std::find(ram + 0x300, ram + 0x300 + ram[0x34], 1) != ram + 0x300 + ram[0x34],
        "変更後に加入できる候補が古い属性マスクで除外されました。");
    tap(256); require(ram[0xdb] == 2 && ram[0xdd] == 1, "変更後の属性で加入可能な候補を加入できません。");

    session.applyStateFile(baseline); tap(16); tap(256); tap(32); tap(256); editHere(0, 1); // 訓練場
    session.applyStateFile(baseline); tap(256); tap(256); for (int member = 0; member < 6; ++member) tap(256, 50);
    tap(1); tap(16, 40); tap(256, 80); tap(256, 300); editHere(0, 2); // キャンプ
    tap(1); editHere(0, 1); tap(32, 20); tap(256, 60); tap(16, 60); editHere(0, 2); // 階段・探索
    require(session.core.can_move() && ram[0xdb] == 6, "属性編集後に探索へ戻れません。");
    ram[0xee] = 1; tap(16, 240); require(!session.core.request_alignment(0, 1), "戦闘で編集が許可されました。");

    session.applyStateFile(baseline); session.toggleMenu(); session.openCharacterMenu(); session.editCharacter = 0; session.editAlignment = 2;
    session.executeAlignmentChange(); session.editCharacter = 1; session.editAlignment = 1; session.executeAlignmentChange();
    for (int slot = 1; slot <= 5; ++slot) require(readFile(session.slotPath(slot)) == slots[slot - 1], "属性編集で5スロットが変更されました。");
    const std::string report = "PASS: Good/Neutral/Evil transitions at all speeds; all race/class bits preserved; WRAM/parameter isolation; native mirrored SRAM; UI; backup/undo; failures; stale tavern candidate/party checks; training/camp/stairs/exploration; title/battle rejection; quick slots preserved\n";
    writeFile(session.dataDirectory / L"character-alignment-check-report.txt", report.data(), report.size());
}
