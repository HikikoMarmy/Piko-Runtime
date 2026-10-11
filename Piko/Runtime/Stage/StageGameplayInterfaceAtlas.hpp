#pragma once

#include "Rendering/RenderTypes.hpp"

#include <array>

namespace piko::runtime::stage::gameplay_interface_atlas
{
    inline constexpr rendering::Rect energy_housing_left_cap{ 0.0f, 0.0f, 33.0f, 40.0f };
    inline constexpr rendering::Rect energy_housing_middle{ 34.0f, 0.0f, 1.0f, 40.0f };
    inline constexpr rendering::Rect energy_housing_right_cap{ 38.0f, 0.0f, 39.0f, 40.0f };
    inline constexpr rendering::Rect energy_fill_left_cap{ 275.0f, 0.0f, 2.0f, 17.0f };
    inline constexpr rendering::Rect energy_fill_middle{ 277.0f, 0.0f, 1.0f, 17.0f };
    inline constexpr rendering::Rect energy_fill_right_cap{ 279.0f, 0.0f, 2.0f, 17.0f };
    inline constexpr rendering::Rect energy_fill = energy_fill_middle;
    inline constexpr rendering::Rect energy_marker{ 243.0f, 0.0f, 32.0f, 56.0f };
    inline constexpr rendering::Rect energy_flash{ 233.0f, 70.0f, 59.0f, 61.0f };
    inline constexpr rendering::Rect energy_warning{ 77.0f, 0.0f, 166.0f, 40.0f };

    inline constexpr rendering::Rect phase_background{ 396.0f, 247.0f, 116.0f, 84.0f };
    inline constexpr rendering::Rect phase_puma{ 168.0f, 185.0f, 68.0f, 62.0f };
    inline constexpr rendering::Rect portable_phase_background{ 78.0f, 186.0f, 116.0f, 80.0f };
    inline constexpr rendering::Rect portable_phase_puma{ 0.0f, 187.0f, 68.0f, 66.0f };

    inline constexpr rendering::Rect judgement_great{ 295.0f, 23.0f, 96.0f, 28.0f };
    inline constexpr rendering::Rect judgement_good{ 295.0f, 52.0f, 96.0f, 28.0f };
    inline constexpr rendering::Rect judgement_miss{ 295.0f, 81.0f, 96.0f, 28.0f };
    inline constexpr rendering::Rect judgement_ok{ 295.0f, 110.0f, 96.0f, 28.0f };

    inline constexpr rendering::Rect player_icon{ 1.0f, 79.0f, 72.0f, 50.0f };
    inline constexpr rendering::Rect player_name{ 1.0f, 305.0f, 243.0f, 38.0f };
    inline constexpr rendering::Rect opponent_icon{ 73.0f, 78.0f, 72.0f, 50.0f };
    inline constexpr rendering::Rect opponent_name{ 1.0f, 344.0f, 243.0f, 38.0f };
    inline constexpr rendering::Rect portable_player_name{ 0.0f, 275.0f, 73.0f, 34.0f };
    inline constexpr rendering::Rect portable_kai_kakoon_name{ 68.0f, 397.0f, 172.0f, 29.0f };
    inline constexpr rendering::Rect portable_ming_ming_name{ 96.0f, 398.0f, 147.0f, 35.0f };

    inline constexpr rendering::Rect trace_line_body{ 374.0f, 142.0f, 6.0f, 40.0f };

    inline constexpr rendering::Rect phrase_tube_body{ 181.0f, 78.0f, 24.0f, 28.0f };
    inline constexpr rendering::Rect phrase_tube_cap{ 207.0f, 78.0f, 15.0f, 28.0f };

    inline constexpr rendering::Rect note_begin_icon{ 295.0f, 151.0f, 31.0f, 31.0f };
    inline constexpr rendering::Rect center_icon{ 146.0f, 78.0f, 19.0f, 19.0f };
    inline constexpr float center_icon_pressed_diameter = 24.0f;
    inline constexpr rendering::Color center_icon_idle_tint{ 1.0f, 0.5f, 1.0f, 1.0f };
    inline constexpr rendering::Color center_icon_pressed_tint{ 1.0f, 0.0f, 1.0f, 1.0f };

    inline constexpr rendering::Rect aim_direction_arrow{ 147.0f, 98.0f, 28.0f, 19.0f };
    inline constexpr rendering::Rect wing_arrow{ 166.0f, 78.0f, 14.0f, 13.0f };

    inline constexpr rendering::Rect note_flash{ 78.0f, 182.0f, 64.0f, 64.0f };
    inline constexpr rendering::Rect portable_note_flash{ 1.0f, 129.0f, 62.0f, 62.0f };

    inline constexpr rendering::Rect guard_triangle{ 80.0f, 130.0f, 50.0f, 50.0f };
    inline constexpr rendering::Rect guard_circle{ 134.0f, 130.0f, 50.0f, 50.0f };
    inline constexpr rendering::Rect guard_cross{ 188.0f, 130.0f, 50.0f, 50.0f };
    inline constexpr rendering::Rect guard_square{ 242.0f, 130.0f, 50.0f, 50.0f };

    inline constexpr std::array<rendering::Rect, 4> guard_button_icons{
        {
            guard_square,
            guard_cross,
            guard_circle,
            guard_triangle,
        }
    };
}
