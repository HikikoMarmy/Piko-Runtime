#include "GMO.hpp"
#include "Utils/BinaryUtil.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <sstream>
#include <utility>

namespace piko::filetypes::gmo::parser
{
    constexpr std::array<std::uint8_t, 12> magic{
        'O',
        'M',
        'G',
        '.',
        '0',
        '0',
        '.',
        '1',
        'P',
        'S',
        'P',
        0
    };

    struct Item
    {
        std::uint16_t type = 0;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
        std::string name;
        std::uint32_t extra_offset = 0;
        std::uint32_t extra_size = 0;
        std::uint32_t data_offset = 0;
        std::uint32_t data_size = 0;
        std::vector<Item> children;
    };

    struct Dequantization
    {
        std::uint32_t mask = 0;
        std::vector<float> values;
    };

    class Reader
    {
    public:
        explicit Reader( std::span<const std::uint8_t> bytes ) : bytes_( bytes )
        {
        }

        void require( std::size_t offset, std::size_t size, const char *context ) const
        {
            if( offset > bytes_.size() || size > bytes_.size() - offset )
            {
                std::ostringstream message;
                message << "Unexpected end of GMO while reading " << context;
                throw DecodeError( message.str() );
            }
        }

        std::uint16_t u16( std::size_t offset, const char *context ) const
        {
            require( offset, 2u, context );
            return piko::utils::read_le_u16( bytes_.data() + offset );
        }

        std::int16_t i16( std::size_t offset, const char *context ) const
        {
            return static_cast<std::int16_t>( u16( offset, context ) );
        }

        std::uint32_t u32( std::size_t offset, const char *context ) const
        {
            require( offset, 4u, context );
            return piko::utils::read_le_u32( bytes_.data() + offset );
        }

        float f32( std::size_t offset, const char *context ) const
        {
            const std::uint32_t bits = u32( offset, context );
            return std::bit_cast<float>( bits );
        }

        std::int8_t i8( std::size_t offset, const char *context ) const
        {
            require( offset, 1u, context );
            return static_cast<std::int8_t>( bytes_[ offset ] );
        }

        std::uint8_t u8( std::size_t offset, const char *context ) const
        {
            require( offset, 1u, context );
            return bytes_[ offset ];
        }

        std::span<const std::uint8_t> span( std::size_t offset, std::size_t size, const char *context ) const
        {
            require( offset, size, context );
            return bytes_.subspan( offset, size );
        }

        std::string string( std::size_t offset, std::size_t size, const char *context ) const
        {
            const auto source = span( offset, size, context );

            const auto end = std::find( source.begin(), source.end(), 0u );
            return std::string( source.begin(), end );
        }

        std::size_t size() const
        {
            return bytes_.size();
        }

    private:
        std::span<const std::uint8_t> bytes_;
    };

    std::size_t align_to( std::size_t value, std::size_t alignment )
    {
        if( alignment <= 1u )
        {
            return value;
        }

        return ( value + alignment - 1u ) & ~( alignment - 1u );
    }

    Item parse_item( const Reader &reader, std::uint32_t offset, std::uint32_t limit )
    {
        reader.require( offset, 8u, "chunk header" );
        Item item{};
        item.type = reader.u16( offset, "chunk type" );

        item.offset = offset;

        if( item.type >= 0x8000u )
        {
            item.size = reader.u32( offset + 4u, "attribute size" );

            if( item.size < 8u || item.size > limit - offset )
            {
                throw DecodeError( "Invalid GMO attribute size" );
            }

            item.data_offset = offset + 8u;

            item.data_size = item.size - 8u;
            return item;
        }

        reader.require( offset, 16u, "chunk header" );
        const std::uint16_t name_end = reader.u16( offset + 2u, "chunk name end" );
        item.size = reader.u32( offset + 4u, "chunk size" );
        const std::uint32_t data_end = reader.u32( offset + 8u, "chunk data end" );
        const std::uint32_t data_start = reader.u32( offset + 12u, "chunk data start" );

        if( item.size < 16u || item.size > limit - offset || name_end < 16u || name_end > data_start ||
            data_start > data_end || data_end > item.size )
        {
            throw DecodeError( "Invalid GMO chunk bounds" );
        }

        item.name = reader.string( offset + 16u, name_end - 16u, "chunk name" );

        item.extra_offset = offset + name_end;

        item.extra_size = data_start - name_end;

        item.data_offset = offset + data_start;

        item.data_size = data_end - data_start;

        std::uint32_t child_offset = offset + data_end;
        const std::uint32_t child_limit = offset + item.size;
        while( child_offset < child_limit )
        {
            Item child = parse_item( reader, child_offset, child_limit );
            child_offset += child.size;
            item.children.push_back( std::move( child ) );
        }

        if( child_offset != child_limit )
        {
            throw DecodeError( "GMO child chunks do not fill their parent" );
        }

        return item;
    }

