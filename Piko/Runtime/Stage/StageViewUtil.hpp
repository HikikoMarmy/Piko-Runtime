#pragma once

#include "Runtime/Stage/StageView.hpp"

#include <cstddef>
#include <vector>

namespace piko::runtime::stage::view
{
    [[nodiscard]] float clamp_min( float value, float minimum );
    [[nodiscard]] float lerp_angle( float a, float b, float t );
    [[nodiscard]] float screen_angle_from_world_angle( float world_angle );
    [[nodiscard]] rendering::Vec2 normalize_delta( const rendering::Vec2 &from, const rendering::Vec2 &to );
    [[nodiscard]] rendering::Vec2 quadratic_bezier( const rendering::Vec2 &a,
        const rendering::Vec2 &b,
        const rendering::Vec2 &c,
        float t );

    [[nodiscard]] float bend_angle_degrees( const rendering::Vec2 &previous,
        const rendering::Vec2 &corner,
        const rendering::Vec2 &next );

    [[nodiscard]] TraceScreenPoint lerp_trace_screen_point( const TraceScreenPoint &a,
        const TraceScreenPoint &b,
        float t );

    [[nodiscard]] TraceScreenPoint trace_point_towards( const TraceScreenPoint &from,
        const TraceScreenPoint &to,
        float distance );

    [[nodiscard]] std::size_t look_back_trace_by_distance( const std::vector<TraceScreenPoint> &points,
        std::size_t index,
        float distance_pixels,
        float &out_distance );

    [[nodiscard]] std::size_t look_forward_trace_by_distance( const std::vector<TraceScreenPoint> &points,
        std::size_t index,
        float distance_pixels,
        float &out_distance );
}
