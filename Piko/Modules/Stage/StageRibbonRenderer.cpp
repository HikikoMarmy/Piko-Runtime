#include "Modules/Stage/StageRibbonRenderer.hpp"
#include "Utils/MathUtil.hpp"
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstddef>

#include "Rendering/RenderUtil.hpp"

namespace piko::modules::stage
{
    bool point_maybe_visible( const rendering::Vec2 &point, const rendering::Rect &area )
    {
        constexpr float margin = 200.0f;

        return point.x >= area.x - margin && point.y >= area.y - margin && point.x <= area.x + area.w + margin &&
               point.y <= area.y + area.h + margin;
    }

    bool path_maybe_visible( const std::vector<rendering::Vec2> &points, const rendering::Rect &area )
    {
        for( const rendering::Vec2 &point : points )
        {
            if( point_maybe_visible( point, area ) )
            {
                return true;
            }
        }

        return false;
    }

    void draw_filled_disc( rendering::Renderer &renderer,
        const rendering::Vec2 &center,
        float radius,
        const rendering::Color &color )
    {
        if( radius <= 0.0f || color.a <= 0.0f )
        {
            return;
        }

        const std::int32_t r = static_cast<std::int32_t>( std::ceil( radius ) );

        for( std::int32_t y = -r; y <= r; ++y )
        {
            const float fy = static_cast<float>( y );
            const float inside = radius * radius - fy * fy;

            if( inside <= 0.0f )
            {
                continue;
            }

            const float half_width = std::sqrt( inside );
            renderer.draw_filled_rect( rendering::Rect{ center.x - half_width, center.y + fy, half_width * 2.0f, 1.0f },
                color );
        }
    }

    void draw_disc_outline( rendering::Renderer &renderer,
        const rendering::Vec2 &center,
        float radius,
        float thickness,
        const rendering::Color &color )
    {
        if( radius <= 0.0f || thickness <= 0.0f || color.a <= 0.0f )
        {
            return;
        }

        constexpr std::int32_t segments = 48;
        constexpr float tau = 6.28318530717958647692f;

        rendering::Vec2 previous{ center.x + std::cos( 0.0f ) * radius, center.y + std::sin( 0.0f ) * radius };

        for( std::int32_t i = 1; i <= segments; ++i )
        {
            const float angle = ( static_cast<float>( i ) / static_cast<float>( segments ) ) * tau;
            const rendering::Vec2 next{ center.x + std::cos( angle ) * radius, center.y + std::sin( angle ) * radius };

            renderer.draw_line( previous, next, thickness, color );
            previous = next;
        }
    }

    std::vector<rendering::Vec2> densify_path( const std::vector<rendering::Vec2> &points, float max_segment_pixels )
    {
        if( points.size() < 2 || max_segment_pixels <= 0.5f )
        {
            return points;
        }

        std::vector<rendering::Vec2> out;
        out.reserve( points.size() * 2u );
        out.push_back( points.front() );

        for( std::size_t i = 1; i < points.size(); ++i )
        {
            const rendering::Vec2 a = points[ i - 1 ];
            const rendering::Vec2 b = points[ i ];
            const float distance = rendering::distance_between( a, b );
            const std::int32_t steps =
                std::max( 1, static_cast<std::int32_t>( std::ceil( distance / max_segment_pixels ) ) );

            for( std::int32_t step = 1; step <= steps; ++step )
            {
                const float t = static_cast<float>( step ) / static_cast<float>( steps );
                out.push_back( rendering::lerp_point( a, b, t ) );
            }
        }

        return out;
    }

    std::vector<runtime::stage::TraceScreenPoint> sample_trace_screen_path(
        const std::vector<runtime::stage::TraceSample> &samples,
        const runtime::stage::TracePosition &current_position,
        std::int64_t current_chart_sample,
        std::int64_t start_sample,
        std::int64_t end_sample,
        const runtime::stage::StageGameplayView &view )
    {
        std::vector<runtime::stage::TraceScreenPoint> points;

        if( samples.size() < 2 || end_sample <= start_sample )
        {
            return points;
        }

        const auto push_sample = [ & ]( std::int64_t sample )
        {
            const runtime::stage::TracePosition position = runtime::stage::visual_trace_position_at( samples, sample );

            runtime::stage::TraceScreenPoint point{};
            point.point = runtime::stage::gameplay_world_to_screen( position.x, position.y, current_position, view );

            point.chart_sample = sample;

            point.angle = runtime::stage::gameplay_world_angle_to_screen_angle( position.angle, view );

            if( !points.empty() && rendering::distance_between( points.back().point, point.point ) <= 0.25f &&
                points.back().chart_sample == point.chart_sample )
            {
                return;
            }

            points.push_back( point );
        };

        push_sample( start_sample );

        for( const runtime::stage::TraceSample &sample : samples )
        {
            const std::int64_t sample_value = sample.chart_sample;

            if( sample_value <= start_sample )
            {
                continue;
            }

            if( sample_value >= end_sample )
            {
                break;
            }

            push_sample( sample_value );
        }

        push_sample( end_sample );
        return points;
    }

