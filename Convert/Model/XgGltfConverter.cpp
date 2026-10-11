#include "Model/XgGltfConverter.hpp"

#include "Model/GltfUtil.hpp"

#include "Utility/JsonUtil.hpp"

#include "FileTypes/XG/XG.hpp"
#include "FileTypes/XGM/XGM.hpp"
#include "Image/ImageConverter.hpp"
#include "Rendering/RenderUtil.hpp"
#include "Utils/StrUtil.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace piko::convert::xg_gltf
{
    constexpr std::int32_t component_float = 5126;
    constexpr std::int32_t component_unsigned_short = 5123;
    constexpr std::int32_t component_unsigned_int = 5125;
    constexpr std::int32_t target_array_buffer = 34962;
    constexpr std::int32_t target_element_array_buffer = 34963;
    constexpr std::int32_t mode_triangles = 4;
    constexpr float frame_rate = 30.0f;

    struct BufferView
    {
        std::size_t byte_offset = 0;
        std::size_t byte_length = 0;
        std::int32_t target = 0;
    };

    struct Accessor
    {
        std::int32_t buffer_view = -1;
        std::size_t byte_offset = 0;
        std::int32_t component_type = component_float;
        std::size_t count = 0;
        std::string type;
        std::vector<double> minimum;
        std::vector<double> maximum;
    };

    struct Material
    {
        std::string name;
        std::int32_t texture = -1;
        std::array<double, 4> diffuse{
            1.0,
            1.0,
            1.0,
            1.0
        };

        std::uint32_t blend_type = 0;
        std::uint32_t shading_type = 0;
        std::uint32_t flags = 0;
        std::uint32_t texture_env = 0;
        std::uint32_t u_tile = 0;
        std::uint32_t v_tile = 0;
        bool uses_texture_alpha = false;
        bool wrap_texture = true;
    };

    struct Primitive
    {
        std::int32_t position = -1;
        std::int32_t normal = -1;
        std::int32_t texcoord = -1;
        std::int32_t color = -1;
        std::int32_t joints = -1;
        std::int32_t weights = -1;
        std::int32_t indices = -1;
        std::int32_t material = -1;
        std::uint32_t cull_func = 0;
        bool skinned = false;

        struct GeometryTrack
        {
            std::uint32_t interpolation_type = 0;
            std::vector<float> times;
            std::vector<std::int32_t> accessors;
        };

        struct MorphTarget
        {
            std::int32_t position = -1;
            std::int32_t normal = -1;
        };

        GeometryTrack position_track;
        GeometryTrack normal_track;
        GeometryTrack texcoord_track;
        std::vector<MorphTarget> morph_targets;
    };

    struct Mesh
    {
        std::string name;
        std::vector<Primitive> primitives;
        std::vector<double> weights;
        std::vector<std::string> target_names;
    };

    struct Node
    {
        std::string name;
        std::int32_t mesh = -1;
        std::int32_t skin = -1;
        std::vector<std::int32_t> children;
        std::array<double, 3> translation{
            0.0,
            0.0,
            0.0
        };

        std::array<double, 4> rotation{
            0.0,
            0.0,
            0.0,
            1.0
        };

        std::array<double, 3> scale{
            1.0,
            1.0,
            1.0
        };
    };

    struct Skin
    {
        std::string name;
        std::int32_t inverse_bind_matrices = -1;
        std::vector<std::int32_t> joints;
    };

    struct AnimationSampler
    {
        std::int32_t input = -1;
        std::int32_t output = -1;
        std::string interpolation = "LINEAR";
    };

    struct AnimationChannel
    {
        std::int32_t sampler = -1;
        std::int32_t node = -1;
        std::string path;
    };

    struct Animation
    {
        std::string name;
        std::vector<AnimationSampler> samplers;
        std::vector<AnimationChannel> channels;
    };

    struct Image
    {
        std::string name;
        std::string uri;
    };

    struct Texture
    {
        std::int32_t source = -1;
        std::int32_t sampler = -1;
    };

    struct Document
    {
        std::vector<std::uint8_t> buffer;
        std::vector<BufferView> buffer_views;
        std::vector<Accessor> accessors;
        std::vector<Image> images;
        std::vector<Texture> textures;
        std::vector<Material> materials;
        std::vector<Mesh> meshes;
        std::vector<Node> nodes;
        std::vector<Skin> skins;
        std::vector<Animation> animations;
        std::vector<std::int32_t> scene_nodes;

        struct PendingWeightAnimation
        {
            std::int32_t node = -1;
            std::vector<float> times;
            std::vector<float> weights;
        };

        std::vector<PendingWeightAnimation> pending_weight_animations;
    };

    struct SkinBinding
    {
        filetypes::xg::Vertex source{};
        filetypes::xg::Vec4 weights{};
        std::array<std::uint16_t, 4> joints{};
        bool valid = false;
    };

    std::string normalize_name( std::string value )
    {
        value = piko::utils::normalize_slashes( std::move( value ) );
        const std::size_t slash = value.find_last_of( '/' );

        if( slash != std::string::npos )
        {
            value.erase( 0, slash + 1u );
        }

        return piko::utils::lowercase_ascii( std::move( value ) );
    }

    std::string model_stem( std::string value )
    {
        return piko::utils::safe_ascii_stem( std::move( value ), "model" );
    }

    std::string safe_stem( std::string value )
    {
        return piko::utils::safe_ascii_stem( std::move( value ), "texture", true );
    }

    template <typename T>
    std::int32_t append_vector( Document &document,
        const std::vector<T> &values,
        std::int32_t component_type_value,
        std::string type,
        std::int32_t target,
        std::vector<double> minimum = {},
        std::vector<double> maximum = {} )
    {
        gltf::align_buffer( document.buffer, 4u );
        const std::size_t offset = document.buffer.size();
        const std::size_t size = values.size() * sizeof( T );
        document.buffer.resize( offset + size );

        if( size != 0u )
        {
            std::memcpy( document.buffer.data() + offset, values.data(), size );
        }

        const std::int32_t view_index = static_cast<std::int32_t>( document.buffer_views.size() );
        document.buffer_views.push_back( BufferView{ offset, size, target } );

        std::size_t count = values.size();

        if( type == "VEC2" )
        {
            count /= 2u;
        }
        else if( type == "VEC3" )
        {
            count /= 3u;
        }
        else if( type == "VEC4" )
        {
            count /= 4u;
        }
        else if( type == "MAT4" )
        {
            count /= 16u;
        }

        const std::int32_t accessor_index = static_cast<std::int32_t>( document.accessors.size() );
        document.accessors.push_back( Accessor{
            view_index,
            0u,
            component_type_value,
            count,
            std::move( type ),
            std::move( minimum ),
            std::move( maximum )
        } );
        return accessor_index;
    }

    filetypes::xg::Quat convert_rotation( filetypes::xg::Quat value )
    {
        value.z = -value.z;
        return filetypes::xg::normalize_quaternion( value );
    }

    std::vector<float> key_times( const std::vector<float> &times, std::size_t count )
    {
        std::vector<float> output;
        output.reserve( count );

        if( times.size() == count )
        {
            for( const float value : times )
            {
                output.push_back( value / frame_rate );
            }

            return output;
        }

        for( std::size_t index = 0; index < count; ++index )
        {
            output.push_back( static_cast<float>( index ) / frame_rate );
        }

        return output;
    }

    std::vector<float> source_key_times( const std::vector<float> &times, std::size_t count )
    {
        if( times.size() == count )
        {
            return times;
        }

        std::vector<float> output( count );

        for( std::size_t index = 0; index < count; ++index )
        {
            output[ index ] = static_cast<float>( index );
        }

        return output;
    }

    bool geometry_requires_texture_repeat( const filetypes::xg::Model &model, const filetypes::xg::Geometry &geometry )
    {
        for( const filetypes::xg::Vertex &vertex : geometry.vertices )
        {
            if( vertex.has_uv && rendering::uv_requires_texture_repeat( vertex.uv.x, vertex.uv.y ) )
            {
                return true;
            }
        }

        for( const std::string &input_node : geometry.input_geometry_nodes )
        {
            const filetypes::xg::TexCoordInterpolator *interpolator = model.find_texcoord_interpolator( input_node );

            if( interpolator == nullptr )
            {
                continue;
            }

            for( const std::vector<rendering::Vec2> &key : interpolator->keys )
            {
                for( const rendering::Vec2 &uv : key )
                {
                    if( rendering::uv_requires_texture_repeat( uv.x, uv.y ) )
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    template <typename T, typename Lerp>
    std::vector<T> sample_track( const std::vector<std::vector<T>> &keys,
        const std::vector<float> &times,
        std::uint32_t interpolation_type,
        float frame,
        Lerp lerp )
    {
        if( keys.empty() )
        {
            return {};
        }

        if( keys.size() == 1u )
        {
            return keys.front();
        }

        const std::vector<float> resolved_times = source_key_times( times, keys.size() );

        if( frame <= resolved_times.front() )
        {
            return keys.front();
        }

        if( frame >= resolved_times.back() )
        {
            return keys.back();
        }

        std::size_t key_index = 0u;

        for( std::size_t index = 0; index + 1u < resolved_times.size(); ++index )
        {
            if( frame < resolved_times[ index + 1u ] )
            {
                key_index = index;
                break;
            }
        }

        if( interpolation_type == 0u || key_index + 1u >= keys.size() )
        {
            return keys[ key_index ];
        }

        const float duration = resolved_times[ key_index + 1u ] - resolved_times[ key_index ];
        const float weight =
            duration > 0.000001f ? std::clamp( ( frame - resolved_times[ key_index ] ) / duration, 0.0f, 1.0f ) : 0.0f;

        const std::vector<T> &a = keys[ key_index ];
        const std::vector<T> &b = keys[ key_index + 1u ];
        const std::size_t count = std::min( a.size(), b.size() );
        std::vector<T> output;
        output.reserve( count );

        for( std::size_t index = 0; index < count; ++index )
        {
            output.push_back( lerp( a[ index ], b[ index ], weight ) );
        }

        return output;
    }

    filetypes::xg::Vertex lerp_vertex( const filetypes::xg::Vertex &a, const filetypes::xg::Vertex &b, float weight )
    {
        filetypes::xg::Vertex output = a;
        output.position = filetypes::xg::lerp( a.position, b.position, weight );

        output.normal = filetypes::xg::lerp( a.normal, b.normal, weight );

        output.color = rendering::lerp_color( a.color, b.color, weight );

        output.uv = rendering::lerp_point( a.uv, b.uv, weight );

        output.has_uv = a.has_uv || b.has_uv;
        return output;
    }

    std::vector<SkinBinding> build_skin_bindings( const filetypes::xg::Model &model,
        const filetypes::xg::Geometry &geometry,
        const std::unordered_map<std::string, std::uint16_t> &bone_indices )
    {
        std::vector<SkinBinding> bindings( geometry.vertices.size() );

        for( const std::string &input_node : geometry.input_geometry_nodes )
        {
            const filetypes::xg::Envelope *envelope = model.find_envelope( input_node );

            if( envelope == nullptr || envelope->input_geometry.empty() )
            {
                continue;
            }

            const filetypes::xg::Geometry *source_geometry = model.find_geometry( envelope->input_geometry );

            if( source_geometry == nullptr )
            {
                continue;
            }

            const std::size_t count = std::min( envelope->weights.size(), envelope->vertex_targets.size() );

            for( std::size_t envelope_index = 0; envelope_index < count; ++envelope_index )
            {
                const std::size_t source_index = static_cast<std::size_t>( envelope->start_vertex ) + envelope_index;

                if( source_index >= source_geometry->vertices.size() )
                {
                    continue;
                }

                const filetypes::xg::Vec4 source_weights = envelope->weights[ envelope_index ];
                const std::array<float, 4>
                    weights{
                    source_weights.x,
                    source_weights.y,
                    source_weights.z,
                    source_weights.w
                };

                SkinBinding binding{};
                binding.source = source_geometry->vertices[ source_index ];
                float total = 0.0f;

                for( std::size_t slot = 0; slot < 4u; ++slot )
                {
                    if( std::abs( weights[ slot ] ) <= 0.000001f || envelope->input_matrices[ slot ].empty() )
                    {
                        continue;
                    }

                    const auto bone = bone_indices.find( envelope->input_matrices[ slot ] );

                    if( bone == bone_indices.end() )
                    {
                        continue;
                    }

                    binding.joints[ slot ] = bone->second;

                    if( slot == 0u )
                    {
                        binding.weights.x = weights[ slot ];
                    }

                    if( slot == 1u )
                    {
                        binding.weights.y = weights[ slot ];
                    }

                    if( slot == 2u )
                    {
                        binding.weights.z = weights[ slot ];
                    }

                    if( slot == 3u )
                    {
                        binding.weights.w = weights[ slot ];
                    }

                    total += weights[ slot ];
                }

                if( total <= 0.000001f )
                {
                    continue;
                }

                binding.weights.x /= total;

                binding.weights.y /= total;

                binding.weights.z /= total;

                binding.weights.w /= total;

                binding.valid = true;

                for( const std::uint32_t target_index : envelope->vertex_targets[ envelope_index ] )
                {
                    if( target_index < bindings.size() )
                    {
                        bindings[ target_index ] = binding;
                    }
                }
            }
        }

        return bindings;
    }

    std::vector<filetypes::xg::Vec3> geometry_positions( const filetypes::xg::Geometry &geometry )
    {
        std::vector<filetypes::xg::Vec3> values;
        values.reserve( geometry.vertices.size() );

        for( const filetypes::xg::Vertex &vertex : geometry.vertices )
        {
            values.push_back( vertex.position );
        }

        return values;
    }

    std::vector<filetypes::xg::Vec3> geometry_normals( const filetypes::xg::Geometry &geometry )
    {
        std::vector<filetypes::xg::Vec3> values;
        values.reserve( geometry.vertices.size() );

        for( const filetypes::xg::Vertex &vertex : geometry.vertices )
        {
            values.push_back( vertex.normal );
        }

        return values;
    }

    std::vector<float> flatten_vec3( const std::vector<filetypes::xg::Vec3> &values )
    {
        std::vector<float> output;
        output.reserve( values.size() * 3u );

        for( const filetypes::xg::Vec3 &value : values )
        {
            output.insert( output.end(), { value.x, value.y, value.z } );
        }

        return output;
    }

    std::vector<float> flatten_vec2( const std::vector<rendering::Vec2> &values )
    {
        std::vector<float> output;
        output.reserve( values.size() * 2u );

        for( const rendering::Vec2 &value : values )
        {
            output.insert( output.end(), { value.x, value.y } );
        }

        return output;
    }

    struct ResolvedGeometryFrame
    {
        std::vector<filetypes::xg::Vec3> positions;
        std::vector<filetypes::xg::Vec3> normals;
        std::vector<rendering::Vec2> texcoords;
    };

    ResolvedGeometryFrame resolve_geometry_frame( const filetypes::xg::Model &model,
        const filetypes::xg::Geometry &geometry,
        float frame )
    {
        std::vector<filetypes::xg::Vertex> vertices = geometry.vertices;

        for( const std::string &input_node : geometry.input_geometry_nodes )
        {
            const filetypes::xg::ShapeInterpolator *shape = model.find_shape_interpolator( input_node );

            if( shape == nullptr )
            {
                continue;
            }

            const std::vector<filetypes::xg::Vertex> sampled =
                sample_track( shape->keys, shape->times, shape->type, frame, lerp_vertex );

            if( sampled.empty() )
            {
                continue;
            }

            const std::size_t count = std::min( vertices.size(), sampled.size() );
            std::copy_n( sampled.begin(), count, vertices.begin() );
        }

        for( const std::string &input_node : geometry.input_geometry_nodes )
        {
            if( const auto vertex_interpolator = model.find_vertex_interpolator( input_node ) )
            {
                const std::vector<filetypes::xg::Vec3> sampled = sample_track( vertex_interpolator->keys,
                    vertex_interpolator->times,
                    vertex_interpolator->type,
                    frame,
                    filetypes::xg::lerp );

                const std::size_t count = vertex_interpolator->targets.empty()
                                              ? std::min( sampled.size(), vertices.size() )
                                              : std::min( sampled.size(), vertex_interpolator->targets.size() );

                for( std::size_t index = 0; index < count; ++index )
                {
                    const std::size_t target = vertex_interpolator->targets.empty()
                                                   ? index
                                                   : static_cast<std::size_t>( vertex_interpolator->targets[ index ] );

                    if( target < vertices.size() )
                    {
                        vertices[ target ].position = sampled[ index ];
                    }
                }
            }
            else if( const auto normal_interpolator = model.find_normal_interpolator( input_node ) )
            {
                const std::vector<filetypes::xg::Vec3> sampled = sample_track( normal_interpolator->keys,
                    normal_interpolator->times,
                    normal_interpolator->type,
                    frame,
                    filetypes::xg::lerp );

                const std::size_t count = normal_interpolator->targets.empty()
                                              ? std::min( sampled.size(), vertices.size() )
                                              : std::min( sampled.size(), normal_interpolator->targets.size() );

                for( std::size_t index = 0; index < count; ++index )
                {
                    const std::size_t target = normal_interpolator->targets.empty()
                                                   ? index
                                                   : static_cast<std::size_t>( normal_interpolator->targets[ index ] );

                    if( target < vertices.size() )
                    {
                        vertices[ target ].normal = sampled[ index ];
                    }
                }
            }
            else if( const auto texcoord_interpolator = model.find_texcoord_interpolator( input_node ) )
            {
                const std::vector<rendering::Vec2> sampled = sample_track( texcoord_interpolator->keys,
                    texcoord_interpolator->times,
                    texcoord_interpolator->type,
                    frame,
                    rendering::lerp_point );

                const std::size_t count = texcoord_interpolator->targets.empty()
                                              ? std::min( sampled.size(), vertices.size() )
                                              : std::min( sampled.size(), texcoord_interpolator->targets.size() );

                for( std::size_t index = 0; index < count; ++index )
                {
                    const std::size_t target =
                        texcoord_interpolator->targets.empty()
                            ? index
                            : static_cast<std::size_t>( texcoord_interpolator->targets[ index ] );

                    if( target < vertices.size() )
                    {
                        vertices[ target ].uv = sampled[ index ];

                        vertices[ target ].has_uv = true;
                    }
                }
            }
        }

        ResolvedGeometryFrame output{};
        output.positions.reserve( vertices.size() );
        output.normals.reserve( vertices.size() );
        output.texcoords.reserve( vertices.size() );

        for( const filetypes::xg::Vertex &vertex : vertices )
        {
            output.positions.push_back( vertex.position );
            output.normals.push_back( vertex.normal );
            output.texcoords.push_back( vertex.has_uv ? vertex.uv : rendering::Vec2{} );
        }

        return output;
    }

    void append_track_times( std::vector<float> &destination, const std::vector<float> &times, std::size_t count )
    {
        const std::vector<float> resolved = source_key_times( times, count );
        destination.insert( destination.end(), resolved.begin(), resolved.end() );
    }

    void sort_unique_track_times( std::vector<float> &times )
    {
        std::sort( times.begin(), times.end() );
        times.erase( std::unique( times.begin(),
                         times.end(),
                         []( float a, float b )
                         {
                             return std::abs( a - b ) <= 0.0001f;
                         } ),
            times.end() );
    }

    void build_geometry_tracks( Document &document,
        const filetypes::xg::Model &model,
        const filetypes::xg::Geometry &geometry,
        Primitive &primitive )
    {
        std::vector<float> position_times;
        std::vector<float> normal_times;
        std::vector<float> texcoord_times;
        bool position_linear = false;
        bool normal_linear = false;
        bool texcoord_linear = false;

        for( const std::string &input_node : geometry.input_geometry_nodes )
        {
            if( const auto shape_interpolator = model.find_shape_interpolator( input_node ) )
            {
                append_track_times( position_times, shape_interpolator->times, shape_interpolator->keys.size() );
                append_track_times( normal_times, shape_interpolator->times, shape_interpolator->keys.size() );
                append_track_times( texcoord_times, shape_interpolator->times, shape_interpolator->keys.size() );
                position_linear = position_linear || shape_interpolator->type != 0u;

                normal_linear = normal_linear || shape_interpolator->type != 0u;

                texcoord_linear = texcoord_linear || shape_interpolator->type != 0u;
            }
            else if( const auto vertex_interpolator = model.find_vertex_interpolator( input_node ) )
            {
                append_track_times( position_times, vertex_interpolator->times, vertex_interpolator->keys.size() );
                position_linear = position_linear || vertex_interpolator->type != 0u;
            }
            else if( const auto normal_interpolator = model.find_normal_interpolator( input_node ) )
            {
                append_track_times( normal_times, normal_interpolator->times, normal_interpolator->keys.size() );
                normal_linear = normal_linear || normal_interpolator->type != 0u;
            }
            else if( const auto texcoord_interpolator = model.find_texcoord_interpolator( input_node ) )
            {
                append_track_times( texcoord_times, texcoord_interpolator->times, texcoord_interpolator->keys.size() );
                texcoord_linear = texcoord_linear || texcoord_interpolator->type != 0u;
            }
        }

        sort_unique_track_times( position_times );
        sort_unique_track_times( normal_times );
        sort_unique_track_times( texcoord_times );

        primitive.position_track.interpolation_type = position_linear ? 1u : 0u;

        primitive.position_track.times = position_times;

        for( const float frame : position_times )
        {
            const ResolvedGeometryFrame resolved = resolve_geometry_frame( model, geometry, frame );
            primitive.position_track.accessors.push_back(
                append_vector( document, flatten_vec3( resolved.positions ), component_float, "VEC3", 0 ) );
        }

        primitive.normal_track.interpolation_type = normal_linear ? 1u : 0u;

        primitive.normal_track.times = normal_times;

        for( const float frame : normal_times )
        {
            const ResolvedGeometryFrame resolved = resolve_geometry_frame( model, geometry, frame );
            primitive.normal_track.accessors.push_back(
                append_vector( document, flatten_vec3( resolved.normals ), component_float, "VEC3", 0 ) );
        }

        primitive.texcoord_track.interpolation_type = texcoord_linear ? 1u : 0u;

        primitive.texcoord_track.times = texcoord_times;

        for( const float frame : texcoord_times )
        {
            const ResolvedGeometryFrame resolved = resolve_geometry_frame( model, geometry, frame );
            primitive.texcoord_track.accessors.push_back(
                append_vector( document, flatten_vec2( resolved.texcoords ), component_float, "VEC2", 0 ) );
        }
    }

    std::vector<float> geometry_morph_times( const Primitive &primitive )
    {
        std::vector<float> times = primitive.position_track.times;
        times.insert( times.end(), primitive.normal_track.times.begin(), primitive.normal_track.times.end() );
        std::sort( times.begin(), times.end() );
        times.erase( std::unique( times.begin(),
                         times.end(),
                         []( float a, float b )
                         {
                             return std::abs( a - b ) <= 0.0001f;
                         } ),
            times.end() );
        return times;
    }

    void build_standard_morph_targets( Document &document,
        const filetypes::xg::Model &model,
        const filetypes::xg::Geometry &geometry,
        Primitive &primitive,
        Mesh &mesh,
        std::int32_t mesh_node_index )
    {
        const std::vector<float> times = geometry_morph_times( primitive );

        if( times.empty() )
        {
            return;
        }

        const std::vector<filetypes::xg::Vec3> base_positions = geometry_positions( geometry );
        const std::vector<filetypes::xg::Vec3> base_normals = geometry_normals( geometry );

        for( const float frame : times )
        {
            const ResolvedGeometryFrame resolved = resolve_geometry_frame( model, geometry, frame );
            Primitive::MorphTarget target{};

            if( !primitive.position_track.times.empty() )
            {
                std::vector<float> deltas;
                deltas.reserve( resolved.positions.size() * 3u );
                const std::size_t count = std::min( resolved.positions.size(), base_positions.size() );

                for( std::size_t index = 0; index < count; ++index )
                {
                    deltas.insert( deltas.end(),
                        { resolved.positions[ index ].x - base_positions[ index ].x,
                            resolved.positions[ index ].y - base_positions[ index ].y,
                            resolved.positions[ index ].z - base_positions[ index ].z } );
                }

                target.position = append_vector( document, deltas, component_float, "VEC3", 0 );
            }

            if( !primitive.normal_track.times.empty() )
            {
                std::vector<float> deltas;
                deltas.reserve( resolved.normals.size() * 3u );
                const std::size_t count = std::min( resolved.normals.size(), base_normals.size() );

                for( std::size_t index = 0; index < count; ++index )
                {
                    deltas.insert( deltas.end(),
                        { resolved.normals[ index ].x - base_normals[ index ].x,
                            resolved.normals[ index ].y - base_normals[ index ].y,
                            resolved.normals[ index ].z - base_normals[ index ].z } );
                }

                target.normal = append_vector( document, deltas, component_float, "VEC3", 0 );
            }

            primitive.morph_targets.push_back( target );
            mesh.weights.push_back( 0.0 );
            std::ostringstream target_name;
            target_name << "frame_" << std::setprecision( 6 ) << frame;
            mesh.target_names.push_back( target_name.str() );
        }

        Document::PendingWeightAnimation pending{};
        pending.node = mesh_node_index;
        pending.times.reserve( times.size() );

        for( const float frame : times )
        {
            pending.times.push_back( frame / frame_rate );
        }

        pending.weights.assign( times.size() * times.size(), 0.0f );

        for( std::size_t index = 0; index < times.size(); ++index )
        {
            pending.weights[ index * times.size() + index ] = 1.0f;
        }

        document.pending_weight_animations.push_back( std::move( pending ) );
    }

    std::unordered_map<std::string, std::string> mesh_matrix_parents( const filetypes::xg::Model &model )
    {
        std::unordered_map<std::string, std::string> parent;

        for( const filetypes::xg::DagNode &node : model.dag_nodes )
        {
            for( const std::string &child : node.children )
            {
                parent[ child ] = node.name;
            }
        }

        std::unordered_map<std::string, std::string> output;

        for( const filetypes::xg::DagMesh &mesh : model.meshes )
        {
            std::string cursor = mesh.name;
            std::unordered_set<std::string> visited;
            while( visited.insert( cursor ).second )
            {
                const auto parent_it = parent.find( cursor );

                if( parent_it == parent.end() )
                {
                    break;
                }

                cursor = parent_it->second;

                if( const filetypes::xg::DagTransform *transform = model.find_transform( cursor ) )
                {
                    output[ mesh.name ] = transform->matrix_name;
                    break;
                }
            }
        }

        return output;
    }

    std::int32_t find_texture_metadata( const filetypes::xgm::Metadata &metadata, std::string_view texture_name )
    {
        const std::string normalized = normalize_name( std::string( texture_name ) );

        for( std::size_t index = 0; index < metadata.textures.size(); ++index )
        {
            if( normalize_name( metadata.textures[ index ].filename ) == normalized )
            {
                return static_cast<std::int32_t>( index );
            }
        }

        return -1;
    }

    void write_binary( const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes )
    {
        std::ofstream file( path, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not create glTF binary buffer: " + path.string() );
        }

        if( !bytes.empty() )
        {
            file.write( reinterpret_cast<const char *>( bytes.data() ), static_cast<std::streamsize>( bytes.size() ) );
        }

        if( !file )
        {
            throw std::runtime_error( "Could not write glTF binary buffer: " + path.string() );
        }
    }

    void write_geometry_track( std::ostream &output,
        std::string_view name,
        const Primitive::GeometryTrack &track,
        bool &wrote_track )
    {
        if( track.accessors.empty() )
        {
            return;
        }

        if( wrote_track )
        {
            output << ',';
        }

        output << "\"" << name << "\":{\"interpolation_type\":" << track.interpolation_type << ",\"times\":";
        gltf::write_float_array( output, track.times );
        output << ",\"accessors\":";
        gltf::write_int_array( output, track.accessors );
        output << '}';
        wrote_track = true;
    }

    void write_document( const Document &document,
        const filetypes::xgm::ModelMetadata &metadata,
        const std::filesystem::path &gltf_path )
    {
        std::ofstream output( gltf_path, std::ios::binary );

        if( !output )
        {
            throw std::runtime_error( "Could not create glTF file: " + gltf_path.string() );
        }

        output << "{\n";
        output << "  \"asset\": {\n";
        output << "    \"version\": \"2.0\",\n";
        output << "    \"generator\": \"Piko Runtime Converter\",\n";
        output << "    \"extras\": {\n";
        output << "      \"piko\": {\n";
        output << "        \"format\": \"piko-xg-model\",\n";
        output << "        \"version\": 2,\n";
        output << "        \"source_model\": \""
               << json_util::escape_string( metadata.filename, json_util::EscapeMode::Ascii ) << "\",\n";

        output << "        \"frame_rate\": " << frame_rate << ",\n";
        output << "        \"global_animation\": 0,\n";
        output << "        \"animation_timings\": [\n";

        for( std::size_t index = 0; index < metadata.animations.size(); ++index )
        {
            const filetypes::xgm::AnimationEntry &animation = metadata.animations[ index ];
            output << "          {\"index\":" << index << ",\"playback_length\":" << std::setprecision( 9 )
                   << animation.playback_length << ",\"keyframe_interval\":" << animation.keyframe_interval
                   << ",\"start_keyframe\":" << animation.start_keyframe << ",\"speed_mode\":" << animation.speed_mode
                   << '}';

            output << ( index + 1u == metadata.animations.size() ? "\n" : ",\n" );
        }

        output << "        ]\n";
        output << "      }\n";
        output << "    }\n";
        output << "  },\n";
        output << "  \"scene\": 0,\n";
        output << "  \"scenes\": [{\"name\":\"Scene\",\"nodes\":";
        gltf::write_int_array( output, document.scene_nodes );
        output << "}],\n";

        output << "  \"nodes\": [\n";

        for( std::size_t index = 0; index < document.nodes.size(); ++index )
        {
            const Node &node = document.nodes[ index ];
            output << "    {\"name\":\"" << json_util::escape_string( node.name, json_util::EscapeMode::Ascii ) << "\"";

            if( node.mesh >= 0 )
            {
                output << ",\"mesh\":" << node.mesh;
            }

            if( node.skin >= 0 )
            {
                output << ",\"skin\":" << node.skin;
            }

            if( !node.children.empty() )
            {
                output << ",\"children\":";
                gltf::write_int_array( output, node.children );
            }

            output << ",\"translation\":";
            gltf::write_double_array( output, node.translation );
            output << ",\"rotation\":";
            gltf::write_double_array( output, node.rotation );
            output << ",\"scale\":";
            gltf::write_double_array( output, node.scale );
            output << '}';
            output << ( index + 1u == document.nodes.size() ? "\n" : ",\n" );
        }

        output << "  ],\n";

        output << "  \"meshes\": [\n";

        for( std::size_t mesh_index = 0; mesh_index < document.meshes.size(); ++mesh_index )
        {
            const Mesh &mesh = document.meshes[ mesh_index ];
            output << "    {\"name\":\"" << json_util::escape_string( mesh.name, json_util::EscapeMode::Ascii )
                   << "\",\"primitives\":[\n";

            for( std::size_t primitive_index = 0; primitive_index < mesh.primitives.size(); ++primitive_index )
            {
                const Primitive &primitive = mesh.primitives[ primitive_index ];
                output << "      {\"attributes\":{\"POSITION\":" << primitive.position;

                if( primitive.normal >= 0 )
                {
                    output << ",\"NORMAL\":" << primitive.normal;
                }

                if( primitive.texcoord >= 0 )
                {
                    output << ",\"TEXCOORD_0\":" << primitive.texcoord;
                }

                if( primitive.color >= 0 )
                {
                    output << ",\"COLOR_0\":" << primitive.color;
                }

                if( primitive.joints >= 0 )
                {
                    output << ",\"JOINTS_0\":" << primitive.joints;
                }

                if( primitive.weights >= 0 )
                {
                    output << ",\"WEIGHTS_0\":" << primitive.weights;
                }

                output << "},\"indices\":" << primitive.indices << ",\"material\":" << primitive.material
                       << ",\"mode\":" << mode_triangles;

                if( !primitive.morph_targets.empty() )
                {
                    output << ",\"targets\":[";

                    for( std::size_t target_index = 0; target_index < primitive.morph_targets.size(); ++target_index )
                    {
                        const Primitive::MorphTarget &target = primitive.morph_targets[ target_index ];
                        output << '{';
                        bool wrote_attribute = false;

                        if( target.position >= 0 )
                        {
                            output << "\"POSITION\":" << target.position;
                            wrote_attribute = true;
                        }

                        if( target.normal >= 0 )
                        {
                            if( wrote_attribute )
                            {
                                output << ',';
                            }

                            output << "\"NORMAL\":" << target.normal;
                        }

                        output << '}';

                        if( target_index + 1u != primitive.morph_targets.size() )
                        {
                            output << ',';
                        }
                    }

                    output << ']';
                }

                output << ",\"extras\":{\"piko\":{\"cull_func\":" << primitive.cull_func
                       << ",\"skinned\":" << ( primitive.skinned ? "true" : "false" );

                bool wrote_track = false;

                if( !primitive.position_track.accessors.empty() || !primitive.normal_track.accessors.empty() ||
                    !primitive.texcoord_track.accessors.empty() )
                {
                    output << ",\"geometry_animation\":{";
                    write_geometry_track( output, "position", primitive.position_track, wrote_track );
                    write_geometry_track( output, "normal", primitive.normal_track, wrote_track );
                    write_geometry_track( output, "texcoord", primitive.texcoord_track, wrote_track );
                    output << '}';
                }

                output << "}}}";
                output << ( primitive_index + 1u == mesh.primitives.size() ? "\n" : ",\n" );
            }

            output << "    ]";

            if( !mesh.weights.empty() )
            {
                output << ",\"weights\":";
                gltf::write_double_array( output, mesh.weights );
            }

            if( !mesh.target_names.empty() )
            {
                output << ",\"extras\":{\"targetNames\":[";

                for( std::size_t target_index = 0; target_index < mesh.target_names.size(); ++target_index )
                {
                    if( target_index != 0u )
                    {
                        output << ',';
                    }

                    output << "\""
                           << json_util::escape_string( mesh.target_names[ target_index ],
                                  json_util::EscapeMode::Ascii )
                           << "\"";
                }

                output << "]}";
            }

            output << '}';
            output << ( mesh_index + 1u == document.meshes.size() ? "\n" : ",\n" );
        }

        output << "  ],\n";

        output << "  \"materials\": [\n";

        for( std::size_t index = 0; index < document.materials.size(); ++index )
        {
            const Material &material = document.materials[ index ];
            output << "    {\"name\":\"" << json_util::escape_string( material.name, json_util::EscapeMode::Ascii )
                   << "\",\"pbrMetallicRoughness\":{\"baseColorFactor\":";

            gltf::write_double_array( output, material.diffuse );
            output << ",\"metallicFactor\":0,\"roughnessFactor\":1";

            if( material.texture >= 0 )
            {
                output << ",\"baseColorTexture\":{\"index\":" << material.texture << '}';
            }

            output << "},\"alphaMode\":\"" << ( material.blend_type == 0u ? "OPAQUE" : "BLEND" )
                   << "\",\"doubleSided\":true,\"extras\":{\"piko\":{"
                   << "\"blend_type\":" << material.blend_type << ",\"shading_type\":" << material.shading_type
                   << ",\"flags\":" << material.flags << ",\"texture_env\":" << material.texture_env
                   << ",\"u_tile\":" << material.u_tile << ",\"v_tile\":" << material.v_tile
                   << ",\"uses_texture_alpha\":" << ( material.uses_texture_alpha ? "true" : "false" )
                   << ",\"wrap_texture\":" << ( material.wrap_texture ? "true" : "false" ) << "}}}";

            output << ( index + 1u == document.materials.size() ? "\n" : ",\n" );
        }

        output << "  ],\n";

        output << "  \"samplers\": [{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":10497,\"wrapT\":10497}],\n";
        output << "  \"images\": [\n";

        for( std::size_t index = 0; index < document.images.size(); ++index )
        {
            const Image &image = document.images[ index ];
            output << "    {\"name\":\"" << json_util::escape_string( image.name, json_util::EscapeMode::Ascii )
                   << "\",\"uri\":\"" << json_util::escape_string( image.uri, json_util::EscapeMode::Ascii ) << "\"}";

            output << ( index + 1u == document.images.size() ? "\n" : ",\n" );
        }

        output << "  ],\n";

        output << "  \"textures\": [\n";

        for( std::size_t index = 0; index < document.textures.size(); ++index )
        {
            const Texture &texture = document.textures[ index ];
            output << "    {\"source\":" << texture.source << ",\"sampler\":" << texture.sampler << '}';
            output << ( index + 1u == document.textures.size() ? "\n" : ",\n" );
        }

        output << "  ],\n";

        output << "  \"skins\": [\n";

        for( std::size_t index = 0; index < document.skins.size(); ++index )
        {
            const Skin &skin = document.skins[ index ];
            output << "    {\"name\":\"" << json_util::escape_string( skin.name, json_util::EscapeMode::Ascii )
                   << "\",\"inverseBindMatrices\":" << skin.inverse_bind_matrices << ",\"joints\":";

            gltf::write_int_array( output, skin.joints );
            output << '}';
            output << ( index + 1u == document.skins.size() ? "\n" : ",\n" );
        }

        output << "  ],\n";

        output << "  \"animations\": [\n";

        for( std::size_t animation_index = 0; animation_index < document.animations.size(); ++animation_index )
        {
            const Animation &animation = document.animations[ animation_index ];
            output << "    {\"name\":\"" << json_util::escape_string( animation.name, json_util::EscapeMode::Ascii )
                   << "\",\"samplers\":[\n";

            for( std::size_t sampler_index = 0; sampler_index < animation.samplers.size(); ++sampler_index )
            {
                const AnimationSampler &sampler = animation.samplers[ sampler_index ];
                output << "      {\"input\":" << sampler.input << ",\"output\":" << sampler.output
                       << ",\"interpolation\":\"" << sampler.interpolation << "\"}";

                output << ( sampler_index + 1u == animation.samplers.size() ? "\n" : ",\n" );
            }

            output << "    ],\"channels\":[\n";

            for( std::size_t channel_index = 0; channel_index < animation.channels.size(); ++channel_index )
            {
                const AnimationChannel &channel = animation.channels[ channel_index ];
                output << "      {\"sampler\":" << channel.sampler << ",\"target\":{\"node\":" << channel.node
                       << ",\"path\":\"" << channel.path << "\"}}";

                output << ( channel_index + 1u == animation.channels.size() ? "\n" : ",\n" );
            }

            output << "    ]}";
            output << ( animation_index + 1u == document.animations.size() ? "\n" : ",\n" );
        }

        output << "  ],\n";

        output << "  \"bufferViews\": [\n";

        for( std::size_t index = 0; index < document.buffer_views.size(); ++index )
        {
            const BufferView &view = document.buffer_views[ index ];
            output << "    {\"buffer\":0,\"byteOffset\":" << view.byte_offset << ",\"byteLength\":" << view.byte_length;

            if( view.target != 0 )
            {
                output << ",\"target\":" << view.target;
            }

            output << '}';
            output << ( index + 1u == document.buffer_views.size() ? "\n" : ",\n" );
        }

        output << "  ],\n";

        output << "  \"accessors\": [\n";

        for( std::size_t index = 0; index < document.accessors.size(); ++index )
        {
            const Accessor &accessor = document.accessors[ index ];
            output << "    {\"bufferView\":" << accessor.buffer_view << ",\"byteOffset\":" << accessor.byte_offset
                   << ",\"componentType\":" << accessor.component_type << ",\"count\":" << accessor.count
                   << ",\"type\":\"" << accessor.type << "\"";

            if( !accessor.minimum.empty() )
            {
                output << ",\"min\":";
                gltf::write_double_array( output, accessor.minimum );
            }

            if( !accessor.maximum.empty() )
            {
                output << ",\"max\":";
                gltf::write_double_array( output, accessor.maximum );
            }

            output << '}';
            output << ( index + 1u == document.accessors.size() ? "\n" : ",\n" );
        }

        output << "  ],\n";
        output << "  \"buffers\": [{\"uri\":\"model.bin\",\"byteLength\":" << document.buffer.size() << "}]\n";
        output << "}\n";

        if( !output )
        {
            throw std::runtime_error( "Could not write glTF file: " + gltf_path.string() );
        }
    }

    bool convert_model( std::span<const std::uint8_t> xgm_bytes,
        const filetypes::xgm::Metadata &package_metadata,
        const filetypes::xgm::ModelMetadata &model_metadata,
        const std::filesystem::path &output_root )
    {
        if( model_metadata.data_offset > xgm_bytes.size() ||
            model_metadata.size > xgm_bytes.size() - model_metadata.data_offset )
        {
            throw std::runtime_error( "XG model extends beyond its XGM package" );
        }

        const filetypes::xg::Model model =
            filetypes::xg::decode( xgm_bytes.subspan( model_metadata.data_offset, model_metadata.size ) );

        if( model.geometries.empty() || model.meshes.empty() )
        {
            return false;
        }

        const std::string output_name = model_stem( model_metadata.filename );
        const std::filesystem::path model_root = output_root / output_name;
        const std::filesystem::path texture_root = model_root / "textures";
        std::filesystem::create_directories( texture_root );

        Document document{};
        std::unordered_map<std::string, std::int32_t> texture_lookup;
        std::unordered_map<std::string, std::int32_t> material_lookup;
        std::unordered_set<std::string> repeat_materials;

        for( const filetypes::xg::DagMesh &source_mesh : model.meshes )
        {
            const filetypes::xg::Geometry *geometry = model.find_geometry( source_mesh.geometry_name );

            if( geometry == nullptr || !geometry_requires_texture_repeat( model, *geometry ) )
            {
                continue;
            }

            if( const filetypes::xg::MultiPassMaterial *multipass =
                    model.find_multipass_material( source_mesh.material_name ) )
            {
                repeat_materials.insert( multipass->material_names.begin(), multipass->material_names.end() );
            }
            else
            {
                repeat_materials.insert( source_mesh.material_name );
            }
        }

        for( const filetypes::xg::Material &source_material : model.materials )
        {
            Material material{};
            material.name = source_material.name;

            material.diffuse = {
                source_material.diffuse.r,
                source_material.diffuse.g,
                source_material.diffuse.b,
                source_material.diffuse.a
            };

            material.blend_type = source_material.blend_type;

            material.shading_type = source_material.shading_type;

            material.flags = source_material.flags;

            material.texture_env = source_material.texture_env;

            material.u_tile = source_material.u_tile;

            material.v_tile = source_material.v_tile;

            material.uses_texture_alpha = source_material.uses_texture_alpha;
            const bool alpha_active = source_material.blend_type == 1u || source_material.blend_type == 5u ||
                                      ( ( source_material.blend_type == 0u || source_material.blend_type == 4u ) &&
                                          ( source_material.flags & 0x01u ) != 0u );

            const bool blend_pass = alpha_active || source_material.blend_type == 2u ||
                                    source_material.blend_type == 3u || source_material.blend_type == 4u;

            material.wrap_texture = source_material.texture_env == 0u &&
                                    ( source_material.u_tile != 0u || source_material.v_tile != 0u || !blend_pass ||
                                        repeat_materials.contains( source_material.name ) );

            if( !source_material.texture_name.empty() )
            {
                const std::string key = normalize_name( source_material.texture_name );

                const auto existing = texture_lookup.find( key );

                if( existing != texture_lookup.end() )
                {
                    material.texture = existing->second;
                }
                else
                {
                    const std::int32_t texture_index =
                        find_texture_metadata( package_metadata, source_material.texture_name );

                    if( texture_index >= 0 )
                    {
                        const filetypes::xgm::TextureMetadata &texture_metadata =
                            package_metadata.textures[ static_cast<std::size_t>( texture_index ) ];

                        if( texture_metadata.data_offset <= xgm_bytes.size() &&
                            texture_metadata.size <= xgm_bytes.size() - texture_metadata.data_offset )
                        {
                            const std::string png_name = safe_stem( texture_metadata.filename ) + ".png";
                            convert_imx_to_png(
                                xgm_bytes.subspan( texture_metadata.data_offset, texture_metadata.size ),
                                texture_root / png_name,
                                texture_metadata.filename );

                            const std::int32_t image_index = static_cast<std::int32_t>( document.images.size() );
                            document.images.push_back( Image{ texture_metadata.filename, "textures/" + png_name } );
                            const std::int32_t gltf_texture_index =
                                static_cast<std::int32_t>( document.textures.size() );

                            document.textures.push_back( Texture{ image_index, 0 } );
                            texture_lookup.emplace( key, gltf_texture_index );
                            material.texture = gltf_texture_index;
                        }
                    }
                }
            }

            const std::int32_t material_index = static_cast<std::int32_t>( document.materials.size() );
            material_lookup.emplace( source_material.name, material_index );
            document.materials.push_back( std::move( material ) );
        }

        std::unordered_map<std::string, std::int32_t> matrix_node_indices;
        std::vector<std::int32_t> node_parent;
        node_parent.reserve( model.matrix_nodes.size() + model.meshes.size() );

        for( const filetypes::xg::MatrixNode &source_node : model.matrix_nodes )
        {
            const filetypes::xg::Quat rotation = convert_rotation( source_node.rotation );
            Node node{};
            node.name = source_node.name;

            node.translation = { source_node.position.x, source_node.position.y, -source_node.position.z };

            node.rotation = { rotation.x, rotation.y, rotation.z, rotation.w };

            node.scale = { source_node.scale.x, source_node.scale.y, source_node.scale.z };
            const std::int32_t node_index = static_cast<std::int32_t>( document.nodes.size() );
            document.nodes.push_back( std::move( node ) );
            node_parent.push_back( -1 );
            matrix_node_indices.emplace( source_node.name, node_index );
        }

        for( const filetypes::xg::MatrixNode &source_node : model.matrix_nodes )
        {
            if( source_node.parent_matrix.empty() )
            {
                continue;
            }

            const auto child = matrix_node_indices.find( source_node.name );

            const auto parent = matrix_node_indices.find( source_node.parent_matrix );

            if( child == matrix_node_indices.end() || parent == matrix_node_indices.end() )
            {
                continue;
            }

            document.nodes[ static_cast<std::size_t>( parent->second ) ].children.push_back( child->second );
            node_parent[ static_cast<std::size_t>( child->second ) ] = parent->second;
        }

        std::unordered_map<std::string, std::uint16_t> bone_indices;
        Skin skin{};
        skin.name = output_name + "_Skeleton";
        std::vector<float> inverse_bind_values;
        inverse_bind_values.reserve( model.bones.size() * 16u );

        for( std::size_t bone_index = 0; bone_index < model.bones.size(); ++bone_index )
        {
            const filetypes::xg::Bone &bone = model.bones[ bone_index ];

            const auto node = matrix_node_indices.find( bone.input_matrix );

            if( node == matrix_node_indices.end() )
            {
                continue;
            }

            if( skin.joints.size() >= std::numeric_limits<std::uint16_t>::max() )
            {
                throw std::runtime_error( "U-1 has too many bones for glTF JOINTS_0" );
            }

            const std::uint16_t joint_index = static_cast<std::uint16_t>( skin.joints.size() );
            bone_indices.emplace( bone.name, joint_index );
            skin.joints.push_back( node->second );
            inverse_bind_values.insert( inverse_bind_values.end(),
                std::begin( bone.rest_matrix.m ),
                std::end( bone.rest_matrix.m ) );
        }

        if( !skin.joints.empty() )
        {
            skin.inverse_bind_matrices = append_vector( document, inverse_bind_values, component_float, "MAT4", 0 );
            document.skins.push_back( std::move( skin ) );
        }

        const std::unordered_map<std::string, std::string> matrix_parents = mesh_matrix_parents( model );

        for( const filetypes::xg::DagMesh &source_mesh : model.meshes )
        {
            const filetypes::xg::Geometry *geometry = model.find_geometry( source_mesh.geometry_name );

            if( geometry == nullptr || geometry->vertices.empty() || source_mesh.triangle_indices.empty() )
            {
                continue;
            }

            const std::vector<SkinBinding> bindings = build_skin_bindings( model, *geometry, bone_indices );
            const bool skinned = std::any_of( bindings.begin(),
                bindings.end(),
                []( const SkinBinding &binding )
                {
                    return binding.valid;
                } );

            std::vector<float> positions;
            std::vector<float> normals;
            std::vector<float> texcoords;
            std::vector<float> colors;
            std::vector<std::uint16_t> joints;
            std::vector<float> weights;
            positions.reserve( geometry->vertices.size() * 3u );
            normals.reserve( geometry->vertices.size() * 3u );
            texcoords.reserve( geometry->vertices.size() * 2u );
            colors.reserve( geometry->vertices.size() * 4u );

            if( skinned )
            {
                joints.reserve( geometry->vertices.size() * 4u );
                weights.reserve( geometry->vertices.size() * 4u );
            }

            for( std::size_t vertex_index = 0; vertex_index < geometry->vertices.size(); ++vertex_index )
            {
                const filetypes::xg::Vertex &target = geometry->vertices[ vertex_index ];
                const SkinBinding &binding = bindings[ vertex_index ];
                const filetypes::xg::Vertex &source = binding.valid ? binding.source : target;
                positions.insert( positions.end(), { source.position.x, source.position.y, source.position.z } );
                normals.insert( normals.end(), { target.normal.x, target.normal.y, target.normal.z } );
                texcoords.insert( texcoords.end(),
                    { target.has_uv ? target.uv.x : 0.0f, target.has_uv ? target.uv.y : 0.0f } );

                colors.insert( colors.end(), { target.color.r, target.color.g, target.color.b, target.color.a } );

                if( skinned )
                {
                    if( binding.valid )
                    {
                        joints.insert( joints.end(), binding.joints.begin(), binding.joints.end() );
                        weights.insert( weights.end(),
                            { binding.weights.x, binding.weights.y, binding.weights.z, binding.weights.w } );
                    }
                    else
                    {
                        joints.insert( joints.end(), { 0u, 0u, 0u, 0u } );
                        weights.insert( weights.end(), { 1.0f, 0.0f, 0.0f, 0.0f } );
                    }
                }
            }

            std::vector<std::uint32_t> indices = source_mesh.triangle_indices;

            const auto [ position_minimum, position_maximum ] = gltf::min_max_vec3( positions );
            Primitive primitive{};
            primitive.position = append_vector( document,
                positions,
                component_float,
                "VEC3",
                target_array_buffer,
                position_minimum,
                position_maximum );

            primitive.normal = append_vector( document, normals, component_float, "VEC3", target_array_buffer );

            primitive.texcoord = append_vector( document, texcoords, component_float, "VEC2", target_array_buffer );

            primitive.color = append_vector( document, colors, component_float, "VEC4", target_array_buffer );

            if( skinned )
            {
                primitive.joints =
                    append_vector( document, joints, component_unsigned_short, "VEC4", target_array_buffer );

                primitive.weights = append_vector( document, weights, component_float, "VEC4", target_array_buffer );
            }

            primitive.indices =
                append_vector( document, indices, component_unsigned_int, "SCALAR", target_element_array_buffer );

            primitive.cull_func = source_mesh.cull_func;

            primitive.skinned = skinned;

            std::vector<std::int32_t> primitive_materials;

            if( const filetypes::xg::MultiPassMaterial *multipass =
                    model.find_multipass_material( source_mesh.material_name ) )
            {
                primitive_materials.reserve( multipass->material_names.size() );

                for( const std::string &material_name : multipass->material_names )
                {
                    const auto material = material_lookup.find( material_name );

                    if( material != material_lookup.end() )
                    {
                        primitive_materials.push_back( material->second );
                    }
                }
            }
            else
            {
                const auto material = material_lookup.find( source_mesh.material_name );

                if( material != material_lookup.end() )
                {
                    primitive_materials.push_back( material->second );
                }
            }

            if( primitive_materials.empty() )
            {
                primitive_materials.push_back( 0 );
            }

            primitive.material = primitive_materials.front();

            build_geometry_tracks( document, model, *geometry, primitive );

            Mesh mesh{};
            mesh.name = source_mesh.name;
            mesh.primitives.push_back( std::move( primitive ) );
            const std::int32_t mesh_node_index = static_cast<std::int32_t>( document.nodes.size() );
            build_standard_morph_targets( document, model, *geometry, mesh.primitives.front(), mesh, mesh_node_index );

            for( std::size_t layer = 1; layer < primitive_materials.size(); ++layer )
            {
                Primitive multipass_primitive = mesh.primitives.front();
                multipass_primitive.material = primitive_materials[ layer ];
                mesh.primitives.push_back( std::move( multipass_primitive ) );
            }

            const std::int32_t mesh_index = static_cast<std::int32_t>( document.meshes.size() );
            document.meshes.push_back( std::move( mesh ) );

            Node mesh_node{};
            mesh_node.name = source_mesh.name;

            mesh_node.mesh = mesh_index;

            mesh_node.skin = skinned && !document.skins.empty() ? 0 : -1;
            document.nodes.push_back( std::move( mesh_node ) );
            node_parent.push_back( -1 );

            const auto matrix_parent = matrix_parents.find( source_mesh.name );

            if( matrix_parent != matrix_parents.end() )
            {
                const auto parent_node = matrix_node_indices.find( matrix_parent->second );

                if( parent_node != matrix_node_indices.end() )
                {
                    document.nodes[ static_cast<std::size_t>( parent_node->second ) ].children.push_back(
                        mesh_node_index );

                    node_parent[ static_cast<std::size_t>( mesh_node_index ) ] = parent_node->second;
                }
            }
        }

        for( std::size_t node_index = 0; node_index < node_parent.size(); ++node_index )
        {
            if( node_parent[ node_index ] < 0 )
            {
                document.scene_nodes.push_back( static_cast<std::int32_t>( node_index ) );
            }
        }

        Animation animation{};
        animation.name = "PIKO_GLOBAL_XG";

        for( const filetypes::xg::MatrixNode &source_node : model.matrix_nodes )
        {
            const auto node = matrix_node_indices.find( source_node.name );

            if( node == matrix_node_indices.end() )
            {
                continue;
            }

            if( const filetypes::xg::Vec3Interpolator *interpolator =
                    model.find_vec3_interpolator( source_node.input_position ) )
            {
                if( !interpolator->keys.empty() )
                {
                    const std::vector<float> times = key_times( interpolator->times, interpolator->keys.size() );
                    std::vector<float> values;
                    values.reserve( interpolator->keys.size() * 3u );

                    for( const filetypes::xg::Vec3 &key : interpolator->keys )
                    {
                        values.insert( values.end(), { key.x, key.y, -key.z } );
                    }

                    const auto [ time_minimum, time_maximum ] = gltf::min_max_scalar( times );
                    const std::int32_t input =
                        append_vector( document, times, component_float, "SCALAR", 0, time_minimum, time_maximum );

                    const std::int32_t output = append_vector( document, values, component_float, "VEC3", 0 );
                    const std::int32_t sampler = static_cast<std::int32_t>( animation.samplers.size() );
                    animation.samplers.push_back(
                        AnimationSampler{ input, output, interpolator->type == 0u ? "STEP" : "LINEAR" } );

                    animation.channels.push_back( AnimationChannel{ sampler, node->second, "translation" } );
                }
            }

            if( const filetypes::xg::QuatInterpolator *interpolator =
                    model.find_quat_interpolator( source_node.input_rotation ) )
            {
                if( !interpolator->keys.empty() )
                {
                    const std::vector<float> times = key_times( interpolator->times, interpolator->keys.size() );
                    std::vector<float> values;
                    values.reserve( interpolator->keys.size() * 4u );

                    for( const filetypes::xg::Quat &source_key : interpolator->keys )
                    {
                        const filetypes::xg::Quat key = convert_rotation( source_key );
                        values.insert( values.end(), { key.x, key.y, key.z, key.w } );
                    }

                    const auto [ time_minimum, time_maximum ] = gltf::min_max_scalar( times );
                    const std::int32_t input =
                        append_vector( document, times, component_float, "SCALAR", 0, time_minimum, time_maximum );

                    const std::int32_t output = append_vector( document, values, component_float, "VEC4", 0 );
                    const std::int32_t sampler = static_cast<std::int32_t>( animation.samplers.size() );
                    animation.samplers.push_back(
                        AnimationSampler{ input, output, interpolator->type == 0u ? "STEP" : "LINEAR" } );

                    animation.channels.push_back( AnimationChannel{ sampler, node->second, "rotation" } );
                }
            }

            if( const filetypes::xg::Vec3Interpolator *interpolator =
                    model.find_vec3_interpolator( source_node.input_scale ) )
            {
                if( !interpolator->keys.empty() )
                {
                    const std::vector<float> times = key_times( interpolator->times, interpolator->keys.size() );
                    std::vector<float> values;
                    values.reserve( interpolator->keys.size() * 3u );

                    for( const filetypes::xg::Vec3 &key : interpolator->keys )
                    {
                        values.insert( values.end(), { key.x, key.y, key.z } );
                    }

                    const auto [ time_minimum, time_maximum ] = gltf::min_max_scalar( times );
                    const std::int32_t input =
                        append_vector( document, times, component_float, "SCALAR", 0, time_minimum, time_maximum );

                    const std::int32_t output = append_vector( document, values, component_float, "VEC3", 0 );
                    const std::int32_t sampler = static_cast<std::int32_t>( animation.samplers.size() );
                    animation.samplers.push_back(
                        AnimationSampler{ input, output, interpolator->type == 0u ? "STEP" : "LINEAR" } );

                    animation.channels.push_back( AnimationChannel{ sampler, node->second, "scale" } );
                }
            }
        }

        for( const Document::PendingWeightAnimation &pending : document.pending_weight_animations )
        {
            if( pending.node < 0 || pending.times.empty() || pending.weights.empty() )
            {
                continue;
            }

            const auto [ time_minimum, time_maximum ] = gltf::min_max_scalar( pending.times );
            const std::int32_t input =
                append_vector( document, pending.times, component_float, "SCALAR", 0, time_minimum, time_maximum );

            const std::int32_t output = append_vector( document, pending.weights, component_float, "SCALAR", 0 );
            const std::int32_t sampler = static_cast<std::int32_t>( animation.samplers.size() );
            animation.samplers.push_back( AnimationSampler{ input, output, "STEP" } );
            animation.channels.push_back( AnimationChannel{ sampler, pending.node, "weights" } );
        }

        if( !animation.channels.empty() )
        {
            document.animations.push_back( std::move( animation ) );
        }

        gltf::align_buffer( document.buffer, 4u );
        write_binary( model_root / "model.bin", document.buffer );
        write_document( document, model_metadata, model_root / "model.gltf" );
        return true;
    }
}

namespace piko::convert
{
    XgConversionResult convert_xgm_models_to_gltf( std::span<const std::uint8_t> xgm_bytes,
        const std::filesystem::path &output_root )
    {
        const filetypes::xgm::Metadata metadata = filetypes::xgm::decode_metadata( xgm_bytes );
        XgConversionResult result;

        for( const filetypes::xgm::ModelMetadata &model : metadata.models )
        {
            try
            {
                if( xg_gltf::convert_model( xgm_bytes, metadata, model, output_root ) )
                {
                    result.converted_models.push_back( model.filename );
                }
            }
            catch( const std::exception &error )
            {
                result.warnings.push_back( model.filename + ": " + error.what() );
            }
        }

        return result;
    }
}
