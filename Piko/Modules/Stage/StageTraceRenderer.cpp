#include "Modules/Stage/StageTraceRenderer.hpp"
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace piko::modules::stage
{
    float trace_tangent_degrees_at( const runtime::stage::TracePosition &current_position,
        const runtime::stage::StageGameplayView &view )
    {
        const float screen_angle = runtime::stage::gameplay_world_angle_to_screen_angle( current_position.angle, view );
        const float direction_x = std::cos( screen_angle ) * view.presentation_scale_x;
        const float direction_y = std::sin( screen_angle ) * view.presentation_scale_y;
        return std::atan2( direction_y, direction_x ) * radians_to_degrees;
    }

    void draw_trace_render_points( rendering::Renderer &renderer,
        const std::vector<runtime::stage::TraceRenderPoint> &points,
        float width,
        const rendering::Color &base_color,
        float alpha_scale,
        const runtime::stage::StageGameplayView *presentation_view )
    {
        if( points.size() < 2 )
        {
            return;
        }

        alpha_scale = std::clamp( alpha_scale, 0.0f, 1.0f );

        for( std::size_t i = 1; i < points.size(); ++i )
        {
            const runtime::stage::TraceRenderPoint &a = points[ i - 1 ];
            const runtime::stage::TraceRenderPoint &b = points[ i ];

            const float alpha = std::min( a.alpha, b.alpha ) * base_color.a * alpha_scale;

            if( alpha <= 0.001f )
            {
                continue;
            }

            rendering::Color color = base_color;
            color.a = alpha;

            renderer.draw_line( presentation_view != nullptr
                                    ? runtime::stage::apply_gameplay_presentation_stretch( a.point, *presentation_view )
                                    : a.point,
                presentation_view != nullptr
                    ? runtime::stage::apply_gameplay_presentation_stretch( b.point, *presentation_view )
                    : b.point,
                width,
                color );
        }
    }

    std::vector<TraceRibbonSample> densify_trace_ribbon_samples(
        const std::vector<runtime::stage::TraceRenderPoint> &points,
        float max_segment_pixels )
    {
        std::vector<TraceRibbonSample> out;

        if( points.empty() )
        {
            return out;
        }

        out.reserve( points.size() * 2u );
        out.push_back( TraceRibbonSample{ points.front().point, points.front().alpha } );

        max_segment_pixels = std::max( max_segment_pixels, 1.0f );

        for( std::size_t i = 1; i < points.size(); ++i )
        {
            const runtime::stage::TraceRenderPoint &a = points[ i - 1 ];
            const runtime::stage::TraceRenderPoint &b = points[ i ];

            const float length = tube_length( tube_sub( b.point, a.point ) );
            const std::int32_t steps =
                std::max( 1, static_cast<std::int32_t>( std::ceil( length / max_segment_pixels ) ) );

            for( std::int32_t step = 1; step <= steps; ++step )
            {
                const float t = static_cast<float>( step ) / static_cast<float>( steps );

                out.push_back( TraceRibbonSample{
                    .point =
                        rendering::Vec2{ std::lerp( a.point.x, b.point.x, t ), std::lerp( a.point.y, b.point.y, t ) },
                    .alpha = std::lerp( a.alpha, b.alpha, t )
                } );
            }
        }

        return out;
    }

    bool draw_generated_trace_render_points( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const std::vector<runtime::stage::TraceRenderPoint> &points,
        float height,
        const rendering::Color &base_color,
        float alpha_scale,
        rendering::Vec2 presentation_center,
        float presentation_scale_x,
        float presentation_scale_y )
    {
        if( points.size() < 2 || !texture.valid() )
        {
            return false;
        }

        std::vector<PikoRibbonSample> samples = piko_ribbon_samples_from_trace_render_points( points, alpha_scale );

        samples = piko_ribbon_densify_samples( samples, piko_ribbon_trace_max_segment_pixels );

        samples = piko_ribbon_remove_near_duplicate_samples( samples );

        piko_ribbon_apply_presentation_stretch( samples,
            presentation_center,
            presentation_scale_x,
            presentation_scale_y );

        piko_ribbon_recompute_geometric_angles( samples );
        piko_ribbon_recompute_distances( samples );

        samples.erase( std::remove_if( samples.begin(),
                           samples.end(),
                           []( const PikoRibbonSample &sample )
                           {
                               return sample.alpha <= 0.001f;
                           } ),
            samples.end() );

        if( samples.size() < 2 )
        {
            return false;
        }

        return draw_textured_ribbon( renderer,
            texture,
            samples,
            TexturedRibbonAppearance{
                .height = height,
                .body_source =
                    rendering::Rect{
                        0.0f,
                        0.0f,
                        static_cast<float>( texture.width ),
                        static_cast<float>( texture.height )
                    },
                .cap_source = nullptr,
                .cap_start_distance = samples.back().distance,
                .tint = base_color,
                .presentation =
                    RibbonPresentation{ .center = rendering::Vec2{ 0.0f, 0.0f }, .scale_x = 1.0f, .scale_y = 1.0f }
            } );
    }
}
