#pragma once

#include "FileTypes/CHC/CHC.hpp"
#include "GameData/StageCatalog.hpp"
#include "Managers/AudioManager.hpp"
#include "Managers/MenuNavigation.hpp"
#include "Managers/InputManager.hpp"
#include "Managers/TextureManager.hpp"
#include "Modules/MainMenu/TitleBackgroundRenderer.hpp"
#include "Modules/PikoModule.hpp"
#include "Rendering/RenderTypes.hpp"
#include "Rendering/RenderUtil.hpp"
#include "Rendering/TrueTypeText.hpp"
#include "Rendering/MenuTheme.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace piko::gamedata
{
    enum class SaveProgressMode;
}

namespace piko::modules
{
    class StageSelectModule final : public PikoModule
    {
    public:
        enum class StageCategory
        {
            SingleEasy,
            SingleNormal,
            SingleMaster,
            Multiplayer,
            All
        };

        enum class LaunchMode
        {
            Standard,
            Practice
        };

        explicit StageSelectModule( StageCategory initial_category = StageCategory::SingleNormal,
            LaunchMode launch_mode = LaunchMode::Standard );

        void enter( App &app ) override;
        void leave( App &app ) override;
        void handle_event( App &app, const SDL_Event &event ) override;
        void update( App &app, double delta_seconds ) override;
        void render( App &app, rendering::Renderer &renderer ) override;
        [[nodiscard]] static std::string stage_preview_key( const gamedata::PlayableStageEntry &stage );
        [[nodiscard]] static bool ensure_stage_preview( App &app,
            rendering::Renderer &renderer,
            const gamedata::PlayableStageEntry &stage );

    private:
        enum class FocusArea
        {
            StageList,
            Difficulty
        };

        enum class TextAlignment
        {
            Left,
            Center,
            Right
        };

        static constexpr std::array<managers::MenuLocation, utils::enum_index( StageCategory::All ) + 1>
            menu_locations_{
            managers::MenuLocation::StageEasy,
            managers::MenuLocation::StageNormal,
            managers::MenuLocation::StageMaster,
            managers::MenuLocation::StageMultiplayer,
            managers::MenuLocation::StageAll
        };

        void load_stage_catalog( App &app );
        void refresh_chart_style_availability( const App &app );
        bool move_focus( std::int32_t direction );
        void move_choice_cursor( std::int32_t direction );
        void apply_focused_choice( App &app );
        void render_chart_style( App &app, rendering::Renderer &renderer, const rendering::LogicalViewport &layout );
        void apply_stage_category();
        void change_category( App &app, std::int32_t delta );
        void set_category( App &app, StageCategory category );
        void load_stage_select_assets( App &app );
        void load_song_titles( const App &app );
        void load_preview_vignette( App &app );
        void load_rank_assets( App &app );
        void unload_stage_select_assets( App &app );
        void initialize_audio( App &app );
        void play_menu_sound( App &app, const char *name );
        void move_selection( App &app, std::int32_t delta );
        void set_selection( App &app, std::int32_t index );
        void reveal_selection();
        void scroll_stage_list( App &app, std::int32_t delta );
        bool update_scrollbar( App &app, const managers::MouseState &mouse );
        void render_scrollbar( rendering::Renderer &renderer, const rendering::LogicalViewport &layout );
        [[nodiscard]] static rendering::LogicalViewport screen_layout( const rendering::Renderer &renderer );
        void confirm_selection( App &app );
        void open_practice_sections( App &app );
        void move_practice_section( App &app, std::int32_t delta );
        void confirm_practice_section( App &app );
        void close_practice_sections( App &app );
        void start_practice_audio_preview( App &app );
        void stop_practice_audio_preview( App &app, bool restore_menu_audio );
        [[nodiscard]] std::filesystem::path resolve_practice_audio_path( const App &app ) const;
        [[nodiscard]] std::filesystem::path resolve_stage_audio_path( const App &app,
            const gamedata::PlayableStageEntry &stage ) const;

        [[nodiscard]] static std::filesystem::path stage_preview_path( const App &app,
            const gamedata::PlayableStageEntry &stage );

        [[nodiscard]] static bool generate_stage_preview( App &app,
            rendering::Renderer &renderer,
            const gamedata::PlayableStageEntry &stage,
            const std::filesystem::path &path );

        [[nodiscard]] managers::TextureHandle selected_stage_preview( App &app );
        void start_stage_audio_preview( App &app );
        void stop_stage_audio_preview( App &app, bool restore_bgm );
        void draw_label( App &app,
            rendering::Renderer &renderer,
            const rendering::LogicalViewport &layout,
            const rendering::Rect &bounds,
            const std::string &label,
            float size,
            bool selected = false,
            TextAlignment alignment = TextAlignment::Left );

        void render_practice_sections( App &app, rendering::Renderer &renderer );
        void render_rank_badge( App &app,
            rendering::Renderer &renderer,
            const rendering::LogicalViewport &layout,
            const rendering::Rect &bounds,
            const gamedata::PlayableStageEntry &stage );

        void return_to_main_menu( App &app );
        std::int32_t hit_test_stage( float x, float y ) const;
        std::int32_t hit_test_category( float x, float y ) const;
        [[nodiscard]] static const char *category_label( StageCategory category );
        [[nodiscard]] static bool stage_matches_category( const gamedata::PlayableStageEntry &stage,
            StageCategory category );

        [[nodiscard]] static std::size_t category_count( const std::vector<gamedata::PlayableStageEntry> &stages,
            StageCategory category );

        [[nodiscard]] static StageCategory first_non_empty_category(
            const std::vector<gamedata::PlayableStageEntry> &stages );

