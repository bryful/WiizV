#include "../app/data_directory.h"
#include <iostream>
#include <string>

namespace fs = std::filesystem;
void check(bool value) { if (!value) throw std::runtime_error("migration check failed"); }
void put(const fs::path& path, const char* text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}
std::string get(const fs::path& path)
{
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), {}};
}
int wmain(int argc, wchar_t** argv)
{
    check(argc == 2);
    const fs::path root = argv[1];
    const auto old = root / L"旧データ";
    const auto next = root / L"新データ";
    put(old / "settings.ini", "old settings");
    put(next / "settings.ini", "new settings");
    put(old / "last-rom.txt", "rom path");
    put(old / "Saves/game.srm", "save");
    put(old / "States/hash/slot1.wvs", "state");
    put(old / "States/game.before-edit.wvs", "backup");
    put(old / "States/partial.wvs.tmp", "partial");
    migrateDataDirectory(old, next);
    check(get(next / "settings.ini") == "new settings");
    check(get(next / "last-rom.txt") == "rom path");
    check(get(next / "Saves/game.srm") == "save");
    check(get(next / "States/hash/slot1.wvs") == "state");
    check(get(next / "States/game.before-edit.wvs") == "backup");
    check(!fs::exists(next / "States/partial.wvs.tmp"));
    check(get(old / "Saves/game.srm") == "save");
    fs::remove(next / "Saves/game.srm");
    migrateDataDirectory(old, next);
    check(!fs::exists(next / "Saves/game.srm"));
    const auto retry = root / "retry";
    put(retry / "States", "blocked");
    bool failed = false;
    try { migrateDataDirectory(old, retry); } catch (const fs::filesystem_error&) { failed = true; }
    check(failed && !fs::exists(retry / "local-data-migrated.txt"));
    fs::remove(retry / "States");
    migrateDataDirectory(old, retry);
    check(get(retry / "States/hash/slot1.wvs") == "state");
    migrateDataDirectory(root / "absent", root / "fresh");
    check(fs::exists(root / "fresh/local-data-migrated.txt"));
    std::cout << "PASS: migration, conflicts, originals, partial files, one-time import, failure/retry, fresh install\n";
}