    rendering::Vec2 tube_add( const rendering::Vec2 &a, const rendering::Vec2 &b )
    {
        return rendering::Vec2{ a.x + b.x, a.y + b.y };
    }

    rendering::Vec2 tube_sub( const rendering::Vec2 &a, const rendering::Vec2 &b )
    {
        return rendering::Vec2{ a.x - b.x, a.y - b.y };
    }

    rendering::Vec2 tube_mul( const rendering::Vec2 &point, float scale )
    {
        return rendering::Vec2{ point.x * scale, point.y * scale };
    }

    float tube_length( const rendering::Vec2 &point )
    {
        return std::sqrt( point.x * point.x + point.y * point.y );
    }

    rendering::Vec2 tube_normalize_or( const rendering::Vec2 &point, const rendering::Vec2 &fallback )
    {
        const float length = tube_length( point );

        if( length <= 0.0001f )
        {
            return fallback;
        }

        return rendering::Vec2{ point.x / length, point.y / length };
    }

    rendering::Vec2 tube_left_normal( const rendering::Vec2 &tangent )
    {
        return rendering::Vec2{ -tangent.y, tangent.x };
    }

    float tube_dot( const rendering::Vec2 &a, const rendering::Vec2 &b )
    {
        return a.x * b.x + a.y * b.y;
    }

    void push_colored_triangle( std::vector<rendering::VertexColor2D> &vertices,
        const rendering::Vec2 &a,
        const rendering::Color &ca,
        const rendering::Vec2 &b,
        const rendering::Color &cb,
        const rendering::Vec2 &c,
        const rendering::Color &cc )
    {
        vertices.push_back( rendering::VertexColor2D{ a, ca } );
        vertices.push_back( rendering::VertexColor2D{ b, cb } );
        vertices.push_back( rendering::VertexColor2D{ c, cc } );
    }

    void push_textured_triangle( std::vector<rendering::VertexTexture2D> &vertices,
        const rendering::Vec2 &a,
        const rendering::Vec2 &uva,
        const rendering::Vec2 &b,
        const rendering::Vec2 &uvb,
        const rendering::Vec2 &c,
        const rendering::Vec2 &uvc,
        const rendering::Color &color )
    {
        vertices.push_back( rendering::VertexTexture2D{ a, uva, color } );
        vertices.push_back( rendering::VertexTexture2D{ b, uvb, color } );
        vertices.push_back( rendering::VertexTexture2D{ c, uvc, color } );
    }

    TexturedTubeUv inset_source_uv( const rendering::Texture &texture, const rendering::Rect &source )
    {
        const float inv_w = 1.0f / static_cast<float>( std::max( texture.width, 1 ) );
        const float inv_h = 1.0f / static_cast<float>( std::max( texture.height, 1 ) );

        return TexturedTubeUv{
            .u0 = ( source.x + 0.5f ) * inv_w,
            .v0 = ( source.y + 0.5f ) * inv_h,
            .u1 = ( source.x + source.w - 0.5f ) * inv_w,
            .v1 = ( source.y + source.h - 0.5f ) * inv_h
        };
    }

    float piko_ribbon_wrap_angle_delta( float from, float to )
    {
        constexpr float tau = 6.28318530717958647692f;
        return std::remainder( to - from, tau );
    }

    float piko_ribbon_lerp_angle( float a, float b, float t )
    {
        return a + piko_ribbon_wrap_angle_delta( a, b ) * std::clamp( t, 0.0f, 1.0f );
    }

    rendering::Vec2 piko_ribbon_tangent_from_angle( float angle )
    {
        return rendering::Vec2{ std::cos( angle ), std::sin( angle ) };
    }

    float piko_ribbon_angle_from_points( const rendering::Vec2 &a, const rendering::Vec2 &b, float fallback )
    {
        const rendering::Vec2 delta = tube_sub( b, a );

        if( tube_length( delta ) <= 0.0001f )
        {
            return fallback;
        }

        return std::atan2( delta.y, delta.x );
    }

    void piko_ribbon_recompute_distances( std::vector<PikoRibbonSample> &samples )
    {
        float distance = 0.0f;

        for( std::size_t i = 0; i < samples.size(); ++i )
        {
            if( i > 0 )
            {
                distance += tube_length( tube_sub( samples[ i ].point, samples[ i - 1 ].point ) );
            }

            samples[ i ].distance = distance;
        }
    }

