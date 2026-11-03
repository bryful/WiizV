#pragma once

// --smokeでのみ呼び出す統合検証。通常のAppDataを使わず専用保存先で実行する。
inline std::string memoryHash(Player& session)
{
    auto* data = static_cast<const uint8_t*>(session.core.get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    const auto size = session.core.get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    require(data && size, "検証用WRAMを取得できません。");
    return sha256(std::vector<uint8_t>(data, data + size));
}

inline void runTestFrames(Player& session, int count)
{
    for (int i = 0; i < count; ++i)
    {
        session.runFrame();
        require(session.callbackError.empty(), session.callbackError);
        SDL_ClearAudioStream(session.audio);
    }
}

inline std::string stateOutcome(Player& session)
{
    return memoryHash(session) + "\n" + sha256(session.pixels) + "\n";
}

inline void runStateResumeChecks(Player& session)
{
    for (int slot = 1; slot <= 5; ++slot)
    {
        session.loadState(slot);
        runTestFrames(session, 60);
        auto expected = readFile(session.dataDirectory / ("expected-slot" + std::to_string(slot) + ".txt"));
        require(stateOutcome(session) == std::string(expected.begin(), expected.end()),
            "別プロセスで復元したステートの映像・WRAMが一致しません: " + std::to_string(slot));
    }
    const auto settings = readFile(session.dataDirectory / L"settings.ini");
    require(session.swapButtons == (std::string(settings.begin(), settings.end()).find("swap_abxy=1\n") != std::string::npos), "ABXY設定を復元できません。");
    const std::string result = "PASS: five slots restored after process restart; video and WRAM match; settings loaded\n";
    writeFile(session.dataDirectory / L"state-resume-report.txt", result.data(), result.size());
}

inline void runStateChecks(Player& session)
{
    // 保存中にコアが進んでもスナップショットと最終保存を混同しない。
    auto* ram = static_cast<uint8_t*>(session.core.get_memory_data(RETRO_MEMORY_SAVE_RAM));
    const auto ramSize = session.core.get_memory_size(RETRO_MEMORY_SAVE_RAM);
    require(ram && ramSize, "自動保存検証用SRAMを取得できません。");
    session.saveRam();
    const auto originalRam = session.savedRam;
    ram[0] ^= 1;
    const std::vector<uint8_t> snapshot(ram, ram + ramSize);
    session.saveRamAsync();
    ram[0] ^= 2;
    session.finishRamSave(true);
    require(readFile(session.savePath) == snapshot && session.savedRam == snapshot,
        "非同期保存が実行中のSRAMを参照しています。");
    session.saveRam();
    require(readFile(session.savePath) == std::vector<uint8_t>(ram, ram + ramSize),
        "最終保存が最新のSRAMを保存していません。");
    auto ramTemporary = session.savePath;
    ramTemporary += L".tmp";
    fs::create_directory(ramTemporary);
    ram[0] ^= 4;
    session.saveRamAsync();
    bool saveFailed = false;
    try { session.finishRamSave(true); }
    catch (const std::exception&) { saveFailed = true; }
    fs::remove(ramTemporary);
    require(saveFailed && readFile(session.savePath) == session.savedRam,
        "非同期保存の失敗が通知されないか、既存保存が破損しています。");
    std::memcpy(ram, originalRam.data(), ramSize);
    session.saveRam();
    const auto startState = session.captureState();
    for (int slot = 1; slot <= 5; ++slot)
    {
        session.saveState(slot);
        runTestFrames(session, 60);
        const auto expected = stateOutcome(session);
        session.loadState(slot);
        runTestFrames(session, 60);
        require(stateOutcome(session) == expected, "復元後の映像・WRAMが一致しません: " + std::to_string(slot));
        writeFile(session.dataDirectory / ("expected-slot" + std::to_string(slot) + ".txt"), expected.data(), expected.size());
    }
    auto original = readFile(session.slotPath(1));
    auto rejects = [&](const std::vector<uint8_t>& file)
    {
        auto before = memoryHash(session);
        bool rejected = false;
        try { session.applyStateFile(file); }
        catch (const std::exception&) { rejected = true; }
        require(rejected && memoryHash(session) == before && !session.stateRecoveryFailed, "不正ステートを安全に拒否できません。");
    };
    auto corrupted = original;
    corrupted.back() ^= 1;
    rejects(corrupted);
    auto wrongRom = original;
    wrongRom[8] = wrongRom[8] == '0' ? '1' : '0';
    rejects(wrongRom);
    auto wrongCore = original;
    wrongCore[72] = wrongCore[72] == '0' ? '1' : '0';
    rejects(wrongCore);
    rejects({});
    rejects(std::vector<uint8_t>(original.begin(), original.begin() + 207));
    auto truncated = original;
    truncated.pop_back();
    rejects(truncated);
    auto invalidCoreState = startState;
    invalidCoreState[0] ^= 1;
    rejects(encodeState(invalidCoreState, session.romId, session.coreId));

    // 書き込み失敗時も既存スロットが残ることを、作業用ファイルの競合で確認する。
    auto blocked = session.slotPath(1);
    blocked += L".tmp";
    require(fs::create_directory(blocked), "書込失敗の検証用ディレクトリを作成できません。");
    bool failed = false;
    try { session.saveState(1); }
    catch (const std::exception&) { failed = true; }
    fs::remove(blocked);
    require(failed && readFile(session.slotPath(1)) == original, "保存失敗で既存スロットが変わりました。");

    // 同じイベント経路でメニュー開閉・スロット選択を検証する。
    session.focused = true;
    session.selectedSlot = 1;
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_LEFT_STICK;
    require(handleAction(session, event) && session.menuOpen, "パッドのメニュー入力が動きません。");
    require(inputState(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A) == 0, "メニュー中の入力がゲームへ渡りました。");
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_DOWN;
    handleAction(session, event);
    require(session.selectedSlot == 2, "キーボードのスロット選択が動きません。");
    event.key.scancode = SDL_SCANCODE_RIGHT;
    handleAction(session, event);
    require(session.menuLoad, "読込側を選択できません。");
    session.uiScreenshot = session.dataDirectory / L"state-menu.bmp";
    session.notice.clear();
    session.draw();
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    handleAction(session, event);
    require(!session.menuOpen && session.inputBlocked, "メニューを閉じた後の入力抑止が動きません。");

    // 仮想パッドから実際のSDL状態を通し、LTの閾値・連続保存防止を確認する。
    sdlCheck(SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1"));
    SDL_VirtualJoystickDesc descriptor{};
    SDL_INIT_INTERFACE(&descriptor);
    descriptor.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    descriptor.name = "WiizV state test pad";
    descriptor.naxes = SDL_GAMEPAD_AXIS_COUNT;
    descriptor.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    descriptor.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    descriptor.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    for (int connection = 0; connection < 2; ++connection)
    {
        auto id = SDL_AttachVirtualJoystick(&descriptor);
        require(id != 0, SDL_GetError());
        session.openPad(id);
        require(session.gamepads.contains(id), "仮想パッドを開けません。");
        auto* joystick = SDL_GetGamepadJoystick(session.gamepads.at(id));
        auto trigger = [&](Sint16 value)
        {
            sdlCheck(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, value));
            SDL_UpdateJoysticks();
            pollExtraControls(session);
        };
        trigger(-32768);
        require(SDL_GetGamepadAxis(session.gamepads.at(id), SDL_GAMEPAD_AXIS_LEFT_TRIGGER) == 0, "仮想LTの初期値が不正です。");
        trigger(32767);
        const auto pressed = readFile(session.slotPath(1));
        runTestFrames(session, 3);
        trigger(32767);
        require(readFile(session.slotPath(1)) == pressed, "LT押しっぱなしで連続保存されました。");
        trigger(-32768);
        trigger(32767);
        require(readFile(session.slotPath(1)) != pressed, "LTを押し直しても保存されません。");
        const auto beforeBackground = readFile(session.slotPath(1));
        session.focused = false;
        trigger(-32768);
        trigger(32767);
        runTestFrames(session, 3);
        session.focused = true;
        trigger(32767);
        require(readFile(session.slotPath(1)) == beforeBackground, "非アクティブ中のLTが保存を実行しました。");
        SDL_CloseGamepad(session.gamepads.at(id));
        session.gamepads.erase(id);
        session.triggerHeld.erase(id);
        sdlCheck(SDL_DetachVirtualJoystick(id));
    }
    writeFile(session.slotPath(1), original.data(), original.size());
    session.toggleButtons();
    session.saveSettings();
    // 最後の画像出力と通常セーブを、検証前の進行へ戻す。
    session.applyStateFile(encodeState(startState, session.romId, session.coreId));
    runTestFrames(session, 1);
    const std::string result = "PASS: five slots; deterministic video/WRAM restoration; corruption/ROM/core/truncation rejection; failed-core rollback; write-failure preservation; menu events; virtual-pad LT debounce/background/reconnect; ABXY settings\n";
    writeFile(session.dataDirectory / L"state-check-report.txt", result.data(), result.size());
}
