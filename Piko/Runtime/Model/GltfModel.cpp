#include "Runtime/Model/GltfModel.hpp"

#include "ThirdParty/TinyGLTFConfig.hpp"
#include "Utils/MathUtil.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace piko::runtime::model::gltf_model
{
    struct NodePose
    {
        filetypes::xg::Vec3 translation{};
        filetypes::xg::Quat rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
        filetypes::xg::Vec3 scale{ 1.0f, 1.0f, 1.0f };
        filetypes::xg::Matrix matrix{};
        bool use_matrix = false;
    };

    filetypes::xg::Quat lerp_quaternion_shortest( filetypes::xg::Quat a, filetypes::xg::Quat b, float weight )
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

        weight = piko::utils::clamp01( weight );
        return filetypes::xg::normalize_quaternion( filetypes::xg::Quat{
            a.x + ( b.x - a.x ) * weight,
            a.y + ( b.y - a.y ) * weight,
            a.z + ( b.z - a.z ) * weight,
            a.w + ( b.w - a.w ) * weight
        } );
    }

    filetypes::xg::Vec3 lerp_vec3( const filetypes::xg::Vec3 &a, const filetypes::xg::Vec3 &b, float weight )
    {
        weight = piko::utils::clamp01( weight );
        return filetypes::xg::Vec3{
            a.x + ( b.x - a.x ) * weight,
            a.y + ( b.y - a.y ) * weight,
            a.z + ( b.z - a.z ) * weight
        };
    }

    std::size_t component_count( std::int32_t type )
    {
#if PIKO_HAS_TINYGLTF
        switch( type )
        {
            case TINYGLTF_TYPE_SCALAR:
                return 1u;
            case TINYGLTF_TYPE_VEC2:
                return 2u;
            case TINYGLTF_TYPE_VEC3:
                return 3u;
            case TINYGLTF_TYPE_VEC4:
                return 4u;
            case TINYGLTF_TYPE_MAT4:
                return 16u;
            default:
                break;
        }
#else
        (void)type;
#endif
        return 0u;
    }

    std::size_t component_size( std::int32_t component_type )
    {
        switch( component_type )
        {
            case 5120:
            case 5121:
                return 1u;
            case 5122:
            case 5123:
                return 2u;
            case 5125:
            case 5126:
                return 4u;
            default:
                return 0u;
        }
    }

