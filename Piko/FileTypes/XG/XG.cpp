#include "XG.hpp"
#include "Utils/BinaryUtil.hpp"
#include "Utils/StrUtil.hpp"

#include <cstdint>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <sstream>
#include <unordered_set>

namespace piko::filetypes::xg
{
    namespace parser
    {
        class Reader
        {
        public:
            explicit Reader( std::span<const std::uint8_t> bytes ) : bytes_( bytes )
            {
            }

            [[nodiscard]] std::size_t offset() const
            {
                return offset_;
            }

            [[nodiscard]] std::size_t remaining() const
            {
                return bytes_.size() - offset_;
            }

            void require( std::size_t size, const char *context ) const
            {
                if( size > remaining() )
                {
                    throw DecodeError( std::string( "Unexpected end of XG while reading " ) + context );
                }
            }

            void skip( std::size_t size, const char *context )
            {
                require( size, context );
                offset_ += size;
            }

            std::string read_pstring( const char *context )
            {
                require( 1, context );
                const std::uint8_t length = bytes_[ offset_++ ];
                require( length, context );

                const char *text = reinterpret_cast<const char *>( bytes_.data() + offset_ );
                offset_ += length;
                return std::string( text, text + length );
            }

            std::uint32_t read_u32( const char *context )
            {
                require( 4, context );
                const std::uint8_t *p = bytes_.data() + offset_;
                offset_ += 4;
                return piko::utils::read_le_u32( p );
            }

            std::int32_t read_i32( const char *context )
            {
                return static_cast<std::int32_t>( read_u32( context ) );
            }

            float read_f32( const char *context )
            {
                const std::uint32_t bits = read_u32( context );
                float value = 0.0f;
                static_assert( sizeof( value ) == sizeof( bits ) );
                std::memcpy( &value, &bits, sizeof( value ) );
                return value;
            }

            Vec3 read_vec3( const char *context )
            {
                return Vec3{ read_f32( context ), read_f32( context ), read_f32( context ) };
            }

            Quat read_quat( const char *context )
            {
                return Quat{ read_f32( context ), read_f32( context ), read_f32( context ), read_f32( context ) };
            }

        private:
            std::span<const std::uint8_t> bytes_;
            std::size_t offset_ = 0;
        };

        std::size_t vertex_float_count( std::uint32_t flags )
        {
            std::size_t count = 0;

            if( ( flags & 0x01u ) != 0 )
            {
                count += 4;
            }

            if( ( flags & 0x02u ) != 0 )
            {
                count += 3;
            }

            if( ( flags & 0x04u ) != 0 )
            {
                count += 4;
            }

            if( ( flags & 0x08u ) != 0 )
            {
                count += 2;
            }

            return count;
        }

        std::vector<Vertex> read_vertex_blob( Reader &reader )
        {
            const std::uint32_t flags = reader.read_u32( "vertex flags" );
            const std::uint32_t count = reader.read_u32( "vertex count" );
            const bool has_position = ( flags & 0x01u ) != 0;
            const bool has_normal = ( flags & 0x02u ) != 0;
            const bool has_color = ( flags & 0x04u ) != 0;
            const bool has_uv = ( flags & 0x08u ) != 0;

            if( vertex_float_count( flags ) == 0 && count != 0 )
            {
                throw DecodeError( "XG geometry has vertices but no recognized vertex fields" );
            }

            std::vector<Vertex> vertices;
            vertices.reserve( count );

            for( std::uint32_t i = 0; i < count; ++i )
            {
                Vertex vertex{};

                if( has_position )
                {
                    vertex.position.x = reader.read_f32( "vertex x" );

                    vertex.position.y = reader.read_f32( "vertex y" );

                    vertex.position.z = -reader.read_f32( "vertex z" );
                    (void)reader.read_f32( "vertex w reserved" );
                }

                if( has_normal )
                {
                    vertex.normal.x = reader.read_f32( "normal x" );

                    vertex.normal.y = reader.read_f32( "normal y" );

                    vertex.normal.z = -reader.read_f32( "normal z" );
                }

                if( has_color )
                {
                    vertex.color.r = reader.read_f32( "color r" );

                    vertex.color.g = reader.read_f32( "color g" );

                    vertex.color.b = reader.read_f32( "color b" );

                    vertex.color.a = reader.read_f32( "color a" );
                }

                if( has_uv )
                {
                    vertex.uv.x = reader.read_f32( "uv s" );

                    vertex.uv.y = reader.read_f32( "uv t" );

                    vertex.has_uv = true;
                }

                vertices.push_back( vertex );
            }

            return vertices;
        }

        std::vector<std::uint32_t> read_index_data( Reader &reader )
        {
            const std::uint32_t size = reader.read_u32( "primitive data size" );
            std::vector<std::uint32_t> values;
            values.reserve( size );

            for( std::uint32_t i = 0; i < size; ++i )
            {
                values.push_back( reader.read_u32( "primitive data" ) );
            }

            return values;
        }

        void append_tri_list_separate( std::vector<std::uint32_t> &out,
            const std::vector<std::uint32_t> &data,
            std::uint32_t list_count )
        {
            std::size_t offset = 0;

            for( std::uint32_t list = 0; list < list_count && offset < data.size(); ++list )
            {
                const std::uint32_t count = data[ offset++ ];
                const std::size_t end = std::min( data.size(), offset + static_cast<std::size_t>( count ) );

                for( std::size_t i = offset; i + 2 < end; i += 3 )
                {
                    out.push_back( data[ i + 0 ] );
                    out.push_back( data[ i + 1 ] );
                    out.push_back( data[ i + 2 ] );
                }

                offset = end;
            }
        }

        void append_tri_strip_indices( std::vector<std::uint32_t> &out, const std::vector<std::uint32_t> &indices )
        {
            if( indices.size() < 3 )
            {
                return;
            }

            for( std::size_t i = 0; i + 2 < indices.size(); ++i )
            {
                const std::uint32_t a = indices[ i + 0 ];
                const std::uint32_t b = indices[ i + 1 ];
                const std::uint32_t c = indices[ i + 2 ];

                if( a == b || b == c || a == c )
                {
                    continue;
                }

                if( ( i & 1u ) == 0 )
                {
                    out.push_back( a );
                    out.push_back( b );
                    out.push_back( c );
                }
                else
                {
                    out.push_back( b );
                    out.push_back( a );
                    out.push_back( c );
                }
            }
        }

