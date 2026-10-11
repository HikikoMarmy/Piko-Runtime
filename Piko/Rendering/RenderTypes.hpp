#pragma once

#include <cstdint>

namespace piko::rendering
{
    struct Color
    {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Rect
    {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    struct VertexColor2D
    {
        Vec2 position{};
        Color color{};
    };

    struct VertexTexture2D
    {
        Vec2 position{};
        Vec2 uv{};
        Color color{};
    };

    struct Texture
    {
        std::uint32_t id = 0;
        std::int32_t width = 0;
        std::int32_t height = 0;

        [[nodiscard]] bool valid() const;
        explicit operator bool() const;
    };
}