    std::vector<PikoRibbonSample> piko_ribbon_remove_near_duplicate_samples(
        const std::vector<PikoRibbonSample> &samples )
    {
        std::vector<PikoRibbonSample> out;
        out.reserve( samples.size() );

        for( const PikoRibbonSample &sample : samples )
        {
            if( !out.empty() && tube_length( tube_sub( sample.point, out.back().point ) ) <= 0.35f )
            {
                out.back() = sample;
                continue;
            }

            out.push_back( sample );
        }

        piko_ribbon_recompute_distances( out );
        return out;
    }

    void piko_ribbon_stabilize_angles( std::vector<PikoRibbonSample> &samples )
    {
        if( samples.size() < 2 )
        {
            return;
        }

        float previous_angle = samples.front().angle;

        for( std::size_t i = 1; i < samples.size(); ++i )
        {
            previous_angle += piko_ribbon_wrap_angle_delta( previous_angle, samples[ i ].angle );

            samples[ i ].angle = previous_angle;
        }
    }

    void piko_ribbon_recompute_geometric_angles( std::vector<PikoRibbonSample> &samples )
    {
        if( samples.size() < 2 )
        {
            return;
        }

        float previous_angle =
            piko_ribbon_angle_from_points( samples[ 0 ].point, samples[ 1 ].point, samples[ 0 ].angle );

        samples[ 0 ].angle = previous_angle;

        for( std::size_t i = 1; i + 1 < samples.size(); ++i )
        {
            const float angle =
                piko_ribbon_angle_from_points( samples[ i - 1 ].point, samples[ i + 1 ].point, previous_angle );

            previous_angle += piko_ribbon_wrap_angle_delta( previous_angle, angle );

            samples[ i ].angle = previous_angle;
        }

        const float end_angle =
            piko_ribbon_angle_from_points( samples[ samples.size() - 2 ].point, samples.back().point, previous_angle );

        samples.back().angle = previous_angle + piko_ribbon_wrap_angle_delta( previous_angle, end_angle );
    }

    void piko_ribbon_apply_presentation_stretch( std::vector<PikoRibbonSample> &samples,
        const rendering::Vec2 &center,
        float scale_x,
        float scale_y )
    {
        if( samples.empty() )
        {
            return;
        }

        scale_x = std::max( 0.001f, scale_x );

        scale_y = std::max( 0.001f, scale_y );

        if( std::abs( scale_x - 1.0f ) <= 0.0001f && std::abs( scale_y - 1.0f ) <= 0.0001f )
        {
            return;
        }

        for( PikoRibbonSample &sample : samples )
        {
            sample.point.x = center.x + ( sample.point.x - center.x ) * scale_x;

            sample.point.y = center.y + ( sample.point.y - center.y ) * scale_y;

            const float tangent_x = std::cos( sample.angle ) * scale_x;
            const float tangent_y = std::sin( sample.angle ) * scale_y;
            sample.angle = std::atan2( tangent_y, tangent_x );
        }

        piko_ribbon_stabilize_angles( samples );
        piko_ribbon_recompute_distances( samples );
    }

    std::vector<PikoRibbonSample> piko_ribbon_samples_from_points( const std::vector<rendering::Vec2> &points,
        float alpha )
    {
        std::vector<PikoRibbonSample> samples;

        if( points.empty() )
        {
            return samples;
        }

        std::vector<rendering::Vec2> clean;
        clean.reserve( points.size() );

        for( const rendering::Vec2 &point : points )
        {
            if( !clean.empty() && tube_length( tube_sub( point, clean.back() ) ) <= 0.35f )
            {
                continue;
            }

            clean.push_back( point );
        }

        if( clean.empty() )
        {
            return samples;
        }

        samples.reserve( clean.size() );

        float last_angle = 0.0f;

        for( std::size_t i = 0; i < clean.size(); ++i )
        {
            float angle = last_angle;

            if( clean.size() >= 2 )
            {
                if( i + 1 < clean.size() )
                {
                    angle = piko_ribbon_angle_from_points( clean[ i ], clean[ i + 1 ], last_angle );
                }
                else
                {
                    angle = piko_ribbon_angle_from_points( clean[ i - 1 ], clean[ i ], last_angle );
                }
            }

            last_angle = angle;
            samples.push_back( PikoRibbonSample{ .point = clean[ i ], .angle = angle, .alpha = alpha } );
        }

        piko_ribbon_stabilize_angles( samples );
        piko_ribbon_recompute_distances( samples );
        return samples;
    }