#if PIKO_HAS_TINYGLTF
    const std::uint8_t *accessor_data( const tinygltf::Model &model,
        const tinygltf::Accessor &accessor,
        std::size_t element_index,
        std::size_t *stride_out )
    {
        if( accessor.bufferView < 0 || static_cast<std::size_t>( accessor.bufferView ) >= model.bufferViews.size() )
        {
            throw std::runtime_error( "glTF accessor has no valid buffer view" );
        }

        const tinygltf::BufferView &view = model.bufferViews[ static_cast<std::size_t>( accessor.bufferView ) ];

        if( view.buffer < 0 || static_cast<std::size_t>( view.buffer ) >= model.buffers.size() )
        {
            throw std::runtime_error( "glTF buffer view has no valid buffer" );
        }

        const tinygltf::Buffer &buffer = model.buffers[ static_cast<std::size_t>( view.buffer ) ];
        const std::size_t components = component_count( accessor.type );
        const std::size_t bytes_per_component = component_size( accessor.componentType );

        if( components == 0u || bytes_per_component == 0u )
        {
            throw std::runtime_error( "glTF accessor uses an unsupported type" );
        }

        const std::size_t packed = components * bytes_per_component;
        const std::size_t stride = view.byteStride == 0u ? packed : static_cast<std::size_t>( view.byteStride );
        const std::size_t offset = static_cast<std::size_t>( view.byteOffset ) +
                                   static_cast<std::size_t>( accessor.byteOffset ) + element_index * stride;

        if( offset > buffer.data.size() || packed > buffer.data.size() - offset )
        {
            throw std::runtime_error( "glTF accessor extends beyond its buffer" );
        }

        if( stride_out != nullptr )
        {
            *stride_out = stride;
        }

        return buffer.data.data() + offset;
    }

    double read_component( const std::uint8_t *data, std::int32_t component_type, bool normalized )
    {
        switch( component_type )
        {
            case 5120:
            {
                std::int8_t value = 0;
                std::memcpy( &value, data, sizeof( value ) );
                return normalized ? std::max( -1.0, static_cast<double>( value ) / 127.0 ) : value;
            }
            case 5121:
            {
                std::uint8_t value = 0;
                std::memcpy( &value, data, sizeof( value ) );
                return normalized ? static_cast<double>( value ) / 255.0 : value;
            }
            case 5122:
            {
                std::int16_t value = 0;
                std::memcpy( &value, data, sizeof( value ) );
                return normalized ? std::max( -1.0, static_cast<double>( value ) / 32767.0 ) : value;
            }
            case 5123:
            {
                std::uint16_t value = 0;
                std::memcpy( &value, data, sizeof( value ) );
                return normalized ? static_cast<double>( value ) / 65535.0 : value;
            }
            case 5125:
            {
                std::uint32_t value = 0;
                std::memcpy( &value, data, sizeof( value ) );
                return value;
            }
            case 5126:
            {
                float value = 0.0f;
                std::memcpy( &value, data, sizeof( value ) );
                return value;
            }
            default:
                throw std::runtime_error( "glTF accessor uses an unsupported component type" );
        }
    }

    std::vector<float> read_float_accessor( const tinygltf::Model &model, std::int32_t accessor_index )
    {
        if( accessor_index < 0 || static_cast<std::size_t>( accessor_index ) >= model.accessors.size() )
        {
            throw std::runtime_error( "glTF references an invalid accessor" );
        }

        const tinygltf::Accessor &accessor = model.accessors[ static_cast<std::size_t>( accessor_index ) ];
        const std::size_t components = component_count( accessor.type );
        const std::size_t bytes_per_component = component_size( accessor.componentType );
        std::vector<float> output;
        output.reserve( static_cast<std::size_t>( accessor.count ) * components );

        for( std::size_t element = 0; element < static_cast<std::size_t>( accessor.count ); ++element )
        {
            const std::uint8_t *data = accessor_data( model, accessor, element, nullptr );

            for( std::size_t component = 0; component < components; ++component )
            {
                output.push_back( static_cast<float>( read_component( data + component * bytes_per_component,
                    accessor.componentType,
                    accessor.normalized ) ) );
            }
        }

        return output;
    }

    std::vector<std::uint32_t> read_unsigned_accessor( const tinygltf::Model &model, std::int32_t accessor_index )
    {
        if( accessor_index < 0 || static_cast<std::size_t>( accessor_index ) >= model.accessors.size() )
        {
            throw std::runtime_error( "glTF references an invalid accessor" );
        }

        const tinygltf::Accessor &accessor = model.accessors[ static_cast<std::size_t>( accessor_index ) ];
        const std::size_t components = component_count( accessor.type );
        const std::size_t bytes_per_component = component_size( accessor.componentType );
        std::vector<std::uint32_t> output;
        output.reserve( static_cast<std::size_t>( accessor.count ) * components );

        for( std::size_t element = 0; element < static_cast<std::size_t>( accessor.count ); ++element )
        {
            const std::uint8_t *data = accessor_data( model, accessor, element, nullptr );

            for( std::size_t component = 0; component < components; ++component )
            {
                const double value =
                    read_component( data + component * bytes_per_component, accessor.componentType, false );

                output.push_back( static_cast<std::uint32_t>( std::max( 0.0, value ) ) );
            }
        }

        return output;
    }

    const tinygltf::Value *value_member( const tinygltf::Value *object, std::string_view key )
    {
        if( object == nullptr || !object->IsObject() )
        {
            return nullptr;
        }

        const std::string name( key );

        if( !object->Has( name ) )
        {
            return nullptr;
        }

        return &object->Get( name );
    }

    const tinygltf::Value *piko_object( const tinygltf::Value &extras )
    {
        const tinygltf::Value *piko = value_member( &extras, "piko" );
        return piko != nullptr && piko->IsObject() ? piko : nullptr;
    }

    std::string value_string( const tinygltf::Value *object, std::string_view key, std::string fallback = {} )
    {
        const tinygltf::Value *value = value_member( object, key );
        return value != nullptr && value->IsString() ? value->Get<std::string>() : std::move( fallback );
    }

    double value_number( const tinygltf::Value *object, std::string_view key, double fallback )
    {
        const tinygltf::Value *value = value_member( object, key );
        return value != nullptr && value->IsNumber() ? value->GetNumberAsDouble() : fallback;
    }

    std::int32_t value_int( const tinygltf::Value *object, std::string_view key, std::int32_t fallback )
    {
        const tinygltf::Value *value = value_member( object, key );
        return value != nullptr && value->IsNumber() ? value->GetNumberAsInt() : fallback;
    }

    std::uint32_t value_u32( const tinygltf::Value *object, std::string_view key, std::uint32_t fallback )
    {
        return static_cast<std::uint32_t>(
            std::max( 0, value_int( object, key, static_cast<std::int32_t>( fallback ) ) ) );
    }

    bool value_bool( const tinygltf::Value *object, std::string_view key, bool fallback )
    {
        const tinygltf::Value *value = value_member( object, key );
        return value != nullptr && value->IsBool() ? value->Get<bool>() : fallback;
    }

    std::vector<float> value_float_array( const tinygltf::Value *value )
    {
        std::vector<float> output;

        if( value == nullptr || !value->IsArray() )
        {
            return output;
        }

        output.reserve( value->ArrayLen() );

        for( std::size_t index = 0; index < value->ArrayLen(); ++index )
        {
            const tinygltf::Value &entry = value->Get( index );
            output.push_back( entry.IsNumber() ? static_cast<float>( entry.GetNumberAsDouble() ) : 0.0f );
        }

        return output;
    }

    std::vector<std::int32_t> value_int_array( const tinygltf::Value *value )
    {
        std::vector<std::int32_t> output;

        if( value == nullptr || !value->IsArray() )
        {
            return output;
        }

        output.reserve( value->ArrayLen() );

        for( std::size_t index = 0; index < value->ArrayLen(); ++index )
        {
            const tinygltf::Value &entry = value->Get( index );
            output.push_back( entry.IsNumber() ? entry.GetNumberAsInt() : -1 );
        }

        return output;
    }

    void load_vec3_track( const tinygltf::Model &model,
        const tinygltf::Value *track_value,
        const std::vector<std::uint32_t> &indices,
        GltfPrimitive::Vec3Track &track )
    {
        if( track_value == nullptr || !track_value->IsObject() )
        {
            return;
        }

        track.interpolation_type = value_u32( track_value, "interpolation_type", 0u );

        track.times = value_float_array( value_member( track_value, "times" ) );
        const std::vector<std::int32_t> accessors = value_int_array( value_member( track_value, "accessors" ) );
        const std::size_t frame_count = std::min( track.times.size(), accessors.size() );
        track.times.resize( frame_count );
        track.frames.reserve( frame_count );

        for( std::size_t frame_index = 0; frame_index < frame_count; ++frame_index )
        {
            const std::vector<float> values = read_float_accessor( model, accessors[ frame_index ] );
            const std::size_t source_count = values.size() / 3u;
            std::vector<filetypes::xg::Vec3> frame;
            frame.reserve( indices.size() );

            for( const std::uint32_t source_index : indices )
            {
                if( source_index >= source_count )
                {
                    frame.push_back( filetypes::xg::Vec3{} );
                    continue;
                }

                const std::size_t offset = static_cast<std::size_t>( source_index ) * 3u;
                frame.push_back(
                    filetypes::xg::Vec3{ values[ offset + 0u ], values[ offset + 1u ], values[ offset + 2u ] } );
            }

            track.frames.push_back( std::move( frame ) );
        }
    }

    void load_vec2_track( const tinygltf::Model &model,
        const tinygltf::Value *track_value,
        const std::vector<std::uint32_t> &indices,
        GltfPrimitive::Vec2Track &track )
    {
        if( track_value == nullptr || !track_value->IsObject() )
        {
            return;
        }

        track.interpolation_type = value_u32( track_value, "interpolation_type", 0u );

        track.times = value_float_array( value_member( track_value, "times" ) );
        const std::vector<std::int32_t> accessors = value_int_array( value_member( track_value, "accessors" ) );
        const std::size_t frame_count = std::min( track.times.size(), accessors.size() );
        track.times.resize( frame_count );
        track.frames.reserve( frame_count );

        for( std::size_t frame_index = 0; frame_index < frame_count; ++frame_index )
        {
            const std::vector<float> values = read_float_accessor( model, accessors[ frame_index ] );
            const std::size_t source_count = values.size() / 2u;
            std::vector<rendering::Vec2> frame;
            frame.reserve( indices.size() );

            for( const std::uint32_t source_index : indices )
            {
                if( source_index >= source_count )
                {
                    frame.push_back( rendering::Vec2{} );
                    continue;
                }

                const std::size_t offset = static_cast<std::size_t>( source_index ) * 2u;
                frame.push_back( rendering::Vec2{ values[ offset + 0u ], values[ offset + 1u ] } );
            }

            track.frames.push_back( std::move( frame ) );
        }
    }
