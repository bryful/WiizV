#pragma once
#include <filesystem>
#include <fstream>
#include <stdexcept>

inline void migrateDataDirectory(const std::filesystem::path& oldDirectory,
    const std::filesystem::path& newDirectory)
{
    namespace fs = std::filesystem;
    const auto marker = newDirectory / L"local-data-migrated.txt";
    if (fs::exists(marker)) return;
    if (fs::is_symlink(oldDirectory)) throw std::runtime_error("旧保存先がリンクです。保存データを確認してください。");
    fs::create_directories(newDirectory);
    const auto copyMissing = [](const fs::path& source, const fs::path& destination)
    {
        if (fs::exists(destination)) return;
        fs::create_directories(destination.parent_path());
        auto temporary = destination;
        temporary += L".migration.tmp";
        fs::copy_file(source, temporary, fs::copy_options::overwrite_existing);
        // 完成したファイルだけを公開し、既存の保存データは置換しない。
        fs::rename(temporary, destination);
    };
    for (const auto* name : {L"settings.ini", L"last-rom.txt"})
    {
        const auto source = oldDirectory / name;
        if (fs::is_symlink(source)) throw std::runtime_error("旧保存先にリンクがあります。保存データを確認してください。");
        if (fs::is_regular_file(source)) copyMissing(source, newDirectory / name);
    }
    for (const auto* name : {L"Saves", L"States"})
    {
        const auto source = oldDirectory / name;
        if (fs::is_symlink(source)) throw std::runtime_error("旧保存先にリンクがあります。保存データを確認してください。");
        if (!fs::is_directory(source)) continue;
        for (const auto& entry : fs::recursive_directory_iterator(source))
        {
            if (entry.is_symlink()) throw std::runtime_error("旧保存先にリンクがあります。保存データを確認してください。");
            if (entry.is_regular_file() && entry.path().extension() != L".tmp")
                copyMissing(entry.path(), newDirectory / name / entry.path().lexically_relative(source));
        }
    }
    // 次回以降、旧フォルダの古いデータを再取り込みしない。
    auto temporaryMarker = marker;
    temporaryMarker += L".tmp";
    std::ofstream file(temporaryMarker, std::ios::binary);
    file << "LocalAppData migration completed. Original files retained.\n";
    file.close();
    if (!file) throw std::runtime_error("保存データの移行記録を書き込めません。");
    fs::rename(temporaryMarker, marker);
}