    const Item *first_child( const Item &item, std::uint16_t type )
    {
        const auto found = std::find_if( item.children.begin(),
            item.children.end(),
            [ type ]( const Item &child )
            {
                return child.type == type;
            } );
        return found == item.children.end() ? nullptr : &*found;
    }

    std::vector<const Item *> children( const Item &item, std::uint16_t type )
    {
        std::vector<const Item *> output;

        for( const Item &child : item.children )
        {
            if( child.type == type )
            {
                output.push_back( &child );
            }
        }

        return output;
    }

    std::uint32_t reference_index( std::uint32_t value, std::uint32_t prefix )
    {
        if( ( value & 0xFFFFF000u ) != prefix )
        {
            return invalid_index;
        }

        return value & 0xFFFu;
    }

    Vec3 read_vec3( const Reader &reader, const Item &item )
    {
        if( item.data_size < 12u )
        {
            throw DecodeError( "GMO Vec3 attribute is too small" );
        }

        return Vec3{
            reader.f32( item.data_offset, "Vec3 x" ),
            reader.f32( item.data_offset + 4u, "Vec3 y" ),
            reader.f32( item.data_offset + 8u, "Vec3 z" )
        };
    }

    Vec4 read_vec4( const Reader &reader, const Item &item )
    {
        if( item.data_size < 16u )
        {
            throw DecodeError( "GMO Vec4 attribute is too small" );
        }

        return Vec4{
            reader.f32( item.data_offset, "Vec4 x" ),
            reader.f32( item.data_offset + 4u, "Vec4 y" ),
            reader.f32( item.data_offset + 8u, "Vec4 z" ),
            reader.f32( item.data_offset + 12u, "Vec4 w" )
        };
    }

    float half_to_float( std::uint16_t bits )
    {
        const std::uint32_t sign = static_cast<std::uint32_t>( bits & 0x8000u ) << 16u;
        const std::uint32_t exponent = ( bits >> 10u ) & 0x1Fu;
        const std::uint32_t mantissa = bits & 0x03FFu;
        std::uint32_t output = 0;

        if( exponent == 0u )
        {
            if( mantissa == 0u )
            {
                output = sign;
            }
            else
            {
                std::uint32_t normalized = mantissa;
                std::int32_t adjusted_exponent = -14;
                while( ( normalized & 0x0400u ) == 0u )
                {
                    normalized <<= 1u;
                    --adjusted_exponent;
                }

                normalized &= 0x03FFu;

                output =
                    sign | ( static_cast<std::uint32_t>( adjusted_exponent + 127 ) << 23u ) | ( normalized << 13u );
            }
        }
        else if( exponent == 0x1Fu )
        {
            output = sign | 0x7F800000u | ( mantissa << 13u );
        }
        else
        {
            output = sign | ( ( exponent + 112u ) << 23u ) | ( mantissa << 13u );
        }

        return std::bit_cast<float>( output );
    }

    std::vector<Dequantization> parse_dequantizations( const Reader &reader, const Item &model_item )
    {
        std::vector<Dequantization> output;

        for( const Item *attribute : children( model_item, 0x8015u ) )
        {
            if( attribute->data_size < 4u || ( attribute->data_size - 4u ) % 4u != 0u )
            {
                continue;
            }

            Dequantization value{};
            value.mask = reader.u32( attribute->data_offset, "dequantization mask" );
            const std::size_t count = ( attribute->data_size - 4u ) / 4u;
            value.values.reserve( count );

            for( std::size_t index = 0; index < count; ++index )
            {
                value.values.push_back(
                    reader.f32( attribute->data_offset + 4u + index * 4u, "dequantization value" ) );
            }

            output.push_back( std::move( value ) );
        }

        return output;
    }

    const Dequantization *find_dequantization( const std::vector<Dequantization> &values, std::uint32_t mask )
    {
        const auto found = std::find_if( values.begin(),
            values.end(),
            [ mask ]( const Dequantization &value )
            {
                return value.mask == mask;
            } );
        return found == values.end() ? nullptr : &*found;
    }

    float decode_signed_component( std::int32_t value,
        float divisor,
        std::size_t component,
        std::size_t dimensions,
        const Dequantization *dequantization )
    {
        const float normalized = static_cast<float>( value ) / divisor;

        if( dequantization == nullptr || dequantization->values.size() < dimensions * 2u )
        {
            return normalized;
        }

        return dequantization->values[ component ] + normalized * dequantization->values[ dimensions + component ];
    }

