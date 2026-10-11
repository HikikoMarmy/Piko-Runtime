#include "StageView.hpp"
#include "StageViewUtil.hpp"

#include "Rendering/RenderUtil.hpp"

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cmath>

namespace piko::runtime::stage
{
    rendering::Vec2 StageView::trace_to_screen( float x, float y ) const
    {
        return rendering::Vec2{ center_x + ( x - trace_center_x ) * scale, center_y - ( y - trace_center_y ) * scale };
    }

    rendering::Vec2 StageView::trace_to_screen( const TraceSample &sample ) const
    {
        return trace_to_screen( sample.x, sample.y );
    }

    TraceBounds calculate_trace_bounds( const std::vector<TraceSample> &samples )
    {
        TraceBounds bounds{};

        if( samples.empty() )
        {
            return bounds;
        }

        bounds.min_x = samples.front().x;

        bounds.max_x = samples.front().x;

        bounds.min_y = samples.front().y;

        bounds.max_y = samples.front().y;

        bounds.valid = true;

        for( const TraceSample &sample : samples )
        {
            bounds.min_x = std::min( bounds.min_x, sample.x );

            bounds.max_x = std::max( bounds.max_x, sample.x );

            bounds.min_y = std::min( bounds.min_y, sample.y );

            bounds.max_y = std::max( bounds.max_y, sample.y );
        }

        return bounds;
    }

    StageView fit_trace_to_area( const std::vector<TraceSample> &samples,
        const rendering::Rect &area,
        float scale_multiplier )
    {
        StageView view{};
        view.area = area;

        view.bounds = calculate_trace_bounds( samples );

        if( !view.bounds.valid )
        {
            view.center_x = area.x + area.w * 0.5f;

            view.center_y = area.y + area.h * 0.5f;
            return view;
        }

        const float trace_w = view::clamp_min( view.bounds.max_x - view.bounds.min_x, 1.0f );
        const float trace_h = view::clamp_min( view.bounds.max_y - view.bounds.min_y, 1.0f );

        view.scale = std::min( area.w / trace_w, area.h / trace_h ) * std::clamp( scale_multiplier, 0.01f, 10.0f );

        view.center_x = area.x + area.w * 0.5f;

        view.center_y = area.y + area.h * 0.5f;

        view.trace_center_x = ( view.bounds.min_x + view.bounds.max_x ) * 0.5f;

        view.trace_center_y = ( view.bounds.min_y + view.bounds.max_y ) * 0.5f;
        return view;
    }

    std::vector<TraceScreenPoint> make_trace_screen_points( const std::vector<TraceSample> &samples,
        const StageView &view,
        std::size_t max_points )
    {
        std::vector<TraceScreenPoint> points;

        if( samples.empty() )
        {
            return points;
        }

        if( max_points == 0 )
        {
            max_points = samples.size();
        }

        const std::size_t stride = std::max<std::size_t>( 1, samples.size() / max_points );
        points.reserve( samples.size() / stride + 1 );

        for( std::size_t i = 0; i < samples.size(); i += stride )
        {
            const TraceSample &sample = samples[ i ];
            points.push_back( TraceScreenPoint{
                .point = view.trace_to_screen( sample ),
                .chart_sample = sample.chart_sample,
                .angle = view::screen_angle_from_world_angle( sample.angle )
            } );
        }

        if( points.empty() || points.back().chart_sample != samples.back().chart_sample )
        {
            const TraceSample &sample = samples.back();
            points.push_back( TraceScreenPoint{
                .point = view.trace_to_screen( sample ),
                .chart_sample = sample.chart_sample,
                .angle = view::screen_angle_from_world_angle( sample.angle )
            } );
        }

        return points;
    }

