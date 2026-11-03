#pragma once
#include <memory>

struct CrtScreenshot
{
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;
};

inline CrtScreenshot readCrtScreenshot(const fs::path& path)
{
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    Surface source(SDL_LoadBMP(utf8(path).c_str()), SDL_DestroySurface);
    require(source != nullptr, SDL_GetError());
    Surface rgba(SDL_ConvertSurface(source.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    require(rgba != nullptr, SDL_GetError());
    CrtScreenshot result{rgba->w, rgba->h, {}};
    const auto* data = static_cast<const uint8_t*>(rgba->pixels);
    for (int row = 0; row < rgba->h; ++row)
        result.pixels.insert(result.pixels.end(), data + row * rgba->pitch, data + row * rgba->pitch + rgba->w * 4);
    return result;
}

inline void compareCrtScreenshots(const CrtScreenshot& normal, const CrtScreenshot& crt)
{
    require(normal.width == crt.width && normal.height == crt.height, "CRT切替で画面サイズが変化しました。");
    const auto toolbar = normal.width * 34 * 4;
    require(std::equal(normal.pixels.begin(), normal.pixels.begin() + toolbar, crt.pixels.begin()), "CRTが上部の操作バーへ適用されました。");
    size_t differences = 0;
    uint64_t normalLight = 0, crtLight = 0;
    for (size_t index = static_cast<size_t>(toolbar); index < normal.pixels.size(); index += 4)
    {
        for (size_t channel = 0; channel < 3; ++channel)
        {
            normalLight += normal.pixels[index + channel];
            crtLight += crt.pixels[index + channel];
            differences += normal.pixels[index + channel] != crt.pixels[index + channel];
        }
    }
    require(differences > 1000, "CRTの表示効果が確認できません。");
    require(crtLight * 10 > normalLight * 6 && crtLight * 10 < normalLight * 11, "CRT表示で明るさが大幅に変化しました。");
}

inline void runCrtResumeChecks(Player& session)
{
    require(session.crtDisplay, "再起動後にCRT設定が維持されません。");
    session.draw();
    SDL_ScaleMode mode{};
    sdlCheck(SDL_GetTextureScaleMode(session.texture, &mode));
    require(mode == SDL_SCALEMODE_LINEAR, "再起動後のCRT描画が有効ではありません。");
    const std::string report = "PASS: CRT setting and rendering restored after restart\n";
    writeFile(session.dataDirectory / L"crt-resume-report.txt", report.data(), report.size());
}

inline void runCrtChecks(Player& session)
{
    require(!session.crtDisplay && session.texture, "CRT検証の開始状態が不正です。");
    auto capture = [&](const std::string& name)
    {
        const auto path = session.dataDirectory / (name + ".bmp");
        session.notice.clear(); session.uiScreenshot = path; session.draw();
        return readCrtScreenshot(path);
    };
    for (int speed = 0; speed < 5; ++speed)
    {
        session.setSpeed(speed);
        const auto ram = memoryHash(session), pixels = sha256(session.pixels);
        const auto audio = session.audioFrames;
        const auto normal = capture("crt-off-" + std::to_string(speed));
        session.toggleCrtDisplay();
        const auto crt = capture("crt-on-" + std::to_string(speed));
        compareCrtScreenshots(normal, crt);
        session.toggleCrtDisplay();
        require(capture("crt-restored-" + std::to_string(speed)).pixels == normal.pixels, "OFFで元の表示へ戻りません。");
        require(memoryHash(session) == ram && sha256(session.pixels) == pixels && session.audioFrames == audio,
            "CRT切替でゲーム・元映像・音声の状態が変化しました。");
    }
    session.saveState(1);
    session.toggleCrtDisplay(); session.loadState(1);
    require(session.crtDisplay, "ステート読込で現在のCRT設定が失われました。");
    runTestFrames(session, 1);
    session.toggleMenu(); session.menuRow = 11;
    const auto frozen = memoryHash(session);
    const auto box = session.menuBounds();
    SDL_Event event{}; event.type = SDL_EVENT_MOUSE_BUTTON_DOWN; event.button.button = SDL_BUTTON_LEFT;
    event.button.x = box.x + 40; event.button.y = box.y + 84 + 11 * session.menuRowHeight();
    require(handleAction(session, event) && !session.crtDisplay, "マウスからCRTを切り替えられません。");
    event.type = SDL_EVENT_KEY_DOWN; event.key.scancode = SDL_SCANCODE_RETURN;
    require(handleAction(session, event) && session.crtDisplay && memoryHash(session) == frozen, "メニュー切替でゲーム状態が変化しました。");
    capture("crt-menu"); session.toggleMenu();

    sdlCheck(SDL_SetWindowSize(session.window, 704, 480));
    sdlCheck(SDL_SyncWindow(session.window));
    session.toggleCrtDisplay(); const auto normal = capture("crt-small-off");
    session.toggleCrtDisplay(); const auto crt = capture("crt-small-on");
    compareCrtScreenshots(normal, crt);
    // 最小サイズの左右余白には効果を重ねない。
    for (int row = 36; row < normal.height; ++row)
    {
        const auto index = static_cast<size_t>(row * normal.width + 5) * 4;
        require(std::equal(normal.pixels.begin() + index, normal.pixels.begin() + index + 4, crt.pixels.begin() + index), "CRTがゲーム外の余白へ適用されました。");
    }
    session.toggleMenu(); session.menuRow = 11; capture("crt-small-menu"); session.toggleMenu();
    sdlCheck(SDL_SetWindowSize(session.window, 896, 708));
    sdlCheck(SDL_SyncWindow(session.window));
    session.setSpeed(0);
    session.replayInput = true;
    session.replayButtons = 8; runTestFrames(session, 10);
    session.replayButtons = 0; runTestFrames(session, 240);
    session.replayButtons = 256; runTestFrames(session, 10);
    session.replayButtons = 0; runTestFrames(session, 590);
    session.replayInput = false;
    session.toggleCrtDisplay(); const auto textNormal = capture("crt-text-off");
    session.toggleCrtDisplay(); const auto textCrt = capture("crt-text-on");
    compareCrtScreenshots(textNormal, textCrt);
    const auto temporary = session.dataDirectory / L"settings.ini.tmp";
    fs::create_directory(temporary);
    bool rejected = false;
    try { session.toggleCrtDisplay(); } catch (const std::exception&) { rejected = true; }
    fs::remove(temporary);
    require(rejected && session.crtDisplay, "設定保存失敗時にCRT設定が変化しました。");
    const std::string report = "PASS: rendered CRT at all speeds; source video/WRAM/audio unchanged; exact OFF restoration; toolbar and margins unchanged; state isolation; minimum-size window; in-game text; menu; write failure\n";
    writeFile(session.dataDirectory / L"crt-check-report.txt", report.data(), report.size());
}