#endif

    rendering::Vec2 lerp_vec2( const rendering::Vec2 &a, const rendering::Vec2 &b, float weight )
    {
        weight = piko::utils::clamp01( weight );
        return rendering::Vec2{ a.x + ( b.x - a.x ) * weight, a.y + ( b.y - a.y ) * weight };
    }

    void assign_vertex_influences( GltfVertex &vertex,
        std::size_t source_index,
        const std::vector<std::uint32_t> &joints0,
        const std::vector<float> &weights0,
        const std::vector<std::uint32_t> &joints1,
        const std::vector<float> &weights1 )
    {
        std::vector<std::pair<std::uint32_t, float>> influences;
        influences.reserve( 8u );

        const auto append_set = [ & ]( const std::vector<std::uint32_t> &joints, const std::vector<float> &weights )
        {
            const std::size_t offset = source_index * 4u;

            if( joints.size() < offset + 4u || weights.size() < offset + 4u )
            {
                return;
            }

            for( std::size_t component = 0; component < 4u; ++component )
            {
                const float weight = weights[ offset + component ];

                if( !std::isfinite( weight ) || weight <= 0.000001f )
                {
                    continue;
                }

                const std::uint32_t joint = joints[ offset + component ];

                const auto existing = std::find_if( influences.begin(),
                    influences.end(),
                    [ joint ]( const auto &influence )
                    {
                        return influence.first == joint;
                    } );
                if( existing == influences.end() )
                {
                    influences.emplace_back( joint, weight );
                }
                else
                {
                    existing->second += weight;
                }
            }
        };

        append_set( joints0, weights0 );
        append_set( joints1, weights1 );
        std::stable_sort( influences.begin(),
            influences.end(),
            []( const auto &left, const auto &right )
            {
                return left.second > right.second;
            } );

        const std::size_t count = std::min<std::size_t>( influences.size(), vertex.joints.size() );
        float total = 0.0f;

        for( std::size_t index = 0; index < count; ++index )
        {
            total += influences[ index ].second;
        }

        if( total <= 0.000001f )
        {
            return;
        }

        for( std::size_t index = 0; index < count; ++index )
        {
            vertex.joints[ index ] = static_cast<std::uint16_t>(
                std::min<std::uint32_t>( influences[ index ].first, std::numeric_limits<std::uint16_t>::max() ) );

            vertex.weights[ index ] = influences[ index ].second / total;
        }
    }

    template <typename T, typename Lerp>
    std::vector<T> sample_geometry_track( const std::vector<float> &times,
        const std::vector<std::vector<T>> &frames,
        std::uint32_t interpolation_type,
        float animation_frame,
        Lerp lerp )
    {
        if( frames.empty() )
        {
            return {};
        }

        if( times.empty() || frames.size() == 1u || animation_frame <= times.front() )
        {
            return frames.front();
        }

        if( animation_frame >= times.back() )
        {
            return frames.back();
        }

        std::size_t frame_index = 0u;

        for( std::size_t index = 0; index + 1u < times.size(); ++index )
        {
            if( animation_frame < times[ index + 1u ] )
            {
                frame_index = index;
                break;
            }
        }

        if( interpolation_type == 0u || frame_index + 1u >= frames.size() )
        {
            return frames[ frame_index ];
        }

        const float duration = times[ frame_index + 1u ] - times[ frame_index ];
        const float weight =
            duration > 0.000001f ? piko::utils::clamp01( ( animation_frame - times[ frame_index ] ) / duration ) : 0.0f;

        const std::vector<T> &a = frames[ frame_index ];
        const std::vector<T> &b = frames[ frame_index + 1u ];
        const std::size_t count = std::min( a.size(), b.size() );
        std::vector<T> output;
        output.reserve( count );

        for( std::size_t index = 0; index < count; ++index )
        {
            output.push_back( lerp( a[ index ], b[ index ], weight ) );
        }

        return output;
    }

    std::array<float, 4> sample_channel( const GltfAnimationChannel &channel, float time )
    {
        if( channel.values.empty() )
        {
            return {};
        }

        if( channel.times.empty() || channel.values.size() == 1u || time <= channel.times.front() )
        {
            return channel.values.front();
        }

        if( time >= channel.times.back() )
        {
            return channel.values.back();
        }

        std::size_t index = 0u;

        for( std::size_t candidate = 0; candidate + 1u < channel.times.size(); ++candidate )
        {
            if( time < channel.times[ candidate + 1u ] )
            {
                index = candidate;
                break;
            }
        }

        if( channel.interpolation == "STEP" || index + 1u >= channel.values.size() )
        {
            return channel.values[ index ];
        }

        const float duration = channel.times[ index + 1u ] - channel.times[ index ];
        const float weight =
            duration > 0.000001f ? piko::utils::clamp01( ( time - channel.times[ index ] ) / duration ) : 0.0f;

        const std::array<float, 4> &a = channel.values[ index ];
        const std::array<float, 4> &b = channel.values[ index + 1u ];

        if( channel.path == GltfChannelPath::Rotation )
        {
            const filetypes::xg::Quat rotation =
                lerp_quaternion_shortest( filetypes::xg::Quat{ a[ 0 ], a[ 1 ], a[ 2 ], a[ 3 ] },
                    filetypes::xg::Quat{ b[ 0 ], b[ 1 ], b[ 2 ], b[ 3 ] },
                    weight );

            return { rotation.x, rotation.y, rotation.z, rotation.w };
        }

        return {
            a[ 0 ] + ( b[ 0 ] - a[ 0 ] ) * weight,
            a[ 1 ] + ( b[ 1 ] - a[ 1 ] ) * weight,
            a[ 2 ] + ( b[ 2 ] - a[ 2 ] ) * weight,
            a[ 3 ] + ( b[ 3 ] - a[ 3 ] ) * weight
        };
    }

    NodePose sample_node_pose( const GltfModelAsset &asset,
        std::int32_t node_index,
        std::uint32_t animation_index,
        float animation_frame )
    {
        if( node_index < 0 || static_cast<std::size_t>( node_index ) >= asset.nodes.size() )
        {
            return NodePose{};
        }

        const GltfNode &node = asset.nodes[ static_cast<std::size_t>( node_index ) ];
        NodePose pose{};
        pose.translation = node.translation;

        pose.rotation = node.rotation;

        pose.scale = node.scale;

        pose.matrix = node.matrix;

        pose.use_matrix = node.has_matrix;
        const std::vector<GltfAnimationChannel> *channels = nullptr;

        if( asset.global_animation >= 0 &&
            static_cast<std::size_t>( asset.global_animation ) < asset.animation_clips.size() )
        {
            channels = &asset.animation_clips[ static_cast<std::size_t>( asset.global_animation ) ].channels;
        }

        float time = asset.frame_rate > 0.0001f ? animation_frame / asset.frame_rate : animation_frame;

        if( asset.indexed_animations && !asset.animation_clips.empty() )
        {
            const std::size_t clip_index = std::min<std::size_t>( animation_index, asset.animation_clips.size() - 1u );
            const GltfAnimationClip &clip = asset.animation_clips[ clip_index ];
            channels = &clip.channels;

            if( clip.uses_gmo_timing )
            {
                time = asset.frame_rate > 0.0001f ? ( animation_frame - clip.start_frame ) / asset.frame_rate
                                                  : animation_frame - clip.start_frame;
            }
            else
            {
                float minimum_time = std::numeric_limits<float>::max();
                float maximum_time = std::numeric_limits<float>::lowest();

                for( const GltfAnimationChannel &channel : clip.channels )
                {
                    if( channel.times.empty() )
                    {
                        continue;
                    }

                    minimum_time = std::min( minimum_time, channel.times.front() );

                    maximum_time = std::max( maximum_time, channel.times.back() );
                }

                if( minimum_time <= maximum_time )
                {
                    float source_start = clip.start_frame;
                    float source_duration = std::max( 0.0f, clip.end_frame - clip.start_frame );

                    if( clip_index < asset.animation_timings.size() )
                    {
                        const AnimationTiming &source = asset.animation_timings[ clip_index ];
                        const float interval =
                            std::abs( source.keyframe_interval ) > 0.0001f ? source.keyframe_interval : 1.0f;

                        source_start = source.start_keyframe;

                        source_duration = std::max( 0.0f, source.playback_length / interval );
                    }

                    const float phase =
                        source_duration > 0.0001f
                            ? piko::utils::clamp01( ( animation_frame - source_start ) / source_duration )
                            : 0.0f;

                    time = minimum_time + ( maximum_time - minimum_time ) * phase;
                }
            }
        }

        if( channels == nullptr )
        {
            return pose;
        }

        for( const GltfAnimationChannel &channel : *channels )
        {
            if( channel.node_index != node_index )
            {
                continue;
            }

            const std::array<float, 4> value = sample_channel( channel, time );

            switch( channel.path )
            {
                case GltfChannelPath::Translation:
                    pose.translation = filetypes::xg::Vec3{ value[ 0 ], value[ 1 ], value[ 2 ] };

                    pose.use_matrix = false;
                    break;
                case GltfChannelPath::Rotation:
                    pose.rotation = filetypes::xg::normalize_quaternion(
                        filetypes::xg::Quat{ value[ 0 ], value[ 1 ], value[ 2 ], value[ 3 ] } );

                    pose.use_matrix = false;
                    break;
                case GltfChannelPath::Scale:
                    pose.scale = filetypes::xg::Vec3{ value[ 0 ], value[ 1 ], value[ 2 ] };

                    pose.use_matrix = false;
                    break;
            }
        }

        return pose;
    }

    NodePose blend_pose( const NodePose &source, const NodePose &target, float weight )
    {
        NodePose output{};
        output.translation = lerp_vec3( source.translation, target.translation, weight );

        output.rotation = lerp_quaternion_shortest( source.rotation, target.rotation, weight );

        output.scale = lerp_vec3( source.scale, target.scale, weight );

        output.use_matrix = false;
        return output;
    }

    filetypes::xg::Matrix pose_matrix( const NodePose &pose )
    {
        return pose.use_matrix ? pose.matrix : filetypes::xg::make_trs( pose.translation, pose.rotation, pose.scale );
    }

    filetypes::xg::Matrix evaluate_world_recursive( const GltfModelAsset &asset,
        std::int32_t node_index,
        std::uint32_t animation_index,
        float animation_frame,
        bool blend_active,
        std::uint32_t source_animation_index,
        float source_frame,
        std::uint32_t target_animation_index,
        float target_frame,
        float blend_weight,
        std::vector<filetypes::xg::Matrix> &cache,
        std::vector<std::uint8_t> &state )
    {
        if( node_index < 0 || static_cast<std::size_t>( node_index ) >= asset.nodes.size() )
        {
            return filetypes::xg::identity_matrix();
        }

        const std::size_t index = static_cast<std::size_t>( node_index );

        if( state[ index ] == 2u )
        {
            return cache[ index ];
        }

        if( state[ index ] == 1u )
        {
            throw std::runtime_error( "glTF node hierarchy contains a cycle" );
        }

        state[ index ] = 1u;

        NodePose pose = sample_node_pose( asset, node_index, animation_index, animation_frame );

        if( blend_active )
        {
            pose = blend_pose( sample_node_pose( asset, node_index, source_animation_index, source_frame ),
                sample_node_pose( asset, node_index, target_animation_index, target_frame ),
                blend_weight );
        }

        filetypes::xg::Matrix world = pose_matrix( pose );
        const std::int32_t parent = asset.nodes[ index ].parent;

        if( parent >= 0 )
        {
            world = filetypes::xg::multiply( evaluate_world_recursive( asset,
                                                 parent,
                                                 animation_index,
                                                 animation_frame,
                                                 blend_active,
                                                 source_animation_index,
                                                 source_frame,
                                                 target_animation_index,
                                                 target_frame,
                                                 blend_weight,
                                                 cache,
                                                 state ),
                world );
        }

        cache[ index ] = world;

        state[ index ] = 2u;
        return world;
    }
}