    std::vector<PikoRibbonSample> piko_ribbon_samples_from_trace_screen_points(
        const std::vector<runtime::stage::TraceScreenPoint> &points,
        float alpha )
    {
        std::vector<PikoRibbonSample> samples;
        samples.reserve( points.size() );

        for( const runtime::stage::TraceScreenPoint &point : points )
        {
            samples.push_back( PikoRibbonSample{ .point = point.point, .angle = point.angle, .alpha = alpha } );
        }

        samples = piko_ribbon_remove_near_duplicate_samples( samples );
        piko_ribbon_stabilize_angles( samples );
        piko_ribbon_recompute_distances( samples );
        return samples;
    }

    std::vector<PikoRibbonSample> piko_ribbon_samples_from_trace_render_points(
        const std::vector<runtime::stage::TraceRenderPoint> &points,
        float alpha_scale )
    {
        std::vector<PikoRibbonSample> samples;
        samples.reserve( points.size() );

        alpha_scale = std::clamp( alpha_scale, 0.0f, 1.0f );

        for( const runtime::stage::TraceRenderPoint &point : points )
        {
            samples.push_back( PikoRibbonSample{
                .point = point.point,
                .angle = point.angle,
                .alpha = std::clamp( point.alpha * alpha_scale, 0.0f, 1.0f )
            } );
        }

        samples = piko_ribbon_remove_near_duplicate_samples( samples );
        piko_ribbon_stabilize_angles( samples );
        piko_ribbon_recompute_distances( samples );
        return samples;
    }

    std::vector<PikoRibbonSample> piko_ribbon_densify_samples( const std::vector<PikoRibbonSample> &samples,
        float max_segment_pixels )
    {
        if( samples.size() < 2 || max_segment_pixels <= 0.5f )
        {
            return samples;
        }

        max_segment_pixels = std::max( 1.0f, max_segment_pixels );

        std::vector<PikoRibbonSample> out;
        out.reserve( samples.size() * 2u );
        out.push_back( samples.front() );

        for( std::size_t i = 1; i < samples.size(); ++i )
        {
            const PikoRibbonSample &a = samples[ i - 1 ];
            const PikoRibbonSample &b = samples[ i ];
            const float length = tube_length( tube_sub( b.point, a.point ) );
            const std::int32_t steps =
                std::max( 1, static_cast<std::int32_t>( std::ceil( length / max_segment_pixels ) ) );

            for( std::int32_t step = 1; step <= steps; ++step )
            {
                const float t = static_cast<float>( step ) / static_cast<float>( steps );
                out.push_back( PikoRibbonSample{
                    .point =
                        rendering::Vec2{ std::lerp( a.point.x, b.point.x, t ), std::lerp( a.point.y, b.point.y, t ) },
                    .angle = piko_ribbon_lerp_angle( a.angle, b.angle, t ),
                    .alpha = std::lerp( a.alpha, b.alpha, t )
                } );
            }
        }

        out = piko_ribbon_remove_near_duplicate_samples( out );
        piko_ribbon_stabilize_angles( out );
        piko_ribbon_recompute_distances( out );
        return out;
    }

    void piko_ribbon_insert_sample_at_distance( std::vector<PikoRibbonSample> &samples, float target_distance )
    {
        if( samples.size() < 2 )
        {
            return;
        }

        piko_ribbon_recompute_distances( samples );

        const float total_distance = samples.back().distance;
        target_distance = std::clamp( target_distance, 0.0f, total_distance );

        if( target_distance <= 0.001f || target_distance >= total_distance - 0.001f )
        {
            return;
        }

        for( std::size_t i = 1; i < samples.size(); ++i )
        {
            const PikoRibbonSample &a = samples[ i - 1 ];
            const PikoRibbonSample &b = samples[ i ];

            if( target_distance <= a.distance + 0.001f || target_distance >= b.distance - 0.001f )
            {
                continue;
            }

            const float span_distance = b.distance - a.distance;

            if( span_distance <= 0.001f )
            {
                continue;
            }

            const float t = std::clamp( ( target_distance - a.distance ) / span_distance, 0.0f, 1.0f );

            PikoRibbonSample inserted{
                .point = rendering::Vec2{ std::lerp( a.point.x, b.point.x, t ), std::lerp( a.point.y, b.point.y, t ) },
                .angle = piko_ribbon_lerp_angle( a.angle, b.angle, t ),
                .distance = target_distance,
                .alpha = std::lerp( a.alpha, b.alpha, t )
            };

            samples.insert( samples.begin() + static_cast<std::ptrdiff_t>( i ), inserted );
            piko_ribbon_recompute_distances( samples );
            return;
        }
    }

