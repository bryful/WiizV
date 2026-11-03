#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

class CrtFilter
{
    SDL_FRect cachedArea{};
    unsigned cachedRows = 0;
    std::array<std::vector<SDL_FRect>, 3> phosphors;
    std::vector<SDL_FRect> scanlines;
    std::array<std::array<SDL_FRect, 4>, 24> edges{};
    std::vector<SDL_FRect> corners;

    void rebuild(const SDL_FRect& area, unsigned rows)
    {
        cachedArea = area;
        cachedRows = rows;
        for (auto& channel : phosphors) channel.clear();
        scanlines.clear();
        corners.clear();
        // RGBマスクは出力画素基準、走査線は元のゲーム画面の行数基準にする。
        for (int x = 0; static_cast<float>(x) < area.w; ++x)
            phosphors[x % 3].push_back({area.x + x, area.y, std::min(1.0f, area.w - x), area.h});
        const float pitch = area.h / static_cast<float>(rows);
        for (unsigned row = 0; row < rows; ++row)
            scanlines.push_back({area.x, area.y + (row + 0.72f) * pitch, area.w, pitch * 0.28f});
        const float horizontal = area.w * 0.06f / edges.size();
        const float vertical = area.h * 0.06f / edges.size();
        for (size_t band = 0; band < edges.size(); ++band)
        {
            const float x = static_cast<float>(band) * horizontal, y = static_cast<float>(band) * vertical;
            edges[band] = {{{area.x + x, area.y, horizontal, area.h},
                            {area.x + area.w - x - horizontal, area.y, horizontal, area.h},
                            {area.x, area.y + y, area.w, vertical},
                            {area.x, area.y + area.h - y - vertical, area.w, vertical}}};
        }
        const float radius = std::clamp(area.h * 0.025f, 6.0f, 18.0f);
        for (float row = 0; row < radius; row += 1)
        {
            const float dy = radius - row - 0.5f;
            const float inset = radius - std::sqrt(std::max(0.0f, radius * radius - dy * dy));
            corners.push_back({area.x, area.y + row, inset, 1});
            corners.push_back({area.x + area.w - inset, area.y + row, inset, 1});
            corners.push_back({area.x, area.y + area.h - row - 1, inset, 1});
            corners.push_back({area.x + area.w - inset, area.y + area.h - row - 1, inset, 1});
        }
    }

public:
    bool draw(SDL_Renderer* renderer, const SDL_FRect& area, unsigned sourceRows)
    {
        if (area.w <= 0 || area.h <= 0 || sourceRows == 0) return true;
        const unsigned rows = sourceRows > 256 ? sourceRows / 2 : sourceRows;
        if (area.x != cachedArea.x || area.y != cachedArea.y || area.w != cachedArea.w ||
            area.h != cachedArea.h || rows != cachedRows) rebuild(area, rows);
        SDL_BlendMode previousBlend{};
        Uint8 r = 0, g = 0, b = 0, a = 0;
        if (!SDL_GetRenderDrawBlendMode(renderer, &previousBlend) || !SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a)) return false;
        bool ok = SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_MOD);
        for (size_t channel = 0; channel < phosphors.size(); ++channel)
        {
            ok = ok && SDL_SetRenderDrawColor(renderer, channel == 0 ? 255 : 242,
                channel == 1 ? 255 : 242, channel == 2 ? 255 : 242, 255);
            ok = ok && SDL_RenderFillRects(renderer, phosphors[channel].data(), static_cast<int>(phosphors[channel].size()));
        }
        ok = ok && SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        ok = ok && SDL_SetRenderDrawColor(renderer, 0, 0, 0, 48);
        ok = ok && SDL_RenderFillRects(renderer, scanlines.data(), static_cast<int>(scanlines.size()));
        for (size_t band = 0; band < edges.size(); ++band)
        {
            const auto opacity = static_cast<Uint8>(64 * (edges.size() - band) / edges.size());
            ok = ok && SDL_SetRenderDrawColor(renderer, 0, 0, 0, opacity);
            ok = ok && SDL_RenderFillRects(renderer, edges[band].data(), 4);
        }
        ok = ok && SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        ok = ok && SDL_RenderFillRects(renderer, corners.data(), static_cast<int>(corners.size()));
        // 後続の機能メニュー・マップ・通知に描画設定を持ち越さない。
        const bool restoredBlend = SDL_SetRenderDrawBlendMode(renderer, previousBlend);
        const bool restoredColor = SDL_SetRenderDrawColor(renderer, r, g, b, a);
        return ok && restoredBlend && restoredColor;
    }
};
