#pragma once

#include "Managers/TextureManager.hpp"
#include "Modules/MainMenu/TitleBackgroundRenderer.hpp"
#include "Rendering/Renderer.hpp"
#include "Runtime/Stage/StageTutorialTimeline.hpp"

#include <array>
#include <string>
#include <unordered_map>

namespace piko
{
    class App;
}

namespace piko::modules::stage
{
    class StageTutorialRenderer
    {
    public:
        void load( App &app, bool teaching );
        void unload( App &app );
        void update( std::int64_t sample );
        void begin_completion( App &app );
        void render( App &app, rendering::Renderer &renderer ) const;
        void render_completion( App &app, rendering::Renderer &renderer, bool retry_selected, float time_seconds );
        [[nodiscard]] static rendering::Rect completion_button( const rendering::Renderer &renderer, bool retry );
        [[nodiscard]] static bool completion_button_contains( const rendering::Renderer &renderer,
            bool retry,
            float x,
            float y );

        [[nodiscard]] const runtime::stage::StageTutorialTimeline &timeline() const;

    private:
        static constexpr float reference_width_ = 640.0f;
        static constexpr float reference_height_ = 448.0f;
        static constexpr rendering::Rect again_source_{ 107.0f, 127.0f, 382.0f, 180.0f };
        static constexpr rendering::Rect again_destination_{ 135.0f, 111.0f, 382.0f, 180.0f };
        static constexpr rendering::Rect puma_source_{ 120.0f, 25.0f, 103.0f, 100.0f };
        static constexpr rendering::Rect puma_destination_{ 275.0f, 68.0f, 103.0f, 100.0f };
        static constexpr rendering::Rect button_source_{ 0.0f, 194.0f, 105.0f, 112.0f };
        static constexpr std::array<rendering::Rect, 2> completion_buttons_{
            rendering::Rect{ 151.0f, 301.0f, 105.0f, 112.0f },
            rendering::Rect{ 393.0f, 300.0f, 105.0f, 112.0f }
        };

        static constexpr std::array<rendering::Rect, 2> completion_label_sources_{
            rendering::Rect{ 0.0f, 308.0f, 87.0f, 23.0f },
            rendering::Rect{ 0.0f, 25.0f, 71.0f, 23.0f }
        };

        static constexpr std::array<rendering::Rect, 2> completion_label_destinations_{
            rendering::Rect{ 156.0f, 338.0f, 87.0f, 23.0f },
            rendering::Rect{ 407.0f, 338.0f, 71.0f, 23.0f }
        };

        static constexpr float selected_label_offset_ = 25.0f;
        static constexpr rendering::Color unselected_button_tint_{ 0.58f, 0.70f, 0.85f, 0.85f };
        static constexpr rendering::Rect meter_fill_source_{ 276.0f, 0.0f, 6.0f, 17.0f };
        static constexpr rendering::Rect meter_marker_source_{ 243.0f, 0.0f, 32.0f, 56.0f };
        static constexpr float meter_fill_right_ = 436.0f;
        static constexpr float meter_width_ = 205.0f;
        static constexpr float meter_fill_y_ = 138.0f;
        static constexpr float meter_marker_x_ = 419.0f;
        static constexpr float meter_marker_y_ = 97.0f;
        runtime::stage::StageTutorialTimeline timeline_;
        TitleBackgroundRenderer completion_background_;
        std::unordered_map<std::string, managers::TextureHandle> textures_;
        managers::TextureHandle again_{};
        managers::TextureHandle title_{};
        std::array<managers::TextureHandle, 2> buttons_{};
    };
}