    std::vector<PikoRibbonSample> piko_ribbon_slice_by_distance( const std::vector<PikoRibbonSample> &input_samples,
        float start_distance,
        float end_distance )
    {
        std::vector<PikoRibbonSample> samples = input_samples;
        samples = piko_ribbon_remove_near_duplicate_samples( samples );

        if( samples.size() < 2 )
        {
            return samples;
        }

        piko_ribbon_recompute_distances( samples );

        const float total_distance = samples.back().distance;
        start_distance = std::clamp( start_distance, 0.0f, total_distance );

        end_distance = std::clamp( end_distance, start_distance, total_distance );

        if( end_distance <= start_distance + 0.5f )
        {
            return {};
        }

        piko_ribbon_insert_sample_at_distance( samples, start_distance );
        piko_ribbon_insert_sample_at_distance( samples, end_distance );
        piko_ribbon_recompute_distances( samples );

        std::vector<PikoRibbonSample> out;
        out.reserve( samples.size() );

        for( const PikoRibbonSample &sample : samples )
        {
            if( sample.distance + 0.001f < start_distance || sample.distance > end_distance + 0.001f )
            {
                continue;
            }

            out.push_back( sample );
        }

        out = piko_ribbon_remove_near_duplicate_samples( out );
        piko_ribbon_stabilize_angles( out );
        piko_ribbon_recompute_distances( out );
        return out;
    }

    void piko_ribbon_append_cap_tail( std::vector<PikoRibbonSample> &samples, float cap_length_pixels )
    {
        if( samples.size() < 2 || cap_length_pixels <= 0.0f )
        {
            return;
        }

        PikoRibbonSample tail = samples.back();
        const rendering::Vec2 tangent = piko_ribbon_tangent_from_angle( tail.angle );
        tail.point = tube_add( tail.point, tube_mul( tangent, cap_length_pixels ) );

        tail.distance = samples.back().distance + cap_length_pixels;
        samples.push_back( tail );
    }

    void piko_ribbon_edge_pair( const PikoRibbonSample &sample,
        float half_width,
        rendering::Vec2 &out_left,
        rendering::Vec2 &out_right )
    {
        const rendering::Vec2 tangent = piko_ribbon_tangent_from_angle( sample.angle );
        const rendering::Vec2 normal = tube_left_normal( tangent );
        out_left = tube_add( sample.point, tube_mul( normal, -half_width ) );

        out_right = tube_add( sample.point, tube_mul( normal, half_width ) );
    }

