#pragma once

#include "FileTypes/CHC/CHC.hpp"
#include "GameData/StageCatalog.hpp"
#include "Managers/AudioManager.hpp"
#include "Managers/TextureManager.hpp"
#include "Modules/PikoModule.hpp"
#include "Runtime/Stage/ResultAtlas.hpp"
#include "Runtime/Stage/StageResultData.hpp"
#include "Rendering/TrueTypeText.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace piko::modules
{
    struct ResultLoadData
    {
        gamedata::PlayableStageEntry stage{};
        filetypes::chc::Song song{};
        bool chc_loaded = false;
        runtime::stage::StageResultData result{};
        std::vector<std::filesystem::path> audio_bank_candidates{};
    };

    class ResultModule final : public PikoModule
    {
    public:
        explicit ResultModule( ResultLoadData load_data );

        void enter( App &app ) override;
        void leave( App &app ) override;
        void update( App &app, double delta_seconds ) override;
        void render( App &app, rendering::Renderer &renderer ) override;

    private:
        enum class RevealState
        {
            Opening,
            StagePanel,
            StatsIntro,
            GreatTally,
            GoodTally,
            OkTally,
            MissTally,
            EnergyTally,
            Rank,
            NoMiss,
            Ready,
            Exiting
        };

        void load_textures( App &app );
        void unload_textures( App &app );
        void calculate_result();
        void initialize_audio( App &app );
        void shutdown_audio( App &app );
        void play_result_bp( App &app );
        void play_result_fx( App &app );
        void advance_tick( App &app );
        [[nodiscard]] bool reveal_advance_pressed( const App &app ) const;
        [[nodiscard]] std::uint32_t displayed_stat_count( std::size_t index ) const;
        void skip_reveal( App &app );
        void begin_exit( App &app );
        void advance( App &app );
        void load_notices( App &app );
        void unload_notices( App &app );
        [[nodiscard]] managers::TextureHandle notice_text( App &app,
            const std::string &key,
            const std::string &label,
            float size,
            const rendering::Color &color );

        void render_notice( App &app, rendering::Renderer &renderer ) const;
        void render_portable_duet( App &app, rendering::Renderer &renderer ) const;

        void render_background_layers( rendering::Renderer &renderer ) const;
        void render_common_base( App &app, rendering::Renderer &renderer ) const;
        void render_stage_panel( App &app, rendering::Renderer &renderer ) const;
        void render_energy_gauge( App &app, rendering::Renderer &renderer ) const;
        void render_score( App &app, rendering::Renderer &renderer ) const;
        void render_statistics( App &app, rendering::Renderer &renderer ) const;
        void render_rank( App &app, rendering::Renderer &renderer ) const;
        void render_no_miss( App &app, rendering::Renderer &renderer ) const;

        struct BackgroundLayer
        {
            float width = 0.0f;
            float height = 0.0f;
            rendering::Color tint{};
        };

        struct StatPanelSpec
        {
            float base_x = 0.0f;
            float base_y = 0.0f;
        };

        [[nodiscard]] static bool can_sample( const rendering::Texture &texture,
            float x,
            float y,
            float width,
            float height );

        static void draw_region( rendering::Renderer &renderer,
            const rendering::Texture &texture,
            float source_x,
            float source_y,
            float source_width,
            float source_height,
            float x,
            float y,
            float width,
            float height,
            const rendering::Color &tint = rendering::Color{} );

        static void draw_region_centered( rendering::Renderer &renderer,
            const rendering::Texture &texture,
            float source_x,
            float source_y,
            float source_width,
            float source_height,
            float center_x,
            float center_y,
            float width,
            float height,
            const rendering::Color &tint = rendering::Color{} );

        static void draw_result_digit_at( rendering::Renderer &renderer,
            const rendering::Texture &texture,
            const runtime::stage::ResultAtlas &atlas,
            std::int32_t digit,
            float x,
            float y,
            float alpha = 1.0f );

        static void draw_three_digits_at( rendering::Renderer &renderer,
            const rendering::Texture &texture,
            const runtime::stage::ResultAtlas &atlas,
            std::int32_t value,
            float x,
            float y,
            float alpha );

        static void draw_two_digits_at( rendering::Renderer &renderer,
            const rendering::Texture &texture,
            const runtime::stage::ResultAtlas &atlas,
            std::int32_t value,
            float x,
            float y,
            float alpha );

        static constexpr double tick_seconds_ = 1.0 / 60.0;
        static constexpr float background_center_x_ = 320.0f;
        static constexpr float background_center_y_ = 160.0f;
        static constexpr float background_edge_margin_ = 2.0f;

        static constexpr std::array<BackgroundLayer, 6> background_layers_{
            BackgroundLayer{ 1123.2f, 826.8f, rendering::Color{ 0.9882353f, 0.9764706f, 0.4627451f, 1.0f } },
            BackgroundLayer{ 972.0f, 715.5f, rendering::Color{ 1.0f, 0.5176471f, 0.2470588f, 1.0f } },
            BackgroundLayer{ 820.8f, 604.2f, rendering::Color{ 1.0f, 0.3960784f, 0.0f, 1.0f } },
            BackgroundLayer{ 712.8f, 524.7f, rendering::Color{ 1.0f, 0.8235294f, 0.4627451f, 1.0f } },
            BackgroundLayer{ 604.8f, 445.2f, rendering::Color{ 0.9568627f, 1.0f, 0.3254902f, 1.0f } },
            BackgroundLayer{ 432.0f, 318.0f, rendering::Color{ 0.9882353f, 1.0f, 0.7490196f, 1.0f } }
        };

        static constexpr std::array<StatPanelSpec, 4> stat_panels_{
            StatPanelSpec{ -40.0f, 32.0f },
            StatPanelSpec{ 14.0f, 201.0f },
            StatPanelSpec{ 364.0f, 201.0f },
            StatPanelSpec{ 418.0f, 32.0f }
        };

    private:
        struct RewardNotice
        {
            std::int32_t collection_index = -1;
            managers::TextureHandle name{};
            managers::TextureHandle icon{};
            rendering::Rect source{};
        };

        ResultLoadData load_data_{};
        runtime::stage::ResultAtlas atlas_{};
        runtime::stage::ResultAtlas digit_atlas_{};
        managers::TextureHandle common_texture_{};
        managers::TextureHandle stage_texture_{};
        managers::TextureHandle itfc_texture_{};
        managers::TextureHandle no_miss_label_texture_{};
        managers::TextureHandle digit_texture_{};
        managers::AudioChannelHandle result_music_channel_{};
        managers::AudioChannelHandle result_bp_channel_{};
        managers::AudioChannelHandle result_fx_channel_{};
        std::array<std::int32_t, 4> percentages_{};
        std::int32_t rank_index_ = 4;
        std::int32_t rank_score_ = 0;
        std::uint32_t display_score_ = 0;
        std::uint32_t tally_count_ = 0;
        std::int32_t display_energy_units_ = 0;
        RevealState reveal_state_ = RevealState::Opening;
        std::int32_t state_tick_ = 0;
        std::uint64_t total_tick_ = 0;
        double tick_accumulator_ = 0.0;
        float no_miss_y_ = -64.0f;
        bool no_miss_ = false;
        bool advancing_ = false;
        bool autosave_attempted_ = false;
        bool autosave_succeeded_ = false;
        std::vector<RewardNotice> rewards_{};
        std::array<managers::TextureHandle, 2> notice_headings_{};
        managers::TextureHandle autosave_icon_{};
        rendering::TrueTypeFont notice_font_{};
        std::int32_t notice_index_ = 0;
        static constexpr float notice_text_size = 12.0f;
        static constexpr float notice_raster_scale = 2.5f;
        static constexpr float notice_margin = 6.0f;
        static constexpr float notice_height = 30.0f;
        static constexpr float notice_icon_size = 24.0f;
        static constexpr float notice_icon_space = 31.0f;
        static constexpr float notice_text_padding = 12.0f;
        static constexpr float notice_minimum_text_width = 110.0f;
        static constexpr std::int32_t notice_duration_ticks = 180;
        static constexpr float notice_fade_in_ticks = 12.0f;
        static constexpr float notice_fade_out_ticks = 18.0f;

        std::int32_t notice_tick_ = 0;
        std::string status_{};
    };
}
