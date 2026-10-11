#include "Localization/Language.hpp"
#include "StageSelectModule.hpp"

#include "App/piko.hpp"
#include "Image/PngWriter.hpp"
#include "FileTypes/CHC/CHCJson.hpp"
#include "GameData/AlbumFlow.hpp"
#include "GameData/SaveData.hpp"
#include "Modules/Loading/LoadingModule.hpp"
#include "Modules/MainMenu/MainMenuModule.hpp"
#include "Modules/MainMenu/TitleScreenBackdrop.hpp"
#include "Modules/Movie/MovieModule.hpp"
#include "Modules/Stage/StageModule.hpp"
#include "Modules/Stage/Stage3DRenderer.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/RenderUtil.hpp"
#include "Rendering/MenuTheme.hpp"
#include "Runtime/Stage/ResultAtlas.hpp"
#include "Utils/Json.hpp"
#include "Utils/FileUtil.hpp"

#include <SDL3/SDL.h>
#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <iomanip>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <sstream>
#include <span>
#include <unordered_set>
#include <utility>

namespace piko::modules
{
    namespace
    {
        constexpr auto stage_select_panel = rendering::menu_theme::card;
        constexpr auto stage_select_border = rendering::menu_theme::separator;
        constexpr auto stage_select_selected = rendering::menu_theme::selected_card;

        struct StagePreviewTiming
        {
            float global_frame = 0.0f;
            std::size_t section_index = 0;
            float section_frame = 0.0f;
            float tempo = 120.0f;
            float portable_animation_frame = 0.0f;
        };

        std::filesystem::path preview_timings_path( const App &app )
        {
            return app.game_data().resource_root() / "ui/stage_select/preview_timings.json";
        }

        std::vector<StagePreviewTiming> stage_preview_timings( const App &app,
            const gamedata::PlayableStageEntry &stage )
        {
            std::vector<StagePreviewTiming> timings;

            try
            {
                const auto root = utils::json::load_file( preview_timings_path( app ) );

                if( root.require( "version" ).integer_or( 0 ) != 1 )
                {
                    throw std::runtime_error( "Unsupported stage preview definitions" );
                }

                for( const auto &definition : root.require( "stages" ).array() )
                {
                    if( definition.require( "stage" ).integer_or( -1 ) != stage.stage_number ||
                        definition.require( "visuals" ).string() != ( stage.portable_visuals ? "PSP" : "PS2" ) )
                    {
                        continue;
                    }

                    for( const auto &value : definition.require( "candidates" ).array() )
                    {
                        const auto index = value.require( "section_index" ).integer_or( -1 );

                        const auto &tempo = value.require( "tempo" );
                        const bool stage_tempo = tempo.is_string() && tempo.string() == "stage";
                        StagePreviewTiming timing{
                            static_cast<float>( value.require( "global_frame" ).number() ),
                            static_cast<std::size_t>( std::max<std::int64_t>( 0, index ) ),
                            static_cast<float>( value.require( "section_frame" ).number() ),
                            stage_tempo ? stage.tempo : static_cast<float>( tempo.number() ),
                            static_cast<float>( value.require( "portable_animation_frame" ).number() )
                        };

                        if( index < 0 || !std::isfinite( timing.global_frame ) || timing.global_frame < 0.0f ||
                            !std::isfinite( timing.section_frame ) || timing.section_frame < 0.0f ||
                            !std::isfinite( timing.tempo ) || ( !stage_tempo && timing.tempo <= 0.0f ) ||
                            !std::isfinite( timing.portable_animation_frame ) ||
                            timing.portable_animation_frame < 0.0f )
                        {
                            throw std::runtime_error( "Invalid stage preview timing" );
                        }

                        timings.push_back( timing );
                    }

                    break;
                }

                if( timings.empty() )
                {
                    throw std::runtime_error(
                        "Stage preview is not defined for stage " + std::to_string( stage.stage_number ) );
                }
            }
            catch( const std::exception &error )
            {
                std::cerr << "[Piko Preview] " << error.what() << '\n';
                timings.clear();
            }

            return timings;
        }

        void draw_stage_select_panel( rendering::Renderer &renderer,
            const rendering::LogicalViewport &layout,
            const rendering::Rect &bounds,
            const rendering::Color &fill = stage_select_panel,
            const rendering::Color &border = stage_select_border,
            float thickness = 0.0f )
        {
            renderer.draw_filled_rect( layout.rect( bounds ), fill );

            if( thickness > 0.0f )
            {
                renderer.draw_filled_rect( layout.rect( bounds.x, bounds.y, bounds.w, thickness ), border );
                renderer.draw_filled_rect(
                    layout.rect( bounds.x, bounds.y + bounds.h - thickness, bounds.w, thickness ),
                    border );

                renderer.draw_filled_rect( layout.rect( bounds.x, bounds.y, thickness, bounds.h ), border );
                renderer.draw_filled_rect(
                    layout.rect( bounds.x + bounds.w - thickness, bounds.y, thickness, bounds.h ),
                    border );
            }
        }

        void draw_choice_card( rendering::Renderer &renderer,
            const rendering::LogicalViewport &layout,
            const rendering::Rect &bounds,
            bool active,
            bool focused,
            bool available = true )
        {
            const auto fill = !available ? rendering::menu_theme::disabled_card
                              : active   ? rendering::menu_theme::active_card
                              : focused  ? rendering::menu_theme::focused_card
                                         : rendering::menu_theme::card;

            draw_stage_select_panel( renderer,
                layout,
                bounds,
                fill,
                rendering::menu_theme::focus_accent,
                focused ? 1.0f : 0.0f );
        }

        rendering::Color clear_rank_color( std::int32_t rank )
        {
            // Match the letters in the result atlas (S, A, B, C, D).
            switch( rank )
            {
                case 5:
                    return rendering::color_from_rgb( 0xFF8400u );
                case 1:
                    return rendering::color_from_rgb( 0xEF78AAu );
                case 2:
                    return rendering::color_from_rgb( 0xC580F0u );
                case 3:
                    return rendering::color_from_rgb( 0x87F1B9u );
                case 4:
                    return rendering::color_from_rgb( 0x8CE1F0u );
                default:
                    return rendering::color_from_rgb( 0xCDBDDDu );
            }
        }
    }

    const char *StageSelectModule::category_label( StageCategory category )
    {
        switch( category )
        {
            case StageSelectModule::StageCategory::SingleEasy:
                return localization::text( "@ui.easy_upper_label" );
            case StageSelectModule::StageCategory::SingleNormal:
                return localization::text( "@ui.normal_upper_label" );
            case StageSelectModule::StageCategory::SingleMaster:
                return localization::text( "@ui.master_upper_label" );
            case StageSelectModule::StageCategory::Multiplayer:
                return localization::text( "@ui.multi_upper" );
            case StageSelectModule::StageCategory::All:
                return localization::text( "@ui.all_stages_upper" );
        }

        return localization::text( "@ui.stages_upper_label" );
    }

    bool StageSelectModule::stage_matches_category( const gamedata::PlayableStageEntry &stage, StageCategory category )
    {
        if( stage.mode == gamedata::PlayMode::Tutorial )
        {
            return !stage.bonus_content && stage.chart_style == gamedata::ChartStyle::GitarooMan &&
                   !stage.portable_visuals &&
                   ( category == StageCategory::SingleEasy || category == StageCategory::SingleNormal ||
                       category == StageCategory::All );
        }

        switch( category )
        {
            case StageSelectModule::StageCategory::SingleEasy:
                return ( !stage.bonus_content || stage.stage_number == 11 || stage.stage_number == 12 ) &&
                       stage.mode == gamedata::PlayMode::SinglePlayer &&
                       gamedata::difficulty_from_chc_path( stage.chc_path ) == "Easy";
            case StageSelectModule::StageCategory::SingleNormal:
                return ( !stage.bonus_content || stage.stage_number == 11 || stage.stage_number == 12 ) &&
                       stage.mode == gamedata::PlayMode::SinglePlayer &&
                       gamedata::difficulty_from_chc_path( stage.chc_path ) == "Normal";
            case StageSelectModule::StageCategory::SingleMaster:
                return ( !stage.bonus_content || stage.stage_number == 11 || stage.stage_number == 12 ) &&
                       stage.mode == gamedata::PlayMode::SinglePlayer &&
                       gamedata::difficulty_from_chc_path( stage.chc_path ) == "Master";
            case StageSelectModule::StageCategory::Multiplayer:
                return !stage.bonus_content && stage.mode == gamedata::PlayMode::Multiplayer;
            case StageSelectModule::StageCategory::All:
                return true;
        }

        return true;
    }

    std::size_t StageSelectModule::category_count( const std::vector<gamedata::PlayableStageEntry> &stages,
        StageCategory category )
    {
        return static_cast<std::size_t>( std::count_if( stages.begin(),
            stages.end(),
            [ category ]( const gamedata::PlayableStageEntry &stage )
            {
                return stage_matches_category( stage, category );
            } ) );
    }

    StageSelectModule::StageCategory StageSelectModule::first_non_empty_category(
        const std::vector<gamedata::PlayableStageEntry> &stages )
    {
        for( const StageSelectModule::StageCategory category : stage_categories_ )
        {
            if( category_count( stages, category ) > 0 )
            {
                return category;
            }
        }

        return StageSelectModule::StageCategory::SingleNormal;
    }

    const char *StageSelectModule::rank_label( std::int32_t rank )
    {
        switch( rank )
        {
            case 5:
                return "S";
            case 1:
                return "A";
            case 2:
                return "B";
            case 3:
                return "C";
            case 4:
                return "D";
            default:
                return "--";
        }
    }

    gamedata::SaveProgressMode StageSelectModule::progress_mode_for_stage( const gamedata::PlayableStageEntry &stage )
    {
        const std::string difficulty = gamedata::difficulty_from_chc_path( stage.chc_path );

        if( difficulty == "Easy" )
        {
            return gamedata::SaveProgressMode::Easy;
        }

        return difficulty == "Master" ? gamedata::SaveProgressMode::Master : gamedata::SaveProgressMode::Normal;
    }