    bool draw_textured_ribbon( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const std::vector<PikoRibbonSample> &input_samples,
        const TexturedRibbonAppearance &appearance )
    {
        if( input_samples.size() < 2 || !texture.valid() || appearance.height <= 0.0f )
        {
            return false;
        }

        const float half_height = std::max( appearance.height * 0.5f, 1.0f );
        const TexturedTubeUv body_uv = inset_source_uv( texture, appearance.body_source );
        const TexturedTubeUv cap_uv =
            appearance.cap_source != nullptr ? inset_source_uv( texture, *appearance.cap_source ) : body_uv;

        const float total_distance = input_samples.back().distance;
        const bool has_cap =
            appearance.cap_source != nullptr && appearance.cap_start_distance < total_distance - 0.001f;

        const float body_distance = has_cap ? std::max( appearance.cap_start_distance, 1.0f ) : total_distance;
        const float cap_distance = has_cap ? std::max( total_distance - appearance.cap_start_distance, 1.0f ) : 1.0f;

        // Repeat at the PNG's aspect ratio.
        const float body_repeat =
            std::max( 1.0f, appearance.height * appearance.body_source.w / std::max( appearance.body_source.h, 1.0f ) );

        auto samples = input_samples;

        for( float distance = body_repeat; distance < body_distance - 0.001f; distance += body_repeat )
        {
            piko_ribbon_insert_sample_at_distance( samples, distance );
        }

        const float start_cutout_radius = std::max( 0.0f, appearance.start_cutout_radius );
        const bool use_start_cutout = start_cutout_radius > 0.5f;

        std::vector<rendering::VertexTexture2D> vertices;
        vertices.reserve( ( samples.size() - 1u ) *
                          static_cast<std::size_t>( use_start_cutout ? phrase_tube_note_head_cutout_bands * 6 : 6 ) );

        const auto sample_tint = [ & ]( const PikoRibbonSample &sample )
        {
            rendering::Color color = appearance.tint;

            if( appearance.pulse.enabled )
            {
                constexpr float tau = 6.28318530717958647692f;
                const float beat_cycles_per_second = std::clamp( appearance.pulse.bpm, 60.0f, 240.0f ) / 60.0f;
                const float wave =
                    0.5f +
                    0.5f * std::sin(
                               ( sample.distance / 96.0f - appearance.time_seconds * beat_cycles_per_second ) * tau );

                const float amount = 0.16f + 0.46f * wave;
                color = rendering::lerp_color( appearance.tint, appearance.pulse.tint, amount );
            }

            color.a *= std::clamp( sample.alpha, 0.0f, 1.0f );
            return color;
        };

        const auto sample_between = []( const PikoRibbonSample &a, const PikoRibbonSample &b, float distance )
        {
            const float span = std::max( b.distance - a.distance, 0.0001f );
            const float t = std::clamp( ( distance - a.distance ) / span, 0.0f, 1.0f );

            return PikoRibbonSample{
                .point = rendering::Vec2{ std::lerp( a.point.x, b.point.x, t ), std::lerp( a.point.y, b.point.y, t ) },
                .angle = piko_ribbon_lerp_angle( a.angle, b.angle, t ),
                .distance = distance,
                .alpha = std::lerp( a.alpha, b.alpha, t )
            };
        };

        const auto sample_point_at_width = [ & ]( const PikoRibbonSample &sample, float y )
        {
            const rendering::Vec2 tangent = piko_ribbon_tangent_from_angle( sample.angle );
            const rendering::Vec2 normal = tube_left_normal( tangent );
            return tube_add( sample.point, tube_mul( normal, y ) );
        };

        const auto append_textured_quad = [ & ]( const rendering::Vec2 &left0,
                                              const rendering::Vec2 &right0,
                                              const rendering::Vec2 &left1,
                                              const rendering::Vec2 &right1,
                                              const rendering::Vec2 &uv_left0,
                                              const rendering::Vec2 &uv_right0,
                                              const rendering::Vec2 &uv_left1,
                                              const rendering::Vec2 &uv_right1,
                                              const rendering::Color &color0,
                                              const rendering::Color &color1 )
        {
            vertices.push_back( rendering::VertexTexture2D{ left0, uv_left0, color0 } );
            vertices.push_back( rendering::VertexTexture2D{ right0, uv_right0, color0 } );
            vertices.push_back( rendering::VertexTexture2D{ left1, uv_left1, color1 } );

            vertices.push_back( rendering::VertexTexture2D{ right0, uv_right0, color0 } );
            vertices.push_back( rendering::VertexTexture2D{ right1, uv_right1, color1 } );
            vertices.push_back( rendering::VertexTexture2D{ left1, uv_left1, color1 } );
        };

        for( std::size_t i = 0; i + 1 < samples.size(); ++i )
        {
            const PikoRibbonSample &a = samples[ i ];
            const PikoRibbonSample &b = samples[ i + 1 ];

            if( b.distance <= a.distance + 0.0001f )
            {
                continue;
            }

            const bool cap_segment = has_cap && a.distance >= appearance.cap_start_distance - 0.001f;
            const TexturedTubeUv &uv = cap_segment ? cap_uv : body_uv;

            const float tile_start = std::floor( ( a.distance + 0.001f ) / body_repeat ) * body_repeat;

            const auto u_at_distance = [ & ]( float distance )
            {
                const float t =
                    cap_segment ? std::clamp( ( distance - appearance.cap_start_distance ) / cap_distance, 0.0f, 1.0f )
                                : std::clamp( ( distance - tile_start ) / body_repeat, 0.0f, 1.0f );

                return std::lerp( uv.u0, uv.u1, t );
            };

            const auto v_at_y = [ & ]( float y )
            {
                const float t = std::clamp( ( y + half_height ) / ( half_height * 2.0f ), 0.0f, 1.0f );
                return std::lerp( uv.v0, uv.v1, t );
            };

            const bool cutout_segment = use_start_cutout && !cap_segment && a.distance < start_cutout_radius - 0.001f;

            if( cutout_segment )
            {
                const std::int32_t bands = std::max( 4, phrase_tube_note_head_cutout_bands );

                for( std::int32_t band = 0; band < bands; ++band )
                {
                    const float band_t0 = static_cast<float>( band ) / static_cast<float>( bands );
                    const float band_t1 = static_cast<float>( band + 1 ) / static_cast<float>( bands );
                    const float y0 = std::lerp( -half_height, half_height, band_t0 );
                    const float y1 = std::lerp( -half_height, half_height, band_t1 );

                    const auto cut_distance_for_y = [ & ]( float y )
                    {
                        const float inner = start_cutout_radius * start_cutout_radius - y * y;
                        return inner > 0.0f ? std::sqrt( inner ) : 0.0f;
                    };

                    const float start0 = std::max( a.distance, cut_distance_for_y( y0 ) );
                    const float start1 = std::max( a.distance, cut_distance_for_y( y1 ) );

                    if( start0 >= b.distance - 0.05f && start1 >= b.distance - 0.05f )
                    {
                        continue;
                    }

                    const float d0 = std::clamp( start0, a.distance, b.distance );
                    const float d1 = std::clamp( start1, a.distance, b.distance );

                    const PikoRibbonSample left_sample0 = sample_between( a, b, d0 );
                    const PikoRibbonSample left_sample1 = sample_between( a, b, d1 );
                    const PikoRibbonSample right_sample = b;

                    const rendering::Vec2 left0 = sample_point_at_width( left_sample0, y0 );
                    const rendering::Vec2 right0 = sample_point_at_width( left_sample1, y1 );
                    const rendering::Vec2 left1 = sample_point_at_width( right_sample, y0 );
                    const rendering::Vec2 right1 = sample_point_at_width( right_sample, y1 );

                    const rendering::Vec2 uv_left0{ u_at_distance( d0 ), v_at_y( y0 ) };
                    const rendering::Vec2 uv_right0{ u_at_distance( d1 ), v_at_y( y1 ) };
                    const rendering::Vec2 uv_left1{ u_at_distance( b.distance ), v_at_y( y0 ) };
                    const rendering::Vec2 uv_right1{ u_at_distance( b.distance ), v_at_y( y1 ) };

                    const rendering::Color color_left0 = sample_tint( left_sample0 );
                    const rendering::Color color_left1 = sample_tint( left_sample1 );
                    const rendering::Color color_right = sample_tint( right_sample );

                    vertices.push_back( rendering::VertexTexture2D{ left0, uv_left0, color_left0 } );
                    vertices.push_back( rendering::VertexTexture2D{ right0, uv_right0, color_left1 } );
                    vertices.push_back( rendering::VertexTexture2D{ left1, uv_left1, color_right } );

                    vertices.push_back( rendering::VertexTexture2D{ right0, uv_right0, color_left1 } );
                    vertices.push_back( rendering::VertexTexture2D{ right1, uv_right1, color_right } );
                    vertices.push_back( rendering::VertexTexture2D{ left1, uv_left1, color_right } );
                }

                continue;
            }

            rendering::Vec2 left0{};
            rendering::Vec2 right0{};
            rendering::Vec2 left1{};
            rendering::Vec2 right1{};
            piko_ribbon_edge_pair( a, half_height, left0, right0 );
            piko_ribbon_edge_pair( b, half_height, left1, right1 );

            const float u0 = u_at_distance( a.distance );
            const float u1 = u_at_distance( b.distance );

            const rendering::Vec2 uv_left0{ u0, uv.v0 };
            const rendering::Vec2 uv_right0{ u0, uv.v1 };
            const rendering::Vec2 uv_left1{ u1, uv.v0 };
            const rendering::Vec2 uv_right1{ u1, uv.v1 };

            rendering::Color color0 = sample_tint( a );
            rendering::Color color1 = sample_tint( b );

            if( color0.a <= 0.001f && color1.a <= 0.001f )
            {
                continue;
            }

            append_textured_quad( left0,
                right0,
                left1,
                right1,
                uv_left0,
                uv_right0,
                uv_left1,
                uv_right1,
                color0,
                color1 );
        }

        if( vertices.empty() )
        {
            return false;
        }

        if( std::abs( appearance.presentation.scale_x - 1.0f ) > 0.0001f ||
            std::abs( appearance.presentation.scale_y - 1.0f ) > 0.0001f )
        {
            for( rendering::VertexTexture2D &vertex : vertices )
            {
                vertex.position.x =
                    appearance.presentation.center.x +
                    ( vertex.position.x - appearance.presentation.center.x ) * appearance.presentation.scale_x;

                vertex.position.y =
                    appearance.presentation.center.y +
                    ( vertex.position.y - appearance.presentation.center.y ) * appearance.presentation.scale_y;
            }
        }

        renderer.draw_textured_triangles_single_coverage( texture, vertices.data(), vertices.size() );
        return true;
    }

