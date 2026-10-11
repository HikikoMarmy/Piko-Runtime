#include "Localization/Language.hpp"
#include "SaveLoadModule.hpp"

#include "App/piko.hpp"
#include "GameData/SaveData.hpp"
#include "GameData/StageCatalog.hpp"
#include "Managers/InputManager.hpp"
#include "Modules/MainMenu/MainMenuModule.hpp"
#include "Modules/Options/OptionsModule.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/MenuTheme.hpp"
#include "Utils/FileUtil.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

namespace piko::modules
{
    SaveLoadModule::SaveLoadModule( std::shared_ptr<OptionsPresentation> presentation )
        : presentation_( std::move( presentation ) )
    {
    }

    std::int32_t SaveLoadModule::profile_clear_count( const gamedata::SaveProfile &profile,
        gamedata::SaveProgressMode mode,
        gamedata::ChartStyle chart_style )
    {
        const auto &chart = profile.chart( chart_style );

        const auto &progress = mode == gamedata::SaveProgressMode::Master ? chart.master
                               : mode == gamedata::SaveProgressMode::Easy ? chart.easy
                                                                          : chart.normal;

        std::int32_t count = 0;

        for( std::size_t stage = 1; stage < progress.size(); ++stage )
        {
            if( progress[ stage ].cleared )
            {
                ++count;
            }
        }

        return count;
    }

    std::uint64_t SaveLoadModule::profile_total_score( const gamedata::SaveProfile &profile,
        gamedata::SaveProgressMode mode,
        gamedata::ChartStyle chart_style )
    {
        const auto &chart = profile.chart( chart_style );

        const auto &progress = mode == gamedata::SaveProgressMode::Master ? chart.master
                               : mode == gamedata::SaveProgressMode::Easy ? chart.easy
                                                                          : chart.normal;

        std::uint64_t total = 0;

        for( std::size_t stage = 1; stage < progress.size(); ++stage )
        {
            total += progress[ stage ].best_score;
        }

        return total;
    }

    const char *SaveLoadModule::rank_label( std::int32_t rank )
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

    std::uint32_t SaveLoadModule::rank_color( std::int32_t rank )
    {
        switch( rank )
        {
            case 5:
                return 0xFF8B26u;
            case 1:
                return 0xFF79B8u;
            case 2:
                return 0xBE86FFu;
            case 3:
                return 0x73F3BCu;
            case 4:
                return 0x79DEFFu;
            default:
                return 0u;
        }
    }

    void SaveLoadModule::enter( App &app )
    {
        leaving_ = false;

        confirmation_open_ = false;

        confirmation_choice_ = 0;

        stage_page_ = 0;

        const auto position = app.menu_navigation().restore( managers::MenuLocation::Profiles,
            gamedata::SaveDataManager::profile_count,
            app.save_data().active_slot() );

        selected_slot_ = position.selected_row;

        easy_stage_total_ = 0;

        normal_stage_total_ = 0;

        master_stage_total_ = 0;
        stage_rows_.clear();

        try
        {
            const auto stages = gamedata::load_playable_stage_catalog( app.game_data(), app.settings().chart_style );
            std::set<std::int32_t> easy_stages;
            std::set<std::int32_t> normal_stages;
            std::set<std::int32_t> master_stages;
            std::map<std::int32_t, StageRow> rows;

            for( const gamedata::PlayableStageEntry &stage : stages )
            {
                if( stage.stage_number < 1 || stage.stage_number >= 100 ||
                    stage.mode == gamedata::PlayMode::Multiplayer )
                {
                    continue;
                }

                StageRow &row = rows[ stage.stage_number ];
                row.number = stage.stage_number;

                row.name = localization::text( "@ui.stage" ) + std::to_string( stage.stage_number );
                const std::string difficulty = gamedata::difficulty_from_chc_path( stage.chc_path );

                if( difficulty == "Easy" )
                {
                    easy_stages.insert( stage.stage_number );
                    row.easy_available = true;
                }
                else if( difficulty == "Normal" )
                {
                    normal_stages.insert( stage.stage_number );
                    row.normal_available = true;
                }
                else if( difficulty == "Master" )
                {
                    master_stages.insert( stage.stage_number );
                    row.master_available = true;
                }
            }

            easy_stage_total_ = static_cast<std::int32_t>( easy_stages.size() );

            normal_stage_total_ = static_cast<std::int32_t>( normal_stages.size() );

            master_stage_total_ = static_cast<std::int32_t>( master_stages.size() );

            for( const auto &[ number, row ] : rows )
            {
                stage_rows_.push_back( row );
            }
        }
        catch( const std::exception & )
        {
        }

        if( stage_rows_.empty() )
        {
            for( std::int32_t stage = 1; stage <= 10; ++stage )
            {
                stage_rows_.push_back( StageRow{ stage, localization::text( "@ui.stage" ) + std::to_string( stage ) } );
            }
        }

        if( easy_stage_total_ <= 0 )
        {
            easy_stage_total_ = static_cast<std::int32_t>( stage_rows_.size() );
        }

        if( normal_stage_total_ <= 0 )
        {
            normal_stage_total_ = static_cast<std::int32_t>( stage_rows_.size() );
        }

        if( master_stage_total_ <= 0 )
        {
            master_stage_total_ = static_cast<std::int32_t>( stage_rows_.size() );
        }

        const std::filesystem::path font_path = utils::find_resource_file( app.game_data().application_root(),
            std::filesystem::path( "fonts" ) / "Anak Paud.ttf" );

        localization::load_menu_font( font_, font_path );

        if( !presentation_ )
        {
            presentation_ = std::make_shared<OptionsPresentation>();
        }

        presentation_->initialize( app );
        presentation_->set_options_music( app, false );
    }

