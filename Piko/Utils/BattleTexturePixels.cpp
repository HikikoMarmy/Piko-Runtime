#include "Utils/BattleTexturePixels.hpp"
#include "Utils/MathUtil.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace piko::ui
{
    std::uint8_t byte_from_float( float value )
    {
        value = std::clamp( value, 0.0f, 1.0f );
        return static_cast<std::uint8_t>( std::lround( value * 255.0f ) );
    }

    void write_rgba( std::vector<std::uint8_t> &pixels,
        std::int32_t width,
        std::int32_t x,
        std::int32_t y,
        float r,
        float g,
        float b,
        float a )
    {
        const std::size_t index =
            ( static_cast<std::size_t>( y ) * static_cast<std::size_t>( width ) + static_cast<std::size_t>( x ) ) * 4u;

        pixels[ index + 0u ] = byte_from_float( r );

        pixels[ index + 1u ] = byte_from_float( g );

        pixels[ index + 2u ] = byte_from_float( b );

        pixels[ index + 3u ] = byte_from_float( a );
    }

    void sample_generated_note_tube_cross_section( float v, float &out_r, float &out_g, float &out_b, float &out_a )
    {
        const float y = std::abs( v * 2.0f - 1.0f );
        const float outer_coverage = 1.0f - piko::utils::smoothstep( 0.94f, 1.00f, y );
        const float inner_coverage = 1.0f - piko::utils::smoothstep( 0.72f, 0.80f, y );
        const float stroke_coverage = std::max( 0.0f, outer_coverage - inner_coverage );

        out_r = 1.0f;

        out_g = 1.0f;

        out_b = 1.0f;

        out_a = std::clamp( stroke_coverage + inner_coverage * 0.50f, 0.0f, 1.0f );
    }

    void sample_generated_note_head( float u, float v, float &out_r, float &out_g, float &out_b, float &out_a )
    {
        const float x = u * 2.0f - 1.0f;
        const float y = v * 2.0f - 1.0f;
        const float radius = std::sqrt( x * x + y * y );

        const float outer_coverage = 1.0f - piko::utils::smoothstep( 0.94f, 1.00f, radius );
        const float inner_coverage = 1.0f - piko::utils::smoothstep( 0.74f, 0.81f, radius );
        const float stroke_coverage = std::max( 0.0f, outer_coverage - inner_coverage );

        out_r = 1.0f;

        out_g = 1.0f;

        out_b = 1.0f;

        out_a = std::clamp( stroke_coverage + inner_coverage * 0.50f, 0.0f, 1.0f );
    }

    std::vector<std::uint8_t> make_generated_note_tube_texture_pixels()
    {
        std::vector<std::uint8_t> pixels( static_cast<std::size_t>( generated_tube_texture_width ) *
                                              static_cast<std::size_t>( generated_tube_texture_height ) * 4u,
            0u );

        const std::int32_t body_x0 = static_cast<std::int32_t>( generated_tube_body_source.x );
        const std::int32_t body_x1 =
            static_cast<std::int32_t>( generated_tube_body_source.x + generated_tube_body_source.w );

        const std::int32_t body_y0 = static_cast<std::int32_t>( generated_tube_body_source.y );
        const std::int32_t body_y1 =
            static_cast<std::int32_t>( generated_tube_body_source.y + generated_tube_body_source.h );

        const std::int32_t cap_x0 = static_cast<std::int32_t>( generated_tube_cap_source.x );
        const std::int32_t cap_x1 =
            static_cast<std::int32_t>( generated_tube_cap_source.x + generated_tube_cap_source.w );

        const std::int32_t cap_y0 = static_cast<std::int32_t>( generated_tube_cap_source.y );
        const std::int32_t cap_y1 =
            static_cast<std::int32_t>( generated_tube_cap_source.y + generated_tube_cap_source.h );

        const std::int32_t note_x0 = static_cast<std::int32_t>( generated_tube_note_source.x );
        const std::int32_t note_x1 =
            static_cast<std::int32_t>( generated_tube_note_source.x + generated_tube_note_source.w );

        const std::int32_t note_y0 = static_cast<std::int32_t>( generated_tube_note_source.y );
        const std::int32_t note_y1 =
            static_cast<std::int32_t>( generated_tube_note_source.y + generated_tube_note_source.h );

        for( std::int32_t y = 0; y < generated_tube_texture_height; ++y )
        {
            for( std::int32_t x = 0; x < generated_tube_texture_width; ++x )
            {
                float r = 0.0f;
                float g = 0.0f;
                float b = 0.0f;
                float a = 0.0f;

                if( x >= note_x0 && x < note_x1 && y >= note_y0 && y < note_y1 )
                {
                    const float note_u = ( static_cast<float>( x ) - generated_tube_note_source.x + 0.5f ) /
                                         std::max( generated_tube_note_source.w, 1.0f );

                    const float note_v = ( static_cast<float>( y ) - generated_tube_note_source.y + 0.5f ) /
                                         std::max( generated_tube_note_source.h, 1.0f );

                    sample_generated_note_head( note_u, note_v, r, g, b, a );
                }
                else if( x >= body_x0 && x < body_x1 && y >= body_y0 && y < body_y1 )
                {
                    const float body_v = ( static_cast<float>( y ) - generated_tube_body_source.y + 0.5f ) /
                                         std::max( generated_tube_body_source.h, 1.0f );

                    sample_generated_note_tube_cross_section( body_v, r, g, b, a );
                }
                else if( x >= cap_x0 && x < cap_x1 && y >= cap_y0 && y < cap_y1 )
                {
                    const float cap_v = ( static_cast<float>( y ) - generated_tube_cap_source.y + 0.5f ) /
                                        std::max( generated_tube_cap_source.h, 1.0f );

                    sample_generated_note_tube_cross_section( cap_v, r, g, b, a );

                    const float local_x = ( static_cast<float>( x ) - generated_tube_cap_source.x + 0.5f ) /
                                          std::max( generated_tube_cap_source.w, 1.0f );

                    const float local_y = cap_v * 2.0f - 1.0f;
                    constexpr float round_start = 0.18f;
                    float cap_mask = 1.0f;

                    if( local_x > round_start )
                    {
                        const float t = ( local_x - round_start ) / std::max( 1.0f - round_start, 0.001f );
                        const float allowed_y = std::sqrt( std::max( 0.0f, 1.0f - t * t ) );
                        cap_mask =
                            1.0f -
                            piko::utils::smoothstep( allowed_y - 0.025f, allowed_y + 0.015f, std::abs( local_y ) );
                    }

                    a *= std::clamp( cap_mask, 0.0f, 1.0f );
                }

                write_rgba( pixels, generated_tube_texture_width, x, y, r, g, b, a );
            }
        }

        return pixels;
    }

    std::vector<std::uint8_t> make_generated_trace_texture_pixels()
    {
        std::vector<std::uint8_t> pixels( static_cast<std::size_t>( generated_trace_texture_width ) *
                                              static_cast<std::size_t>( generated_trace_texture_height ) * 4u,
            0u );

        for( std::int32_t y = 0; y < generated_trace_texture_height; ++y )
        {
            const float v = ( static_cast<float>( y ) + 0.5f ) / static_cast<float>( generated_trace_texture_height );

            const float radius = std::abs( v * 2.0f - 1.0f );
            const float core = 1.0f - piko::utils::smoothstep( 0.24f, 0.44f, radius );
            const float soft = 1.0f - piko::utils::smoothstep( 0.48f, 1.00f, radius );
            const float edge = piko::utils::smoothstep( 0.30f, 0.48f, radius ) *
                               ( 1.0f - piko::utils::smoothstep( 0.58f, 0.86f, radius ) );

            const float a = std::clamp( core * 0.88f + edge * 0.36f + soft * 0.22f, 0.0f, 0.94f );

            for( std::int32_t x = 0; x < generated_trace_texture_width; ++x )
            {
                write_rgba( pixels, generated_trace_texture_width, x, y, 1.0f, 1.0f, 1.0f, a );
            }
        }

        return pixels;
    }

    BattleImage crop_battle_image( const BattleImage &image, const rendering::Rect &source )
    {
        const auto x = static_cast<std::int32_t>( source.x );

        const auto y = static_cast<std::int32_t>( source.y );

        const auto width = static_cast<std::int32_t>( source.w );

        const auto height = static_cast<std::int32_t>( source.h );

        if( x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > image.width || y + height > image.height ||
            image.pixels.size() != static_cast<std::size_t>( image.width ) * image.height * 4u )
        {
            throw std::runtime_error( "Invalid battle image crop" );
        }

        BattleImage result{
            width,
            height,
            std::vector<std::uint8_t>( static_cast<std::size_t>( width ) * height * 4u )
        };

        for( std::int32_t row = 0; row < height; ++row )
        {
            const auto offset = ( static_cast<std::size_t>( y + row ) * image.width + x ) * 4u;

            const auto destination = static_cast<std::size_t>( row ) * width * 4u;
            std::copy_n( image.pixels.data() + offset,
                static_cast<std::size_t>( width ) * 4u,
                result.pixels.data() + destination );
        }

        return result;
    }

    std::array<BattleImage, 3> generated_note_images()
    {
        const BattleImage atlas{
            generated_tube_texture_width,
            generated_tube_texture_height,
            make_generated_note_tube_texture_pixels()
        };

        return {
            crop_battle_image( atlas, generated_tube_body_source ),
            crop_battle_image( atlas, generated_tube_cap_source ),
            crop_battle_image( atlas, generated_tube_note_source )
        };
    }

    float note_solid_height( const BattleImage &image )
    {
        std::uint8_t maximum_alpha = 0;

        for( std::size_t pixel = 3; pixel < image.pixels.size(); pixel += 4u )
        {
            maximum_alpha = std::max( maximum_alpha, image.pixels[ pixel ] );
        }

        if( maximum_alpha == 0 )
        {
            return static_cast<float>( image.height );
        }

        // Measure the solid part; keep all pixels and soft edges.
        const auto solid_alpha = ( static_cast<std::uint32_t>( maximum_alpha ) * 3u + 3u ) / 4u;
        std::int32_t first_row = image.height;
        std::int32_t last_row = -1;

        for( std::int32_t y = 0; y < image.height; ++y )
        {
            for( std::int32_t x = 0; x < image.width; ++x )
            {
                const auto alpha = image.pixels[ ( static_cast<std::size_t>( y ) * image.width + x ) * 4u + 3u ];

                if( alpha >= solid_alpha )
                {
                    first_row = std::min( first_row, y );

                    last_row = std::max( last_row, y );
                    break;
                }
            }
        }

        return static_cast<float>( last_row - first_row + 1 );
    }

    float note_image_height( const BattleImage &image, const BattleImage &reference )
    {
        if( reference.pixels.empty() )
        {
            return note_texture_canvas_height;
        }

        const float height_scale = static_cast<float>( image.height ) / static_cast<float>( reference.height );
        const float solid_scale = note_solid_height( reference ) / note_solid_height( image );
        return note_texture_canvas_height * height_scale * solid_scale;
    }

    NoteTextureAtlas pack_note_images( const std::array<BattleImage, 3> &images,
        const std::array<BattleImage, 3> &size_reference )
    {
        constexpr std::int32_t padding = 2;
        constexpr std::int32_t maximum_dimension = 8192;
        std::int32_t width = padding;
        std::int32_t height = 0;

        for( const auto &image : images )
        {
            if( image.width <= 0 || image.height <= 0 || image.width > maximum_dimension ||
                image.height > maximum_dimension ||
                image.pixels.size() != static_cast<std::size_t>( image.width ) * image.height * 4u )
            {
                throw std::runtime_error( "Invalid note image dimensions" );
            }

            width += image.width + padding * 2;

            height = std::max( height, image.height + padding * 2 );
        }

        if( width > maximum_dimension || height > maximum_dimension )
        {
            throw std::runtime_error( "Note image atlas is too large" );
        }

        NoteTextureAtlas atlas;
        atlas.regions.body_height = note_image_height( images[ 0 ], size_reference[ 0 ] );

        atlas.regions.note_height = note_image_height( images[ 2 ], size_reference[ 2 ] );

        atlas.image = { width, height, std::vector<std::uint8_t>( static_cast<std::size_t>( width ) * height * 4u ) };
        std::array<rendering::Rect *, 3> regions{
            &atlas.regions.body,
            &atlas.regions.cap,
            &atlas.regions.note
        };

        std::int32_t left = padding;

        for( std::size_t index = 0; index < images.size(); ++index )
        {
            const auto &image = images[ index ];
            *regions[ index ] = {
                static_cast<float>( left ),
                static_cast<float>( padding ),
                static_cast<float>( image.width ),
                static_cast<float>( image.height )
            };

            // Keep filtering inside each image.
            for( std::int32_t y = -padding; y < image.height + padding; ++y )
            {
                for( std::int32_t x = -padding; x < image.width + padding; ++x )
                {
                    const auto source =
                        ( static_cast<std::size_t>( std::clamp( y, 0, image.height - 1 ) ) * image.width +
                            std::clamp( x, 0, image.width - 1 ) ) *
                        4u;

                    const auto destination = ( static_cast<std::size_t>( y + padding ) * width + left + x ) * 4u;
                    std::copy_n( image.pixels.data() + source, 4, atlas.image.pixels.data() + destination );
                }
            }

            left += image.width + padding * 2;
        }

        return atlas;
    }
}
