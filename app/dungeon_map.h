#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

struct DungeonCell
{
    int x, y;
    uint8_t walls, flags, event;
};

using DungeonFloor = std::vector<DungeonCell>;

inline const DungeonCell* findCell(const DungeonFloor& floor, int x, int y)
{
    const auto found = std::find_if(floor.begin(), floor.end(), [=](const auto& cell) { return cell.x == x && cell.y == y; });
    return found == floor.end() ? nullptr : &*found;
}

// ROMの可変長LZWを展開する。上限と辞書参照を検証して破損データを拒否する。
inline std::vector<uint8_t> decodeDungeonData(std::span<const uint8_t> rom, size_t offset, size_t expected)
{
    size_t bit = 0;
    auto read = [&](unsigned count)
    {
        unsigned value = 0;
        for (unsigned i = 0; i < count; ++i, ++bit)
        {
            if (offset + bit / 8 >= rom.size()) throw std::runtime_error("マップの圧縮データが途中で切れています。");
            value = (value << 1) | ((rom[offset + bit / 8] >> (7 - bit % 8)) & 1);
        }
        return value;
    };
    std::vector<std::vector<uint8_t>> dictionary(258);
    for (unsigned i = 0; i < 256; ++i) dictionary[i].push_back(static_cast<uint8_t>(i));
    unsigned width = 9;
    unsigned previous = read(width);
    if (previous >= 256) throw std::runtime_error("マップ辞書の先頭が不正です。");
    auto output = dictionary[previous];
    for (unsigned operations = 0; operations < 10000; ++operations)
    {
        const unsigned code = read(width);
        if (code == 256)
        {
            if (output.size() != expected) throw std::runtime_error("マップの展開サイズが不正です。");
            return output;
        }
        if (code == 257)
        {
            if (++width > 12) throw std::runtime_error("マップの辞書幅が不正です。");
            continue;
        }
        if (code > dictionary.size() || code == 256 || code == 257) throw std::runtime_error("マップの辞書参照が不正です。");
        auto entry = code < dictionary.size() ? dictionary[code] : dictionary[previous];
        if (code == dictionary.size()) entry.push_back(entry.front());
        if (entry.empty() || output.size() + entry.size() > expected) throw std::runtime_error("マップの展開サイズを超えました。");
        output.insert(output.end(), entry.begin(), entry.end());
        if (dictionary.size() < 4096)
        {
            auto next = dictionary[previous];
            next.push_back(entry.front());
            dictionary.push_back(std::move(next));
        }
        previous = code;
    }
    throw std::runtime_error("マップの圧縮データが終了しません。");
}

inline std::array<DungeonFloor, 8> readDungeonMaps(std::span<const uint8_t> rom)
{
    if (rom.size() != 1024 * 1024) throw std::runtime_error("このROMのマップ形式には対応していません。");
    auto readSection = [&](size_t table, size_t floor, size_t size)
    {
        const size_t entry = 6 * 0x8000 + (table & 0x7fff) + floor * 2;
        const unsigned pointer = rom[entry] | (rom[entry + 1] << 8);
        if (pointer < 0x8000) throw std::runtime_error("マップのアドレスが不正です。");
        return decodeDungeonData(rom, 6 * 0x8000 + (pointer & 0x7fff), size);
    };
    std::array<DungeonFloor, 8> floors;
    for (size_t floor = 0; floor < floors.size(); ++floor)
    {
        const auto map = readSection(0x892e, floor, 1088);
        const auto flags = readSection(0xa0c0, floor, 1024);
        const auto events = readSection(0xb33e, floor, 1024);
        for (size_t block = 0; block < 16; ++block)
        {
            const int originX = map[block * 4] - 0x81;
            const int originY = map[block * 4 + 1] - 0x81;
            const size_t base = map[block * 4 + 2] | (map[block * 4 + 3] << 8);
            if (base + 64 > 1024) throw std::runtime_error("マップ区画の範囲が不正です。");
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                {
                    const size_t index = base + (7 - y) * 8 + x;
                    // ゲームの範囲外判定と同じ岩盤フラグ。イベント番号や壁は移動制限に使わない。
                    if (flags[index] & 0x20) continue;
                    floors[floor].push_back({originX + x, originY + y, map[64 + index], flags[index], events[index]});
                }
        }
        if (floors[floor].empty()) throw std::runtime_error("階層に移動できるマスがありません。");
    }
    return floors;
}
