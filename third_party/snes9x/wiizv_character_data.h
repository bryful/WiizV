#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

namespace characterData
{
constexpr std::uint64_t MAX_GOLD = 999999999999ULL;
constexpr std::uint64_t MAX_EXPERIENCE = 999999999999ULL;

inline bool readDecimal(const std::uint8_t* bytes, unsigned count, std::uint64_t& value)
{
    value = 0;
    for (unsigned byte = 0; byte < count; ++byte)
    {
        const unsigned high = bytes[byte] >> 4, low = bytes[byte] & 15;
        if (high > 9 || low > 9) return false;
        value = value * 100 + high * 10 + low;
    }
    return true;
}

inline void writeDecimal(std::uint8_t* bytes, unsigned count, std::uint64_t value)
{
    for (int byte = static_cast<int>(count) - 1; byte >= 0; --byte)
    {
        const auto pair = value % 100;
        bytes[byte] = static_cast<std::uint8_t>((pair / 10) * 16 + pair % 10);
        value /= 100;
    }
}

inline bool readGold(const std::uint8_t* record, std::uint64_t& gold) { return readDecimal(record + 0x11, 6, gold); }
inline void writeGold(std::uint8_t* record, std::uint64_t gold) { writeDecimal(record + 0x11, 6, gold); }
inline bool readExperience(const std::uint8_t* record, std::uint64_t& experience) { return readDecimal(record + 0x17, 6, experience); }
inline void writeExperience(std::uint8_t* record, std::uint64_t experience) { writeDecimal(record + 0x17, 6, experience); }
inline unsigned level(const std::uint8_t* record) { return record[0x21] | (record[0x22] << 8); }

// 原作00:9DFCの保存チェックサム。変換表は実行中ROMの00:9EB9〜から読む。
inline bool updateChecksum(std::uint8_t* record,const std::uint8_t* rom,std::size_t size)
{
    if(size<0x1f09)return false;
    record[0x7e]=record[0x7f]=0;
    std::uint8_t low=0xff^record[0],high=0xff^record[1];
    for(unsigned offset=2;offset<0x80;++offset){
        const unsigned upper=low>>4;
        const auto nextHigh=high^rom[0x1ec9+upper];
        const auto value=record[offset]^rom[0x1ed9+upper];
        const unsigned index=low^rom[0x1eb9+upper];
        if(index>=16)return false;
        low=static_cast<std::uint8_t>(nextHigh^rom[0x1ee9+index]);
        high=static_cast<std::uint8_t>(value^rom[0x1ef9+index]);
    }
    record[0x7e]=low;record[0x7f]=high;return true;
}

inline bool allSpellsLearned(const std::uint8_t* record)
{
    for(unsigned i=0x35;i<0x43;++i)if(record[i]!=0xff)return false;
    return true;
}

inline bool experienceAtLevel(const std::uint8_t* record, const std::uint8_t* rom, std::size_t romSize,
    unsigned targetLevel, std::uint64_t& target)
{
    if (!record[0] || targetLevel < 1 || romSize < 0x8000) return false;
    if (targetLevel == 1) { target = 0; return true; }
    // 原作00:E2F3の累積経験値計算。表は実行中のROMから読み、配布物には複製しない。
    const unsigned pointer = 0x63c9 + ((record[0xa] >> 2) & 7) * 2;
    const unsigned table = (rom[pointer] | (rom[pointer + 1] << 8)) & 0x7fff;
    if (table > 0x8000 - 0x30) return false;
    const unsigned previousLevel = targetLevel - 1;
    if (previousLevel < 13) return readDecimal(rom + table + (previousLevel - 1) * 3, 3, target);
    std::uint64_t base = 0, increment = 0;
    if (!readDecimal(rom + table + 0x21, 3, base) || !readDecimal(rom + table + 0x2a, 6, increment)) return false;
    target = base + increment * (previousLevel - 12);
    return target <= MAX_EXPERIENCE;
}

inline bool experienceTarget(const std::uint8_t* record, const std::uint8_t* rom, std::size_t romSize,
    unsigned levels, std::uint64_t& target)
{
    std::uint64_t current = 0, base = 0, next = 0;
    if (level(record) < 1 || levels < 1 || levels > 3 || !readExperience(record, current) ||
        !experienceAtLevel(record, rom, romSize, 13, base) ||
        !experienceAtLevel(record, rom, romSize, 14, next) || next <= base) return false;
    if (current >= base)
    {
        const auto increment = next - base;
        // 現在の経験値を超える次の到達点を起点にし、繰り返すたび先へ進める。
        target = base + ((current - base) / increment + levels) * increment;
        if (target > MAX_EXPERIENCE) target = MAX_EXPERIENCE;
        return true;
    }
    for (unsigned targetLevel = 2; targetLevel <= 13; ++targetLevel)
    {
        if (!experienceAtLevel(record, rom, romSize, targetLevel, next)) return false;
        if (next > current) return experienceAtLevel(record, rom, romSize, targetLevel + levels - 1, target);
    }
    return false;
}

inline bool experienceGain(const std::uint8_t* record, const std::uint8_t* rom, std::size_t romSize,
    unsigned levels, std::uint64_t& gain)
{
    std::uint64_t current = 0, target = 0;
    if (!readExperience(record, current) || !experienceTarget(record, rom, romSize, levels, target)) return false;
    gain = target - current;
    return true;
}

inline std::string goldText(std::uint64_t gold)
{
    auto result = std::to_string(gold);
    for (int position = static_cast<int>(result.size()) - 3; position > 0; position -= 3)
        result.insert(static_cast<size_t>(position), ",");
    return result;
}
}