    std::string StageSelectModule::score_text( std::uint32_t score )
    {
        char text[ 24 ]{};
        std::snprintf( text, sizeof( text ), "%07u", score );
        return text;
    }

    StageSelectModule::StageSelectModule( StageCategory initial_category, LaunchMode launch_mode )
        : initial_category_( initial_category ), launch_mode_( launch_mode )
    {
    }

    void StageSelectModule::enter( App &app )
    {
        navigation_ = &app.menu_navigation();

        elapsed_seconds_ = 0.0;

        preview_selection_seconds_ = 0.0;

        audio_preview_stage_number_ = -1;

        audio_preview_requested_stage_number_ = -1;

        audio_preview_elapsed_seconds_ = 0.0;

        audio_preview_finished_ = false;
        pending_preview_cue_.clear();
        unavailable_previews_.clear();
        loaded_preview_audio_banks_.clear();
        selected_stage_ = 0;

        first_visible_stage_ = 0;

        wheel_scroll_fraction_ = 0.0f;

        scrollbar_dragging_ = false;

        active_category_ = initial_category_;

        selecting_practice_section_ = false;

        practice_stage_ = {};

        practice_song_ = {};
        practice_sections_.clear();
        selected_practice_section_ = 0;

        practice_preview_bank_loaded_ = false;
        all_stages_.clear();
        stages_.clear();
        stage_hit_rects_.clear();
        action_message_.clear();
        const std::filesystem::path font_path = utils::find_resource_file( app.game_data().application_root(),
            std::filesystem::path( "fonts" ) / "Anak Paud.ttf" );

        localization::load_menu_font( stage_select_font_, font_path );

        load_stage_select_assets( app );
        load_song_titles( app );
        load_rank_assets( app );
        refresh_chart_style_availability( app );
        focus_ = FocusArea::StageList;
        app.settings().chart_style = app.game_data().resolve_chart_style( app.settings().chart_style );
        wave_background_.initialize( app, "ui/stage_select/wave_background.json" );
        circle_texture_ = TitleScreenBackdrop::create_circle_texture( app, "stage-select/circle" );
        load_stage_catalog( app );
        focused_category_ = active_category_;

        load_preview_vignette( app );
        initialize_audio( app );
    }

    void StageSelectModule::leave( App &app )
    {
        if( !stages_.empty() )
        {
            navigation_->remember( menu_locations_[ utils::enum_index( active_category_ ) ], selected_stage_ );
        }

        stop_stage_audio_preview( app, false );

        for( const auto &[ key, handle ] : preview_textures_ )
        {
            app.texture_manager().unload( handle );
        }

        preview_textures_.clear();

        for( const auto &[ key, handle ] : text_textures_ )
        {
            app.texture_manager().unload( handle );
        }

        text_textures_.clear();
        stage_song_titles_.clear();
        stage_select_font_.clear();
        app.texture_manager().unload( circle_texture_ );
        circle_texture_ = {};
        app.texture_manager().unload( preview_vignette_ );
        preview_vignette_ = {};
        wave_background_.shutdown();
        app.audio().release_channel( practice_preview_channel_ );
        app.audio().release_channel( sfx_channel_ );
        app.audio().release_channel( bgm_channel_ );
        practice_preview_channel_ = {};

        sfx_channel_ = {};

        bgm_channel_ = {};
        unload_stage_select_assets( app );
    }

    void StageSelectModule::initialize_audio( App &app )
    {
        const std::filesystem::path audio_path = app.game_data().main_menu_audio_path();

        if( !audio_path.empty() )
        {
            app.audio().load_audio_bank( audio_path );
        }

        bgm_channel_ = app.audio().claim_channel( "Stage Select BGM" );

        sfx_channel_ = app.audio().claim_channel( "Stage Select SE" );

        practice_preview_channel_ = app.audio().claim_channel( "Practice Preview" );

        if( bgm_channel_ && app.audio().has_subsong( "BGM_B2" ) )
        {
            app.audio().play_subsong( bgm_channel_, "BGM_B2", true );
        }
    }

    void StageSelectModule::play_menu_sound( App &app, const char *name )
    {
        if( sfx_channel_ && app.audio().has_subsong( name ) )
        {
            app.audio().play_subsong( sfx_channel_, name, false );
        }
    }

    void StageSelectModule::load_stage_select_assets( App &app )
    {
        unload_stage_select_assets( app );
        const std::filesystem::path atlas_path = app.game_data().resolve_runtime_asset( "UI/STAGE_SELECT/atlas.json" );

        if( atlas_path.empty() )
        {
            return;
        }

        try
        {
            const utils::json::Value root = utils::json::load_file( atlas_path );

            if( root.require( "format" ).string() != "piko-stage-select-atlas" ||
                root.require( "version" ).integer_or( 0 ) != 1 )
            {
                return;
            }

            const auto read_rect = []( const utils::json::Value &value )
            {
                const auto &numbers = value.array();

                if( numbers.size() != 4u )
                {
                    throw std::runtime_error( "Stage-select atlas rectangle must contain four values" );
                }

                return rendering::Rect{
                    static_cast<float>( numbers[ 0 ].number() ),
                    static_cast<float>( numbers[ 1 ].number() ),
                    static_cast<float>( numbers[ 2 ].number() ),
                    static_cast<float>( numbers[ 3 ].number() )
                };
            };

            const utils::json::Value &textures = root.require( "textures" );
            const std::filesystem::path portraits_path =
                app.game_data().resolve_runtime_asset( textures.require( "portraits" ).string() );

            if( portraits_path.empty() )
            {
                return;
            }

            stage_select_portraits_texture_ =
                app.texture_manager().load_png( "stage-select/portraits", portraits_path );

            const utils::json::Value &rects = root.require( "rects" );

            const auto &portraits = rects.require( "portraits" ).array();

            if( portraits.size() != stage_select_portrait_rects_.size() )
            {
                throw std::runtime_error( "Stage-select atlas must contain ten portrait rectangles" );
            }

            for( std::size_t index = 0; index < portraits.size(); ++index )
            {
                stage_select_portrait_rects_[ index ] = read_rect( portraits[ index ] );
            }
        }
        catch( const std::exception & )
        {
            unload_stage_select_assets( app );
        }
    }

    void StageSelectModule::load_preview_vignette( App &app )
    {
        std::vector<std::uint8_t> pixels( vignette_width_ * vignette_height_ * 4, 0 );

        for( std::int32_t y = 0; y < vignette_height_; ++y )
        {
            for( std::int32_t x = 0; x < vignette_width_; ++x )
            {
                const float nx = ( static_cast<float>( x ) + 0.5f ) / vignette_width_ * 2.0f - 1.0f;
                const float ny = ( static_cast<float>( y ) + 0.5f ) / vignette_height_ * 2.0f - 1.0f;
                const float radius = std::sqrt( nx * nx + ny * ny );
                const float fade = std::clamp( radius - 0.30f, 0.0f, 1.0f );
                const float alpha = fade * fade * ( 3.0f - 2.0f * fade ) * vignette_opacity_;
                pixels[ ( y * vignette_width_ + x ) * 4 + 3 ] =
                    static_cast<std::uint8_t>( std::lround( alpha * 255.0f ) );
            }
        }

        preview_vignette_ = app.texture_manager().create_rgba( "stage-select/preview-vignette",
            pixels.data(),
            vignette_width_,
            vignette_height_ );
    }

    void StageSelectModule::load_song_titles( const App &app )
    {
        stage_song_titles_.clear();

        const auto path = app.game_data().resource_root() / "ui/stage_select/songs.json";

        if( !utils::regular_file( path ) )
        {
            return;
        }

        try
        {
            const auto root = utils::json::load_file( path );

            for( const auto &stage : root.require( "stages" ).array() )
            {
                const auto number = stage.require( "stage" ).integer_or( -1 );

                if( number < 1 || number > 99 )
                {
                    continue;
                }

                auto &titles = stage_song_titles_[ static_cast<std::int32_t>( number ) ];

                const auto &songs = stage.require( "songs" ).array();

                for( std::size_t index = 0; index < std::min( songs.size(), titles.size() ); ++index )
                {
                    const auto &key = songs[ index ].require( "key" ).string();

                    const auto translated = localization::translate( key );
                    titles[ index ] = translated == key ? songs[ index ].require( "title" ).string() : translated;
                }
            }
        }
        catch( const std::exception &error )
        {
            stage_song_titles_.clear();
            std::cerr << "[Piko UI] Could not load stage songs: " << error.what() << '\n';
        }
    }

    void StageSelectModule::load_rank_assets( App &app )
    {
        // Use rank sprites from the result sheet.
        for( const char *atlas_relative : { "UI/RESULT/PS2/atlas.json", "UI/RESULT/PSP/atlas.json" } )
        {
            const std::filesystem::path atlas_path = app.game_data().resolve_runtime_asset( atlas_relative );

            if( atlas_path.empty() )
            {
                continue;
            }

            try
            {
                const runtime::stage::ResultAtlas atlas = runtime::stage::load_result_atlas( atlas_path );
                const std::filesystem::path texture_path =
                    app.game_data().resolve_runtime_asset( runtime::stage::result_atlas_stage_path( atlas, 1 ) );

                if( texture_path.empty() )
                {
                    continue;
                }

                rank_texture_ = app.texture_manager().load_png( "stage-select/ranks", texture_path );

                if( rank_texture_ )
                {
                    rank_rects_ = atlas.ranks;
                    return;
                }
            }
            catch( const std::exception & )
            {
                // Use text if the rank sprite is missing
            }
        }
    }

    void StageSelectModule::unload_stage_select_assets( App &app )
    {
        app.texture_manager().unload( stage_select_portraits_texture_ );
        stage_select_portraits_texture_ = {};
        app.texture_manager().unload( rank_texture_ );
        rank_texture_ = {};

        rank_rects_ = {};
    }

