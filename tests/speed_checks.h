#pragma once

inline void runSpeedChecks(Player& session)
{
    require(session.core.set_audio_speed != nullptr, "倍速検証用のコア拡張がありません。");
    runTestFrames(session, 900);
    const auto baseline = encodeState(session.captureState(), session.romId, session.coreId);
    std::vector<int16_t> reference;
    std::ostringstream report;
    for (int index = 0; index < static_cast<int>(GAME_SPEEDS.size()); ++index)
    {
        session.applyStateFile(baseline);
        session.setSpeed(index);
        session.recordedAudio.clear();
        session.recordAudio = true;
        const int frames = static_cast<int>(600 * session.gameSpeed());
        runTestFrames(session, frames);
        session.recordAudio = false;
        const auto& samples = session.recordedAudio;
        require(!samples.empty(), "倍速検証の音声が空です。");
        if (index == 0) reference = samples;
        const auto difference = static_cast<int64_t>(samples.size()) - static_cast<int64_t>(reference.size());
        size_t mismatches = 0;
        const auto common = std::min(samples.size(), reference.size());
        for (size_t i = 0; i < common; ++i) if (samples[i] != reference[i]) ++mismatches;
        double bestCorrelation = -1;
        int bestOffset = 0;
        for (int offset = -256; offset <= 256; offset += 2)
        {
            double ab = 0, aa = 0, bb = 0;
            const size_t end = std::min<size_t>(common - 512, 32000);
            for (size_t i = 512; i < end; i += 2)
            {
                const auto shifted = static_cast<size_t>(static_cast<int64_t>(i) + offset);
                const double a = reference[i];
                const double b = samples[shifted];
                ab += a * b; aa += a * a; bb += b * b;
            }
            const double correlation = aa && bb ? ab / std::sqrt(aa * bb) : 0;
            if (correlation > bestCorrelation) { bestCorrelation = correlation; bestOffset = offset; }
        }
        report << "speed=" << session.gameSpeed() << " frames=" << frames << " audio_frames=" << samples.size() / 2
            << " sample_difference=" << difference << " mismatches=" << mismatches
            << " correlation=" << bestCorrelation << " offset=" << bestOffset << "\n";
        auto text = report.str();
        writeFile(session.dataDirectory / L"speed-report.txt", text.data(), text.size());
        // 同じ実時間分のゲームを実行しても、音声の長さと波形は通常速度を維持する。
        require(std::abs(difference) <= 32 && bestCorrelation > 0.995, "倍速で通常音声を維持できません。speed-report.txtを確認してください。");

        const auto saved = encodeState(session.captureState(), session.romId, session.coreId);
        session.recordedAudio.clear();
        session.recordAudio = true;
        runTestFrames(session, 60);
        session.recordAudio = false;
        const auto expectedAudio = session.recordedAudio;
        const auto expectedMemory = memoryHash(session);
        session.applyStateFile(saved);
        session.recordedAudio.clear();
        session.recordAudio = true;
        runTestFrames(session, 60);
        session.recordAudio = false;
        require(session.recordedAudio == expectedAudio && memoryHash(session) == expectedMemory,
            "倍速中のステート復元で音声またはゲーム内メモリが一致しません。");
        report << "state_at_speed=" << session.gameSpeed() << " PASS\n";
        text = report.str();
        writeFile(session.dataDirectory / L"speed-report.txt", text.data(), text.size());
    }
    session.setSpeed(0);
    session.applyStateFile(baseline);
    runTestFrames(session, 1);
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_F2;
    handleAction(session, key);
    require(session.speedIndex == 1, "F2で倍率を切り替えられません。");
    session.menuOpen = true;
    session.menuRow = 6;
    session.activateMenu();
    require(session.speedIndex == 2, "メニューで倍率を切り替えられません。");
    session.setSpeed(4);
    session.notice.clear();
    session.uiScreenshot = session.dataDirectory / L"speed-menu.bmp";
    session.draw();
    session.menuOpen = false;
    session.setSpeed(0);
}