    float decode_unsigned_component( std::uint32_t value,
        float divisor,
        std::size_t component,
        std::size_t dimensions,
        const Dequantization *dequantization )
    {
        const float normalized = static_cast<float>( value ) / divisor;

        if( dequantization == nullptr || dequantization->values.size() < dimensions * 2u )
        {
            return normalized;
        }

        return dequantization->values[ component ] + normalized * dequantization->values[ dimensions + component ];
    }

    std::size_t component_size( std::uint32_t format )
    {
        if( format == 1u )
        {
            return 1u;
        }

        if( format == 2u )
        {
            return 2u;
        }

        if( format == 3u )
        {
            return 4u;
        }

        return 0u;
    }

    float read_numeric_component( const Reader &reader, std::size_t offset, std::uint32_t format, const char *context )
    {
        if( format == 1u )
        {
            return static_cast<float>( reader.i8( offset, context ) );
        }

        if( format == 2u )
        {
            return static_cast<float>( reader.i16( offset, context ) );
        }

        if( format == 3u )
        {
            return reader.f32( offset, context );
        }

        return 0.0f;
    }

    Vec4 decode_color( const Reader &reader, std::size_t offset, std::uint32_t format )
    {
        if( format == 4u )
        {
            const std::uint16_t value = reader.u16( offset, "RGB565 color" );
            return Vec4{
                static_cast<float>( value & 31u ) / 31.0f,
                static_cast<float>( ( value >> 5u ) & 63u ) / 63.0f,
                static_cast<float>( ( value >> 11u ) & 31u ) / 31.0f,
                1.0f
            };
        }

        if( format == 5u )
        {
            const std::uint16_t value = reader.u16( offset, "RGBA5551 color" );
            return Vec4{
                static_cast<float>( value & 31u ) / 31.0f,
                static_cast<float>( ( value >> 5u ) & 31u ) / 31.0f,
                static_cast<float>( ( value >> 10u ) & 31u ) / 31.0f,
                ( value & 0x8000u ) != 0u ? 1.0f : 0.0f
            };
        }

        if( format == 6u )
        {
            const std::uint16_t value = reader.u16( offset, "RGBA4444 color" );
            return Vec4{
                static_cast<float>( value & 15u ) / 15.0f,
                static_cast<float>( ( value >> 4u ) & 15u ) / 15.0f,
                static_cast<float>( ( value >> 8u ) & 15u ) / 15.0f,
                static_cast<float>( ( value >> 12u ) & 15u ) / 15.0f
            };
        }

        if( format == 7u )
        {
            return Vec4{
                static_cast<float>( reader.u8( offset, "RGBA8888 red" ) ) / 255.0f,
                static_cast<float>( reader.u8( offset + 1u, "RGBA8888 green" ) ) / 255.0f,
                static_cast<float>( reader.u8( offset + 2u, "RGBA8888 blue" ) ) / 255.0f,
                static_cast<float>( reader.u8( offset + 3u, "RGBA8888 alpha" ) ) / 255.0f
            };
        }

        return Vec4{};
    }

    std::size_t color_size( std::uint32_t format )
    {
        if( format >= 4u && format <= 6u )
        {
            return 2u;
        }

        if( format == 7u )
        {
            return 4u;
        }

        return 0u;
    }