    std::vector<rendering::Vec2> remove_near_duplicate_points( const std::vector<rendering::Vec2> &points )
    {
        std::vector<rendering::Vec2> out;
        out.reserve( points.size() );

        for( const rendering::Vec2 &point : points )
        {
            if( !out.empty() && tube_length( tube_sub( point, out.back() ) ) <= 0.35f )
            {
                continue;
            }

            out.push_back( point );
        }

        return out;
    }

    std::vector<PhraseTubeFrame> build_phrase_tube_frames( const std::vector<rendering::Vec2> &points )
    {
        std::vector<PhraseTubeFrame> frames;

        if( points.empty() )
        {
            return frames;
        }

        frames.resize( points.size() );

        float distance = 0.0f;
        rendering::Vec2 last_good_tangent{ 1.0f, 0.0f };

        for( std::size_t i = 0; i < points.size(); ++i )
        {
            rendering::Vec2 tangent{ 1.0f, 0.0f };

            if( points.size() == 1 )
            {
                tangent = last_good_tangent;
            }
            else if( i == 0 )
            {
                tangent = tube_normalize_or( tube_sub( points[ 1 ], points[ 0 ] ), last_good_tangent );
            }
            else if( i + 1 == points.size() )
            {
                tangent = tube_normalize_or( tube_sub( points[ i ], points[ i - 1 ] ), last_good_tangent );
            }
            else
            {
                const rendering::Vec2 prev =
                    tube_normalize_or( tube_sub( points[ i ], points[ i - 1 ] ), last_good_tangent );

                const rendering::Vec2 next = tube_normalize_or( tube_sub( points[ i + 1 ], points[ i ] ), prev );

                tangent = tube_normalize_or( tube_add( prev, next ), next );
            }

            last_good_tangent = tangent;

            if( i > 0 )
            {
                distance += tube_length( tube_sub( points[ i ], points[ i - 1 ] ) );
            }

            frames[ i ].point = points[ i ];

            frames[ i ].tangent = tangent;

            frames[ i ].normal = tube_normalize_or( tube_left_normal( tangent ), rendering::Vec2{ 0.0f, -1.0f } );

            frames[ i ].distance = distance;
        }

        return frames;
    }