    void StageSelectModule::refresh_chart_style_availability( const App &app )
    {
        for( std::size_t index = 0; index < chart_styles_.size(); ++index )
        {
            chart_styles_available_[ index ] = app.game_data().chart_style_available( chart_styles_[ index ] );
        }
    }

    bool StageSelectModule::move_focus( std::int32_t direction )
    {
        const FocusArea previous = focus_;

        if( direction < 0 && focus_ == FocusArea::StageList )
        {
            focus_ = FocusArea::Difficulty;

            focused_category_ = active_category_;
        }
        else if( direction > 0 && focus_ == FocusArea::Difficulty )
        {
            focus_ = FocusArea::StageList;
        }

        return focus_ != previous;
    }

    void StageSelectModule::move_choice_cursor( std::int32_t direction )
    {
        if( direction == 0 || focus_ == FocusArea::StageList )
        {
            return;
        }

        if( focus_ == FocusArea::Difficulty )
        {
            const auto current = std::find( stage_categories_.begin(), stage_categories_.end(), focused_category_ );

            const auto count = static_cast<std::int32_t>( stage_categories_.size() );
            std::int32_t index = current == stage_categories_.end()
                                     ? 0
                                     : static_cast<std::int32_t>( std::distance( stage_categories_.begin(), current ) );

            for( std::int32_t step = 0; step < count; ++step )
            {
                index = ( index + direction + count ) % count;

                const auto category = stage_categories_[ static_cast<std::size_t>( index ) ];

                if( category_count( all_stages_, category ) > 0 )
                {
                    focused_category_ = category;
                    return;
                }
            }
        }
    }

    void StageSelectModule::apply_focused_choice( App &app )
    {
        switch( focus_ )
        {
            case FocusArea::StageList:
                confirm_selection( app );
                break;
            case FocusArea::Difficulty:
                set_category( app, focused_category_ );
                break;
        }
    }

    void StageSelectModule::render_chart_style( App &app,
        rendering::Renderer &renderer,
        const rendering::LogicalViewport &layout )
    {
        const auto index = utils::enum_index( app.settings().chart_style );

        const auto translated = localization::translate( chart_style_label_keys_[ index ] );
        const std::string label =
            translated == chart_style_label_keys_[ index ] ? chart_style_labels_[ index ] : translated;

        draw_label( app, renderer, layout, chart_style_label_bounds_, label, 16.0f, false, TextAlignment::Right );
    }

    void StageSelectModule::load_stage_catalog( App &app )
    {
        all_stages_.clear();
        stages_.clear();
        stage_hit_rects_.clear();
        action_message_.clear();

        if( !app.game_data().valid() )
        {
            action_message_ = localization::text( "@ui.game_data_not_found_upper" );
            return;
        }

        try
        {
            all_stages_ = gamedata::load_playable_stage_catalog( app.game_data(), app.settings().chart_style );

            if( all_stages_.empty() )
            {
                action_message_ = localization::text( "@ui.no_playable_stages_upper" );
                return;
            }

            if( category_count( all_stages_, active_category_ ) == 0 )
            {
                active_category_ = first_non_empty_category( all_stages_ );
            }

            apply_stage_category();
        }
        catch( const std::exception &e )
        {
            all_stages_.clear();
            stages_.clear();
            action_message_ = e.what();
        }
    }

    void StageSelectModule::apply_stage_category()
    {
        stages_.clear();

        for( const gamedata::PlayableStageEntry &stage : all_stages_ )
        {
            if( stage_matches_category( stage, active_category_ ) )
            {
                if( stage.mode == gamedata::PlayMode::Tutorial && std::any_of( stages_.begin(),
                                                                      stages_.end(),
                                                                      [ &stage ]( const auto &entry )
                                                                      {
                                                                          return entry.chc_path == stage.chc_path;
                                                                      } ) )
                {
                    continue;
                }

                stages_.push_back( stage );

                if( stage.mode == gamedata::PlayMode::Tutorial )
                {
                    stages_.back().tutorial_difficulty = active_category_ == StageCategory::SingleEasy
                                                             ? gamedata::TutorialDifficulty::Easy
                                                             : gamedata::TutorialDifficulty::Normal;
                }
            }
        }

        std::stable_partition( stages_.begin(),
            stages_.end(),
            []( const auto &stage )
            {
                return stage.mode == gamedata::PlayMode::Tutorial;
            } );

        const auto position = navigation_
                                  ? navigation_->restore( menu_locations_[ utils::enum_index( active_category_ ) ],
                                        static_cast<std::int32_t>( stages_.size() ) )
                                  : managers::MenuPosition{};

        selected_stage_ = position.selected_row;

        first_visible_stage_ = 0;
        reveal_selection();

        stage_hit_rects_.assign( stages_.size(), rendering::Rect{} );
    }

    void StageSelectModule::change_category( App &app, std::int32_t delta )
    {
        auto it = std::find( stage_categories_.begin(), stage_categories_.end(), active_category_ );
        std::int32_t index = it == stage_categories_.end()
                                 ? 0
                                 : static_cast<std::int32_t>( std::distance( stage_categories_.begin(), it ) );

        const std::int32_t count = static_cast<std::int32_t>( stage_categories_.size() );

        for( std::int32_t step = 0; step < count; ++step )
        {
            index = ( index + delta + count ) % count;
            const StageCategory candidate = stage_categories_[ static_cast<std::size_t>( index ) ];

            if( category_count( all_stages_, candidate ) > 0 )
            {
                set_category( app, candidate );
                return;
            }
        }
    }

    void StageSelectModule::set_category( App &app, StageCategory category )
    {
        focused_category_ = category;

        if( active_category_ == category )
        {
            return;
        }

        const auto stage_number =
            stages_.empty() ? -1 : stages_[ static_cast<std::size_t>( selected_stage_ ) ].stage_number;

        const auto first = first_visible_stage_;
        navigation_->remember( menu_locations_[ utils::enum_index( active_category_ ) ], selected_stage_ );
        active_category_ = category;
        apply_stage_category();

        const auto matching = std::find_if( stages_.begin(),
            stages_.end(),
            [ stage_number ]( const auto &stage )
            {
                return stage.stage_number == stage_number;
            } );

        if( matching != stages_.end() )
        {
            selected_stage_ = static_cast<std::int32_t>( std::distance( stages_.begin(), matching ) );

            first_visible_stage_ = first;
            reveal_selection();
        }
        else
        {
            preview_selection_seconds_ = 0.0;

            audio_preview_requested_stage_number_ = -1;
            pending_preview_cue_.clear();
        }

        play_menu_sound( app, "SELECT" );
    }

    void StageSelectModule::move_selection( App &app, std::int32_t delta )
    {
        if( stages_.empty() )
        {
            return;
        }

        const std::int32_t count = static_cast<std::int32_t>( stages_.size() );
        selected_stage_ = ( selected_stage_ + delta + count ) % count;
        reveal_selection();

        preview_selection_seconds_ = 0.0;

        audio_preview_requested_stage_number_ = -1;
        pending_preview_cue_.clear();

        if( audio_preview_stage_number_ >= 0 )
        {
            app.audio().set_channel_stem_gains( practice_preview_channel_,
                managers::AudioStemGains{ 0.0f, 0.0f, 0.0f, 0.0f },
                9600 );

            if( bgm_channel_ )
            {
                app.audio().set_channel_stem_gains( bgm_channel_, managers::AudioStemGains{}, 9600 );
            }
        }

        play_menu_sound( app, "SELECT" );
    }

    rendering::LogicalViewport StageSelectModule::screen_layout( const rendering::Renderer &renderer )
    {
        auto layout = rendering::make_logical_viewport( renderer );
        const float margin = ( layout.logical_height - window_bounds_.h ) * 0.5f;
        layout.y += ( margin - window_bounds_.y ) * layout.scale;
        return layout;
    }

    void StageSelectModule::reveal_selection()
    {
        const auto count = static_cast<std::int32_t>( stages_.size() );

        const auto maximum = std::max( 0, count - visible_stage_rows_ );
        first_visible_stage_ = std::clamp( first_visible_stage_, 0, maximum );

        if( selected_stage_ < first_visible_stage_ )
        {
            first_visible_stage_ = selected_stage_;
        }
        else if( selected_stage_ >= first_visible_stage_ + visible_stage_rows_ )
        {
            first_visible_stage_ = selected_stage_ - visible_stage_rows_ + 1;
        }
    }

    void StageSelectModule::scroll_stage_list( App &app, std::int32_t delta )
    {
        const auto count = static_cast<std::int32_t>( stages_.size() );

        const auto maximum = std::max( 0, count - visible_stage_rows_ );
        first_visible_stage_ = std::clamp( first_visible_stage_ + delta, 0, maximum );

        if( count > 0 )
        {
            const auto last = std::min( count - 1, first_visible_stage_ + visible_stage_rows_ - 1 );
            set_selection( app, std::clamp( selected_stage_, first_visible_stage_, last ) );
        }
    }

    bool StageSelectModule::update_scrollbar( App &app, const managers::MouseState &mouse )
    {
        if( mouse.left_clicked && scrollbar_track_rect_.h > 0.0f &&
            rendering::point_inside( scrollbar_track_rect_, mouse.x, mouse.y ) )
        {
            focus_ = FocusArea::StageList;

            if( rendering::point_inside( scrollbar_thumb_rect_, mouse.x, mouse.y ) )
            {
                scrollbar_dragging_ = true;

                scrollbar_grab_offset_ = mouse.y - scrollbar_thumb_rect_.y;
            }
            else
            {
                scroll_stage_list( app,
                    mouse.y < scrollbar_thumb_rect_.y ? -visible_stage_rows_ : visible_stage_rows_ );
            }

            return true;
        }

        if( !scrollbar_dragging_ )
        {
            return false;
        }

        if( !mouse.left_down )
        {
            scrollbar_dragging_ = false;
            return true;
        }

        const float travel = scrollbar_track_rect_.h - scrollbar_thumb_rect_.h;

        const auto maximum = std::max( 0, static_cast<std::int32_t>( stages_.size() ) - visible_stage_rows_ );

        if( travel > 0.0f )
        {
            const float position =
                std::clamp( ( mouse.y - scrollbar_track_rect_.y - scrollbar_grab_offset_ ) / travel, 0.0f, 1.0f );

            const auto first = static_cast<std::int32_t>( std::lround( position * maximum ) );
            scroll_stage_list( app, first - first_visible_stage_ );
        }

        return true;
    }