        void append_tri_strip_separate( std::vector<std::uint32_t> &out,
            const std::vector<std::uint32_t> &data,
            std::uint32_t strip_count )
        {
            std::size_t offset = 0;

            for( std::uint32_t strip = 0; strip < strip_count && offset < data.size(); ++strip )
            {
                const std::uint32_t count = data[ offset++ ];
                const std::size_t end = std::min( data.size(), offset + static_cast<std::size_t>( count ) );
                std::vector<std::uint32_t> strip_indices( data.begin() + static_cast<std::ptrdiff_t>( offset ),
                    data.begin() + static_cast<std::ptrdiff_t>( end ) );

                append_tri_strip_indices( out, strip_indices );
                offset = end;
            }
        }

        void append_tri_fan_separate( std::vector<std::uint32_t> &out,
            const std::vector<std::uint32_t> &data,
            std::uint32_t fan_count )
        {
            std::size_t offset = 0;

            for( std::uint32_t fan = 0; fan < fan_count && offset < data.size(); ++fan )
            {
                const std::uint32_t count = data[ offset++ ];
                const std::size_t end = std::min( data.size(), offset + static_cast<std::size_t>( count ) );

                if( end - offset >= 3 )
                {
                    const std::uint32_t root = data[ offset ];

                    for( std::size_t i = offset + 1; i + 1 < end; ++i )
                    {
                        out.push_back( root );
                        out.push_back( data[ i ] );
                        out.push_back( data[ i + 1 ] );
                    }
                }

                offset = end;
            }
        }

        std::vector<std::uint32_t> group_indices( const std::vector<std::uint32_t> &data, std::uint32_t group_index )
        {
            if( data.empty() || group_index + 1 >= data.size() )
            {
                return {};
            }

            std::uint32_t cursor = data.front();

            for( std::uint32_t i = 0; i < group_index; ++i )
            {
                cursor += data[ static_cast<std::size_t>( i ) + 1u ];
            }

            const std::uint32_t count = data[ static_cast<std::size_t>( group_index ) + 1u ];
            std::vector<std::uint32_t> values;
            values.reserve( count );

            for( std::uint32_t i = 0; i < count; ++i )
            {
                values.push_back( cursor + i );
            }

            return values;
        }

        void append_grouped_primitives( std::vector<std::uint32_t> &out,
            const std::vector<std::uint32_t> &data,
            std::uint32_t primitive_count,
            const char *kind )
        {
            for( std::uint32_t i = 0; i < primitive_count; ++i )
            {
                std::vector<std::uint32_t> indices = group_indices( data, i );

                if( std::strcmp( kind, "list" ) == 0 )
                {
                    for( std::size_t j = 0; j + 2 < indices.size(); j += 3 )
                    {
                        out.push_back( indices[ j + 0 ] );
                        out.push_back( indices[ j + 1 ] );
                        out.push_back( indices[ j + 2 ] );
                    }
                }
                else if( std::strcmp( kind, "strip" ) == 0 )
                {
                    append_tri_strip_indices( out, indices );
                }
                else
                {
                    if( indices.size() >= 3 )
                    {
                        const std::uint32_t root = indices.front();

                        for( std::size_t j = 1; j + 1 < indices.size(); ++j )
                        {
                            out.push_back( root );
                            out.push_back( indices[ j ] );
                            out.push_back( indices[ j + 1 ] );
                        }
                    }
                }
            }
        }

        Matrix read_raw_matrix( Reader &reader, const char *context )
        {
            Matrix matrix{};

            for( float &value : matrix.m )
            {
                value = reader.read_f32( context );
            }

            return matrix;
        }

        MatrixNode read_matrix_node( Reader &reader, std::string name )
        {
            MatrixNode node{};
            node.name = std::move( name );

            while( true )
            {
                const std::string property = reader.read_pstring( "xgBgMatrix property" );

                if( property == "}" )
                {
                    break;
                }

                if( property == "position" )
                {
                    node.position = reader.read_vec3( "xgBgMatrix position" );
                }
                else if( property == "rotation" )
                {
                    node.rotation = reader.read_quat( "xgBgMatrix rotation" );
                }
                else if( property == "scale" )
                {
                    node.scale = reader.read_vec3( "xgBgMatrix scale" );
                }
                else if( property == "inputPosition" )
                {
                    node.input_position = reader.read_pstring( "xgBgMatrix position interpolator" );
                    (void)reader.read_pstring( "xgBgMatrix position output" );
                }
                else if( property == "inputRotation" )
                {
                    node.input_rotation = reader.read_pstring( "xgBgMatrix rotation interpolator" );
                    (void)reader.read_pstring( "xgBgMatrix rotation output" );
                }
                else if( property == "inputScale" )
                {
                    node.input_scale = reader.read_pstring( "xgBgMatrix scale interpolator" );
                    (void)reader.read_pstring( "xgBgMatrix scale output" );
                }
                else if( property == "inputParentMatrix" )
                {
                    node.parent_matrix = reader.read_pstring( "xgBgMatrix parent" );
                    (void)reader.read_pstring( "xgBgMatrix output" );
                }
                else
                {
                    throw DecodeError( "Unhandled xgBgMatrix property: " + property );
                }
            }

            return node;
        }

        Matrix static_matrix_from_node( const MatrixNode &node )
        {
            return convert_lh_matrix_to_rh( make_trs( node.position, node.rotation, node.scale ) );
        }

        Geometry read_geometry_node( Reader &reader, std::string name )
        {
            Geometry geometry{};
            geometry.name = std::move( name );

            while( true )
            {
                const std::string property = reader.read_pstring( "xgBgGeometry property" );

                if( property == "}" )
                {
                    break;
                }

                if( property == "density" )
                {
                    (void)reader.read_f32( "xgBgGeometry density" );
                }
                else if( property == "vertices" )
                {
                    geometry.vertices = read_vertex_blob( reader );
                }
                else if( property == "inputGeometry" )
                {
                    geometry.input_geometry_nodes.push_back( reader.read_pstring( "xgBgGeometry input" ) );
                    (void)reader.read_pstring( "xgBgGeometry output" );
                }
                else
                {
                    throw DecodeError( "Unhandled xgBgGeometry property: " + property );
                }
            }

            return geometry;
        }

