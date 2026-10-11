#pragma once

#include "FileTypes/CHC/CHC.hpp"
#include "GameData/StageCatalog.hpp"
#include "Managers/GameSettings.hpp"
#include "Managers/InputManager.hpp"
#include "Managers/TextureManager.hpp"
#include "Modules/PikoModule.hpp"
#include "Modules/MainMenu/TitleBackgroundRenderer.hpp"
#include "Modules/Stage/StageModuleConstants.hpp"
#include "Modules/Stage/StageTutorialRenderer.hpp"
#include "Utils/BattleTexturePixels.hpp"
#include "Modules/Stage/Stage3DRenderer.hpp"
#include "Rendering/RenderTypes.hpp"
#include "Runtime/Stage/StageRuntime.hpp"
#include "Runtime/Stage/StageHudModel.hpp"
#include "Runtime/Stage/StageAudioController.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace piko::rendering
{
    enum class TextureFilterMode;
}

namespace piko::modules
{
    enum class StageSessionMode
    {
        Standard,
        TrueGitaroo,
        Practice
    };

    struct StageLoadData
    {
        gamedata::PlayableStageEntry stage{};
        filetypes::chc::Song song{};
        bool chc_loaded = false;
        StageSessionMode session_mode = StageSessionMode::Standard;
        float carried_health = 1.0f;
        std::size_t practice_section_index = 0;
    };

    [[nodiscard]] std::optional<StageLoadData> load_true_gitaroo_stage( App &app,
        std::int32_t stage_number,
        float carried_health );

    class StageModule final : public PikoModule
    {
    public:
        explicit StageModule( gamedata::PlayableStageEntry stage );
        explicit StageModule( StageLoadData load_data );

        void enter( App &app ) override;
        void leave( App &app ) override;
        void handle_event( App &app, const SDL_Event &event ) override;
        void update( App &app, double delta_seconds ) override;
        void render( App &app, rendering::Renderer &renderer ) override;

    private:
        [[nodiscard]] bool tutorial_session() const;
        [[nodiscard]] bool teaching_tutorial() const;
        bool update_tutorial_session( App &app );
        [[nodiscard]] std::optional<StageLoadData> load_tutorial_practice( App &app ) const;
        [[nodiscard]] std::optional<StageLoadData> load_tutorial_next_stage( App &app ) const;
        void start_tutorial_next_stage( App &app );
        [[nodiscard]] double play_tutorial_menu_sound( App &app, const char *name ) const;

        enum class TutorialChoice
        {
            None,
            Again,
            Next,
            Back
        };

        stage::StageTutorialRenderer tutorial_;
        bool tutorial_completion_active_ = false;
        bool tutorial_retry_selected_ = true;
        TutorialChoice tutorial_choice_ = TutorialChoice::None;
        double tutorial_choice_ready_seconds_ = 0.0;
        std::int64_t tutorial_scene_start_sample_ = 0;

        struct RetailNoteFlashSample
        {
            float size = 20.0f;
            float angle_degrees = 0.0f;
            float alpha = 1.0f;
        };

        static constexpr float stage3d_debug_camera_nudge_units = 80.0f;
        static constexpr double stage3d_ssq_frames_per_second = 30.0;
        static constexpr double stage3d_result_frames_per_second = 30.0;
        static constexpr double stage3d_audio_sample_rate = 48000.0;

        static constexpr std::array<rendering::Rect, 2> retry_button_sources_{
            rendering::Rect{ 113.0f, 48.0f, 128.0f, 129.0f },
            rendering::Rect{ 242.0f, 48.0f, 128.0f, 129.0f }
        };

        static constexpr std::array<rendering::Rect, 2> retry_button_bounds_{
            rendering::Rect{ 139.0f, 293.0f, 128.0f, 129.0f },
            rendering::Rect{ 382.0f, 293.0f, 128.0f, 129.0f }
        };
        static constexpr std::uint32_t duet_partner_model_type = 3u;

