#include "Stage3DUtil.hpp"

#include "Rendering/RenderUtil.hpp"
#include "Utils/Json.hpp"
#include "Utils/MathUtil.hpp"

#include <cstdint>
#include <glad/gl.h>

#include <algorithm>
#include <cmath>

namespace piko::modules::stage3d
{
    std::string gltf_source_model_name( const std::filesystem::path &path )
    {
        try
        {
            const utils::json::Value document = utils::json::load_file( path );
            const utils::json::Value *asset = document.find( "asset" );
            const utils::json::Value *extras = asset != nullptr ? asset->find( "extras" ) : nullptr;
            const utils::json::Value *piko = extras != nullptr ? extras->find( "piko" ) : nullptr;
            const utils::json::Value *source_model = piko != nullptr ? piko->find( "source_model" ) : nullptr;
            return source_model != nullptr && source_model->is_string() ? source_model->string() : std::string{};
        }
        catch( const std::exception & )
        {
            return {};
        }
    }

    constexpr float pi = 3.14159265358979323846f;
    constexpr float two_pi = pi * 2.0f;

    float normalize_angle_radians( float value )
    {
        if( !std::isfinite( value ) )
        {
            return 0.0f;
        }

        value = std::fmod( value + pi, two_pi );

        if( value < 0.0f )
        {
            value += two_pi;
        }

        return value - pi;
    }

    filetypes::xg::Vec3 transform_point( const filetypes::xg::Matrix &m, const filetypes::xg::Vec3 &p )
    {
        return filetypes::xg::Vec3{
            m.m[ 0 ] * p.x + m.m[ 4 ] * p.y + m.m[ 8 ] * p.z + m.m[ 12 ],
            m.m[ 1 ] * p.x + m.m[ 5 ] * p.y + m.m[ 9 ] * p.z + m.m[ 13 ],
            m.m[ 2 ] * p.x + m.m[ 6 ] * p.y + m.m[ 10 ] * p.z + m.m[ 14 ]
        };
    }

    filetypes::xg::Vec3 transform_vector( const filetypes::xg::Matrix &m, const filetypes::xg::Vec3 &v )
    {
        return filetypes::xg::normalize( filetypes::xg::Vec3{
            m.m[ 0 ] * v.x + m.m[ 4 ] * v.y + m.m[ 8 ] * v.z,
            m.m[ 1 ] * v.x + m.m[ 5 ] * v.y + m.m[ 9 ] * v.z,
            m.m[ 2 ] * v.x + m.m[ 6 ] * v.y + m.m[ 10 ] * v.z
        } );
    }

    filetypes::xg::Vec3 cross_vec3( const filetypes::xg::Vec3 &a, const filetypes::xg::Vec3 &b )
    {
        return filetypes::xg::Vec3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }

    filetypes::xg::Vec3 rotate_vector_by_quat( filetypes::xg::Quat q, const filetypes::xg::Vec3 &v )
    {
        const float q_len = std::sqrt( q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w );

        if( q_len > 0.000001f )
        {
            q.x /= q_len;

            q.y /= q_len;

            q.z /= q_len;

            q.w /= q_len;
        }
        else
        {
            q = filetypes::xg::Quat{ 0.0f, 0.0f, 0.0f, 1.0f };
        }

        const filetypes::xg::Vec3 qv{ q.x, q.y, q.z };
        const filetypes::xg::Vec3 t = cross_vec3( qv, v );
        const filetypes::xg::Vec3 doubled_t{ t.x * 2.0f, t.y * 2.0f, t.z * 2.0f };
        const filetypes::xg::Vec3 c = cross_vec3( qv, doubled_t );
        return filetypes::xg::normalize( filetypes::xg::Vec3{
            v.x + q.w * doubled_t.x + c.x,
            v.y + q.w * doubled_t.y + c.y,
            v.z + q.w * doubled_t.z + c.z
        } );
    }