        DagMesh read_dag_mesh_node( Reader &reader, std::string name )
        {
            DagMesh mesh{};
            mesh.name = std::move( name );
            std::uint32_t fan_count = 0;
            std::uint32_t strip_count = 0;
            std::uint32_t list_count = 0;
            std::vector<std::uint32_t> fan_data;
            std::vector<std::uint32_t> strip_data;
            std::vector<std::uint32_t> list_data;

            while( true )
            {
                const std::string property = reader.read_pstring( "xgDagMesh property" );

                if( property == "}" )
                {
                    break;
                }

                if( property == "primType" )
                {
                    mesh.prim_type = reader.read_u32( "xgDagMesh primType" );
                }
                else if( property == "primCount" )
                {
                    (void)reader.read_u32( "xgDagMesh primCount" );
                }
                else if( property == "primData" )
                {
                    (void)read_index_data( reader );
                }
                else if( property == "triFanCount" )
                {
                    fan_count = reader.read_u32( "xgDagMesh fan count" );
                }
                else if( property == "triFanData" )
                {
                    fan_data = read_index_data( reader );
                }
                else if( property == "triStripCount" )
                {
                    strip_count = reader.read_u32( "xgDagMesh strip count" );
                }
                else if( property == "triStripData" )
                {
                    strip_data = read_index_data( reader );
                }
                else if( property == "triListCount" )
                {
                    list_count = reader.read_u32( "xgDagMesh list count" );
                }
                else if( property == "triListData" )
                {
                    list_data = read_index_data( reader );
                }
                else if( property == "cullFunc" )
                {
                    mesh.cull_func = reader.read_u32( "xgDagMesh cullFunc" );
                }
                else if( property == "inputGeometry" )
                {
                    mesh.geometry_name = reader.read_pstring( "xgDagMesh geometry" );
                    (void)reader.read_pstring( "xgDagMesh geometry output" );
                }
                else if( property == "inputMaterial" )
                {
                    mesh.material_name = reader.read_pstring( "xgDagMesh material" );
                    (void)reader.read_pstring( "xgDagMesh material output" );
                }
                else
                {
                    throw DecodeError( "Unhandled xgDagMesh property: " + property );
                }
            }

            if( mesh.prim_type == 5 )
            {
                append_grouped_primitives( mesh.triangle_indices, fan_data, fan_count, "fan" );
                append_grouped_primitives( mesh.triangle_indices, strip_data, strip_count, "strip" );
                append_grouped_primitives( mesh.triangle_indices, list_data, list_count, "list" );
            }
            else
            {
                append_tri_fan_separate( mesh.triangle_indices, fan_data, fan_count );
                append_tri_strip_separate( mesh.triangle_indices, strip_data, strip_count );
                append_tri_list_separate( mesh.triangle_indices, list_data, list_count );
            }

            return mesh;
        }

        DagTransform read_dag_transform_node( Reader &reader, std::string name )
        {
            DagTransform transform{};
            transform.name = std::move( name );

            while( true )
            {
                const std::string property = reader.read_pstring( "xgDagTransform property" );

                if( property == "}" )
                {
                    break;
                }

                if( property == "inputMatrix" )
                {
                    transform.matrix_name = reader.read_pstring( "xgDagTransform matrix" );
                    (void)reader.read_pstring( "xgDagTransform output" );
                }
                else
                {
                    throw DecodeError( "Unhandled xgDagTransform property: " + property );
                }
            }

            return transform;
        }

        Material read_material_node( Reader &reader, std::string name )
        {
            Material material{};
            material.name = std::move( name );

            while( true )
            {
                const std::string property = reader.read_pstring( "xgMaterial property" );

                if( property == "}" )
                {
                    break;
                }

                if( property == "blendType" )
                {
                    material.blend_type = reader.read_u32( "xgMaterial blendType" );
                }
                else if( property == "shadingType" )
                {
                    material.shading_type = reader.read_u32( "xgMaterial shadingType" );
                }
                else if( property == "diffuse" )
                {
                    material.diffuse.r = reader.read_f32( "xgMaterial diffuse r" );

                    material.diffuse.g = reader.read_f32( "xgMaterial diffuse g" );

                    material.diffuse.b = reader.read_f32( "xgMaterial diffuse b" );

                    material.diffuse.a = reader.read_f32( "xgMaterial diffuse a" );
                }
                else if( property == "specular" )
                {
                    (void)reader.read_f32( "xgMaterial specular r" );
                    (void)reader.read_f32( "xgMaterial specular g" );
                    (void)reader.read_f32( "xgMaterial specular b" );
                    (void)reader.read_f32( "xgMaterial specular exponent" );
                }
                else if( property == "flags" )
                {
                    material.flags = reader.read_u32( "xgMaterial flags" );

                    material.uses_texture_alpha = ( material.flags & 0x01u ) != 0;
                }
                else if( property == "textureEnv" )
                {
                    material.texture_env = reader.read_u32( "xgMaterial textureEnv" );
                }
                else if( property == "uTile" )
                {
                    material.u_tile = reader.read_u32( "xgMaterial uTile" );
                }
                else if( property == "vTile" )
                {
                    material.v_tile = reader.read_u32( "xgMaterial vTile" );
                }
                else if( property == "inputTexture" )
                {
                    material.texture_name =
                        piko::utils::lowercase_filename( reader.read_pstring( "xgMaterial texture" ) );

                    (void)reader.read_pstring( "xgMaterial texture output" );
                }
                else
                {
                    throw DecodeError( "Unhandled xgMaterial property: " + property );
                }
            }

            if( material.blend_type == 5 )
            {
                material.uses_texture_alpha = true;
            }

            return material;
        }

        std::vector<std::string> read_multipass_material_node( Reader &reader )
        {
            std::vector<std::string> material_names;

            while( true )
            {
                const std::string property = reader.read_pstring( "xgMultiPassMaterial property" );

                if( property == "}" )
                {
                    break;
                }

                if( property == "inputMaterial" )
                {

                    material_names.push_back( reader.read_pstring( "xgMultiPassMaterial material" ) );
                    (void)reader.read_pstring( "xgMultiPassMaterial output" );
                }
                else
                {
                    throw DecodeError( "Unhandled xgMultiPassMaterial property: " + property );
                }
            }

            return material_names;
        }