    void SaveLoadModule::leave( App &app )
    {
        app.menu_navigation().remember( managers::MenuLocation::Profiles, selected_slot_ );

        unload_visuals( app );

        if( presentation_.use_count() == 1 )
        {
            presentation_->shutdown( app );
        }
    }

    void SaveLoadModule::unload_visuals( App &app )
    {
        for( const auto &[ key, handle ] : text_textures_ )
        {
            app.texture_manager().unload( handle );
        }

        text_textures_.clear();
        font_.clear();
    }

    void SaveLoadModule::move_selection( App &app, std::int32_t delta )
    {
        selected_slot_ = ( selected_slot_ + delta + gamedata::SaveDataManager::profile_count ) %
                         gamedata::SaveDataManager::profile_count;

        presentation_->play_sound( app, "SELECT" );
    }

    void SaveLoadModule::change_stage_page( App &app, std::int32_t delta )
    {
        constexpr std::int32_t rows_per_page = 14;
        const std::int32_t page_count =
            std::max( 1, ( static_cast<std::int32_t>( stage_rows_.size() ) + rows_per_page - 1 ) / rows_per_page );

        const std::int32_t next = std::clamp( stage_page_ + delta, 0, page_count - 1 );

        if( next == stage_page_ )
        {
            return;
        }

        stage_page_ = next;
        presentation_->play_sound( app, "SELECT" );
    }

    void SaveLoadModule::confirm_selection( App &app )
    {
        confirmation_open_ = true;

        confirmation_choice_ = 0;
        presentation_->play_sound( app, "SELECT" );
    }

    void SaveLoadModule::complete_selection( App &app )
    {
        const gamedata::SaveProfile &profile = app.save_data().profiles()[ static_cast<std::size_t>( selected_slot_ ) ];
        const bool succeeded = profile.occupied ? app.save_data().select_profile( selected_slot_ )
                                                : app.save_data().create_profile( selected_slot_ );

        if( !succeeded )
        {
            return;
        }

        presentation_->play_sound( app, "OK" );
        leaving_ = true;
        app.change_module( std::make_unique<MainMenuModule>( presentation_ ) );
    }

    void SaveLoadModule::handle_event( App &app, const SDL_Event &event )
    {
        if( event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y != 0.0f )
        {
            change_stage_page( app, event.wheel.y < 0.0f ? 1 : -1 );
        }
    }