        static constexpr std::uint32_t ssq_event_press_start = 0x001u;
        static constexpr std::uint32_t ssq_event_hold = 0x002u;
        static constexpr std::uint32_t ssq_event_bad_or_no_hit = 0x004u;
        static constexpr std::uint32_t ssq_event_end_success = 0x008u;
        static constexpr std::uint32_t ssq_event_forced_damage = 0x010u;
        static constexpr std::uint32_t ssq_event_drop_or_miss = 0x020u;
        static constexpr std::uint32_t ssq_event_success_action = 0x040u;
        static constexpr std::uint32_t ssq_event_alt_damage_a = 0x080u;
        static constexpr std::uint32_t ssq_event_alt_damage_b = 0x100u;
        static constexpr std::uint32_t ssq_event_aim_or_pending = 0x200u;

        static std::uint32_t retail_note_flash_random( std::uint32_t &state );
        static RetailNoteFlashSample retail_note_flash_sample( std::int64_t retail_tick, bool portable = false );
        static float ssq_action_angle_from_aim( float aim_x, float aim_y );
        [[nodiscard]] static Stage3DRenderer::ActionAnimationInput make_actor_animation_input(
            const runtime::stage::StageActorActionState &actor,
            std::uint32_t model_type,
            bool duet_stage );

        static rendering::TextureFilterMode renderer_filter_mode_from_settings( managers::TextureFilteringMode mode );
        static std::uint32_t clamp_score_for_display( std::uint32_t score );
        [[nodiscard]] static std::string modifier_text( const managers::GameSettings &settings );
        void update_mouse_capture( App &app );
        static const char *stage_audio_source_channel_role( std::int32_t source_channel,
            std::int32_t source_channel_count,
            managers::AudioPhraseStemMode phrase_stem_mode );

        static std::int32_t peak_percent( float peak );

        void return_to_stage_select( App &app );
        void initialize_runtime( App &app );
        [[nodiscard]] std::int64_t practice_section_global_sample() const;
        void restart_practice_section( App &app );
        void update_practice_timing_pulse( App &app );
        void load_stage_textures( App &app );
        void unload_stage_textures( App &app );
        void load_modifier_texture( App &app );
        void initialize_stage_audio( App &app );
        void shutdown_stage_audio( App &app );
        [[nodiscard]] std::filesystem::path resolve_stage_audio_path( const App &app ) const;

        [[nodiscard]] std::int32_t runtime_player_count() const;
        [[nodiscard]] float song_tempo_at( std::size_t section_index, std::int64_t section_sample ) const;
        [[nodiscard]] float portable_song_animation_frame_at( std::size_t section_index,
            std::int64_t section_sample,
            std::int64_t global_sample ) const;

        void render_fullscreen_hud( App &app,
            rendering::Renderer &renderer,
            float width,
            float height,
            float pulse ) const;

        void render_score_hud( const App &app, rendering::Renderer &renderer, float width, float height ) const;
        void render_mouse_tether( const App &app, rendering::Renderer &renderer, float width, float height ) const;
        void render_modifier_hud( const App &app, rendering::Renderer &renderer, float width, float height ) const;
        void render_phase_announcement( App &app, rendering::Renderer &renderer ) const;
        void render_retail_hud( App &app, rendering::Renderer &renderer, float width, float height ) const;
        void render_side_hud( const App &app,
            rendering::Renderer &renderer,
            const runtime::stage::StageSideState &side,
            const rendering::Rect &rect,
            bool align_right,
            float ui_scale ) const;

        void render_audio_channel_debug( App &app, rendering::Renderer &renderer, float width, float height ) const;
        void render_stage3d_material_debug( rendering::Renderer &renderer, float width, float height ) const;
        void render_stage3d_texture_debug( const App &app,
            rendering::Renderer &renderer,
            float width,
            float height ) const;

        void render_stage3d_camera_debug( rendering::Renderer &renderer, float width, float height ) const;
        void render_debug_footer( App &app, rendering::Renderer &renderer, float width, float height ) const;
        void render_bpm_edge_glow( const App &app, rendering::Renderer &renderer, float width, float height ) const;
        void render_stage_result( App &app, rendering::Renderer &renderer, float width, float height );
        void render_imgui_debug( App &app, rendering::Renderer &renderer );
        void update_energy_warning( double delta_seconds );
        [[nodiscard]] float energy_warning_alpha( std::size_t side_index ) const;
        [[nodiscard]] std::string preview_timing_text() const;
        void copy_preview_timing();
        void render_preview_timing( rendering::Renderer &renderer, float width, float height ) const;
        void render_trace_runtime( rendering::Renderer &renderer,
            const rendering::Texture *itfc_texture,
            const rendering::Texture *tube_texture,
            const rendering::Texture *trace_texture,
            const rendering::Texture *arrow_texture,
            const rendering::Texture *wing_texture,
            const rendering::Texture *center_nub_texture,
            const rendering::Texture *pressed_nub_texture,
            const std::array<const rendering::Texture *, 4> &custom_guard_icons,
            const rendering::Rect &area,
            const managers::GameSettings &settings,
            float pulse );

