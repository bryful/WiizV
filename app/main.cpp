#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <bcrypt.h>
#endif
#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <SDL3/SDL_main.h>
#include "android_platform.h"
#include "../third_party/snes9x/upstream/sha256.h"
#define __cdecl
using HMODULE = SDL_SharedObject*;
inline auto GetProcAddress(HMODULE module, const char* name) { return SDL_LoadFunction(module, name); }
inline void FreeLibrary(HMODULE module) { SDL_UnloadObject(module); }
#else
inline bool renderOutputSize(SDL_Renderer* renderer, int* width, int* height) { return SDL_GetRenderOutputSize(renderer, width, height); }
#endif
#include <libretro.h>
#include "state_file.h"
#include "input_repeat.h"
#include "playback_speed.h"
#include "dungeon_map.h"
#include "crt_filter.h"
#include "data_directory.h"
#include "../third_party/snes9x/wiizv_character_data.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <cwctype>

namespace fs = std::filesystem;

std::string utf8(const fs::path& path)
{
    auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void require(bool ok, const std::string& message)
{
    if (!ok) throw std::runtime_error(message);
}

void sdlCheck(bool ok)
{
    require(ok, SDL_GetError());
}

std::vector<uint8_t> readFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    require(file.good(), "読み込めません: " + utf8(path));
    const auto size = file.tellg();
    require(size >= 0 && size <= 32 * 1024 * 1024, "ファイルサイズが不正です: " + utf8(path));
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0);
    if (!data.empty()) file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    require(file.good(), "読み込みに失敗しました: " + utf8(path));
    return data;
}

void writeFile(const fs::path& path, const void* data, size_t size)
{
    auto temporary = path;
    temporary += L".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        require(file.good(), "保存先を開けません: " + utf8(temporary));
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        file.flush();
        require(file.good(), "保存に失敗しました: " + utf8(temporary));
        file.close();
        require(!file.fail(), "保存を完了できません: " + utf8(temporary));
    }
#ifdef _WIN32
    require(MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
        "保存ファイルを置換できません: " + utf8(path));
#else
    androidCommit(path, data, size);
    fs::rename(temporary, path);
#endif
}

std::string sha256(const std::vector<uint8_t>& data)
{
    std::array<uint8_t, 32> hash{};
#ifdef _WIN32
    require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
        const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), hash.data(), static_cast<ULONG>(hash.size())) >= 0,
        "データの識別に失敗しました。");
#else
    sha256sum(const_cast<uint8_t*>(data.data()), static_cast<unsigned>(data.size()), hash.data());
#endif
    std::ostringstream result;
    for (auto byte : hash) result << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte);
    return result.str();
}

fs::path executableDirectory()
{
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    require(length > 0 && length < buffer.size(), "実行ファイルの場所を取得できません。");
    return fs::path(std::wstring(buffer.data(), length)).parent_path();
#else
    return androidString("nativeDirectory");
#endif
}

#ifdef _WIN32
fs::path knownDataDirectory(REFKNOWNFOLDERID folder)
{
    PWSTR value = nullptr;
    require(SUCCEEDED(SHGetKnownFolderPath(folder, 0, nullptr, &value)), "AppDataを取得できません。");
    fs::path result = fs::path(value) / L"WiizV";
    CoTaskMemFree(value);
    return result;
}