    void SaveLoadModule::update( App &app, double delta_seconds )
    {
        if( leaving_ )
        {
            return;
        }

        presentation_->update( app, delta_seconds );
        const managers::InputManager &input = app.input();
        const managers::MouseState &mouse = input.mouse();

        if( confirmation_open_ )
        {
            if( mouse.left_clicked )
            {
                if( rendering::point_inside( load_rect_, mouse.x, mouse.y ) )
                {
                    complete_selection( app );
                }
                else if( rendering::point_inside( cancel_rect_, mouse.x, mouse.y ) )
                {
                    confirmation_open_ = false;
                    presentation_->play_sound( app, "SELECT" );
                }

                return;
            }

            if( input.pressed( managers::InputAction::MenuLeft ) || input.pressed( managers::InputAction::MenuRight ) )
            {
                confirmation_choice_ = 1 - confirmation_choice_;
                presentation_->play_sound( app, "SELECT" );
            }

            if( input.pressed( managers::InputAction::Confirm ) )
            {
                if( confirmation_choice_ == 0 )
                {
                    complete_selection( app );
                }
                else
                {
                    confirmation_open_ = false;
                    presentation_->play_sound( app, "SELECT" );
                }
            }
            else if( input.pressed( managers::InputAction::Back ) )
            {
                confirmation_open_ = false;
                presentation_->play_sound( app, "SELECT" );
            }

            return;
        }

        if( mouse.left_clicked )
        {
            for( std::int32_t slot = 0; slot < gamedata::SaveDataManager::profile_count; ++slot )
            {
                if( !rendering::point_inside( slot_rects_[ static_cast<std::size_t>( slot ) ], mouse.x, mouse.y ) )
                {
                    continue;
                }

                if( selected_slot_ == slot )
                {
                    confirm_selection( app );
                }
                else
                {
                    move_selection( app, slot - selected_slot_ );
                }

                return;
            }

            if( rendering::point_inside( previous_page_rect_, mouse.x, mouse.y ) )
            {
                change_stage_page( app, -1 );
            }
            else if( rendering::point_inside( next_page_rect_, mouse.x, mouse.y ) )
            {
                change_stage_page( app, 1 );
            }

            return;
        }

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
            change_stage_page( app, -1 );
        }

        if( input.pressed( managers::InputAction::MenuRight ) )
        {
            change_stage_page( app, 1 );
        }

