#include "Modules/Stage/StageTutorialRenderer.hpp"
#include "App/piko.hpp"
#include "Localization/LabelTexture.hpp"
#include "Modules/MainMenu/TitleScreenBackdrop.hpp"
#include "Rendering/MenuTheme.hpp"
#include "Rendering/RenderUtil.hpp"
#include "Runtime/Stage/StageGameplayInterfaceAtlas.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace piko::modules::stage
{
    void StageTutorialRenderer::load( App &app, bool teaching )
    {
        unload( app );
        const bool japanese = app.game_data().runtime_root().filename() == "JP";

        if( teaching )
        {
            timeline_.initialize(
                japanese ? runtime::stage::TutorialRegion::Japanese : runtime::stage::TutorialRegion::Us );

            for( const auto &sprite : timeline_.sprites() )
            {
                if( textures_.contains( sprite.texture ) )
                {
                    continue;
                }

                const auto path = app.game_data().resolve_runtime_asset( "STAGE00/TEXTURES/STAGE00/" + sprite.texture );

                if( path.empty() )
                {
                    continue;
                }

                try
                {
                    textures_[ sprite.texture ] =
                        app.texture_manager().load_png( "stage/tutorial/" + sprite.texture, path );
                }
                catch( const std::exception &exception )
                {
                    std::cerr << "[Piko Tutorial] " << sprite.texture << ": " << exception.what() << '\n';
                }
            }
        }

        if( teaching )
        {
            const auto meter_path = app.game_data().resolve_runtime_asset( "STAGE00/TEXTURES/STAGE00/ITFC.png" );

            if( !meter_path.empty() )
            {
                try
                {
                    textures_[ "ITFC.png" ] = app.texture_manager().load_png( "stage/tutorial/ITFC.png", meter_path );
                }
                catch( const std::exception &exception )
                {
                    std::cerr << "[Piko Tutorial] Meter image: " << exception.what() << '\n';
                }
            }
        }

        const auto again_path = app.game_data().resolve_runtime_asset( "LOADING/TEXTURES/STLOAD00/SAIGO.png" );

        if( !again_path.empty() )
        {
            try
            {
                again_ = app.texture_manager().load_png( "stage/tutorial/practice-again", again_path );
            }
            catch( const std::exception &exception )
            {
                std::cerr << "[Piko Tutorial] Retry image: " << exception.what() << '\n';
            }
        }

        title_ = localization::make_label( app.texture_manager(), "@ui.tutorial_upper", "menu" );

        buttons_[ 0 ] = localization::make_label( app.texture_manager(), "@ui.result.retry", "menu" );

        buttons_[ 1 ] = localization::make_label( app.texture_manager(), "@ui.back_upper", "menu" );
    }

    void StageTutorialRenderer::unload( App &app )
    {
        completion_background_.shutdown();

        for( const auto &[ name, handle ] : textures_ )
        {
            (void)name;
            app.texture_manager().unload( handle );
        }

        textures_.clear();
        app.texture_manager().unload( again_ );
        app.texture_manager().unload( title_ );

        for( const auto handle : buttons_ )
        {
            app.texture_manager().unload( handle );
        }

        again_ = {};

        title_ = {};

        buttons_ = {};

        timeline_ = {};
    }

    void StageTutorialRenderer::update( std::int64_t sample )
    {
        timeline_.update( sample );
    }

    void StageTutorialRenderer::begin_completion( App &app )
    {
        completion_background_.initialize( app, "ui/stage_select/wave_background.json" );
    }

    const runtime::stage::StageTutorialTimeline &StageTutorialRenderer::timeline() const
    {
        return timeline_;
    }

    void StageTutorialRenderer::render( App &app, rendering::Renderer &renderer ) const
    {
        if( !timeline_.valid() )
        {
            return;
        }

        const auto layout = rendering::make_logical_viewport( renderer, reference_width_, reference_height_ );

        const auto draw = [ & ]( const rendering::Texture &texture,
                              const rendering::Rect &source,
                              const rendering::Rect &destination,
                              const rendering::Color &colour )
        {
            const auto target = layout.rect( destination );
            const float direction_x = std::copysign( 1.0f, source.w );
            const float direction_y = std::copysign( 1.0f, source.h );
            renderer.draw_texture_region( texture,
                source.x + direction_x * 0.5f,
                source.y + direction_y * 0.5f,
                source.w - direction_x,
                source.h - direction_y,
                target.x,
                target.y,
                target.w,
                target.h,
                colour );
        };

        if( timeline_.example_meter_visible() )
        {
            const auto entry = textures_.find( "ITFC.png" );

            const auto *texture = entry != textures_.end() ? app.texture_manager().get( entry->second ) : nullptr;

            if( texture != nullptr )
            {
                namespace atlas = runtime::stage::gameplay_interface_atlas;
                draw( *texture, atlas::energy_housing_left_cap, { 200.0f, 130.0f, 33.0f, 40.0f }, {} );
                draw( *texture, atlas::energy_housing_middle, { 233.0f, 130.0f, 200.0f, 40.0f }, {} );
                draw( *texture, atlas::energy_housing_right_cap, { 433.0f, 130.0f, 33.0f, 40.0f }, {} );
                const float width = meter_width_ * timeline_.example_meter_value();

                if( width > 0.0f )
                {
                    const rendering::Rect flipped_source{
                        meter_fill_source_.x + meter_fill_source_.w,
                        meter_fill_source_.y,
                        -meter_fill_source_.w,
                        meter_fill_source_.h
                    };

                    draw( *texture, flipped_source, { meter_fill_right_ - width, meter_fill_y_, width, 17.0f }, {} );
                }

                draw( *texture, meter_marker_source_, { meter_marker_x_ - width, meter_marker_y_, 32.0f, 56.0f }, {} );
            }
        }

        const auto &sprites = timeline_.sprites();

        const auto &states = timeline_.states();

        for( std::size_t index = 0; index < sprites.size(); ++index )
        {
            const auto &sprite = sprites[ index ];

            const auto &state = states[ index ];

            if( !state.visible || state.colour.a <= 0.0f )
            {
                continue;
            }

            const auto entry = textures_.find( sprite.texture );

            const auto *texture = entry != textures_.end() ? app.texture_manager().get( entry->second ) : nullptr;

            if( texture == nullptr )
            {
                continue;
            }

            draw( *texture,
                state.source,
                { state.position.x + sprite.origin.x,
                    state.position.y + sprite.origin.y,
                    sprite.size.x * sprite.scale,
                    sprite.size.y * sprite.scale },
                state.colour );
        }
    }

    rendering::Rect StageTutorialRenderer::completion_button( const rendering::Renderer &renderer, bool retry )
    {
        const auto layout = rendering::make_logical_viewport( renderer, reference_width_, reference_height_ );
        return layout.rect( completion_buttons_[ retry ? 0u : 1u ] );
    }

    bool StageTutorialRenderer::completion_button_contains( const rendering::Renderer &renderer,
        bool retry,
        float x,
        float y )
    {
        const auto bounds = completion_button( renderer, retry );

        if( bounds.w <= 0.0f || bounds.h <= 0.0f )
        {
            return false;
        }

        const float dx = ( x - bounds.x - bounds.w * 0.5f ) / ( bounds.w * 0.5f );
        const float dy = ( y - bounds.y - bounds.h * 0.5f ) / ( bounds.h * 0.5f );
        return dx * dx + dy * dy <= 1.0f;
    }

    void StageTutorialRenderer::render_completion( App &app,
        rendering::Renderer &renderer,
        bool retry_selected,
        float time_seconds )
    {
        TitleScreenBackdrop::draw( renderer,
            completion_background_,
            renderer.output_width(),
            renderer.output_height(),
            time_seconds );

        const auto layout = rendering::make_logical_viewport( renderer, reference_width_, reference_height_ );

        const auto *again = app.texture_manager().get( again_ );

        const auto draw = [ & ]( const rendering::Rect &source,
                              const rendering::Rect &destination,
                              const rendering::Color &tint = rendering::Color{} )
        {
            const auto bounds = layout.rect( destination );
            renderer.draw_texture_region( *again,
                source.x + 0.5f,
                source.y + 0.5f,
                source.w - 1.0f,
                source.h - 1.0f,
                bounds.x,
                bounds.y,
                bounds.w,
                bounds.h,
                tint );
        };

        if( again != nullptr && again->valid() )
        {
            draw( again_source_, again_destination_ );
            draw( puma_source_, puma_destination_ );
        }
        else
        {
            localization::draw_label( renderer,
                app.texture_manager().get( title_ ),
                layout.rect( { 135.0f, 180.0f, 382.0f, 32.0f } ) );
        }

        for( std::size_t index = 0; index < completion_buttons_.size(); ++index )
        {
            const bool selected = ( index == 0 ) == retry_selected;

            const auto &button = completion_buttons_[ index ];

            if( again != nullptr && again->valid() )
            {
                draw( button_source_, button, selected ? rendering::Color{} : unselected_button_tint_ );
                auto label = completion_label_sources_[ index ];

                if( selected )
                {
                    label.y += selected_label_offset_;
                }

                draw( label, completion_label_destinations_[ index ] );
            }
            else
            {
                renderer.draw_filled_ellipse( layout.rect( button ),
                    selected ? rendering::menu_theme::focus_accent : rendering::menu_theme::card );

                localization::draw_label( renderer,
                    app.texture_manager().get( buttons_[ index ] ),
                    layout.rect( { button.x + 8.0f, button.y + 38.0f, button.w - 16.0f, 28.0f } ) );
            }
        }
    }
}