    std::vector<TraceScreenPoint> round_hard_trace_corners_windowed( const std::vector<TraceScreenPoint> &points,
        float radius_pixels,
        std::int32_t steps,
        float min_bend_degrees )
    {
        if( points.size() < 3 )
        {
            return points;
        }

        std::vector<TraceScreenPoint> out;
        out.reserve( points.size() * static_cast<std::size_t>( std::max( 2, steps ) ) );

        out.push_back( points.front() );

        std::size_t i = 1;

        while( i + 1 < points.size() )
        {
            float back_distance = 0.0f;
            float forward_distance = 0.0f;

            const std::size_t back_index = view::look_back_trace_by_distance( points, i, radius_pixels, back_distance );

            const std::size_t forward_index =
                view::look_forward_trace_by_distance( points, i, radius_pixels, forward_distance );

            const bool has_enough_space =
                back_index < i && forward_index > i && back_distance >= 8.0f && forward_distance >= 8.0f;

            if( !has_enough_space )
            {
                out.push_back( points[ i ] );
                ++i;
                continue;
            }

            const TraceScreenPoint &previous = points[ back_index ];
            const TraceScreenPoint &corner = points[ i ];
            const TraceScreenPoint &next = points[ forward_index ];

            const float bend = view::bend_angle_degrees( previous.point, corner.point, next.point );

            if( bend < min_bend_degrees )
            {
                out.push_back( points[ i ] );
                ++i;
                continue;
            }

            const float cut_back = std::min( radius_pixels, back_distance * 0.45f );
            const float cut_forward = std::min( radius_pixels, forward_distance * 0.45f );

            TraceScreenPoint entry = view::trace_point_towards( corner, previous, cut_back );
            TraceScreenPoint exit = view::trace_point_towards( corner, next, cut_forward );

            if( rendering::distance_between( out.back().point, entry.point ) > 0.5f )
            {
                out.push_back( entry );
            }

            const std::int32_t safe_steps = std::max( 4, steps );

            for( std::int32_t step = 1; step < safe_steps; ++step )
            {
                const float t = static_cast<float>( step ) / static_cast<float>( safe_steps );

                TraceScreenPoint curved = view::lerp_trace_screen_point( entry, exit, t );
                curved.point = view::quadratic_bezier( entry.point, corner.point, exit.point, t );
                out.push_back( curved );
            }

            out.push_back( exit );
            i = std::max( i + 1, forward_index );
        }

        out.push_back( points.back() );
        return out;
    }

    TracePosition visual_trace_position_at( const std::vector<TraceSample> &samples,
        std::int64_t chart_sample,
        bool use_prestart_extrapolation )
    {
        if( !use_prestart_extrapolation || samples.empty() || samples.size() < 2 ||
            chart_sample >= samples.front().chart_sample )
        {
            return trace_position_at( samples, chart_sample );
        }

        const TraceSample &first = samples[ 0 ];
        const TraceSample &second = samples[ 1 ];

        const std::int64_t sample_span =
            static_cast<std::int64_t>( second.chart_sample ) - static_cast<std::int64_t>( first.chart_sample );

        if( sample_span <= 0 )
        {
            return trace_position_at( samples, chart_sample );
        }

        const float dx_per_sample = ( second.x - first.x ) / static_cast<float>( sample_span );

        const float dy_per_sample = ( second.y - first.y ) / static_cast<float>( sample_span );

        const float samples_before =
            static_cast<float>( static_cast<std::int64_t>( first.chart_sample ) - chart_sample );

        const float x = first.x - dx_per_sample * samples_before;
        const float y = first.y - dy_per_sample * samples_before;

        return TracePosition{ .x = x, .y = y, .angle = std::atan2( dy_per_sample, dx_per_sample ) };
    }

    rendering::Vec2 gameplay_world_to_screen( float world_x,
        float world_y,
        const TracePosition &current_position,
        const StageGameplayView &view )
    {
        return rendering::Vec2{
            view.center_x + ( world_x - current_position.x ) * view.zoom * view.scale_x * view.note_speed,
            view.center_y - ( world_y - current_position.y ) * view.zoom * view.scale_y * view.note_speed
        };
    }

    rendering::Vec2 apply_gameplay_presentation_stretch( const rendering::Vec2 &point, const StageGameplayView &view )
    {
        return rendering::Vec2{
            view.presentation_center_x + ( point.x - view.presentation_center_x ) * view.presentation_scale_x,
            view.presentation_center_y + ( point.y - view.presentation_center_y ) * view.presentation_scale_y
        };
    }

