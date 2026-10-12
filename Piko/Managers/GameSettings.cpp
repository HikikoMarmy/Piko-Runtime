#include "GameSettings.hpp"
#include "SettingsWriter.hpp"
#include "Localization/Language.hpp"
#include "Utils/FileUtil.hpp"
#include "Utils/Json.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace piko::managers
{
    namespace
    {
        float clamp_number( double value, float minimum, float maximum )
        {
            return std::clamp( static_cast<float>( value ), minimum, maximum );
        }

        void load_bool( const utils::json::Value &object, const char *key, bool &value )
        {
            if( const utils::json::Value *entry = object.find( key ); entry != nullptr )
            {
                value = entry->boolean_or( value );
            }
        }

        void load_int( const utils::json::Value &object,
            const char *key,
            std::int32_t &value,
            std::int32_t minimum,
            std::int32_t maximum )
        {
            if( const utils::json::Value *entry = object.find( key ); entry != nullptr )
            {
                const auto number = entry->integer_or( value );
                const auto clamped = std::clamp<std::int64_t>( number, minimum, maximum );
                value = static_cast<std::int32_t>( clamped );
            }
        }

        void load_float( const utils::json::Value &object, const char *key, float &value, float minimum, float maximum )
        {
            if( const utils::json::Value *entry = object.find( key ); entry != nullptr )
            {
                value = clamp_number( entry->number_or( value ), minimum, maximum );
            }
        }

        void load_color( const utils::json::Value &object, const char *key, std::array<float, 4> &color )
        {
            const utils::json::Value *entry = object.find( key );

            if( entry == nullptr || !entry->is_array() )
            {
                return;
            }

            const utils::json::Value::Array &array = entry->array();

            if( array.size() != color.size() )
            {
                return;
            }

            for( std::size_t index = 0; index < color.size(); ++index )
            {
                color[ index ] = clamp_number( array[ index ].number_or( color[ index ] ), 0.0f, 1.0f );
            }
        }

        void load_string( const utils::json::Value &object,
            const char *key,
            std::string &value,
            const std::string &fallback = {} )
        {
            if( const auto *entry = object.find( key ); entry != nullptr )
            {
                value = entry->string_or( fallback );
            }
        }

        template<typename Enum>
        void load_enum( const utils::json::Value &object,
            const char *key,
            Enum &value,
            Enum minimum,
            Enum maximum )
        {
            auto number = utils::to_int( value );
            load_int( object, key, number, utils::to_int( minimum ), utils::to_int( maximum ) );
            value = utils::to_enum<Enum>( number );
        }

        void load_interface_styles( const utils::json::Value &root, GameSettings &settings )
        {
            const auto *entries = root.find( "interface_styles" );

            if( entries == nullptr || !entries->is_object() )
            {
                return;
            }

            for( const auto &[ language, entry ] : entries->object() )
            {
                if( !entry.is_object() )
                {
                    continue;
                }

                InterfaceStyle style;

                if( const auto *fonts = entry.find( "fonts" ); fonts != nullptr && fonts->is_array() )
                {
                    for( std::size_t index = 0; index < std::min( style.fonts.size(), fonts->array().size() ); ++index )
                    {
                        style.fonts[ index ] = fonts->array()[ index ].string_or();
                    }
                }

                settings.interface_styles[ localization::normalize_language_id( language ) ] = std::move( style );
            }
        }

        void write_interface_styles( SettingsWriter &writer, const GameSettings &settings )
        {
            std::vector<std::string> languages;

            for( const auto &entry : settings.interface_styles )
            {
                languages.push_back( entry.first );
            }

            std::sort( languages.begin(), languages.end() );
            writer.begin_object( "interface_styles" );

            for( const auto &language : languages )
            {
                writer.begin_object( language );

                writer.write( "fonts", settings.interface_styles.at( language ).fonts );
                writer.end_object();
            }

            writer.end_object();
        }

        void load_gameplay_visual( const utils::json::Value &root, GameplayVisualStyle &style )
        {
            const utils::json::Value *visual = root.find( "gameplay_visual" );

            if( visual == nullptr || !visual->is_object() )
            {
                return;
            }

            load_float( *visual, "trace_scale", style.trace_scale, 0.40f, 2.00f );

            load_float( *visual, "note_scale", style.note_scale, 0.50f, 1.80f );

            style.note_speed = GameplayVisualStyle{}.note_speed;
            load_float( *visual,
                "note_speed",
                style.note_speed,
                GameplayVisualStyle::note_speed_minimum,
                GameplayVisualStyle::note_speed_maximum );

            load_float( *visual, "guard_note_scale", style.guard_note_scale, 0.50f, 1.50f );

            load_float( *visual, "guard_hit_pop_scale", style.guard_hit_pop_scale, 0.50f, 2.00f );

            load_float( *visual, "guard_hit_fade_scale", style.guard_hit_fade_scale, 0.50f, 2.00f );

            load_float( *visual, "guard_rail_scale", style.guard_rail_scale, 0.35f, 2.00f );

            load_string( *visual, "aim_style", style.aim_style, "default" );

            load_string( *visual, "note_style", style.note_style, "default" );

            style.guard_icon_set.clear();
            load_string( *visual, "guard_icon_set", style.guard_icon_set );

            style.centre_icon_set.clear();
            load_string( *visual, "centre_icon_set", style.centre_icon_set );

            load_float( *visual, "center_nub_scale", style.center_nub_scale,
                GameplayVisualStyle::icon_scale_minimum, GameplayVisualStyle::icon_scale_maximum );

            load_float( *visual, "pressed_nub_scale", style.pressed_nub_scale,
                GameplayVisualStyle::icon_scale_minimum, GameplayVisualStyle::icon_scale_maximum );

            style.center_nub_scale = GameplayVisualStyle::snap_icon_scale( style.center_nub_scale );

            style.pressed_nub_scale = GameplayVisualStyle::snap_icon_scale( style.pressed_nub_scale );

            load_float( *visual, "cone_inner_diameter", style.cone_inner_diameter, 0.0f, 40.0f );

            load_float( *visual, "cone_length", style.cone_length, 60.0f, 240.0f );

            load_float( *visual, "cone_angle", style.cone_angle, 10.0f, 160.0f );

            load_float( *visual, "cone_line_width", style.cone_line_width, 1.0f, 8.0f );

            load_float( *visual, "cone_arrow_scale", style.cone_arrow_scale, 0.50f, 1.80f );

            load_float( *visual, "cone_arrow_width_scale", style.cone_arrow_width_scale, 0.35f, 1.50f );

            load_float( *visual, "cone_wing_spread_scale", style.cone_wing_spread_scale, 0.45f, 1.60f );

            load_float( *visual, "cone_wing_scale", style.cone_wing_scale, 0.50f, 1.80f );

            load_float( *visual, "cone_wing_width_scale", style.cone_wing_width_scale, 0.35f, 1.50f );

            load_float( *visual, "arrow_offset_x", style.arrow_offset_x, -60.00f, 60.00f );

            load_float( *visual, "wing_offset_x", style.wing_offset_x, -60.00f, 60.00f );

            load_color( *visual, "trace_idle_color", style.trace_idle_color );

            load_color( *visual, "trace_locked_color", style.trace_locked_color );

            load_color( *visual, "trace_lost_color", style.trace_lost_color );

            load_color( *visual, "note_color", style.note_color );

            load_color( *visual, "held_note_color", style.held_note_color );

            load_color( *visual, "guard_rail_color", style.guard_rail_color );

            load_color( *visual, "note_body_color", style.note_body_color );
        }

        void write_gameplay_visual( SettingsWriter &writer, const GameplayVisualStyle &style )
        {
            writer.begin_object( "gameplay_visual" );

            writer.write( "trace_scale", style.trace_scale );

            writer.write( "trace_idle_color", style.trace_idle_color );

            writer.write( "trace_locked_color", style.trace_locked_color );

            writer.write( "trace_lost_color", style.trace_lost_color );

            writer.write( "note_style", style.note_style );

            writer.write( "note_scale", style.note_scale );

            writer.write( "note_speed", style.note_speed );

            writer.write( "note_color", style.note_color );

            writer.write( "held_note_color", style.held_note_color );

            writer.write( "note_body_color", style.note_body_color );

            writer.write( "guard_icon_set", style.guard_icon_set );

            writer.write( "guard_note_scale", style.guard_note_scale );

            writer.write( "guard_hit_pop_scale", style.guard_hit_pop_scale );

            writer.write( "guard_hit_fade_scale", style.guard_hit_fade_scale );

            writer.write( "guard_rail_scale", style.guard_rail_scale );

            writer.write( "guard_rail_color", style.guard_rail_color );

            writer.write( "centre_icon_set", style.centre_icon_set );

            writer.write( "center_nub_scale", style.center_nub_scale );

            writer.write( "pressed_nub_scale", style.pressed_nub_scale );

            writer.write( "cone_inner_diameter", style.cone_inner_diameter );

            writer.write( "cone_length", style.cone_length );

            writer.write( "cone_angle", style.cone_angle );

            writer.write( "cone_line_width", style.cone_line_width );

            writer.write( "aim_style", style.aim_style );

            writer.write( "cone_arrow_scale", style.cone_arrow_scale );

            writer.write( "cone_arrow_width_scale", style.cone_arrow_width_scale );

            writer.write( "arrow_offset_x", style.arrow_offset_x );

            writer.write( "cone_wing_scale", style.cone_wing_scale );

            writer.write( "cone_wing_width_scale", style.cone_wing_width_scale );

            writer.write( "cone_wing_spread_scale", style.cone_wing_spread_scale );

            writer.write( "wing_offset_x", style.wing_offset_x );

            writer.end_object();
        }

        void load_snap_angles( const utils::json::Value &root,
            const char *key,
            std::array<float, utils::enum_index( AimDifficulty::Count )> &values )
        {
            const auto *angles = root.find( key );

            if( angles == nullptr || !angles->is_object() )
            {
                return;
            }

            load_float( *angles, "easy", values[ utils::enum_index( AimDifficulty::Easy ) ], 5.0f, 80.0f );

            load_float( *angles, "normal", values[ utils::enum_index( AimDifficulty::Normal ) ], 5.0f, 80.0f );

            load_float( *angles, "master", values[ utils::enum_index( AimDifficulty::Master ) ], 5.0f, 80.0f );
        }

        void load_aim_settings( const utils::json::Value &root, GameSettings &settings )
        {
            load_snap_angles( root, "aim_snap_angles", settings.aim_snap_angles );

            load_snap_angles( root, "psp_aim_snap_angles", settings.psp_aim_snap_angles );
        }

        void write_snap_angles( SettingsWriter &writer,
            const char *key,
            const std::array<float, utils::enum_index( AimDifficulty::Count )> &values )
        {
            writer.begin_object( key );
            writer.write( "easy", values[ utils::enum_index( AimDifficulty::Easy ) ] );

            writer.write( "normal", values[ utils::enum_index( AimDifficulty::Normal ) ] );

            writer.write( "master", values[ utils::enum_index( AimDifficulty::Master ) ] );
            writer.end_object();
        }

        void write_aim_settings( SettingsWriter &writer, const GameSettings &settings )
        {
            write_snap_angles( writer, "aim_snap_angles", settings.aim_snap_angles );
            write_snap_angles( writer, "psp_aim_snap_angles", settings.psp_aim_snap_angles );
        }

        void load_cone_colors( const utils::json::Value &root, GameSettings &settings )
        {
            load_color( root, "view_cone_on_target_line_color", settings.view_cone_on_target_line_color );

            load_color( root, "view_cone_off_target_line_color", settings.view_cone_off_target_line_color );

            load_color( root, "view_cone_on_target_fill_color", settings.view_cone_on_target_fill_color );

            load_color( root, "view_cone_off_target_fill_color", settings.view_cone_off_target_fill_color );
        }

        void write_cone_colors( SettingsWriter &writer, const GameSettings &settings )
        {
            writer.write( "view_cone_on_target_line_color", settings.view_cone_on_target_line_color );

            writer.write( "view_cone_off_target_line_color", settings.view_cone_off_target_line_color );

            writer.write( "view_cone_on_target_fill_color", settings.view_cone_on_target_fill_color );

            writer.write( "view_cone_off_target_fill_color", settings.view_cone_off_target_fill_color );
        }

        void load_graphics( const utils::json::Value &root, GameSettings &settings )
        {
            load_bool( root, "msaa_enabled", settings.msaa_enabled );

            load_int( root, "msaa_samples", settings.msaa_samples, 0, 16 );

            load_int( root, "window_width", settings.window_width, 640, 7680 );

            load_int( root, "window_height", settings.window_height, 360, 4320 );

            load_int( root, "render_resolution_percent", settings.render_resolution_percent, 25, 200 );

            load_bool( root, "per_pixel_lighting", settings.per_pixel_lighting );

            load_enum( root, "texture_filtering_mode", settings.texture_filtering_mode,
                TextureFilteringMode::Nearest, TextureFilteringMode::Anisotropic );

            load_float( root, "texture_anisotropy", settings.texture_anisotropy, 1.0f, 16.0f );

            load_enum( root, "bloom_preset", settings.bloom_preset,
                BloomPreset::Off, BloomPreset::High );

            load_bool( root, "crt_filter_enabled", settings.crt_filter_enabled );

            load_float( root, "crt_scanline_strength", settings.crt_scanline_strength, 0.0f, 1.0f );

            load_float( root, "crt_vignette_strength", settings.crt_vignette_strength, 0.0f, 1.0f );

            load_float( root, "crt_noise_strength", settings.crt_noise_strength, 0.0f, 1.0f );
        }

        void write_graphics( SettingsWriter &writer, const GameSettings &settings )
        {
            writer.write( "msaa_enabled", settings.msaa_enabled );

            writer.write( "msaa_samples", settings.msaa_samples );

            writer.write( "window_width", settings.window_width );

            writer.write( "window_height", settings.window_height );

            writer.write( "render_resolution_percent", settings.render_resolution_percent );

            writer.write( "per_pixel_lighting", settings.per_pixel_lighting );

            writer.write( "texture_filtering_mode", settings.texture_filtering_mode );

            writer.write( "texture_anisotropy", settings.texture_anisotropy );

            writer.write( "bloom_preset", settings.bloom_preset );

            writer.write( "crt_filter_enabled", settings.crt_filter_enabled );

            writer.write( "crt_scanline_strength", settings.crt_scanline_strength );

            writer.write( "crt_vignette_strength", settings.crt_vignette_strength );

            writer.write( "crt_noise_strength", settings.crt_noise_strength );
        }

        void load_game_data( const utils::json::Value &root, GameSettings &settings )
        {
            load_enum( root, "chart_style", settings.chart_style,
                gamedata::ChartStyle::GitarooMan, gamedata::ChartStyle::GitarooManLives );

            load_enum( root, "language", settings.language,
                gamedata::GameLanguage::Default, gamedata::GameLanguage::German );

            load_string( root, "ui_language", settings.ui_language, settings.ui_language );

            load_int( root, "audio_import_compression", settings.audio_import_compression, 0, 1 );

            load_bool( root, "play_movies", settings.play_movies );
        }

        void write_game_data( SettingsWriter &writer, const GameSettings &settings )
        {
            writer.write( "chart_style", settings.chart_style );

            writer.write( "language", settings.language );

            writer.write( "ui_language", settings.ui_language );

            writer.write( "audio_import_compression", settings.audio_import_compression );

            writer.write( "play_movies", settings.play_movies );
        }

        void load_audio( const utils::json::Value &root, GameSettings &settings )
        {
            load_bool( root, "stereo_audio", settings.stereo_audio );

            load_bool( root, "background_fx_enabled", settings.background_fx_enabled );

            load_int( root, "instrument_routing", settings.instrument_routing, 0, 2 );

            load_enum( root, "instrument_playback", settings.instrument_playback,
                runtime::audio::InstrumentPlaybackMode::Original, runtime::audio::InstrumentPlaybackMode::TraceEnd );
        }

        void write_audio( SettingsWriter &writer, const GameSettings &settings )
        {
            writer.write( "stereo_audio", settings.stereo_audio );

            writer.write( "background_fx_enabled", settings.background_fx_enabled );

            writer.write( "instrument_routing", settings.instrument_routing );

            writer.write( "instrument_playback", settings.instrument_playback );
        }

        void load_controller( const utils::json::Value &root, GameSettings &settings )
        {
            load_enum( root, "controller_orientation", settings.controller_orientation,
                ControllerOrientation::TypeA, ControllerOrientation::TypeC );

            load_string( root, "input_device_name", settings.input_device_name );

            load_bool( root, "mouse_controls_enabled", settings.mouse_controls_enabled );

            load_bool( root, "guard_notes_accept_dpad", settings.guard_notes_accept_dpad );

            load_int( root, "duet_chart_mode", settings.duet_chart_mode, 0, 3 );

            load_bool( root, "always_retrigger_player_strum", settings.always_retrigger_player_strum );
        }

        void write_controller( SettingsWriter &writer, const GameSettings &settings )
        {
            writer.write( "controller_orientation", settings.controller_orientation );

            writer.write( "input_device_name", settings.input_device_name );

            writer.write( "mouse_controls_enabled", settings.mouse_controls_enabled );

            writer.write( "guard_notes_accept_dpad", settings.guard_notes_accept_dpad );

            writer.write( "duet_chart_mode", settings.duet_chart_mode );

            writer.write( "always_retrigger_player_strum", settings.always_retrigger_player_strum );
        }

        void load_interface( const utils::json::Value &root, GameSettings &settings )
        {
            load_bool( root, "hide_battle_ui", settings.hide_battle_ui );

            load_bool( root, "phrase_tube_wobble", settings.phrase_tube_wobble );

            load_bool( root, "phrase_tube_gradient_pulse", settings.phrase_tube_gradient_pulse );

            load_bool( root, "stage_bpm_edge_glow", settings.stage_bpm_edge_glow );

            load_int( root, "phrase_note_color_index", settings.phrase_note_color_index, 0, 7 );

            load_int( root, "phrase_note_held_color_index", settings.phrase_note_held_color_index, 0, 7 );

            load_interface_styles( root, settings );

            load_gameplay_visual( root, settings.gameplay_visual );

            load_cone_colors( root, settings );
        }

        void write_interface( SettingsWriter &writer, const GameSettings &settings )
        {
            writer.write( "hide_battle_ui", settings.hide_battle_ui );

            writer.write( "phrase_tube_wobble", settings.phrase_tube_wobble );

            writer.write( "phrase_tube_gradient_pulse", settings.phrase_tube_gradient_pulse );

            writer.write( "stage_bpm_edge_glow", settings.stage_bpm_edge_glow );

            writer.write( "phrase_note_color_index", settings.phrase_note_color_index );

            writer.write( "phrase_note_held_color_index", settings.phrase_note_held_color_index );

            write_interface_styles( writer, settings );
            write_gameplay_visual( writer, settings.gameplay_visual );
            write_cone_colors( writer, settings );
        }

        void load_modifiers( const utils::json::Value &root, GameSettings &settings )
        {
            load_bool( root, "no_death_mode", settings.no_death_mode );

            load_bool( root, "auto_press_mode", settings.auto_press_mode );

            load_bool( root, "auto_aim_mode", settings.auto_aim_mode );

            load_bool( root, "all_sections_mode", settings.all_sections_mode );

            load_bool( root, "mirror_mode", settings.mirror_mode );

            load_float( root, "player_damage_multiplier", settings.player_damage_multiplier, 0.0f, 4.0f );

            load_float( root, "enemy_damage_multiplier", settings.enemy_damage_multiplier, 0.0f, 4.0f );

            load_aim_settings( root, settings );
        }

        void write_modifiers( SettingsWriter &writer, const GameSettings &settings )
        {
            writer.write( "no_death_mode", settings.no_death_mode );

            writer.write( "auto_press_mode", settings.auto_press_mode );

            writer.write( "auto_aim_mode", settings.auto_aim_mode );

            writer.write( "all_sections_mode", settings.all_sections_mode );

            writer.write( "mirror_mode", settings.mirror_mode );

            writer.write( "player_damage_multiplier", settings.player_damage_multiplier );

            writer.write( "enemy_damage_multiplier", settings.enemy_damage_multiplier );

            write_aim_settings( writer, settings );
        }

#if defined( _DEBUG )
        void load_debug( const utils::json::Value &root, GameSettings &settings )
        {
            load_bool( root, "show_performance_overlay", settings.show_performance_overlay );

            load_bool( root, "show_stage_debug_overlay", settings.show_stage_debug_overlay );

            load_bool( root, "show_judgement_detail", settings.show_judgement_detail );

            load_bool( root, "show_raw_energy_values", settings.show_raw_energy_values );

            load_bool( root, "show_stage_audio_status", settings.show_stage_audio_status );
        }

        void write_debug( SettingsWriter &writer, const GameSettings &settings )
        {
            writer.write( "show_performance_overlay", settings.show_performance_overlay );

            writer.write( "show_stage_debug_overlay", settings.show_stage_debug_overlay );

            writer.write( "show_judgement_detail", settings.show_judgement_detail );

            writer.write( "show_raw_energy_values", settings.show_raw_energy_values );

            writer.write( "show_stage_audio_status", settings.show_stage_audio_status );
        }
#endif

        void write_settings( SettingsWriter &writer, const GameSettings &settings )
        {
            writer.write( "version", GameSettings::file_version );
            write_graphics( writer, settings );
            write_game_data( writer, settings );
            write_audio( writer, settings );
            write_controller( writer, settings );
            write_interface( writer, settings );
            write_modifiers( writer, settings );
#if defined( _DEBUG )
            write_debug( writer, settings );
#endif
        }

        void write_profile( SettingsWriter &writer, const GameSettings &settings )
        {
            writer.write( "version", GameSettings::visual_profile_version );
            write_interface_styles( writer, settings );
            write_gameplay_visual( writer, settings.gameplay_visual );
            write_aim_settings( writer, settings );
            writer.write( "guard_notes_accept_dpad", settings.guard_notes_accept_dpad );

            writer.write( "chart_style", settings.chart_style );
            write_cone_colors( writer, settings );
        }

        bool write_settings_file( const GameSettings &settings,
            const std::filesystem::path &path,
            void ( *write )( SettingsWriter &, const GameSettings & ),
            std::string *error )
        {
            auto temporary = path;
            temporary += ".tmp";
            bool temporary_created = false;

            try
            {
                if( !path.parent_path().empty() )
                {
                    std::filesystem::create_directories( path.parent_path() );
                }

                std::ofstream stream( temporary, std::ios::binary | std::ios::trunc );

                if( !stream )
                {
                    throw std::runtime_error( "Could not open settings file for writing" );
                }

                temporary_created = true;
                SettingsWriter writer( stream );
                writer.begin_object();
                write( writer, settings );
                writer.end_object();
                stream << '\n';
                stream.close();

                if( !stream )
                {
                    throw std::runtime_error( "Could not finish writing settings file" );
                }

                std::filesystem::rename( temporary, path );

                return true;
            }
            catch( const std::exception &exception )
            {
                if( temporary_created )
                {
                    std::error_code remove_error;
                    std::filesystem::remove( temporary, remove_error );
                }

                if( error != nullptr )
                {
                    *error = exception.what();
                }

                return false;
            }
        }
    }

    void GameplayVisualStyle::reset_defaults()
    {
        *this = GameplayVisualStyle{};
    }

    float GameplayVisualStyle::snap_icon_scale( float value )
    {
        const float size = std::clamp( value, icon_scale_minimum, icon_scale_maximum );
        const float steps = std::floor( size / icon_scale_step + 0.0001f );
        return steps * icon_scale_step;
    }

    void GameSettings::reset_defaults()
    {
        *this = GameSettings{};
    }

    std::filesystem::path game_settings_path()
    {
        const char *base_path = SDL_GetBasePath();

        const auto root = base_path != nullptr && *base_path != '\0' ? utils::path_from_utf8( base_path )
                                                                     : std::filesystem::current_path();

        return root / "Save" / "settings.json";
    }

    std::filesystem::path visual_profile_directory()
    {
        return game_settings_path().parent_path() / "visual_profiles";
    }

    bool save_visual_profile( const GameSettings &settings, const std::filesystem::path &path, std::string *error )
    {
        return write_settings_file( settings, path, write_profile, error );
    }

    bool load_visual_profile( GameSettings &settings, const std::filesystem::path &path, std::string *error )
    {
        try
        {
            const utils::json::Value root = utils::json::load_file( path );
            const utils::json::Value *visual = root.find( "gameplay_visual" );

            if( !root.is_object() || visual == nullptr || !visual->is_object() )
            {
                throw std::runtime_error( "Invalid visual profile" );
            }

            GameSettings loaded = settings;
            load_interface_styles( root, loaded );

            if( root.require( "version" ).integer_or( 0 ) != GameSettings::visual_profile_version )
            {
                throw std::runtime_error( "Unsupported visual profile format" );
            }

            load_gameplay_visual( root, loaded.gameplay_visual );

            load_aim_settings( root, loaded );

            load_bool( root, "guard_notes_accept_dpad", loaded.guard_notes_accept_dpad );

            load_enum( root, "chart_style", loaded.chart_style,
                gamedata::ChartStyle::GitarooMan, gamedata::ChartStyle::GitarooManLives );

            load_cone_colors( root, loaded );

            settings = std::move( loaded );
            return true;
        }
        catch( const std::exception &exception )
        {
            if( error != nullptr )
            {
                *error = exception.what();
            }

            return false;
        }
    }

    bool load_game_settings( GameSettings &settings, std::string *error )
    {
        const std::filesystem::path path = game_settings_path();
        std::error_code status_error;

        if( !std::filesystem::is_regular_file( path, status_error ) )
        {
            return true;
        }

        try
        {
            const utils::json::Value root = utils::json::load_file( path );

            if( !root.is_object() )
            {
                throw std::runtime_error( "Settings root is not an object" );
            }

            if( root.require( "version" ).integer_or( 0 ) != GameSettings::file_version )
            {
                throw std::runtime_error( "Unsupported settings format" );
            }

            GameSettings loaded = settings;
            load_graphics( root, loaded );

            load_game_data( root, loaded );

            load_audio( root, loaded );

            load_controller( root, loaded );

            load_interface( root, loaded );

            load_modifiers( root, loaded );
#if defined( _DEBUG )
            load_debug( root, loaded );
#endif
            settings = std::move( loaded );
            return true;
        }
        catch( const std::exception &exception )
        {
            if( error != nullptr )
            {
                *error = exception.what();
            }

            return false;
        }
    }

    bool save_game_settings( const GameSettings &settings, std::string *error )
    {
        return write_settings_file( settings, game_settings_path(), write_settings, error );
    }
}
