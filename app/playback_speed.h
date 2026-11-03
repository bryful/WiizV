#pragma once
#include <array>

inline constexpr std::array<double, 5> GAME_SPEEDS{1.0, 1.5, 2.0, 3.0, 4.0};
inline constexpr std::array<unsigned, 5> SPEED_NUMERATORS{1, 3, 2, 3, 4};
inline constexpr std::array<unsigned, 5> SPEED_DENOMINATORS{1, 2, 1, 1, 1};
inline constexpr std::array<const char*, 5> SPEED_LABELS{"1x", "1.5x", "2x", "3x", "4x"};
inline constexpr std::array<const char*, 5> SPEED_VALUES{"1", "1.5", "2", "3", "4"};