    float gameplay_world_angle_to_screen_angle( float world_angle, const StageGameplayView &view )
    {
        const float x_scale = std::max( std::abs( view.scale_x ), 0.0001f );
        const float y_scale = std::max( std::abs( view.scale_y ), 0.0001f );
        const float dx = std::cos( world_angle ) * x_scale;
        const float dy = -std::sin( world_angle ) * y_scale;

        return std::atan2( dy, dx );
    }

    std::vector<TraceRenderPoint> make_gameplay_trace_render_points( const std::vector<TraceSample> &samples,
        const TracePosition &current_position,
        std::int64_t current_chart_sample,
        const StageGameplayView &view,
        std::int64_t lookahead_samples,
        std::int64_t fade_start_samples,
        float min_alpha,
        float max_alpha )
    {
        std::vector<TraceRenderPoint> points;

        if( samples.size() < 2 || lookahead_samples <= 0 )
        {
            return points;
        }

        if( fade_start_samples < 0 )
        {
            fade_start_samples = 0;
        }

        if( fade_start_samples >= lookahead_samples )
        {
            fade_start_samples = lookahead_samples - 1;
        }

        min_alpha = std::clamp( min_alpha, 0.0f, 1.0f );

        max_alpha = std::clamp( max_alpha, 0.0f, 1.0f );

        const auto alpha_for_delta = [ & ]( std::int64_t delta_samples ) -> float
        {
            if( delta_samples < 0 || delta_samples > lookahead_samples )
            {
                return 0.0f;
            }

            if( delta_samples <= fade_start_samples )
            {
                return max_alpha;
            }

            const float t = static_cast<float>( delta_samples - fade_start_samples ) /
                            static_cast<float>( lookahead_samples - fade_start_samples );

            return std::lerp( max_alpha, min_alpha, std::clamp( t, 0.0f, 1.0f ) );
        };

        const auto append_position = [ & ]( const TracePosition &position, std::int64_t delta_samples )
        {
            const float alpha = alpha_for_delta( delta_samples );

            if( alpha <= 0.0f )
            {
                return;
            }

            const rendering::Vec2 point = gameplay_world_to_screen( position.x, position.y, current_position, view );

            if( !points.empty() && rendering::distance_between( points.back().point, point ) <= 0.0001f )
            {
                points.back().delta_samples = delta_samples;
                points.back().alpha = alpha;
                points.back().angle = gameplay_world_angle_to_screen_angle( position.angle, view );
                return;
            }

            points.push_back( TraceRenderPoint{
                .point = point,
                .delta_samples = delta_samples,
                .alpha = alpha,
                .angle = gameplay_world_angle_to_screen_angle( position.angle, view )
            } );
        };

        points.reserve( samples.size() + 2u );

        const std::int64_t first_sample = samples.front().chart_sample;
        const std::int64_t last_sample = samples.back().chart_sample;

        if( current_chart_sample > last_sample || first_sample - current_chart_sample > lookahead_samples )
        {
            return points;
        }

        if( current_chart_sample >= first_sample )
        {
            append_position( current_position, 0 );
        }
        else
        {
            const TracePosition first_position{
                .x = samples.front().x,
                .y = samples.front().y,
                .angle = samples.front().angle
            };

            append_position( first_position, first_sample - current_chart_sample );
        }

        for( const TraceSample &sample : samples )
        {
            if( sample.chart_sample <= current_chart_sample ||
                ( sample.chart_sample <= first_sample && current_chart_sample < first_sample ) )
            {
                continue;
            }

            const std::int64_t delta_samples = sample.chart_sample - current_chart_sample;

            if( delta_samples > lookahead_samples )
            {
                break;
            }

            append_position( TracePosition{ .x = sample.x, .y = sample.y, .angle = sample.angle }, delta_samples );
        }

        const std::int64_t endpoint_sample = std::min( current_chart_sample + lookahead_samples, last_sample );
        const std::int64_t endpoint_delta = endpoint_sample - current_chart_sample;

        if( endpoint_sample >= first_sample && endpoint_delta >= 0 &&
            ( points.empty() || points.back().delta_samples < endpoint_delta ) )
        {
            append_position( trace_position_at( samples, endpoint_sample ), endpoint_delta );
        }

        return points;
    }

}
