#include "Localization/Language.hpp"
#include "MainMenuModule.hpp"

#include "App/piko.hpp"
#include "Modules/MainMenu/MainMenuAtlas.hpp"
#include "Modules/Collection/CollectionModule.hpp"
#include "Modules/Options/OptionsModule.hpp"
#include "Modules/SaveLoad/SaveLoadModule.hpp"
#include "Modules/StageSelect/StageSelectModule.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/TrueTypeText.hpp"
#include "Rendering/RenderUtil.hpp"
#include "Utils/FileUtil.hpp"
#include "Utils/MathUtil.hpp"

#include <cstdint>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace piko::modules
{
    MainMenuModule::MainMenuModule( std::shared_ptr<OptionsPresentation> presentation )
        : presentation_( std::move( presentation ) )
    {
    }

    float MainMenuModule::menu_item_center_y( std::int32_t index )
    {
        return menu_item_first_center_y_ + static_cast<float>( index ) * menu_item_gap_;
    }

    rendering::Rect MainMenuModule::menu_item_reference_rect( std::int32_t index )
    {
        if( index < 0 || index >= static_cast<std::int32_t>( menu_labels_.size() ) )
        {
            return {};
        }

        const float center_y = menu_item_center_y( index );
        return rendering::Rect{
            menu_item_center_x_ - menu_item_width_ * 0.5f,
            center_y - menu_item_height_ * 0.5f,
            menu_item_width_,
            menu_item_height_
        };
    }

    rendering::Rect MainMenuModule::menu_item_rect( std::int32_t index,
        std::int32_t output_width,
        std::int32_t output_height )
    {
        return rendering::make_logical_viewport( output_width, output_height, reference_width_, reference_height_ )
            .rect( menu_item_reference_rect( index ) );
    }

    std::filesystem::path MainMenuModule::title_font_path( const App &app )
    {
        return piko::utils::find_resource_file( app.game_data().application_root(),
            std::filesystem::path( "fonts" ) / title_font_filename_ );
    }

    std::filesystem::path MainMenuModule::footer_font_path( const App &app )
    {
        return piko::utils::find_resource_file( app.game_data().application_root(),
            std::filesystem::path( "fonts" ) / footer_font_filename_ );
    }

    std::filesystem::path MainMenuModule::title_logo_path( const App &app )
    {
        return piko::utils::find_resource_file( app.game_data().application_root(),
            std::filesystem::path( "ui/title/PikoLogo.png" ) );
    }

    void MainMenuModule::draw_sprite( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const rendering::Rect &source,
        const rendering::Rect &destination,
        const rendering::Color &tint )
    {
        renderer.draw_texture_region( texture,
            source.x,
            source.y,
            source.w,
            source.h,
            destination.x,
            destination.y,
            destination.w,
            destination.h,
            tint );
    }

    void MainMenuModule::draw_atlas_sprite_centered( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const rendering::Rect &source,
        float center_x,
        float center_y,
        float scale,
        const rendering::Color &tint )
    {
        const float width = source.w * scale;
        const float height = source.h * scale;

        draw_sprite( renderer,
            texture,
            source,
            rendering::Rect{ center_x - width * 0.5f, center_y - height * 0.5f, width, height },
            tint );
    }

    void MainMenuModule::draw_texture_fit_centered( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const rendering::Rect &bounds,
        const rendering::Color &tint )
    {
        if( texture.width <= 0 || texture.height <= 0 || bounds.w <= 0.0f || bounds.h <= 0.0f )
        {
            return;
        }

        const float scale =
            std::min( bounds.w / static_cast<float>( texture.width ), bounds.h / static_cast<float>( texture.height ) );

        const float width = static_cast<float>( texture.width ) * scale;
        const float height = static_cast<float>( texture.height ) * scale;

        renderer.draw_texture( texture,
            bounds.x + ( bounds.w - width ) * 0.5f,
            bounds.y + ( bounds.h - height ) * 0.5f,
            width,
            height,
            tint );
    }

    void MainMenuModule::draw_soft_panel( rendering::Renderer &renderer, const rendering::Rect &rect )
    {
        renderer.draw_filled_rect( rendering::Rect{ rect.x - 2.0f, rect.y - 2.0f, rect.w + 4.0f, rect.h + 4.0f },
            rendering::Color{ 0.34f, 0.46f, 0.72f, 0.22f } );

        renderer.draw_filled_rect( rect, rendering::Color{ 0.0f, 0.0f, 0.0f, 0.34f } );
    }

    void MainMenuModule::enter( App &app )
    {
        const auto position =
            app.menu_navigation().restore( managers::MenuLocation::Main, static_cast<std::int32_t>( menu_item_count ) );

        selected_item_ = position.selected_row;

        previous_portrait_item_ = selected_item_;

        puma_y_ = menu_item_center_y( selected_item_ );

        portrait_blend_ = 1.0;

        pending_action_ = PendingAction::None;

        pending_transition_seconds_ = 0.0;

        if( !presentation_ )
        {
            presentation_ = std::make_shared<OptionsPresentation>();
        }

        presentation_->initialize( app );

        unload_title_art( app );
        unload_menu_textures( app );
        load_title_art( app );
        load_menu_textures( app );
        presentation_->set_options_music( app, false );
    }

    void MainMenuModule::leave( App &app )
    {
        app.menu_navigation().remember( managers::MenuLocation::Main, selected_item_ );

        if( presentation_.use_count() > 1 )
        {
            const auto retain = [ this ]( managers::TextureHandle &handle )
            {
                presentation_->retain_texture( handle );
                handle = {};
            };

            for( MenuTextTextures &pair : menu_textures_ )
            {
                retain( pair.normal );
                retain( pair.selected );
            }

            for( managers::TextureHandle &handle : footer_textures_ )
            {
                retain( handle );
            }

            retain( title_logo_texture_ );

            for( managers::TextureHandle &handle : character_textures_ )
            {
                retain( handle );
            }

            menu_texture_ = {};
        }
        else
        {
            unload_menu_textures( app );
            unload_title_art( app );
            presentation_->shutdown( app );
        }
    }

    void MainMenuModule::handle_event( App &, const SDL_Event & )
    {
    }

    bool MainMenuModule::load_menu_textures( App &app )
    {
        bool all_cached = true;

        for( std::size_t index = 0; index < menu_labels_.size(); ++index )
        {
            menu_textures_[ index ].normal = app.texture_manager().find(
                localization::texture_key( "mainmenu/title_text_normal_" + std::to_string( index ) ) );

            menu_textures_[ index ].selected = app.texture_manager().find(
                localization::texture_key( "mainmenu/title_text_selected_" + std::to_string( index ) ) );

            all_cached = all_cached && menu_textures_[ index ].normal && menu_textures_[ index ].selected;
        }

        for( std::size_t index = 0; index < footer_textures_.size(); ++index )
        {
            footer_textures_[ index ] =
                app.texture_manager().find( localization::texture_key( "mainmenu/footer_" + std::to_string( index ) ) );

            all_cached = all_cached && static_cast<bool>( footer_textures_[ index ] );
        }

        if( all_cached )
        {
            return true;
        }

        const std::filesystem::path font_path = title_font_path( app );

        if( font_path.empty() )
        {
            std::cerr << "[Piko UI] Title font not found: GAME_DATA/PIKO/fonts/" << title_font_filename_ << '\n';
            return false;
        }

        rendering::TrueTypeFont font;
        std::string error;

        if( !localization::load_menu_font( font, font_path, &error ) )
        {
            std::cerr << "[Piko UI] " << error << '\n';
            return false;
        }

        rendering::TrueTypeFont footer_font;

        if( !footer_font.load( footer_font_path( app ), &error ) )
        {
            std::cerr << "[Piko UI] " << error << '\n';
            return false;
        }

        const rendering::StrokedTextStyle normal_style{
            rendering::color_from_rgb( 0xEFBB4Bu ),
            rendering::color_from_rgb( 0x0D005Au ),
            rendering::color_from_rgb( 0x4287DDu ),
            8,
            17,
            1.5f
        };

        const rendering::StrokedTextStyle selected_style{
            rendering::color_from_rgb( 0x19C615u ),
            rendering::color_from_rgb( 0xFEF67Au ),
            rendering::color_from_rgb( 0xFFC2FAu ),
            8,
            17,
            1.5f
        };

        try
        {
            for( std::size_t index = 0; index < menu_labels_.size(); ++index )
            {
                if( menu_textures_[ index ].normal && menu_textures_[ index ].selected )
                {
                    continue;
                }

                const rendering::RasterizedText normal = font.rasterize_stroked( menu_labels_[ index ],
                    title_text_height_ * title_text_raster_scale_,
                    normal_style,
                    &error );

                if( !normal.valid() )
                {
                    throw std::runtime_error( error.empty() ? "Could not rasterize title text" : error );
                }

                const rendering::RasterizedText selected = font.rasterize_stroked( menu_labels_[ index ],
                    title_text_height_ * title_text_raster_scale_,
                    selected_style,
                    &error );

                if( !selected.valid() )
                {
                    throw std::runtime_error( error.empty() ? "Could not rasterize selected title text" : error );
                }

                const std::string normal_name = "mainmenu/title_text_normal_" + std::to_string( index );
                const std::string selected_name = "mainmenu/title_text_selected_" + std::to_string( index );

                menu_textures_[ index ].normal =
                    app.texture_manager().create_rgba( localization::texture_key( normal_name ),
                        normal.rgba.data(),
                        normal.width,
                        normal.height );

                menu_textures_[ index ].selected =
                    app.texture_manager().create_rgba( localization::texture_key( selected_name ),
                        selected.rgba.data(),
                        selected.width,
                        selected.height );
            }

            const std::array<std::string, 3> footer_lines{
                "Build " __DATE__,
                "https://github.com/HikikoMarmy/Piko-Runtime",
                "\xC2\xA9 326 / KOEI Co., Ltd."
            };

            const rendering::StrokedTextStyle footer_style{
                rendering::color_from_rgb( 0xF3FCFFu ),
                rendering::color_from_rgb( 0x0A3860u ),
                rendering::color_from_rgb( 0x071E3Au ),
                2,
                0,
                0.0f
            };

            for( std::size_t index = 0; index < footer_lines.size(); ++index )
            {
                if( footer_textures_[ index ] )
                {
                    continue;
                }

                const rendering::RasterizedText raster = footer_font.rasterize_stroked( footer_lines[ index ],
                    footer_text_height_ * footer_text_raster_scale_,
                    footer_style,
                    &error );

                if( !raster.valid() )
                {
                    throw std::runtime_error( error.empty() ? "Could not rasterize title footer" : error );
                }

                footer_textures_[ index ] = app.texture_manager().create_rgba(
                    localization::texture_key( "mainmenu/footer_" + std::to_string( index ) ),
                    raster.rgba.data(),
                    raster.width,
                    raster.height );
            }
        }
        catch( const std::exception &exception )
        {
            std::cerr << "[Piko UI] Could not create title menu text: " << exception.what() << '\n';
            unload_menu_textures( app );
            return false;
        }

        std::cout << "[Piko UI] Title font: " << font_path.string() << '\n';
        return true;
    }

    void MainMenuModule::unload_menu_textures( App &app )
    {
        for( MenuTextTextures &textures : menu_textures_ )
        {
            app.texture_manager().unload( textures.normal );
            app.texture_manager().unload( textures.selected );
            textures = {};
        }

        for( managers::TextureHandle &handle : footer_textures_ )
        {
            app.texture_manager().unload( handle );
            handle = {};
        }
    }

    void MainMenuModule::draw_footer( App &app,
        rendering::Renderer &renderer,
        const rendering::LogicalViewport &layout )
    {
        const std::array<rendering::Rect, 3> bounds{
            rendering::Rect{ 12.0f, 398.0f, 305.0f, 20.0f },
            rendering::Rect{ 12.0f, 420.0f, 341.0f, 17.0f },
            rendering::Rect{ 365.0f, 420.0f, 263.0f, 17.0f }
        };

        for( std::size_t index = 0; index < footer_textures_.size(); ++index )
        {
            const rendering::Texture *texture = app.texture_manager().get( footer_textures_[ index ] );

            if( texture == nullptr || texture->width <= 0 || texture->height <= 0 )
            {
                continue;
            }

            rendering::Rect box = layout.rect( bounds[ index ] );

            if( index == 2 )
            {
                box.x = static_cast<float>( renderer.output_width() ) - ( bounds[ index ].w + 8.0f ) * layout.scale;
            }
            else
            {
                box.x = 8.0f * layout.scale;
            }

            const float scale = std::min(
                { layout.scale / footer_text_raster_scale_,
                    box.w / static_cast<float>( texture->width ),
                    box.h / static_cast<float>( texture->height ) } );

            const float w = static_cast<float>( texture->width ) * scale;
            const float h = static_cast<float>( texture->height ) * scale;
            renderer.draw_texture( *texture,
                index == 2 ? box.x + box.w - w : box.x,
                box.y + ( box.h - h ) * 0.5f,
                w,
                h );
        }
    }

    void MainMenuModule::load_title_art( App &app )
    {
        menu_texture_ = presentation_->menu_texture();

        for( std::size_t index = 0; index < character_texture_names_.size(); ++index )
        {
            const std::filesystem::path filename = character_texture_names_[ index ];
            const std::filesystem::path path = app.game_data().resolve_runtime_asset(
                ( std::filesystem::path( "MAINMENU" ) / "TEXTURES" / "MAINMENU" / filename ).generic_string() );

            if( path.empty() || !std::filesystem::is_regular_file( path ) )
            {
                continue;
            }

            try
            {
                character_textures_[ index ] = app.texture_manager().load_png( character_texture_keys_[ index ], path );
            }
            catch( const std::exception &exception )
            {
                std::cerr << "[Piko UI] Could not load converted " << filename.string() << ": " << exception.what()
                          << '\n';

                character_textures_[ index ] = {};
            }
        }

        const std::filesystem::path logo_path = title_logo_path( app );

        if( !logo_path.empty() )
        {
            try
            {
                title_logo_texture_ = app.texture_manager().load_png( title_logo_texture_name_, logo_path );
            }
            catch( const std::exception &exception )
            {
                std::cerr << "[Piko UI] Could not load title logo: " << exception.what() << '\n';
                title_logo_texture_ = {};
            }
        }
    }

    void MainMenuModule::unload_title_art( App &app )
    {
        menu_texture_ = {};

        app.texture_manager().unload( title_logo_texture_ );
        title_logo_texture_ = {};

        for( managers::TextureHandle &texture : character_textures_ )
        {
            app.texture_manager().unload( texture );
            texture = {};
        }
    }

    void MainMenuModule::move_selection( App &app, std::int32_t delta )
    {
        const std::int32_t count = static_cast<std::int32_t>( menu_item_count );
        std::int32_t next = ( selected_item_ + delta ) % count;

        if( next < 0 )
        {
            next += count;
        }

        if( next != selected_item_ )
        {
            set_selection( next );
            presentation_->play_sound( app, "SELECT" );
        }
    }

    void MainMenuModule::set_selection( std::int32_t index )
    {
        if( index < 0 || index >= static_cast<std::int32_t>( menu_item_count ) || index == selected_item_ )
        {
            return;
        }

        previous_portrait_item_ = selected_item_;

        selected_item_ = index;

        portrait_blend_ = 0.0;
    }

    void MainMenuModule::confirm_selection( App &app )
    {
        if( selected_item_ < 0 || selected_item_ >= static_cast<std::int32_t>( menu_item_count ) )
        {
            return;
        }

        presentation_->play_sound( app, "OK" );
        pending_transition_seconds_ = transition_duration_;

        switch( selected_item_ )
        {
            case 0:
                pending_action_ = PendingAction::SingleMode;
                break;
            case 1:
                pending_action_ = PendingAction::Collection;
                break;
            case 2:
                pending_action_ = PendingAction::Profiles;
                break;
            case 3:
                pending_action_ = PendingAction::Options;
                break;
            case 4:
                pending_action_ = PendingAction::Exit;
                break;
            default:
                pending_action_ = PendingAction::None;
                break;
        }
    }

    std::int32_t MainMenuModule::hit_test_menu_item( float x, float y ) const
    {
        for( std::int32_t i = 0; i < static_cast<std::int32_t>( menu_item_count ); ++i )
        {
            if( rendering::point_inside( menu_item_rect( i, last_output_width_, last_output_height_ ), x, y ) )
            {
                return i;
            }
        }

        return -1;
    }

    void MainMenuModule::update( App &app, double delta_seconds )
    {
        presentation_->update( app, delta_seconds );
        portrait_blend_ = std::min( portrait_blend_ + delta_seconds * 5.5, 1.0 );

        const float target_puma_y = menu_item_center_y( selected_item_ );
        const float cursor_factor = 1.0f - std::exp( -14.0f * static_cast<float>( delta_seconds ) );
        puma_y_ += ( target_puma_y - puma_y_ ) * cursor_factor;

        if( pending_action_ != PendingAction::None )
        {
            pending_transition_seconds_ -= delta_seconds;

            if( pending_transition_seconds_ <= 0.0 )
            {
                const PendingAction action = pending_action_;
                pending_action_ = PendingAction::None;

                switch( action )
                {
                    case PendingAction::SingleMode:
                        app.change_module(
                            std::make_unique<StageSelectModule>( StageSelectModule::StageCategory::SingleNormal ) );
                        break;
                    case PendingAction::Collection:
                        app.change_module( std::make_unique<CollectionModule>() );
                        break;
                    case PendingAction::Profiles:
                        app.change_module( std::make_unique<SaveLoadModule>( presentation_ ) );
                        break;
                    case PendingAction::Options:
                        app.change_module( std::make_unique<OptionsModule>( presentation_ ) );
                        break;
                    case PendingAction::Exit:
                        app.request_quit();
                        break;
                    case PendingAction::None:
                        break;
                }
            }

            return;
        }

        const managers::InputManager &input = app.input();

        if( input.pressed( managers::InputAction::MenuUp ) )
        {
            move_selection( app, -1 );
        }

        if( input.pressed( managers::InputAction::MenuDown ) )
        {
            move_selection( app, 1 );
        }

        const managers::MouseState &mouse = input.mouse();

        if( mouse.moved )
        {
            if( const std::int32_t hovered = hit_test_menu_item( mouse.x, mouse.y ); hovered >= 0 )
            {
                set_selection( hovered );
            }
        }

        if( mouse.left_clicked )
        {
            if( const std::int32_t clicked = hit_test_menu_item( mouse.x, mouse.y ); clicked >= 0 )
            {
                set_selection( clicked );
                confirm_selection( app );
            }
        }
        else if( input.pressed( managers::InputAction::Confirm ) )
        {
            confirm_selection( app );
        }

        if( input.pressed( managers::InputAction::Back ) && !mouse.right_clicked )
        {
            app.request_quit();
        }
    }

    void MainMenuModule::render( App &app, rendering::Renderer &renderer )
    {
        last_output_width_ = std::max( renderer.output_width(), 1 );

        last_output_height_ = std::max( renderer.output_height(), 1 );

        const float output_w = static_cast<float>( last_output_width_ );
        const float output_h = static_cast<float>( last_output_height_ );
        const float animation_time = static_cast<float>( presentation_->elapsed_seconds() );
        const rendering::LogicalViewport layout = rendering::make_logical_viewport( last_output_width_,
            last_output_height_,
            reference_width_,
            reference_height_ );

        const rendering::Texture *menu_texture = app.texture_manager().get( menu_texture_ );
        presentation_->draw( app, renderer );

        if( menu_texture == nullptr )
        {
            const rendering::Rect panel{ 80.0f, 80.0f, output_w - 160.0f, output_h - 160.0f };

            draw_soft_panel( renderer, panel );

            renderer.draw_text( panel.x + 40.0f,
                panel.y + 42.0f,
                "PIKO",
                rendering::Color{ 0.94f, 0.96f, 1.0f, 1.0f },
                5.0f );

            renderer.draw_text( panel.x + 44.0f,
                panel.y + 118.0f,
                "Converted MAINMENU/MENU.png was not loaded.",
                rendering::Color{ 0.86f, 0.90f, 0.98f, 0.94f },
                2.0f );

            renderer.draw_text( panel.x + 44.0f,
                panel.y + 152.0f,
                "Check GAME_DATA/<REGION>/MAINMENU/TEXTURES/MAINMENU/MENU.png",
                rendering::Color{ 0.70f, 0.76f, 0.88f, 0.88f },
                1.0f );

            draw_footer( app, renderer, layout );
            return;
        }

        const rendering::Texture *title_logo = app.texture_manager().get( title_logo_texture_ );

        if( title_logo != nullptr )
        {
            draw_texture_fit_centered( renderer,
                *title_logo,
                layout.rect( rendering::Rect{ 12.0f, 10.0f, 170.0f, 106.0f } ) );
        }
        else
        {
            const bool japanese = app.game_data().runtime_root().filename() == "JP";

            const auto &logo = japanese ? main_menu_atlas::gitaroo_logo_japanese : main_menu_atlas::gitaroo_logo;
            const float scale = japanese ? std::min( 170.0f / logo.w, 106.0f / logo.h ) : 1.0f;

            draw_atlas_sprite_centered( renderer,
                *menu_texture,
                logo,
                layout.to_x( 84.0f ),
                layout.to_y( japanese ? 63.0f : 59.0f ),
                layout.scale * scale );
        }

        const rendering::Texture *circle_texture = app.texture_manager().get( presentation_->circle_texture() );

        if( circle_texture != nullptr )
        {
            const float diameter = 129.0f * 2.18f * layout.scale;
            renderer.draw_texture( *circle_texture,
                layout.to_x( portrait_center_x_ ) - diameter * 0.5f,
                layout.to_y( portrait_center_y_ ) - diameter * 0.5f,
                diameter,
                diameter,
                rendering::Color{ 0.78f, 0.82f, 0.96f, 0.88f } );
        }

        const auto draw_portrait = [ & ]( std::int32_t item, float alpha )
        {
            if( item < 0 || item >= static_cast<std::int32_t>( portrait_definitions_.size() ) || alpha <= 0.0f )
            {
                return;
            }

            const PortraitDefinition &definition = portrait_definitions_[ static_cast<std::size_t>( item ) ];
            const rendering::Texture *texture =
                app.texture_manager().get( character_textures_[ definition.texture_index ] );

            if( texture == nullptr )
            {
                return;
            }

            const float bob = std::sin( animation_time * 1.35f + static_cast<float>( item ) * 0.4f ) * 2.5f;
            draw_atlas_sprite_centered( renderer,
                *texture,
                definition.source,
                layout.to_x( portrait_center_x_ ),
                layout.to_y( portrait_center_y_ + bob ),
                definition.scale * layout.scale,
                rendering::Color{ 1.0f, 1.0f, 1.0f, alpha } );
        };

        const float portrait_t = piko::utils::smoothstep( static_cast<float>( portrait_blend_ ) );

        if( previous_portrait_item_ != selected_item_ && portrait_t < 1.0f )
        {
            draw_portrait( previous_portrait_item_, 1.0f - portrait_t );
        }

        draw_portrait( selected_item_, portrait_t );

        const float puma_bob = std::sin( animation_time * 3.0f ) * 1.5f;
        const float puma_y = puma_y_ + puma_bob;

        if( circle_texture != nullptr )
        {
            const float diameter = 66.0f * 1.02f * layout.scale;
            renderer.draw_texture( *circle_texture,
                layout.to_x( puma_center_x_ ) - diameter * 0.5f,
                layout.to_y( puma_y ) - diameter * 0.5f,
                diameter,
                diameter,
                rendering::Color{ 1.0f, 1.0f, 1.0f, 0.82f } );
        }

        draw_atlas_sprite_centered( renderer,
            *menu_texture,
            main_menu_atlas::puma_head,
            layout.to_x( puma_center_x_ ),
            layout.to_y( puma_y ),
            0.76f * layout.scale );

        for( std::int32_t i = 0; i < static_cast<std::int32_t>( menu_item_count ); ++i )
        {
            const bool selected = i == selected_item_;
            const rendering::Rect destination = menu_item_rect( i, last_output_width_, last_output_height_ );
            const MenuTextTextures &textures = menu_textures_[ static_cast<std::size_t>( i ) ];
            const rendering::Texture *normal_text_texture = app.texture_manager().get( textures.normal );
            const rendering::Texture *selected_text_texture = app.texture_manager().get( textures.selected );
            const rendering::Texture *text_texture =
                selected && selected_text_texture != nullptr ? selected_text_texture : normal_text_texture;

            const rendering::Texture *text_metrics =
                normal_text_texture != nullptr ? normal_text_texture : text_texture;

            if( text_texture != nullptr && text_metrics != nullptr )
            {
                float text_scale = layout.scale / title_text_raster_scale_;
                const float maximum_width = destination.w * 0.96f;
                const float maximum_height = destination.h * 0.94f;
                text_scale =
                    std::min( text_scale, maximum_width / static_cast<float>( std::max( text_metrics->width, 1 ) ) );

                text_scale =
                    std::min( text_scale, maximum_height / static_cast<float>( std::max( text_metrics->height, 1 ) ) );

                const float text_width = static_cast<float>( text_metrics->width ) * text_scale;
                const float text_height = static_cast<float>( text_metrics->height ) * text_scale;

                renderer.draw_texture( *text_texture,
                    destination.x + ( destination.w - text_width ) * 0.5f,
                    destination.y + ( destination.h - text_height ) * 0.5f,
                    text_width,
                    text_height,
                    rendering::Color{ 1.0f, 1.0f, 1.0f, 1.0f } );
            }
            else
            {
                renderer.draw_text( destination.x + 20.0f * layout.scale,
                    destination.y + 18.0f * layout.scale,
                    menu_labels_[ static_cast<std::size_t>( i ) ],
                    selected ? rendering::color_from_rgb( 0x19C615u ) : rendering::color_from_rgb( 0xEFBB4Bu ),
                    std::max( layout.scale * 2.0f, 1.0f ) );
            }
        }

        draw_footer( app, renderer, layout );

        if( pending_action_ != PendingAction::None )
        {
            const float remaining =
                static_cast<float>( std::clamp( pending_transition_seconds_ / transition_duration_, 0.0, 1.0 ) );

            renderer.draw_filled_rect( rendering::Rect{ 0.0f, 0.0f, output_w, output_h },
                rendering::Color{ 0.0f, 0.0f, 0.0f, 1.0f - remaining } );
        }
    }
}