    filetypes::xg::Matrix transpose_matrix( const filetypes::xg::Matrix &m )
    {
        filetypes::xg::Matrix result{};

        for( std::int32_t row = 0; row < 4; ++row )
        {
            for( std::int32_t col = 0; col < 4; ++col )
            {
                result.m[ col * 4 + row ] = m.m[ row * 4 + col ];
            }
        }

        return result;
    }

    float determinant_3x3( const filetypes::xg::Matrix &m )
    {
        const float a00 = m.m[ 0 ];
        const float a01 = m.m[ 4 ];
        const float a02 = m.m[ 8 ];
        const float a10 = m.m[ 1 ];
        const float a11 = m.m[ 5 ];
        const float a12 = m.m[ 9 ];
        const float a20 = m.m[ 2 ];
        const float a21 = m.m[ 6 ];
        const float a22 = m.m[ 10 ];
        return a00 * ( a11 * a22 - a12 * a21 ) - a01 * ( a10 * a22 - a12 * a20 ) + a02 * ( a10 * a21 - a11 * a20 );
    }

    std::uint32_t cull_func_after_winding_transform( std::uint32_t cull_func,
        const filetypes::xg::Matrix &winding_matrix )
    {
        if( cull_func != 1u && cull_func != 2u )
        {
            return cull_func;
        }

        const float det = determinant_3x3( winding_matrix );

        if( std::isfinite( det ) && det < 0.0f )
        {
            return cull_func == 1u ? 2u : 1u;
        }

        return cull_func;
    }

    void set_xg_cull_state( std::uint32_t cull_func )
    {

        if( cull_func == 1u )
        {
            glEnable( GL_CULL_FACE );
            glCullFace( GL_BACK );
            glFrontFace( GL_CCW );
        }
        else if( cull_func == 2u )
        {
            glEnable( GL_CULL_FACE );
            glCullFace( GL_BACK );
            glFrontFace( GL_CW );
        }
        else
        {
            glDisable( GL_CULL_FACE );
            glFrontFace( GL_CCW );
        }
    }

    std::array<float, 9> matrix3_from_matrix4( const filetypes::xg::Matrix &m )
    {
        return { m.m[ 0 ], m.m[ 1 ], m.m[ 2 ], m.m[ 4 ], m.m[ 5 ], m.m[ 6 ], m.m[ 8 ], m.m[ 9 ], m.m[ 10 ] };
    }

    std::array<float, 9> normal_matrix3_from_model( const filetypes::xg::Matrix &matrix )
    {
        return matrix3_from_matrix4( transpose_matrix( filetypes::xg::inverse_affine_matrix( matrix ) ) );
    }

    filetypes::xg::Vec3 transform_normal( const filetypes::xg::Matrix &matrix, const filetypes::xg::Vec3 &normal )
    {
        return transform_vector( transpose_matrix( filetypes::xg::inverse_affine_matrix( matrix ) ), normal );
    }

    float key_lerp_t( float frame, float current_frame, float next_frame, float reciprocal )
    {
        if( next_frame <= current_frame + 0.0001f )
        {
            return 0.0f;
        }

        if( std::isfinite( reciprocal ) && std::abs( reciprocal ) > 0.000001f )
        {
            return piko::utils::clamp01( ( frame - current_frame ) * reciprocal );
        }

        return piko::utils::clamp01( ( frame - current_frame ) / ( next_frame - current_frame ) );
    }

    filetypes::xg::Vertex lerp_vertex( const filetypes::xg::Vertex &a, const filetypes::xg::Vertex &b, float t )
    {
        filetypes::xg::Vertex out = a;
        out.position = filetypes::xg::lerp( a.position, b.position, t );

        out.normal = filetypes::xg::normalize( filetypes::xg::lerp( a.normal, b.normal, t ) );

        out.color = rendering::lerp_color( a.color, b.color, t );

        out.uv = rendering::lerp_point( a.uv, b.uv, t );

        out.has_uv = a.has_uv || b.has_uv;
        return out;
    }