fs::path pickRom(SDL_Window* window, const fs::path& initial)
{
    std::array<wchar_t, 32768> name{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    dialog.lpstrFilter = L"SNES ROM (*.smc;*.sfc)\0*.smc;*.sfc\0すべてのファイル\0*.*\0";
    dialog.lpstrFile = name.data();
    dialog.nMaxFile = static_cast<DWORD>(name.size());
    dialog.lpstrInitialDir = initial.c_str();
    dialog.lpstrTitle = L"Wizardry VのROMを選択";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (GetOpenFileNameW(&dialog)) return name.data();
    require(CommDlgExtendedError() == 0, "ROM選択画面を開けません。");
    return {};
}
#else
fs::path pickRom(SDL_Window*, const fs::path&) { androidChooseRom(); return {}; }
#endif

struct Core
{
    HMODULE module = nullptr;
    bool (__cdecl* set_audio_speed)(unsigned, unsigned) = nullptr;
    bool (__cdecl* can_move)() = nullptr;
    bool (__cdecl* request_move)(int, int, unsigned) = nullptr;
    void (__cdecl* cancel_move)() = nullptr;
    unsigned (__cdecl* move_status)() = nullptr;
    bool (__cdecl* set_effects)(unsigned) = nullptr;
    void (__cdecl* normalize_mp)() = nullptr;
    bool (__cdecl* set_max_bonus)(bool) = nullptr;
    bool (__cdecl* set_skip_logo)(bool) = nullptr;
    bool (__cdecl* set_mixed_alignment)(bool) = nullptr;
    bool (__cdecl* can_edit_character)() = nullptr;
    bool (__cdecl* request_gold)(unsigned, uint64_t) = nullptr;
    bool (__cdecl* request_experience)(unsigned, unsigned) = nullptr;
    bool (__cdecl* request_alignment)(unsigned, unsigned) = nullptr;
    unsigned (__cdecl* gold_status)() = nullptr;
    void (__cdecl* cancel_gold)() = nullptr;
#define CORE_FUNCTION(name) decltype(&retro_##name) name = nullptr
    CORE_FUNCTION(set_environment);
    CORE_FUNCTION(set_video_refresh);
    CORE_FUNCTION(set_audio_sample);
    CORE_FUNCTION(set_audio_sample_batch);
    CORE_FUNCTION(set_input_poll);
    CORE_FUNCTION(set_input_state);
    CORE_FUNCTION(init);
    CORE_FUNCTION(deinit);
    CORE_FUNCTION(api_version);
    CORE_FUNCTION(get_system_info);
    CORE_FUNCTION(get_system_av_info);
    CORE_FUNCTION(set_controller_port_device);
    CORE_FUNCTION(load_game);
    CORE_FUNCTION(unload_game);
    CORE_FUNCTION(run);
    CORE_FUNCTION(reset);
    CORE_FUNCTION(serialize_size);
    CORE_FUNCTION(serialize);
    CORE_FUNCTION(unserialize);
    CORE_FUNCTION(get_memory_data);
    CORE_FUNCTION(get_memory_size);
#undef CORE_FUNCTION
    bool initialized = false;
    bool loaded = false;

    void open(const fs::path& path)
    {
#ifdef _WIN32
        module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#else
        module = SDL_LoadObject(path.c_str());
#endif
        require(module != nullptr, "Snes9xコアを読み込めません: " + utf8(path));
#define LOAD(name) name = reinterpret_cast<decltype(name)>(GetProcAddress(module, "retro_" #name)); require(name != nullptr, "コアの関数がありません: retro_" #name)
        LOAD(set_environment); LOAD(set_video_refresh); LOAD(set_audio_sample); LOAD(set_audio_sample_batch);
        LOAD(set_input_poll); LOAD(set_input_state); LOAD(init); LOAD(deinit); LOAD(api_version);
        LOAD(get_system_info); LOAD(get_system_av_info); LOAD(set_controller_port_device);
        LOAD(load_game); LOAD(unload_game); LOAD(run); LOAD(reset); LOAD(get_memory_data); LOAD(get_memory_size);
        LOAD(serialize_size); LOAD(serialize); LOAD(unserialize);
        set_audio_speed = reinterpret_cast<decltype(set_audio_speed)>(GetProcAddress(module, "wiizv_set_audio_speed"));
        can_move = reinterpret_cast<decltype(can_move)>(GetProcAddress(module, "wiizv_can_move"));
        request_move = reinterpret_cast<decltype(request_move)>(GetProcAddress(module, "wiizv_request_move"));
        cancel_move = reinterpret_cast<decltype(cancel_move)>(GetProcAddress(module, "wiizv_cancel_move"));
        move_status = reinterpret_cast<decltype(move_status)>(GetProcAddress(module, "wiizv_move_status"));
        set_effects = reinterpret_cast<decltype(set_effects)>(GetProcAddress(module, "wiizv_set_effects"));
        normalize_mp = reinterpret_cast<decltype(normalize_mp)>(GetProcAddress(module, "wiizv_normalize_mp"));
        set_max_bonus = reinterpret_cast<decltype(set_max_bonus)>(GetProcAddress(module, "wiizv_set_max_bonus"));
        set_skip_logo = reinterpret_cast<decltype(set_skip_logo)>(GetProcAddress(module, "wiizv_set_skip_logo"));
        set_mixed_alignment = reinterpret_cast<decltype(set_mixed_alignment)>(GetProcAddress(module, "wiizv_set_mixed_alignment"));
        can_edit_character = reinterpret_cast<decltype(can_edit_character)>(GetProcAddress(module, "wiizv_can_edit_character"));
        request_gold = reinterpret_cast<decltype(request_gold)>(GetProcAddress(module, "wiizv_request_gold"));
        request_experience = reinterpret_cast<decltype(request_experience)>(GetProcAddress(module, "wiizv_request_experience"));
        request_alignment = reinterpret_cast<decltype(request_alignment)>(GetProcAddress(module, "wiizv_request_alignment"));
        gold_status = reinterpret_cast<decltype(gold_status)>(GetProcAddress(module, "wiizv_gold_status"));
        cancel_gold = reinterpret_cast<decltype(cancel_gold)>(GetProcAddress(module, "wiizv_cancel_gold"));
#undef LOAD
        require(api_version() == RETRO_API_VERSION, "コアAPIのバージョンが一致しません。");
    }

    ~Core()
    {
        if (loaded) unload_game();
        if (initialized) deinit();
        if (module) FreeLibrary(module);
    }
};

void video(const void* data, unsigned width, unsigned height, size_t pitch);
#ifdef __ANDROID__
struct Player;
void androidMenu(Player& session);
void androidDraw(Player& session);
bool androidEvent(Player& session, const SDL_Event& event);
void androidTick(Player& session);
inline uint16_t androidTouchMask = 0;
inline bool androidTouchesHeld = false;
inline unsigned androidMenuSerial = 0;
#endif

struct Player
{
    Core core;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    SDL_AudioStream* audio = nullptr;
    std::map<SDL_JoystickID, SDL_Gamepad*> gamepads;
    std::map<std::string, std::string> variables;
    fs::path dataDirectory;
    fs::path romPath;
    fs::path savePath;
    fs::path stateDirectory;
    std::string romId;
    std::string coreId;
    std::string directoryUtf8;
    std::vector<uint8_t> rom;
    std::vector<uint8_t> savedRam;
    std::future<std::vector<uint8_t>> pendingRamSave;
    std::vector<uint8_t> pixels;
    retro_pixel_format pixelFormat = RETRO_PIXEL_FORMAT_0RGB1555;
    unsigned width = 0;
    unsigned height = 0;
    size_t pitch = 0;
    double fps = 60.0;
    float aspect = 4.0f / 3.0f;
    bool paused = false;
    bool focused = true;
    bool swapButtons = false;
    bool smoke = false;
    bool loadedSavedRam = false;
    bool menuOpen = false;
    bool effectsMenu = false;
    bool characterMenu = false;
    unsigned editCharacter = 0, goldStep = 4, experienceLevels = 1;
    unsigned editAlignment = 0;
    static constexpr std::array<const char*, 4> ALIGNMENT_LABELS{"Good", "Neutral", "Evil", "Unknown"};
    enum class CharacterEdit { Gold, Experience, Alignment, Spells };
    static constexpr std::array<uint64_t, 13> GOLD_STEPS{1, 10, 100, 1000, 10000, 100000, 1000000,
        10000000, 100000000, 1000000000, 10000000000ULL, 100000000000ULL, characterData::MAX_GOLD};
    bool maximumBonus = false;
    bool skipMakerLogo = false;
    bool mixedAlignment = false;
    bool crtDisplay = false;
    CrtFilter crtFilter;
    unsigned effectMask = 0;
    static constexpr std::array<const char*, 5> EFFECT_KEYS{"always_lomilwa", "always_maporfic", "always_latumapic", "always_litofeito", "always_mp9"};
    static constexpr std::array<const char*, 5> EFFECT_LABELS{"Light (LOMILWA)", "Protection (MAPORFIC)", "Identify (LATUMAPIC)", "Levitate (LITOFEITO)", "MP: always 9 / no cost"};
    bool mapOpen = false;
    std::array<DungeonFloor, 8> dungeonMaps;
    int mapFloor = 1, mapX = 0, mapY = 0;
    Uint64 mapRepeatAt = 0;
    int mapStickX = 0, mapStickY = 0;
    std::map<SDL_JoystickID, bool> moveTriggerHeld;
    bool menuLoad = false;
    bool stateRecoveryFailed = false;
    bool inputBlocked = false;
    int menuRow = 0;
    int selectedSlot = 1;
    std::array<bool, 5> occupiedSlots{};
    std::map<SDL_JoystickID, bool> triggerHeld;
    std::string notice;
    Uint64 noticeUntil = 0;
    fs::path uiScreenshot;
    int speedIndex = 0;
    bool recordAudio = false;
    std::vector<int16_t> recordedAudio;
    bool replayInput = false;
    uint16_t replayButtons = 0;
    uint16_t gameButtons = 0;
    InputRepeat inputRepeat;
    uint64_t videoFrames = 0;
    uint64_t audioFrames = 0;
    uint64_t nonzeroSamples = 0;
    uint64_t audioHash = 1469598103934665603ull;
    std::string callbackError;

    void applyAudioSpeed(int index)
    {
        require(index >= 0 && index < static_cast<int>(GAME_SPEEDS.size()), "倍速の指定が不正です。");
        if (!core.set_audio_speed)
        {
            require(index == 0, "このコアは通常音声を維持する倍速に対応していません。");
            return;
        }
        require(core.set_audio_speed(SPEED_NUMERATORS[index], SPEED_DENOMINATORS[index]), "音声速度を設定できません。");
    }

    double gameSpeed() const
    {
        return GAME_SPEEDS[speedIndex];
    }

    void setSpeed(int index)
    {
        const int previous = speedIndex;
        applyAudioSpeed(index);
        speedIndex = index;
        try { saveSettings(); }
        catch (...) { speedIndex = previous; applyAudioSpeed(previous); throw; }
        inputRepeat.reset();
        gameButtons = 0;
        inputBlocked = true;
        if (audio) SDL_ClearAudioStream(audio);
    }

    void showNotice(const std::string& message)
    {
        notice = message;
        noticeUntil = SDL_GetTicks() + 4000;
    }

    void saveSettings()
    {
        std::string text = std::string("swap_abxy=") + (swapButtons ? "1" : "0") + "\nspeed_value=" + SPEED_VALUES[speedIndex] + "\n";
        for (unsigned effect = 0; effect < EFFECT_KEYS.size(); ++effect)
            text += std::string(EFFECT_KEYS[effect]) + "=" + ((effectMask & (1u << effect)) ? "1\n" : "0\n");
        text += std::string("maximum_bonus=") + (maximumBonus ? "1\n" : "0\n");
        text += std::string("skip_maker_logo=") + (skipMakerLogo ? "1\n" : "0\n");
        text += std::string("mixed_alignment=") + (mixedAlignment ? "1\n" : "0\n");
        text += std::string("crt_display=") + (crtDisplay ? "1\n" : "0\n");
        writeFile(dataDirectory / L"settings.ini", text.data(), text.size());
    }

    bool supportsEffects() const
    {
        return core.set_effects && romId == "afef55c8e8877b4324f6cc2da6aeacc9cb27bb83a8661e35a14ce7d73e5f0456";
    }

    void refreshEffects()
    {
        if (supportsEffects()) require(core.set_effects(effectMask), "常時効果を設定できません。");
        if (core.set_max_bonus) require(core.set_max_bonus(maximumBonus && supportsEffects()), "ボーナス最大化を設定できません。");
        if (core.set_skip_logo) require(core.set_skip_logo(skipMakerLogo && supportsEffects()), "ロゴスキップを設定できません。");
        if (core.set_mixed_alignment) require(core.set_mixed_alignment(mixedAlignment && supportsEffects()), "善悪混合設定を適用できません。");
    }

    void runFrame()
    {
        refreshEffects();
        core.run();
        refreshEffects();
    }

    void toggleEffect(unsigned index)
    {
        require(index < EFFECT_KEYS.size() && supportsEffects(), "このROMまたはコアは常時効果に対応していません。");
        require(index != 4 || core.normalize_mp, "このコアはMP維持に対応していません。");
        const auto previous = effectMask;
        effectMask ^= 1u << index;
        try { saveSettings(); }
        catch (...) { effectMask = previous; throw; }
        refreshEffects();
        showNotice(std::string(EFFECT_LABELS[index]) + ((effectMask & (1u << index)) ? ": ON" :
            index == 4 ? ": OFF (normal MP limits)" : ": OFF (effect cleared)"));
    }

    void toggleMaximumBonus()
    {
        require(supportsEffects() && core.set_max_bonus, "このROMまたはコアはボーナス最大化に対応していません。");
        maximumBonus = !maximumBonus;
        try { saveSettings(); }
        catch (...) { maximumBonus = !maximumBonus; throw; }
        refreshEffects();
        showNotice(maximumBonus ? "New character bonus: maximum (60)" : "New character bonus: normal");
    }

    void toggleSkipMakerLogo()
    {
        require(supportsEffects() && core.set_skip_logo, "このROMまたはコアはロゴスキップに対応していません。");
        skipMakerLogo = !skipMakerLogo;
        try { saveSettings(); }
        catch (...) { skipMakerLogo = !skipMakerLogo; throw; }
        refreshEffects();
        showNotice(skipMakerLogo ? "Skip maker logo: ON (next boot / reset)" : "Skip maker logo: OFF");
    }

    void toggleMixedAlignment()
    {
        require(supportsEffects() && core.set_mixed_alignment, "このROMまたはコアは善悪混合に対応していません。");
        mixedAlignment = !mixedAlignment;
        try { saveSettings(); }
        catch (...) { mixedAlignment = !mixedAlignment; throw; }
        refreshEffects();
        showNotice(mixedAlignment ? "Mixed alignment: ON (reopen tavern list)" : "Mixed alignment: OFF");
    }

    void toggleCrtDisplay()
    {
        crtDisplay = !crtDisplay;
        try { saveSettings(); }
        catch (...) { crtDisplay = !crtDisplay; throw; }
        showNotice(crtDisplay ? "CRT display: ON" : "CRT display: OFF");
    }

    int menuRows() const { return characterMenu ? 10 : effectsMenu ? static_cast<int>(EFFECT_KEYS.size()) + 1 : 14; }
    int menuRowHeight() const { return characterMenu || effectsMenu ? 32 : 24; }

    void toggleButtons()
    {
        swapButtons = !swapButtons;
        try { saveSettings(); }
        catch (...) { swapButtons = !swapButtons; throw; }
        inputRepeat.reset();
        gameButtons = 0;
        inputBlocked = true;
    }

    fs::path slotPath(int slot) const
    {
        require(slot >= 1 && slot <= 5, "スロット番号は1〜5で指定してください。");
        return stateDirectory / (romPath.stem().wstring() + L".slot" + std::to_wstring(slot) + L".wvs");
    }

    std::vector<uint8_t> captureState()
    {
        require(core.loaded, "ROMが起動していません。");
        auto size = core.serialize_size();
        require(size > 0 && size <= 31 * 1024 * 1024, "コアがステート保存に対応していません。");
        std::vector<uint8_t> state(size);
        // ステート内のAPU端数は1倍基準に統一し、読込時の倍率に依存させない。
        applyAudioSpeed(0);
        const bool captured = core.serialize(state.data(), state.size());
        applyAudioSpeed(speedIndex);
        require(captured, "現在のステートを取得できません。");
        return state;
    }

    void refreshSlots()
    {
        for (int slot = 1; slot <= 5; ++slot) occupiedSlots[slot - 1] = fs::is_regular_file(slotPath(slot));
    }

    void saveState(int slot)
    {
        require(!stateRecoveryFailed, "復元に失敗したため、ROMを開き直してください。");
        auto file = encodeState(captureState(), romId, coreId);
        fs::create_directories(stateDirectory);
        writeFile(slotPath(slot), file.data(), file.size());
        occupiedSlots[slot - 1] = true;
        showNotice("Saved slot " + std::to_string(slot));
    }

    void applyStateFile(const std::vector<uint8_t>& file)
    {
        // 同一上流コミットの旧配布コアだけは、復元互換性を検証したうえで受け入れる。
        auto expectedCore = coreId;
        constexpr std::array<std::string_view, 23> LEGACY_CORES{
            "d5e144475deeb22d152d1e28a2649f5a53092a65c9ec204c6f5d97968f30f139",
            "9f42fb5ef81ad89adb57a47643f9f62badde4ca0c3c8a6709b98649cbf45cd7e",
            "92869c40012d003d8c5194824e14b3270db9f28235ce769064d6f967304a4309",
            "854326931f62dcbfee7774e51973921618c28223653b298e4ffe743eabc479b6",
            "3d972590a2fdfb97c90139252eae31068ede41aa0c57127c163631ae48b4f0a4",
            "d668312162011c27f98dda326a3bceeaceec140c979518dfcf56ceb457da04be",
            "69f1eafb2c32452632db7cb4518e43231bb6684db3ff8abf4a11bfb83f520cb5",
            "c19db7144b6de063eedbc0db43ad18fc04c24d3a654d607b7b4bb8e60ebbf8f8",
            "8601f2bf26cd8dd8e4be02be6da12dd2f618070dd09a54a88363861108e3ddfc",
            "d948938febb02754ff06d87d1d7554a57642c7225936e02fc2f07d628759db12",
            "bf9304fc4f58426eb88d4c6860a041db7c2fa53cfdba3e0e741ead8d3b132902",
            "ea9ccc55453f6908c82378b1dbbedf3efcbf2b88f110df85b0ed83a61bf91d9c",
            "5a3e49bbca8fde8ef8ad571792b0fbc1f631bf2e0cdc713b87301d344fd7790d",
            "a113f9dc5b1533d65a34f4e4783a9310944786bf1dbb00e32ddb0f79948bcef3",
            "aac9a940e696908b2df5c699f0a7272031b0cde3f72f44b2544b9d05c8d3163f",
            "fb33259602e870ea9342aca9863c4f1257bd43d8874e943907c9f5dc2041cc64",
            "cad00256d6fff4b46db0b35c5710764b224a00f5e2fef0e4009345f871c3d566",
            "fdbe9d4a85ea32dcbce593472e0b11e5b9149b636dd255a399bcf94d29189d2b",
            "04398d4d325e36448ccf5582c6a547ce68476d274acb1c51daef77dd048de1f8",
            "615ad30670706cd086e99a042f5bf41d6b739a0e9a4e69c7fbf196dc2f31732f",
            "4cfaa74f2145620fad167613344e7b2867eb15e2e3cf5b66c198c09766593ff2",
            "32ac5a34e8a5555d33664afbfccc52a7aaf36b89f54d6414709250804217124c",
            "6f86b87de0dd186a31f0b89b473af3d58f8bf2c0d97472fc345d726ec79e2d97"};
        for (auto legacy : LEGACY_CORES)
            if (core.set_audio_speed && file.size() >= 208 &&
                std::string_view(reinterpret_cast<const char*>(file.data()) + 72, 64) == legacy)
                expectedCore = legacy;
        auto payload = decodeState(file, romId, expectedCore, core.serialize_size());
        auto rollback = captureState();
        if (core.cancel_move) core.cancel_move();
        if (core.cancel_gold) core.cancel_gold();
        applyAudioSpeed(0);
        if (!core.unserialize(payload.data(), payload.size()))
        {
            if (!core.unserialize(rollback.data(), rollback.size()))
            {
                stateRecoveryFailed = true;
                paused = true;
                throw std::runtime_error("ステートと元の状態の復元に失敗しました。ROMを開き直してください。");
            }
            applyAudioSpeed(speedIndex);
            throw std::runtime_error("コアがステートを読み込めませんでした。元の状態を維持します。");
        }
        applyAudioSpeed(speedIndex);
        refreshEffects();
        if (supportsEffects() && !(effectMask & 16) && core.normalize_mp) core.normalize_mp();
        inputRepeat.reset();
        gameButtons = 0;
        SDL_ClearAudioStream(audio);
        // 復元直後は以前のフレームを表示せず、次の実行フレームで更新する。
        if (texture) { SDL_DestroyTexture(texture); texture = nullptr; }
        pixels.clear();
    }

    void loadState(int slot)
    {
        require(!stateRecoveryFailed, "復元に失敗したため、ROMを開き直してください。");
        const auto path = slotPath(slot);
        try { applyStateFile(readFile(path)); }
        catch (const std::exception& error) { throw std::runtime_error(std::string(error.what()) + "\n" + utf8(path)); }
        menuOpen = false;
        paused = false;
        inputBlocked = true;
        showNotice("Loaded slot " + std::to_string(slot));
    }

    void toggleMenu()
    {
        if (!menuOpen)
        {
            effectsMenu = false;
            characterMenu = false;
            refreshSlots();
            menuRow = selectedSlot - 1;
        }
        menuOpen = !menuOpen;
        if (!menuOpen) inputBlocked = true;
        SDL_ClearAudioStream(audio);
#ifdef __ANDROID__
        if (!smoke && menuOpen) { ++androidMenuSerial; androidMenu(*this); }
#endif
    }

    void activateMenu()
    {
        if (characterMenu)
        {
            if (menuRow < 2 || menuRow == 3 || menuRow == 5) changeCharacterChoice(1);
            else if (menuRow == 2) executeGoldAdd();
            else if (menuRow == 4) executeExperienceAdd();
            else if (menuRow == 6) executeAlignmentChange();
            else if (menuRow == 7) restoreEditBackup();
            else if (menuRow == 8) executeLearnAllSpells();
            else backMenu();
            return;
        }
        if (effectsMenu)
        {
            if (menuRow < static_cast<int>(EFFECT_KEYS.size())) toggleEffect(static_cast<unsigned>(menuRow));
            else { effectsMenu = false; menuRow = 7; }
            return;
        }
        if (menuRow < 5)
        {
            selectedSlot = menuRow + 1;
            if (menuLoad) loadState(selectedSlot);
            else saveState(selectedSlot);
        }
        else if (menuRow == 5) toggleButtons();
        else if (menuRow == 6) setSpeed((speedIndex + 1) % static_cast<int>(GAME_SPEEDS.size()));
        else if (menuRow == 7) { effectsMenu = true; menuRow = 0; }
        else if (menuRow == 8) toggleMaximumBonus();
        else if (menuRow == 9) toggleSkipMakerLogo();
        else if (menuRow == 10) toggleMixedAlignment();
        else if (menuRow == 11) toggleCrtDisplay();
        else if (menuRow == 12) openCharacterMenu();
        else toggleMenu();
    }

    void backMenu()
    {
        if (characterMenu) { characterMenu = false; menuRow = 12; }
        else if (effectsMenu) { effectsMenu = false; menuRow = 7; }
        else toggleMenu();
    }

    std::vector<unsigned> registeredCharacters() const
    {
        std::vector<unsigned> result;
        if (!supportsEffects()) return result;
        for (unsigned character = 0; character < 20; ++character)
            if (gameRam()[0x1000 + character * 0x80]) result.push_back(character);
        return result;
    }

    uint64_t characterGold() const
    {
        require(editCharacter < 20 && gameRam()[0x1000 + editCharacter * 0x80], "対象のキャラクターが登録されていません。");
        uint64_t gold = 0;
        require(characterData::readGold(gameRam() + 0x1000 + editCharacter * 0x80, gold), "GOLDの保存形式が不正なため編集できません。");
        return gold;
    }

    uint64_t characterExperience() const
    {
        require(editCharacter < 20 && gameRam()[0x1000 + editCharacter * 0x80], "対象のキャラクターが登録されていません。");
        uint64_t experience = 0;
        require(characterData::readExperience(gameRam() + 0x1000 + editCharacter * 0x80, experience), "経験値の保存形式が不正です。");
        return experience;
    }

    unsigned characterAlignment() const
    {
        require(editCharacter < 20 && gameRam()[0x1000 + editCharacter * 0x80], "対象のキャラクターが登録されていません。");
        return gameRam()[0x1000 + editCharacter * 0x80 + 0xa] & 3;
    }

    uint64_t targetExperience() const
    {
        require(editCharacter < 20, "対象のキャラクターが不正です。");
        uint64_t target = 0;
        require(characterData::experienceTarget(gameRam() + 0x1000 + editCharacter * 0x80,
            rom.data(), rom.size(), experienceLevels, target), "加算する経験値を求められません。キャラクターデータを確認してください。");
        return target;
    }

    void openCharacterMenu()
    {
        require(!stateRecoveryFailed && supportsEffects() && core.can_edit_character && core.request_gold && core.gold_status && core.cancel_gold,
            "このROMまたはコアはキャラクター編集に対応していません。");
        require(core.can_edit_character(), "キャラクター編集は城・酒場・訓練場・迷宮の入力待ちで開いてください。");
        const auto characters = registeredCharacters();
        require(!characters.empty(), "登録済みのキャラクターがいません。");
        if (std::find(characters.begin(), characters.end(), editCharacter) == characters.end()) editCharacter = characters.front();
        editAlignment = characterAlignment() % 3;
        characterMenu = true; effectsMenu = false; menuRow = 0;
    }

    void changeCharacterChoice(int direction)
    {
        if (menuRow == 0)
        {
            const auto characters = registeredCharacters();
            require(!characters.empty(), "登録済みのキャラクターがいません。");
            const auto found = std::find(characters.begin(), characters.end(), editCharacter);
            const int index = found == characters.end() ? 0 : static_cast<int>(found - characters.begin());
            const int count = static_cast<int>(characters.size());
            editCharacter = characters[(index + direction + count) % count];
            editAlignment = characterAlignment() % 3;
        }
        else if (menuRow == 1)
            goldStep = static_cast<unsigned>((static_cast<int>(goldStep) + direction + static_cast<int>(GOLD_STEPS.size())) % static_cast<int>(GOLD_STEPS.size()));
        else if (menuRow == 3)
            experienceLevels = static_cast<unsigned>((static_cast<int>(experienceLevels) - 1 + direction + 3) % 3 + 1);
        else if (menuRow == 5)
            editAlignment = static_cast<unsigned>((static_cast<int>(editAlignment) + direction + 3) % 3);
    }

    fs::path editBackupPath() const { return slotPath(1).parent_path() / (romPath.stem().wstring() + L".before-edit.wvs"); }

    void executeGoldAdd() { executeCharacterEdit(CharacterEdit::Gold); }
    void executeExperienceAdd() { executeCharacterEdit(CharacterEdit::Experience); }
    void executeAlignmentChange() { executeCharacterEdit(CharacterEdit::Alignment); }
    void executeLearnAllSpells() { executeCharacterEditDirect(CharacterEdit::Spells); }

    void executeCharacterEditDirect(CharacterEdit kind)
    {
        require(!stateRecoveryFailed&&supportsEffects()&&characterMenu&&core.can_edit_character&&core.can_edit_character(),"城の施設または迷宮の入力待ちで編集してください。");
        require(editCharacter<20&&gameRam()[0x1000+editCharacter*0x80],"対象のキャラクターが登録されていません。");
        auto* record=gameRam()+0x1000+editCharacter*0x80;
        auto* sram=static_cast<uint8_t*>(core.get_memory_data(RETRO_MEMORY_SAVE_RAM));
        require(sram&&core.get_memory_size(RETRO_MEMORY_SAVE_RAM)>=0x1400,"通常セーブ領域が不正です。");
        std::array<uint8_t,0x80> edited{},oldRecord{},oldPrimary{},oldMirror{};
        std::copy_n(record,0x80,oldRecord.begin());edited=oldRecord;
        std::copy_n(sram+editCharacter*0x80,0x80,oldPrimary.begin());
        std::copy_n(sram+0xa00+editCharacter*0x80,0x80,oldMirror.begin());
        if(kind==CharacterEdit::Gold){
            require(goldStep<GOLD_STEPS.size(),"加算額が不正です。");
            const auto previous=characterGold();require(previous<characterData::MAX_GOLD,"GOLDはすでに上限です。");
            characterData::writeGold(edited.data(),previous+std::min(GOLD_STEPS[goldStep],characterData::MAX_GOLD-previous));
        }else if(kind==CharacterEdit::Experience){
            const auto previous=characterExperience(),target=targetExperience();require(previous!=target,"経験値はすでに保存可能な上限です。");
            characterData::writeExperience(edited.data(),target);
        }else if(kind==CharacterEdit::Alignment){
            require(editAlignment<3&&characterAlignment()!=editAlignment,"すでに指定した属性、または無効な属性です。");
            edited[0xa]=static_cast<uint8_t>((edited[0xa]&0xfc)|editAlignment);
        }else{
            if(characterData::allSpellsLearned(record)){showNotice("All spells already learned");return;}
            std::fill(edited.begin()+0x35,edited.begin()+0x43,0xff);
        }
        require(characterData::updateChecksum(edited.data(),rom.data(),rom.size()),"保存チェックサムを更新できません。");
        const auto before=encodeState(captureState(),romId,coreId);
        writeFile(editBackupPath(),before.data(),before.size());
        // CPU・APU・映像は実行せず、対象レコードと二重SRAMだけを更新する。
        std::copy(edited.begin(),edited.end(),record);
        std::copy(edited.begin(),edited.end(),sram+editCharacter*0x80);
        std::copy(edited.begin(),edited.end(),sram+0xa00+editCharacter*0x80);
        try{saveRam();}
        catch(...){
            std::copy(oldRecord.begin(),oldRecord.end(),record);
            std::copy(oldPrimary.begin(),oldPrimary.end(),sram+editCharacter*0x80);
            std::copy(oldMirror.begin(),oldMirror.end(),sram+0xa00+editCharacter*0x80);throw;
        }
        inputBlocked=true;SDL_ClearAudioStream(audio);
        showNotice(kind==CharacterEdit::Spells?"Learned all spells - backup saved":"Character updated - backup saved");
    }

    void executeCharacterEdit(CharacterEdit kind)
    {
        executeCharacterEditDirect(kind);
    }

    void restoreEditBackup()
    {
        require(!stateRecoveryFailed && characterMenu && core.can_edit_character && core.can_edit_character(), "城の施設または迷宮の入力待ちで復元してください。");
        const auto before = encodeState(captureState(), romId, coreId);
        const auto backup = readFile(editBackupPath());
        try
        {
            applyStateFile(backup);
            saveRam();
            showNotice("Restored pre-edit game state");
        }
        catch (...) { applyStateFile(before); throw; }
        inputBlocked = true;
    }

    void reportActionError(const std::exception& error)
    {
        showNotice("Failed - see error message");
        SDL_ClearAudioStream(audio);
#ifdef __ANDROID__
        if (!smoke) androidToast(error.what());
#else
        if (!smoke) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "WiizV", error.what(), window);
#endif
    }

    SDL_FRect menuBounds() const
    {
        int w = 0, h = 0;
        renderOutputSize(renderer, &w, &h);
        const float menuHeight = effectsMenu ? 414.0f : 446.0f;
        return {(w - 560.0f) / 2, std::max(34.0f, (h - menuHeight) / 2), 560, menuHeight};
    }

    ~Player()
    {
        if (pendingRamSave.valid()) pendingRamSave.wait();
#ifdef __ANDROID__
        androidClearText();
#endif
        // コア終了時のコールバックに備え、出力デバイスより先に終了する。
        if (core.loaded) { core.unload_game(); core.loaded = false; }
        if (core.initialized) { core.deinit(); core.initialized = false; }
        for (auto [id, pad] : gamepads) { (void)id; SDL_CloseGamepad(pad); }
        if (audio) SDL_DestroyAudioStream(audio);
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
    }

    void saveRam()
    {
        finishRamSave(true);
        if (!core.loaded || stateRecoveryFailed) return;
        auto size = core.get_memory_size(RETRO_MEMORY_SAVE_RAM);
        auto* data = static_cast<uint8_t*>(core.get_memory_data(RETRO_MEMORY_SAVE_RAM));
        if (!size || !data) return;
        if (savedRam.size() == size && std::memcmp(savedRam.data(), data, size) == 0) return;
        writeFile(savePath, data, size);
        savedRam.assign(data, data + size);
    }

    void finishRamSave(bool wait = false)
    {
        if (!pendingRamSave.valid()) return;
        if (!wait && pendingRamSave.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        savedRam = pendingRamSave.get();
    }

    void saveRamAsync()
    {
        finishRamSave();
        if (pendingRamSave.valid() || !core.loaded || stateRecoveryFailed) return;
        const auto size = core.get_memory_size(RETRO_MEMORY_SAVE_RAM);
        const auto* data = static_cast<const uint8_t*>(core.get_memory_data(RETRO_MEMORY_SAVE_RAM));
        if (!size || !data) return;
        if (savedRam.size() == size && std::memcmp(savedRam.data(), data, size) == 0) return;
        // コアのメモリは実行スレッドでコピーし、保存先への書き込みだけを分離する。
        pendingRamSave = std::async(std::launch::async,
            [path = savePath, snapshot = std::vector<uint8_t>(data, data + size)]() mutable
            {
                writeFile(path, snapshot.data(), snapshot.size());
                return std::move(snapshot);
            });
    }

    uint8_t* gameRam() const
    {
        return static_cast<uint8_t*>(core.get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    }

    void closeMap()
    {
        mapOpen = false;
        inputBlocked = true;
        gameButtons = 0;
        inputRepeat.reset();
        SDL_ClearAudioStream(audio);
    }

    void openMap()
    {
        require(core.can_move && core.request_move && core.cancel_move && core.move_status && !dungeonMaps[0].empty(), "このROMまたはコアは移動機能に対応していません。");
        require(!stateRecoveryFailed && core.can_move(), "移動メニューは迷宮の探索中に開いてください。");
        auto* ram = gameRam();
        mapFloor = ram[0xb7];
        mapX = static_cast<int8_t>(ram[0xb5]);
        mapY = static_cast<int8_t>(ram[0xb6]);
        if (!findCell(dungeonMaps[mapFloor - 1], mapX, mapY))
        {
            mapX = dungeonMaps[mapFloor - 1].front().x;
            mapY = dungeonMaps[mapFloor - 1].front().y;
        }
        menuOpen = false;
        mapOpen = true;
        mapStickX = mapStickY = 0;
        SDL_ClearAudioStream(audio);
    }

    void selectMapFloor(int delta)
    {
        mapFloor = (mapFloor - 1 + delta + 8) % 8 + 1;
        const auto& cells = dungeonMaps[mapFloor - 1];
        const auto closest = std::min_element(cells.begin(), cells.end(), [&](const auto& a, const auto& b)
        {
            return std::abs(a.x - mapX) + std::abs(a.y - mapY) < std::abs(b.x - mapX) + std::abs(b.y - mapY);
        });
        mapX = closest->x;
        mapY = closest->y;
    }

    void moveMapCursor(int dx, int dy)
    {
        const auto& cells = dungeonMaps[mapFloor - 1];
        const DungeonCell* best = nullptr;
        int bestDistance = 100000;
        for (const auto& cell : cells)
        {
            const int along = (cell.x - mapX) * dx + (cell.y - mapY) * dy;
            const int across = std::abs((cell.x - mapX) * dy - (cell.y - mapY) * dx);
            if (along <= 0) continue;
            const int distance = across * 512 + along;
            if (distance < bestDistance) { best = &cell; bestDistance = distance; }
        }
        if (best) { mapX = best->x; mapY = best->y; }
    }

    void executeMapMove()
    {
        require(mapOpen && findCell(dungeonMaps[mapFloor - 1], mapX, mapY), "マップに存在しない座標へは移動できません。");
        require(core.can_move && core.can_move(), "探索状態が変化したため移動できません。");
        const auto before = encodeState(captureState(), romId, coreId);
        saveState(1);
        require(core.request_move(mapX, mapY, static_cast<unsigned>(mapFloor)), "移動を開始できません。");
        try
        {
            // 入力待ちからゲーム本来の階層読込・描画へ進める。入力はこの間遮断する。
            gameButtons = 0;
            for (int frame = 0; frame < 180; ++frame)
            {
                runFrame();
                require(callbackError.empty(), callbackError);
                auto* ram = gameRam();
                if (core.move_status() == 2)
                {
                    require(ram[0xb5] == static_cast<uint8_t>(mapX) && ram[0xb6] == static_cast<uint8_t>(mapY) && ram[0xb7] == mapFloor, "移動先の座標が一致しません。");
                    for (int refresh = 0; refresh < 12; ++refresh)
                    {
                        runFrame();
                        require(callbackError.empty(), callbackError);
                    }
                    core.cancel_move();
                    closeMap();
                    showNotice("Moved to B" + std::to_string(mapFloor) + " (" + std::to_string(mapX) + ", " + std::to_string(mapY) + ") - saved slot 1");
                    return;
                }
            }
            throw std::runtime_error("移動の完了を確認できませんでした。");
        }
        catch (...)
        {
            core.cancel_move();
            applyStateFile(before);
            closeMap();
            throw;
        }
    }

    struct MapLayout { float x, y, cell; int minX, maxY; };

    MapLayout mapLayout() const
    {
        int outputWidth = 0, outputHeight = 0;
        renderOutputSize(renderer, &outputWidth, &outputHeight);
        const int columns = std::max(1, (outputWidth - 280) / 20);
        const int rows = std::max(1, (outputHeight - 190) / 20);
        return {32, 116, 20, mapX - columns / 2, mapY + rows / 2};
    }

    MapLayout overviewLayout() const
    {
        int outputWidth = 0, outputHeight = 0;
        renderOutputSize(renderer, &outputWidth, &outputHeight);
        int minX = 127, maxX = -128, minY = 127, maxY = -128;
        for (const auto& cell : dungeonMaps[mapFloor - 1])
        {
            minX = std::min(minX, cell.x); maxX = std::max(maxX, cell.x);
            minY = std::min(minY, cell.y); maxY = std::max(maxY, cell.y);
        }
        const float size = std::min(200.0f / (maxX - minX + 1), 200.0f / (maxY - minY + 1));
        return {outputWidth - 224.0f + (200 - size * (maxX - minX + 1)) / 2, 144 + (200 - size * (maxY - minY + 1)) / 2, size, minX, maxY};
    }

    void drawMap()
    {
        int outputWidth = 0, outputHeight = 0;
        renderOutputSize(renderer, &outputWidth, &outputHeight);
        sdlCheck(SDL_SetRenderDrawColor(renderer, 12, 18, 28, 255));
        SDL_FRect background{0, 36, static_cast<float>(outputWidth), outputHeight - 36.0f};
        sdlCheck(SDL_RenderFillRect(renderer, &background));
        sdlCheck(SDL_SetRenderDrawColor(renderer, 230, 235, 240, 255));
        const auto title = "MOVE  B" + std::to_string(mapFloor) + "   X: " + std::to_string(mapX) + "  Y: " + std::to_string(mapY);
        sdlCheck(SDL_RenderDebugText(renderer, 24, 48, title.c_str()));
        sdlCheck(SDL_RenderDebugText(renderer, 24, 67, "Arrows / left stick: XY   Q/W or LB/RB: floor"));
        sdlCheck(SDL_RenderDebugText(renderer, 24, 84, swapButtons ? "Enter / pad bottom: Move (overwrites slot 1)   Esc / pad right: Back" : "Enter / pad right: Move (overwrites slot 1)   Esc / pad bottom: Back"));
        const auto* ram = gameRam();
        const auto layout = mapLayout();
        SDL_Rect clip{32, 116, (outputWidth - 280) / 20 * 20 + 1, (outputHeight - 190) / 20 * 20 + 1};
        auto drawCells = [&](const MapLayout& grid)
        {
          for (const auto& cell : dungeonMaps[mapFloor - 1])
          {
            const float x = grid.x + (cell.x - grid.minX) * grid.cell;
            const float y = grid.y + (grid.maxY - cell.y) * grid.cell;
            const float size = grid.cell;
            const bool selected = cell.x == mapX && cell.y == mapY;
            const bool current = ram[0xb7] == mapFloor && static_cast<int8_t>(ram[0xb5]) == cell.x && static_cast<int8_t>(ram[0xb6]) == cell.y;
            SDL_FRect square{x, y, size, size};
            SDL_SetRenderDrawColor(renderer, selected ? 210 : current ? 45 : 38, selected ? 160 : current ? 155 : 55, selected ? 45 : current ? 155 : 76, 255);
            SDL_RenderFillRect(renderer, &square);
            if (cell.event && size >= 8)
            {
                SDL_SetRenderDrawColor(renderer, 145, 165, 190, 255);
                SDL_FRect dot{x + size * .4f, y + size * .4f, size * .2f, size * .2f};
                SDL_RenderFillRect(renderer, &dot);
            }
            const float lines[4][4]{{x,y,x+size,y}, {x+size,y,x+size,y+size}, {x,y+size,x+size,y+size}, {x,y,x,y+size}};
            for (int direction = 0; direction < 4; ++direction)
            {
                const auto wall = (cell.walls >> (direction * 2)) & 3;
                SDL_SetRenderDrawColor(renderer, wall == 2 ? 90 : wall ? 195 : 53, wall == 2 ? 180 : wall ? 205 : 72, wall == 2 ? 220 : wall ? 220 : 96, 255);
                SDL_RenderLine(renderer, lines[direction][0], lines[direction][1], lines[direction][2], lines[direction][3]);
            }
            if (selected)
            {
                SDL_SetRenderDrawColor(renderer, 255, 220, 80, 255);
                SDL_RenderRect(renderer, &square);
            }
          }
        };
        sdlCheck(SDL_SetRenderClipRect(renderer, &clip));
        drawCells(layout);
        sdlCheck(SDL_SetRenderClipRect(renderer, nullptr));
        SDL_SetRenderDrawColor(renderer, 80, 100, 125, 255);
        SDL_FRect border{32, 116, static_cast<float>(clip.w), static_cast<float>(clip.h)};
        SDL_RenderRect(renderer, &border);
        SDL_SetRenderDrawColor(renderer, 230, 235, 240, 255);
        SDL_RenderDebugText(renderer, outputWidth - 224.0f, 116, "FULL FLOOR (click)");
        drawCells(overviewLayout());
        SDL_SetRenderDrawColor(renderer, 200, 210, 225, 255);
        for (int column = 0; column < clip.w / 20; ++column)
        {
            const auto value = layout.minX + column;
            if (value % 5 == 0) SDL_RenderDebugText(renderer, layout.x + column * 20, layout.y + clip.h + 5, std::to_string(value).c_str());
        }
        for (int row = 0; row < clip.h / 20; ++row)
        {
            const auto value = layout.maxY - row;
            if (value % 5 == 0) SDL_RenderDebugText(renderer, 4, layout.y + row * 20 + 6, std::to_string(value).c_str());
        }
        SDL_SetRenderDrawColor(renderer, 230, 235, 240, 255);
        SDL_RenderDebugText(renderer, 24, outputHeight - 49.0f, "N: up / Y+   Cyan: current position   Yellow: destination   Blue wall: door");
    }

    void drawCharacterName(float x, float y)
    {
        const auto* vram = static_cast<const uint8_t*>(core.get_memory_data(RETRO_MEMORY_VIDEO_RAM));
        if (!vram || core.get_memory_size(RETRO_MEMORY_VIDEO_RAM) < 0x5000 || editCharacter >= 20) return;
        const auto* record = gameRam() + 0x1000 + editCharacter * 0x80;
        std::vector<SDL_FRect> dots;
        // 城で使用中のゲーム内フォントから名前を描き、日本語名も同じ字形で表示する。
        for (unsigned letter = 0; letter < 8; ++letter)
        {
            const auto* glyph = vram + 0x4000 + record[2 + letter] * 16;
            for (unsigned row = 0; row < 8; ++row)
            {
                const auto foreground = glyph[row * 2] & glyph[row * 2 + 1];
                for (unsigned column = 0; column < 8; ++column)
                    if (foreground & (0x80 >> column)) dots.push_back({x + (letter * 8 + column) * 2, y + row * 2, 2, 2});
            }
        }
        if (!dots.empty()) sdlCheck(SDL_RenderFillRects(renderer, dots.data(), static_cast<int>(dots.size())));
        constexpr const char* classes[]{"Fig", "Mag", "Pri", "Thi", "Bis", "Sam", "Lor", "Nin"};
        const auto kind = (record[0xa] >> 2) & 7;
        const char alignment = "GNE?"[record[0xa] & 3];
        const std::string label = std::string(1, alignment) + "-" + classes[kind];
        sdlCheck(SDL_RenderDebugText(renderer, x + 152, y + 4, label.c_str()));
    }

    void draw()
    {
#ifdef __ANDROID__
        if (!smoke) { androidDraw(*this); return; }
#endif
        int windowWidth = 0, windowHeight = 0;
        sdlCheck(renderOutputSize(renderer, &windowWidth, &windowHeight));
        sdlCheck(SDL_SetRenderDrawColor(renderer, 8, 10, 15, 255));
        sdlCheck(SDL_RenderClear(renderer));
        if (texture)
        {
            const float availableHeight = static_cast<float>(std::max(1, windowHeight - 36));
            const float drawWidth = std::min(static_cast<float>(windowWidth), availableHeight * aspect);
            const float drawHeight = drawWidth / aspect;
            SDL_FRect destination{(windowWidth - drawWidth) / 2, 36 + (availableHeight - drawHeight) / 2, drawWidth, drawHeight};
            sdlCheck(SDL_SetTextureScaleMode(texture, crtDisplay ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST));
            sdlCheck(SDL_RenderTexture(renderer, texture, nullptr, &destination));
            if (crtDisplay) sdlCheck(crtFilter.draw(renderer, destination, height));
        }
        sdlCheck(SDL_SetRenderDrawColor(renderer, 35, 42, 55, 255));
        SDL_FRect bar{0, 0, static_cast<float>(windowWidth), 34};
        sdlCheck(SDL_RenderFillRect(renderer, &bar));
        sdlCheck(SDL_SetRenderDrawColor(renderer, 230, 235, 240, 255));
        sdlCheck(SDL_RenderDebugText(renderer, 10, 12, "[ Open ROM ]  [ Reset ]  [ Pause ]  [ ABXY ]  [ Menu ]"));
        sdlCheck(SDL_RenderDebugText(renderer, 450, 12, swapButtons ? "SWAPPED" : "SNES"));
        if (paused || !focused || menuOpen || mapOpen) sdlCheck(SDL_RenderDebugText(renderer, 530, 12, "PAUSED"));
        const auto speedText = std::string("Speed: ") + SPEED_LABELS[speedIndex];
        sdlCheck(SDL_RenderDebugText(renderer, 600, 12, speedText.c_str()));
        if (menuOpen)
        {
            auto box = menuBounds();
            sdlCheck(SDL_SetRenderDrawColor(renderer, 22, 29, 43, 255));
            sdlCheck(SDL_RenderFillRect(renderer, &box));
            sdlCheck(SDL_SetRenderDrawColor(renderer, 160, 180, 210, 255));
            sdlCheck(SDL_RenderRect(renderer, &box));
            sdlCheck(SDL_RenderDebugText(renderer, box.x + 20, box.y + 18, characterMenu ? "CHARACTER EDIT" : effectsMenu ? "ALWAYS EFFECTS" : "FUNCTION MENU"));
            sdlCheck(SDL_RenderDebugText(renderer, box.x + 20, box.y + 40, characterMenu ? "Left/Right: character or amount   Enter: next / apply" : effectsMenu ? "Up/Down: effect   Confirm: ON or OFF" : "Up/Down: slot   Left/Right: Save or Load"));
            sdlCheck(SDL_RenderDebugText(renderer, box.x + 20, box.y + 56, swapButtons ? "Enter / pad bottom: OK   Esc / pad right: Back" : "Enter / pad right: OK   Esc / pad bottom: Back"));
            for (int row = 0; row < menuRows(); ++row)
            {
                const float y = box.y + 84 + row * static_cast<float>(menuRowHeight());
                if (row == menuRow)
                {
                    sdlCheck(SDL_SetRenderDrawColor(renderer, 48, 68, 98, 255));
                    SDL_FRect selection{box.x + 12, y - 7, box.w - 24, static_cast<float>(menuRowHeight() - 2)};
                    sdlCheck(SDL_RenderFillRect(renderer, &selection));
                }
                sdlCheck(SDL_SetRenderDrawColor(renderer, 230, 235, 240, 255));
                if (characterMenu)
                {
                    uint64_t gold = 0;
                    const bool valid = editCharacter < 20 && gameRam()[0x1000 + editCharacter * 0x80] &&
                        characterData::readGold(gameRam() + 0x1000 + editCharacter * 0x80, gold);
                    const auto after = gold + std::min(GOLD_STEPS[goldStep], characterData::MAX_GOLD - gold);
                    uint64_t experience = 0, target = 0, gain = 0;
                    const auto* record = gameRam() + 0x1000 + editCharacter * 0x80;
                    const bool validExperience = core.request_experience && characterData::readExperience(record, experience) &&
                        characterData::experienceTarget(record, rom.data(), rom.size(), experienceLevels, target) &&
                        characterData::experienceGain(record, rom.data(), rom.size(), experienceLevels, gain);
                    const auto label = row == 0 ? "Character: #" + std::to_string(editCharacter + 1) :
                        row == 1 ? "Add: +" + characterData::goldText(GOLD_STEPS[goldStep]) :
                        row == 2 ? valid ? "Add GOLD: " + characterData::goldText(gold) + " -> " + characterData::goldText(after) : std::string("Add GOLD: invalid data") :
                        row == 3 ? "Next EXP +" + std::to_string(experienceLevels) + " level(s): +" + characterData::goldText(gain) :
                        row == 4 ? validExperience ? "Add EXP: " + characterData::goldText(experience) + " -> " + characterData::goldText(target) : std::string("Add EXP: invalid data") :
                        row == 5 ? std::string("Alignment: ") + ALIGNMENT_LABELS[editAlignment < 3 ? editAlignment : 3] :
                        row == 6 ? std::string("Apply alignment: ") + ALIGNMENT_LABELS[record[0xa] & 3] + " -> " + ALIGNMENT_LABELS[editAlignment < 3 ? editAlignment : 3] :
                        row == 7 ? std::string(fs::exists(editBackupPath()) ? "Restore pre-edit game state" : "Restore pre-edit game state (no backup)") : row == 8 ? std::string(characterData::allSpellsLearned(record) ? "All spells learned" : "Learn all spells") : std::string("Back");
                    sdlCheck(SDL_RenderDebugText(renderer, box.x + 24, y, label.c_str()));
                    if (row == 0) drawCharacterName(box.x + 170, y - 3);
                }
                else if (effectsMenu)
                {
                    const auto label = row < static_cast<int>(EFFECT_LABELS.size()) ? std::string(EFFECT_LABELS[row]) + ((effectMask & (1u << row)) ? ": ON" : ": OFF") : "Back";
                    sdlCheck(SDL_RenderDebugText(renderer, box.x + 24, y, label.c_str()));
                }
                else if (row < 5)
                {
                    auto label = "Slot " + std::to_string(row + 1) + (occupiedSlots[row] ? "  Saved" : "  Empty");
                    sdlCheck(SDL_RenderDebugText(renderer, box.x + 24, y, label.c_str()));
                    sdlCheck(SDL_RenderDebugText(renderer, box.x + 310, y, row == menuRow && !menuLoad ? "> Save <" : "  Save  "));
                    sdlCheck(SDL_RenderDebugText(renderer, box.x + 420, y, row == menuRow && menuLoad ? "> Load <" : "  Load  "));
                }
                else
                {
                    const auto label = row == 5 ? std::string(swapButtons ? "ABXY: Swapped (A/B, X/Y)" : "ABXY: SNES") :
                        row == 6 ? speedText + " (normal BGM / SE)" : row == 7 ? std::string("Always effects >") :
                        row == 8 ? std::string("New character bonus: ") + (maximumBonus ? "Maximum (60)" : "Normal") :
                        row == 9 ? std::string("Skip maker logo: ") + (skipMakerLogo ? "ON" : "OFF") :
                        row == 10 ? std::string("Mixed alignment party: ") + (mixedAlignment ? "ON" : "OFF") :
                        row == 11 ? std::string("CRT display: ") + (crtDisplay ? "ON" : "OFF") :
                        row == 12 ? std::string("Character edit >") : std::string("Resume");
                    sdlCheck(SDL_RenderDebugText(renderer, box.x + 24, y, label.c_str()));
                }
            }
            sdlCheck(SDL_RenderDebugText(renderer, box.x + 20, box.y + box.h - 28, characterMenu ? "Apply saves a separate pre-edit backup." : effectsMenu ? "OFF: spell effects cleared / MP returns to normal limits." : "F2: speed   F5: save slot   F8: load slot"));
        }
        if (mapOpen) drawMap();
        if (!notice.empty() && SDL_GetTicks() < noticeUntil)
        {
            sdlCheck(SDL_SetRenderDrawColor(renderer, 20, 30, 44, 255));
            SDL_FRect messageBox{0, static_cast<float>(windowHeight - 26), static_cast<float>(windowWidth), 26};
            sdlCheck(SDL_RenderFillRect(renderer, &messageBox));
            sdlCheck(SDL_SetRenderDrawColor(renderer, 230, 235, 240, 255));
            sdlCheck(SDL_RenderDebugText(renderer, 12, windowHeight - 18.0f, notice.c_str()));
        }
        if (!uiScreenshot.empty())
        {
            auto* surface = SDL_RenderReadPixels(renderer, nullptr);
            require(surface != nullptr, SDL_GetError());
            const bool saved = SDL_SaveBMP(surface, utf8(uiScreenshot).c_str());
            SDL_DestroySurface(surface);
            sdlCheck(saved);
            uiScreenshot.clear();
        }
        sdlCheck(SDL_RenderPresent(renderer));
    }

    void openPad(SDL_JoystickID id)
    {
        if (gamepads.contains(id) || !SDL_IsGamepad(id)) return;
        if (auto* pad = SDL_OpenGamepad(id))
        {
            gamepads.emplace(id, pad);
            triggerHeld[id] = true;
            moveTriggerHeld[id] = true;
        }
    }
};

// libretroのCコールバックから、単一の実行セッションを参照する。
Player* player = nullptr;

bool environment(unsigned command, void* data)
{
    switch (command)
    {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    {
        auto format = *static_cast<retro_pixel_format*>(data);
        if (format > RETRO_PIXEL_FORMAT_RGB565) return false;
        player->pixelFormat = format;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *static_cast<const char**>(data) = player->directoryUtf8.c_str();
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        for (auto* variable = static_cast<retro_variable*>(data); variable && variable->key; ++variable)
        {
            std::string options = variable->value ? variable->value : "";
            auto start = options.find(';');
            if (start == std::string::npos) continue;
            start = options.find_first_not_of(' ', start + 1);
            if (start == std::string::npos) continue;
            player->variables[variable->key] = options.substr(start, options.find('|', start) - start);
        }
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
        auto* variable = static_cast<retro_variable*>(data);
        auto found = player->variables.find(variable->key);
        variable->value = found == player->variables.end() ? nullptr : found->second.c_str();
        return found != player->variables.end();
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *static_cast<bool*>(data) = false;
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *static_cast<bool*>(data) = true;
        return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *static_cast<unsigned*>(data) = 0;
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    {
        auto* geometry = static_cast<retro_game_geometry*>(data);
        if (geometry->aspect_ratio > 0) player->aspect = geometry->aspect_ratio;
        return true;
    }
    default:
        return false;
    }
}

void video(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (!data) return;
    if (data == RETRO_HW_FRAME_BUFFER_VALID || width == 0 || height == 0 || width > 2048 || height > 2048)
    {
        player->callbackError = "コアから不正な映像を受信しました。";
        return;
    }
    SDL_PixelFormat format = SDL_PIXELFORMAT_XRGB1555;
    size_t bytesPerPixel = 2;
    if (player->pixelFormat == RETRO_PIXEL_FORMAT_RGB565) format = SDL_PIXELFORMAT_RGB565;
    if (player->pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888) { format = SDL_PIXELFORMAT_XRGB8888; bytesPerPixel = 4; }
    if (pitch < width * bytesPerPixel || pitch > 16384)
    {
        player->callbackError = "コアから不正な映像ピッチを受信しました。";
        return;
    }
    if (!player->texture || width != player->width || height != player->height)
    {
        if (player->texture) SDL_DestroyTexture(player->texture);
        player->texture = SDL_CreateTexture(player->renderer, format, SDL_TEXTUREACCESS_STREAMING, static_cast<int>(width), static_cast<int>(height));
        if (!player->texture) { player->callbackError = SDL_GetError(); return; }
        SDL_SetTextureScaleMode(player->texture, SDL_SCALEMODE_NEAREST);
    }
    player->width = width;
    player->height = height;
    player->pitch = pitch;
    player->pixels.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + pitch * height);
    if (!SDL_UpdateTexture(player->texture, nullptr, data, static_cast<int>(pitch))) player->callbackError = SDL_GetError();
    ++player->videoFrames;
}

size_t audioBatch(const int16_t* data, size_t frames)
{
    if(!player->smoke&&player->menuOpen)return frames;
    if (player->recordAudio) player->recordedAudio.insert(player->recordedAudio.end(), data, data + frames * 2);
    if (player->audio)
    {
        if (!SDL_PutAudioStreamData(player->audio, data, static_cast<int>(frames * 2 * sizeof(int16_t))))
            player->callbackError = SDL_GetError();
    }
    player->audioFrames += frames;
    if (player->smoke)
    {
        for (size_t i = 0; i < frames * 2; ++i)
        {
            player->audioHash ^= static_cast<uint16_t>(data[i]);
            player->audioHash *= 1099511628211ull;
            if (data[i]) ++player->nonzeroSamples;
        }
    }
    return frames;
}

void audioSample(int16_t left, int16_t right)
{
    const int16_t samples[]{left, right};
    audioBatch(samples, 1);
}

int16_t inputState(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (player->smoke && player->replayInput && port == 0 && device == RETRO_DEVICE_JOYPAD && index == 0 && id < 16)
        return (player->replayButtons >> id) & 1;
    if (port || device != RETRO_DEVICE_JOYPAD || index || !player->focused || player->paused || player->menuOpen || player->mapOpen || player->inputBlocked) return 0;
    if (player->swapButtons)
    {
        if (id == RETRO_DEVICE_ID_JOYPAD_A) id = RETRO_DEVICE_ID_JOYPAD_B;
        else if (id == RETRO_DEVICE_ID_JOYPAD_B) id = RETRO_DEVICE_ID_JOYPAD_A;
        else if (id == RETRO_DEVICE_ID_JOYPAD_X) id = RETRO_DEVICE_ID_JOYPAD_Y;
        else if (id == RETRO_DEVICE_ID_JOYPAD_Y) id = RETRO_DEVICE_ID_JOYPAD_X;
    }
    return id < 12 ? static_cast<int16_t>((player->gameButtons >> id) & 1) : 0;
}

uint16_t readPhysicalButtons(Player& session)
{
    uint16_t mask = 0;
#ifdef __ANDROID__
    mask = androidTouchMask;
#endif
    const auto* keys = SDL_GetKeyboardState(nullptr);
    struct Binding { unsigned id; SDL_Scancode key; SDL_GamepadButton button; };
    constexpr Binding bindings[]{
        {RETRO_DEVICE_ID_JOYPAD_UP, SDL_SCANCODE_UP, SDL_GAMEPAD_BUTTON_DPAD_UP},
        {RETRO_DEVICE_ID_JOYPAD_DOWN, SDL_SCANCODE_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
        {RETRO_DEVICE_ID_JOYPAD_LEFT, SDL_SCANCODE_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
        {RETRO_DEVICE_ID_JOYPAD_RIGHT, SDL_SCANCODE_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
        {RETRO_DEVICE_ID_JOYPAD_A, SDL_SCANCODE_X, SDL_GAMEPAD_BUTTON_EAST},
        {RETRO_DEVICE_ID_JOYPAD_B, SDL_SCANCODE_Z, SDL_GAMEPAD_BUTTON_SOUTH},
        {RETRO_DEVICE_ID_JOYPAD_X, SDL_SCANCODE_S, SDL_GAMEPAD_BUTTON_NORTH},
        {RETRO_DEVICE_ID_JOYPAD_Y, SDL_SCANCODE_A, SDL_GAMEPAD_BUTTON_WEST},
        {RETRO_DEVICE_ID_JOYPAD_L, SDL_SCANCODE_Q, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
        {RETRO_DEVICE_ID_JOYPAD_R, SDL_SCANCODE_W, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
        {RETRO_DEVICE_ID_JOYPAD_START, SDL_SCANCODE_RETURN, SDL_GAMEPAD_BUTTON_START},
        {RETRO_DEVICE_ID_JOYPAD_SELECT, SDL_SCANCODE_RSHIFT, SDL_GAMEPAD_BUTTON_BACK}
    };
    for (auto binding : bindings)
    {
        const auto bit = static_cast<uint16_t>(1u << binding.id);
        if (keys[binding.key]) mask |= bit;
        for (auto [joystick, pad] : session.gamepads)
        {
            (void)joystick;
            if (SDL_GetGamepadButton(pad, binding.button)) mask |= bit;
            auto x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
            auto y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
            const auto id = binding.id;
            if ((id == RETRO_DEVICE_ID_JOYPAD_LEFT && x < -16000) || (id == RETRO_DEVICE_ID_JOYPAD_RIGHT && x > 16000) ||
                (id == RETRO_DEVICE_ID_JOYPAD_UP && y < -16000) || (id == RETRO_DEVICE_ID_JOYPAD_DOWN && y > 16000)) mask |= bit;
        }
    }
    return mask;
}

void sampleGameInput(Player& session, uint16_t physical, uint64_t nowMs)
{
    if (!session.focused || session.paused || session.menuOpen || session.mapOpen || session.inputBlocked)
    {
        session.gameButtons = 0;
        session.inputRepeat.reset();
        return;
    }
    session.gameButtons = session.inputRepeat.update(physical, nowMs, session.gameSpeed() > 1.0);
}

void loadRom(Player& session, const fs::path& path)
{
    auto rom = readFile(path);
    // SMCの512バイトヘッダーを除き、同じROMの保存先を統一する。
    if (rom.size() % 32768 == 512) rom.erase(rom.begin(), rom.begin() + 512);
    require(rom.size() >= 32768 && rom.size() <= 8 * 1024 * 1024 && rom.size() % 32768 == 0, "SNES ROMのサイズが不正です: " + utf8(path));
    const auto identity = sha256(rom);
    const auto saveFolder = session.dataDirectory / L"Saves";
    auto savePath = saveFolder / (path.stem().wstring() + L".srm");
    const auto identityPath = saveFolder / (path.stem().wstring() + L".rom-id");
    if (fs::exists(identityPath))
    {
        const auto recorded = readFile(identityPath);
        require(std::string(recorded.begin(), recorded.end()) == identity,
            "同名の別ROMの保存データがあります。ROMファイル名を変更してください: " + utf8(savePath));
    }
    session.saveRam();
    if (session.core.set_effects) session.core.set_effects(0);
    if (session.core.loaded) { session.core.unload_game(); session.core.loaded = false; }
    if (session.audio) { SDL_DestroyAudioStream(session.audio); session.audio = nullptr; }
    if (session.texture) { SDL_DestroyTexture(session.texture); session.texture = nullptr; }
    session.pixels.clear();
    session.rom = std::move(rom);
    session.romId = identity;
    session.stateDirectory = session.dataDirectory / L"States";
    auto pathUtf8 = utf8(path);
    retro_game_info game{pathUtf8.c_str(), session.rom.data(), session.rom.size(), nullptr};
    require(session.core.load_game(&game), "ROMを起動できません: " + pathUtf8);
    session.core.loaded = true;
    session.refreshEffects();
    session.applyAudioSpeed(session.speedIndex);
    session.romPath = path;
    session.savePath = savePath;
    session.savedRam.clear();
    session.loadedSavedRam = false;
    const auto ramSize = session.core.get_memory_size(RETRO_MEMORY_SAVE_RAM);
    const auto legacySave = saveFolder / (identity + ".srm");
    if (!fs::exists(savePath) && fs::exists(legacySave))
    {
        const auto oldSave = readFile(legacySave);
        require(oldSave.size() == ramSize, "旧セーブのサイズが不正です: " + utf8(legacySave));
        // Androidでも選択した保存先へROM名のファイルを作成する。
        writeFile(savePath, oldSave.data(), oldSave.size());
    }
    if (fs::exists(savePath))
    {
        auto saved = readFile(savePath);
        require(saved.size() == ramSize && session.core.get_memory_data(RETRO_MEMORY_SAVE_RAM), "セーブサイズが一致しません: " + utf8(savePath));
        std::memcpy(session.core.get_memory_data(RETRO_MEMORY_SAVE_RAM), saved.data(), saved.size());
        session.savedRam = std::move(saved);
        session.loadedSavedRam = true;
    }
    for (int slot = 1; slot <= 5; ++slot)
    {
        const auto named = session.slotPath(slot);
        if (fs::exists(named))
        {
            const auto file = readFile(named);
            if (file.size() >= 72 && std::string_view(reinterpret_cast<const char*>(file.data()), 8) == "WIIZVST1")
                require(std::string_view(reinterpret_cast<const char*>(file.data()) + 8, 64) == identity,
                    "同名の別ROMのステートがあります: " + utf8(named));
        }
        const auto legacy = session.stateDirectory / identity / ("slot" + std::to_string(slot) + ".wvs");
        if (!fs::exists(named) && fs::exists(legacy))
        {
            fs::create_directories(session.stateDirectory);
            const auto file = readFile(legacy);
            writeFile(named, file.data(), file.size());
        }
    }
    if (!fs::exists(identityPath)) writeFile(identityPath, identity.data(), identity.size());
    retro_system_av_info info{};
    session.core.get_system_av_info(&info);
    require(info.timing.fps >= 20 && info.timing.fps <= 120 && info.timing.sample_rate >= 8000 && info.timing.sample_rate <= 192000, "コアの映像・音声設定が不正です。");
    session.fps = info.timing.fps;
    session.aspect = info.geometry.aspect_ratio > 0 ? info.geometry.aspect_ratio : 4.0f / 3.0f;
    SDL_AudioSpec spec{SDL_AUDIO_S16, 2, static_cast<int>(std::round(info.timing.sample_rate))};
    session.audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    require(session.audio != nullptr, std::string("音声を開始できません: ") + SDL_GetError());
    sdlCheck(SDL_ResumeAudioStreamDevice(session.audio));
    session.paused = false;
    session.menuOpen = false;
    session.mapOpen = false;
    if (session.core.cancel_move) session.core.cancel_move();
    if (session.core.cancel_gold) session.core.cancel_gold();
    session.dungeonMaps = {};
    if (session.romId == "afef55c8e8877b4324f6cc2da6aeacc9cb27bb83a8661e35a14ce7d73e5f0456")
        session.dungeonMaps = readDungeonMaps(session.rom);
    session.stateRecoveryFailed = false;
    session.notice.clear();
    session.triggerHeld.clear();
    session.moveTriggerHeld.clear();
    session.inputRepeat.reset();
    session.gameButtons = 0;
    session.inputBlocked = true;
    for (const auto& [id, pad] : session.gamepads) { (void)pad; session.triggerHeld[id] = true; session.moveTriggerHeld[id] = true; }
    session.refreshSlots();
    session.core.set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    session.core.set_controller_port_device(1, RETRO_DEVICE_NONE);
    std::wstring remembered = fs::absolute(path).wstring();
    writeFile(session.dataDirectory / L"last-rom.txt", remembered.data(), remembered.size() * sizeof(wchar_t));
    auto title = "WiizV - " + utf8(path.filename());
    sdlCheck(SDL_SetWindowTitle(session.window, title.c_str()));
}

bool handleAction(Player& session, const SDL_Event& event)
{
    if (session.mapOpen)
    {
        if (!session.focused) return true;
        if (event.type == SDL_EVENT_KEY_DOWN)
        {
            const auto key = event.key.scancode;
            if (key == SDL_SCANCODE_UP) session.moveMapCursor(0, 1);
            if (key == SDL_SCANCODE_DOWN) session.moveMapCursor(0, -1);
            if (key == SDL_SCANCODE_LEFT) session.moveMapCursor(-1, 0);
            if (key == SDL_SCANCODE_RIGHT) session.moveMapCursor(1, 0);
            if (!event.key.repeat)
            {
                if (key == SDL_SCANCODE_Q) session.selectMapFloor(-1);
                if (key == SDL_SCANCODE_W) session.selectMapFloor(1);
                if (key == SDL_SCANCODE_RETURN) session.executeMapMove();
                if (key == SDL_SCANCODE_ESCAPE || key == SDL_SCANCODE_F3) session.closeMap();
            }
        }
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
        {
            const auto button = event.gbutton.button;
            if (button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) session.selectMapFloor(-1);
            if (button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) session.selectMapFloor(1);
            if (button == (session.swapButtons?SDL_GAMEPAD_BUTTON_SOUTH:SDL_GAMEPAD_BUTTON_EAST)) session.executeMapMove();
            if (button == (session.swapButtons?SDL_GAMEPAD_BUTTON_EAST:SDL_GAMEPAD_BUTTON_SOUTH)) session.closeMap();
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
        {
            float x = 0, y = 0;
            SDL_RenderCoordinatesFromWindow(session.renderer, event.button.x, event.button.y, &x, &y);
            int outputWidth = 0, outputHeight = 0;
            renderOutputSize(session.renderer, &outputWidth, &outputHeight);
            const bool overview = x >= outputWidth - 240;
            if ((!overview && (x < 32 || y < 116 || y >= outputHeight - 70)) || (overview && (y < 144 || y >= 344))) return true;
            const auto layout = overview ? session.overviewLayout() : session.mapLayout();
            const int mapX = layout.minX + static_cast<int>(std::floor((x - layout.x) / layout.cell));
            const int mapY = layout.maxY - static_cast<int>(std::floor((y - layout.y) / layout.cell));
            if (findCell(session.dungeonMaps[session.mapFloor - 1], mapX, mapY)) { session.mapX = mapX; session.mapY = mapY; }
        }
        return true;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        const auto key = event.key.scancode;
        if (key == SDL_SCANCODE_F3) { session.openMap(); return true; }
        if (key == SDL_SCANCODE_F1) { session.toggleMenu(); return true; }
        if (key == SDL_SCANCODE_F2) { session.setSpeed((session.speedIndex + 1) % static_cast<int>(GAME_SPEEDS.size())); return true; }
        if (key == SDL_SCANCODE_F5) { session.saveState(session.selectedSlot); return true; }
        if (key == SDL_SCANCODE_F8) { session.loadState(session.selectedSlot); return true; }
        if (key == SDL_SCANCODE_ESCAPE)
        {
            if (session.menuOpen) session.backMenu();
            else session.paused = !session.paused;
            SDL_ClearAudioStream(session.audio);
            return true;
        }
        if (session.menuOpen)
        {
            if (key == SDL_SCANCODE_UP) session.menuRow = (session.menuRow + session.menuRows() - 1) % session.menuRows();
            if (key == SDL_SCANCODE_DOWN) session.menuRow = (session.menuRow + 1) % session.menuRows();
            if (key == SDL_SCANCODE_LEFT) { if (session.characterMenu) session.changeCharacterChoice(-1); else session.menuLoad = false; }
            if (key == SDL_SCANCODE_RIGHT) { if (session.characterMenu) session.changeCharacterChoice(1); else session.menuLoad = true; }
            if (!session.effectsMenu && !session.characterMenu && session.menuRow < 5) session.selectedSlot = session.menuRow + 1;
            if (key == SDL_SCANCODE_RETURN) session.activateMenu();
            return true;
        }
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && session.focused)
    {
        const auto button = event.gbutton.button;
        if (button == SDL_GAMEPAD_BUTTON_LEFT_STICK) { session.toggleMenu(); return true; }
        if (button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) { session.loadState(1); return true; }
        if (session.menuOpen)
        {
            if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) session.menuRow = (session.menuRow + session.menuRows() - 1) % session.menuRows();
            if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) session.menuRow = (session.menuRow + 1) % session.menuRows();
            if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) { if (session.characterMenu) session.changeCharacterChoice(-1); else session.menuLoad = false; }
            if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) { if (session.characterMenu) session.changeCharacterChoice(1); else session.menuLoad = true; }
            if (!session.effectsMenu && !session.characterMenu && session.menuRow < 5) session.selectedSlot = session.menuRow + 1;
            if (button == (session.swapButtons ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST)) session.activateMenu();
            if (button == (session.swapButtons ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH))
            {
                session.backMenu();
            }
            return true;
        }
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
    {
        float x = 0, y = 0;
        sdlCheck(SDL_RenderCoordinatesFromWindow(session.renderer, event.button.x, event.button.y, &x, &y));
        if (session.menuOpen)
        {
            const auto box = session.menuBounds();
            if (x >= box.x + 12 && x < box.x + box.w - 12 && y >= box.y + 77 && y < box.y + 77 + session.menuRows() * session.menuRowHeight())
            {
                session.menuRow = static_cast<int>((y - box.y - 77) / session.menuRowHeight());
                if (!session.effectsMenu && !session.characterMenu && session.menuRow < 5)
                {
                    session.selectedSlot = session.menuRow + 1;
                    if (x < box.x + 300) return true;
                    session.menuLoad = x >= box.x + 410;
                }
                session.activateMenu();
            }
            return true;
        }
        if (y >= 0 && y < 34 && x >= 365 && x < 440) { session.toggleMenu(); return true; }
    }
    return false;
}

void pollExtraControls(Player& session)
{
    bool held = false;
    int mapDirectionX = 0, mapDirectionY = 0;
    const auto* keys = SDL_GetKeyboardState(nullptr);
    constexpr SDL_Scancode gameKeys[]{SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_X, SDL_SCANCODE_Z, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_Q, SDL_SCANCODE_W,
        SDL_SCANCODE_RETURN, SDL_SCANCODE_RSHIFT};
    for (auto key : gameKeys) held |= keys[key];
    for (auto [id, pad] : session.gamepads)
    {
        const auto trigger = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        const auto moveTrigger = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        bool allowExtraAction=session.focused&&!session.mapOpen;
        allowExtraAction=allowExtraAction&&!session.menuOpen;
        bool& moveHeld = session.moveTriggerHeld[id];
        if (moveTrigger < 12000) moveHeld = false;
        if (moveTrigger > 20000 && !moveHeld)
        {
            moveHeld = true;
            if (allowExtraAction)
            {
                try { session.openMap(); }
                catch (const std::exception& error) { session.reportActionError(error); }
            }
        }
        if (session.mapOpen && session.focused)
        {
            const auto axisX = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
            const auto axisY = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
            const int dx = axisX > 16000 || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? 1 :
                axisX < -16000 || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) ? -1 : 0;
            const int dy = axisY > 16000 || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) ? -1 :
                axisY < -16000 || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP) ? 1 : 0;
            if (dx) mapDirectionX = dx;
            if (dy) mapDirectionY = dy;
        }
        bool& wasHeld = session.triggerHeld[id];
        if (trigger < 12000) wasHeld = false;
        if (trigger > 20000 && !wasHeld)
        {
            wasHeld = true;
            if (allowExtraAction)
            {
                try { session.saveState(1); }
                catch (const std::exception& error) { session.reportActionError(error); }
            }
        }
        for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
            held |= SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(button));
        held |= std::abs(static_cast<int>(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX))) > 16000;
        held |= std::abs(static_cast<int>(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY))) > 16000;
        held |= trigger > 12000;
        held |= moveTrigger > 12000;
    }
    if (session.mapOpen && session.focused)
    {
        const auto now = SDL_GetTicks();
        const bool changed = mapDirectionX != session.mapStickX || mapDirectionY != session.mapStickY;
        if ((mapDirectionX || mapDirectionY) && (changed || now >= session.mapRepeatAt))
        {
            if (mapDirectionX) session.moveMapCursor(mapDirectionX, 0);
            else session.moveMapCursor(0, mapDirectionY);
            session.mapRepeatAt = now + (changed ? 300 : 100);
        }
        session.mapStickX = mapDirectionX; session.mapStickY = mapDirectionY;
    }
