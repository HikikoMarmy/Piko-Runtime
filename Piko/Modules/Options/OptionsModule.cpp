#include "Utils/EnumUtil.hpp"
#include "Localization/Language.hpp"
#include "OptionsModule.hpp"

#include "App/piko.hpp"
#include "Modules/MainMenu/MainMenuModule.hpp"
#include "Modules/Import/ImportModule.hpp"
#include "Modules/MainMenu/TitleScreenBackdrop.hpp"
#include "Modules/Options/GameplayVisualModule.hpp"
#include "Modules/Options/GameModifiersModule.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/RenderUtil.hpp"
#include "Rendering/MenuTheme.hpp"
#include "Utils/FileUtil.hpp"

#include <SDL3/SDL.h>
#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace piko::modules
{
    void OptionsPresentation::initialize( App &app )
    {
        if( initialized_ )
        {
            return;
        }

        initialized_ = true;

        submenu_audio_loaded_ = false;

        options_music_active_ = false;

        options_music_requested_ = false;

        elapsed_seconds_ = 0.0;
        background_.initialize( app );
        circle_texture_ = TitleScreenBackdrop::create_circle_texture( app, "options/clean-circle" );

        const std::filesystem::path menu_path =
            app.game_data().resolve_runtime_asset( "MAINMENU/TEXTURES/MAINMENU/MENU.png" );

        if( std::filesystem::is_regular_file( menu_path ) )
        {
            try
            {
                menu_texture_ = app.texture_manager().load_png( "options/shared-menu-background", menu_path );
            }
            catch( const std::exception & )
            {
                menu_texture_ = {};
            }
        }

        bgm_channel_ = app.audio().claim_channel( "Options BGM", 0.8f );

        sfx_channel_ = app.audio().claim_channel( "Options SE" );
        const std::filesystem::path audio_path = app.game_data().main_menu_audio_path();

        if( !audio_path.empty() && app.audio().load_audio_bank( audio_path ) )
        {
            app.audio().preload_subsongs( { "BGM_A2", "SELECT", "OK" } );

            if( bgm_channel_ && app.audio().has_subsong( "BGM_A2" ) )
            {
                app.audio().play_subsong( bgm_channel_, "BGM_A2", true );
            }

            prepare_submenu_audio( app );
        }
    }

    void OptionsPresentation::shutdown( App &app )
    {
        if( !initialized_ )
        {
            return;
        }

        app.audio().release_channel( bgm_channel_ );
        app.audio().release_channel( sfx_channel_ );

        for( managers::TextureHandle handle : retained_textures_ )
        {
            app.texture_manager().unload( handle );
        }

        retained_textures_.clear();
        bgm_channel_ = {};

        sfx_channel_ = {};
        app.texture_manager().unload( menu_texture_ );
        app.texture_manager().unload( circle_texture_ );
        menu_texture_ = {};

        circle_texture_ = {};
        background_.shutdown();
        initialized_ = false;

        submenu_audio_loaded_ = false;

        options_music_active_ = false;

        options_music_requested_ = false;
    }

    void OptionsPresentation::update( App &app, double delta_seconds )
    {
        elapsed_seconds_ += delta_seconds;

        if( options_music_requested_ && !options_music_active_ && bgm_channel_ &&
            app.audio().is_subsong_preloaded( "BGM_B2" ) && app.audio().play_subsong( bgm_channel_, "BGM_B2", true ) )
        {
            options_music_active_ = true;
        }
    }

    void OptionsPresentation::draw( App &app, rendering::Renderer &renderer )
    {
        TitleScreenBackdrop::draw( renderer,
            background_,
            renderer.output_width(),
            renderer.output_height(),
            static_cast<float>( elapsed_seconds_ ),
            app.texture_manager().get( circle_texture_ ) );
    }

    void OptionsPresentation::play_sound( App &app, const char *name )
    {
        if( sfx_channel_ && app.audio().has_subsong( name ) )
        {
            app.audio().play_subsong( sfx_channel_, name, false );
        }
    }

    void OptionsPresentation::prepare_submenu_audio( App &app )
    {
        if( submenu_audio_loaded_ )
        {
            return;
        }

        std::filesystem::path manifest = app.game_data().resolve_runtime_asset( "SOUND/AUDIO/SUBMENU/audio.json" );

        if( !std::filesystem::is_regular_file( manifest ) )
        {
            manifest = app.game_data().resolve_runtime_asset( "MAINMENU/AUDIO/SUBMENU/audio.json" );
        }

        if( !std::filesystem::is_regular_file( manifest ) ||
            !app.audio().merge_audio_bank( manifest.parent_path(), "BGM_B2" ) )
        {
            std::cerr << "[Piko UI] Converted SUBMENU audio is unavailable; Options music unchanged\n";
            return;
        }

        submenu_audio_loaded_ = true;
        app.audio().preload_subsong_async( "BGM_B2" );
    }

    void OptionsPresentation::set_options_music( App &app, bool options )
    {
        if( !initialized_ )
        {
            return;
        }

        options_music_requested_ = options;

        if( options )
        {
            if( !submenu_audio_loaded_ )
            {
                prepare_submenu_audio( app );
            }

            update( app, 0.0 );
        }
        else if( options_music_active_ && bgm_channel_ && app.audio().play_subsong( bgm_channel_, "BGM_A2", true ) )
        {
            options_music_active_ = false;
        }
    }

    managers::TextureHandle OptionsPresentation::menu_texture() const
    {
        return menu_texture_;
    }

    managers::TextureHandle OptionsPresentation::circle_texture() const
    {
        return circle_texture_;
    }

    double OptionsPresentation::elapsed_seconds() const
    {
        return elapsed_seconds_;
    }

    void OptionsPresentation::retain_texture( managers::TextureHandle handle )
    {
        if( !handle )
        {
            return;
        }

        const auto existing = std::find_if( retained_textures_.begin(),
            retained_textures_.end(),
            [ handle ]( managers::TextureHandle other )
            {
                return other.id == handle.id;
            } );
        if( existing == retained_textures_.end() )
        {
            retained_textures_.push_back( handle );
        }
    }

    OptionsModule::OptionsModule( std::shared_ptr<OptionsPresentation> presentation, EntryPage entry_page )
        : presentation_( std::move( presentation ) ), entry_page_( entry_page )
    {
    }

    std::vector<OptionsModule::Row> OptionsModule::active_rows() const
    {
        switch( page_ )
        {
            case Page::Graphics:
                return {
                    Row::RenderResolution,
                    Row::Msaa,
                    Row::TextureFiltering,
                    Row::Anisotropy,
                    Row::Lighting,
                    Row::Bloom,
                    Row::Back
                };
            case Page::Audio:
                return { Row::Stereo, Row::InstrumentRouting, Row::InstrumentMuting, Row::BackgroundFx, Row::Back };
            case Page::Controller:
                return { Row::InputDevice, Row::ControllerOrientation, Row::MouseControls, Row::Back };
            case Page::Language:
                return { Row::UiLanguage, Row::Back };
            case Page::GameData:
                return { Row::ImportGameData, Row::GameLanguage, Row::ChartStyle, Row::Back };
            case Page::Main:
                return {
                    Row::Languages,
                    Row::PlayMovies,
                    Row::GameData,
                    Row::Graphics,
                    Row::Audio,
                    Row::Controller,
                    Row::GameplayVisuals,
                    Row::GameModifiers,
                    Row::Back
                };
        }

        return { Row::Back };
    }

    OptionsModule::Row OptionsModule::selected_row_kind() const
    {
        return active_rows().at( static_cast<std::size_t>( selected_row_ ) );
    }

    void OptionsModule::set_page( Page page )
    {
        if( navigation_ )
        {
            navigation_->remember( menu_locations_[ utils::enum_index( page_ ) ], selected_row_ );
        }

        page_ = page;

        const auto position = navigation_ ? navigation_->restore( menu_locations_[ utils::enum_index( page_ ) ],
                                                static_cast<std::int32_t>( active_rows().size() ) )
                                          : managers::MenuPosition{};

        selected_row_ = position.selected_row;
    }

    rendering::Rect OptionsModule::reference_row_rect( std::int32_t index ) const
    {
        if( index < 0 || index >= static_cast<std::int32_t>( active_rows().size() ) )
        {
            return {};
        }

        return rendering::Rect{
            row_x,
            page_ == Page::Main
                ? row_y[ static_cast<std::size_t>( index ) ]
                : 178.0f + ( row_height + rendering::menu_theme::control_gap ) * static_cast<float>( index ),
            row_width,
            row_height
        };
    }

    rendering::Rect OptionsModule::row_rect( std::int32_t index,
        std::int32_t output_width,
        std::int32_t output_height ) const
    {
        return rendering::make_logical_viewport( output_width, output_height, reference_width, reference_height )
            .rect( reference_row_rect( index ) );
    }

    rendering::Rect OptionsModule::controller_card_rect( std::int32_t orientation )
    {
        return { 126.0f + 352.0f * static_cast<float>( orientation ), 366.0f, 324.0f, 306.0f };
    }

    std::int32_t OptionsModule::hit_test_controller_card( float x, float y ) const
    {
        if( page_ != Page::Controller )
        {
            return -1;
        }

        const auto layout = rendering::make_logical_viewport( last_output_width_,
            last_output_height_,
            reference_width,
            reference_height );

        for( std::int32_t index = 0; index < 3; ++index )
        {
            if( rendering::point_inside( layout.rect( controller_card_rect( index ) ), x, y ) )
            {
                return index;
            }
        }

        return -1;
    }

    const char *OptionsModule::language_name( gamedata::GameLanguage language )
    {
        switch( language )
        {
            case gamedata::GameLanguage::Japanese:
                return localization::text( "@ui.japanese" );
            case gamedata::GameLanguage::English:
                return localization::text( "@ui.english" );
            case gamedata::GameLanguage::French:
                return localization::text( "@ui.french" );
            case gamedata::GameLanguage::German:
                return localization::text( "@ui.german" );
            case gamedata::GameLanguage::Default:
                return localization::text( "@ui.default" );
        }

        return localization::text( "@ui.default" );
    }

    const char *OptionsModule::on_off( bool enabled )
    {
        return enabled ? localization::text( "@ui.on_upper" ) : localization::text( "@ui.off_upper" );
    }

    void OptionsModule::draw_texture_left_centered( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const rendering::LogicalViewport &layout,
        float x,
        float center_y,
        float raster_scale )
    {
        const float scale = layout.scale / raster_scale;
        renderer.draw_texture( texture,
            layout.to_x( x ),
            layout.to_y( center_y ) - static_cast<float>( texture.height ) * scale * 0.5f,
            static_cast<float>( texture.width ) * scale,
            static_cast<float>( texture.height ) * scale );
    }

    void OptionsModule::draw_texture_fit( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const rendering::LogicalViewport &layout,
        const rendering::Rect &reference_bounds,
        float raster_scale,
        bool centered )
    {
        const float natural_scale = layout.scale / raster_scale;
        const float maximum_width = reference_bounds.w * layout.scale;
        const float maximum_height = reference_bounds.h * layout.scale;
        float scale = natural_scale;
        scale = std::min( scale, maximum_width / static_cast<float>( std::max( texture.width, 1 ) ) );

        scale = std::min( scale, maximum_height / static_cast<float>( std::max( texture.height, 1 ) ) );
        const float width = static_cast<float>( texture.width ) * scale;
        const float height = static_cast<float>( texture.height ) * scale;
        renderer.draw_texture( texture,
            layout.to_x( reference_bounds.x ) +
                ( reference_bounds.w * layout.scale - width ) * ( centered ? 0.5f : 1.0f ),
            layout.to_y( reference_bounds.y ) + ( reference_bounds.h * layout.scale - height ) * 0.5f,
            width,
            height );
    }

    void OptionsModule::enter( App &app )
    {
        elapsed_seconds_ = 0.0;

        navigation_ = &app.menu_navigation();

        page_ = entry_page_ == EntryPage::GameData ? Page::GameData : Page::Main;

        const auto position = navigation_->restore( menu_locations_[ utils::enum_index( page_ ) ],
            static_cast<std::int32_t>( active_rows().size() ) );

        selected_row_ = position.selected_row;

        pending_language_ = app.game_data().language();

        app.settings().chart_style = app.game_data().resolve_chart_style( app.settings().chart_style );

        if( !row_enabled( app, selected_row_kind() ) )
        {
            selected_row_ = ( selected_row_ + 1 ) % static_cast<std::int32_t>( active_rows().size() );
        }

        if( !presentation_ )
        {
            presentation_ = std::make_shared<OptionsPresentation>();
        }

        presentation_->initialize( app );
        load_visual_assets( app );
        presentation_->set_options_music( app, true );
    }

    void OptionsModule::leave( App &app )
    {
        navigation_->remember( menu_locations_[ utils::enum_index( page_ ) ], selected_row_ );

        const bool language_changed = pending_language_ != app.game_data().language();

        if( language_changed )
        {
            std::string message;

            if( !app.set_game_language( pending_language_, message ) )
            {
                std::cerr << "[Piko UI] Language change failed: " << message << '\n';
            }
        }

        managers::save_game_settings( app.settings() );

        if( !language_changed && presentation_.use_count() > 1 )
        {
            retain_visual_assets();
        }
        else
        {
            unload_visual_assets( app );
        }

        if( language_changed || presentation_.use_count() == 1 )
        {
            presentation_->shutdown( app );
        }
    }

    void OptionsModule::handle_event( App &, const SDL_Event & )
    {
    }

    bool OptionsModule::row_enabled( const App &app, Row row ) const
    {
        if( row == Row::Msaa && app.settings().render_resolution_percent > 100 )
        {
            return false;
        }

        if( row == Row::Anisotropy &&
            app.settings().texture_filtering_mode != managers::TextureFilteringMode::Anisotropic )
        {
            return false;
        }

        return row != Row::Count;
    }

    bool OptionsModule::row_adjustable( Row row ) const
    {
        return row == Row::ChartStyle || row == Row::UiLanguage || row == Row::GameLanguage || row == Row::PlayMovies ||
               row == Row::RenderResolution || row == Row::Msaa || row == Row::Lighting || row == Row::Bloom ||
               row == Row::TextureFiltering || row == Row::Anisotropy || row == Row::Stereo ||
               row == Row::InstrumentRouting || row == Row::InstrumentMuting || row == Row::BackgroundFx ||
               row == Row::InputDevice || row == Row::ControllerOrientation || row == Row::MouseControls;
    }

    void OptionsModule::move_selection( App &app, std::int32_t delta )
    {
        std::int32_t next = selected_row_;
        do
        {
            next = ( next + delta ) % static_cast<std::int32_t>( active_rows().size() );

            if( next < 0 )
            {
                next += static_cast<std::int32_t>( active_rows().size() );
            }
        } while( !row_enabled( app, active_rows().at( static_cast<std::size_t>( next ) ) ) );

        if( next != selected_row_ )
        {
            selected_row_ = next;
            presentation_->play_sound( app, "SELECT" );
        }
    }

    void OptionsModule::set_selection( App &app, std::int32_t index )
    {
        if( index < 0 || index >= static_cast<std::int32_t>( active_rows().size() ) )
        {
            return;
        }

        if( !row_enabled( app, active_rows().at( static_cast<std::size_t>( index ) ) ) || index == selected_row_ )
        {
            return;
        }

        selected_row_ = index;
        presentation_->play_sound( app, "SELECT" );
    }

    const char *OptionsModule::row_label( Row row ) const
    {
        switch( row )
        {
            case Row::GameData:
            {
                const char *key = "@ui.options.game_data";
                const char *label = localization::text( key );
                return std::string_view( label ) == key ? "Game Data" : label;
            }
            case Row::ChartStyle:
                return localization::text( "@ui.chart_style" );
            case Row::ImportGameData:
                return localization::text( "@ui.options.import_game_data" );
            case Row::Languages:
                return localization::text( "@ui.options.languages" );
            case Row::UiLanguage:
                return localization::text( "@ui.options.ui_language" );
            case Row::GameLanguage:
                return localization::text( "@ui.options.game_language" );
            case Row::PlayMovies:
                return localization::text( "@ui.play_movies" );
            case Row::Graphics:
                return localization::text( "@ui.graphics" );
            case Row::Audio:
                return localization::text( "@ui.audio" );
            case Row::Controller:
                return localization::text( "@ui.controller" );
            case Row::GameplayVisuals:
                return localization::text( "@ui.interface.title" );
            case Row::GameModifiers:
                return localization::text( "@ui.game_modifiers" );
            case Row::Back:
                return localization::text( "@ui.back" );
            case Row::RenderResolution:
                return localization::text( "@ui.rendering_resolution" );
            case Row::Msaa:
                return "MSAA";
            case Row::Lighting:
                return localization::text( "@ui.lighting" );
            case Row::Bloom:
                return localization::text( "@ui.bloom" );
            case Row::TextureFiltering:
                return localization::text( "@ui.texture_filtering" );
            case Row::Anisotropy:
                return localization::text( "@ui.anisotropy" );
            case Row::Stereo:
                return localization::text( "@ui.output" );
            case Row::InstrumentRouting:
                return localization::text( "@ui.instrument_routing" );
            case Row::InstrumentMuting:
                return localization::text( "@ui.instrument_muting" );
            case Row::BackgroundFx:
                return localization::text( "@ui.options.background_fx" );
            case Row::InputDevice:
                return localization::text( "@ui.input_device" );
            case Row::ControllerOrientation:
                return localization::text( "@ui.controller_orientation" );
            case Row::MouseControls:
                return localization::text( "@ui.mouse_controls" );
            case Row::Count:
                break;
        }

        return "";
    }

    std::string OptionsModule::row_value( const App &app, Row row ) const
    {
        const managers::GameSettings &settings = app.settings();

        switch( row )
        {
            case Row::UiLanguage:
            {
                const auto &languages = localization::languages();

                const auto found = std::find_if( languages.begin(),
                    languages.end(),
                    []( const auto &entry )
                    {
                        return entry.id == localization::language_id();
                    } );
                return found != languages.end() ? found->name : localization::language_id();
            }
            case Row::GameLanguage:
                return language_name( pending_language_ );
            case Row::ChartStyle:
            {
                const bool lives = settings.chart_style == gamedata::ChartStyle::GitarooManLives;
                const char *key = lives ? "@ui.chart_style.gitaroo_man_lives" : "@ui.chart_style.gitaroo_man";

                const auto label = localization::translate( key );
                return label == key ? lives ? "Gitaroo Man Lives!" : "Gitaroo Man" : label;
            }
            case Row::PlayMovies:
                return on_off( settings.play_movies );
            case Row::GameData:
            case Row::ImportGameData:
            case Row::Languages:
            case Row::Graphics:
            case Row::Audio:
            case Row::Controller:
                return {};
            case Row::RenderResolution:
                return std::to_string( settings.render_resolution_percent ) + "%";
            case Row::Msaa:
                return settings.msaa_enabled && settings.msaa_samples > 0
                           ? std::to_string( settings.msaa_samples ) + "x"
                           : localization::text( "@ui.off_upper" );
            case Row::Lighting:
                return settings.per_pixel_lighting ? localization::text( "@ui.per_pixel" )
                                                   : localization::text( "@ui.vertex" );
            case Row::Bloom:
                switch( settings.bloom_preset )
                {
                    case managers::BloomPreset::Low:
                        return localization::text( "@ui.bloom.low" );
                    case managers::BloomPreset::Medium:
                        return localization::text( "@ui.bloom.medium" );
                    case managers::BloomPreset::High:
                        return localization::text( "@ui.bloom.high" );
                    default:
                        return localization::text( "@ui.off_upper" );
                }
            case Row::TextureFiltering:
                switch( settings.texture_filtering_mode )
                {
                    case managers::TextureFilteringMode::Nearest:
                        return localization::text( "@ui.nearest" );
                    case managers::TextureFilteringMode::Linear:
                        return localization::text( "@ui.linear" );
                    case managers::TextureFilteringMode::LinearMipmap:
                        return localization::text( "@ui.mipmap" );
                    case managers::TextureFilteringMode::Anisotropic:
                        return localization::text( "@ui.anisotropic" );
                }

                return localization::text( "@ui.linear" );
            case Row::Anisotropy:
                return std::to_string( static_cast<std::int32_t>( settings.texture_anisotropy ) ) + "x";
            case Row::Stereo:
                return settings.stereo_audio ? localization::text( "@ui.stereo" ) : localization::text( "@ui.mono" );
            case Row::InstrumentRouting:
                return settings.instrument_routing == 1   ? localization::text( "@ui.reversed" )
                       : settings.instrument_routing == 2 ? localization::text( "@ui.centered" )
                                                          : localization::text( "@ui.original" );
            case Row::InstrumentMuting:
                return settings.instrument_playback == runtime::audio::InstrumentPlaybackMode::TraceEnd
                           ? localization::text( "@ui.trace_end" )
                           : localization::text( "@ui.original" );
            case Row::BackgroundFx:
                return on_off( settings.background_fx_enabled );
            case Row::MouseControls:
                return on_off( settings.mouse_controls_enabled );
            case Row::InputDevice:
                return app.input().gamepad_connected() ? app.input().gamepad_name()
                                                       : localization::text( "@ui.no_controller" );
            case Row::ControllerOrientation:
                return settings.controller_orientation == managers::ControllerOrientation::TypeA
                           ? localization::text( "@ui.type_a" )
                       : settings.controller_orientation == managers::ControllerOrientation::TypeB
                           ? localization::text( "@ui.type_b" )
                           : localization::text( "@ui.type_c" );
            case Row::GameplayVisuals:
            case Row::GameModifiers:
            case Row::Back:
                return {};
            case Row::Count:
                break;
        }

        return "";
    }

    void OptionsModule::adjust_selected( App &app, std::int32_t direction )
    {
        if( direction == 0 )
        {
            return;
        }

        managers::GameSettings &settings = app.settings();
        const Row row = selected_row_kind();

        if( !row_enabled( app, row ) )
        {
            return;
        }

        switch( row )
        {
            case Row::UiLanguage:
            {
                const auto languages = localization::languages();

                if( languages.empty() )
                {
                    break;
                }

                const auto current = std::find_if( languages.begin(),
                    languages.end(),
                    []( const auto &entry )
                    {
                        return entry.id == localization::language_id();
                    } );
                const auto index = current == languages.end() ? 0 : std::distance( languages.begin(), current );

                const auto count = static_cast<std::int32_t>( languages.size() );

                const auto next = ( static_cast<std::int32_t>( index ) + direction + count ) % count;
                std::string error;

                if( app.set_ui_language( languages[ static_cast<std::size_t>( next ) ].id, error ) )
                {
                    unload_visual_assets( app );
                    title_font_.clear();
                    load_visual_assets( app );
                }
                else
                {
                    std::cerr << "[Piko UI] Language change failed: " << error << '\n';
                }
                break;
            }
            case Row::GameLanguage:
            {
                const std::vector<gamedata::GameLanguage> languages = app.game_data().available_languages();

                if( languages.empty() )
                {
                    return;
                }

                const gamedata::GameLanguage current = pending_language_;
                auto current_it = std::find( languages.begin(), languages.end(), current );
                std::int32_t index = current_it == languages.end()
                                         ? 0
                                         : static_cast<std::int32_t>( std::distance( languages.begin(), current_it ) );

                index = ( index + direction ) % static_cast<std::int32_t>( languages.size() );

                if( index < 0 )
                {
                    index += static_cast<std::int32_t>( languages.size() );
                }

                pending_language_ = languages[ static_cast<std::size_t>( index ) ];
                break;
            }

            case Row::ChartStyle:
            {
                constexpr std::array<gamedata::ChartStyle, 2> styles{
                    gamedata::ChartStyle::GitarooMan,
                    gamedata::ChartStyle::GitarooManLives
                };

                const auto count = static_cast<std::int32_t>( styles.size() );
                auto index = utils::to_int( settings.chart_style );

                for( std::int32_t step = 0; step < count; ++step )
                {
                    index = ( index + direction + count ) % count;

                    if( app.game_data().chart_style_available( styles[ static_cast<std::size_t>( index ) ] ) )
                    {
                        settings.chart_style = styles[ static_cast<std::size_t>( index ) ];
                        break;
                    }
                }
                break;
            }
            case Row::PlayMovies:
                settings.play_movies = !settings.play_movies;
                break;

            case Row::Stereo:
                settings.stereo_audio = !settings.stereo_audio;
                app.audio().set_stereo_output( settings.stereo_audio );
                break;

            case Row::Bloom:
                settings.bloom_preset =
                    utils::to_enum<managers::BloomPreset>( ( utils::to_int( settings.bloom_preset ) + direction +
                                                               utils::to_int( managers::BloomPreset::Count ) ) %
                                                           utils::to_int( managers::BloomPreset::Count ) );
                break;

            case Row::RenderResolution:
            {
                constexpr std::array<std::int32_t, 9> levels{
                    25,
                    50,
                    75,
                    100,
                    110,
                    125,
                    150,
                    175,
                    200
                };

                const auto found = std::find( levels.begin(), levels.end(), settings.render_resolution_percent );
                std::int32_t current = found == levels.end() ? 3 : static_cast<std::int32_t>( found - levels.begin() );
                current = ( current + direction + static_cast<std::int32_t>( levels.size() ) ) %
                          static_cast<std::int32_t>( levels.size() );

                settings.render_resolution_percent = levels[ static_cast<std::size_t>( current ) ];
                app.renderer().set_msaa_enabled( settings.msaa_enabled && settings.render_resolution_percent <= 100 );
                break;
            }
            case Row::Msaa:
            {
                GLint supported = 0;
                glGetIntegerv( GL_MAX_SAMPLES, &supported );
                std::vector<std::int32_t> levels{
                    0
                };

                for( const std::int32_t samples : { 2, 4, 8 } )
                {
                    if( samples <= supported )
                    {
                        levels.push_back( samples );
                    }
                }

                const std::int32_t value = settings.msaa_enabled ? settings.msaa_samples : 0;

                const auto found = std::find( levels.begin(), levels.end(), value );
                const std::int32_t current =
                    found == levels.end() ? 0 : static_cast<std::int32_t>( found - levels.begin() );

                const std::int32_t next = ( current + direction + static_cast<std::int32_t>( levels.size() ) ) %
                                          static_cast<std::int32_t>( levels.size() );

                settings.msaa_enabled = levels[ static_cast<std::size_t>( next ) ] > 0;

                if( settings.msaa_enabled )
                {
                    settings.msaa_samples = levels[ static_cast<std::size_t>( next ) ];
                }

                app.renderer().set_stage_msaa_samples( settings.msaa_samples );
                app.renderer().set_msaa_enabled( settings.msaa_enabled );
                break;
            }
            case Row::Lighting:
                settings.per_pixel_lighting = !settings.per_pixel_lighting;
                break;
            case Row::TextureFiltering:
            {
                const std::int32_t current = utils::to_int( settings.texture_filtering_mode );
                settings.texture_filtering_mode =
                    utils::to_enum<managers::TextureFilteringMode>( ( current + direction + 4 ) % 4 );

                rendering::TextureSamplingState sampling = app.renderer().texture_sampling();
                sampling.mode =
                    utils::to_enum<rendering::TextureFilterMode>( utils::to_int( settings.texture_filtering_mode ) );

                app.renderer().set_texture_sampling( sampling );
                break;
            }
            case Row::Anisotropy:
            {
                constexpr std::array<std::int32_t, 5> levels{
                    1,
                    2,
                    4,
                    8,
                    16
                };

                const std::int32_t current = static_cast<std::int32_t>( settings.texture_anisotropy );

                const auto found = std::find( levels.begin(), levels.end(), current );
                const std::int32_t index =
                    found == levels.end() ? 2 : static_cast<std::int32_t>( found - levels.begin() );

                settings.texture_anisotropy =
                    static_cast<float>( levels[ ( index + direction + levels.size() ) % levels.size() ] );

                rendering::TextureSamplingState sampling = app.renderer().texture_sampling();
                sampling.anisotropy = settings.texture_anisotropy;
                app.renderer().set_texture_sampling( sampling );
                break;
            }
            case Row::InstrumentRouting:
                settings.instrument_routing = ( settings.instrument_routing + direction + 3 ) % 3;
                app.audio().set_instrument_routing( settings.instrument_routing );
                break;
            case Row::InstrumentMuting:
            {
                const auto count = utils::to_int( runtime::audio::InstrumentPlaybackMode::Count );

                const auto selected = ( utils::to_int( settings.instrument_playback ) + direction + count ) % count;
                settings.instrument_playback = utils::to_enum<runtime::audio::InstrumentPlaybackMode>( selected );
                break;
            }
            case Row::BackgroundFx:
                settings.background_fx_enabled = !settings.background_fx_enabled;
                app.audio().set_background_fx_enabled( settings.background_fx_enabled );
                break;
            case Row::InputDevice:
            {
                const std::vector<managers::InputManager::Device> devices = app.input().available_devices();

                if( devices.empty() )
                {
                    return;
                }

                std::int32_t current = 0;

                for( std::int32_t index = 0; index < static_cast<std::int32_t>( devices.size() ); ++index )
                {
                    if( devices[ index ].id == app.input().selected_device() )
                    {
                        current = index;
                    }
                }

                const auto &device = devices[ ( current + direction + devices.size() ) % devices.size() ];

                if( app.input().select_device( device.id ) )
                {
                    settings.input_device_name = device.name;
                }
                break;
            }
            case Row::ControllerOrientation:
                settings.controller_orientation = managers::controller_orientation_from_index(
                    ( utils::to_int( settings.controller_orientation ) + direction + 3 ) % 3 );

                app.input().set_controller_orientation( settings.controller_orientation );
                break;
            case Row::MouseControls:
                settings.mouse_controls_enabled = !settings.mouse_controls_enabled;
                break;

            case Row::GameData:
            case Row::ImportGameData:
            case Row::Languages:
            case Row::Graphics:
            case Row::Audio:
            case Row::Controller:
            case Row::GameplayVisuals:
            case Row::GameModifiers:
            case Row::Back:
            case Row::Count:
                return;
        }

        presentation_->play_sound( app, "SELECT" );
    }

    void OptionsModule::confirm_selection( App &app )
    {
        const Row row = selected_row_kind();

        if( !row_enabled( app, row ) )
        {
            return;
        }

        if( row_adjustable( row ) )
        {
            adjust_selected( app, 1 );
            return;
        }

        if( row == Row::GameData )
        {
            set_page( Page::GameData );
            presentation_->play_sound( app, "OK" );
            return;
        }

        if( row == Row::ImportGameData )
        {
            presentation_->play_sound( app, "OK" );
            app.change_module( std::make_unique<ImportModule>( ImportModule::ReturnTarget::Options ) );
            return;
        }

        if( row == Row::Languages )
        {
            set_page( Page::Language );
            presentation_->play_sound( app, "OK" );
            return;
        }

        if( row == Row::Graphics || row == Row::Audio || row == Row::Controller )
        {
            set_page( row == Row::Graphics ? Page::Graphics : row == Row::Audio ? Page::Audio : Page::Controller );
            presentation_->play_sound( app, "OK" );
            return;
        }

        if( row == Row::GameplayVisuals )
        {
            presentation_->play_sound( app, "OK" );
            app.change_module( std::make_unique<GameplayVisualModule>( presentation_ ) );
            return;
        }

        if( row == Row::GameModifiers )
        {
            presentation_->play_sound( app, "OK" );
            app.change_module( std::make_unique<GameModifiersModule>( presentation_ ) );
            return;
        }

        if( row == Row::Back )
        {
            presentation_->play_sound( app, "OK" );

            if( page_ == Page::Main )
            {
                app.change_module( std::make_unique<MainMenuModule>( presentation_ ) );
            }
            else
            {
                set_page( Page::Main );
            }
        }
    }

    std::int32_t OptionsModule::hit_test_row( const App &app, float x, float y ) const
    {
        if( hit_test_controller_card( x, y ) >= 0 )
        {
            return 1;
        }

        for( std::int32_t index = 0; index < static_cast<std::int32_t>( active_rows().size() ); ++index )
        {
            const Row row = active_rows().at( static_cast<std::size_t>( index ) );

            if( row_enabled( app, row ) &&
                rendering::point_inside( row_rect( index, last_output_width_, last_output_height_ ), x, y ) )
            {
                return index;
            }
        }

        return -1;
    }

    void OptionsModule::update( App &app, double delta_seconds )
    {
        elapsed_seconds_ += delta_seconds;
        presentation_->update( app, delta_seconds );
        const managers::InputManager &input = app.input();

        if( input.pressed( managers::InputAction::MenuUp ) )
        {
            move_selection( app, -1 );
        }

        if( input.pressed( managers::InputAction::MenuDown ) )
        {
            move_selection( app, 1 );
        }

        if( input.pressed( managers::InputAction::MenuLeft ) )
        {
            adjust_selected( app, -1 );
        }

        if( input.pressed( managers::InputAction::MenuRight ) )
        {
            adjust_selected( app, 1 );
        }

        const managers::MouseState &mouse = input.mouse();

        if( mouse.moved )
        {
            if( const std::int32_t hovered = hit_test_row( app, mouse.x, mouse.y ); hovered >= 0 )
            {
                set_selection( app, hovered );
            }
        }

        if( mouse.left_clicked )
        {
            if( const std::int32_t orientation = hit_test_controller_card( mouse.x, mouse.y ); orientation >= 0 )
            {
                set_selection( app, 1 );

                const managers::ControllerOrientation selected_orientation =
                    managers::controller_orientation_from_index( orientation );

                if( app.settings().controller_orientation != selected_orientation )
                {
                    app.settings().controller_orientation = selected_orientation;
                    app.input().set_controller_orientation( selected_orientation );
                    presentation_->play_sound( app, "OK" );
                }
            }
            else if( const std::int32_t clicked = hit_test_row( app, mouse.x, mouse.y ); clicked >= 0 )
            {
                set_selection( app, clicked );

                const auto box = row_rect( clicked, last_output_width_, last_output_height_ );

                const auto layout = rendering::make_logical_viewport( last_output_width_,
                    last_output_height_,
                    reference_width,
                    reference_height );

                const auto direction =
                    rendering::menu_theme::adjustment_direction( box, layout.scale, mouse.x, mouse.y );

                if( row_adjustable( active_rows()[ static_cast<std::size_t>( clicked ) ] ) && direction != 0 )
                {
                    adjust_selected( app, direction );
                }
                else
                {
                    confirm_selection( app );
                }
            }
        }
        else if( input.pressed( managers::InputAction::Confirm ) )
        {
            confirm_selection( app );
        }

        if( input.pressed( managers::InputAction::Back ) && !mouse.right_clicked )
        {
            if( page_ == Page::Main )
            {
                app.change_module( std::make_unique<MainMenuModule>( presentation_ ) );
            }
            else
            {
                set_page( Page::Main );
            }

            presentation_->play_sound( app, "OK" );
        }
    }

    bool OptionsModule::load_fonts( App &app )
    {
        std::string error;
        const std::filesystem::path title_path = piko::utils::find_resource_file( app.game_data().application_root(),
            std::filesystem::path( "fonts" ) / "Anak Paud.ttf" );

        if( !localization::load_menu_font( title_font_, title_path, &error ) )
        {
            std::cerr << "[Piko UI] Options title font failed: " << error << '\n';
            return false;
        }

        return true;
    }

    managers::TextureHandle OptionsModule::create_text_texture( App &app,
        const std::string &name,
        const std::string &text,
        float pixel_height,
        const rendering::StrokedTextStyle &style )
    {
        if( managers::TextureHandle cached = app.texture_manager().find( localization::texture_key( name ) ); cached )
        {
            return cached;
        }

        std::string error;
        const rendering::RasterizedText raster = title_font_.rasterize_stroked( text, pixel_height, style, &error );

        if( !raster.valid() )
        {
            std::cerr << "[Piko UI] Options text failed: " << error << '\n';
            return {};
        }

        return app.texture_manager().create_rgba( localization::texture_key( name ),
            raster.rgba.data(),
            raster.width,
            raster.height );
    }

    OptionsModule::TextTextures OptionsModule::create_text_pair( App &app,
        const std::string &name,
        const std::string &text,
        float pixel_height,
        const rendering::Color &normal_color,
        const rendering::Color &selected_color )
    {
        const rendering::StrokedTextStyle normal_style{
            normal_color,
            rendering::color_from_rgb( 0x0D005Au ),
            rendering::color_from_rgb( 0x4287DDu ),
            8,
            17,
            1.5f
        };

        const rendering::StrokedTextStyle selected_style{
            selected_color,
            rendering::color_from_rgb( 0x0D005Au ),
            rendering::color_from_rgb( 0xFFD469u ),
            8,
            17,
            1.5f
        };

        return TextTextures{
            create_text_texture( app, name + "_normal", text, pixel_height * title_raster_scale, normal_style ),
            create_text_texture( app, name + "_selected", text, pixel_height * title_raster_scale, selected_style )
        };
    }

    bool OptionsModule::create_static_textures( App &app )
    {
        heading_texture_ = app.texture_manager().find( localization::texture_key( "options/heading" ) );
        std::string error;
        const rendering::StrokedTextStyle heading_style{
            rendering::color_from_rgb( 0xEFBB4Bu ),
            rendering::color_from_rgb( 0x0D005Au ),
            rendering::color_from_rgb( 0x4287DDu ),
            8,
            17,
            1.5f
        };

        if( !heading_texture_ )
        {
            const rendering::RasterizedText heading =
                title_font_.rasterize_stroked( localization::text( "@ui.options_upper" ),
                    52.0f * title_raster_scale,
                    heading_style,
                    &error );

            if( heading.valid() )
            {
                heading_texture_ = app.texture_manager().create_rgba( localization::texture_key( "options/heading" ),
                    heading.rgba.data(),
                    heading.width,
                    heading.height );
            }
        }

        const rendering::Color label_normal = rendering::color_from_rgb( rendering::menu_theme::normal_text_rgb );
        const rendering::Color label_selected = rendering::color_from_rgb( rendering::menu_theme::selected_text_rgb );

        for( std::size_t index = 0; index < row_count; ++index )
        {
            label_textures_[ index ] = create_text_pair( app,
                "options/label_" + std::to_string( index ),
                row_label( utils::to_enum<Row>( index ) ),
                body_font_height,
                label_normal,
                label_selected );
        }

        const rendering::StrokedTextStyle controller_style{
            rendering::color_from_rgb( 0xD7F4FFu ),
            rendering::color_from_rgb( 0x0D005Au ),
            rendering::color_from_rgb( 0x4287DDu ),
            8,
            17,
            1.5f
        };

        constexpr std::array<const char *, 3> types{
            "@ui.type_a",
            "@ui.type_b",
            "@ui.type_c"
        };

        for( std::size_t index = 0; index < types.size(); ++index )
        {
            controller_label_textures_[ index ] = create_text_texture( app,
                "options/controller-type-" + std::to_string( index ),
                types[ index ],
                24.0f * title_raster_scale,
                controller_style );
        }

        const rendering::StrokedTextStyle section_style{
            rendering::color_from_rgb( 0x8E2943u ),
            rendering::color_from_rgb( 0xFEF67Au ),
            rendering::color_from_rgb( 0xFFC2FAu ),
            8,
            17,
            1.5f
        };

        system_header_texture_ = create_text_texture( app,
            "options/system_header",
            localization::text( "@ui.system_upper" ),
            section_font_height * title_raster_scale,
            section_style );

        gameplay_header_texture_ = create_text_texture( app,
            "options/gameplay_header",
            localization::text( "@ui.game_play_upper" ),
            section_font_height * title_raster_scale,
            section_style );

        constexpr std::array<const char *, 5> submenu_names{
            "@ui.graphics_upper",
            "@ui.audio_upper",
            "@ui.controller_upper",
            "@ui.options.languages",
            "@ui.game_data_upper"
        };

        for( std::size_t index = 0; index < submenu_names.size(); ++index )
        {
            submenu_header_textures_[ index ] = create_text_texture( app,
                "options/submenu-header-" + std::to_string( index ),
                index == utils::enum_index( Page::GameData ) - 1 &&
                        std::string_view( localization::text( submenu_names[ index ] ) ) == submenu_names[ index ]
                    ? "GAME DATA"
                    : submenu_names[ index ],
                section_font_height * title_raster_scale,
                section_style );
        }

        return heading_texture_.valid() && create_value_textures( app );
    }

    bool OptionsModule::create_value_textures( App &app )
    {
        const rendering::Color value_normal = rendering::color_from_rgb( rendering::menu_theme::normal_text_rgb );
        const rendering::Color value_selected = rendering::color_from_rgb( rendering::menu_theme::selected_text_rgb );

        const auto cache_value =
            [ & ]( const std::string &key, const rendering::Color &normal, const rendering::Color &selected )
        {
            value_texture_cache_.emplace( key,
                create_text_pair( app,
                    "options/value_" + std::to_string( value_texture_cache_.size() ),
                    key,
                    value_font_height,
                    normal,
                    selected ) );
        };

        constexpr std::array<const char *, 12> values{
            "@ui.default",
            "@ui.japanese",
            "@ui.english",
            "@ui.french",
            "@ui.german",
            "@ui.on_upper",
            "@ui.off_upper",
            "@ui.stereo",
            "@ui.mono",
            "@ui.bloom.low",
            "@ui.bloom.medium",
            "@ui.bloom.high"
        };

        for( const char *value : values )
        {
            cache_value( localization::text( value ), value_normal, value_selected );
        }

        return !value_texture_cache_.empty();
    }

    const OptionsModule::TextTextures *OptionsModule::find_value_texture( App &app, const std::string &value )
    {
        auto found = value_texture_cache_.find( value );

        if( found == value_texture_cache_.end() && !value.empty() )
        {
            const std::string name = "options/value-dynamic-" + std::to_string( std::hash<std::string>{}( value ) );
            const rendering::Color normal = rendering::color_from_rgb( 0xEFBB4Bu );
            const rendering::Color selected = rendering::color_from_rgb( rendering::menu_theme::selected_text_rgb );
            found = value_texture_cache_
                        .emplace( value, create_text_pair( app, name, value, value_font_height, normal, selected ) )
                        .first;
        }

        return found == value_texture_cache_.end() ? nullptr : &found->second;
    }

    bool OptionsModule::load_visual_assets( App &app )
    {
        unload_visual_assets( app );

        const auto atlas_path = app.game_data().resolve_runtime_asset( "OPTION/TEXTURES/OPTION/OPTION.png" );

        if( std::filesystem::is_regular_file( atlas_path ) )
        {
            try
            {
                controller_option_atlas_ =
                    app.texture_manager().load_png( "options/controller-option-atlas", atlas_path );
            }
            catch( const std::exception & )
            {
                controller_option_atlas_ = {};
            }
        }

        if( !load_fonts( app ) )
        {
            return false;
        }

        return create_static_textures( app );
    }

    void OptionsModule::unload_text_pair( App &app, TextTextures &textures )
    {
        app.texture_manager().unload( textures.normal );
        app.texture_manager().unload( textures.selected );
        textures = {};
    }

    void OptionsModule::unload_visual_assets( App &app )
    {
        app.texture_manager().unload( controller_option_atlas_ );
        controller_option_atlas_ = {};
        app.texture_manager().unload( heading_texture_ );
        app.texture_manager().unload( system_header_texture_ );
        app.texture_manager().unload( gameplay_header_texture_ );

        for( managers::TextureHandle &handle : controller_label_textures_ )
        {
            app.texture_manager().unload( handle );
            handle = {};
        }

        for( managers::TextureHandle &handle : submenu_header_textures_ )
        {
            app.texture_manager().unload( handle );
            handle = {};
        }

        heading_texture_ = {};

        system_header_texture_ = {};

        gameplay_header_texture_ = {};

        for( TextTextures &textures : label_textures_ )
        {
            unload_text_pair( app, textures );
        }

        for( auto &entry : value_texture_cache_ )
        {
            unload_text_pair( app, entry.second );
        }

        value_texture_cache_.clear();
        title_font_.clear();
    }

    void OptionsModule::retain_visual_assets()
    {
        const auto retain = [ this ]( managers::TextureHandle &handle )
        {
            presentation_->retain_texture( handle );
            handle = {};
        };

        retain( heading_texture_ );
        retain( controller_option_atlas_ );
        retain( system_header_texture_ );
        retain( gameplay_header_texture_ );

        for( managers::TextureHandle &handle : controller_label_textures_ )
        {
            retain( handle );
        }

        for( managers::TextureHandle &handle : submenu_header_textures_ )
        {
            retain( handle );
        }

        for( TextTextures &pair : label_textures_ )
        {
            retain( pair.normal );
            retain( pair.selected );
        }

        for( auto &entry : value_texture_cache_ )
        {
            retain( entry.second.normal );
            retain( entry.second.selected );
        }

        value_texture_cache_.clear();

        title_font_.clear();
    }

    void OptionsModule::draw_controller_diagram( App &app,
        rendering::Renderer &renderer,
        const rendering::LogicalViewport &layout,
        managers::ControllerOrientation orientation ) const
    {
        // US sub_231690 creates these three OPTION.IMX silhouettes and labels.
        constexpr std::array<rendering::Rect, 3> controllers{
            {
                { 232.0f, 412.0f, 112.0f, 80.0f },
                { 346.0f, 396.0f, 80.0f, 113.0f },
                { 429.0f, 396.0f, 80.0f, 113.0f }
            }
        };

        const rendering::Texture *atlas = app.texture_manager().get( controller_option_atlas_ );

        const auto draw_region =
            [ & ]( const rendering::Rect &source, const rendering::Rect &destination, const rendering::Color &tint )
        {
            const rendering::Rect box = layout.rect( destination );
            renderer.draw_texture_region( *atlas,
                source.x,
                source.y,
                source.w,
                source.h,
                box.x,
                box.y,
                box.w,
                box.h,
                tint );
        };

        const auto draw_text_centered =
            [ & ]( managers::TextureHandle handle, float center_x, float center_y, float maximum_width )
        {
            if( const rendering::Texture *text = app.texture_manager().get( handle ); text != nullptr )
            {
                const float scale = std::min( 1.0f / title_raster_scale,
                    maximum_width / static_cast<float>( std::max( text->width, 1 ) ) );

                const float w = static_cast<float>( text->width ) * scale;
                const float h = static_cast<float>( text->height ) * scale;
                renderer.draw_texture( *text,
                    layout.to_x( center_x - w * 0.5f ),
                    layout.to_y( center_y - h * 0.5f ),
                    w * layout.scale,
                    h * layout.scale );
            }
        };

        for( std::int32_t index = 0; index < 3; ++index )
        {
            const rendering::Rect card = controller_card_rect( index );
            const bool selected = managers::controller_orientation_from_index( index ) == orientation;
            renderer.draw_filled_rect( layout.rect( card ),
                selected ? rendering::Color{ 1.0f, 0.74f, 0.94f, 0.95f }
                         : rendering::Color{ 0.72f, 0.87f, 0.98f, 0.55f } );
            renderer.draw_filled_rect( layout.rect( card.x + 3.0f, card.y + 3.0f, card.w - 6.0f, card.h - 6.0f ),
                selected ? rendering::Color{ 0.06f, 0.14f, 0.25f, 0.95f }
                         : rendering::Color{ 0.025f, 0.045f, 0.095f, 0.88f } );
            const float center_x = card.x + card.w * 0.5f;

            if( atlas != nullptr )
            {
                draw_region( { 399.0f, 3.0f + 23.0f * static_cast<float>( index ), 110.0f, 20.0f },
                    { center_x - 99.0f, card.y + 15.0f, 198.0f, 36.0f },
                    rendering::Color{ 1.0f, 1.0f, 1.0f, 1.0f } );

                const rendering::Rect &source = controllers[ static_cast<std::size_t>( index ) ];
                constexpr float sprite_scale = 1.6f;
                const float w = source.w * sprite_scale;
                const float h = source.h * sprite_scale;
                draw_region( source,
                    { center_x - w * 0.5f, card.y + 146.0f - h * 0.5f, w, h },
                    rendering::Color{ 1.0f, 1.0f, 1.0f, selected ? 1.0f : 0.65f } );
            }
            else
            {
                draw_text_centered( controller_label_textures_[ static_cast<std::size_t>( index ) ],
                    center_x,
                    card.y + 33.0f,
                    card.w - 28.0f );
            }
        }
    }

    void OptionsModule::render( App &app, rendering::Renderer &renderer )
    {
        last_output_width_ = std::max( renderer.output_width(), 1 );

        last_output_height_ = std::max( renderer.output_height(), 1 );
        const rendering::LogicalViewport layout = rendering::make_logical_viewport( last_output_width_,
            last_output_height_,
            reference_width,
            reference_height );

        presentation_->draw( app, renderer );

        if( const rendering::Texture *heading = app.texture_manager().get( heading_texture_ ); heading != nullptr )
        {
            draw_texture_fit( renderer, *heading, layout, heading_rect, title_raster_scale );
        }

        const auto draw_section = [ & ]( managers::TextureHandle texture_handle, float y )
        {
            const rendering::Texture *texture = app.texture_manager().get( texture_handle );
            float line_start = 278.0f;

            if( texture != nullptr )
            {
                draw_texture_left_centered( renderer, *texture, layout, 132.0f, y, title_raster_scale );
                line_start = 132.0f + static_cast<float>( texture->width ) / title_raster_scale + 20.0f;
            }

            renderer.draw_filled_rect(
                layout.rect( rendering::Rect{ line_start, y - 1.0f, 1136.0f - line_start, 2.0f } ),
                rendering::menu_theme::separator );
        };

        if( page_ == Page::Main )
        {
            draw_section( system_header_texture_, 142.0f );
            draw_section( gameplay_header_texture_, 352.0f );
        }
        else
        {
            draw_section( submenu_header_textures_[ utils::enum_index( page_ ) - 1 ], 142.0f );
        }

        const std::vector<Row> rows = active_rows();

        for( std::int32_t index = 0; index < static_cast<std::int32_t>( rows.size() ); ++index )
        {
            const Row row = rows[ static_cast<std::size_t>( index ) ];
            const bool enabled = row_enabled( app, row );
            const bool selected = enabled && index == selected_row_;
            const rendering::Rect reference = reference_row_rect( index );
            const rendering::Rect box = layout.rect( reference );

            renderer.draw_filled_rect( box, rendering::menu_theme::card_fill( selected, enabled ) );

            const TextTextures &label_pair = label_textures_[ utils::enum_index( row ) ];
            const rendering::Texture *label =
                app.texture_manager().get( selected ? label_pair.selected : label_pair.normal );

            if( label != nullptr )
            {
                const float fit = std::min( 1.0f / title_raster_scale, 600.0f / static_cast<float>( label->width ) );
                renderer.draw_texture( *label,
                    layout.x + ( reference.x + 25.0f ) * layout.scale,
                    layout.y + ( reference.y + ( reference.h - label->height * fit ) * 0.5f ) * layout.scale,
                    label->width * fit * layout.scale,
                    label->height * fit * layout.scale );
            }

            const bool adjustable = row_adjustable( row );

            if( adjustable )
            {
                rendering::menu_theme::draw_adjustment_buttons( renderer, box, layout.scale, selected );
            }

            const rendering::Rect value_bounds =
                adjustable ? rendering::menu_theme::adjustment_value_rect( reference, 1.0f )
                           : rendering::Rect{ reference.x + reference.w - 366.0f, reference.y, 340.0f, reference.h };

            const TextTextures *value_pair = find_value_texture( app, row_value( app, row ) );
            const rendering::Texture *value =
                value_pair == nullptr
                    ? nullptr
                    : app.texture_manager().get( selected ? value_pair->selected : value_pair->normal );

            if( value != nullptr )
            {
                draw_texture_fit( renderer, *value, layout, value_bounds, title_raster_scale, adjustable );
            }

            if( !enabled )
            {
                renderer.draw_filled_rect( box, rendering::Color{ 0.02f, 0.04f, 0.08f, 0.60f } );
            }
        }

        if( page_ == Page::Controller )
        {
            draw_controller_diagram( app, renderer, layout, app.settings().controller_orientation );
        }
    }
}