    filetypes::xg::Quat identity_quat()
    {
        return filetypes::xg::Quat{ 0.0f, 0.0f, 0.0f, 1.0f };
    }

    filetypes::xg::Quat axis_angle_quat( float x, float y, float z, float radians )
    {
        const float half = radians * 0.5f;
        const float s = std::sin( half );
        return filetypes::xg::Quat{ x * s, y * s, z * s, std::cos( half ) };
    }

    filetypes::xg::Quat lerp_quat_shortest( filetypes::xg::Quat a, filetypes::xg::Quat b, float t )
    {

        a = filetypes::xg::normalize_quaternion( a );

        b = filetypes::xg::normalize_quaternion( b );

        const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

        if( dot < 0.0f )
        {
            b.x = -b.x;

            b.y = -b.y;

            b.z = -b.z;

            b.w = -b.w;
        }

        return filetypes::xg::normalize_quaternion( filetypes::xg::Quat{
            a.x + ( b.x - a.x ) * t,
            a.y + ( b.y - a.y ) * t,
            a.z + ( b.z - a.z ) * t,
            a.w + ( b.w - a.w ) * t
        } );
    }

    filetypes::xg::Quat slerp_quat_shortest( filetypes::xg::Quat a, filetypes::xg::Quat b, float t )
    {
        a = filetypes::xg::normalize_quaternion( a );

        b = filetypes::xg::normalize_quaternion( b );
        float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

        if( dot < 0.0f )
        {
            dot = -dot;

            b.x = -b.x;

            b.y = -b.y;

            b.z = -b.z;

            b.w = -b.w;
        }

        dot = std::clamp( dot, -1.0f, 1.0f );

        if( dot > 0.9995f )
        {
            return lerp_quat_shortest( a, b, t );
        }

        const float theta = std::acos( dot );
        const float sin_theta = std::sin( theta );

        if( std::abs( sin_theta ) <= 0.000001f )
        {
            return a;
        }

        const float wa = std::sin( ( 1.0f - t ) * theta ) / sin_theta;
        const float wb = std::sin( t * theta ) / sin_theta;
        return filetypes::xg::normalize_quaternion(
            filetypes::xg::Quat{ a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb } );
    }

    filetypes::xg::Quat conjugate_quat( filetypes::xg::Quat q )
    {
        q.x = -q.x;

        q.y = -q.y;

        q.z = -q.z;
        return q;
    }

    filetypes::xg::Vec3 sample_vec3_keys( const std::vector<filetypes::ssq::Vec3Key> &keys,
        const filetypes::xg::Vec3 &fallback,
        float frame )
    {
        if( keys.empty() )
        {
            return fallback;
        }

        if( keys.size() == 1 || frame <= keys.front().frame )
        {
            return keys.front().value;
        }

        for( std::size_t i = 0; i + 1 < keys.size(); ++i )
        {
            const auto &a = keys[ i ];

            const auto &b = keys[ i + 1 ];

            if( frame < b.frame )
            {
                if( !a.interpolate )
                {
                    return a.value;
                }

                return filetypes::xg::lerp( a.value,
                    b.value,
                    key_lerp_t( frame, a.frame, b.frame, a.reciprocal_frames_to_next ) );
            }
        }

        return keys.back().value;
    }

    rendering::Color sample_color_keys( const std::vector<filetypes::ssq::ColorKey> &keys,
        const rendering::Color &fallback,
        float frame )
    {
        if( keys.empty() )
        {
            return fallback;
        }

        if( keys.size() == 1 || frame <= keys.front().frame )
        {
            return keys.front().value;
        }

        for( std::size_t i = 0; i + 1 < keys.size(); ++i )
        {
            const auto &a = keys[ i ];

            const auto &b = keys[ i + 1 ];

            if( frame < b.frame )
            {
                if( !a.interpolate )
                {
                    return a.value;
                }

                return rendering::lerp_color( a.value,
                    b.value,
                    key_lerp_t( frame, a.frame, b.frame, a.reciprocal_frames_to_next ) );
            }
        }

        return keys.back().value;
    }