namespace piko::runtime::model
{
    GltfModelAsset load_gltf_model( const std::filesystem::path &path )
    {
#if PIKO_HAS_TINYGLTF
        if( !std::filesystem::is_regular_file( path ) )
        {
            throw std::runtime_error( "glTF model was not found: " + path.string() );
        }

        tinygltf::TinyGLTF loader;
        tinygltf::Model source;
        std::string warnings;
        std::string errors;
        const bool loaded = loader.LoadASCIIFromFile( &source, &errors, &warnings, path.string() );

        if( !loaded )
        {
            std::string message = "Could not load glTF model: " + path.string();

            if( !errors.empty() )
            {
                message += ": " + errors;
            }

            throw std::runtime_error( message );
        }

        const tinygltf::Value *asset_piko = gltf_model::piko_object( source.asset.extras );

        GltfModelAsset asset{};
        asset.source_path = path.lexically_normal();

        if( asset_piko != nullptr )
        {
            asset.source_model_name = gltf_model::value_string( asset_piko, "source_model" );

            asset.frame_rate = static_cast<float>( gltf_model::value_number( asset_piko, "frame_rate", 30.0 ) );

            asset.global_animation = gltf_model::value_int( asset_piko, "global_animation", 0 );

            asset.indexed_animations = gltf_model::value_string( asset_piko, "animation_mode" ) == "indexed-clips";

            asset.gmo_model = gltf_model::value_string( asset_piko, "format" ) == "piko-gmo-model";
            const tinygltf::Value *animations = gltf_model::value_member( asset_piko, "animation_timings" );

            if( animations == nullptr )
            {
                animations = gltf_model::value_member( asset_piko, "xgm_animations" );
            }

            if( animations != nullptr && animations->IsArray() )
            {
                asset.animation_timings.reserve( animations->ArrayLen() );

                for( std::size_t animation_index = 0; animation_index < animations->ArrayLen(); ++animation_index )
                {
                    const tinygltf::Value &entry = animations->Get( animation_index );
                    AnimationTiming animation{};
                    animation.playback_length =
                        static_cast<float>( gltf_model::value_number( &entry, "playback_length", 0.0 ) );

                    animation.keyframe_interval =
                        static_cast<float>( gltf_model::value_number( &entry, "keyframe_interval", 0.0 ) );

                    animation.start_keyframe =
                        static_cast<float>( gltf_model::value_number( &entry, "start_keyframe", 0.0 ) );

                    animation.speed_mode = gltf_model::value_u32( &entry, "speed_mode", 0u );
                    asset.animation_timings.push_back( animation );
                }
            }
        }

        asset.nodes.resize( source.nodes.size() );

        for( std::size_t node_index = 0; node_index < source.nodes.size(); ++node_index )
        {
            const tinygltf::Node &source_node = source.nodes[ node_index ];
            GltfNode &node = asset.nodes[ node_index ];
            node.name = source_node.name;

            node.children = source_node.children;

            if( source_node.translation.size() == 3u )
            {
                node.translation = filetypes::xg::Vec3{
                    static_cast<float>( source_node.translation[ 0 ] ),
                    static_cast<float>( source_node.translation[ 1 ] ),
                    static_cast<float>( source_node.translation[ 2 ] )
                };
            }

            if( source_node.rotation.size() == 4u )
            {
                node.rotation = filetypes::xg::normalize_quaternion( filetypes::xg::Quat{
                    static_cast<float>( source_node.rotation[ 0 ] ),
                    static_cast<float>( source_node.rotation[ 1 ] ),
                    static_cast<float>( source_node.rotation[ 2 ] ),
                    static_cast<float>( source_node.rotation[ 3 ] )
                } );
            }

            if( source_node.scale.size() == 3u )
            {
                node.scale = filetypes::xg::Vec3{
                    static_cast<float>( source_node.scale[ 0 ] ),
                    static_cast<float>( source_node.scale[ 1 ] ),
                    static_cast<float>( source_node.scale[ 2 ] )
                };
            }

            if( source_node.matrix.size() == 16u )
            {
                for( std::size_t component = 0; component < 16u; ++component )
                {
                    node.matrix.m[ component ] = static_cast<float>( source_node.matrix[ component ] );
                }

                node.has_matrix = true;
            }
            else
            {
                node.matrix = filetypes::xg::identity_matrix();
            }
        }

        for( std::size_t parent = 0; parent < asset.nodes.size(); ++parent )
        {
            for( const std::int32_t child : asset.nodes[ parent ].children )
            {
                if( child >= 0 && static_cast<std::size_t>( child ) < asset.nodes.size() )
                {
                    asset.nodes[ static_cast<std::size_t>( child ) ].parent = static_cast<std::int32_t>( parent );
                }
            }
        }

        asset.materials.reserve( source.materials.size() );

        for( std::size_t material_index = 0; material_index < source.materials.size(); ++material_index )
        {
            const tinygltf::Material &source_material = source.materials[ material_index ];
            const tinygltf::Value *material_piko = gltf_model::piko_object( source_material.extras );

            GltfMaterial material{};
            material.name = source_material.name;
            const std::vector<double> &factor = source_material.pbrMetallicRoughness.baseColorFactor;

            if( factor.size() == 4u )
            {
                material.diffuse = rendering::Color{
                    static_cast<float>( factor[ 0 ] ),
                    static_cast<float>( factor[ 1 ] ),
                    static_cast<float>( factor[ 2 ] ),
                    static_cast<float>( factor[ 3 ] )
                };
            }

            material.blend_type =
                gltf_model::value_u32( material_piko, "blend_type", source_material.alphaMode == "BLEND" ? 5u : 0u );

            material.shading_type = gltf_model::value_u32( material_piko, "shading_type", 0u );

            material.flags = gltf_model::value_u32( material_piko, "flags", 0u );

            material.texture_env = gltf_model::value_u32( material_piko, "texture_env", 0u );

            material.u_tile = gltf_model::value_u32( material_piko, "u_tile", 0u );

            material.v_tile = gltf_model::value_u32( material_piko, "v_tile", 0u );

            material.uses_texture_alpha =
                gltf_model::value_bool( material_piko, "uses_texture_alpha", source_material.alphaMode == "BLEND" );

            const std::int32_t texture_index = source_material.pbrMetallicRoughness.baseColorTexture.index;

            if( texture_index >= 0 && static_cast<std::size_t>( texture_index ) < source.textures.size() )
            {
                const tinygltf::Texture &texture = source.textures[ static_cast<std::size_t>( texture_index ) ];

                if( texture.source >= 0 && static_cast<std::size_t>( texture.source ) < source.images.size() )
                {
                    const tinygltf::Image &image = source.images[ static_cast<std::size_t>( texture.source ) ];

                    if( !image.uri.empty() )
                    {
                        material.texture_path =
                            ( path.parent_path() / std::filesystem::path( image.uri ) ).lexically_normal();
                    }
                }

                if( texture.sampler >= 0 && static_cast<std::size_t>( texture.sampler ) < source.samplers.size() )
                {
                    const tinygltf::Sampler &sampler = source.samplers[ static_cast<std::size_t>( texture.sampler ) ];
                    material.wrap_texture = sampler.wrapS == 10497 || sampler.wrapT == 10497;
                }
            }

            material.wrap_texture = gltf_model::value_bool( material_piko, "wrap_texture", material.wrap_texture );
            asset.materials.push_back( std::move( material ) );
        }

        asset.skins.reserve( source.skins.size() );

        for( const tinygltf::Skin &source_skin : source.skins )
        {
            GltfSkin skin{};
            skin.name = source_skin.name;

            skin.joints = source_skin.joints;

            if( source_skin.inverseBindMatrices >= 0 )
            {
                const std::vector<float> values =
                    gltf_model::read_float_accessor( source, source_skin.inverseBindMatrices );

                for( std::size_t offset = 0; offset + 15u < values.size(); offset += 16u )
                {
                    filetypes::xg::Matrix matrix{};
                    std::copy_n( values.begin() + static_cast<std::ptrdiff_t>( offset ), 16u, matrix.m );
                    skin.inverse_bind_matrices.push_back( matrix );
                }
            }
            while( skin.inverse_bind_matrices.size() < skin.joints.size() )
            {
                skin.inverse_bind_matrices.push_back( filetypes::xg::identity_matrix() );
            }

            asset.skins.push_back( std::move( skin ) );
        }

        for( std::size_t node_index = 0; node_index < source.nodes.size(); ++node_index )
        {
            const tinygltf::Node &source_node = source.nodes[ node_index ];

            if( source_node.mesh < 0 || static_cast<std::size_t>( source_node.mesh ) >= source.meshes.size() )
            {
                continue;
            }

            const tinygltf::Mesh &source_mesh = source.meshes[ static_cast<std::size_t>( source_node.mesh ) ];

            for( std::size_t primitive_index = 0; primitive_index < source_mesh.primitives.size(); ++primitive_index )
            {
                const tinygltf::Primitive &source_primitive = source_mesh.primitives[ primitive_index ];

                if( source_primitive.mode != 4 )
                {
                    continue;
                }

                const auto position_attribute = source_primitive.attributes.find( "POSITION" );

                if( position_attribute == source_primitive.attributes.end() )
                {
                    continue;
                }

                const std::vector<float> positions =
                    gltf_model::read_float_accessor( source, position_attribute->second );

                const std::size_t vertex_count = positions.size() / 3u;
                std::vector<float> normals;
                std::vector<float> texcoords;
                std::vector<float> colors;
                std::vector<std::uint32_t> joints0;
                std::vector<float> weights0;
                std::vector<std::uint32_t> joints1;
                std::vector<float> weights1;

                if( const auto attribute = source_primitive.attributes.find( "NORMAL" );
                    attribute != source_primitive.attributes.end() )
                {
                    normals = gltf_model::read_float_accessor( source, attribute->second );
                }

                if( const auto attribute = source_primitive.attributes.find( "TEXCOORD_0" );
                    attribute != source_primitive.attributes.end() )
                {
                    texcoords = gltf_model::read_float_accessor( source, attribute->second );
                }

                if( const auto attribute = source_primitive.attributes.find( "COLOR_0" );
                    attribute != source_primitive.attributes.end() )
                {
                    colors = gltf_model::read_float_accessor( source, attribute->second );
                }

                if( const auto attribute = source_primitive.attributes.find( "JOINTS_0" );
                    attribute != source_primitive.attributes.end() )
                {
                    joints0 = gltf_model::read_unsigned_accessor( source, attribute->second );
                }

                if( const auto attribute = source_primitive.attributes.find( "WEIGHTS_0" );
                    attribute != source_primitive.attributes.end() )
                {
                    weights0 = gltf_model::read_float_accessor( source, attribute->second );
                }

                if( const auto attribute = source_primitive.attributes.find( "JOINTS_1" );
                    attribute != source_primitive.attributes.end() )
                {
                    joints1 = gltf_model::read_unsigned_accessor( source, attribute->second );
                }

                if( const auto attribute = source_primitive.attributes.find( "WEIGHTS_1" );
                    attribute != source_primitive.attributes.end() )
                {
                    weights1 = gltf_model::read_float_accessor( source, attribute->second );
                }

                std::vector<std::uint32_t> indices;

                if( source_primitive.indices >= 0 )
                {
                    indices = gltf_model::read_unsigned_accessor( source, source_primitive.indices );
                }
                else
                {
                    indices.resize( vertex_count );

                    for( std::size_t index = 0; index < vertex_count; ++index )
                    {
                        indices[ index ] = static_cast<std::uint32_t>( index );
                    }
                }

                std::vector<std::uint32_t> valid_indices;
                valid_indices.reserve( indices.size() );

                for( std::size_t triangle = 0u; triangle + 2u < indices.size(); triangle += 3u )
                {
                    const std::uint32_t a = indices[ triangle ];
                    const std::uint32_t b = indices[ triangle + 1u ];
                    const std::uint32_t c = indices[ triangle + 2u ];

                    if( a < vertex_count && b < vertex_count && c < vertex_count )
                    {
                        valid_indices.insert( valid_indices.end(), { a, b, c } );
                    }
                }

                if( valid_indices.empty() )
                {
                    continue;
                }

                std::vector<std::uint32_t> attribute_indices( vertex_count );

                for( std::size_t index = 0u; index < vertex_count; ++index )
                {
                    attribute_indices[ index ] = static_cast<std::uint32_t>( index );
                }

                const tinygltf::Value *primitive_piko = gltf_model::piko_object( source_primitive.extras );

                GltfPrimitive primitive{};
                primitive.name = source_mesh.name + ":" + std::to_string( primitive_index );

                primitive.node_index = static_cast<std::int32_t>( node_index );

                primitive.skin_index = source_node.skin;

                primitive.material_index = source_primitive.material;

                primitive.cull_func = gltf_model::value_u32( primitive_piko, "cull_func", 0u );

                primitive.skinned =
                    gltf_model::value_bool( primitive_piko, "skinned", !joints0.empty() && !weights0.empty() );

                if( primitive_piko != nullptr )
                {
                    const tinygltf::Value *geometry_animation =
                        gltf_model::value_member( primitive_piko, "geometry_animation" );

                    if( geometry_animation != nullptr && geometry_animation->IsObject() )
                    {
                        gltf_model::load_vec3_track( source,
                            gltf_model::value_member( geometry_animation, "position" ),
                            attribute_indices,
                            primitive.position_track );

                        gltf_model::load_vec3_track( source,
                            gltf_model::value_member( geometry_animation, "normal" ),
                            attribute_indices,
                            primitive.normal_track );

                        gltf_model::load_vec2_track( source,
                            gltf_model::value_member( geometry_animation, "texcoord" ),
                            attribute_indices,
                            primitive.texcoord_track );
                    }
                }

                primitive.indices = std::move( valid_indices );
                primitive.vertices.reserve( vertex_count );

                for( std::uint32_t source_index = 0u; source_index < vertex_count; ++source_index )
                {
                    GltfVertex vertex{};
                    vertex.position = filetypes::xg::Vec3{
                        positions[ static_cast<std::size_t>( source_index ) * 3u + 0u ],
                        positions[ static_cast<std::size_t>( source_index ) * 3u + 1u ],
                        positions[ static_cast<std::size_t>( source_index ) * 3u + 2u ]
                    };

                    if( normals.size() >= ( static_cast<std::size_t>( source_index ) + 1u ) * 3u )
                    {
                        vertex.normal = filetypes::xg::Vec3{
                            normals[ static_cast<std::size_t>( source_index ) * 3u + 0u ],
                            normals[ static_cast<std::size_t>( source_index ) * 3u + 1u ],
                            normals[ static_cast<std::size_t>( source_index ) * 3u + 2u ]
                        };
                    }

                    if( texcoords.size() >= ( static_cast<std::size_t>( source_index ) + 1u ) * 2u )
                    {
                        vertex.uv = rendering::Vec2{
                            texcoords[ static_cast<std::size_t>( source_index ) * 2u + 0u ],
                            texcoords[ static_cast<std::size_t>( source_index ) * 2u + 1u ]
                        };
                    }

                    if( colors.size() >= ( static_cast<std::size_t>( source_index ) + 1u ) * 4u )
                    {
                        vertex.color = rendering::Color{
                            colors[ static_cast<std::size_t>( source_index ) * 4u + 0u ],
                            colors[ static_cast<std::size_t>( source_index ) * 4u + 1u ],
                            colors[ static_cast<std::size_t>( source_index ) * 4u + 2u ],
                            colors[ static_cast<std::size_t>( source_index ) * 4u + 3u ]
                        };
                    }

                    gltf_model::assign_vertex_influences( vertex, source_index, joints0, weights0, joints1, weights1 );
                    primitive.vertices.push_back( vertex );
                }

                asset.primitives.push_back( std::move( primitive ) );
            }
        }

        for( const tinygltf::Animation &animation : source.animations )
        {
            const tinygltf::Value *animation_piko = gltf_model::piko_object( animation.extras );

            GltfAnimationClip clip{};
            clip.name = animation.name;

            if( animation_piko != nullptr )
            {
                clip.start_frame = static_cast<float>( gltf_model::value_number( animation_piko, "start_frame", 0.0 ) );

                clip.end_frame =
                    static_cast<float>( gltf_model::value_number( animation_piko, "end_frame", clip.start_frame ) );

                if( asset.gmo_model )
                {
                    const std::string timing_mode = gltf_model::value_string( animation_piko, "timing_mode" );
                    const tinygltf::Value *playback_rate_value =
                        gltf_model::value_member( animation_piko, "playback_rate" );

                    if( timing_mode == "model-speed" )
                    {
                        clip.uses_gmo_timing = true;

                        clip.uses_model_playback_speed = true;
                    }
                    else if( timing_mode == "fixed-rate" )
                    {
                        clip.uses_gmo_timing = true;

                        if( playback_rate_value != nullptr && playback_rate_value->IsNumber() )
                        {
                            clip.playback_rate = static_cast<float>( playback_rate_value->GetNumberAsDouble() );
                        }
                    }
                    else
                    {
                        throw std::runtime_error( "Invalid GMO animation timing mode: " + clip.name );
                    }
                }
            }

            for( const tinygltf::AnimationChannel &source_channel : animation.channels )
            {
                if( source_channel.sampler < 0 ||
                    static_cast<std::size_t>( source_channel.sampler ) >= animation.samplers.size() )
                {
                    continue;
                }

                const tinygltf::AnimationSampler &source_sampler =
                    animation.samplers[ static_cast<std::size_t>( source_channel.sampler ) ];

                GltfAnimationChannel channel{};
                channel.node_index = source_channel.target_node;

                channel.interpolation = source_sampler.interpolation;

                if( source_channel.target_path == "translation" )
                {
                    channel.path = GltfChannelPath::Translation;
                }
                else if( source_channel.target_path == "rotation" )
                {
                    channel.path = GltfChannelPath::Rotation;
                }
                else if( source_channel.target_path == "scale" )
                {
                    channel.path = GltfChannelPath::Scale;
                }
                else
                {
                    continue;
                }

                channel.times = gltf_model::read_float_accessor( source, source_sampler.input );
                const std::vector<float> values = gltf_model::read_float_accessor( source, source_sampler.output );
                const std::size_t components = channel.path == GltfChannelPath::Rotation ? 4u : 3u;

                for( std::size_t offset = 0; offset + components - 1u < values.size(); offset += components )
                {
                    std::array<float, 4> value{};

                    for( std::size_t component = 0; component < components; ++component )
                    {
                        value[ component ] = values[ offset + component ];
                    }

                    if( channel.path == GltfChannelPath::Rotation )
                    {
                        const filetypes::xg::Quat rotation = filetypes::xg::normalize_quaternion(
                            filetypes::xg::Quat{ value[ 0 ], value[ 1 ], value[ 2 ], value[ 3 ] } );

                        value = { rotation.x, rotation.y, rotation.z, rotation.w };
                    }

                    channel.values.push_back( value );
                }

                clip.channels.push_back( std::move( channel ) );
            }

            asset.animation_clips.push_back( std::move( clip ) );
        }

        if( asset.indexed_animations && asset.animation_timings.empty() )
        {
            asset.animation_timings.reserve( asset.animation_clips.size() );

            for( const GltfAnimationClip &clip : asset.animation_clips )
            {
                AnimationTiming animation{};
                const float source_length = std::max( 0.0f, clip.end_frame - clip.start_frame );
                const float playback_rate =
                    std::isfinite( clip.playback_rate ) && clip.playback_rate > 0.0f ? clip.playback_rate : 0.0f;

                animation.playback_length = source_length;

                animation.start_keyframe = clip.start_frame;

                if( asset.gmo_model )
                {
                    if( clip.uses_model_playback_speed || playback_rate <= 0.0f )
                    {
                        animation.keyframe_interval = 1.0f;

                        animation.speed_mode = 2u;
                    }
                    else
                    {
                        animation.keyframe_interval = 60.0f / playback_rate;

                        animation.speed_mode = 3u;
                    }
                }
                else
                {
                    animation.keyframe_interval = 1.0f;

                    animation.speed_mode = 1u;
                }

                asset.animation_timings.push_back( animation );
            }
        }

        return asset;
#else
        (void)path;
        throw std::runtime_error( "TinyGLTF is not installed" );
#endif
    }