    VertexArray parse_vertex_array( const Reader &reader,
        const Item &item,
        const std::vector<Dequantization> &dequantizations )
    {
        if( item.extra_size < 16u )
        {
            throw DecodeError( "GMO vertex array header is too small" );
        }

        VertexArray output{};
        output.name = item.name;

        output.vertex_type = reader.u32( item.extra_offset, "vertex type" );

        output.vertex_count = reader.u32( item.extra_offset + 4u, "vertex count" );

        output.morph_count = reader.u32( item.extra_offset + 8u, "morph count" );

        if( output.morph_count == 0u )
        {
            output.morph_count = 1u;
        }

        const std::uint32_t texture_format = output.vertex_type & 0x3u;
        const std::uint32_t color_format = ( output.vertex_type >> 2u ) & 0x7u;
        const std::uint32_t normal_format = ( output.vertex_type >> 5u ) & 0x3u;
        const std::uint32_t position_format = ( output.vertex_type >> 7u ) & 0x3u;
        const std::uint32_t weight_format = ( output.vertex_type >> 9u ) & 0x3u;
        output.index_format = ( output.vertex_type >> 11u ) & 0x3u;
        const std::uint32_t weight_count = weight_format == 0u ? 0u : ( ( output.vertex_type >> 14u ) & 0x7u ) + 1u;

        const std::size_t total_vertices = static_cast<std::size_t>( output.vertex_count ) * output.morph_count;
        std::size_t calculated_stride = 0u;
        const std::size_t weight_component_size = component_size( weight_format );

        if( weight_count != 0u )
        {
            calculated_stride = align_to( calculated_stride, weight_component_size );

            calculated_stride += weight_component_size * weight_count;
        }

        const std::size_t texture_component_size = component_size( texture_format );

        if( texture_format != 0u )
        {
            calculated_stride = align_to( calculated_stride, texture_component_size );

            calculated_stride += texture_component_size * 2u;
        }

        const std::size_t packed_color_size = color_size( color_format );

        if( packed_color_size != 0u )
        {
            calculated_stride = align_to( calculated_stride, packed_color_size );

            calculated_stride += packed_color_size;
        }

        const std::size_t normal_component_size = component_size( normal_format );

        if( normal_format != 0u )
        {
            calculated_stride = align_to( calculated_stride, normal_component_size );

            calculated_stride += normal_component_size * 3u;
        }

        const std::size_t position_component_size = component_size( position_format );

        if( position_component_size == 0u )
        {
            throw DecodeError( "GMO vertex array has no position format" );
        }

        calculated_stride = align_to( calculated_stride, position_component_size );

        calculated_stride += position_component_size * 3u;

        const std::size_t expected_size = total_vertices * calculated_stride;

        if( total_vertices == 0u || expected_size > item.data_size || item.data_size - expected_size >= 4u )
        {
            throw DecodeError( "GMO vertex array has an invalid byte count: " + item.name );
        }

        output.stride = static_cast<std::uint32_t>( calculated_stride );

        const Dequantization *position_dequantization = find_dequantization( dequantizations, 0x180u );
        const Dequantization *texture_dequantization = find_dequantization( dequantizations, 0x3u );

        output.vertices.reserve( output.vertex_count );

        for( std::uint32_t vertex_index = 0; vertex_index < output.vertex_count; ++vertex_index )
        {
            const std::size_t base = item.data_offset + static_cast<std::size_t>( vertex_index ) * output.stride;
            std::size_t cursor = 0u;
            Vertex vertex{};
            vertex.weight_count = weight_count;

            const std::size_t weight_size = component_size( weight_format );

            if( weight_count != 0u )
            {
                cursor = align_to( cursor, weight_size );
                float total = 0.0f;

                for( std::uint32_t index = 0; index < weight_count && index < vertex.weights.size(); ++index )
                {
                    float value = 0.0f;
                    const std::size_t weight_offset = base + cursor + index * weight_size;

                    if( weight_format == 1u )
                    {
                        value = static_cast<float>( reader.u8( weight_offset, "vertex weight" ) ) / 128.0f;
                    }
                    else if( weight_format == 2u )
                    {
                        value = static_cast<float>( reader.u16( weight_offset, "vertex weight" ) ) / 32768.0f;
                    }
                    else
                    {
                        value = reader.f32( weight_offset, "vertex weight" );
                    }

                    vertex.weights[ index ] = std::max( value, 0.0f );

                    total += vertex.weights[ index ];
                }

                if( total > 0.000001f )
                {
                    for( std::uint32_t index = 0; index < weight_count && index < vertex.weights.size(); ++index )
                    {
                        vertex.weights[ index ] /= total;
                    }
                }

                cursor += weight_size * weight_count;
            }

            const std::size_t texture_size = component_size( texture_format );

            if( texture_format != 0u )
            {
                cursor = align_to( cursor, texture_size );

                if( texture_format == 1u )
                {
                    const std::uint8_t u = reader.u8( base + cursor, "texture coordinate u" );
                    const std::uint8_t v = reader.u8( base + cursor + texture_size, "texture coordinate v" );
                    vertex.uv.x = decode_unsigned_component( u, 128.0f, 0u, 2u, texture_dequantization );

                    vertex.uv.y = decode_unsigned_component( v, 128.0f, 1u, 2u, texture_dequantization );
                }
                else if( texture_format == 2u )
                {
                    const std::uint16_t u = reader.u16( base + cursor, "texture coordinate u" );
                    const std::uint16_t v = reader.u16( base + cursor + texture_size, "texture coordinate v" );
                    vertex.uv.x = decode_unsigned_component( u, 32768.0f, 0u, 2u, texture_dequantization );

                    vertex.uv.y = decode_unsigned_component( v, 32768.0f, 1u, 2u, texture_dequantization );
                }
                else
                {
                    const float u = reader.f32( base + cursor, "texture coordinate u" );
                    const float v = reader.f32( base + cursor + texture_size, "texture coordinate v" );
                    vertex.uv = Vec2{ u, v };
                }

                vertex.has_uv = true;

                cursor += texture_size * 2u;
            }

            if( packed_color_size != 0u )
            {
                cursor = align_to( cursor, packed_color_size );

                vertex.color = decode_color( reader, base + cursor, color_format );

                vertex.has_color = true;

                cursor += packed_color_size;
            }

            const std::size_t normal_size = component_size( normal_format );

            if( normal_format != 0u )
            {
                cursor = align_to( cursor, normal_size );
                const float x = read_numeric_component( reader, base + cursor, normal_format, "normal x" );
                const float y =
                    read_numeric_component( reader, base + cursor + normal_size, normal_format, "normal y" );

                const float z =
                    read_numeric_component( reader, base + cursor + normal_size * 2u, normal_format, "normal z" );

                if( normal_format == 1u )
                {
                    vertex.normal = Vec3{ x / 127.0f, y / 127.0f, z / 127.0f };
                }
                else if( normal_format == 2u )
                {
                    vertex.normal = Vec3{ x / 32767.0f, y / 32767.0f, z / 32767.0f };
                }
                else
                {
                    vertex.normal = Vec3{ x, y, z };
                }

                vertex.has_normal = true;

                cursor += normal_size * 3u;
            }

            const std::size_t position_size = component_size( position_format );

            if( position_format == 0u )
            {
                throw DecodeError( "GMO vertex array has no position format" );
            }

            cursor = align_to( cursor, position_size );
            const float x = read_numeric_component( reader, base + cursor, position_format, "position x" );
            const float y =
                read_numeric_component( reader, base + cursor + position_size, position_format, "position y" );

            const float z =
                read_numeric_component( reader, base + cursor + position_size * 2u, position_format, "position z" );

            if( position_format == 1u )
            {
                vertex.position = Vec3{
                    decode_signed_component( static_cast<std::int32_t>( x ), 127.0f, 0u, 3u, position_dequantization ),
                    decode_signed_component( static_cast<std::int32_t>( y ), 127.0f, 1u, 3u, position_dequantization ),
                    decode_signed_component( static_cast<std::int32_t>( z ), 127.0f, 2u, 3u, position_dequantization )
                };
            }
            else if( position_format == 2u )
            {
                vertex.position = Vec3{
                    decode_signed_component( static_cast<std::int32_t>( x ),
                        32767.0f,
                        0u,
                        3u,
                        position_dequantization ),
                    decode_signed_component( static_cast<std::int32_t>( y ),
                        32767.0f,
                        1u,
                        3u,
                        position_dequantization ),
                    decode_signed_component( static_cast<std::int32_t>( z ), 32767.0f, 2u, 3u, position_dequantization )
                };
            }
            else
            {
                vertex.position = Vec3{ x, y, z };
            }

            output.vertices.push_back( vertex );
        }

        return output;
    }

