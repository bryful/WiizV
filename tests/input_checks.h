#pragma once

inline void runInputChecks(Player& session)
{
    require(session.romId == "afef55c8e8877b4324f6cc2da6aeacc9cb27bb83a8661e35a14ce7d73e5f0456", "入力検証用ROMが一致しません。");
    session.setSpeed(0);
    session.replayInput = true;
    session.replayButtons = 0;
    runTestFrames(session, 50);
    session.replayButtons = 8;
    runTestFrames(session, 10);
    session.replayButtons = 0;
    runTestFrames(session, 240);
    session.replayButtons = 256;
    runTestFrames(session, 10);
    session.replayButtons = 0;
    runTestFrames(session, 590);
    session.replayInput = false;
    session.focused = true;
    auto* ram = static_cast<uint8_t*>(session.core.get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    // 所有ROMの城メニューで、下を1回押すと0→1になることを画像とRAM差分で確認した値。
    require(ram && ram[0x33] == 0, "城メニューの入力検証開始位置が不正です。");
    const auto baseline = encodeState(session.captureState(), session.romId, session.coreId);
    int normalChanges = 0;
    std::ostringstream report;
    for (int speed = 0; speed < static_cast<int>(GAME_SPEEDS.size()); ++speed)
    {
        session.applyStateFile(baseline);
        session.setSpeed(speed);
        session.inputBlocked = false;
        auto cursor = ram[0x33];
        int changes = 0;
        int firstMs = -1;
        const double framesPerSecond = session.fps * session.gameSpeed();
        const int frames = static_cast<int>(std::ceil(framesPerSecond * 1.2));
        for (int frame = 0; frame < frames; ++frame)
        {
            const auto now = static_cast<uint64_t>(frame * 1000.0 / framesPerSecond);
            sampleGameInput(session, now < 1000 ? 32 : 0, now);
            runTestFrames(session, 1);
            if (ram[0x33] != cursor)
            {
                ++changes;
                cursor = ram[0x33];
                if (firstMs < 0) firstMs = static_cast<int>(now);
            }
        }
        if (speed == 0) normalChanges = changes;
        report << "speed=" << session.gameSpeed() << " held_down_1s_cursor_changes=" << changes << " first_response_ms=" << firstMs << "\n";
        auto text = report.str();
        writeFile(session.dataDirectory / L"input-check-report.txt", text.data(), text.size());
        require(changes > 1 && changes <= normalChanges + 1 && firstMs >= 0 && firstMs <= 50,
            "倍速中のメニュー操作回数または初回反応が不正です。");

        session.applyStateFile(baseline);
        session.inputBlocked = false;
        session.inputRepeat.reset();
        cursor = ram[0x33];
        changes = 0;
        for (int frame = 0; frame < frames; ++frame)
        {
            const auto now = static_cast<uint64_t>(frame * 1000.0 / framesPerSecond);
            const uint16_t buttons = now < 600 && now % 200 < 40 ? 32 : 0;
            sampleGameInput(session, buttons, now);
            runTestFrames(session, 1);
            if (ram[0x33] != cursor) { ++changes; cursor = ram[0x33]; }
        }
        report << "speed=" << session.gameSpeed() << " three_taps_cursor_changes=" << changes << "\n";
        text = report.str();
        writeFile(session.dataDirectory / L"input-check-report.txt", text.data(), text.size());
        require(changes == 3, "3回の短い押下が3回の操作になりません。");

        if (speed > 0)
        {
            auto actionOutcome = [&](uint64_t heldMs)
            {
                session.applyStateFile(baseline);
                session.inputBlocked = false;
                for (int frame = 0; frame < frames; ++frame)
                {
                    const auto now = static_cast<uint64_t>(frame * 1000.0 / framesPerSecond);
                    sampleGameInput(session, now < heldMs ? 256 : 0, now);
                    runTestFrames(session, 1);
                }
                return stateOutcome(session);
            };
            const auto tap = actionOutcome(40);
            const auto held = actionOutcome(1000);
            require(tap == held, "決定ボタンの長押しで短い1回押下と異なる処理が実行されました。");
            report << "speed=" << session.gameSpeed() << " held_confirm_matches_single_tap=PASS\n";
            text = report.str();
            writeFile(session.dataDirectory / L"input-check-report.txt", text.data(), text.size());
        }
    }
    session.setSpeed(0);
    session.applyStateFile(baseline);
    runTestFrames(session, 1);
}