    private:
        enum class GuardButton : std::int32_t
        {
            None = -1,
            Left,
            Down,
            Right,
            Up
        };

        struct InputSource
        {
            managers::InputAction action;
            GuardButton guard = GuardButton::None;
            bool optional_dpad = false;
        };

        static constexpr std::array<InputSource, 15> input_sources_{
            InputSource{ managers::InputAction::StageAction },
            InputSource{ managers::InputAction::StageGuardLeft, GuardButton::Left },
            InputSource{ managers::InputAction::StageGuardDown, GuardButton::Down },
            InputSource{ managers::InputAction::StageGuardRight, GuardButton::Right },
            InputSource{ managers::InputAction::StageGuardUp, GuardButton::Up },
            InputSource{ managers::InputAction::StageDpadGuardLeft, GuardButton::Left, true },
            InputSource{ managers::InputAction::StageDpadGuardDown, GuardButton::Down, true },
            InputSource{ managers::InputAction::StageDpadGuardRight, GuardButton::Right, true },
            InputSource{ managers::InputAction::StageDpadGuardUp, GuardButton::Up, true },
            InputSource{ managers::InputAction::StageKeyboardGuardLeft, GuardButton::Left },
            InputSource{ managers::InputAction::StageKeyboardGuardDown, GuardButton::Down },
            InputSource{ managers::InputAction::StageKeyboardGuardRight, GuardButton::Right },
            InputSource{ managers::InputAction::StageKeyboardGuardUp, GuardButton::Up },
            InputSource{ managers::InputAction::StageMouseLeft },
            InputSource{ managers::InputAction::StageMouseRight }
        };

        static constexpr float tether_dot_spacing = 10.0f;
        static constexpr float tether_dot_radius = 1.75f;
        static constexpr float tether_ball_radius = 7.0f;
        static constexpr float tether_start_distance = 24.0f;

        static constexpr float modifier_text_x = 14.0f;
        static constexpr float modifier_text_y = 436.0f;
        static constexpr float modifier_font_height = 11.0f;
        static constexpr float modifier_raster_scale = 3.0f;
        static constexpr std::int32_t modifier_border_pixels = 3;
        static constexpr rendering::Color modifier_fill_color{ 1.0f, 0.85f, 0.18f, 1.0f };
        static constexpr rendering::Color modifier_border_color{ 0.0f, 0.0f, 0.0f, 0.85f };
        static constexpr std::array<const char *, 2> modifier_font_filenames{
            "noto-sans.bold.ttf",
            "noto-sans.regular.ttf"
        };

        StageLoadData load_data_{};
        std::string active_modifier_text_{};
        managers::TextureHandle modifier_text_texture_{};
        filetypes::chc::Song mirrored_song_{};
        runtime::stage::StageRuntime runtime_{};
        runtime::stage::StageAudioController stage_audio_{};
        managers::AudioChannelHandle result_menu_sound_channel_{};
        Stage3DRenderer stage3d_{};
        bool runtime_initialized_ = false;
        std::array<bool, 4> energy_warning_active_{};
        std::array<double, 4> energy_warning_tick_phase_{};

        struct PreviewTiming
        {
            float global_frame = 0.0f;
            std::size_t section_index = 0;
            float section_frame = 0.0f;
            float tempo = 120.0f;
            float portable_animation_frame = 0.0f;
        };

        PreviewTiming preview_timing_{};
        bool preview_timing_valid_ = false;
        bool show_preview_timing_ = false;
        std::int64_t practice_section_global_sample_ = 0;
        std::int64_t practice_last_timing_sample_ = -1;
        std::size_t practice_last_timing_section_ = static_cast<std::size_t>( -1 );
        std::vector<std::int64_t> practice_timing_cues_{};
        std::size_t practice_next_timing_cue_ = 0;