    DrawPrimitive parse_draw( const Reader &reader, const Item &attribute, const Part &part )
    {
        if( attribute.data_size < 16u )
        {
            throw DecodeError( "GMO draw attribute is too small" );
        }

        DrawPrimitive output{};
        output.array_index = reference_index( reader.u32( attribute.data_offset, "draw array reference" ), 0x71000u );

        output.flags = reader.u32( attribute.data_offset + 4u, "draw flags" );

        output.primitive_type = output.flags & 0xFFu;
        const std::uint32_t count = reader.u32( attribute.data_offset + 8u, "draw index count" );

        if( output.array_index >= part.arrays.size() )
        {
            throw DecodeError( "GMO draw references an invalid vertex array" );
        }

        output.indices.reserve( count );

        if( ( output.flags & 0x100u ) != 0u )
        {
            if( attribute.data_size < 20u )
            {
                throw DecodeError( "Sequential GMO draw is missing its start index" );
            }

            const std::uint32_t start = reader.u32( attribute.data_offset + 16u, "draw start index" );

            for( std::uint32_t index = 0; index < count; ++index )
            {
                output.indices.push_back( start + index );
            }

            return output;
        }

        const std::uint32_t index_format = part.arrays[ output.array_index ].index_format;
        const std::size_t index_size = component_size( index_format );

        if( index_size == 0u || 16u + static_cast<std::size_t>( count ) * index_size > attribute.data_size )
        {
            throw DecodeError( "GMO draw has an unsupported or truncated index stream" );
        }

        for( std::uint32_t index = 0; index < count; ++index )
        {
            const std::size_t offset = attribute.data_offset + 16u + static_cast<std::size_t>( index ) * index_size;

            if( index_format == 1u )
            {
                output.indices.push_back( reader.u8( offset, "8-bit index" ) );
            }
            else if( index_format == 2u )
            {
                output.indices.push_back( reader.u16( offset, "16-bit index" ) );
            }
            else
            {
                output.indices.push_back( reader.u32( offset, "32-bit index" ) );
            }
        }

        return output;
    }