        void skip_name_output( Reader &reader )
        {
            (void)reader.read_pstring( "input node" );
            (void)reader.read_pstring( "input output" );
        }

        Vec3Interpolator read_vec3_interpolator_node( Reader &reader, std::string name )
        {
            Vec3Interpolator interpolator{};
            interpolator.name = std::move( name );

            while( true )
            {
                const std::string property = reader.read_pstring( "xgVec3Interpolator property" );

                if( property == "}" )
                {
                    break;
                }

                if( property == "type" )
                {
                    interpolator.type = reader.read_u32( "xgVec3Interpolator type" );
                }
                else if( property == "times" )
                {
                    const std::uint32_t count = reader.read_u32( "xgVec3Interpolator times count" );
                    interpolator.times.reserve( count );

                    for( std::uint32_t i = 0; i < count; ++i )
                    {
                        interpolator.times.push_back( reader.read_f32( "xgVec3Interpolator time" ) );
                    }
                }
                else if( property == "keys" )
                {
                    const std::uint32_t count = reader.read_u32( "xgVec3Interpolator keys count" );
                    interpolator.keys.reserve( count );

                    for( std::uint32_t i = 0; i < count; ++i )
                    {
                        interpolator.keys.push_back( reader.read_vec3( "xgVec3Interpolator key" ) );
                    }
                }
                else if( property == "targets" )
                {
                    const std::uint32_t count = reader.read_u32( "xgVec3Interpolator targets count" );
                    interpolator.targets.reserve( count );

                    for( std::uint32_t i = 0; i < count; ++i )
                    {
                        interpolator.targets.push_back( reader.read_u32( "xgVec3Interpolator target" ) );
                    }
                }
                else if( property == "inputTime" )
                {
                    skip_name_output( reader );
                }
                else
                {
                    throw DecodeError( "Unhandled xgVec3Interpolator property: " + property );
                }
            }

            return interpolator;
        }

        QuatInterpolator read_quat_interpolator_node( Reader &reader, std::string name )
        {
            QuatInterpolator interpolator{};
            interpolator.name = std::move( name );

            while( true )
            {
                const std::string property = reader.read_pstring( "xgQuatInterpolator property" );

                if( property == "}" )
                {
                    break;
                }

                if( property == "type" )
                {
                    interpolator.type = reader.read_u32( "xgQuatInterpolator type" );
                }
                else if( property == "times" )
                {
                    const std::uint32_t count = reader.read_u32( "xgQuatInterpolator times count" );
                    interpolator.times.reserve( count );

                    for( std::uint32_t i = 0; i < count; ++i )
                    {
                        interpolator.times.push_back( reader.read_f32( "xgQuatInterpolator time" ) );
                    }
                }
                else if( property == "keys" )
                {
                    const std::uint32_t count = reader.read_u32( "xgQuatInterpolator keys count" );
                    interpolator.keys.reserve( count );

                    for( std::uint32_t i = 0; i < count; ++i )
                    {
                        interpolator.keys.push_back( reader.read_quat( "xgQuatInterpolator key" ) );
                    }
                }
                else if( property == "inputTime" )
                {
                    skip_name_output( reader );
                }
                else
                {
                    throw DecodeError( "Unhandled xgQuatInterpolator property: " + property );
                }
            }

            return interpolator;
        }

        std::vector<std::uint32_t> read_u32_targets( Reader &reader, const char *context )
        {
            const std::uint32_t count = reader.read_u32( context );
            std::vector<std::uint32_t> targets;
            targets.reserve( count );

            for( std::uint32_t i = 0; i < count; ++i )
            {
                targets.push_back( reader.read_u32( context ) );
            }

            return targets;
        }

        VertexInterpolator read_vertex_interpolator_node( Reader &reader, std::string name )
        {
            VertexInterpolator interpolator{};
            interpolator.name = std::move( name );

            while( true )
            {
                const std::string property = reader.read_pstring( "xgVertexInterpolator property" );

                if( property == "}" )
                {
                    break;
                }

                if( property == "type" )
                {
                    interpolator.type = reader.read_u32( "xgVertexInterpolator type" );
                }
                else if( property == "times" )
                {
                    const std::uint32_t count = reader.read_u32( "xgVertexInterpolator times count" );
                    interpolator.times.reserve( count );

                    for( std::uint32_t i = 0; i < count; ++i )
                    {
                        interpolator.times.push_back( reader.read_f32( "xgVertexInterpolator time" ) );
                    }
                }
                else if( property == "keys" )
                {
                    const std::uint32_t key_count = reader.read_u32( "xgVertexInterpolator key count" );
                    interpolator.keys.reserve( key_count );

                    for( std::uint32_t key = 0; key < key_count; ++key )
                    {
                        const std::uint32_t position_count = reader.read_u32( "xgVertexInterpolator position count" );
                        std::vector<Vec3> positions;
                        positions.reserve( position_count );

                        for( std::uint32_t i = 0; i < position_count; ++i )
                        {
                            Vec3 p = reader.read_vec3( "xgVertexInterpolator position" );
                            p.z = -p.z;
                            positions.push_back( p );
                        }

                        interpolator.keys.push_back( std::move( positions ) );
                    }
                }
                else if( property == "targets" )
                {
                    interpolator.targets = read_u32_targets( reader, "xgVertexInterpolator targets" );
                }
                else if( property == "inputTime" )
                {
                    skip_name_output( reader );
                }
                else
                {
                    throw DecodeError( "Unhandled xgVertexInterpolator property: " + property );
                }
            }

            return interpolator;
        }