    void StageSelectModule::render_scrollbar( rendering::Renderer &renderer, const rendering::LogicalViewport &layout )
    {
        scrollbar_track_rect_ = {};

        scrollbar_thumb_rect_ = {};

        const auto count = static_cast<std::int32_t>( stages_.size() );

        if( count <= visible_stage_rows_ )
        {
            return;
        }

        const rendering::Rect track{
            body_split_x_ - scrollbar_width_ - card_gap_,
            body_top_,
            scrollbar_width_,
            body_bottom_ - body_top_
        };

        const float height =
            std::max( scrollbar_minimum_thumb_, track.h * visible_stage_rows_ / static_cast<float>( count ) );

        const float fraction =
            static_cast<float>( first_visible_stage_ ) / static_cast<float>( count - visible_stage_rows_ );

        const rendering::Rect thumb{ track.x, track.y + ( track.h - height ) * fraction, track.w, height };
        scrollbar_track_rect_ = layout.rect( track );

        scrollbar_thumb_rect_ = layout.rect( thumb );
        renderer.draw_filled_rect( scrollbar_track_rect_, rendering::menu_theme::card );
        renderer.draw_filled_rect( scrollbar_thumb_rect_, rendering::menu_theme::focus_accent );
    }

    void StageSelectModule::set_selection( App &app, std::int32_t index )
    {
        if( index < 0 || index >= static_cast<std::int32_t>( stages_.size() ) || index == selected_stage_ )
        {
            return;
        }

        selected_stage_ = index;

        preview_selection_seconds_ = 0.0;

        audio_preview_requested_stage_number_ = -1;
        pending_preview_cue_.clear();

        if( audio_preview_stage_number_ >= 0 )
        {
            app.audio().set_channel_stem_gains( practice_preview_channel_,
                managers::AudioStemGains{ 0.0f, 0.0f, 0.0f, 0.0f },
                9600 );

            if( bgm_channel_ )
            {
                app.audio().set_channel_stem_gains( bgm_channel_, managers::AudioStemGains{}, 9600 );
            }
        }

        play_menu_sound( app, "SELECT" );
    }

    void StageSelectModule::confirm_selection( App &app )
    {
        if( selected_stage_ < 0 || selected_stage_ >= static_cast<std::int32_t>( stages_.size() ) )
        {
            return;
        }

        play_menu_sound( app, "OK" );
        const gamedata::PlayableStageEntry selected_entry = stages_[ static_cast<std::size_t>( selected_stage_ ) ];

        if( launch_mode_ == LaunchMode::Practice && selected_entry.mode != gamedata::PlayMode::Tutorial )
        {
            open_practice_sections( app );
            return;
        }

        if( app.settings().play_movies && !selected_entry.bonus_content &&
            selected_entry.mode != gamedata::PlayMode::Tutorial )
        {
            try
            {
                const std::optional<std::int32_t> movie_page =
                    gamedata::album_intro_movie_page( app.game_data(), selected_entry.page_id );

                if( movie_page.has_value() )
                {
                    const gamedata::AlbumFlowTarget target =
                        gamedata::resolve_album_flow_target( app.game_data(), *movie_page, app.settings().chart_style );

                    if( target.type == gamedata::AlbumFlowTargetType::Movie )
                    {
                        app.change_module( std::make_unique<MovieModule>( target.page_id, target.movie_path ) );
                        return;
                    }
                }
            }
            catch( const std::exception &error )
            {
                action_message_ = error.what();
            }
        }

        LoadingTask task = make_stage_loading_task( selected_entry,
            [ category = active_category_, launch_mode = launch_mode_ ]()
            {
                return std::make_unique<StageSelectModule>( category, launch_mode );
            } );
        app.change_module( std::make_unique<LoadingModule>( std::move( task ) ) );
    }

    void StageSelectModule::open_practice_sections( App &app )
    {
        if( selected_stage_ < 0 || selected_stage_ >= static_cast<std::int32_t>( stages_.size() ) )
        {
            return;
        }

        try
        {
            stop_stage_audio_preview( app, true );
            practice_stage_ = stages_[ static_cast<std::size_t>( selected_stage_ ) ];

            if( practice_stage_.chc_path.empty() || !practice_stage_.chc_exists )
            {
                throw std::runtime_error( "Selected stage has no converted chart JSON" );
            }

            practice_song_ = filetypes::chc::load_json_file( practice_stage_.chc_path );
            practice_sections_.clear();

            for( std::size_t index = 0; index < practice_song_.sections.size(); ++index )
            {
                const runtime::stage::StageActionMode mode =
                    runtime::stage::stage_action_mode_from_phase( practice_song_.sections[ index ].battle_phase );

                if( mode != runtime::stage::StageActionMode::Intro && mode != runtime::stage::StageActionMode::End )
                {
                    practice_sections_.push_back( index );
                }
            }

            if( practice_sections_.empty() )
            {
                for( std::size_t index = 0; index < practice_song_.sections.size(); ++index )
                {
                    if( runtime::stage::stage_action_mode_from_phase( practice_song_.sections[ index ].battle_phase ) !=
                        runtime::stage::StageActionMode::End )
                    {
                        practice_sections_.push_back( index );
                    }
                }
            }

            if( practice_sections_.empty() )
            {
                throw std::runtime_error( localization::text( "@ui.stage.no_practice_sections" ) );
            }

            selected_practice_section_ = 0;

            selecting_practice_section_ = true;
            action_message_.clear();

            const std::filesystem::path audio_path = resolve_practice_audio_path( app );

            if( !audio_path.empty() )
            {
                app.audio().stop_channel( bgm_channel_ );
                practice_preview_bank_loaded_ = app.audio().merge_audio_bank( audio_path );

                if( practice_preview_bank_loaded_ )
                {
                    start_practice_audio_preview( app );
                }
                else if( bgm_channel_ && app.audio().has_subsong( "BGM_B2" ) )
                {
                    app.audio().play_subsong( bgm_channel_, "BGM_B2", true );
                }
            }
        }
        catch( const std::exception &error )
        {
            action_message_ = error.what();

            selecting_practice_section_ = false;
            practice_sections_.clear();
        }
    }

    void StageSelectModule::move_practice_section( App &app, std::int32_t delta )
    {
        if( practice_sections_.empty() )
        {
            return;
        }

        const std::int32_t count = static_cast<std::int32_t>( practice_sections_.size() );
        selected_practice_section_ = ( selected_practice_section_ + delta + count ) % count;
        play_menu_sound( app, "SELECT" );
        start_practice_audio_preview( app );
    }

    void StageSelectModule::confirm_practice_section( App &app )
    {
        if( selected_practice_section_ < 0 ||
            selected_practice_section_ >= static_cast<std::int32_t>( practice_sections_.size() ) )
        {
            return;
        }

        play_menu_sound( app, "OK" );
        StageLoadData load_data{};
        load_data.stage = practice_stage_;

        load_data.song = practice_song_;

        load_data.chc_loaded = true;

        load_data.session_mode = StageSessionMode::Practice;

        load_data.practice_section_index = practice_sections_[ static_cast<std::size_t>( selected_practice_section_ ) ];
        app.change_module( std::make_unique<StageModule>( std::move( load_data ) ) );
    }

    void StageSelectModule::close_practice_sections( App &app )
    {
        stop_practice_audio_preview( app, true );
        play_menu_sound( app, "CANCEL" );
        selecting_practice_section_ = false;
        practice_sections_.clear();
        selected_practice_section_ = 0;
    }

    std::filesystem::path StageSelectModule::resolve_practice_audio_path( const App &app ) const
    {
        return resolve_stage_audio_path( app, practice_stage_ );
    }

    std::filesystem::path StageSelectModule::resolve_stage_audio_path( const App &app,
        const gamedata::PlayableStageEntry &stage ) const
    {
        if( !stage.runtime_audio_path.empty() )
        {
            std::error_code ec{};

            if( std::filesystem::is_regular_file( stage.runtime_audio_path / "audio.json", ec ) )
            {
                return stage.runtime_audio_path;
            }
        }

        const gamedata::ChartStyle chart_style =
            stage.portable_visuals ? gamedata::ChartStyle::GitarooManLives : app.settings().chart_style;

        const std::filesystem::path runtime_audio =
            app.game_data().runtime_stage_audio( stage.stage_number, chart_style, stage.chc_path.filename().string() );

        std::error_code ec{};

        if( std::filesystem::is_regular_file( runtime_audio / "audio.json", ec ) )
        {
            return runtime_audio;
        }

        if( chart_style == gamedata::ChartStyle::GitarooManLives )
        {
            const std::filesystem::path fallback = app.game_data().runtime_stage_audio( stage.stage_number,
                gamedata::ChartStyle::GitarooMan,
                stage.chc_path.filename().string() );

            if( std::filesystem::is_regular_file( fallback / "audio.json", ec ) )
            {
                return fallback;
            }
        }

        return {};
    }

    std::string StageSelectModule::stage_preview_key( const gamedata::PlayableStageEntry &stage )
    {
        const std::string identity = stage.ssq_path.lexically_normal().generic_string() + "|" +
                                     stage.runtime_model_root.lexically_normal().generic_string() + "|" +
                                     ( stage.portable_visuals ? "PSP" : "PS2" );

        std::uint64_t hash = 14695981039346656037ull;

        for( const unsigned char value : identity )
        {
            hash ^= value;

            hash *= 1099511628211ull;
        }

        std::ostringstream stream;
        stream << std::hex << std::setw( 16 ) << std::setfill( '0' ) << hash;
        return stream.str();
    }