        managers::TextureHandle itfc_texture_{};
        managers::TextureHandle aim_arrow_texture_{};
        managers::TextureHandle wing_arrow_texture_{};
        managers::TextureHandle center_nub_texture_{};
        managers::TextureHandle pressed_nub_texture_{};
        std::array<managers::TextureHandle, 4> guard_icon_handles_{};
        managers::TextureHandle you_win_texture_{};
        managers::TextureHandle you_lose_texture_{};
        managers::TextureHandle again_texture_{};
        std::array<managers::TextureHandle, 2> retry_button_labels_{};
        std::array<managers::TextureHandle, 4> phase_announcement_textures_{};
        managers::TextureHandle result_menu_texture_{};
        TitleBackgroundRenderer result_background_{};
        ui::NoteTextureRegions note_texture_regions_{};
        managers::TextureHandle generated_tube_texture_{};
        managers::TextureHandle generated_trace_texture_{};
        managers::TextureHandle generated_score_digit_texture_{};
        std::string itfc_status_;
        std::string debug_message_;
        float debug_zoom_multiplier_ = stage::default_debug_zoom_multiplier;
        float debug_presentation_scale_x_ = stage::gameplay_presentation_scale_x;
        float debug_presentation_scale_y_ = stage::gameplay_presentation_scale_y;
        float debug_phrase_tube_visual_trim_pixels_ = stage::phrase_tube_visual_trim_pixels;
        bool debug_phrase_cap_consumes_body_ = stage::phrase_tube_cap_consumes_body_default;
        float latest_aim_x_ = 0.0f;
        float latest_aim_y_ = 0.0f;
        float latest_aim_strength_ = 0.0f;
        bool latest_center_pressed_ = false;
        bool previous_phrase_action_down_ = false;

        struct SsqPlayerActionRecord
        {
            std::uint32_t descriptor = 0;
            std::uint32_t event_mask = 0;
            float angle_radians = 0.0f;
            bool has_angle = false;
            bool action_down = false;
            bool guard_down = false;
            bool guard_pressed = false;
            std::uint64_t successful_strum_result_serial = 0;
            std::uint32_t effect_owner_model_type = 0;
            std::uint64_t effect_result_serial = 0;
            std::uint32_t effect_hold_owner_model_type = 0;
            std::uint32_t effect_hold_owner_model_mask = 0;
        };

        SsqPlayerActionRecord current_ssq_action_record_{};
        SsqPlayerActionRecord next_ssq_action_record_{};
        std::uint64_t last_latched_ssq_result_serial_ = 0;
        double elapsed_seconds_ = 0.0;
        double last_frame_delta_seconds_ = 1.0 / 60.0;
        bool stage_result_active_ = false;
        bool stage_result_won_ = true;
        bool stage_result_audio_started_ = false;
        bool stage_result_handoff_started_ = false;
        bool lose_retry_selected_ = true;
        bool lose_choice_pending_ = false;
        bool lose_pending_retry_ = true;
        float lose_choice_delay_frames_ = 0.0f;
        float stage_result_elapsed_frame_ = 0.0f;
        float lose_pre_duration_frames_ = 150.0f;
        runtime::stage::StageActionMode hud_last_phase_ = runtime::stage::StageActionMode::Unknown;
        float hud_phase_change_seconds_ = 99.0f;
        runtime::stage::StageActionMode hud_outgoing_phase_ = runtime::stage::StageActionMode::Unknown;
        static constexpr float phase_exit_seconds_ = 0.15f;
        static constexpr float phase_slide_distance_ = 20.0f;
        static constexpr float phase_announcement_center_x_ = 320.0f;
        static constexpr float phase_announcement_center_y_ = 140.0f;
        static constexpr float phase_announcement_width_ = 256.0f;
        static constexpr float phase_announcement_height_ = 64.0f;
        runtime::stage::PhaseAnnouncementState phase_announcement_{};
        float guard_rail_visual_alpha_ = 0.0f;
        float cone_arrow_visual_alpha_ = 0.0f;
        float cone_wing_slide_ = 0.0f;
        float latest_cone_degrees_ = 0.0f;
    };
}
