#pragma once

inline void checkMpNine(Player& session)
{
    const auto* ram = session.gameRam();
    for (unsigned member = 0; member < ram[0xdb]; ++member)
        for (unsigned level = 0; level < 14; ++level)
            require((ram[0x1027 + ram[0xdc + member] * 0x80 + level] >> 4) == 9, "パーティのMPが9に維持されません。");
}

inline void runMpResumeChecks(Player& session)
{
    require(session.effectMask == 16, "再起動後にMP維持設定が復元されません。");
    checkMpNine(session);
    const std::string report = "PASS: MP setting restored after restart and state load\n";
    writeFile(session.dataDirectory / L"mp-resume-report.txt", report.data(), report.size());
}

inline void runMpChecks(Player& session)
{
    require(session.supportsEffects() && session.core.normalize_mp && session.effectMask == 0, "MP検証開始状態が不正です。");
    session.setSpeed(0);
    session.replayInput = true;
    auto tap = [&](uint16_t buttons, int after = 60)
    {
        session.replayButtons = buttons; runTestFrames(session, 10);
        session.replayButtons = 0; runTestFrames(session, after);
    };
    tap(8, 240); tap(256, 590); tap(256, 100); tap(256, 100); tap(256, 100);
    for (int i = 0; i < 5; ++i) tap(256, 50);
    tap(1, 100); tap(16, 40); tap(256, 80);
    session.gameRam()[0x112e] = 0x92;
    tap(256, 300);
    require(session.gameRam()[0x112e] == 0x22, "迷宮入場時に保存済みのMP上書きを解除できません。");
    tap(1, 100); tap(32, 20); tap(256); tap(16);
    require(session.core.can_move(), "MP検証用の探索状態に入れません。");
    auto* ram = session.gameRam();
    const auto exploration = encodeState(session.captureState(), session.romId, session.coreId);
    std::vector<uint8_t> maximumAndSpells;
    auto originalCharacterData = [&]
    {
        std::vector<uint8_t> values;
        for (unsigned character = 0; character < 20; ++character)
        {
            const auto base = 0x1000 + character * 0x80;
            for (unsigned level = 0; level < 14; ++level) values.push_back(ram[base + 0x27 + level] & 15);
            values.insert(values.end(), ram + base + 0x35, ram + base + 0x43);
        }
        return values;
    };
    maximumAndSpells = originalCharacterData();

    // キャンプ→しらべる→僧侶→呪文一覧。実際のディオスを対象選択から実行する。
    tap(1); tap(256); tap(32, 20); tap(32, 20); tap(256); tap(256);
    require(ram[0x112e] == 0x22, "僧侶の初期MPが一致しません。");
    const auto beforeSpell = encodeState(session.captureState(), session.romId, session.coreId);
    tap(256); tap(256);
    require(ram[0x112e] == 0x12, "通常のキャンプ呪文でMPが1回消費されません。");

    for (int speed = 0; speed < static_cast<int>(GAME_SPEEDS.size()); ++speed)
    {
        session.applyStateFile(beforeSpell);
        session.toggleEffect(4);
        session.setSpeed(speed);
        for (int cast = 0; cast < 3; ++cast)
        {
            tap(256); tap(256);
            checkMpNine(session);
            require(ram[0x112e] == 0x92 && originalCharacterData() == maximumAndSpells, "呪文使用で最大MPか習得呪文が変化しました。");
            tap(256); tap(256);
        }
        session.toggleEffect(4);
        require(ram[0x112e] == 0x22 && originalCharacterData() == maximumAndSpells, "MP維持OFFで本来の上限に戻りません。");
    }
    session.setSpeed(0);

    // 戦闘でも通常のコマンド入力から僧侶の回復呪文を実行する。
    session.applyStateFile(exploration);
    ram[0xee] = 1;
    tap(16, 240);
    require(!session.core.can_move(), "MP検証用の戦闘に入れません。");
    auto commandReady = [&]
    {
        return ram[0x1a] == 0xc8 && ram[0x1b] == 0x1f && ram[0x1fd2] == 0xfe &&
            ram[0x1fd3] == 0xd6 && ram[0x1fd4] == 5 && ram[0xbd] == 0;
    };
    // 先制攻撃や遭遇メッセージを終え、最初の味方のコマンド待ちまで進める。
    for (int message = 0; message < 100 && !commandReady(); ++message) tap(256);
    require(commandReady(), "戦闘コマンドの入力待ちに入りません。");
    const auto battle = encodeState(session.captureState(), session.romId, session.coreId);
    writeFile(session.dataDirectory / L"battle-fixture.wvs", battle.data(), battle.size());
    auto battleCommands = [&]
    {
        for (int member = 0; member < 2; ++member) { tap(32, 20); tap(256, 40); }
        for (int row = 0; row < 3; ++row) tap(32, 20);
        tap(256, 40); tap(256, 40); tap(256, 40);
        tap(32, 20); tap(256, 40); // 盗賊は後列の「みをまもる」。
        tap(256, 40); tap(256, 40); tap(256, 600);
    };
    battleCommands();
    int prompts = 0;
    while (ram[0x112e] == 0x22 && prompts < 20) { tap(256); ++prompts; }
    session.uiScreenshot = session.dataDirectory / L"battle-cast.bmp"; session.draw();
    require(ram[0x112e] == 0x12, "通常の戦闘呪文でMPが消費されません: " + std::to_string(ram[0x112e]));
    session.applyStateFile(battle);
    session.toggleEffect(4);
    battleCommands();
    for (int prompt = 0; prompt < prompts; ++prompt) tap(256);
    checkMpNine(session);
    require(originalCharacterData() == maximumAndSpells, "戦闘中に最大MPか習得呪文が変化しました。");
    session.toggleEffect(4);

    session.applyStateFile(beforeSpell);
    for (unsigned effect = 0; effect < 5; ++effect) session.toggleEffect(effect);
    checkMpNine(session); checkEffectValues(session, 15);
    session.toggleEffect(4); checkEffectValues(session, 15);
    for (unsigned effect = 0; effect < 4; ++effect) session.toggleEffect(effect);

    // ONのステートをOFF設定で読む場合も、残り回数を本来の上限に戻す。
    session.applyStateFile(beforeSpell); session.toggleEffect(4);
    const auto enabledState = encodeState(session.captureState(), session.romId, session.coreId);
    session.toggleEffect(4); session.applyStateFile(enabledState);
    require(ram[0x112e] == 0x22 && originalCharacterData() == maximumAndSpells, "OFF設定でステート内のMP上書きが残りました。");
    session.toggleEffect(4); session.applyStateFile(beforeSpell); checkMpNine(session);

    session.replayInput = false;
    session.toggleMenu(); session.menuRow = 7; session.activateMenu(); session.menuRow = 4;
    session.activateMenu(); require(session.effectMask == 0, "MPメニューからOFFにできません。");
    session.activateMenu(); checkMpNine(session);
    session.notice.clear(); session.uiScreenshot = session.dataDirectory / L"mp-menu.bmp"; session.draw();
    session.toggleMenu();
    auto temporary = session.dataDirectory / L"settings.ini.tmp";
    fs::create_directory(temporary);
    bool rejected = false;
    const auto beforeFailure = memoryHash(session);
    try { session.toggleEffect(4); } catch (const std::exception&) { rejected = true; }
    fs::remove(temporary);
    require(rejected && session.effectMask == 16 && memoryHash(session) == beforeFailure, "設定保存失敗時にMP設定が変わりました。");
    session.saveState(1);
    const std::string report = "PASS: real camp spells at every speed; real battle spell; normal MP consumption when OFF; current MP fixed at 9; original maximums and learned spells preserved; OFF/state-load normalization; menu; write failure\n";
    writeFile(session.dataDirectory / L"mp-check-report.txt", report.data(), report.size());
}