    std::filesystem::path StageSelectModule::stage_preview_path( const App &app,
        const gamedata::PlayableStageEntry &stage )
    {
        std::error_code ec{};

        if( !stage.runtime_model_root.empty() )
        {
            const std::filesystem::path supplied = stage.runtime_model_root.parent_path() / "preview.png";

            if( std::filesystem::is_regular_file( supplied, ec ) )
            {
                return supplied;
            }
        }

        if( !stage.chc_path.empty() )
        {
            const std::filesystem::path supplied = stage.chc_path.parent_path() / "preview.png";

            if( std::filesystem::is_regular_file( supplied, ec ) )
            {
                return supplied;
            }
        }

        const std::filesystem::path root = app.game_data().piko_data_root();

        if( root.empty() )
        {
            return {};
        }

        return root / "ui" / "stage_select" / "previews" / ( stage_preview_key( stage ) + ".png" );
    }

    bool StageSelectModule::generate_stage_preview( App &app,
        rendering::Renderer &renderer,
        const gamedata::PlayableStageEntry &stage,
        const std::filesystem::path &path )
    {
        if( !stage.ssq_exists || stage.runtime_model_root.empty() || path.empty() )
        {
            return false;
        }

        const auto candidates = stage_preview_timings( app, stage );

        if( candidates.empty() )
        {
            return false;
        }

        Stage3DRenderer scene;

        if( !scene.load( app, stage ) )
        {
            return false;
        }

        constexpr std::int32_t width = 512;
        constexpr std::int32_t height = 288;
        GLint previous_framebuffer = 0;
        GLint previous_viewport[ 4 ]{};
        GLint previous_pack_alignment = 4;
        glGetIntegerv( GL_FRAMEBUFFER_BINDING, &previous_framebuffer );
        glGetIntegerv( GL_VIEWPORT, previous_viewport );
        glGetIntegerv( GL_PACK_ALIGNMENT, &previous_pack_alignment );

        GLuint frame = 0;
        GLuint color = 0;
        GLuint depth = 0;
        glGenFramebuffers( 1, &frame );
        glBindFramebuffer( GL_FRAMEBUFFER, frame );
        glGenTextures( 1, &color );
        glBindTexture( GL_TEXTURE_2D, color );
        glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0 );
        glGenRenderbuffers( 1, &depth );
        glBindRenderbuffer( GL_RENDERBUFFER, depth );
        glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height );
        glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth );

        bool success = glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
        std::vector<std::uint8_t> best_pixels;
        float best_score = -1.0f;

        if( success )
        {
            glPixelStorei( GL_PACK_ALIGNMENT, 1 );

            for( const StagePreviewTiming &candidate : candidates )
            {
                scene.set_stage_timing( candidate.global_frame,
                    candidate.section_index,
                    candidate.section_frame,
                    candidate.tempo );

                scene.set_portable_song_animation_frame( candidate.portable_animation_frame );
                glBindFramebuffer( GL_FRAMEBUFFER, frame );
                glViewport( 0, 0, width, height );
                glClearColor( 0.02f, 0.03f, 0.07f, 1.0f );
                glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );

                if( !scene.render( app, renderer, width, height ) )
                {
                    continue;
                }

                std::vector<std::uint8_t> pixels( width * height * 4u );
                glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
                double luminance = 0.0;
                double luminance_squared = 0.0;
                std::int32_t samples = 0;

                for( std::int32_t y = 12; y < height - 12; y += 8 )
                {
                    for( std::int32_t x = 12; x < width - 12; x += 8 )
                    {
                        const std::size_t index = static_cast<std::size_t>( ( y * width + x ) * 4 );
                        const double value =
                            ( pixels[ index ] * 0.21 + pixels[ index + 1 ] * 0.72 + pixels[ index + 2 ] * 0.07 ) /
                            255.0;

                        luminance += value;

                        luminance_squared += value * value;
                        ++samples;
                    }
                }

                const double mean = luminance / std::max( samples, 1 );
                const float score = static_cast<float>(
                    luminance_squared / std::max( samples, 1 ) - mean * mean + std::min( mean, 0.35 ) * 0.08 );

                if( score > best_score )
                {
                    best_score = score;

                    best_pixels = std::move( pixels );
                }
            }

            success = !best_pixels.empty();
        }

        glBindFramebuffer( GL_FRAMEBUFFER, static_cast<GLuint>( previous_framebuffer ) );
        glViewport( previous_viewport[ 0 ], previous_viewport[ 1 ], previous_viewport[ 2 ], previous_viewport[ 3 ] );
        glPixelStorei( GL_PACK_ALIGNMENT, previous_pack_alignment );

        if( depth != 0 )
        {
            glDeleteRenderbuffers( 1, &depth );
        }

        if( color != 0 )
        {
            glDeleteTextures( 1, &color );
        }

        if( frame != 0 )
        {
            glDeleteFramebuffers( 1, &frame );
        }

        scene.unload( app );

        if( !success )
        {
            return false;
        }

        std::vector<std::uint8_t> output( best_pixels.size() );

        for( std::int32_t y = 0; y < height; ++y )
        {
            const std::size_t source = static_cast<std::size_t>( height - 1 - y ) * width * 4u;
            const std::size_t target = static_cast<std::size_t>( y ) * width * 4u;
            std::copy_n( best_pixels.data() + source, width * 4u, output.data() + target );

            for( std::int32_t x = 0; x < width; ++x )
            {
                output[ target + static_cast<std::size_t>( x ) * 4u + 3u ] = 255u;
            }
        }

        try
        {
            std::filesystem::create_directories( path.parent_path() );
            convert::write_rgba_png( path,
                width,
                height,
                std::span<const std::uint8_t>( output.data(), output.size() ) );

            return true;
        }
        catch( const std::exception &error )
        {
            std::cerr << "Stage preview write failed: " << error.what() << '\n';
            return false;
        }
    }

    bool StageSelectModule::ensure_stage_preview( App &app,
        rendering::Renderer &renderer,
        const gamedata::PlayableStageEntry &stage )
    {
        const std::filesystem::path path = stage_preview_path( app, stage );

        if( path.empty() )
        {
            return false;
        }

        std::error_code ec{};
        bool current = std::filesystem::is_regular_file( path, ec );

        if( current && path.filename() != "preview.png" )
        {
            const auto generated = std::filesystem::last_write_time( path, ec );

            if( !ec && std::filesystem::is_regular_file( stage.ssq_path, ec ) )
            {
                current = generated >= std::filesystem::last_write_time( stage.ssq_path, ec );
            }

            if( !ec && std::filesystem::is_directory( stage.runtime_model_root, ec ) )
            {
                current = current && generated >= std::filesystem::last_write_time( stage.runtime_model_root, ec );
            }

            if( !ec && std::filesystem::is_regular_file( preview_timings_path( app ), ec ) )
            {
                current = current && generated >= std::filesystem::last_write_time( preview_timings_path( app ), ec );
            }
        }

        return current || generate_stage_preview( app, renderer, stage, path );
    }

    managers::TextureHandle StageSelectModule::selected_stage_preview( App &app )
    {
        if( stages_.empty() )
        {
            return {};
        }

        const gamedata::PlayableStageEntry &stage = stages_[ static_cast<std::size_t>( selected_stage_ ) ];
        const std::filesystem::path path = stage_preview_path( app, stage );

        if( path.empty() )
        {
            return {};
        }

        const std::string key = path.generic_string();

        if( const auto existing = preview_textures_.find( key ); existing != preview_textures_.end() )
        {
            return existing->second;
        }

        // Import prepares these images after conversion; browsing only loads PNGs.
        std::error_code ec{};

        if( unavailable_previews_.contains( key ) || !std::filesystem::is_regular_file( path, ec ) )
        {
            unavailable_previews_.insert( key );
            return {};
        }

        try
        {
            const managers::TextureHandle handle =
                app.texture_manager().load_png( "stage-select/preview/" + stage_preview_key( stage ), path );

            preview_textures_.emplace( key, handle );
            return handle;
        }
        catch( const std::exception &error )
        {
            std::cerr << "Stage preview load failed: " << error.what() << '\n';
            unavailable_previews_.insert( key );
            return {};
        }
    }

    void StageSelectModule::stop_stage_audio_preview( App &app, bool restore_bgm )
    {
        app.audio().stop_channel( practice_preview_channel_ );
        audio_preview_stage_number_ = -1;

        audio_preview_requested_stage_number_ = -1;

        audio_preview_elapsed_seconds_ = 0.0;

        audio_preview_finished_ = false;
        pending_preview_cue_.clear();

        if( restore_bgm && bgm_channel_ )
        {
            app.audio().set_channel_stem_gains( bgm_channel_, managers::AudioStemGains{}, 9600 );
        }
    }

    void StageSelectModule::start_stage_audio_preview( App &app )
    {
        if( selecting_practice_section_ || !practice_preview_channel_ || stages_.empty() ||
            preview_selection_seconds_ < 0.35 )
        {
            return;
        }

        const auto &stage = stages_[ static_cast<std::size_t>( selected_stage_ ) ];

        const auto number = stage.stage_number;

        if( audio_preview_stage_number_ == number )
        {
            return;
        }

        if( audio_preview_requested_stage_number_ != number )
        {
            audio_preview_requested_stage_number_ = number;
            pending_preview_cue_.clear();
            const std::filesystem::path audio_path = resolve_stage_audio_path( app, stage );

            if( audio_path.empty() || !stage.chc_exists )
            {
                return;
            }

            try
            {
                const filetypes::chc::Song song = filetypes::chc::load_json_file( stage.chc_path );
                std::string name;

                for( std::size_t index = 0; index < song.cues.size(); ++index )
                {
                    if( index < song.sections.size() )
                    {
                        const runtime::stage::StageActionMode mode =
                            runtime::stage::stage_action_mode_from_phase( song.sections[ index ].battle_phase );

                        if( mode == runtime::stage::StageActionMode::Intro ||
                            mode == runtime::stage::StageActionMode::End )
                        {
                            continue;
                        }
                    }

                    const filetypes::chc::Cue &cue = song.cues[ index ];
                    name = cue.audio_section_name.empty() ? cue.fallback_name : cue.audio_section_name;

                    if( !name.empty() )
                    {
                        break;
                    }
                }

                if( name.empty() )
                {
                    return;
                }

                const std::string prefix =
                    "preview/" + stage_preview_key( stage ) +
                    ( stage.chart_style == gamedata::ChartStyle::GitarooManLives ? "/PSP/" : "/PS2/" );

                if( loaded_preview_audio_banks_.insert( prefix ).second &&
                    !app.audio().merge_audio_bank( audio_path, {}, prefix ) )
                {
                    return;
                }

                pending_preview_cue_ = prefix + name;

                if( !app.audio().has_subsong( pending_preview_cue_ ) )
                {
                    pending_preview_cue_.clear();
                    return;
                }

                app.audio().preload_subsong_async( pending_preview_cue_ );
            }
            catch( const std::exception &error )
            {
                std::cerr << "Stage sample unavailable: " << error.what() << '\n';
            }
        }

        if( pending_preview_cue_.empty() || !app.audio().is_subsong_preloaded( pending_preview_cue_ ) )
        {
            return;
        }

        app.audio().stop_channel( practice_preview_channel_ );

        if( app.audio().play_subsong( practice_preview_channel_, pending_preview_cue_, false, 48000 * 20, 9600 ) )
        {
            audio_preview_stage_number_ = number;

            audio_preview_elapsed_seconds_ = 0.0;

            audio_preview_finished_ = false;

            if( bgm_channel_ )
            {
                app.audio().set_channel_stem_gains( bgm_channel_,
                    managers::AudioStemGains{ 0.25f, 0.25f, 0.25f, 0.25f },
                    9600 );
            }
        }
    }

    void StageSelectModule::draw_label( App &app,
        rendering::Renderer &renderer,
        const rendering::LogicalViewport &layout,
        const rendering::Rect &bounds,
        const std::string &label,
        float size,
        bool selected,
        TextAlignment alignment )
    {
        if( label.empty() )
        {
            return;
        }

        const rendering::Rect destination = layout.rect( bounds );

        if( !stage_select_font_.loaded() )
        {
            const float scale = std::max( 0.5f, size * layout.scale / 10.0f );
            const float width = static_cast<float>( label.size() ) * 6.0f * scale;
            const float x = alignment == TextAlignment::Center  ? destination.x + ( destination.w - width ) * 0.5f
                            : alignment == TextAlignment::Right ? destination.x + destination.w - width
                                                                : destination.x;

            renderer.draw_text( x,
                destination.y + ( destination.h - 7.0f * scale ) * 0.5f,
                label.c_str(),
                rendering::color_from_rgb(
                    selected ? rendering::menu_theme::selected_text_rgb : rendering::menu_theme::normal_text_rgb ),
                scale );

            return;
        }

        const std::string key =
            label + "|" + std::to_string( static_cast<std::int32_t>( size ) ) + ( selected ? "|selected" : "|normal" );

        managers::TextureHandle handle{};

        if( const auto found = text_textures_.find( key ); found != text_textures_.end() )
        {
            handle = found->second;
        }
        else
        {
            const auto style = rendering::menu_theme::compact_text_style( selected );

            const rendering::RasterizedText raster =
                stage_select_font_.rasterize_stroked( label, size * text_raster_scale_, style );

            if( !raster.valid() )
            {
                return;
            }

            handle = app.texture_manager().create_rgba(
                localization::texture_key( "stage-select/text/" + std::to_string( text_textures_.size() ) ),
                raster.rgba.data(),
                raster.width,
                raster.height );

            text_textures_.emplace( key, handle );
        }

        const rendering::Texture *texture = app.texture_manager().get( handle );

        if( texture == nullptr )
        {
            return;
        }

        const float scale = std::min(
            { layout.scale / text_raster_scale_,
                destination.w / static_cast<float>( std::max( texture->width, 1 ) ),
                destination.h / static_cast<float>( std::max( texture->height, 1 ) ) } );

        const float w = static_cast<float>( texture->width ) * scale;
        const float h = static_cast<float>( texture->height ) * scale;
        const float x = alignment == TextAlignment::Center  ? destination.x + ( destination.w - w ) * 0.5f
                        : alignment == TextAlignment::Right ? destination.x + destination.w - w
                                                            : destination.x;

        const auto sampling = renderer.texture_sampling();
        renderer.set_texture_sampling( { rendering::TextureFilterMode::LinearMipmap, 1.0f } );

        renderer.draw_texture( *texture, x, destination.y + ( destination.h - h ) * 0.5f, w, h );

        renderer.set_texture_sampling( sampling );
    }

    void StageSelectModule::start_practice_audio_preview( App &app )
    {
        if( !practice_preview_bank_loaded_ || !practice_preview_channel_ || practice_sections_.empty() ||
            selected_practice_section_ < 0 ||
            selected_practice_section_ >= static_cast<std::int32_t>( practice_sections_.size() ) )
        {
            return;
        }

        const std::size_t section_index = practice_sections_[ static_cast<std::size_t>( selected_practice_section_ ) ];

        if( section_index >= practice_song_.cues.size() )
        {
            return;
        }

        const filetypes::chc::Cue &cue = practice_song_.cues[ section_index ];
        const std::string cue_name = cue.audio_section_name.empty() ? cue.fallback_name : cue.audio_section_name;

        if( cue_name.empty() || !app.audio().has_subsong( cue_name ) )
        {
            return;
        }

        app.audio().stop_channel( practice_preview_channel_ );
        constexpr std::int64_t preview_frames = 48000 * 8;
        app.audio().play_subsong( practice_preview_channel_, cue_name, false, preview_frames );
    }

    void StageSelectModule::stop_practice_audio_preview( App &app, bool restore_menu_audio )
    {
        if( practice_preview_channel_ )
        {
            app.audio().stop_channel( practice_preview_channel_ );
        }

        if( !restore_menu_audio )
        {
            return;
        }

        const std::filesystem::path audio_path = app.game_data().main_menu_audio_path();

        if( !audio_path.empty() && app.audio().load_audio_bank( audio_path ) )
        {
            practice_preview_bank_loaded_ = false;

            if( bgm_channel_ && app.audio().has_subsong( "BGM_B2" ) )
            {
                app.audio().play_subsong( bgm_channel_, "BGM_B2", true );
            }
        }
    }

    void StageSelectModule::render_rank_badge( App &app,
        rendering::Renderer &renderer,
        const rendering::LogicalViewport &layout,
        const rendering::Rect &bounds,
        const gamedata::PlayableStageEntry &stage )
    {
        const gamedata::SavedStageProgress *progress =
            app.save_data().stage_progress( stage.stage_number, progress_mode_for_stage( stage ), stage.chart_style );

        const std::int32_t rank =
            progress != nullptr && progress->cleared && progress->best_rank >= 1 && progress->best_rank <= 5
                ? progress->best_rank
                : 0;

        const rendering::Color color = clear_rank_color( rank );
        draw_stage_select_panel( renderer, layout, bounds, rendering::menu_theme::card, color, 1.0f );
        const rendering::Rect destination =
            layout.rect( bounds.x + 3.0f, bounds.y + 3.0f, bounds.w - 6.0f, bounds.h - 6.0f );

        if( const rendering::Texture *texture = app.texture_manager().get( rank_texture_ );
            rank != 0 && texture != nullptr )
        {
            const rendering::Rect &source = rank_rects_[ static_cast<std::size_t>( rank ) ];

            if( source.w > 0.0f && source.h > 0.0f )
            {
                const float fit = std::min( destination.w / source.w, destination.h / source.h );
                const float width = source.w * fit;
                const float height = source.h * fit;
                renderer.draw_texture_region( *texture,
                    source.x,
                    source.y,
                    source.w,
                    source.h,
                    destination.x + ( destination.w - width ) * 0.5f,
                    destination.y + ( destination.h - height ) * 0.5f,
                    width,
                    height );

                return;
            }
        }

        const std::string label = rank_label( rank );

        if( stage_select_font_.loaded() )
        {
            const std::string key = "rank|" + std::to_string( rank );
            managers::TextureHandle handle{};

            if( const auto found = text_textures_.find( key ); found != text_textures_.end() )
            {
                handle = found->second;
            }
            else
            {
                const rendering::StrokedTextStyle style{
                    color,
                    rendering::color_from_rgb( 0x34243Fu ),
                    rendering::color_from_rgb( 0xFCE7C9u ),
                    2,
                    3,
                    0.0f
                };

                const rendering::RasterizedText raster = stage_select_font_.rasterize_stroked( label, 58.0f, style );

                if( raster.valid() )
                {
                    handle = app.texture_manager().create_rgba( localization::texture_key( "stage-select/" + key ),
                        raster.rgba.data(),
                        raster.width,
                        raster.height );

                    text_textures_.emplace( key, handle );
                }
            }

            if( const rendering::Texture *texture = app.texture_manager().get( handle ) )
            {
                const float fit = std::min( destination.w / static_cast<float>( texture->width ),
                    destination.h / static_cast<float>( texture->height ) );

                const float width = static_cast<float>( texture->width ) * fit;
                const float height = static_cast<float>( texture->height ) * fit;
                renderer.draw_texture( *texture,
                    destination.x + ( destination.w - width ) * 0.5f,
                    destination.y + ( destination.h - height ) * 0.5f,
                    width,
                    height );

                return;
            }
        }

        const float scale = 2.0f * layout.scale;
        renderer.draw_text( destination.x +
                                ( destination.w - static_cast<float>( label.size() ) * 6.0f * scale ) * 0.5f,
            destination.y + ( destination.h - 7.0f * scale ) * 0.5f,
            label.c_str(),
            color,
            scale );
    }

    void StageSelectModule::render_practice_sections( App &app, rendering::Renderer &renderer )
    {
        const rendering::LogicalViewport layout = rendering::make_logical_viewport( renderer );
        TitleScreenBackdrop::draw( renderer,
            wave_background_,
            renderer.output_width(),
            renderer.output_height(),
            static_cast<float>( elapsed_seconds_ ),
            app.texture_manager().get( circle_texture_ ) );

        draw_stage_select_panel( renderer, layout, rendering::Rect{ 30.0f, 22.0f, 580.0f, 72.0f } );
        draw_stage_select_panel( renderer, layout, rendering::Rect{ 48.0f, 101.0f, 544.0f, 303.0f } );

        renderer.draw_text( layout.x + 48.0f * layout.scale,
            layout.y + 34.0f * layout.scale,
            localization::text( "@ui.practice_mode_upper" ),
            rendering::Color{ 0.90f, 1.0f, 0.70f, 1.0f },
            2.0f * layout.scale );

        const std::string stage_text = localization::format( "@ui.stage.heading",
            { { "stage", std::to_string( practice_stage_.stage_number ) },
                { "name", localization::stage_name( practice_stage_ ) } } );

        renderer.draw_text( layout.x + 50.0f * layout.scale,
            layout.y + 72.0f * layout.scale,
            stage_text.c_str(),
            rendering::Color{ 0.72f, 0.88f, 1.0f, 0.95f },
            0.9f * layout.scale );

        if( practice_sections_.empty() )
        {
            return;
        }

        const std::int32_t count = static_cast<std::int32_t>( practice_sections_.size() );
        const std::int32_t first = std::max( 0, selected_practice_section_ - 4 );
        const std::int32_t last = std::min( count, first + 9 );

        for( std::int32_t row = first; row < last; ++row )
        {
            const bool selected = row == selected_practice_section_;
            const std::size_t section_index = practice_sections_[ static_cast<std::size_t>( row ) ];
            const filetypes::chc::SongSection &section = practice_song_.sections[ section_index ];
            const runtime::stage::StageActionMode mode =
                runtime::stage::stage_action_mode_from_phase( section.battle_phase );

            std::string cue_name;

            if( section_index < practice_song_.cues.size() )
            {
                const filetypes::chc::Cue &cue = practice_song_.cues[ section_index ];
                cue_name = cue.audio_section_name.empty() ? cue.fallback_name : cue.audio_section_name;
            }

            const float y = 112.0f + static_cast<float>( row - first ) * 30.0f;

            if( selected )
            {
                draw_stage_select_panel( renderer,
                    layout,
                    rendering::Rect{ 62.0f, y - 5.0f, 516.0f, 26.0f },
                    stage_select_selected,
                    stage_select_border,
                    1.0f );
            }

            const std::string line = localization::format( "@ui.stage.section",
                { { "section", std::to_string( section_index + 1u ) },
                    { "mode", runtime::stage::stage_action_mode_name( mode ) },
                    { "cue", cue_name } } );

            renderer.draw_text( layout.x + 76.0f * layout.scale,
                layout.y + y * layout.scale,
                line.c_str(),
                selected ? rendering::Color{ 1.0f, 1.0f, 0.82f, 1.0f } : rendering::Color{ 0.70f, 0.82f, 0.90f, 0.88f },
                0.86f * layout.scale );
        }
    }

    void StageSelectModule::return_to_main_menu( App &app )
    {
        play_menu_sound( app, "CANCEL" );
        app.change_module( std::make_unique<MainMenuModule>() );
    }

    std::int32_t StageSelectModule::hit_test_stage( float x, float y ) const
    {
        for( std::size_t index = 0; index < stage_hit_rects_.size(); ++index )
        {
            if( rendering::point_inside( stage_hit_rects_[ index ], x, y ) )
            {
                return static_cast<std::int32_t>( index );
            }
        }

        return -1;
    }

    std::int32_t StageSelectModule::hit_test_category( float x, float y ) const
    {
        for( std::size_t index = 0; index < category_hit_rects_.size(); ++index )
        {
            if( rendering::point_inside( category_hit_rects_[ index ], x, y ) )
            {
                return static_cast<std::int32_t>( index );
            }
        }

        return -1;
    }

    void StageSelectModule::handle_event( App &app, const SDL_Event &event )
    {
        if( selecting_practice_section_ )
        {
            return;
        }

        if( event.type == SDL_EVENT_MOUSE_WHEEL )
        {
            const auto &mouse = app.input().mouse();

            if( rendering::point_inside( stage_list_rect_, mouse.x, mouse.y ) )
            {
                const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? 1.0f : -1.0f;
                wheel_scroll_fraction_ += event.wheel.y * direction;

                const auto delta = static_cast<std::int32_t>( std::trunc( wheel_scroll_fraction_ ) );

                if( delta != 0 )
                {
                    wheel_scroll_fraction_ -= static_cast<float>( delta );

                    focus_ = FocusArea::StageList;
                    scroll_stage_list( app, delta );
                }
            }

            return;
        }

        if( event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat )
        {
            if( event.key.key == SDLK_Q )
            {
                change_category( app, -1 );
            }
            else if( event.key.key == SDLK_E || event.key.key == SDLK_TAB )
            {
                change_category( app, 1 );
            }
        }
        else if( event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN )
        {
            if( event.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER )
            {
                change_category( app, -1 );
            }
            else if( event.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER )
            {
                change_category( app, 1 );
            }
        }
        else if( event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN )
        {
            if( event.jbutton.button == 4 )
            {
                change_category( app, -1 );
            }
            else if( event.jbutton.button == 5 )
            {
                change_category( app, 1 );
            }
        }
    }

    void StageSelectModule::update( App &app, double delta_seconds )
    {
        const double delta = std::max( 0.0, delta_seconds );
        elapsed_seconds_ += delta;

        preview_selection_seconds_ += delta;

        const managers::InputManager &input = app.input();

        if( selecting_practice_section_ )
        {
            if( input.pressed( managers::InputAction::MenuLeft ) || input.pressed( managers::InputAction::MenuUp ) )
            {
                move_practice_section( app, -1 );
            }

            if( input.pressed( managers::InputAction::MenuRight ) || input.pressed( managers::InputAction::MenuDown ) )
            {
                move_practice_section( app, 1 );
            }

            if( input.pressed( managers::InputAction::Confirm ) )
            {
                confirm_practice_section( app );
                return;
            }

            if( input.pressed( managers::InputAction::Back ) )
            {
                close_practice_sections( app );
                return;
            }

            return;
        }

        if( audio_preview_stage_number_ >= 0 && !audio_preview_finished_ )
        {
            audio_preview_elapsed_seconds_ += delta;

            if( audio_preview_elapsed_seconds_ >= 19.6 && audio_preview_elapsed_seconds_ - delta < 19.6 )
            {
                app.audio().set_channel_stem_gains( practice_preview_channel_,
                    managers::AudioStemGains{ 0.0f, 0.0f, 0.0f, 0.0f },
                    12000 );
            }

            if( audio_preview_elapsed_seconds_ >= 20.0 )
            {
                app.audio().stop_channel( practice_preview_channel_ );

                if( bgm_channel_ )
                {
                    app.audio().set_channel_stem_gains( bgm_channel_, managers::AudioStemGains{}, 9600 );
                }

                audio_preview_finished_ = true;
            }
        }

        start_stage_audio_preview( app );

        if( input.pressed( managers::InputAction::MenuUp ) )
        {
            if( focus_ == FocusArea::StageList && selected_stage_ > 0 )
            {
                move_selection( app, -1 );
            }
            else if( move_focus( -1 ) )
            {
                play_menu_sound( app, "SELECT" );
            }
        }

        if( input.pressed( managers::InputAction::MenuDown ) )
        {
            if( focus_ == FocusArea::StageList )
            {
                move_selection( app, 1 );
            }
            else if( move_focus( 1 ) )
            {
                play_menu_sound( app, "SELECT" );
            }
        }

        const auto move_choice = [ & ]( std::int32_t direction )
        {
            const auto category = focused_category_;

            move_choice_cursor( direction );

            if( focused_category_ != category )
            {
                play_menu_sound( app, "SELECT" );
            }
        };

        if( input.pressed( managers::InputAction::MenuLeft ) )
        {
            move_choice( -1 );
        }

        if( input.pressed( managers::InputAction::MenuRight ) )
        {
            move_choice( 1 );
        }

        const managers::MouseState &mouse = input.mouse();

        if( update_scrollbar( app, mouse ) )
        {
            return;
        }

        if( mouse.moved && focus_ == FocusArea::StageList )
        {
            const auto hovered = hit_test_stage( mouse.x, mouse.y );

            if( hovered >= 0 )
            {
                set_selection( app, hovered );
            }
        }

        if( mouse.left_clicked )
        {
            const auto category = hit_test_category( mouse.x, mouse.y );

            if( category >= 0 )
            {
                const auto value = stage_categories_[ static_cast<std::size_t>( category ) ];

                if( category_count( all_stages_, value ) > 0 )
                {
                    focus_ = FocusArea::Difficulty;
                    set_category( app, value );
                }
            }
            else
            {
                const auto stage = hit_test_stage( mouse.x, mouse.y );

                if( stage >= 0 )
                {
                    focus_ = FocusArea::StageList;
                    set_selection( app, stage );
                    confirm_selection( app );
                }
            }
        }
        else if( input.pressed( managers::InputAction::Confirm ) )
        {
            apply_focused_choice( app );
        }

        if( input.pressed( managers::InputAction::Back ) )
        {
            if( focus_ != FocusArea::StageList )
            {
                move_focus( 1 );
                return;
            }

            return_to_main_menu( app );
        }
    }

    void StageSelectModule::render( App &app, rendering::Renderer &renderer )
    {
        if( selecting_practice_section_ )
        {
            render_practice_sections( app, renderer );
            return;
        }

        const rendering::LogicalViewport layout = screen_layout( renderer );
        const rendering::Texture *portraits_texture = app.texture_manager().get( stage_select_portraits_texture_ );

        TitleScreenBackdrop::draw( renderer,
            wave_background_,
            renderer.output_width(),
            renderer.output_height(),
            static_cast<float>( elapsed_seconds_ ),
            app.texture_manager().get( circle_texture_ ) );

        draw_stage_select_panel( renderer,
            layout,
            rendering::Rect{
                window_bounds_.x,
                window_bounds_.y,
                window_bounds_.w,
                header_bottom_ - window_bounds_.y
            } );

        const float right_x = body_split_x_ + card_gap_;
        const float right_width = window_bounds_.x + window_bounds_.w - right_x;
        draw_stage_select_panel( renderer,
            layout,
            rendering::Rect{ right_x, body_top_, right_width, preview_bottom_ - body_top_ } );

        draw_stage_select_panel( renderer,
            layout,
            rendering::Rect{
                right_x,
                preview_bottom_ + card_gap_,
                right_width,
                window_bounds_.y + window_bounds_.h - preview_bottom_ - card_gap_
            } );

        draw_label( app,
            renderer,
            layout,
            rendering::Rect{ 31.0f, 8.0f, 252.0f, 41.0f },
            localization::text( "@ui.stage_select_upper" ),
            27.0f );

        render_chart_style( app, renderer, layout );

        for( std::size_t index = 0; index < stage_categories_.size(); ++index )
        {
            const float tab_width =
                ( window_bounds_.w - card_gap_ * static_cast<float>( stage_categories_.size() - 1u ) ) /
                static_cast<float>( stage_categories_.size() );

            const rendering::Rect reference{
                window_bounds_.x + static_cast<float>( index ) * ( tab_width + card_gap_ ),
                tabs_top_,
                tab_width,
                body_top_ - card_gap_ - tabs_top_
            };

            const rendering::Rect tab = layout.rect( reference );
            category_hit_rects_[ index ] = tab;
            const StageCategory category = stage_categories_[ index ];
            const bool selected = category == active_category_;
            const bool available = category_count( all_stages_, category ) > 0;
            const bool focused = available && focus_ == FocusArea::Difficulty && focused_category_ == category;
            draw_choice_card( renderer, layout, reference, selected, focused, available );

            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ reference.x + 5.0f, reference.y + 1.0f, reference.w - 10.0f, reference.h - 2.0f },
                category_label( category ),
                15.0f,
                selected && available && !focused,
                TextAlignment::Center );
        }

        stage_list_rect_ =
            layout.rect( window_bounds_.x, body_top_, body_split_x_ - window_bounds_.x, body_bottom_ - body_top_ );

        renderer.draw_filled_rect( stage_list_rect_, rendering::menu_theme::card );
        scrollbar_track_rect_ = {};

        scrollbar_thumb_rect_ = {};
        stage_hit_rects_.assign( stages_.size(), rendering::Rect{} );

        if( stages_.empty() )
        {
            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 36.0f, 210.0f, 256.0f, 40.0f },
                action_message_.empty() ? localization::text( "@ui.no_stages_in_this_category_upper" )
                                        : action_message_,
                21.0f,
                true );

            return;
        }

        const std::int32_t count = static_cast<std::int32_t>( stages_.size() );
        const std::int32_t visible_rows = visible_stage_rows_;
        first_visible_stage_ = std::clamp( first_visible_stage_, 0, std::max( 0, count - visible_rows ) );
        const std::int32_t first = first_visible_stage_;
        const std::int32_t last = std::min( count, first + visible_rows );
        const float scrollbar_gutter = count > visible_stage_rows_ ? scrollbar_width_ + card_gap_ * 2.0f : 0.0f;
        const float entry_width = body_split_x_ - window_bounds_.x - scrollbar_gutter;

        for( std::int32_t index = first; index < last; ++index )
        {
            const std::int32_t row = index - first;
            const float y = body_top_ + static_cast<float>( row ) * stage_row_pitch_;
            const bool selected = index == selected_stage_ && focus_ == FocusArea::StageList;
            const rendering::Rect box = layout.rect( window_bounds_.x, y, entry_width, stage_row_height_ );

            stage_hit_rects_[ static_cast<std::size_t>( index ) ] = box;
            const bool focused = selected && focus_ == FocusArea::StageList;
            draw_choice_card( renderer,
                layout,
                rendering::Rect{ window_bounds_.x, y, entry_width, stage_row_height_ },
                selected,
                focused );

            const gamedata::PlayableStageEntry &stage = stages_[ static_cast<std::size_t>( index ) ];
            char number[ 20 ]{};
            const bool tutorial = stage.mode == gamedata::PlayMode::Tutorial;

            if( !tutorial )
            {
                std::snprintf( number, sizeof( number ), "%02d", stage.stage_number );
            }

            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 31.0f, y + 6.0f, 31.0f, 32.0f },
                number,
                16.0f,
                selected && !focused );

            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 66.0f, y + 6.0f, 182.0f, 32.0f },
                tutorial ? localization::text( "@ui.tutorial_upper" )
                         : std::string( localization::text( "@ui.stage" ) ) + number,
                18.0f,
                selected && !focused );

            if( !tutorial )
            {
                render_rank_badge( app, renderer, layout, rendering::Rect{ 264.0f, y + 6.0f, 32.0f, 32.0f }, stage );
            }
        }

        render_scrollbar( renderer, layout );

        const gamedata::PlayableStageEntry &selected = stages_[ static_cast<std::size_t>( selected_stage_ ) ];
        const float preview_width = right_width - card_gap_ * 2.0f;
        const float preview_height = preview_width * 9.0f / 16.0f;
        const float preview_y = body_top_ + ( preview_bottom_ - body_top_ - preview_height ) * 0.5f;
        const rendering::Rect image_box = layout.rect( right_x + card_gap_, preview_y, preview_width, preview_height );

        const managers::TextureHandle preview_handle = selected_stage_preview( app );
        const rendering::Texture *preview = app.texture_manager().get( preview_handle );

        if( preview != nullptr )
        {
            const auto sampling = renderer.texture_sampling();
            renderer.set_texture_sampling( { rendering::TextureFilterMode::LinearMipmap, 1.0f } );
            renderer.push_clip_rect( image_box );
            renderer.draw_texture( *preview, image_box.x, image_box.y, image_box.w, image_box.h );

            if( const auto *vignette = app.texture_manager().get( preview_vignette_ ) )
            {
                renderer.draw_texture( *vignette, image_box.x, image_box.y, image_box.w, image_box.h );
            }

            renderer.pop_clip_rect();
            renderer.set_texture_sampling( sampling );
        }
        else
        {
            if( portraits_texture != nullptr && !selected.bonus_content && selected.stage_number >= 1 &&
                selected.stage_number <= 10 )
            {
                const rendering::Rect &source =
                    stage_select_portrait_rects_[ static_cast<std::size_t>( selected.stage_number - 1 ) ];

                renderer.draw_texture_region( *portraits_texture,
                    source.x,
                    source.y,
                    source.w,
                    source.h,
                    image_box.x + image_box.w * 0.36f,
                    image_box.y + image_box.h * 0.16f,
                    image_box.w * 0.28f,
                    image_box.h * 0.52f,
                    rendering::Color{ 1.0f, 1.0f, 1.0f, 0.78f } );
            }

            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 337.0f, 225.0f, 260.0f, 29.0f },
                localization::text( "@ui.preview_unavailable_upper" ),
                16.0f );
        }

        if( const auto songs = stage_song_titles_.find( selected.stage_number ); songs != stage_song_titles_.end() )
        {
            for( std::size_t index = 0; index < songs->second.size(); ++index )
            {
                const float y = song_title_first_y_ + static_cast<float>( index ) * song_title_line_height_;
                draw_label( app,
                    renderer,
                    layout,
                    rendering::Rect{ 329.0f, y, 273.0f, song_title_line_height_ },
                    songs->second[ index ],
                    song_title_font_size_ );
            }
        }

        const auto mode = progress_mode_for_stage( selected );

        const auto difficulty =
            selected.mode == gamedata::PlayMode::Tutorial
                ? std::string( selected.tutorial_difficulty == gamedata::TutorialDifficulty::Easy ? "Easy" : "Normal" )
                : gamedata::difficulty_from_chc_path( selected.chc_path );

        draw_label( app,
            renderer,
            layout,
            rendering::Rect{ 329.0f, 322.0f, 273.0f, 18.0f },
            localization::difficulty_name( difficulty ),
            12.0f );

        if( selected.mode == gamedata::PlayMode::Tutorial )
        {
            return;
        }

        for( std::size_t index = 0; index < chart_styles_.size(); ++index )
        {
            const auto *progress =
                app.save_data().stage_progress( selected.stage_number, mode, chart_styles_[ index ] );

            const float y = 346.0f + static_cast<float>( index ) * 25.0f;
            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 329.0f, y, 35.0f, 21.0f },
                index == 0 ? "PS2" : "PSP",
                13.0f );

            const bool cleared = progress != nullptr && progress->cleared;
            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 370.0f, y, cleared ? 60.0f : 85.0f, 21.0f },
                localization::text( cleared ? "@ui.clear_upper" : "@ui.not_cleared_upper" ),
                11.0f );

            if( cleared )
            {
                auto record_stage = selected;
                record_stage.chart_style = chart_styles_[ index ];
                render_rank_badge( app, renderer, layout, rendering::Rect{ 432.0f, y, 21.0f, 21.0f }, record_stage );
            }

            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 462.0f, y, 139.0f, 21.0f },
                score_text( progress != nullptr ? progress->best_score : 0u ),
                14.0f,
                false,
                TextAlignment::Right );
        }
    }

}
