#pragma once
#include <array>
#include <cstdint>

// 倍速中はゲーム内のフレーム数ではなく実時間で長押しを扱う。
// 方向以外は押下時の1回だけを渡し、決定・取消の連続実行を防ぐ。
class InputRepeat
{
public:
    uint16_t update(uint16_t physical, uint64_t nowMs, bool compensate)
    {
        if (!compensate)
        {
            reset();
            return physical;
        }
        uint16_t result = 0;
        for (unsigned id = 0; id < buttons.size(); ++id)
        {
            auto& button = buttons[id];
            const auto mask = static_cast<uint16_t>(1u << id);
            if (!(physical & mask))
            {
                button.held = false;
                if (button.pulseFrames) { result |= mask; --button.pulseFrames; }
                continue;
            }
            if (!button.held)
            {
                button.held = true;
                button.nextRepeat = nowMs + 300;
                button.pulseFrames = 2;
            }
            else if (id >= 4 && id <= 7 && nowMs >= button.nextRepeat)
            {
                button.pulseFrames = 2;
                button.nextRepeat = nowMs + 100;
            }
            // ROM側が次フレームで読む入力も取りこぼさないよう2フレーム保持する。
            if (button.pulseFrames) { result |= mask; --button.pulseFrames; }
        }
        return result;
    }

    void reset() { buttons = {}; }

private:
    struct Button
    {
        bool held = false;
        uint64_t nextRepeat = 0;
        unsigned pulseFrames = 0;
    };
    std::array<Button, 12> buttons{};
};
