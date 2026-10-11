#include "TitleScreenBackdrop.hpp"

#include "App/piko.hpp"

#include "Modules/MainMenu/TitleBackgroundRenderer.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/RenderUtil.hpp"

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <exception>
#include <vector>

namespace piko::modules
{
    managers::TextureHandle TitleScreenBackdrop::create_circle_texture( App &app, const char *name )
    {
        constexpr std::int32_t size = 512;
        std::vector<std::uint8_t> pixels( size * size * 4, 255 );

        for( std::int32_t y = 0; y < size; ++y )
        {
            for( std::int32_t x = 0; x < size; ++x )
            {
                const float dx = static_cast<float>( x ) + 0.5f - size * 0.5f;
                const float dy = static_cast<float>( y ) + 0.5f - size * 0.5f;
                const float edge = size * 0.5f - std::sqrt( dx * dx + dy * dy );
                const float coverage = std::clamp( edge / 3.0f + 0.5f, 0.0f, 1.0f );
                pixels[ ( y * size + x ) * 4 + 3 ] = static_cast<std::uint8_t>( coverage * 255.0f + 0.5f );
            }
        }

        try
        {
            return app.texture_manager().create_rgba( name, pixels.data(), size, size );
        }
        catch( const std::exception & )
        {
            return {};
        }
    }

    void TitleScreenBackdrop::draw_fallback( rendering::Renderer &renderer,
        std::int32_t output_width,
        std::int32_t output_height,
        float time_seconds )
    {
        renderer.draw_filled_rect(
            rendering::Rect{ 0.0f, 0.0f, static_cast<float>( output_width ), static_cast<float>( output_height ) },
            rendering::Color{ 0.03f, 0.035f, 0.10f, 1.0f } );

        for( std::int32_t index = 0; index < 12; ++index )
        {
            const float t = static_cast<float>( index ) / 11.0f;
            renderer.draw_filled_rect(
                rendering::Rect{
                    0.0f,
                    t * static_cast<float>( output_height ),
                    static_cast<float>( output_width ),
                    static_cast<float>( output_height ) / 11.0f + 2.0f
                },
                rendering::Color{ 0.02f, 0.035f, 0.12f + 0.16f * t, 1.0f } );
        }

        for( std::int32_t index = 0; index < 36; ++index )
        {
            const float base_x = static_cast<float>( ( index * 173 ) % std::max( output_width, 1 ) );
            const float base_y = static_cast<float>( ( index * 97 ) % std::max( output_height, 1 ) );
            const float drift = std::sin( time_seconds * 0.8f + static_cast<float>( index ) * 0.53f ) * 10.0f;
            const float size = 3.0f + static_cast<float>( index % 3 );

            renderer.draw_filled_rect( rendering::Rect{ base_x + drift, base_y, size, size },
                rendering::Color{ 1.0f, 1.0f, 1.0f, 0.10f } );
        }
    }

    void TitleScreenBackdrop::draw_circle_field( rendering::Renderer &renderer,
        const rendering::Texture &circle_texture,
        std::int32_t output_width,
        std::int32_t output_height,
        float time_seconds )
    {
        constexpr float cell_size = 140.0f;
        constexpr float horizontal_speed = 22.0f;
        constexpr float vertical_speed = horizontal_speed * 0.72f;

        const rendering::LogicalViewport layout = rendering::make_logical_viewport( output_width, output_height );
        const float horizontal_distance = std::max( time_seconds, 0.0f ) * horizontal_speed;
        const float vertical_distance = std::max( time_seconds, 0.0f ) * vertical_speed;
        const std::int32_t horizontal_cell = static_cast<std::int32_t>( std::floor( horizontal_distance / cell_size ) );
        const std::int32_t vertical_cell = static_cast<std::int32_t>( std::floor( vertical_distance / cell_size ) );
        const float horizontal_phase = horizontal_distance - static_cast<float>( horizontal_cell ) * cell_size;
        const float vertical_phase = vertical_distance - static_cast<float>( vertical_cell ) * cell_size;

        for( std::int32_t row = -2; row <= 7; ++row )
        {
            for( std::int32_t column = -2; column <= 11; ++column )
            {
                const std::int32_t field_column = column + horizontal_cell;
                const std::int32_t field_row = row + vertical_cell;
                const float x = static_cast<float>( column ) * cell_size - horizontal_phase;
                const float y = static_cast<float>( row ) * cell_size - vertical_phase;
                const float wave = 0.5f + 0.5f * std::sin( time_seconds * 0.55f +
                                                           static_cast<float>( field_column + field_row ) * 0.43f );

                const float size = 60.0f + wave * 20.0f;
                const float draw_width = size * layout.scale;

                renderer.draw_texture( circle_texture,
                    layout.to_x( x ) - draw_width * 0.5f,
                    layout.to_y( y ) - draw_width * 0.5f,
                    draw_width,
                    draw_width,
                    rendering::Color{ 1.0f, 1.0f, 1.0f, 0.22f + wave * 0.08f } );
            }
        }
    }

    void TitleScreenBackdrop::draw( rendering::Renderer &renderer,
        TitleBackgroundRenderer &background,
        std::int32_t output_width,
        std::int32_t output_height,
        float time_seconds,
        const rendering::Texture *circle_texture )
    {
        if( !background.render( renderer, time_seconds ) )
        {
            draw_fallback( renderer, output_width, output_height, time_seconds );
        }

        if( circle_texture != nullptr )
        {
            draw_circle_field( renderer, *circle_texture, output_width, output_height, time_seconds );
        }
    }
}