#ifdef __ANDROID__
    held = held || androidTouchesHeld;
#endif
    if (!held) session.inputBlocked = false;
}

#ifdef __ANDROID__
#include "android_ui.h"
#endif
#include "../tests/state_checks.h"
#include "../tests/speed_checks.h"
#include "../tests/input_checks.h"
#include "../tests/movement_checks.h"
#include "../tests/effects_checks.h"
#include "../tests/mp_checks.h"
#include "../tests/bonus_checks.h"
#include "../tests/logo_checks.h"
#include "../tests/alignment_checks.h"
#include "../tests/crt_checks.h"
#include "../tests/gold_checks.h"
#include "../tests/experience_checks.h"
#include "../tests/character_alignment_checks.h"

int run(const std::vector<std::wstring>& args)
{
    Player session;
    player = &session;
    fs::path requestedRom;
    fs::path screenshot;
    fs::path corePath;
    fs::path replayPath;
    fs::path ramDump;
    fs::path startState;
    int requestedSpeed = -1;
    std::map<unsigned, uint16_t> replay;
    unsigned limit = 600;
    bool checkStates = false;
    bool resumeStates = false;
    bool checkSpeed = false;
    bool checkInput = false;
    bool checkMovement = false;
    bool checkEffects = false;
    bool resumeEffects = false;
    bool checkMp = false;
    bool resumeMp = false;
    bool checkBonus = false, resumeBonus = false;
    bool checkLogo = false, resumeLogo = false;
    bool checkAlignment = false, resumeAlignment = false;
    bool checkCrt = false, resumeCrt = false;
    bool checkGold = false, resumeGold = false;
    bool checkExperience = false, resumeExperience = false;
    bool checkCharacterAlignment = false, resumeCharacterAlignment = false;
    for (size_t i = 1; i < args.size(); ++i)
    {
        if (args[i] == L"--smoke") session.smoke = true;
        else if (args[i] == L"--state-check") checkStates = true;
        else if (args[i] == L"--state-resume-check") resumeStates = true;
        else if (args[i] == L"--speed-check") checkSpeed = true;
        else if (args[i] == L"--input-check") checkInput = true;
        else if (args[i] == L"--movement-check") checkMovement = true;
        else if (args[i] == L"--effects-check") checkEffects = true;
        else if (args[i] == L"--effects-resume-check") resumeEffects = true;
        else if (args[i] == L"--mp-check") checkMp = true;
        else if (args[i] == L"--mp-resume-check") resumeMp = true;
        else if (args[i] == L"--bonus-check") checkBonus = true;
        else if (args[i] == L"--bonus-resume-check") resumeBonus = true;
        else if (args[i] == L"--logo-check") checkLogo = true;
        else if (args[i] == L"--logo-resume-check") resumeLogo = true;
        else if (args[i] == L"--alignment-check") checkAlignment = true;
        else if (args[i] == L"--alignment-resume-check") resumeAlignment = true;
        else if (args[i] == L"--crt-check") checkCrt = true;
        else if (args[i] == L"--crt-resume-check") resumeCrt = true;
        else if (args[i] == L"--gold-check") checkGold = true;
        else if (args[i] == L"--gold-resume-check") resumeGold = true;
        else if (args[i] == L"--experience-check") checkExperience = true;
        else if (args[i] == L"--experience-resume-check") resumeExperience = true;
        else if (args[i] == L"--character-alignment-check") checkCharacterAlignment = true;
        else if (args[i] == L"--character-alignment-resume-check") resumeCharacterAlignment = true;
        else if (args[i] == L"--core" && i + 1 < args.size()) corePath = args[++i];
        else if (args[i] == L"--replay" && i + 1 < args.size()) replayPath = args[++i];
        else if (args[i] == L"--dump-ram" && i + 1 < args.size()) ramDump = args[++i];
        else if (args[i] == L"--start-state" && i + 1 < args.size()) startState = args[++i];
        else if (args[i] == L"--speed" && i + 1 < args.size())
        {
            const auto value = args[++i];
            for (size_t speed = 0; speed < SPEED_VALUES.size(); ++speed)
                if (value == fs::path(SPEED_VALUES[speed]).wstring()) requestedSpeed = static_cast<int>(speed);
            require(requestedSpeed >= 0, "倍率は1/1.5/2/3/4で指定してください。");
        }
        else if (args[i] == L"--data-dir" && i + 1 < args.size()) session.dataDirectory = args[++i];
        else if (args[i] == L"--frames" && i + 1 < args.size()) limit = static_cast<unsigned>(std::stoul(args[++i]));
        else if (args[i] == L"--screenshot" && i + 1 < args.size()) screenshot = args[++i];
        else if (requestedRom.empty()) requestedRom = args[i];
        else throw std::runtime_error("不明な起動引数です。");
    }
    if (session.dataDirectory.empty())
    {
#ifdef _WIN32
        session.dataDirectory = knownDataDirectory(FOLDERID_RoamingAppData);
        migrateDataDirectory(knownDataDirectory(FOLDERID_LocalAppData), session.dataDirectory);
#else
        session.dataDirectory = androidString("dataDirectory");
#endif
    }
    require((!checkStates && !resumeStates && !checkSpeed && !checkInput && !checkMovement && !checkEffects && !resumeEffects && !checkMp && !resumeMp && !checkBonus && !resumeBonus && !checkLogo && !resumeLogo && !checkAlignment && !resumeAlignment && !checkCrt && !resumeCrt && !checkGold && !resumeGold && !checkExperience && !resumeExperience && !checkCharacterAlignment && !resumeCharacterAlignment) || session.smoke, "検証には--smokeが必要です。");
    require((replayPath.empty() && ramDump.empty() && startState.empty()) || session.smoke, "解析用引数には--smokeが必要です。");
    if (!replayPath.empty())
    {
        auto bytes = readFile(replayPath);
        std::istringstream script(std::string(bytes.begin(), bytes.end()));
        std::string line;
        while (std::getline(script, line))
        {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream command(line);
            unsigned frame = 0, buttons = 0;
            require(static_cast<bool>(command >> frame >> buttons) && buttons <= 4095, "リプレイの行が不正です。");
            replay[frame] = static_cast<uint16_t>(buttons);
        }
        session.replayInput = true;
    }
    fs::create_directories(session.dataDirectory / L"Saves");
    const auto settingsPath = session.dataDirectory / L"settings.ini";
    if (fs::exists(settingsPath))
    {
        const auto settings = readFile(settingsPath);
        std::istringstream values(std::string(settings.begin(), settings.end()));
        std::string line;
        while (std::getline(values, line))
        {
            if (line == "swap_abxy=1") session.swapButtons = true;
            if (line == "maximum_bonus=1") session.maximumBonus = true;
            if (line == "skip_maker_logo=1") session.skipMakerLogo = true;
            if (line == "mixed_alignment=1") session.mixedAlignment = true;
            if (line == "crt_display=1") session.crtDisplay = true;
            for (unsigned effect = 0; effect < Player::EFFECT_KEYS.size(); ++effect)
                if (line == std::string(Player::EFFECT_KEYS[effect]) + "=1") session.effectMask |= 1u << effect;
            if (line.size() == 7 && line.starts_with("speed=") && line[6] >= '0' && line[6] <= '3')
                session.speedIndex = line[6] == '3' ? 4 : line[6] - '0';
            for (size_t index = 0; index < SPEED_VALUES.size(); ++index)
                if (line == std::string("speed_value=") + SPEED_VALUES[index]) session.speedIndex = static_cast<int>(index);
        }
    }
    session.directoryUtf8 = utf8(session.dataDirectory);
#ifdef __ANDROID__
    // SDLの待機より先に、中断状態の保存イベントを処理する。
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
#endif
    sdlCheck(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD));
    session.window = SDL_CreateWindow("WiizV", 896, 708, SDL_WINDOW_RESIZABLE);
    require(session.window != nullptr, SDL_GetError());