    Part parse_part( const Reader &reader, const Item &item, const std::vector<Dequantization> &dequantizations )
    {
        Part output{};
        output.name = item.name;

        for( const Item *array : children( item, 7u ) )
        {
            output.arrays.push_back( parse_vertex_array( reader, *array, dequantizations ) );
        }

        for( const Item *mesh_item : children( item, 6u ) )
        {
            Mesh mesh{};
            mesh.name = mesh_item->name;

            if( const Item *material = first_child( *mesh_item, 0x8061u ) )
            {
                if( material->data_size >= 4u )
                {
                    mesh.material_index =
                        reference_index( reader.u32( material->data_offset, "material reference" ), 0x82000u );
                }
            }

            if( const Item *palette = first_child( *mesh_item, 0x8062u ) )
            {
                if( palette->data_size >= 4u )
                {
                    const std::uint32_t count = reader.u32( palette->data_offset, "bone palette count" );

                    if( 4u + static_cast<std::size_t>( count ) * 4u <= palette->data_size )
                    {
                        mesh.bone_palette.reserve( count );

                        for( std::uint32_t index = 0; index < count; ++index )
                        {
                            mesh.bone_palette.push_back(
                                reader.u32( palette->data_offset + 4u + index * 4u, "bone palette index" ) );
                        }
                    }
                }
            }

            for( const Item *draw : children( *mesh_item, 0x8066u ) )
            {
                mesh.draws.push_back( parse_draw( reader, *draw, output ) );
            }

            output.meshes.push_back( std::move( mesh ) );
        }

        return output;
    }

    Node parse_node( const Reader &reader, const Item &item )
    {
        Node output{};
        output.name = item.name;

        if( const Item *parent = first_child( item, 0x8041u ) )
        {
            if( parent->data_size >= 4u )
            {
                output.parent_index =
                    reference_index( reader.u32( parent->data_offset, "parent reference" ), 0x41000u );
            }
        }

        if( const Item *translation = first_child( item, 0x8048u ) )
        {
            output.translation = read_vec3( reader, *translation );
        }

        if( const Item *rotation = first_child( item, 0x804Bu ) )
        {
            output.rotation = read_vec4( reader, *rotation );
        }

        if( const Item *scale = first_child( item, 0x804Cu ) )
        {
            output.scale = read_vec3( reader, *scale );
        }

        for( const Item *part : children( item, 0x804Eu ) )
        {
            if( part->data_size >= 4u )
            {
                const std::uint32_t part_index =
                    reference_index( reader.u32( part->data_offset, "part reference" ), 0x51000u );

                if( part_index != invalid_index &&
                    std::find( output.part_indices.begin(), output.part_indices.end(), part_index ) ==
                        output.part_indices.end() )
                {
                    output.part_indices.push_back( part_index );
                }
            }
        }

        if( !output.part_indices.empty() )
        {
            output.part_index = output.part_indices.front();
        }

        if( const Item *bones = first_child( item, 0x8044u ) )
        {
            if( bones->data_size >= 4u )
            {
                const std::uint32_t count = reader.u32( bones->data_offset, "bone table count" );

                if( 4u + static_cast<std::size_t>( count ) * 4u <= bones->data_size )
                {
                    output.bone_nodes.reserve( count );

                    for( std::uint32_t index = 0; index < count; ++index )
                    {
                        output.bone_nodes.push_back(
                            reference_index( reader.u32( bones->data_offset + 4u + index * 4u, "bone node reference" ),
                                0x41000u ) );
                    }
                }
            }
        }

        if( const Item *matrices = first_child( item, 0x8045u ) )
        {
            if( matrices->data_size >= 4u )
            {
                const std::uint32_t count = reader.u32( matrices->data_offset, "bind matrix count" );

                if( 4u + static_cast<std::size_t>( count ) * 64u <= matrices->data_size )
                {
                    output.inverse_bind_matrices.reserve( count );

                    for( std::uint32_t matrix_index = 0; matrix_index < count; ++matrix_index )
                    {
                        Matrix matrix{};
                        const std::size_t matrix_offset =
                            matrices->data_offset + 4u + static_cast<std::size_t>( matrix_index ) * 64u;

                        for( std::size_t component = 0; component < 16u; ++component )
                        {
                            matrix.values[ component ] =
                                reader.f32( matrix_offset + component * 4u, "bind matrix component" );
                        }

                        output.inverse_bind_matrices.push_back( matrix );
                    }
                }
            }
        }

        return output;
    }