        if( input.pressed( managers::InputAction::Confirm ) )
        {
            confirm_selection( app );
        }
        else if( input.pressed( managers::InputAction::Back ) )
        {
            leaving_ = true;
            presentation_->play_sound( app, "SELECT" );
            app.change_module( std::make_unique<MainMenuModule>( presentation_ ) );
        }
    }

    void SaveLoadModule::draw_label( App &app,
        rendering::Renderer &renderer,
        const rendering::LogicalViewport &layout,
        const rendering::Rect &bounds,
        const std::string &label,
        float size,
        bool selected,
        TextAlignment alignment,
        std::uint32_t accent_rgb )
    {
        if( label.empty() )
        {
            return;
        }

        const rendering::Rect destination = layout.rect( bounds );

        if( !font_.loaded() )
        {
            const float scale = std::max( 0.5f, size * layout.scale / 10.0f );
            const float width = static_cast<float>( label.size() ) * 6.0f * scale;
            const float x = alignment == TextAlignment::Center  ? destination.x + ( destination.w - width ) * 0.5f
                            : alignment == TextAlignment::Right ? destination.x + destination.w - width
                                                                : destination.x;

            renderer.draw_text( x,
                destination.y + ( destination.h - 7.0f * scale ) * 0.5f,
                label.c_str(),
                accent_rgb != 0u ? rendering::color_from_rgb( accent_rgb )
                : selected       ? rendering::color_from_rgb( rendering::menu_theme::selected_text_rgb )
                                 : rendering::color_from_rgb( rendering::menu_theme::normal_text_rgb ),
                scale );

            return;
        }

        const std::string key = label + "|" + std::to_string( static_cast<std::int32_t>( size ) ) +
                                ( selected ? "|selected|" : "|normal|" ) + std::to_string( accent_rgb );

        managers::TextureHandle handle{};

        if( const auto found = text_textures_.find( key ); found != text_textures_.end() )
        {
            handle = found->second;
        }
        else
        {
            const auto style = rendering::menu_theme::compact_text_style( selected, accent_rgb );

            const rendering::RasterizedText raster =
                font_.rasterize_stroked( label, size * rendering::menu_theme::compact_raster_scale, style );

            if( !raster.valid() )
            {
                return;
            }

            handle = app.texture_manager().create_rgba(
                localization::texture_key( "save-load/text/" + std::to_string( text_textures_.size() ) ),
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
            { layout.scale / rendering::menu_theme::compact_raster_scale,
                destination.w / static_cast<float>( std::max( texture->width, 1 ) ),
                destination.h / static_cast<float>( std::max( texture->height, 1 ) ) } );

        const float width = static_cast<float>( texture->width ) * scale;
        const float height = static_cast<float>( texture->height ) * scale;
        const float x = alignment == TextAlignment::Center  ? destination.x + ( destination.w - width ) * 0.5f
                        : alignment == TextAlignment::Right ? destination.x + destination.w - width
                                                            : destination.x;

        const auto sampling = renderer.texture_sampling();
        renderer.set_texture_sampling( { rendering::TextureFilterMode::LinearMipmap, 1.0f } );

        renderer.draw_texture( *texture, x, destination.y + ( destination.h - height ) * 0.5f, width, height );

        renderer.set_texture_sampling( sampling );
    }

    void SaveLoadModule::render( App &app, rendering::Renderer &renderer )
    {
        const rendering::LogicalViewport layout = rendering::make_logical_viewport( renderer );

        presentation_->draw( app, renderer );

        renderer.draw_filled_rect( layout.rect( 19.0f, 7.0f, 602.0f, 49.0f ), rendering::menu_theme::card );
        renderer.draw_filled_rect( layout.rect( 308.0f, 57.0f, 313.0f, 310.0f ), rendering::menu_theme::card );
        renderer.draw_filled_rect( layout.rect( 19.0f, 368.0f, 288.0f, 66.0f ), rendering::menu_theme::card );

        draw_label( app,
            renderer,
            layout,
            rendering::Rect{ 31.0f, 14.0f, 250.0f, 35.0f },
            localization::text( "@ui.profiles.title" ),
            25.0f );

        draw_label( app,
            renderer,
            layout,
            rendering::Rect{ 329.0f, 14.0f, 280.0f, 35.0f },
            localization::text( "@ui.stage_records_upper" ),
            21.0f,
            false,
            TextAlignment::Right );

        const auto &profiles = app.save_data().profiles();

        for( std::int32_t slot = 0; slot < gamedata::SaveDataManager::profile_count; ++slot )
        {
            const bool selected = slot == selected_slot_;
            const bool active = slot == app.save_data().active_slot();
            const float y = 57.0f + static_cast<float>( slot ) * 31.0f;
            const rendering::Rect row{ 19.0f, y, 288.0f, 30.0f };
            slot_rects_[ static_cast<std::size_t>( slot ) ] = layout.rect( row );
            renderer.draw_filled_rect( slot_rects_[ static_cast<std::size_t>( slot ) ],
                rendering::menu_theme::card_fill( selected ) );

            const gamedata::SaveProfile &profile = profiles[ static_cast<std::size_t>( slot ) ];
            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 31.0f, y + 3.0f, 20.0f, 24.0f },
                std::to_string( slot + 1 ),
                14.0f,
                selected );

            if( profile.occupied )
            {
                draw_label( app,
                    renderer,
                    layout,
                    rendering::Rect{ 61.0f, y + 3.0f, 174.0f, 24.0f },
                    profile.name,
                    15.0f,
                    selected );

                if( active )
                {
                    draw_label( app,
                        renderer,
                        layout,
                        rendering::Rect{ 238.0f, y + 5.0f, 63.0f, 20.0f },
                        localization::text( "@ui.active_upper" ),
                        11.0f,
                        true,
                        TextAlignment::Right );
                }
            }
            else
            {
                draw_label( app,
                    renderer,
                    layout,
                    rendering::Rect{ 61.0f, y + 3.0f, 234.0f, 24.0f },
                    localization::text( "@ui.new_profile_upper" ),
                    15.0f,
                    selected );
            }
        }

        const gamedata::SaveProfile &selected = profiles[ static_cast<std::size_t>( selected_slot_ ) ];

        if( selected.occupied )
        {
            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 31.0f, 373.0f, 264.0f, 14.0f },
                selected.name,
                12.0f,
                true );

            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 31.0f, 388.0f, 264.0f, 13.0f },
                localization::text( "@ui.easy_upper" ) +
                    std::to_string( profile_clear_count( selected,
                        gamedata::SaveProgressMode::Easy,
                        app.settings().chart_style ) ) +
                    "/" + std::to_string( easy_stage_total_ ) + "   " +
                    std::to_string(
                        profile_total_score( selected, gamedata::SaveProgressMode::Easy, app.settings().chart_style ) ),
                10.0f );

            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 31.0f, 402.0f, 264.0f, 13.0f },
                localization::text( "@ui.normal_upper" ) +
                    std::to_string( profile_clear_count( selected,
                        gamedata::SaveProgressMode::Normal,
                        app.settings().chart_style ) ) +
                    "/" + std::to_string( normal_stage_total_ ) + "   " +
                    std::to_string( profile_total_score( selected,
                        gamedata::SaveProgressMode::Normal,
                        app.settings().chart_style ) ),
                10.0f );

            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 31.0f, 416.0f, 264.0f, 13.0f },
                localization::text( "@ui.master_upper" ) +
                    std::to_string( profile_clear_count( selected,
                        gamedata::SaveProgressMode::Master,
                        app.settings().chart_style ) ) +
                    "/" + std::to_string( master_stage_total_ ) + "   " +
                    std::to_string( profile_total_score( selected,
                        gamedata::SaveProgressMode::Master,
                        app.settings().chart_style ) ),
                10.0f );
        }
        else
        {
            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 31.0f, 383.0f, 264.0f, 33.0f },
                localization::text( "@ui.create_a_new_profile" ),
                15.0f,
                true );
        }

        draw_label( app,
            renderer,
            layout,
            rendering::Rect{ 336.0f, 62.0f, 117.0f, 22.0f },
            localization::text( "@ui.stage" ),
            13.0f );

        draw_label( app, renderer, layout, rendering::Rect{ 463.0f, 62.0f, 22.0f, 22.0f }, "E", 13.0f );
        draw_label( app, renderer, layout, rendering::Rect{ 492.0f, 62.0f, 22.0f, 22.0f }, "N", 13.0f );
        draw_label( app, renderer, layout, rendering::Rect{ 521.0f, 62.0f, 22.0f, 22.0f }, "M", 13.0f );
        draw_label( app,
            renderer,
            layout,
            rendering::Rect{ 550.0f, 62.0f, 53.0f, 22.0f },
            localization::text( "@ui.best_upper" ),
            12.0f,
            false,
            TextAlignment::Right );

        renderer.draw_filled_rect( layout.rect( 320.0f, 86.0f, 289.0f, 1.0f ), rendering::menu_theme::separator );

        constexpr std::int32_t rows_per_page = 14;
        const std::int32_t first = stage_page_ * rows_per_page;
        const std::int32_t last = std::min( static_cast<std::int32_t>( stage_rows_.size() ), first + rows_per_page );

        for( std::int32_t index = first; index < last; ++index )
        {
            const StageRow &row = stage_rows_[ static_cast<std::size_t>( index ) ];
            const float y = 90.0f + static_cast<float>( index - first ) * 19.5f;

            if( ( index - first ) % 2 == 0 )
            {
                renderer.draw_filled_rect( layout.rect( 320.0f, y, 289.0f, 18.5f ), rendering::menu_theme::card );
            }

            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 336.0f, y, 117.0f, 17.5f },
                row.name,
                12.0f,
                false,
                TextAlignment::Left,
                row.number == 11 || row.number == 12 ? 0xF5E79Eu : 0u );

            if( selected.occupied )
            {
                const auto &easy =
                    selected.chart( app.settings().chart_style ).easy[ static_cast<std::size_t>( row.number ) ];

                const auto &normal =
                    selected.chart( app.settings().chart_style ).normal[ static_cast<std::size_t>( row.number ) ];

                const auto &master =
                    selected.chart( app.settings().chart_style ).master[ static_cast<std::size_t>( row.number ) ];

                draw_label( app,
                    renderer,
                    layout,
                    rendering::Rect{ 463.0f, y, 22.0f, 17.5f },
                    row.easy_available && easy.cleared ? rank_label( easy.best_rank ) : "--",
                    13.0f,
                    false,
                    TextAlignment::Left,
                    row.easy_available && easy.cleared ? rank_color( easy.best_rank ) : 0u );

                draw_label( app,
                    renderer,
                    layout,
                    rendering::Rect{ 492.0f, y, 22.0f, 17.5f },
                    row.normal_available && normal.cleared ? rank_label( normal.best_rank ) : "--",
                    13.0f,
                    false,
                    TextAlignment::Left,
                    row.normal_available && normal.cleared ? rank_color( normal.best_rank ) : 0u );

                draw_label( app,
                    renderer,
                    layout,
                    rendering::Rect{ 521.0f, y, 22.0f, 17.5f },
                    row.master_available && master.cleared ? rank_label( master.best_rank ) : "--",
                    13.0f,
                    false,
                    TextAlignment::Left,
                    row.master_available && master.cleared ? rank_color( master.best_rank ) : 0u );

                const std::uint32_t best_score = std::max( { easy.best_score, normal.best_score, master.best_score } );
                draw_label( app,
                    renderer,
                    layout,
                    rendering::Rect{ 550.0f, y, 53.0f, 17.5f },
                    best_score == 0u ? "--" : std::to_string( best_score ),
                    11.0f,
                    false,
                    TextAlignment::Right );
            }
            else
            {
                for( const float x : { 463.0f, 492.0f, 521.0f } )
                {
                    draw_label( app, renderer, layout, rendering::Rect{ x, y, 22.0f, 17.5f }, "--", 13.0f );
                }

                draw_label( app,
                    renderer,
                    layout,
                    rendering::Rect{ 550.0f, y, 53.0f, 17.5f },
                    "--",
                    11.0f,
                    false,
                    TextAlignment::Right );
            }
        }

        const std::int32_t page_count =
            std::max( 1, ( static_cast<std::int32_t>( stage_rows_.size() ) + rows_per_page - 1 ) / rows_per_page );

        previous_page_rect_ = {};

        next_page_rect_ = {};

        if( page_count > 1 )
        {
            renderer.draw_filled_rect( layout.rect( 308.0f, 368.0f, 313.0f, 31.0f ), rendering::menu_theme::card );
            previous_page_rect_ = layout.rect( 309.0f, 369.0f, 30.0f, 29.0f );

            next_page_rect_ = layout.rect( 590.0f, 369.0f, 30.0f, 29.0f );
            renderer.draw_filled_rect( previous_page_rect_, rendering::menu_theme::card );
            renderer.draw_filled_rect( next_page_rect_, rendering::menu_theme::card );
            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 309.0f, 369.0f, 30.0f, 29.0f },
                "<",
                13.0f,
                false,
                TextAlignment::Center );

            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 590.0f, 369.0f, 30.0f, 29.0f },
                ">",
                13.0f,
                false,
                TextAlignment::Center );

            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 344.0f, 372.0f, 240.0f, 23.0f },
                localization::text( "@ui.stages_upper" ) + std::to_string( first + 1 ) + "-" + std::to_string( last ) +
                    " / " + std::to_string( stage_rows_.size() ),
                11.0f );
        }

        if( confirmation_open_ )
        {
            renderer.draw_filled_rect( layout.rect( 176.0f, 159.0f, 288.0f, 136.0f ), rendering::menu_theme::modal );

            renderer.draw_filled_rect( layout.rect( 188.0f, 163.0f, 264.0f, 1.0f ), rendering::menu_theme::separator );

            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 188.0f, 181.0f, 264.0f, 32.0f },
                selected.occupied ? localization::text( "@ui.load_profile_upper" )
                                  : localization::text( "@ui.create_profile_upper" ),
                22.0f,
                false,
                TextAlignment::Center );

            load_rect_ = layout.rect( 188.0f, 230.0f, 131.0f, 42.0f );

            cancel_rect_ = layout.rect( 320.0f, 230.0f, 132.0f, 42.0f );
            renderer.draw_filled_rect( load_rect_, rendering::menu_theme::card_fill( confirmation_choice_ == 0 ) );
            renderer.draw_filled_rect( cancel_rect_, rendering::menu_theme::card_fill( confirmation_choice_ == 1 ) );
            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 188.0f, 230.0f, 131.0f, 42.0f },
                selected.occupied ? localization::text( "@ui.load_upper" ) : localization::text( "@ui.create_upper" ),
                16.0f,
                confirmation_choice_ == 0,
                TextAlignment::Center );

            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 320.0f, 230.0f, 132.0f, 42.0f },
                localization::text( "@ui.cancel_upper" ),
                16.0f,
                confirmation_choice_ == 1,
                TextAlignment::Center );
        }

        if( !app.save_data().last_error().empty() )
        {
            draw_label( app,
                renderer,
                layout,
                rendering::Rect{ 39.0f, 436.0f, 555.0f, 10.0f },
                app.save_data().last_error(),
                9.0f,
                true );
        }
    }
}