        NormalInterpolator read_normal_interpolator_node( Reader &reader, std::string name )
        {
            NormalInterpolator interpolator{};
            interpolator.name = std::move( name );

            while( true )
            {
                const std::string property = reader.read_pstring( "xgNormalInterpolator property" );

                if( property == "}" )
                {
                    break;
                }

                if( property == "type" )
                {
                    interpolator.type = reader.read_u32( "xgNormalInterpolator type" );
                }
                else if( property == "times" )
                {
                    const std::uint32_t count = reader.read_u32( "xgNormalInterpolator times count" );
                    interpolator.times.reserve( count );

                    for( std::uint32_t i = 0; i < count; ++i )
                    {
                        interpolator.times.push_back( reader.read_f32( "xgNormalInterpolator time" ) );
                    }
                }
                else if( property == "keys" )
                {
                    const std::uint32_t key_count = reader.read_u32( "xgNormalInterpolator key count" );
                    interpolator.keys.reserve( key_count );

                    for( std::uint32_t key = 0; key < key_count; ++key )
                    {
                        const std::uint32_t normal_count = reader.read_u32( "xgNormalInterpolator normal count" );
                        std::vector<Vec3> normals;
                        normals.reserve( normal_count );

                        for( std::uint32_t i = 0; i < normal_count; ++i )
                        {
                            Vec3 n = reader.read_vec3( "xgNormalInterpolator normal" );
                            n.z = -n.z;
                            normals.push_back( n );
                        }

                        interpolator.keys.push_back( std::move( normals ) );
                    }
                }
                else if( property == "targets" )
                {
                    interpolator.targets = read_u32_targets( reader, "xgNormalInterpolator targets" );
                }
                else if( property == "inputTime" )
                {
                    skip_name_output( reader );
                }
                else
                {
                    throw DecodeError( "Unhandled xgNormalInterpolator property: " + property );
                }
            }

            return interpolator;
        }

        TexCoordInterpolator read_texcoord_interpolator_node( Reader &reader, std::string name )
        {
            TexCoordInterpolator interpolator{};
            interpolator.name = std::move( name );

            while( true )
            {
                const std::string property = reader.read_pstring( "xgTexCoordInterpolator property" );

                if( property == "}" )
                {
                    break;
                }

                if( property == "type" )
                {
                    interpolator.type = reader.read_u32( "xgTexCoordInterpolator type" );
                }
                else if( property == "times" )
                {
                    const std::uint32_t count = reader.read_u32( "xgTexCoordInterpolator times count" );
                    interpolator.times.reserve( count );

                    for( std::uint32_t i = 0; i < count; ++i )
                    {
                        interpolator.times.push_back( reader.read_f32( "xgTexCoordInterpolator time" ) );
                    }
                }
                else if( property == "keys" )
                {
                    const std::uint32_t key_count = reader.read_u32( "xgTexCoordInterpolator key count" );
                    interpolator.keys.reserve( key_count );

                    for( std::uint32_t key = 0; key < key_count; ++key )
                    {
                        const std::uint32_t uv_count = reader.read_u32( "xgTexCoordInterpolator uv count" );
                        std::vector<rendering::Vec2> uvs;
                        uvs.reserve( uv_count );

                        for( std::uint32_t i = 0; i < uv_count; ++i )
                        {
                            uvs.push_back( rendering::Vec2{
                                reader.read_f32( "xgTexCoordInterpolator u" ),
                                reader.read_f32( "xgTexCoordInterpolator v" )
                            } );
                        }

                        interpolator.keys.push_back( std::move( uvs ) );
                    }
                }
                else if( property == "targets" )
                {
                    interpolator.targets = read_u32_targets( reader, "xgTexCoordInterpolator targets" );
                }
                else if( property == "inputTime" )
                {
                    skip_name_output( reader );
                }
                else
                {
                    throw DecodeError( "Unhandled xgTexCoordInterpolator property: " + property );
                }
            }

            return interpolator;
        }

        ShapeInterpolator read_shape_interpolator_node( Reader &reader, std::string name )
        {
            ShapeInterpolator interpolator{};
            interpolator.name = std::move( name );

            while( true )
            {
                const std::string property = reader.read_pstring( "xgShapeInterpolator property" );

                if( property == "}" )
                {
                    break;
                }

                if( property == "type" )
                {
                    interpolator.type = reader.read_u32( "xgShapeInterpolator type" );
                }
                else if( property == "times" )
                {
                    const std::uint32_t count = reader.read_u32( "xgShapeInterpolator times count" );
                    interpolator.times.reserve( count );

                    for( std::uint32_t i = 0; i < count; ++i )
                    {
                        interpolator.times.push_back( reader.read_f32( "xgShapeInterpolator time" ) );
                    }
                }
                else if( property == "keys" )
                {
                    const std::uint32_t key_count = reader.read_u32( "xgShapeInterpolator key count" );
                    interpolator.keys.reserve( key_count );

                    for( std::uint32_t i = 0; i < key_count; ++i )
                    {
                        interpolator.keys.push_back( read_vertex_blob( reader ) );
                    }
                }
                else if( property == "inputTime" )
                {
                    skip_name_output( reader );
                }
                else
                {
                    throw DecodeError( "Unhandled xgShapeInterpolator property: " + property );
                }
            }

            return interpolator;
        }

        Envelope read_envelope_node( Reader &reader, std::string name )
        {
            Envelope envelope{};
            envelope.name = std::move( name );

            while( true )
            {
                const std::string property = reader.read_pstring( "xgEnvelope property" );

                if( property == "}" )
                {
                    break;
                }

                if( property == "startVertex" )
                {
                    envelope.start_vertex = reader.read_u32( "xgEnvelope startVertex" );
                }
                else if( property == "weights" )
                {
                    const std::uint32_t count = reader.read_u32( "xgEnvelope weights count" );
                    envelope.weights.reserve( count );

                    for( std::uint32_t i = 0; i < count; ++i )
                    {
                        envelope.weights.push_back( Vec4{
                            reader.read_f32( "xgEnvelope weight0" ),
                            reader.read_f32( "xgEnvelope weight1" ),
                            reader.read_f32( "xgEnvelope weight2" ),
                            reader.read_f32( "xgEnvelope weight3" )
                        } );
                    }
                }
                else if( property == "vertexTargets" )
                {
                    const std::uint32_t count = reader.read_u32( "xgEnvelope targets count" );
                    std::vector<std::uint32_t> current;

                    for( std::uint32_t i = 0; i < count; ++i )
                    {
                        const std::int32_t value = reader.read_i32( "xgEnvelope target" );

                        if( value < 0 )
                        {
                            envelope.vertex_targets.push_back( std::move( current ) );
                            current.clear();
                        }
                        else
                        {
                            current.push_back( static_cast<std::uint32_t>( value ) );
                        }
                    }

                    if( !current.empty() )
                    {
                        envelope.vertex_targets.push_back( std::move( current ) );
                    }
                }
                else if( property == "inputGeometry" )
                {
                    envelope.input_geometry = reader.read_pstring( "xgEnvelope input geometry" );
                    (void)reader.read_pstring( "xgEnvelope geometry output" );
                }
                else if( property.rfind( "inputMatrix", 0 ) == 0 )
                {
                    std::uint32_t slot = 0;

                    if( property.size() > 11 && property[ 11 ] >= '1' && property[ 11 ] <= '4' )
                    {
                        slot = static_cast<std::uint32_t>( property[ 11 ] - '1' );
                    }

                    envelope.input_matrices[ slot ] = reader.read_pstring( "xgEnvelope input matrix" );
                    (void)reader.read_pstring( "xgEnvelope matrix output" );
                }
                else
                {
                    throw DecodeError( "Unhandled xgEnvelope property: " + property );
                }
            }

            return envelope;
        }