    filetypes::ssq::LightColorKey sample_light_color_keys( const std::vector<filetypes::ssq::LightColorKey> &keys,
        const rendering::Color &primary_fallback,
        const rendering::Color &secondary_fallback,
        float frame )
    {
        filetypes::ssq::LightColorKey fallback{};
        fallback.primary = primary_fallback;

        fallback.secondary = secondary_fallback;

        if( keys.empty() )
        {
            return fallback;
        }

        // Retail treats the last key frame as the track period/sentinel.
        if( keys.size() >= 2 && keys.back().frame > 0.0f && std::isfinite( keys.back().frame ) )
        {
            frame = std::fmod( std::max( 0.0f, frame ), keys.back().frame );

            if( frame < 0.0f )
            {
                frame += keys.back().frame;
            }
        }

        if( keys.size() == 1 || frame <= keys.front().frame )
        {
            return keys.front();
        }

        for( std::size_t i = 0; i + 1 < keys.size(); ++i )
        {
            const auto &a = keys[ i ];

            const auto &b = keys[ i + 1 ];

            if( frame < b.frame )
            {
                if( !a.interpolate )
                {
                    return a;
                }

                filetypes::ssq::LightColorKey out = a;
                const float t = key_lerp_t( frame, a.frame, b.frame, a.reciprocal_frames_to_next );
                out.primary = rendering::lerp_color( a.primary, b.primary, t );

                out.secondary = rendering::lerp_color( a.secondary, b.secondary, t );
                return out;
            }
        }

        return keys.back();
    }

    filetypes::xg::Quat light_rotation_key_value( const filetypes::ssq::LightRotationKey &key, bool raw )
    {
        return raw ? key.raw_value : key.value;
    }

    filetypes::xg::Quat sample_light_rotation_keys( const std::vector<filetypes::ssq::LightRotationKey> &keys,
        const filetypes::xg::Quat &fallback,
        float frame,
        bool raw )
    {
        if( keys.empty() )
        {
            return fallback;
        }

        if( keys.size() >= 2 && keys.back().frame > 0.0f && std::isfinite( keys.back().frame ) )
        {
            frame = std::fmod( std::max( 0.0f, frame ), keys.back().frame );

            if( frame < 0.0f )
            {
                frame += keys.back().frame;
            }
        }

        if( keys.size() == 1 || frame <= keys.front().frame )
        {
            return light_rotation_key_value( keys.front(), raw );
        }

        for( std::size_t i = 0; i + 1 < keys.size(); ++i )
        {
            const auto &a = keys[ i ];

            const auto &b = keys[ i + 1 ];

            if( frame < b.frame )
            {
                if( !a.interpolate )
                {
                    return light_rotation_key_value( a, raw );
                }

                return slerp_quat_shortest( light_rotation_key_value( a, raw ),
                    light_rotation_key_value( b, raw ),
                    key_lerp_t( frame, a.frame, b.frame, a.reciprocal_frames_to_next ) );
            }
        }

        return light_rotation_key_value( keys.back(), raw );
    }

    filetypes::xg::Quat quat_key_value( const filetypes::ssq::QuatKey &key, bool raw )
    {
        return raw ? key.raw_value : key.value;
    }

    filetypes::xg::Quat sample_quat_keys( const std::vector<filetypes::ssq::QuatKey> &keys,
        const filetypes::xg::Quat &fallback,
        float frame,
        bool raw )
    {
        if( keys.empty() )
        {
            return fallback;
        }

        if( keys.size() == 1 || frame <= keys.front().frame )
        {
            return quat_key_value( keys.front(), raw );
        }

        for( std::size_t i = 0; i + 1 < keys.size(); ++i )
        {
            const auto &a = keys[ i ];

            const auto &b = keys[ i + 1 ];

            if( frame < b.frame )
            {
                if( !a.interpolate )
                {
                    return quat_key_value( a, raw );
                }

                return lerp_quat_shortest( quat_key_value( a, raw ),
                    quat_key_value( b, raw ),
                    key_lerp_t( frame, a.frame, b.frame, a.reciprocal_frames_to_next ) );
            }
        }

        return quat_key_value( keys.back(), raw );
    }

