#pragma once

#include "Rendering/RenderUtil.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/TrueTypeText.hpp"

#include <cstdint>

namespace piko::rendering::menu_theme
{
    inline constexpr Color card{ 0.0f, 0.02f, 0.06f, 0.30f };
    inline constexpr Color selected_card{ 0.08f, 0.36f, 0.70f, 0.50f };
    inline constexpr Color active_card{ 0.08f, 0.36f, 0.70f, 0.42f };
    inline constexpr Color focused_card{ 0.10f, 0.43f, 0.82f, 0.58f };
    inline constexpr Color focus_accent{ 0.42f, 0.78f, 1.0f, 0.90f };
    inline constexpr Color disabled_card{ 0.0f, 0.02f, 0.05f, 0.10f };
    inline constexpr Color separator{ 0.86f, 0.94f, 1.0f, 0.34f };
    inline constexpr Color modal{ 0.0f, 0.02f, 0.06f, 0.90f };
    inline constexpr float control_gap = 2.0f;
    inline constexpr float compact_raster_scale = 4.0f;
    inline constexpr std::uint32_t normal_text_rgb = 0xEFBB4Bu;
    inline constexpr std::uint32_t selected_text_rgb = 0xFEF67Au;
    inline constexpr float adjustment_button_width = 30.0f;
    inline constexpr float adjustment_left_margin = 220.0f;
    inline constexpr float adjustment_right_margin = 42.0f;

    inline Rect adjustment_button( const Rect &row, float scale, std::int32_t direction )
    {
        return {
            row.x + row.w - ( direction < 0 ? adjustment_left_margin : adjustment_right_margin ) * scale,
            row.y + 4.0f * scale,
            adjustment_button_width * scale,
            row.h - 8.0f * scale
        };
    }

    inline Rect adjustment_value_rect( const Rect &row, float scale )
    {
        const Rect left = adjustment_button( row, scale, -1 );
        const Rect right = adjustment_button( row, scale, 1 );
        const float padding = 8.0f * scale;
        const float x = left.x + left.w + padding;
        return { x, row.y, right.x - padding - x, row.h };
    }

    inline std::int32_t adjustment_direction( const Rect &row, float scale, float x, float y )
    {
        for( const std::int32_t direction : { -1, 1 } )
        {
            if( point_inside( adjustment_button( row, scale, direction ), x, y ) )
            {
                return direction;
            }
        }

        return 0;
    }

    inline void draw_adjustment_buttons( Renderer &renderer, const Rect &row, float scale, bool selected )
    {
        for( const std::int32_t direction : { -1, 1 } )
        {
            const auto button = adjustment_button( row, scale, direction );
            renderer.draw_filled_rect( button, selected ? active_card : card );
            const float x = button.x + button.w * 0.5f;
            const float y = button.y + button.h * 0.5f;
            const float side = static_cast<float>( direction ) * 4.0f * scale;

            const auto colour = selected ? focus_accent : Color{ 0.76f, 0.86f, 0.96f, 0.85f };
            renderer.draw_line( x - side, y - 6.0f * scale, x + side, y, 2.0f * scale, colour );
            renderer.draw_line( x + side, y, x - side, y + 6.0f * scale, 2.0f * scale, colour );
        }
    }

    inline Color card_fill( bool selected, bool enabled = true )
    {
        return selected ? selected_card : enabled ? card : disabled_card;
    }

    inline StrokedTextStyle compact_text_style( bool selected, std::uint32_t accent_rgb = 0u )
    {
        return {
            color_from_rgb( accent_rgb != 0u ? accent_rgb
                            : selected       ? selected_text_rgb
                                             : normal_text_rgb ),
            color_from_rgb( 0x0D005Au ),
            color_from_rgb( selected ? 0x8DCFFFu : 0x4287DDu ),
            4,
            8,
            1.0f
        };
    }
}