        Bone read_bone_node( Reader &reader, std::string name )
        {
            Bone bone{};
            bone.name = std::move( name );

            bone.rest_matrix = identity_matrix();

            while( true )
            {
                const std::string property = reader.read_pstring( "xgBone property" );

                if( property == "}" )
                {
                    break;
                }

                if( property == "restMatrix" )
                {
                    bone.rest_matrix = convert_lh_matrix_to_rh( read_raw_matrix( reader, "xgBone restMatrix" ) );
                }
                else if( property == "inputMatrix" )
                {
                    bone.input_matrix = reader.read_pstring( "xgBone input matrix" );
                    (void)reader.read_pstring( "xgBone matrix output" );
                }
                else
                {
                    throw DecodeError( "Unhandled xgBone property: " + property );
                }
            }

            return bone;
        }

        void skip_time_node( Reader &reader )
        {
            while( true )
            {
                const std::string property = reader.read_pstring( "xgTime property" );

                if( property == "}" )
                {
                    break;
                }

                if( property == "numFrames" || property == "time" )
                {
                    (void)reader.read_f32( "xgTime value" );
                }
                else
                {
                    throw DecodeError( "Unhandled xgTime property: " + property );
                }
            }
        }

        std::vector<DagNode> read_dag( Reader &reader )
        {
            const std::string open = reader.read_pstring( "dag open" );

            if( open != "{" )
            {
                throw DecodeError( "XG dag missing opening brace" );
            }

            std::vector<DagNode> nodes;
            while( true )
            {
                const std::string name = reader.read_pstring( "dag node" );

                if( name == "}" )
                {
                    break;
                }

                const std::string bracket = reader.read_pstring( "dag bracket" );

                if( bracket != "[" )
                {
                    throw DecodeError( "XG dag node missing '['" );
                }

                DagNode node{};
                node.name = name;
                while( true )
                {
                    const std::string child = reader.read_pstring( "dag child" );

                    if( child == "]" )
                    {
                        break;
                    }

                    node.children.push_back( child );
                }

                nodes.push_back( std::move( node ) );
            }

            return nodes;
        }
    }

    DecodeError::DecodeError( const std::string &message ) : std::runtime_error( message )
    {
    }

    const Geometry *Model::find_geometry( const std::string &name ) const
    {
        const auto it = std::find_if( geometries.begin(),
            geometries.end(),
            [ & ]( const Geometry &g )
            {
                return g.name == name;
            } );
        return it == geometries.end() ? nullptr : &*it;
    }

    const Material *Model::find_material( const std::string &name ) const
    {
        const auto it = std::find_if( materials.begin(),
            materials.end(),
            [ & ]( const Material &m )
            {
                return m.name == name;
            } );
        return it == materials.end() ? nullptr : &*it;
    }

    const MultiPassMaterial *Model::find_multipass_material( const std::string &name ) const
    {
        const auto it = std::find_if( multipass_materials.begin(),
            multipass_materials.end(),
            [ & ]( const MultiPassMaterial &m )
            {
                return m.name == name;
            } );
        return it == multipass_materials.end() ? nullptr : &*it;
    }

    const Vec3Interpolator *Model::find_vec3_interpolator( const std::string &name ) const
    {
        const auto it = std::find_if( vec3_interpolators.begin(),
            vec3_interpolators.end(),
            [ & ]( const Vec3Interpolator &v )
            {
                return v.name == name;
            } );
        return it == vec3_interpolators.end() ? nullptr : &*it;
    }

    const QuatInterpolator *Model::find_quat_interpolator( const std::string &name ) const
    {
        const auto it = std::find_if( quat_interpolators.begin(),
            quat_interpolators.end(),
            [ & ]( const QuatInterpolator &q )
            {
                return q.name == name;
            } );
        return it == quat_interpolators.end() ? nullptr : &*it;
    }

    const VertexInterpolator *Model::find_vertex_interpolator( const std::string &name ) const
    {
        const auto it = std::find_if( vertex_interpolators.begin(),
            vertex_interpolators.end(),
            [ & ]( const VertexInterpolator &v )
            {
                return v.name == name;
            } );
        return it == vertex_interpolators.end() ? nullptr : &*it;
    }

    const NormalInterpolator *Model::find_normal_interpolator( const std::string &name ) const
    {
        const auto it = std::find_if( normal_interpolators.begin(),
            normal_interpolators.end(),
            [ & ]( const NormalInterpolator &n )
            {
                return n.name == name;
            } );
        return it == normal_interpolators.end() ? nullptr : &*it;
    }

    const TexCoordInterpolator *Model::find_texcoord_interpolator( const std::string &name ) const
    {
        const auto it = std::find_if( texcoord_interpolators.begin(),
            texcoord_interpolators.end(),
            [ & ]( const TexCoordInterpolator &t )
            {
                return t.name == name;
            } );
        return it == texcoord_interpolators.end() ? nullptr : &*it;
    }

    const ShapeInterpolator *Model::find_shape_interpolator( const std::string &name ) const
    {
        const auto it = std::find_if( shape_interpolators.begin(),
            shape_interpolators.end(),
            [ & ]( const ShapeInterpolator &s )
            {
                return s.name == name;
            } );
        return it == shape_interpolators.end() ? nullptr : &*it;
    }