    filetypes::xg::Vec3 sample_scale_keys( const std::vector<filetypes::ssq::ScaleKey> &keys,
        const filetypes::xg::Vec3 &fallback,
        float frame )
    {
        if( keys.empty() )
        {
            return fallback;
        }

        if( keys.size() == 1 || frame <= keys.front().frame )
        {
            return keys.front().value;
        }

        for( std::size_t i = 0; i + 1 < keys.size(); ++i )
        {
            const auto &a = keys[ i ];

            const auto &b = keys[ i + 1 ];

            if( frame < b.frame )
            {
                if( !a.interpolate )
                {
                    return a.value;
                }

                return filetypes::xg::lerp( a.value,
                    b.value,
                    key_lerp_t( frame, a.frame, b.frame, a.reciprocal_frames_to_next ) );
            }
        }

        return keys.back().value;
    }

    std::size_t interpolator_key_index( std::size_t key_count, const std::vector<float> &times, float frame, float *t )
    {
        if( t != nullptr )
        {
            *t = 0.0f;
        }

        if( key_count <= 1u )
        {
            return 0u;
        }

        if( times.size() == key_count )
        {
            if( frame <= times.front() )
            {
                return 0u;
            }

            for( std::size_t i = 0; i + 1u < times.size(); ++i )
            {
                if( frame < times[ i + 1u ] )
                {
                    if( t != nullptr )
                    {
                        *t = key_lerp_t( frame, times[ i ], times[ i + 1u ], 0.0f );
                    }

                    return i;
                }
            }

            return key_count - 1u;
        }

        const float clamped = std::clamp( frame, 0.0f, static_cast<float>( key_count - 1u ) );
        const std::size_t index =
            std::min<std::size_t>( static_cast<std::size_t>( std::floor( clamped ) ), key_count - 1u );

        if( t != nullptr && index + 1u < key_count )
        {
            *t = clamped - static_cast<float>( index );
        }

        return index;
    }

    filetypes::xg::Vec3 sample_xg_vec3_interpolator( const filetypes::xg::Vec3Interpolator &interpolator, float frame )
    {
        if( interpolator.keys.empty() )
        {
            return filetypes::xg::Vec3{};
        }

        float t = 0.0f;
        const std::size_t index = interpolator_key_index( interpolator.keys.size(), interpolator.times, frame, &t );

        if( interpolator.type == 0 || index + 1u >= interpolator.keys.size() )
        {
            return interpolator.keys[ index ];
        }

        return filetypes::xg::lerp( interpolator.keys[ index ], interpolator.keys[ index + 1u ], t );
    }

    filetypes::xg::Quat sample_xg_quat_interpolator( const filetypes::xg::QuatInterpolator &interpolator, float frame )
    {
        if( interpolator.keys.empty() )
        {
            return identity_quat();
        }

        float t = 0.0f;
        const std::size_t index = interpolator_key_index( interpolator.keys.size(), interpolator.times, frame, &t );

        if( interpolator.type == 0 || index + 1u >= interpolator.keys.size() )
        {
            return filetypes::xg::normalize_quaternion( interpolator.keys[ index ] );
        }

        return lerp_quat_shortest( interpolator.keys[ index ], interpolator.keys[ index + 1u ], t );
    }