    filetypes::xg::Matrix evaluate_gltf_node_world( const GltfModelAsset &asset,
        std::int32_t node_index,
        std::uint32_t animation_index,
        float animation_frame )
    {
        std::vector<filetypes::xg::Matrix> cache( asset.nodes.size(), filetypes::xg::identity_matrix() );
        std::vector<std::uint8_t> state( asset.nodes.size(), 0u );
        return gltf_model::evaluate_world_recursive( asset,
            node_index,
            animation_index,
            animation_frame,
            false,
            animation_index,
            0.0f,
            animation_index,
            0.0f,
            0.0f,
            cache,
            state );
    }

    filetypes::xg::Matrix evaluate_gltf_node_world_blended( const GltfModelAsset &asset,
        std::int32_t node_index,
        std::uint32_t source_animation_index,
        float source_frame,
        std::uint32_t target_animation_index,
        float target_frame,
        float blend_weight )
    {
        std::vector<filetypes::xg::Matrix> cache( asset.nodes.size(), filetypes::xg::identity_matrix() );
        std::vector<std::uint8_t> state( asset.nodes.size(), 0u );
        return gltf_model::evaluate_world_recursive( asset,
            node_index,
            target_animation_index,
            target_frame,
            true,
            source_animation_index,
            source_frame,
            target_animation_index,
            target_frame,
            blend_weight,
            cache,
            state );
    }