    const MatrixNode *Model::find_matrix_node( const std::string &name ) const
    {
        const auto it = std::find_if( matrix_nodes.begin(),
            matrix_nodes.end(),
            [ & ]( const MatrixNode &m )
            {
                return m.name == name;
            } );
        return it == matrix_nodes.end() ? nullptr : &*it;
    }

    const Bone *Model::find_bone( const std::string &name ) const
    {
        const auto it = std::find_if( bones.begin(),
            bones.end(),
            [ & ]( const Bone &b )
            {
                return b.name == name;
            } );
        return it == bones.end() ? nullptr : &*it;
    }

    const Envelope *Model::find_envelope( const std::string &name ) const
    {
        const auto it = std::find_if( envelopes.begin(),
            envelopes.end(),
            [ & ]( const Envelope &e )
            {
                return e.name == name;
            } );
        return it == envelopes.end() ? nullptr : &*it;
    }

    const DagMesh *Model::find_mesh( const std::string &name ) const
    {
        const auto it = std::find_if( meshes.begin(),
            meshes.end(),
            [ & ]( const DagMesh &m )
            {
                return m.name == name;
            } );
        return it == meshes.end() ? nullptr : &*it;
    }

    const DagTransform *Model::find_transform( const std::string &name ) const
    {
        const auto it = std::find_if( transforms.begin(),
            transforms.end(),
            [ & ]( const DagTransform &t )
            {
                return t.name == name;
            } );
        return it == transforms.end() ? nullptr : &*it;
    }

    const DagNode *Model::find_dag_node( const std::string &name ) const
    {
        const auto it = std::find_if( dag_nodes.begin(),
            dag_nodes.end(),
            [ & ]( const DagNode &n )
            {
                return n.name == name;
            } );
        return it == dag_nodes.end() ? nullptr : &*it;
    }

    Matrix identity_matrix()
    {
        Matrix matrix{};
        matrix.m[ 0 ] = 1.0f;

        matrix.m[ 5 ] = 1.0f;

        matrix.m[ 10 ] = 1.0f;

        matrix.m[ 15 ] = 1.0f;
        return matrix;
    }

    Vec3 normalize( Vec3 value )
    {
        const float length = std::sqrt( value.x * value.x + value.y * value.y + value.z * value.z );

        if( length <= 0.000001f )
        {
            return Vec3{ 0.0f, 1.0f, 0.0f };
        }

        value.x /= length;

        value.y /= length;

        value.z /= length;
        return value;
    }

    Quat normalize_quaternion( Quat value )
    {
        const float length = std::sqrt( value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w );

        if( length <= 0.000001f )
        {
            return Quat{ 0.0f, 0.0f, 0.0f, 1.0f };
        }

        value.x /= length;

        value.y /= length;

        value.z /= length;

        value.w /= length;
        return value;
    }

    Vec3 lerp( const Vec3 &a, const Vec3 &b, float t )
    {
        return Vec3{ a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t, a.z + ( b.z - a.z ) * t };
    }

    Matrix inverse_affine_matrix( const Matrix &matrix )
    {
        const float a00 = matrix.m[ 0 ];
        const float a01 = matrix.m[ 4 ];
        const float a02 = matrix.m[ 8 ];
        const float a10 = matrix.m[ 1 ];
        const float a11 = matrix.m[ 5 ];
        const float a12 = matrix.m[ 9 ];
        const float a20 = matrix.m[ 2 ];
        const float a21 = matrix.m[ 6 ];
        const float a22 = matrix.m[ 10 ];

        const float c00 = a11 * a22 - a12 * a21;
        const float c01 = -( a10 * a22 - a12 * a20 );
        const float c02 = a10 * a21 - a11 * a20;
        const float c10 = -( a01 * a22 - a02 * a21 );
        const float c11 = a00 * a22 - a02 * a20;
        const float c12 = -( a00 * a21 - a01 * a20 );
        const float c20 = a01 * a12 - a02 * a11;
        const float c21 = -( a00 * a12 - a02 * a10 );
        const float c22 = a00 * a11 - a01 * a10;

        const float determinant = a00 * c00 + a01 * c01 + a02 * c02;

        if( std::abs( determinant ) <= 0.000001f || !std::isfinite( determinant ) )
        {
            return identity_matrix();
        }

        const float reciprocal = 1.0f / determinant;
        Matrix result = identity_matrix();
        result.m[ 0 ] = c00 * reciprocal;

        result.m[ 4 ] = c10 * reciprocal;

        result.m[ 8 ] = c20 * reciprocal;

        result.m[ 1 ] = c01 * reciprocal;

        result.m[ 5 ] = c11 * reciprocal;

        result.m[ 9 ] = c21 * reciprocal;

        result.m[ 2 ] = c02 * reciprocal;

        result.m[ 6 ] = c12 * reciprocal;

        result.m[ 10 ] = c22 * reciprocal;

        const Vec3 translation{ matrix.m[ 12 ], matrix.m[ 13 ], matrix.m[ 14 ] };
        result.m[ 12 ] =
            -( result.m[ 0 ] * translation.x + result.m[ 4 ] * translation.y + result.m[ 8 ] * translation.z );

        result.m[ 13 ] =
            -( result.m[ 1 ] * translation.x + result.m[ 5 ] * translation.y + result.m[ 9 ] * translation.z );

        result.m[ 14 ] =
            -( result.m[ 2 ] * translation.x + result.m[ 6 ] * translation.y + result.m[ 10 ] * translation.z );

        return result;
    }

    Matrix multiply( const Matrix &a, const Matrix &b )
    {
        Matrix result{};

        for( std::int32_t col = 0; col < 4; ++col )
        {
            for( std::int32_t row = 0; row < 4; ++row )
            {
                float value = 0.0f;

                for( std::int32_t k = 0; k < 4; ++k )
                {
                    value += a.m[ k * 4 + row ] * b.m[ col * 4 + k ];
                }

                result.m[ col * 4 + row ] = value;
            }
        }

        return result;
    }

