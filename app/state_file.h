#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

std::string sha256(const std::vector<uint8_t>& data);

// 固定長ヘッダー。数値はリトルエンディアン、ハッシュはASCIIの16進表記。
inline std::vector<uint8_t> encodeState(const std::vector<uint8_t>& payload, std::string_view romId, std::string_view coreId)
{
    if (romId.size() != 64 || coreId.size() != 64 || payload.empty() || payload.size() > 31 * 1024 * 1024)
        throw std::runtime_error("ステートの保存情報が不正です。");
    std::string header = "WIIZVST1";
    header += romId;
    header += coreId;
    header += sha256(payload);
    std::vector<uint8_t> result(header.begin(), header.end());
    for (unsigned i = 0; i < 8; ++i) result.push_back(static_cast<uint8_t>((static_cast<uint64_t>(payload.size()) >> (i * 8)) & 255));
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

inline std::vector<uint8_t> decodeState(const std::vector<uint8_t>& data, std::string_view romId, std::string_view coreId, size_t expectedSize)
{
    constexpr size_t HEADER_SIZE = 208;
    if (data.size() < HEADER_SIZE) throw std::runtime_error("ステートファイルが途中で切れています。");
    std::string_view header(reinterpret_cast<const char*>(data.data()), 200);
    if (header.substr(0, 8) != "WIIZVST1") throw std::runtime_error("未対応のステート形式です。");
    if (header.substr(8, 64) != romId) throw std::runtime_error("別のROMのステートです。");
    if (header.substr(72, 64) != coreId) throw std::runtime_error("別バージョンのコアのステートです。");
    uint64_t size = 0;
    for (unsigned i = 0; i < 8; ++i) size |= static_cast<uint64_t>(data[200 + i]) << (i * 8);
    if (size != data.size() - HEADER_SIZE || size != expectedSize || size == 0)
        throw std::runtime_error("ステートのデータサイズが一致しません。");
    std::vector<uint8_t> payload(data.begin() + HEADER_SIZE, data.end());
    if (sha256(payload) != header.substr(136, 64)) throw std::runtime_error("ステートの破損を検出しました。");
    return payload;
}
