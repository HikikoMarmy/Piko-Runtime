#include "Rendering/RenderUtil.hpp"

#include <cstdint>
#include <algorithm>
#include <cmath>

namespace piko::rendering
{
    float LogicalViewport::to_x( float logical_x ) const
    {
        return x + logical_x * scale;
    }

    float LogicalViewport::to_y( float logical_y ) const
    {
        return y + logical_y * scale;
    }

    Rect LogicalViewport::rect( float logical_x, float logical_y, float width, float height ) const
    {
        return Rect{ to_x( logical_x ), to_y( logical_y ), width * scale, height * scale };
    }

    Rect LogicalViewport::rect( const Rect &value ) const
    {
        return rect( value.x, value.y, value.w, value.h );
    }

    LogicalViewport make_logical_viewport( std::int32_t output_width,
        std::int32_t output_height,
        float logical_width,
        float logical_height )
    {
        const float width = static_cast<float>( std::max( output_width, 1 ) );
        const float height = static_cast<float>( std::max( output_height, 1 ) );
        const float safe_logical_width = std::max( logical_width, 1.0f );
        const float safe_logical_height = std::max( logical_height, 1.0f );
        const float scale = std::min( width / safe_logical_width, height / safe_logical_height );
        return LogicalViewport{
            scale,
            ( width - safe_logical_width * scale ) * 0.5f,
            ( height - safe_logical_height * scale ) * 0.5f,
            safe_logical_width,
            safe_logical_height
        };
    }

    Color color_from_rgb( std::uint32_t rgb, float alpha )
    {
        return Color{
            static_cast<float>( ( rgb >> 16u ) & 0xFFu ) / 255.0f,
            static_cast<float>( ( rgb >> 8u ) & 0xFFu ) / 255.0f,
            static_cast<float>( rgb & 0xFFu ) / 255.0f,
            alpha
        };
    }

    Color scale_color_rgb( Color color, float scale )
    {
        color.r = std::clamp( color.r * scale, 0.0f, 1.0f );

        color.g = std::clamp( color.g * scale, 0.0f, 1.0f );

        color.b = std::clamp( color.b * scale, 0.0f, 1.0f );
        return color;
    }

    Color scale_color_alpha( Color color, float scale )
    {
        color.a = std::clamp( color.a * scale, 0.0f, 1.0f );
        return color;
    }

    Color lerp_color( Color a, Color b, float t )
    {
        t = std::clamp( t, 0.0f, 1.0f );
        return Color{
            std::lerp( a.r, b.r, t ),
            std::lerp( a.g, b.g, t ),
            std::lerp( a.b, b.b, t ),
            std::lerp( a.a, b.a, t )
        };
    }

    float distance_between( const Vec2 &a, const Vec2 &b )
    {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        return std::sqrt( dx * dx + dy * dy );
    }

    Vec2 lerp_point( const Vec2 &a, const Vec2 &b, float t )
    {
        return Vec2{ std::lerp( a.x, b.x, t ), std::lerp( a.y, b.y, t ) };
    }

    bool uv_requires_texture_repeat( float u, float v )
    {
        constexpr float epsilon = 0.0001f;
        return u < -epsilon || u > 1.0f + epsilon || v < -epsilon || v > 1.0f + epsilon;
    }

    bool point_inside( const Rect &rect, float x, float y )
    {
        return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
    }
}