    Material parse_material( const Reader &reader, const Item &item )
    {
        Material output{};
        output.name = item.name;

        if( const Item *diffuse = first_child( item, 0x8082u ) )
        {
            output.diffuse = read_vec4( reader, *diffuse );
        }

        for( const Item *state : children( item, 0x8081u ) )
        {
            if( state->data_size >= 8u )
            {
                output.states.push_back( std::array<std::uint32_t, 2>{
                    reader.u32( state->data_offset, "material state key" ),
                    reader.u32( state->data_offset + 4u, "material state value" )
                } );
            }
        }

        for( const Item *layer : children( item, 9u ) )
        {
            if( output.texture_index == invalid_index )
            {
                if( const Item *texture = first_child( *layer, 0x8091u ) )
                {
                    if( texture->data_size >= 4u )
                    {
                        output.texture_index =
                            reference_index( reader.u32( texture->data_offset, "texture reference" ), 0xA2000u );
                    }
                }
            }

            if( !output.has_blend_function )
            {
                if( const Item *blend = first_child( *layer, 0x8094u ) )
                {
                    if( blend->data_size >= 12u )
                    {
                        output.has_blend_function = true;

                        output.blend_operation = reader.u32( blend->data_offset, "blend operation" );

                        output.blend_source = reader.u32( blend->data_offset + 4u, "blend source" );

                        output.blend_destination = reader.u32( blend->data_offset + 8u, "blend destination" );
                    }
                }
            }

            if( output.texture_index != invalid_index && output.has_blend_function )
            {
                break;
            }
        }

        return output;
    }

    Texture parse_texture( const Reader &reader, const Item &item )
    {
        Texture output{};
        output.name = item.name;

        if( const Item *source = first_child( item, 0x8012u ) )
        {
            output.source_name = reader.string( source->data_offset, source->data_size, "texture source name" );
        }

        if( const Item *embedded = first_child( item, 0x8013u ) )
        {
            const auto bytes = reader.span( embedded->data_offset, embedded->data_size, "embedded texture" );
            output.embedded_data.assign( bytes.begin(), bytes.end() );
        }

        return output;
    }

    Curve parse_curve( const Reader &reader, const Item &item )
    {
        if( item.extra_size < 16u )
        {
            throw DecodeError( "GMO curve header is too small" );
        }

        Curve output{};
        output.name = item.name;

        output.format = reader.u32( item.extra_offset, "curve format" );

        output.dimensions = reader.u32( item.extra_offset + 4u, "curve dimensions" );
        const std::uint32_t key_count = reader.u32( item.extra_offset + 8u, "curve key count" );

        if( output.dimensions == 0u || output.dimensions > 64u )
        {
            throw DecodeError( "GMO curve has invalid dimensions" );
        }

        const bool float_components = ( output.format & 0x80u ) == 0u;
        const std::size_t component_size = float_components ? 4u : 2u;
        const std::size_t packed_record_size = static_cast<std::size_t>( output.dimensions + 1u ) * component_size;
        const std::size_t record_stride =
            output.format == 0x80u ? align_to( packed_record_size, 4u ) : packed_record_size;

        const std::size_t required_size =
            key_count == 0u ? 0u : static_cast<std::size_t>( key_count - 1u ) * record_stride + packed_record_size;

        if( required_size > item.data_size )
        {
            throw DecodeError( "GMO curve key data is truncated" );
        }

        output.times.reserve( key_count );
        output.values.reserve( static_cast<std::size_t>( key_count ) * output.dimensions );

        for( std::uint32_t key_index = 0; key_index < key_count; ++key_index )
        {
            const std::size_t offset = item.data_offset + static_cast<std::size_t>( key_index ) * record_stride;

            if( float_components )
            {
                output.times.push_back( reader.f32( offset, "curve key time" ) );
            }
            else
            {
                output.times.push_back( half_to_float( reader.u16( offset, "curve key time" ) ) );
            }

            for( std::uint32_t component = 0; component < output.dimensions; ++component )
            {
                const std::size_t value_offset =
                    offset + component_size + static_cast<std::size_t>( component ) * component_size;

                if( float_components )
                {
                    output.values.push_back( reader.f32( value_offset, "curve key value" ) );
                }
                else
                {
                    output.values.push_back( half_to_float( reader.u16( value_offset, "curve key value" ) ) );
                }
            }
        }

        return output;
    }

