#include "Utils/EnumUtil.hpp"
#include "GameSettings.hpp"
#include "Localization/Language.hpp"

#include "Utils/Json.hpp"
#include "Utils/FileUtil.hpp"

#include <cstdint>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace piko::managers
{
    namespace game_settings
    {
        constexpr std::int32_t settings_version = 1;
        constexpr std::int32_t visual_profile_version = 1;

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
                value = std::clamp( static_cast<std::int32_t>( entry->integer_or( value ) ), minimum, maximum );
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

        void write_color( std::ostream &stream, const std::array<float, 4> &color )
        {
            stream << '[' << color[ 0 ] << ", " << color[ 1 ] << ", " << color[ 2 ] << ", " << color[ 3 ] << ']';
        }

        void write_json_string( std::ostream &stream, const std::string &value )
        {
            constexpr char digits[] = "0123456789abcdef";
            stream << '"';

            for( const unsigned char character : value )
            {
                if( character == '"' || character == '\\' )
                {
                    stream << '\\' << static_cast<char>( character );
                }
                else if( character < 0x20 )
                {
                    stream << "\\u00" << digits[ character >> 4 ] << digits[ character & 0x0f ];
                }
                else
                {
                    stream << static_cast<char>( character );
                }
            }

            stream << '"';
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

        void write_interface_styles( std::ostream &stream, const GameSettings &settings )
        {
            stream << "  \"interface_styles\": {";
            bool first = true;

            for( const auto &[ language, style ] : settings.interface_styles )
            {
                if( !first )
                {
                    stream << ',';
                }

                first = false;
                write_json_string( stream, language );
                stream << ":{\"fonts\":[";

                for( std::size_t index = 0; index < style.fonts.size(); ++index )
                {
                    if( index > 0 )
                    {
                        stream << ',';
                    }

                    write_json_string( stream, style.fonts[ index ] );
                }

                stream << "]}";
            }

            stream << "},\n";
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

            if( const auto *entry = visual->find( "aim_style" ); entry != nullptr )
            {
                style.aim_style = entry->string_or( "default" );
            }

            if( const utils::json::Value *entry = visual->find( "guard_icon_set" ); entry != nullptr )
            {
                style.guard_icon_set = entry->string_or();
            }
            else
            {
                style.guard_icon_set.clear();
            }

            if( const auto *entry = visual->find( "note_style" ); entry != nullptr )
            {
                style.note_style = entry->string_or( "default" );
            }

            const auto *centre_set = visual->find( "centre_icon_set" );
            style.centre_icon_set = centre_set != nullptr ? centre_set->string_or() : std::string{};

            load_float( *visual, "center_nub_scale", style.center_nub_scale, 0.25f, 2.00f );
            load_float( *visual, "pressed_nub_scale", style.pressed_nub_scale, 0.25f, 2.00f );

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

        void load_aim_settings( const utils::json::Value &root, GameSettings &settings )
        {
            const auto *angles = root.find( "aim_snap_angles" );

            if( angles != nullptr && angles->is_object() )
            {
                load_float( *angles,
                    "easy",
                    settings.aim_snap_angles[ utils::enum_index( AimDifficulty::Easy ) ],
                    5.0f,
                    80.0f );

                load_float( *angles,
                    "normal",
                    settings.aim_snap_angles[ utils::enum_index( AimDifficulty::Normal ) ],
                    5.0f,
                    80.0f );

                load_float( *angles,
                    "master",
                    settings.aim_snap_angles[ utils::enum_index( AimDifficulty::Master ) ],
                    5.0f,
                    80.0f );
            }

            const auto *portable_angles = root.find( "psp_aim_snap_angles" );

            if( portable_angles != nullptr && portable_angles->is_object() )
            {
                load_float( *portable_angles,
                    "easy",
                    settings.psp_aim_snap_angles[ utils::enum_index( AimDifficulty::Easy ) ],
                    5.0f,
                    80.0f );

                load_float( *portable_angles,
                    "normal",
                    settings.psp_aim_snap_angles[ utils::enum_index( AimDifficulty::Normal ) ],
                    5.0f,
                    80.0f );

                load_float( *portable_angles,
                    "master",
                    settings.psp_aim_snap_angles[ utils::enum_index( AimDifficulty::Master ) ],
                    5.0f,
                    80.0f );
            }
        }

        void write_aim_settings( std::ostream &stream, const GameSettings &settings )
        {
            stream << "  \"aim_snap_angles\": {\n";
            stream << "    \"easy\": " << settings.snap_half_angle_for( AimDifficulty::Easy ) << ",\n";
            stream << "    \"normal\": " << settings.snap_half_angle_for( AimDifficulty::Normal ) << ",\n";
            stream << "    \"master\": " << settings.snap_half_angle_for( AimDifficulty::Master ) << "\n";
            stream << "  },\n";
            stream << "  \"psp_aim_snap_angles\": {\n";
            stream << "    \"easy\": " << settings.psp_aim_snap_angles[ utils::enum_index( AimDifficulty::Easy ) ]
                   << ",\n";

            stream << "    \"normal\": " << settings.psp_aim_snap_angles[ utils::enum_index( AimDifficulty::Normal ) ]
                   << ",\n";

            stream << "    \"master\": " << settings.psp_aim_snap_angles[ utils::enum_index( AimDifficulty::Master ) ]
                   << "\n";

            stream << "  },\n";
        }

        void write_gameplay_visual( std::ostream &stream, const GameplayVisualStyle &style )
        {
            stream << "  \"gameplay_visual\": {\n";
            stream << "    \"trace_scale\": " << style.trace_scale << ",\n";
            stream << "    \"note_scale\": " << style.note_scale << ",\n";
            stream << "    \"note_speed\": " << style.note_speed << ",\n";
            stream << "    \"guard_note_scale\": " << style.guard_note_scale << ",\n";
            stream << "    \"guard_hit_pop_scale\": " << style.guard_hit_pop_scale << ",\n";
            stream << "    \"guard_hit_fade_scale\": " << style.guard_hit_fade_scale << ",\n";
            stream << "    \"guard_rail_scale\": " << style.guard_rail_scale << ",\n";
            stream << "    \"aim_style\": ";
            write_json_string( stream, style.aim_style );
            stream << ",\n";
            stream << "    \"guard_icon_set\": ";
            write_json_string( stream, style.guard_icon_set );
            stream << ",\n";
            stream << "    \"centre_icon_set\": ";
            write_json_string( stream, style.centre_icon_set );
            stream << ",\n";

            stream << "    \"note_style\": ";
            write_json_string( stream, style.note_style );
            stream << ",\n";

            stream << "    \"center_nub_scale\": " << style.center_nub_scale << ",\n";
            stream << "    \"pressed_nub_scale\": " << style.pressed_nub_scale << ",\n";
            stream << "    \"cone_inner_diameter\": " << style.cone_inner_diameter << ",\n";
            stream << "    \"cone_length\": " << style.cone_length << ",\n";
            stream << "    \"cone_angle\": " << style.cone_angle << ",\n";
            stream << "    \"cone_line_width\": " << style.cone_line_width << ",\n";
            stream << "    \"cone_arrow_scale\": " << style.cone_arrow_scale << ",\n";
            stream << "    \"cone_arrow_width_scale\": " << style.cone_arrow_width_scale << ",\n";
            stream << "    \"cone_wing_spread_scale\": " << style.cone_wing_spread_scale << ",\n";
            stream << "    \"cone_wing_scale\": " << style.cone_wing_scale << ",\n";
            stream << "    \"cone_wing_width_scale\": " << style.cone_wing_width_scale << ",\n";
            stream << "    \"arrow_offset_x\": " << style.arrow_offset_x << ",\n";
            stream << "    \"wing_offset_x\": " << style.wing_offset_x << ",\n";
            stream << "    \"trace_idle_color\": ";
            write_color( stream, style.trace_idle_color );
            stream << ",\n    \"trace_locked_color\": ";
            write_color( stream, style.trace_locked_color );
            stream << ",\n    \"trace_lost_color\": ";
            write_color( stream, style.trace_lost_color );
            stream << ",\n    \"note_color\": ";
            write_color( stream, style.note_color );
            stream << ",\n    \"held_note_color\": ";
            write_color( stream, style.held_note_color );
            stream << ",\n    \"guard_rail_color\": ";
            write_color( stream, style.guard_rail_color );
            stream << ",\n    \"note_body_color\": ";
            write_color( stream, style.note_body_color );
            stream << "\n  },\n";
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
        try
        {
            std::filesystem::create_directories( path.parent_path() );
            auto temporary = path;
            temporary += ".tmp";
            std::ofstream stream( temporary, std::ios::binary | std::ios::trunc );

            if( !stream )
            {
                throw std::runtime_error( "Could not open visual profile for writing" );
            }

            stream << std::fixed << std::setprecision( 6 );
            stream << "{\n  \"version\": " << game_settings::visual_profile_version << ",\n";
            game_settings::write_interface_styles( stream, settings );
            game_settings::write_gameplay_visual( stream, settings.gameplay_visual );
            game_settings::write_aim_settings( stream, settings );
            stream << "  \"guard_notes_accept_dpad\": " << ( settings.guard_notes_accept_dpad ? "true" : "false" )
                   << ",\n";

            stream << "  \"chart_style\": " << utils::to_int( settings.chart_style ) << ",\n";
            stream << "  \"view_cone_on_target_line_color\": ";
            game_settings::write_color( stream, settings.view_cone_on_target_line_color );
            stream << ",\n  \"view_cone_off_target_line_color\": ";
            game_settings::write_color( stream, settings.view_cone_off_target_line_color );
            stream << ",\n  \"view_cone_on_target_fill_color\": ";
            game_settings::write_color( stream, settings.view_cone_on_target_fill_color );
            stream << ",\n  \"view_cone_off_target_fill_color\": ";
            game_settings::write_color( stream, settings.view_cone_off_target_fill_color );
            stream << "\n}\n";
            stream.close();

            if( !stream )
            {
                throw std::runtime_error( "Could not finish writing visual profile" );
            }

            std::error_code rename_error;
            std::filesystem::rename( temporary, path, rename_error );

            if( rename_error )
            {
                auto backup = path;
                backup += ".backup";
                std::filesystem::copy_file( path, backup, std::filesystem::copy_options::overwrite_existing );
                std::filesystem::remove( path );
                std::filesystem::rename( temporary, path, rename_error );

                if( rename_error )
                {
                    std::filesystem::rename( backup, path );
                    throw std::runtime_error( "Could not replace visual profile" );
                }

                std::filesystem::remove( backup );
            }

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
            game_settings::load_interface_styles( root, loaded );

            if( root.require( "version" ).integer_or( 0 ) != game_settings::visual_profile_version )
            {
                throw std::runtime_error( "Unsupported visual profile format" );
            }

            game_settings::load_gameplay_visual( root, loaded.gameplay_visual );
            game_settings::load_aim_settings( root, loaded );

            game_settings::load_bool( root, "guard_notes_accept_dpad", loaded.guard_notes_accept_dpad );
            std::int32_t chart_style = utils::to_int( loaded.chart_style );
            game_settings::load_int( root, "chart_style", chart_style, 0, 1 );
            loaded.chart_style = utils::to_enum<gamedata::ChartStyle>( chart_style );
            game_settings::load_color( root, "view_cone_on_target_line_color", loaded.view_cone_on_target_line_color );
            game_settings::load_color( root,
                "view_cone_off_target_line_color",
                loaded.view_cone_off_target_line_color );

            game_settings::load_color( root, "view_cone_on_target_fill_color", loaded.view_cone_on_target_fill_color );
            game_settings::load_color( root,
                "view_cone_off_target_fill_color",
                loaded.view_cone_off_target_fill_color );

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

            if( root.require( "version" ).integer_or( 0 ) != game_settings::settings_version )
            {
                throw std::runtime_error( "Unsupported settings format" );
            }

#if defined( _DEBUG )
            game_settings::load_bool( root, "show_performance_overlay", settings.show_performance_overlay );
            game_settings::load_bool( root, "show_stage_debug_overlay", settings.show_stage_debug_overlay );
            game_settings::load_bool( root, "show_judgement_detail", settings.show_judgement_detail );
            game_settings::load_bool( root, "show_raw_energy_values", settings.show_raw_energy_values );
            game_settings::load_bool( root, "show_stage_audio_status", settings.show_stage_audio_status );
#endif
            game_settings::load_bool( root, "msaa_enabled", settings.msaa_enabled );
            game_settings::load_int( root, "msaa_samples", settings.msaa_samples, 0, 16 );
            game_settings::load_int( root, "window_width", settings.window_width, 640, 7680 );
            game_settings::load_int( root, "window_height", settings.window_height, 360, 4320 );
            game_settings::load_int( root, "render_resolution_percent", settings.render_resolution_percent, 25, 200 );

            game_settings::load_bool( root, "per_pixel_lighting", settings.per_pixel_lighting );

            std::int32_t filtering = utils::to_int( settings.texture_filtering_mode );
            game_settings::load_int( root, "texture_filtering_mode", filtering, 0, 3 );
            settings.texture_filtering_mode = utils::to_enum<TextureFilteringMode>( filtering );

            game_settings::load_float( root, "texture_anisotropy", settings.texture_anisotropy, 1.0f, 16.0f );
            std::int32_t bloom = utils::to_int( settings.bloom_preset );
            game_settings::load_int( root, "bloom_preset", bloom, 0, utils::to_int( BloomPreset::Count ) - 1 );
            settings.bloom_preset = utils::to_enum<BloomPreset>( bloom );

            game_settings::load_bool( root, "crt_filter_enabled", settings.crt_filter_enabled );
            game_settings::load_float( root, "crt_scanline_strength", settings.crt_scanline_strength, 0.0f, 1.0f );
            game_settings::load_float( root, "crt_vignette_strength", settings.crt_vignette_strength, 0.0f, 1.0f );
            game_settings::load_float( root, "crt_noise_strength", settings.crt_noise_strength, 0.0f, 1.0f );
            std::int32_t chart_style = utils::to_int( settings.chart_style );
            game_settings::load_int( root, "chart_style", chart_style, 0, 1 );
            settings.chart_style = utils::to_enum<gamedata::ChartStyle>( chart_style );

            std::int32_t language = utils::to_int( settings.language );
            game_settings::load_int( root, "language", language, 0, 4 );
            settings.language = utils::to_enum<gamedata::GameLanguage>( language );

            if( const auto *entry = root.find( "ui_language" ); entry != nullptr && entry->is_string() )
            {
                settings.ui_language = entry->string();
            }

            game_settings::load_interface_styles( root, settings );

            game_settings::load_int( root, "audio_import_compression", settings.audio_import_compression, 0, 1 );
            game_settings::load_bool( root, "play_movies", settings.play_movies );
            game_settings::load_bool( root, "stereo_audio", settings.stereo_audio );
            game_settings::load_bool( root, "background_fx_enabled", settings.background_fx_enabled );
            game_settings::load_int( root, "instrument_routing", settings.instrument_routing, 0, 2 );

            std::int32_t instrument_playback = utils::to_int( settings.instrument_playback );
            game_settings::load_int( root,
                "instrument_playback",
                instrument_playback,
                0,
                utils::to_int( runtime::audio::InstrumentPlaybackMode::Count ) - 1 );

            settings.instrument_playback =
                utils::to_enum<runtime::audio::InstrumentPlaybackMode>( instrument_playback );

            std::int32_t orientation = utils::to_int( settings.controller_orientation );
            game_settings::load_int( root, "controller_orientation", orientation, 0, 2 );
            settings.controller_orientation = controller_orientation_from_index( orientation );

            if( const utils::json::Value *device = root.find( "input_device_name" ); device != nullptr )
            {
                settings.input_device_name = device->string_or();
            }

            game_settings::load_bool( root, "mouse_controls_enabled", settings.mouse_controls_enabled );
            game_settings::load_bool( root, "guard_notes_accept_dpad", settings.guard_notes_accept_dpad );
            game_settings::load_bool( root, "always_retrigger_player_strum", settings.always_retrigger_player_strum );
            game_settings::load_int( root, "duet_chart_mode", settings.duet_chart_mode, 0, 3 );
            game_settings::load_bool( root, "phrase_tube_wobble", settings.phrase_tube_wobble );
            game_settings::load_bool( root, "phrase_tube_gradient_pulse", settings.phrase_tube_gradient_pulse );
            game_settings::load_bool( root, "stage_bpm_edge_glow", settings.stage_bpm_edge_glow );
            game_settings::load_bool( root, "no_death_mode", settings.no_death_mode );
            game_settings::load_bool( root, "only_aim_mode", settings.only_aim_mode );
            game_settings::load_bool( root, "all_sections_mode", settings.all_sections_mode );
            game_settings::load_bool( root, "mirror_mode", settings.mirror_mode );
            game_settings::load_float( root,
                "player_damage_multiplier",
                settings.player_damage_multiplier,
                0.0f,
                4.0f );

            game_settings::load_float( root, "enemy_damage_multiplier", settings.enemy_damage_multiplier, 0.0f, 4.0f );
            game_settings::load_int( root, "phrase_note_color_index", settings.phrase_note_color_index, 0, 7 );
            game_settings::load_int( root,
                "phrase_note_held_color_index",
                settings.phrase_note_held_color_index,
                0,
                7 );

            game_settings::load_gameplay_visual( root, settings.gameplay_visual );
            game_settings::load_aim_settings( root, settings );

            game_settings::load_color( root,
                "view_cone_on_target_line_color",
                settings.view_cone_on_target_line_color );

            game_settings::load_color( root,
                "view_cone_off_target_line_color",
                settings.view_cone_off_target_line_color );

            game_settings::load_color( root,
                "view_cone_on_target_fill_color",
                settings.view_cone_on_target_fill_color );

            game_settings::load_color( root,
                "view_cone_off_target_fill_color",
                settings.view_cone_off_target_fill_color );

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
        const std::filesystem::path path = game_settings_path();

        try
        {
            std::error_code directory_error;
            std::filesystem::create_directories( path.parent_path(), directory_error );

            if( directory_error )
            {
                throw std::runtime_error( directory_error.message() );
            }

            std::ofstream stream( path, std::ios::binary | std::ios::trunc );

            if( !stream )
            {
                throw std::runtime_error( "Could not open settings file for writing" );
            }

            stream << std::fixed << std::setprecision( 6 );
            stream << "{\n";
            stream << "  \"version\": " << game_settings::settings_version << ",\n";
#if defined( _DEBUG )
            stream << "  \"show_performance_overlay\": " << ( settings.show_performance_overlay ? "true" : "false" )
                   << ",\n";

            stream << "  \"show_stage_debug_overlay\": " << ( settings.show_stage_debug_overlay ? "true" : "false" )
                   << ",\n";

            stream << "  \"show_judgement_detail\": " << ( settings.show_judgement_detail ? "true" : "false" ) << ",\n";
            stream << "  \"show_raw_energy_values\": " << ( settings.show_raw_energy_values ? "true" : "false" )
                   << ",\n";

            stream << "  \"show_stage_audio_status\": " << ( settings.show_stage_audio_status ? "true" : "false" )
                   << ",\n";

#endif

            stream << "  \"msaa_enabled\": " << ( settings.msaa_enabled ? "true" : "false" ) << ",\n";
            stream << "  \"msaa_samples\": " << settings.msaa_samples << ",\n";
            stream << "  \"window_width\": " << settings.window_width << ",\n";
            stream << "  \"window_height\": " << settings.window_height << ",\n";
            stream << "  \"render_resolution_percent\": " << settings.render_resolution_percent << ",\n";
            stream << "  \"per_pixel_lighting\": " << ( settings.per_pixel_lighting ? "true" : "false" ) << ",\n";
            stream << "  \"texture_filtering_mode\": " << utils::to_int( settings.texture_filtering_mode ) << ",\n";

            stream << "  \"texture_anisotropy\": " << settings.texture_anisotropy << ",\n";
            stream << "  \"bloom_preset\": " << utils::to_int( settings.bloom_preset ) << ",\n";
            stream << "  \"crt_filter_enabled\": " << ( settings.crt_filter_enabled ? "true" : "false" ) << ",\n";
            stream << "  \"crt_scanline_strength\": " << settings.crt_scanline_strength << ",\n";
            stream << "  \"crt_vignette_strength\": " << settings.crt_vignette_strength << ",\n";
            stream << "  \"crt_noise_strength\": " << settings.crt_noise_strength << ",\n";
            stream << "  \"chart_style\": " << utils::to_int( settings.chart_style ) << ",\n";
            stream << "  \"language\": " << utils::to_int( settings.language ) << ",\n";
            stream << "  \"ui_language\": ";
            game_settings::write_json_string( stream, settings.ui_language );
            stream << ",\n";
            game_settings::write_interface_styles( stream, settings );

            stream << "  \"audio_import_compression\": " << settings.audio_import_compression << ",\n";
            stream << "  \"play_movies\": " << ( settings.play_movies ? "true" : "false" ) << ",\n";
            stream << "  \"stereo_audio\": " << ( settings.stereo_audio ? "true" : "false" ) << ",\n";
            stream << "  \"background_fx_enabled\": " << ( settings.background_fx_enabled ? "true" : "false" ) << ",\n";
            stream << "  \"instrument_routing\": " << settings.instrument_routing << ",\n";

            stream << "  \"instrument_playback\": " << utils::to_int( settings.instrument_playback ) << ",\n";
            stream << "  \"controller_orientation\": " << utils::to_int( settings.controller_orientation ) << ",\n";

            stream << "  \"input_device_name\": ";
            game_settings::write_json_string( stream, settings.input_device_name );
            stream << ",\n";
            stream << "  \"mouse_controls_enabled\": " << ( settings.mouse_controls_enabled ? "true" : "false" )
                   << ",\n";

            stream << "  \"guard_notes_accept_dpad\": " << ( settings.guard_notes_accept_dpad ? "true" : "false" )
                   << ",\n";

            stream << "  \"always_retrigger_player_strum\": "
                   << ( settings.always_retrigger_player_strum ? "true" : "false" ) << ",\n";

            stream << "  \"duet_chart_mode\": " << settings.duet_chart_mode << ",\n";
            stream << "  \"phrase_tube_wobble\": " << ( settings.phrase_tube_wobble ? "true" : "false" ) << ",\n";
            stream << "  \"phrase_tube_gradient_pulse\": " << ( settings.phrase_tube_gradient_pulse ? "true" : "false" )
                   << ",\n";

            stream << "  \"stage_bpm_edge_glow\": " << ( settings.stage_bpm_edge_glow ? "true" : "false" ) << ",\n";
            stream << "  \"no_death_mode\": " << ( settings.no_death_mode ? "true" : "false" ) << ",\n";
            stream << "  \"only_aim_mode\": " << ( settings.only_aim_mode ? "true" : "false" ) << ",\n";
            stream << "  \"all_sections_mode\": " << ( settings.all_sections_mode ? "true" : "false" ) << ",\n";
            stream << "  \"mirror_mode\": " << ( settings.mirror_mode ? "true" : "false" ) << ",\n";
            stream << "  \"player_damage_multiplier\": " << settings.player_damage_multiplier << ",\n";
            stream << "  \"enemy_damage_multiplier\": " << settings.enemy_damage_multiplier << ",\n";
            stream << "  \"phrase_note_color_index\": " << settings.phrase_note_color_index << ",\n";
            stream << "  \"phrase_note_held_color_index\": " << settings.phrase_note_held_color_index << ",\n";
            game_settings::write_gameplay_visual( stream, settings.gameplay_visual );
            game_settings::write_aim_settings( stream, settings );
            stream << "  \"view_cone_on_target_line_color\": ";
            game_settings::write_color( stream, settings.view_cone_on_target_line_color );
            stream << ",\n  \"view_cone_off_target_line_color\": ";
            game_settings::write_color( stream, settings.view_cone_off_target_line_color );
            stream << ",\n  \"view_cone_on_target_fill_color\": ";
            game_settings::write_color( stream, settings.view_cone_on_target_fill_color );
            stream << ",\n  \"view_cone_off_target_fill_color\": ";
            game_settings::write_color( stream, settings.view_cone_off_target_fill_color );
            stream << "\n}\n";

            if( !stream )
            {
                throw std::runtime_error( "Could not finish writing settings file" );
            }

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
}