    std::vector<filetypes::xg::Matrix> build_gltf_skin_matrices( const GltfModelAsset &asset,
        std::int32_t skin_index,
        std::int32_t mesh_node_index,
        std::uint32_t animation_index,
        float animation_frame,
        bool blend_active,
        std::uint32_t source_animation_index,
        float source_frame,
        std::uint32_t target_animation_index,
        float target_frame,
        float blend_weight )
    {
        if( skin_index < 0 || static_cast<std::size_t>( skin_index ) >= asset.skins.size() )
        {
            return {};
        }

        const GltfSkin &skin = asset.skins[ static_cast<std::size_t>( skin_index ) ];
        std::vector<filetypes::xg::Matrix> world_cache( asset.nodes.size(), filetypes::xg::identity_matrix() );
        std::vector<std::uint8_t> world_state( asset.nodes.size(), 0u );

        const auto evaluate_world = [ & ]( std::int32_t node_index )
        {
            return gltf_model::evaluate_world_recursive( asset,
                node_index,
                animation_index,
                animation_frame,
                blend_active,
                source_animation_index,
                source_frame,
                target_animation_index,
                target_frame,
                blend_weight,
                world_cache,
                world_state );
        };

        const filetypes::xg::Matrix mesh_world = evaluate_world( mesh_node_index );
        const filetypes::xg::Matrix inverse_mesh_world = filetypes::xg::inverse_affine_matrix( mesh_world );
        std::vector<filetypes::xg::Matrix> output;
        output.reserve( skin.joints.size() );

        for( std::size_t joint_index = 0; joint_index < skin.joints.size(); ++joint_index )
        {
            const filetypes::xg::Matrix world = evaluate_world( skin.joints[ joint_index ] );
            const filetypes::xg::Matrix inverse_bind = joint_index < skin.inverse_bind_matrices.size()
                                                           ? skin.inverse_bind_matrices[ joint_index ]
                                                           : filetypes::xg::identity_matrix();

            output.push_back(
                filetypes::xg::multiply( filetypes::xg::multiply( inverse_mesh_world, world ), inverse_bind ) );
        }

        return output;
    }

