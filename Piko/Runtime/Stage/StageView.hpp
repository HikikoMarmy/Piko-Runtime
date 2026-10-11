#pragma once

#include "Rendering/RenderTypes.hpp"
#include "Runtime/Stage/StageTraceBuilder.hpp"

#include <cstdint>
#include <cstddef>
#include <vector>

namespace piko::runtime::stage
{
    struct TraceBounds
    {
        float min_x = 0.0f;
        float max_x = 0.0f;
        float min_y = 0.0f;
        float max_y = 0.0f;
        bool valid = false;
    };

    struct TraceScreenPoint
    {
        rendering::Vec2 point{};
        std::int64_t chart_sample = 0;

        float angle = 0.0f;
    };

    struct TraceRenderPoint
    {
        rendering::Vec2 point{};
        std::int64_t delta_samples = 0;
        float alpha = 1.0f;

        float angle = 0.0f;
    };

    struct StageView
    {
        rendering::Rect area{};
        TraceBounds bounds{};
        float scale = 1.0f;
        float center_x = 0.0f;
        float center_y = 0.0f;
        float trace_center_x = 0.0f;
        float trace_center_y = 0.0f;

        [[nodiscard]] rendering::Vec2 trace_to_screen( float x, float y ) const;
        [[nodiscard]] rendering::Vec2 trace_to_screen( const TraceSample &sample ) const;
    };

    struct StageGameplayView
    {
        float center_x = 640.0f;
        float center_y = 360.0f;
        float zoom = 1.55f;
        float note_speed = 1.00f;

        float scale_x = 1.0f;
        float scale_y = 1.0f;

        float presentation_center_x = 640.0f;
        float presentation_center_y = 360.0f;
        float presentation_scale_x = 1.0f;
        float presentation_scale_y = 1.0f;
    };

    [[nodiscard]] rendering::Vec2 apply_gameplay_presentation_stretch( const rendering::Vec2 &point,
        const StageGameplayView &view );

    [[nodiscard]] TraceBounds calculate_trace_bounds( const std::vector<TraceSample> &samples );

    [[nodiscard]] StageView fit_trace_to_area( const std::vector<TraceSample> &samples,
        const rendering::Rect &area,
        float scale_multiplier = 0.88f );

    [[nodiscard]] std::vector<TraceScreenPoint> make_trace_screen_points( const std::vector<TraceSample> &samples,
        const StageView &view,
        std::size_t max_points = 1600 );

    [[nodiscard]] std::vector<TraceScreenPoint> round_hard_trace_corners_windowed(
        const std::vector<TraceScreenPoint> &points,
        float radius_pixels,
        std::int32_t steps,
        float min_bend_degrees );

    [[nodiscard]] TracePosition visual_trace_position_at( const std::vector<TraceSample> &samples,
        std::int64_t chart_sample,
        bool use_prestart_extrapolation = true );

    [[nodiscard]] rendering::Vec2 gameplay_world_to_screen( float world_x,
        float world_y,
        const TracePosition &current_position,
        const StageGameplayView &view );

    [[nodiscard]] float gameplay_world_angle_to_screen_angle( float world_angle, const StageGameplayView &view );

    [[nodiscard]] std::vector<TraceRenderPoint> make_gameplay_trace_render_points(
        const std::vector<TraceSample> &samples,
        const TracePosition &current_position,
        std::int64_t current_chart_sample,
        const StageGameplayView &view,
        std::int64_t lookahead_samples,
        std::int64_t fade_start_samples,
        float min_alpha,
        float max_alpha );
}
