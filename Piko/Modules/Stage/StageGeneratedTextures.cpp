#include "Modules/Stage/StageGeneratedTextures.hpp"

#include "Utils/MathUtil.hpp"

#include <cstdint>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace piko::modules::stage
{
    std::vector<std::uint8_t> make_generated_score_digit_texture_pixels()
    {
        struct ScoreDigitRect
        {
            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
            float radius = 0.0f;
        };

        const auto signed_distance = []( float x, float y, const ScoreDigitRect &rect )
        {
            const float half_w = rect.w * 0.5f;
            const float half_h = rect.h * 0.5f;
            const float center_x = rect.x + half_w;
            const float center_y = rect.y + half_h;
            const float qx = std::fabs( x - center_x ) - ( half_w - rect.radius );
            const float qy = std::fabs( y - center_y ) - ( half_h - rect.radius );
            const float ox = std::max( qx, 0.0f );
            const float oy = std::max( qy, 0.0f );
            const float outside = std::sqrt( ox * ox + oy * oy );
            const float inside = std::min( std::max( qx, qy ), 0.0f );
            return outside + inside - rect.radius;
        };

        const auto coverage_for_rect = [ & ]( float x, float y, const ScoreDigitRect &rect )
        {
            return std::clamp( 0.5f - signed_distance( x, y, rect ), 0.0f, 1.0f );
        };

        static constexpr bool digit_segments[ 10 ][ 7 ] = {
            { true, true, true, false, true, true, true },
            { false, false, true, false, false, true, false },
            { true, false, true, true, true, false, true },
            { true, false, true, true, false, true, true },
            { false, true, true, true, false, true, false },
            { true, true, false, true, false, true, true },
            { true, true, false, true, true, true, true },
            { true, false, true, false, false, true, false },
            { true, true, true, true, true, true, true },
            { true, true, true, true, false, true, true }
        };

        static constexpr std::array<ScoreDigitRect, 7> segments = {
            ScoreDigitRect{ 17.0f, 6.0f, 30.0f, 10.0f, 5.0f },
            ScoreDigitRect{ 8.0f, 14.0f, 10.0f, 24.0f, 5.0f },
            ScoreDigitRect{ 46.0f, 14.0f, 10.0f, 24.0f, 5.0f },
            ScoreDigitRect{ 17.0f, 35.0f, 30.0f, 10.0f, 5.0f },
            ScoreDigitRect{ 8.0f, 42.0f, 10.0f, 24.0f, 5.0f },
            ScoreDigitRect{ 46.0f, 42.0f, 10.0f, 24.0f, 5.0f },
            ScoreDigitRect{ 17.0f, 64.0f, 30.0f, 10.0f, 5.0f }
        };

        std::vector<std::uint8_t> pixels( static_cast<std::size_t>( score_digit_texture_width ) *
                                              static_cast<std::size_t>( score_digit_texture_height ) * 4u,
            0u );

        for( std::int32_t digit = 0; digit < score_digit_texture_digit_count; ++digit )
        {
            const std::int32_t atlas_x = digit * score_digit_texture_digit_width;

            for( std::int32_t y = 0; y < score_digit_texture_digit_height; ++y )
            {
                for( std::int32_t x = 0; x < score_digit_texture_digit_width; ++x )
                {
                    float coverage = 0.0f;
                    const float sample_x = static_cast<float>( x ) + 0.5f;
                    const float sample_y = static_cast<float>( y ) + 0.5f;

                    for( std::int32_t segment = 0; segment < 7; ++segment )
                    {
                        if( !digit_segments[ digit ][ segment ] )
                        {
                            continue;
                        }

                        coverage = std::max( coverage, coverage_for_rect( sample_x, sample_y, segments[ segment ] ) );
                    }

                    const std::size_t pixel_index =
                        ( static_cast<std::size_t>( y ) * static_cast<std::size_t>( score_digit_texture_width ) +
                            static_cast<std::size_t>( atlas_x + x ) ) *
                        4u;

                    const std::uint8_t alpha =
                        static_cast<std::uint8_t>( std::clamp( coverage, 0.0f, 1.0f ) * 255.0f + 0.5f );

                    pixels[ pixel_index + 0u ] = 255u;

                    pixels[ pixel_index + 1u ] = 255u;

                    pixels[ pixel_index + 2u ] = 255u;

                    pixels[ pixel_index + 3u ] = alpha;
                }
            }
        }

        return pixels;
    }

}