    void phrase_tube_ribbon_edges_at( const std::vector<PhraseTubeFrame> &frames,
        std::size_t index,
        float half_width,
        float miter_limit,
        rendering::Vec2 &out_left,
        rendering::Vec2 &out_right )
    {
        const PhraseTubeFrame &frame = frames[ index ];
        rendering::Vec2 normal = frame.normal;
        float half_width_scale = half_width;

        if( index > 0 && index + 1 < frames.size() )
        {
            const rendering::Vec2 prev_dir =
                tube_normalize_or( tube_sub( frames[ index ].point, frames[ index - 1 ].point ), frame.tangent );

            const rendering::Vec2 next_dir =
                tube_normalize_or( tube_sub( frames[ index + 1 ].point, frames[ index ].point ), frame.tangent );

            const rendering::Vec2 tangent = tube_normalize_or( tube_add( prev_dir, next_dir ), next_dir );

            rendering::Vec2 miter = tube_normalize_or( tube_left_normal( tangent ), frame.normal );

            const rendering::Vec2 prev_normal = tube_normalize_or( tube_left_normal( prev_dir ), frame.normal );

            float denom = tube_dot( miter, prev_normal );

            if( denom < 0.0f )
            {
                miter = tube_mul( miter, -1.0f );

                denom = -denom;
            }

            if( denom > 0.18f )
            {
                normal = miter;

                const float limit = std::max( 1.0f, miter_limit ) * half_width;
                half_width_scale = std::clamp( half_width / denom, half_width * 0.65f, limit );
            }
        }

        out_left = tube_add( frame.point, tube_mul( normal, -half_width_scale ) );

        out_right = tube_add( frame.point, tube_mul( normal, half_width_scale ) );
    }

    void phrase_tube_ribbon_edges_stable_at( const std::vector<PhraseTubeFrame> &frames,
        std::size_t index,
        float half_width,
        rendering::Vec2 &out_left,
        rendering::Vec2 &out_right )
    {
        const PhraseTubeFrame &frame = frames[ index ];
        rendering::Vec2 normal = frame.normal;

        if( index > 0 && tube_dot( normal, frames[ index - 1 ].normal ) < 0.0f )
        {
            normal = tube_mul( normal, -1.0f );
        }

        out_left = tube_add( frame.point, tube_mul( normal, -half_width ) );

        out_right = tube_add( frame.point, tube_mul( normal, half_width ) );
    }

    void draw_phrase_tube_outer_ribbon( rendering::Renderer &renderer,
        const std::vector<PhraseTubeFrame> &frames,
        float half_width,
        const rendering::Color &color )
    {
        if( frames.size() < 2 || half_width <= 0.0f || color.a <= 0.0f )
        {
            return;
        }

        std::vector<rendering::VertexColor2D> vertices;
        vertices.reserve( ( frames.size() - 1u ) * 6u );

        for( std::size_t i = 0; i + 1 < frames.size(); ++i )
        {
            rendering::Vec2 left0{};
            rendering::Vec2 right0{};
            rendering::Vec2 left1{};
            rendering::Vec2 right1{};

            phrase_tube_ribbon_edges_at( frames, i, half_width, phrase_tube_miter_limit, left0, right0 );
            phrase_tube_ribbon_edges_at( frames, i + 1, half_width, phrase_tube_miter_limit, left1, right1 );

            push_colored_triangle( vertices, left0, color, right0, color, left1, color );
            push_colored_triangle( vertices, right0, color, right1, color, left1, color );
        }

        renderer.draw_colored_triangles( vertices.data(), vertices.size() );
    }
}