    std::vector<filetypes::xg::Vertex> sample_shape_interpolator( const filetypes::xg::ShapeInterpolator &interpolator,
        float frame )
    {
        if( interpolator.keys.empty() )
        {
            return {};
        }

        float t = 0.0f;
        const std::size_t index = interpolator_key_index( interpolator.keys.size(), interpolator.times, frame, &t );

        if( interpolator.type == 0 || index + 1u >= interpolator.keys.size() )
        {
            return interpolator.keys[ index ];
        }

        const auto &a = interpolator.keys[ index ];

        const auto &b = interpolator.keys[ index + 1u ];
        const std::size_t count = std::min( a.size(), b.size() );
        std::vector<filetypes::xg::Vertex> result;
        result.reserve( count );

        for( std::size_t i = 0; i < count; ++i )
        {
            result.push_back( lerp_vertex( a[ i ], b[ i ], t ) );
        }

        return result;
    }

    std::vector<filetypes::xg::Vec3> sample_vertex_interpolator( const filetypes::xg::VertexInterpolator &interpolator,
        float frame )
    {
        if( interpolator.keys.empty() )
        {
            return {};
        }

        float t = 0.0f;
        const std::size_t index = interpolator_key_index( interpolator.keys.size(), interpolator.times, frame, &t );

        if( interpolator.type == 0 || index + 1u >= interpolator.keys.size() )
        {
            return interpolator.keys[ index ];
        }

        const auto &a = interpolator.keys[ index ];

        const auto &b = interpolator.keys[ index + 1u ];
        const std::size_t count = std::min( a.size(), b.size() );
        std::vector<filetypes::xg::Vec3> result;
        result.reserve( count );

        for( std::size_t i = 0; i < count; ++i )
        {
            result.push_back( filetypes::xg::lerp( a[ i ], b[ i ], t ) );
        }

        return result;
    }

    std::vector<filetypes::xg::Vec3> sample_normal_interpolator( const filetypes::xg::NormalInterpolator &interpolator,
        float frame )
    {
        if( interpolator.keys.empty() )
        {
            return {};
        }

        float t = 0.0f;
        const std::size_t index = interpolator_key_index( interpolator.keys.size(), interpolator.times, frame, &t );

        if( interpolator.type == 0 || index + 1u >= interpolator.keys.size() )
        {
            return interpolator.keys[ index ];
        }

        const auto &a = interpolator.keys[ index ];

        const auto &b = interpolator.keys[ index + 1u ];
        const std::size_t count = std::min( a.size(), b.size() );
        std::vector<filetypes::xg::Vec3> result;
        result.reserve( count );

        for( std::size_t i = 0; i < count; ++i )
        {
            result.push_back( filetypes::xg::normalize( filetypes::xg::lerp( a[ i ], b[ i ], t ) ) );
        }

        return result;
    }

    std::vector<rendering::Vec2> sample_texcoord_interpolator( const filetypes::xg::TexCoordInterpolator &interpolator,
        float frame )
    {
        if( interpolator.keys.empty() )
        {
            return {};
        }

        float t = 0.0f;
        const std::size_t index = interpolator_key_index( interpolator.keys.size(), interpolator.times, frame, &t );

        if( interpolator.type == 0 || index + 1u >= interpolator.keys.size() )
        {
            return interpolator.keys[ index ];
        }

        const auto &a = interpolator.keys[ index ];

        const auto &b = interpolator.keys[ index + 1u ];
        const std::size_t count = std::min( a.size(), b.size() );
        std::vector<rendering::Vec2> result;
        result.reserve( count );

        for( std::size_t i = 0; i < count; ++i )
        {
            result.push_back( rendering::lerp_point( a[ i ], b[ i ], t ) );
        }

        return result;
    }

