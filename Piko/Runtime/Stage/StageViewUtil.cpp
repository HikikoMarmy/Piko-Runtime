#include "StageViewUtil.hpp"

#include "Rendering/RenderUtil.hpp"

#include <cstddef>
#include <algorithm>
#include <cmath>

namespace piko::runtime::stage::view
{
    float clamp_min( float value, float minimum )
    {
        return value < minimum ? minimum : value;
    }

    constexpr float trace_pi = 3.14159265358979323846f;

    float lerp_angle( float a, float b, float t )
    {
        const float delta = std::remainder( b - a, trace_pi * 2.0f );
        return a + delta * t;
    }

    float screen_angle_from_world_angle( float world_angle )
    {

        return -world_angle;
    }

    rendering::Vec2 normalize_delta( const rendering::Vec2 &from, const rendering::Vec2 &to )
    {
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float length = std::sqrt( dx * dx + dy * dy );

        if( length <= 0.0001f )
        {
            return rendering::Vec2{};
        }

        return rendering::Vec2{ dx / length, dy / length };
    }

    rendering::Vec2 quadratic_bezier( const rendering::Vec2 &a,
        const rendering::Vec2 &b,
        const rendering::Vec2 &c,
        float t )
    {
        const float u = 1.0f - t;

        return rendering::Vec2{
            u * u * a.x + 2.0f * u * t * b.x + t * t * c.x,
            u * u * a.y + 2.0f * u * t * b.y + t * t * c.y
        };
    }

    float bend_angle_degrees( const rendering::Vec2 &previous,
        const rendering::Vec2 &corner,
        const rendering::Vec2 &next )
    {
        const rendering::Vec2 incoming = normalize_delta( previous, corner );
        const rendering::Vec2 outgoing = normalize_delta( corner, next );

        const float dot = std::clamp( incoming.x * outgoing.x + incoming.y * outgoing.y, -1.0f, 1.0f );

        constexpr float radians_to_degrees = 57.29577951308232f;
        return std::acos( dot ) * radians_to_degrees;
    }

    TraceScreenPoint lerp_trace_screen_point( const TraceScreenPoint &a, const TraceScreenPoint &b, float t )
    {
        TraceScreenPoint out{};
        out.point = rendering::lerp_point( a.point, b.point, t );

        out.chart_sample = static_cast<std::int64_t>( std::lerp( static_cast<double>( a.chart_sample ),
            static_cast<double>( b.chart_sample ),
            static_cast<double>( t ) ) );

        out.angle = lerp_angle( a.angle, b.angle, t );
        return out;
    }

    TraceScreenPoint trace_point_towards( const TraceScreenPoint &from, const TraceScreenPoint &to, float distance )
    {
        const float total_distance = rendering::distance_between( from.point, to.point );
        const float t = total_distance <= 0.0001f ? 0.0f : std::clamp( distance / total_distance, 0.0f, 1.0f );

        return lerp_trace_screen_point( from, to, t );
    }

    std::size_t look_back_trace_by_distance( const std::vector<TraceScreenPoint> &points,
        std::size_t index,
        float distance_pixels,
        float &out_distance )
    {
        out_distance = 0.0f;

        if( points.empty() || index == 0 )
        {
            return index;
        }

        std::size_t result = index;

        while( result > 0 && out_distance < distance_pixels )
        {
            out_distance += rendering::distance_between( points[ result ].point, points[ result - 1 ].point );

            --result;
        }

        return result;
    }

    std::size_t look_forward_trace_by_distance( const std::vector<TraceScreenPoint> &points,
        std::size_t index,
        float distance_pixels,
        float &out_distance )
    {
        out_distance = 0.0f;

        if( points.empty() || index + 1 >= points.size() )
        {
            return index;
        }

        std::size_t result = index;

        while( result + 1 < points.size() && out_distance < distance_pixels )
        {
            out_distance += rendering::distance_between( points[ result ].point, points[ result + 1 ].point );

            ++result;
        }

        return result;
    }
}
