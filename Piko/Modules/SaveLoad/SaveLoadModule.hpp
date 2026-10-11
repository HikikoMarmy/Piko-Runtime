#pragma once

#include "Managers/AudioManager.hpp"
#include "Managers/TextureManager.hpp"
#include "Modules/PikoModule.hpp"
#include "Rendering/RenderUtil.hpp"
#include "Rendering/TrueTypeText.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace piko::gamedata
{
    enum class SaveProgressMode;
    enum class ChartStyle;
    struct SaveProfile;
}

namespace piko::modules
{
    class OptionsPresentation;

    class SaveLoadModule final : public PikoModule
    {
    public:
        explicit SaveLoadModule( std::shared_ptr<OptionsPresentation> presentation = {} );
        void enter( App &app ) override;
        void leave( App &app ) override;
        void handle_event( App &app, const SDL_Event &event ) override;
        void update( App &app, double delta_seconds ) override;
        void render( App &app, rendering::Renderer &renderer ) override;

    private:
        enum class TextAlignment
        {
            Left,
            Center,
            Right
        };

        void move_selection( App &app, std::int32_t delta );
        void confirm_selection( App &app );
        void complete_selection( App &app );
        void unload_visuals( App &app );
        void change_stage_page( App &app, std::int32_t delta );
        void draw_label( App &app,
            rendering::Renderer &renderer,
            const rendering::LogicalViewport &layout,
            const rendering::Rect &bounds,
            const std::string &label,
            float size,
            bool selected = false,
            TextAlignment alignment = TextAlignment::Left,
            std::uint32_t accent_rgb = 0u );

        [[nodiscard]] static std::int32_t profile_clear_count( const gamedata::SaveProfile &profile,
            gamedata::SaveProgressMode mode,
            gamedata::ChartStyle chart_style );

        [[nodiscard]] static std::uint64_t profile_total_score( const gamedata::SaveProfile &profile,
            gamedata::SaveProgressMode mode,
            gamedata::ChartStyle chart_style );

        [[nodiscard]] static const char *rank_label( std::int32_t rank );
        [[nodiscard]] static std::uint32_t rank_color( std::int32_t rank );

        struct StageRow
        {
            std::int32_t number = 0;
            std::string name;
            bool easy_available = false;
            bool normal_available = false;
            bool master_available = false;
        };

        std::array<rendering::Rect, 10> slot_rects_{};
        rendering::Rect previous_page_rect_{};
        rendering::Rect next_page_rect_{};
        rendering::Rect load_rect_{};
        rendering::Rect cancel_rect_{};
        rendering::TrueTypeFont font_{};
        std::unordered_map<std::string, managers::TextureHandle> text_textures_{};
        std::vector<StageRow> stage_rows_{};
        std::shared_ptr<OptionsPresentation> presentation_{};
        std::int32_t selected_slot_ = 0;
        std::int32_t confirmation_choice_ = 0;
        std::int32_t stage_page_ = 0;
        std::int32_t easy_stage_total_ = 0;
        std::int32_t normal_stage_total_ = 0;
        std::int32_t master_stage_total_ = 0;
        bool leaving_ = false;
        bool confirmation_open_ = false;
    };
}