    Motion parse_motion( const Reader &reader, const Item &item )
    {
        Motion output{};
        output.name = item.name;

        if( const Item *range = first_child( item, 0x80B1u ) )
        {
            if( range->data_size >= 8u )
            {
                output.start_frame = reader.f32( range->data_offset, "motion start frame" );

                output.end_frame = reader.f32( range->data_offset + 4u, "motion end frame" );
            }
        }

        if( const Item *playback_rate = first_child( item, 0x80B2u ) )
        {
            if( playback_rate->data_size >= 4u )
            {
                output.playback_rate = reader.f32( playback_rate->data_offset, "motion playback rate" );

                output.has_playback_rate = true;
            }
        }

        for( const Item *curve : children( item, 12u ) )
        {
            output.curves.push_back( parse_curve( reader, *curve ) );
        }

        for( const Item *channel : children( item, 0x80B3u ) )
        {
            if( channel->data_size < 16u )
            {
                continue;
            }

            MotionChannel value{};
            value.node_index = reference_index( reader.u32( channel->data_offset, "motion node reference" ), 0x41000u );

            value.target_attribute = reader.u32( channel->data_offset + 4u, "motion target attribute" );

            value.curve_index =
                reference_index( reader.u32( channel->data_offset + 12u, "motion curve reference" ), 0xC0000u );

            output.channels.push_back( value );
        }

        return output;
    }

    Model parse_model( const Reader &reader, const Item &model_item )
    {
        Model output{};
        output.name = model_item.name;

        const auto dequantizations = parse_dequantizations( reader, model_item );

        for( const Item *node : children( model_item, 4u ) )
        {
            output.nodes.push_back( parse_node( reader, *node ) );
        }

        for( const Item *part : children( model_item, 5u ) )
        {
            output.parts.push_back( parse_part( reader, *part, dequantizations ) );
        }

        for( const Item *material : children( model_item, 8u ) )
        {
            output.materials.push_back( parse_material( reader, *material ) );
        }

        for( const Item *texture : children( model_item, 10u ) )
        {
            output.textures.push_back( parse_texture( reader, *texture ) );
        }

        for( const Item *motion : children( model_item, 11u ) )
        {
            output.motions.push_back( parse_motion( reader, *motion ) );
        }

        return output;
    }
}

namespace piko::filetypes::gmo
{
    DecodeError::DecodeError( const std::string &message ) : std::runtime_error( message )
    {
    }

    bool is_gmo( std::span<const std::uint8_t> bytes )
    {
        return bytes.size() >= 16u && std::equal( parser::magic.begin(), parser::magic.end(), bytes.begin() );
    }

    Model decode( std::span<const std::uint8_t> bytes )
    {
        if( !is_gmo( bytes ) )
        {
            throw DecodeError( "Not a GMO file" );
        }

        const parser::Reader reader( bytes );
        const parser::Item root = parser::parse_item( reader, 16u, static_cast<std::uint32_t>( reader.size() ) );

        if( root.type != 2u )
        {
            throw DecodeError( "GMO root chunk is not type 2" );
        }

        const parser::Item *model = parser::first_child( root, 3u );

        if( model == nullptr )
        {
            throw DecodeError( "GMO has no model chunk" );
        }

        return parser::parse_model( reader, *model );
    }

    std::vector<PakEntry> decode_pak_entries( std::span<const std::uint8_t> bytes, bool decode_model_names )
    {
        const auto magic_position =
            std::search( bytes.begin(), bytes.end(), parser::magic.begin(), parser::magic.end() );

        if( magic_position == bytes.end() )
        {
            throw DecodeError( "PAK contains no GMO files" );
        }

        const std::size_t header_size = static_cast<std::size_t>( magic_position - bytes.begin() );

        if( header_size == 0u || header_size % 4u != 0u )
        {
            throw DecodeError( "PAK has an invalid GMO size table" );
        }

        const std::size_t entry_count = header_size / 4u;
        parser::Reader reader( bytes );

        std::vector<PakEntry> output;
        output.reserve( entry_count );
        std::uint64_t offset = header_size;

        for( std::size_t index = 0; index < entry_count; ++index )
        {
            const std::uint32_t size = reader.u32( index * 4u, "PAK GMO size" );

            if( size < 16u || offset + size > bytes.size() )
            {
                throw DecodeError( "PAK GMO entry extends beyond the package" );
            }

            const auto entry_bytes = bytes.subspan( static_cast<std::size_t>( offset ), size );

            if( !is_gmo( entry_bytes ) )
            {
                throw DecodeError( "PAK size table does not point to GMO data" );
            }

            std::string model_name;

            if( decode_model_names )
            {
                try
                {
                    model_name = decode( entry_bytes ).name;
                }
                catch( const DecodeError & )
                {
                }
            }

            output.push_back( PakEntry{
                static_cast<std::uint32_t>( index ),
                static_cast<std::uint32_t>( offset ),
                size,
                std::move( model_name )
            } );

            offset += size;
        }

        if( offset != bytes.size() )
        {
            throw DecodeError( "PAK size table does not cover the complete package" );
        }

        return output;
    }
}