    std::vector<GltfVertex> sample_gltf_primitive_vertices( const GltfPrimitive &primitive, float animation_frame )
    {
        std::vector<GltfVertex> output = primitive.vertices;

        if( output.empty() )
        {
            return output;
        }

        const std::vector<filetypes::xg::Vec3> positions =
            gltf_model::sample_geometry_track( primitive.position_track.times,
                primitive.position_track.frames,
                primitive.position_track.interpolation_type,
                animation_frame,
                gltf_model::lerp_vec3 );

        const std::vector<filetypes::xg::Vec3> normals =
            gltf_model::sample_geometry_track( primitive.normal_track.times,
                primitive.normal_track.frames,
                primitive.normal_track.interpolation_type,
                animation_frame,
                gltf_model::lerp_vec3 );

        const std::vector<rendering::Vec2> texcoords =
            gltf_model::sample_geometry_track( primitive.texcoord_track.times,
                primitive.texcoord_track.frames,
                primitive.texcoord_track.interpolation_type,
                animation_frame,
                gltf_model::lerp_vec2 );

        const std::size_t position_count = std::min( output.size(), positions.size() );

        for( std::size_t index = 0; index < position_count; ++index )
        {
            output[ index ].position = positions[ index ];
        }

        const std::size_t normal_count = std::min( output.size(), normals.size() );

        for( std::size_t index = 0; index < normal_count; ++index )
        {
            output[ index ].normal = filetypes::xg::normalize( normals[ index ] );
        }

        const std::size_t texcoord_count = std::min( output.size(), texcoords.size() );

        for( std::size_t index = 0; index < texcoord_count; ++index )
        {
            output[ index ].uv = texcoords[ index ];
        }

        return output;
    }

    std::vector<GltfVertex> sample_gltf_primitive_vertices_blended( const GltfPrimitive &primitive,
        float source_frame,
        float target_frame,
        float blend_weight )
    {
        std::vector<GltfVertex> source = sample_gltf_primitive_vertices( primitive, source_frame );
        const std::vector<GltfVertex> target = sample_gltf_primitive_vertices( primitive, target_frame );
        const std::size_t count = std::min( source.size(), target.size() );
        blend_weight = std::clamp( blend_weight, 0.0f, 1.0f );

        for( std::size_t index = 0; index < count; ++index )
        {
            source[ index ].position =
                gltf_model::lerp_vec3( source[ index ].position, target[ index ].position, blend_weight );

            source[ index ].normal = filetypes::xg::normalize(
                gltf_model::lerp_vec3( source[ index ].normal, target[ index ].normal, blend_weight ) );

            source[ index ].uv = gltf_model::lerp_vec2( source[ index ].uv, target[ index ].uv, blend_weight );
        }

        return source;
    }
}