#ifdef _WIN32
    sdlCheck(SDL_SetWindowMinimumSize(session.window, 704, 480));
    // 実行ファイルの埋め込みアイコンをウィンドウとタスクバーにも使用する。
    const auto nativeWindow = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(session.window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (nativeWindow)
    {
        const auto module = GetModuleHandleW(nullptr);
        const auto largeIcon = LoadImageW(module, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
        const auto smallIcon = LoadImageW(module, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
        if (largeIcon) SendMessageW(nativeWindow, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
        if (smallIcon) SendMessageW(nativeWindow, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
    }
#else
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#endif
    session.renderer = SDL_CreateRenderer(session.window, nullptr);
    require(session.renderer != nullptr, SDL_GetError());
    SDL_SetRenderVSync(session.renderer, 0);
    if (corePath.empty()) corePath = executableDirectory() /
#ifdef _WIN32
        L"wiizv_snes9x.dll";
#else
        "libwiizv_snes9x.so";
#endif
    corePath = fs::absolute(corePath);
    session.core.open(corePath);
    session.coreId = sha256(readFile(corePath));
    if (!session.core.set_audio_speed) session.speedIndex = 0;
    session.core.set_environment(environment);
    session.core.set_video_refresh(video);
    session.core.set_audio_sample(audioSample);
    session.core.set_audio_sample_batch(audioBatch);
    session.core.set_input_poll([] {});
    session.core.set_input_state(inputState);
    session.core.init();
    session.core.initialized = true;
    int count = 0;
    auto* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count; ++i) session.openPad(ids[i]);
    SDL_free(ids);
    const auto romDirectory = executableDirectory() / L"Rom";
#ifdef __ANDROID__
    if (requestedRom.empty()) requestedRom = androidString("romPath");
#endif
    if (requestedRom.empty())
    {
        const auto remembered = session.dataDirectory / L"last-rom.txt";
        if (fs::exists(remembered))
        {
            auto bytes = readFile(remembered);
            if (bytes.size() % sizeof(wchar_t) == 0)
            {
                std::wstring text(bytes.size() / sizeof(wchar_t), L'\0');
                std::memcpy(text.data(), bytes.data(), bytes.size());
                if (fs::is_regular_file(fs::path(text))) requestedRom = text;
            }
        }
        if (requestedRom.empty() && fs::exists(romDirectory))
        {
            std::vector<fs::path> candidates;
            for (const auto& entry : fs::directory_iterator(romDirectory))
            {
                auto extension = entry.path().extension().wstring();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
                if (entry.is_regular_file() && (extension == L".smc" || extension == L".sfc")) candidates.push_back(entry.path());
            }
            if (candidates.size() == 1) requestedRom = candidates.front();
        }
        if (requestedRom.empty()) requestedRom = pickRom(session.window, romDirectory);
    }
    if (requestedRom.empty()) return 0;
    loadRom(session, requestedRom);
#ifdef __ANDROID__
    if (!session.smoke)
    {
        androidCommandEvent = SDL_RegisterEvents(1);
        const auto resume = session.slotPath(1).parent_path() / (session.romPath.stem().string() + ".resume.wvs");
        if (fs::exists(resume))
        {
            try { session.applyStateFile(readFile(resume)); }
            catch (const std::exception&) { androidToast("中断データを読めないため、通常セーブから起動します。"); }
        }
    }
#endif
    if (requestedSpeed >= 0) session.setSpeed(requestedSpeed);
    if (!startState.empty()) session.applyStateFile(readFile(startState));
    bool running = true;
    bool menuAudioPaused = false;
#ifdef __ANDROID__
    bool androidBackground = false;
#endif
    unsigned frames = 0;
    using Clock = std::chrono::steady_clock;
    auto nextFrame = Clock::now();
    auto nextSave = nextFrame + std::chrono::seconds(5);
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
#ifdef __ANDROID__
            try { if (!session.smoke && androidEvent(session, event)) continue; }
            catch (const std::exception& error) { session.reportActionError(error); if (session.menuOpen) androidMenu(session); continue; }
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data)
            {
                try { session.saveRam(); loadRom(session, fs::path(event.drop.data)); }
                catch (const std::exception& error) { session.reportActionError(error); }
            }
            if (event.type == SDL_EVENT_WILL_ENTER_BACKGROUND && !session.smoke)
            {
                androidBackground = true;
                try { androidSuspend(session); }
                catch (const std::exception& error) { androidToast(error.what()); }
            }
            if (event.type == SDL_EVENT_DID_ENTER_FOREGROUND) androidBackground = false;
#endif
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_GAMEPAD_ADDED) session.openPad(event.gdevice.which);
            if (event.type == SDL_EVENT_GAMEPAD_REMOVED)
            {
                auto found = session.gamepads.find(event.gdevice.which);
                if (found != session.gamepads.end()) { SDL_CloseGamepad(found->second); session.gamepads.erase(found); }
                session.triggerHeld.erase(event.gdevice.which);
                session.moveTriggerHeld.erase(event.gdevice.which);
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
            {
                session.focused = false;
#ifdef __ANDROID__
                try { session.saveRam(); } catch(const std::exception& error) { session.reportActionError(error); }
#else
                session.saveRam();
#endif
                SDL_ClearAudioStream(session.audio);
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) { session.focused = true; nextFrame = Clock::now(); }
            try { if (handleAction(session, event)) continue; }
            catch (const std::exception& error) { session.reportActionError(error); continue; }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && event.button.y < 34)
            {
                int action = -1;
                if (event.button.x < 110) action = 0;
                else if (event.button.x < 200) action = 1;
                else if (event.button.x < 290) action = 2;
                else if (event.button.x < 365) action = 3;
                if (action == 0)
                {
                    session.saveRam();
                    SDL_ClearAudioStream(session.audio);
                    auto path = pickRom(session.window, session.romPath.parent_path());
                    if (!path.empty()) loadRom(session, path);
                    nextFrame = Clock::now();
                }
                if (action == 1) { session.saveRam(); session.core.reset(); SDL_ClearAudioStream(session.audio); }
                if (action == 2) { session.paused = !session.paused; SDL_ClearAudioStream(session.audio); }
                if (action == 3)
                {
                    try { session.toggleButtons(); }
                    catch (const std::exception& error) { session.reportActionError(error); }
                }
            }
        }
        if (!running) break;
#ifdef __ANDROID__
        if (androidBackground && !session.smoke) { SDL_Delay(50); continue; }
#endif
        if(!session.smoke&&session.menuOpen!=menuAudioPaused){
            SDL_ClearAudioStream(session.audio);
            if(session.menuOpen)SDL_PauseAudioStreamDevice(session.audio);else SDL_ResumeAudioStreamDevice(session.audio);
            menuAudioPaused=session.menuOpen;
        }
#ifdef __ANDROID__
        if (!session.smoke) androidTick(session);
#endif
        pollExtraControls(session);
        if (!session.stateRecoveryFailed && ((!session.paused && !session.menuOpen && !session.mapOpen && session.focused) || session.smoke))
        {
            if (auto command = replay.find(frames); command != replay.end()) session.replayButtons = command->second;
            sampleGameInput(session, readPhysicalButtons(session), SDL_GetTicks());
            session.runFrame();
            require(session.callbackError.empty(), session.callbackError);
            ++frames;
        }
#ifdef __ANDROID__
        if(session.smoke||!session.menuOpen)session.draw();
#else
        session.draw();
#endif
        try { session.finishRamSave(); }
        catch (const std::exception& error) { session.paused = true; session.reportActionError(error); }
        if (Clock::now() >= nextSave)
        {
            try { session.saveRamAsync(); }
            catch (const std::exception& error) { session.paused = true; session.reportActionError(error); }
            nextSave = Clock::now() + std::chrono::seconds(5);
        }
        if (session.smoke)
        {
            SDL_ClearAudioStream(session.audio);
            if (frames >= limit) running = false;
        }
        else
        {
            nextFrame += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / (session.fps * session.gameSpeed())));
            if (Clock::now() - nextFrame > std::chrono::milliseconds(100)) nextFrame = Clock::now();
            std::this_thread::sleep_until(nextFrame);
        }
    }
    session.saveRam();