    void sample_projection_keys( const std::vector<filetypes::ssq::ProjectionKey> &keys,
        float frame,
        float &fov_degrees,
        float &aspect,
        float &z_near,
        float &z_far )
    {
        if( keys.empty() )
        {
            return;
        }

        const auto assign = [ & ]( const filetypes::ssq::ProjectionKey &key )
        {
            fov_degrees = key.fov_degrees;

            aspect = key.aspect;

            z_near = key.z_near;

            z_far = key.z_far;
        };

        if( keys.size() == 1 || frame <= keys.front().frame )
        {
            assign( keys.front() );
            return;
        }

        for( std::size_t i = 0; i + 1 < keys.size(); ++i )
        {
            const auto &a = keys[ i ];

            const auto &b = keys[ i + 1 ];

            if( frame < b.frame )
            {
                if( !a.interpolate )
                {
                    assign( a );
                    return;
                }

                const float t = key_lerp_t( frame, a.frame, b.frame, a.reciprocal_frames_to_next );
                fov_degrees = a.fov_degrees + ( b.fov_degrees - a.fov_degrees ) * t;

                aspect = a.aspect + ( b.aspect - a.aspect ) * t;

                z_near = a.z_near + ( b.z_near - a.z_near ) * t;

                z_far = a.z_far + ( b.z_far - a.z_far ) * t;
                return;
            }
        }

        assign( keys.back() );
    }

    bool material_alpha_is_active( std::uint32_t blend_type, std::uint32_t flags )
    {
        return blend_type == 1u || blend_type == 5u ||
               ( ( blend_type == 0u || blend_type == 4u ) && ( flags & 0x01u ) != 0 );
    }

    float material_alpha_cutoff( std::uint32_t blend_type, std::uint32_t flags )
    {

        if( blend_type == 1u )
        {
            return 0.035f;
        }

        if( blend_type == 5u )
        {
            return 0.030f;
        }

        if( ( blend_type == 0u || blend_type == 4u ) && ( flags & 0x01u ) != 0 )
        {
            return 0.045f;
        }

        return 0.010f;
    }

    bool material_should_use_blend_pass( const filetypes::xg::Material &material )
    {
        if( material_alpha_is_active( material.blend_type, material.flags ) )
        {
            return true;
        }

        return material.blend_type == 2u || material.blend_type == 3u || material.blend_type == 4u;
    }

    bool texcoord_interpolator_needs_repeat( const filetypes::xg::TexCoordInterpolator &interpolator )
    {
        for( const auto &key : interpolator.keys )
        {
            for( const rendering::Vec2 &uv : key )
            {
                if( rendering::uv_requires_texture_repeat( uv.x, uv.y ) )
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool material_should_wrap_texture( const filetypes::xg::Material &material )
    {

        if( material.texture_env != 0u )
        {
            return false;
        }

        if( material.u_tile != 0u || material.v_tile != 0u )
        {
            return true;
        }

        return !material_should_use_blend_pass( material );
    }

    filetypes::xg::Matrix transpose_rotation_inverse_rigid( const filetypes::xg::Matrix &world )
    {
        filetypes::xg::Matrix view = filetypes::xg::identity_matrix();

        view.m[ 0 ] = world.m[ 0 ];

        view.m[ 1 ] = world.m[ 4 ];

        view.m[ 2 ] = world.m[ 8 ];

        view.m[ 4 ] = world.m[ 1 ];

        view.m[ 5 ] = world.m[ 5 ];

        view.m[ 6 ] = world.m[ 9 ];

        view.m[ 8 ] = world.m[ 2 ];

        view.m[ 9 ] = world.m[ 6 ];

        view.m[ 10 ] = world.m[ 10 ];

        const float tx = world.m[ 12 ];
        const float ty = world.m[ 13 ];
        const float tz = world.m[ 14 ];

        view.m[ 12 ] = -( view.m[ 0 ] * tx + view.m[ 4 ] * ty + view.m[ 8 ] * tz );

        view.m[ 13 ] = -( view.m[ 1 ] * tx + view.m[ 5 ] * ty + view.m[ 9 ] * tz );

        view.m[ 14 ] = -( view.m[ 2 ] * tx + view.m[ 6 ] * ty + view.m[ 10 ] * tz );
        return view;
    }
}
