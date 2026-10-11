#pragma once

#include "Rendering/RenderTypes.hpp"

#include <cstdint>

namespace piko::rendering
{
    class Renderer;

    struct LogicalViewport
    {
        float scale = 1.0f;
        float x = 0.0f;
        float y = 0.0f;
        float logical_width = 640.0f;
        float logical_height = 448.0f;

        [[nodiscard]] float to_x( float logical_x ) const;
        [[nodiscard]] float to_y( float logical_y ) const;
        [[nodiscard]] Rect rect( float x, float y, float width, float height ) const;
        [[nodiscard]] Rect rect( const Rect &value ) const;
    };

    [[nodiscard]] LogicalViewport make_logical_viewport( std::int32_t output_width,
        std::int32_t output_height,
        float logical_width = 640.0f,
        float logical_height = 448.0f );

    [[nodiscard]] LogicalViewport make_logical_viewport( const Renderer &renderer,
        float logical_width = 640.0f,
        float logical_height = 448.0f );

    Color color_from_rgb( std::uint32_t rgb, float alpha = 1.0f );
    Color scale_color_rgb( Color color, float scale );
    Color scale_color_alpha( Color color, float scale );
    Color lerp_color( Color a, Color b, float t );
    float distance_between( const Vec2 &a, const Vec2 &b );
    Vec2 lerp_point( const Vec2 &a, const Vec2 &b, float t );
    bool point_inside( const Rect &rect, float x, float y );
    bool uv_requires_texture_repeat( float u, float v );
}