#ifdef __ANDROID__
    if (!session.smoke) androidSuspend(session);
#endif
    if (!ramDump.empty())
    {
        auto* memory = session.core.get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        auto size = session.core.get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
        require(memory && size, "RAMを取得できません。");
        writeFile(ramDump, memory, size);
    }
    if (session.smoke)
    {
        require(session.videoFrames > 0 && session.audioFrames > 0 && session.nonzeroSamples > 0 && !session.pixels.empty(), "映像または音声が出力されませんでした。");
        if (!screenshot.empty())
        {
            SDL_PixelFormat format = session.pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888 ? SDL_PIXELFORMAT_XRGB8888 :
                session.pixelFormat == RETRO_PIXEL_FORMAT_RGB565 ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_XRGB1555;
            auto* surface = SDL_CreateSurfaceFrom(static_cast<int>(session.width), static_cast<int>(session.height), format, session.pixels.data(), static_cast<int>(session.pitch));
            require(surface != nullptr, SDL_GetError());
            const bool ok = SDL_SaveBMP(surface, utf8(screenshot).c_str());
            SDL_DestroySurface(surface);
            sdlCheck(ok);
        }
        retro_system_info info{};
        session.core.get_system_info(&info);
        std::ostringstream report;
        report << "core=" << info.library_name << " " << info.library_version << "\nframes=" << frames
            << "\nvideo_frames=" << session.videoFrames << "\naudio_frames=" << session.audioFrames
            << "\naudio_hash=" << session.audioHash << "\nwidth=" << session.width << "\nheight=" << session.height
            << "\nnonzero_audio_samples=" << session.nonzeroSamples
            << "\nsram_loaded=" << session.loadedSavedRam
            << "\ngame_speed=" << session.gameSpeed()
            << "\nsram_bytes=" << session.core.get_memory_size(RETRO_MEMORY_SAVE_RAM) << "\n";
        auto text = report.str();
        writeFile(session.dataDirectory / L"smoke-report.txt", text.data(), text.size());
    }
    if (checkStates) runStateChecks(session);
    if (resumeStates) runStateResumeChecks(session);
    if (checkSpeed) runSpeedChecks(session);
    if (checkInput) runInputChecks(session);
    if (checkMovement) runMovementChecks(session);
    if (checkEffects) runEffectsChecks(session);
    if (resumeEffects) runEffectsResumeChecks(session);
    if (checkMp) runMpChecks(session);
    if (resumeMp) runMpResumeChecks(session);
    if (checkBonus) runBonusChecks(session);
    if (resumeBonus) runBonusResumeChecks(session);
    if (checkLogo) runLogoChecks(session);
    if (resumeLogo) runLogoResumeChecks(session);
    if (checkAlignment) runAlignmentChecks(session);
    if (resumeAlignment) runAlignmentResumeChecks(session);
    if (checkCrt) runCrtChecks(session);
    if (resumeCrt) runCrtResumeChecks(session);
    if (checkGold) runGoldChecks(session);
    if (resumeGold) runGoldResumeChecks(session);
    if (checkExperience) runExperienceChecks(session);
    if (resumeExperience) runExperienceResumeChecks(session);
    if (checkCharacterAlignment) runCharacterAlignmentChecks(session);
    if (resumeCharacterAlignment) runCharacterAlignmentResumeChecks(session);
    return 0;
}

#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int count = 0;
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!argv) return 1;
    std::vector<std::wstring> args(argv, argv + count);
    LocalFree(argv);
    int result = 1;
    try { result = run(args); }
    catch (const std::exception& error)
    {
        if (std::find(args.begin(), args.end(), L"--smoke") == args.end())
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "WiizV", error.what(), nullptr);
        OutputDebugStringA(error.what());
        auto message = std::string(error.what());
        try { writeFile(executableDirectory() / L"last-error.txt", message.data(), message.size()); } catch (...) {}
    }
    player = nullptr;
    SDL_Quit();
    return result;
}
#else
int main(int argc, char** argv)
{
    std::vector<std::wstring> args;
    for (int i = 0; i < argc; ++i) args.push_back(fs::path(argv[i]).wstring());
    try { const int result = run(args); player = nullptr; SDL_Quit(); return result; }
    catch (const std::exception& error)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.what());
        try { androidToast(error.what()); } catch (...) {}
        player = nullptr; SDL_Quit(); return 1;
    }
}
#endif