    Matrix make_trs( const Vec3 &position, const Quat &rotation, const Vec3 &scale )
    {
        Quat q = rotation;
        const float len = std::sqrt( q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w );

        if( len > 0.000001f )
        {
            q.x /= len;

            q.y /= len;

            q.z /= len;

            q.w /= len;
        }

        const float xx = q.x * q.x;
        const float yy = q.y * q.y;
        const float zz = q.z * q.z;
        const float xy = q.x * q.y;
        const float xz = q.x * q.z;
        const float yz = q.y * q.z;
        const float wx = q.w * q.x;
        const float wy = q.w * q.y;
        const float wz = q.w * q.z;

        Matrix matrix = identity_matrix();
        matrix.m[ 0 ] = ( 1.0f - 2.0f * ( yy + zz ) ) * scale.x;

        matrix.m[ 1 ] = ( 2.0f * ( xy + wz ) ) * scale.x;

        matrix.m[ 2 ] = ( 2.0f * ( xz - wy ) ) * scale.x;

        matrix.m[ 4 ] = ( 2.0f * ( xy - wz ) ) * scale.y;

        matrix.m[ 5 ] = ( 1.0f - 2.0f * ( xx + zz ) ) * scale.y;

        matrix.m[ 6 ] = ( 2.0f * ( yz + wx ) ) * scale.y;

        matrix.m[ 8 ] = ( 2.0f * ( xz + wy ) ) * scale.z;

        matrix.m[ 9 ] = ( 2.0f * ( yz - wx ) ) * scale.z;

        matrix.m[ 10 ] = ( 1.0f - 2.0f * ( xx + yy ) ) * scale.z;

        matrix.m[ 12 ] = position.x;

        matrix.m[ 13 ] = position.y;

        matrix.m[ 14 ] = position.z;
        return matrix;
    }

    Matrix convert_lh_matrix_to_rh( const Matrix &matrix )
    {
        Matrix result = matrix;

        for( std::int32_t row = 0; row < 4; ++row )
        {
            result.m[ 2 * 4 + row ] = -result.m[ 2 * 4 + row ];
        }

        for( std::int32_t col = 0; col < 4; ++col )
        {
            result.m[ col * 4 + 2 ] = -result.m[ col * 4 + 2 ];
        }

        return result;
    }

    Model decode( std::span<const std::uint8_t> bytes )
    {
        if( bytes.size() < 8 || std::memcmp( bytes.data(), "XGBv1.00", 8 ) != 0 )
        {
            throw DecodeError( "XG file does not start with XGBv1.00" );
        }

        parser::Reader reader( bytes );
        reader.skip( 8, "XG magic" );

        Model model{};
        std::unordered_map<std::string, std::string> texture_node_to_url;

        while( reader.remaining() > 0 )
        {
            const std::string type = reader.read_pstring( "XG node type" );

            if( type == "dag" )
            {
                model.dag_nodes = read_dag( reader );
                break;
            }

            const std::string name = reader.read_pstring( "XG node name" );
            const std::string open_or_semicolon = reader.read_pstring( "XG declaration/definition marker" );

            if( open_or_semicolon == ";" )
            {
                continue;
            }

            if( open_or_semicolon != "{" )
            {
                throw DecodeError( "XG node definition missing opening brace for " + type + " " + name );
            }

            if( type == "xgBgGeometry" )
            {
                model.geometries.push_back( read_geometry_node( reader, name ) );
            }
            else if( type == "xgBgMatrix" )
            {
                MatrixNode matrix_node = read_matrix_node( reader, name );
                model.matrices.emplace( matrix_node.name, parser::static_matrix_from_node( matrix_node ) );
                model.matrix_nodes.push_back( std::move( matrix_node ) );
            }
            else if( type == "xgDagMesh" )
            {
                model.meshes.push_back( read_dag_mesh_node( reader, name ) );
            }
            else if( type == "xgDagTransform" )
            {
                model.transforms.push_back( read_dag_transform_node( reader, name ) );
            }
            else if( type == "xgMaterial" )
            {
                model.materials.push_back( read_material_node( reader, name ) );
            }
            else if( type == "xgMultiPassMaterial" )
            {
                MultiPassMaterial multipass{};
                multipass.name = name;

                multipass.material_names = read_multipass_material_node( reader );
                model.multipass_materials.push_back( std::move( multipass ) );
            }
            else if( type == "xgTexture" )
            {
                std::string url;
                while( true )
                {
                    const std::string property = reader.read_pstring( "xgTexture property" );

                    if( property == "}" )
                    {
                        break;
                    }

                    if( property == "url" )
                    {
                        url = piko::utils::lowercase_filename( reader.read_pstring( "xgTexture url" ) );
                    }
                    else if( property == "mipmap_depth" )
                    {
                        (void)reader.read_u32( "xgTexture mipmap_depth" );
                    }
                    else
                    {
                        throw DecodeError( "Unhandled xgTexture property: " + property );
                    }
                }

                texture_node_to_url.emplace( piko::utils::lowercase_filename( name ), url );
            }
            else if( type == "xgBone" )
            {
                model.bones.push_back( read_bone_node( reader, name ) );
            }
            else if( type == "xgEnvelope" )
            {
                model.envelopes.push_back( read_envelope_node( reader, name ) );
            }
            else if( type == "xgTime" )
            {
                skip_time_node( reader );
            }
            else if( type == "xgQuatInterpolator" )
            {
                model.quat_interpolators.push_back( read_quat_interpolator_node( reader, name ) );
            }
            else if( type == "xgVec3Interpolator" )
            {
                model.vec3_interpolators.push_back( read_vec3_interpolator_node( reader, name ) );
            }
            else if( type == "xgVertexInterpolator" )
            {
                model.vertex_interpolators.push_back( read_vertex_interpolator_node( reader, name ) );
            }
            else if( type == "xgNormalInterpolator" )
            {
                model.normal_interpolators.push_back( read_normal_interpolator_node( reader, name ) );
            }
            else if( type == "xgTexCoordInterpolator" )
            {
                model.texcoord_interpolators.push_back( read_texcoord_interpolator_node( reader, name ) );
            }
            else if( type == "xgShapeInterpolator" )
            {
                model.shape_interpolators.push_back( read_shape_interpolator_node( reader, name ) );
            }
            else
            {
                throw DecodeError( "Unhandled XG node type: " + type );
            }
        }

        for( Material &material : model.materials )
        {
            const auto it = texture_node_to_url.find( piko::utils::lowercase_filename( material.texture_name ) );

            if( it != texture_node_to_url.end() )
            {
                material.texture_name = it->second;
            }
        }

        return model;
    }
}