        [[nodiscard]] static const char *rank_label( std::int32_t rank );
        [[nodiscard]] static gamedata::SaveProgressMode progress_mode_for_stage(
            const gamedata::PlayableStageEntry &stage );

        [[nodiscard]] static std::string score_text( std::uint32_t score );

        static constexpr std::array<StageCategory, 3> stage_categories_{
            StageCategory::SingleEasy,
            StageCategory::SingleNormal,
            StageCategory::SingleMaster
        };

        static constexpr std::array chart_styles_{
            gamedata::ChartStyle::GitarooMan,
            gamedata::ChartStyle::GitarooManLives
        };

        static constexpr float card_gap_ = rendering::menu_theme::control_gap * 0.5f;
        static constexpr float header_bottom_ = 50.0f;
        static constexpr float tabs_top_ = header_bottom_ + card_gap_;
        static constexpr float body_top_ = 84.0f;
        static constexpr float body_split_x_ = 307.0f;
        static constexpr float text_raster_scale_ = rendering::menu_theme::compact_raster_scale;
        static constexpr float stage_row_height_ = 44.0f;
        static constexpr float stage_row_pitch_ = stage_row_height_ + card_gap_;
        static constexpr std::int32_t visible_stage_rows_ = 7;
        static constexpr std::int32_t preview_stage_rows_ = 4;
        static constexpr float body_bottom_ = body_top_ + stage_row_pitch_ * visible_stage_rows_ - card_gap_;
        static constexpr float preview_bottom_ = body_top_ + stage_row_pitch_ * preview_stage_rows_ - card_gap_;
        static constexpr rendering::Rect window_bounds_{ 19.0f, 7.0f, 602.0f, body_bottom_ - 7.0f };
        static constexpr std::int32_t vignette_width_ = 256;
        static constexpr std::int32_t vignette_height_ = 144;
        static constexpr float vignette_opacity_ = 0.34f;
        static constexpr std::size_t maximum_song_titles_ = 2;
        static constexpr float song_title_font_size_ = 16.0f;
        static constexpr float song_title_first_y_ = 274.0f;
        static constexpr float song_title_line_height_ = 23.0f;
        static constexpr float scrollbar_width_ = 6.0f;
        static constexpr float scrollbar_minimum_thumb_ = 24.0f;

        static constexpr rendering::Rect chart_style_label_bounds_{ 299.0f, 8.0f, 310.0f, 41.0f };

        static constexpr std::array chart_style_label_keys_{
            "@ui.chart_style.gitaroo_man",
            "@ui.chart_style.gitaroo_man_lives"
        };

        static constexpr std::array<const char *, 2> chart_style_labels_{
            "Gitaroo Man",
            "Gitaroo Man Lives!"
        };

        std::array<bool, chart_styles_.size()> chart_styles_available_{};
        FocusArea focus_ = FocusArea::StageList;
        StageCategory focused_category_ = StageCategory::SingleNormal;

        std::vector<gamedata::PlayableStageEntry> all_stages_;
        std::vector<gamedata::PlayableStageEntry> stages_;
        std::unordered_map<std::int32_t, std::array<std::string, maximum_song_titles_>> stage_song_titles_;
        StageCategory initial_category_ = StageCategory::SingleNormal;
        StageCategory active_category_ = StageCategory::SingleNormal;
        LaunchMode launch_mode_ = LaunchMode::Standard;
        managers::MenuNavigation *navigation_ = nullptr;
        std::int32_t selected_stage_ = 0;
        std::int32_t first_visible_stage_ = 0;
        float wheel_scroll_fraction_ = 0.0f;
        bool scrollbar_dragging_ = false;
        float scrollbar_grab_offset_ = 0.0f;
        rendering::Rect stage_list_rect_{};
        rendering::Rect scrollbar_track_rect_{};
        rendering::Rect scrollbar_thumb_rect_{};
        bool selecting_practice_section_ = false;
        gamedata::PlayableStageEntry practice_stage_{};
        filetypes::chc::Song practice_song_{};
        std::vector<std::size_t> practice_sections_{};
        std::int32_t selected_practice_section_ = 0;
        std::string action_message_;
        TitleBackgroundRenderer wave_background_{};
        managers::TextureHandle circle_texture_{};
        managers::TextureHandle preview_vignette_{};
        managers::TextureHandle stage_select_portraits_texture_{};
        std::array<rendering::Rect, 10> stage_select_portrait_rects_{};
        managers::TextureHandle rank_texture_{};
        std::array<rendering::Rect, 6> rank_rects_{};
        managers::AudioChannelHandle bgm_channel_{};
        managers::AudioChannelHandle sfx_channel_{};
        managers::AudioChannelHandle practice_preview_channel_{};
        bool practice_preview_bank_loaded_ = false;
        std::unordered_map<std::string, managers::TextureHandle> preview_textures_{};
        std::unordered_map<std::string, managers::TextureHandle> text_textures_{};
        rendering::TrueTypeFont stage_select_font_{};
        std::unordered_set<std::string> unavailable_previews_{};
        std::unordered_set<std::string> loaded_preview_audio_banks_{};
        std::int32_t audio_preview_stage_number_ = -1;
        std::int32_t audio_preview_requested_stage_number_ = -1;
        std::string pending_preview_cue_{};
        double preview_selection_seconds_ = 0.0;
        double audio_preview_elapsed_seconds_ = 0.0;
        bool audio_preview_finished_ = false;
        std::vector<rendering::Rect> stage_hit_rects_;
        std::array<rendering::Rect, stage_categories_.size()> category_hit_rects_{};
        double elapsed_seconds_ = 0.0;
    };
}
