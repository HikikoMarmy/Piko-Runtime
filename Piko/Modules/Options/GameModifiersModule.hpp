#pragma once

#include "Utils/EnumUtil.hpp"
#include "Managers/TextureManager.hpp"
#include "Modules/PikoModule.hpp"
#include "Rendering/TrueTypeText.hpp"

#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <span>
#include <unordered_map>

namespace piko::modules
{
    class OptionsPresentation;

    class GameModifiersModule final : public PikoModule
    {
    public:
        explicit GameModifiersModule( std::shared_ptr<OptionsPresentation> presentation );
        void enter( App &app ) override;
        void leave( App &app ) override;
        void handle_event( App &app, const SDL_Event &event ) override;
        void update( App &app, double delta_seconds ) override;
        void render( App &app, rendering::Renderer &renderer ) override;

    private:
        enum class Row : std::int32_t
        {
            NoDeath,
            AutoPress,
            AutoAim,
            AllSections,
            Mirror,
            PlayerDamage,
            EnemyDamage,
            NoteSpeed,
            SnapAngles,
            SnapEasy,
            SnapNormal,
            SnapMaster,
            SnapPspEasy,
            SnapPspNormal,
            SnapPspMaster,
            ResetSnapAngles,
            Back,
            Count
        };

        static constexpr float reference_width = 1280.0f;
        static constexpr float reference_height = 720.0f;
        static constexpr float row_height = 48.0f;
        static constexpr float row_gap = 2.0f;
        static constexpr float label_text_size = 22.0f;
        static constexpr float value_text_size = 21.0f;

        struct TextPair
        {
            managers::TextureHandle normal{};
            managers::TextureHandle selected{};
        };

        static constexpr std::int32_t row_count = utils::to_int( Row::Count );
        static constexpr std::int32_t damage_step_count = 17;
        static constexpr float minimum_snap_angle = 5.0f;
        static constexpr float snap_angle_step = 0.5f;
        static constexpr std::int32_t snap_step_count = 151;
        static constexpr std::array<Row, 10> main_rows{
            Row::NoDeath,
            Row::AutoPress,
            Row::AutoAim,
            Row::AllSections,
            Row::Mirror,
            Row::PlayerDamage,
            Row::EnemyDamage,
            Row::NoteSpeed,
            Row::SnapAngles,
            Row::Back
        };

        static constexpr std::array<Row, 8> snap_rows{
            Row::SnapEasy,
            Row::SnapNormal,
            Row::SnapMaster,
            Row::SnapPspEasy,
            Row::SnapPspNormal,
            Row::SnapPspMaster,
            Row::ResetSnapAngles,
            Row::Back,
        };

        [[nodiscard]] std::span<const Row> active_rows() const;
        float *row_snap_setting( App &app, Row row );
        void show_snap_settings( App &app, bool show );
        void prepare_snap_value( App &app, float value );
        void prepare_note_speed_value( App &app );
        static rendering::Rect reference_row_rect( std::int32_t index );
        static const char *row_name( Row row );
        bool *row_setting( App &app, Row row );
        float *row_damage_setting( App &app, Row row );
        void move_selection( App &app, std::int32_t direction );
        void activate( App &app, std::int32_t direction = 1 );
        void create_textures( App &app );
        void prepare_damage_value( App &app, std::int32_t index );
        void unload_textures( App &app );
        managers::TextureHandle create_text( App &app,
            const std::string &name,
            const std::string &value,
            float size,
            bool selected,
            bool section = false,
            bool heading = false );

        void draw_text( App &app,
            rendering::Renderer &renderer,
            managers::TextureHandle handle,
            float x,
            float center_y,
            float scale,
            bool right_aligned = false,
            bool centered = false,
            float maximum_width = 0.0f ) const;

        std::shared_ptr<OptionsPresentation> presentation_{};
        rendering::TrueTypeFont font_{};
        managers::TextureHandle heading_{};
        managers::TextureHandle section_heading_{};
        std::array<TextPair, row_count> labels_{};
        TextPair on_{};
        TextPair off_{};
        std::array<TextPair, damage_step_count> damage_values_{};
        std::array<TextPair, snap_step_count> snap_values_{};
        std::unordered_map<std::int32_t, TextPair> note_speed_values_{};
        bool snap_settings_open_ = false;
        Row selected_row_ = Row::NoDeath;
        double elapsed_seconds_ = 0.0;
        std::int32_t next_damage_preload_ = 0;
    };
}
