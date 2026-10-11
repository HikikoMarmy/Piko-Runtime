#pragma once

#include "Managers/ControllerOrientation.hpp"
#include "Managers/InterfaceStyle.hpp"
#include "Runtime/Audio/InstrumentPlayback.hpp"
#include "Utils/EnumUtil.hpp"

#include "GameData/GameDataRoot.hpp"

#include <cstdint>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace piko::managers
{
    enum class TextureFilteringMode
    {
        Nearest = 0,
        Linear = 1,
        LinearMipmap = 2,
        Anisotropic = 3
    };

    enum class BloomPreset : std::int32_t
    {
        Off,
        Low,
        Medium,
        High,
        Count
    };

    struct BloomSettings
    {
        bool enabled;
        float threshold;
        float intensity;
        std::int32_t iterations;
        float radius;
    };

    inline constexpr std::array<BloomSettings, utils::enum_index( BloomPreset::Count )> bloom_presets{
        BloomSettings{ false, 1.0f, 0.0f, 3, 1.5f },
        BloomSettings{ true, 0.90f, 0.35f, 3, 1.5f },
        BloomSettings{ true, 0.85f, 0.50f, 4, 2.0f },
        BloomSettings{ true, 0.80f, 0.65f, 5, 2.5f }
    };

    enum class AimDifficulty
    {
        Easy,
        Normal,
        Master,
        Count
    };

    [[nodiscard]] inline AimDifficulty aim_difficulty( std::string_view name )
    {
        if( name == "Easy" )
        {
            return AimDifficulty::Easy;
        }

        if( name == "Master" )
        {
            return AimDifficulty::Master;
        }

        return AimDifficulty::Normal;
    }

    struct GameplayVisualStyle
    {
        static constexpr float icon_scale_step = 0.05f;
        static constexpr float icon_scale_minimum = 0.25f;
        static constexpr float icon_scale_maximum = 2.00f;
        static constexpr float note_speed_minimum = 0.50f;
        static constexpr float note_speed_maximum = 2.00f;
        static constexpr float note_speed_step = 0.01f;
        [[nodiscard]] static float snap_icon_scale( float value );

        float trace_scale = 1.00f;
        float note_scale = 1.20f;
        float note_speed = 1.00f;
        float guard_note_scale = 1.00f;
        float guard_hit_pop_scale = 1.00f;
        float guard_hit_fade_scale = 1.00f;
        float guard_rail_scale = 0.50f;
        std::string aim_style = "default";
        std::string guard_icon_set;
        std::string centre_icon_set;
        std::string note_style = "default";
        float center_nub_scale = 1.00f;
        float pressed_nub_scale = 1.00f;
        float cone_inner_diameter = 0.0f;
        float cone_length = 100.0f;
        float cone_angle = 75.0f;
        float cone_line_width = 1.00f;
        float cone_arrow_scale = 1.00f;
        float cone_arrow_width_scale = 1.00f;
        float cone_wing_spread_scale = 1.60f;
        float cone_wing_scale = 1.00f;
        float cone_wing_width_scale = 1.00f;
        float arrow_offset_x = 0.00f;
        float wing_offset_x = 0.00f;

        std::array<float, 4> trace_idle_color{
            16.0f / 255.0f,
            213.0f / 255.0f,
            1.0f,
            0.92f
        };

        std::array<float, 4> trace_locked_color{
            40.0f / 255.0f,
            225.0f / 255.0f,
            50.0f / 255.0f,
            0.96f
        };

        std::array<float, 4> trace_lost_color{
            0.0f,
            0.0f,
            1.0f,
            0.92f
        };

        std::array<float, 4> note_color{
            1.0f,
            150.0f / 255.0f,
            50.0f / 255.0f,
            1.0f
        };

        std::array<float, 4> held_note_color{
            210.0f / 255.0f,
            110.0f / 255.0f,
            110.0f / 255.0f,
            1.0f
        };

        std::array<float, 4> note_body_color{
            210.0f / 255.0f,
            110.0f / 255.0f,
            110.0f / 255.0f,
            1.0f
        };

        std::array<float, 4> guard_rail_color{
            0.005f,
            0.035f,
            0.16f,
            0.30f
        };

        void reset_defaults();
    };

    struct GameSettings
    {
        bool show_performance_overlay = false;
        bool show_stage_debug_overlay = false;
        bool show_judgement_detail = false;
        bool show_raw_energy_values = false;
        bool show_stage_audio_status = false;

        bool msaa_enabled = true;
        std::int32_t msaa_samples = 4;
        std::int32_t window_width = 1280;
        std::int32_t window_height = 720;
        std::int32_t render_resolution_percent = 100;
        bool per_pixel_lighting = false;
        TextureFilteringMode texture_filtering_mode = TextureFilteringMode::LinearMipmap;
        float texture_anisotropy = 4.0f;

        BloomPreset bloom_preset = BloomPreset::Off;

        [[nodiscard]] BloomSettings bloom_settings() const
        {
            const auto index = utils::enum_index( bloom_preset );
            return bloom_presets[ index < bloom_presets.size() ? index : 0 ];
        }

        bool crt_filter_enabled = false;
        float crt_scanline_strength = 0.16f;
        float crt_vignette_strength = 0.18f;
        float crt_noise_strength = 0.025f;

        gamedata::ChartStyle chart_style = gamedata::ChartStyle::GitarooMan;
        gamedata::GameLanguage language = gamedata::GameLanguage::Default;
        std::string ui_language = "en";
        std::unordered_map<std::string, InterfaceStyle> interface_styles{};
        std::int32_t audio_import_compression = 0;
        bool play_movies = true;
        bool stereo_audio = true;
        bool background_fx_enabled = true;
        std::int32_t instrument_routing = 0;
        runtime::audio::InstrumentPlaybackMode instrument_playback = runtime::audio::InstrumentPlaybackMode::Original;
        ControllerOrientation controller_orientation = ControllerOrientation::TypeA;
        std::string input_device_name;
        bool mouse_controls_enabled = true;
        bool guard_notes_accept_dpad = false;
        bool always_retrigger_player_strum = false;
        std::int32_t duet_chart_mode = 0;
        bool phrase_tube_wobble = false;
        bool phrase_tube_gradient_pulse = false;
        bool stage_bpm_edge_glow = false;
        bool no_death_mode = false;
        bool only_aim_mode = false;
        bool all_sections_mode = false;
        bool mirror_mode = false;
        float player_damage_multiplier = 1.0f;
        float enemy_damage_multiplier = 1.0f;
        std::int32_t phrase_note_color_index = 0;
        std::int32_t phrase_note_held_color_index = 0;

        GameplayVisualStyle gameplay_visual{};

        std::array<float, utils::enum_index( AimDifficulty::Count )> aim_snap_angles{
            31.5f,
            31.5f,
            31.5f
        };

        std::array<float, utils::enum_index( AimDifficulty::Count )> psp_aim_snap_angles{
            60.0f,
            60.0f,
            60.0f
        };

        [[nodiscard]] float snap_half_angle_for( AimDifficulty difficulty,
            gamedata::ChartStyle chart_style = gamedata::ChartStyle::GitarooMan ) const
        {
            const auto &angles =
                chart_style == gamedata::ChartStyle::GitarooManLives ? psp_aim_snap_angles : aim_snap_angles;

            return angles[ utils::enum_index( difficulty ) ];
        }

        std::array<float, 4> view_cone_on_target_line_color{
            1.0f,
            176.0f / 255.0f,
            0.0f,
            1.0f
        };

        std::array<float, 4> view_cone_off_target_line_color{
            128.0f / 255.0f,
            233.0f / 255.0f,
            1.0f,
            1.0f
        };

        std::array<float, 4> view_cone_on_target_fill_color{
            1.0f,
            176.0f / 255.0f,
            0.0f,
            0.52f
        };

        std::array<float, 4> view_cone_off_target_fill_color{
            105.0f / 255.0f,
            229.0f / 255.0f,
            1.0f,
            189.0f / 255.0f
        };

        void reset_defaults();
    };

    [[nodiscard]] std::filesystem::path game_settings_path();
    bool load_game_settings( GameSettings &settings, std::string *error = nullptr );
    bool save_game_settings( const GameSettings &settings, std::string *error = nullptr );
    [[nodiscard]] std::filesystem::path visual_profile_directory();
    bool save_visual_profile( const GameSettings &settings,
        const std::filesystem::path &path,
        std::string *error = nullptr );

    bool load_visual_profile( GameSettings &settings, const std::filesystem::path &path, std::string *error = nullptr );
}
