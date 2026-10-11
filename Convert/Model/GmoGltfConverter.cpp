#include "Model/GmoGltfConverter.hpp"

#include "Model/GltfUtil.hpp"

#include "Utility/JsonUtil.hpp"

#include "FileTypes/GMO/GMO.hpp"
#include "Image/PngWriter.hpp"
#include "Utils/StrUtil.hpp"
#include "Utils/BinaryUtil.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <compare>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <map>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

namespace piko::convert::gmo_gltf
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
        std::int32_t component_type = component_float;
        std::size_t count = 0;
        std::string type;
        std::vector<double> minimum;
        std::vector<double> maximum;
    };

    struct Primitive
    {
        std::int32_t position = -1;
        std::int32_t normal = -1;
        std::int32_t texcoord = -1;
        std::int32_t color = -1;
        std::int32_t joints0 = -1;
        std::int32_t weights0 = -1;
        std::int32_t joints1 = -1;
        std::int32_t weights1 = -1;
        std::int32_t indices = -1;
        std::int32_t material = -1;
        std::uint32_t source_primitive = 0;
        std::uint32_t source_flags = 0;
        bool skinned = false;
    };

    struct Mesh
    {
        std::string name;
        std::vector<Primitive> primitives;
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

    struct Material
    {
        std::string name;
        std::array<double, 4> diffuse{
            1.0,
            1.0,
            1.0,
            1.0
        };

        std::string texture_name;
        std::int32_t texture = -1;
        bool texture_contains_alpha = false;
        bool uses_texture_alpha = false;
        bool blend = false;
        std::uint32_t blend_type = 0;
        std::uint32_t shading_type = 2;
        bool has_gmo_blend_function = false;
        std::uint32_t gmo_blend_operation = 0;
        std::uint32_t gmo_blend_source = 0;
        std::uint32_t gmo_blend_destination = 0;
        std::int32_t alpha_variant = -1;
        std::vector<std::array<std::uint32_t, 2>> states;
    };

    struct Image
    {
        std::string name;
        std::string source_name;
        std::string uri;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> rgba;
    };

    struct Texture
    {
        std::string name;
        std::int32_t source = -1;
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
        float start_frame = 0.0f;
        float end_frame = 0.0f;
        float playback_rate = 0.0f;
        bool has_playback_rate = false;
        std::vector<AnimationSampler> samplers;
        std::vector<AnimationChannel> channels;
    };

    struct Document
    {
        std::vector<std::uint8_t> buffer;
        std::vector<BufferView> buffer_views;
        std::vector<Accessor> accessors;
        std::vector<Material> materials;
        std::vector<Image> images;
        std::vector<Texture> textures;
        std::vector<Mesh> meshes;
        std::vector<Node> nodes;
        std::vector<Skin> skins;
        std::vector<Animation> animations;
        std::vector<std::int32_t> scene_nodes;
    };

    std::uint16_t read_le16( std::span<const std::uint8_t> bytes, std::size_t offset )
    {
        if( offset > bytes.size() || 2u > bytes.size() - offset )
        {
            throw std::runtime_error( "Embedded GMO image is truncated" );
        }

        return piko::utils::read_le_u16( bytes.data() + offset );
    }

    std::uint32_t read_le32( std::span<const std::uint8_t> bytes, std::size_t offset )
    {
        if( offset > bytes.size() || 4u > bytes.size() - offset )
        {
            throw std::runtime_error( "Embedded GMO image is truncated" );
        }

        return piko::utils::read_le_u32( bytes.data() + offset );
    }

    std::int32_t read_le_i32( std::span<const std::uint8_t> bytes, std::size_t offset )
    {
        return static_cast<std::int32_t>( read_le32( bytes, offset ) );
    }

    std::span<const std::uint8_t> embedded_image_file( std::span<const std::uint8_t> bytes )
    {
        if( bytes.size() < 4u )
        {
            throw std::runtime_error( "Embedded GMO image header is truncated" );
        }

        const std::uint32_t size = read_le32( bytes, 0u );

        if( size == 0u || size > bytes.size() - 4u )
        {
            throw std::runtime_error( "Embedded GMO image size is invalid" );
        }

        return bytes.subspan( 4u, size );
    }

    Image decode_bmp_image( std::span<const std::uint8_t> bytes,
        std::string name,
        std::string source_name,
        std::string uri )
    {
        if( bytes.size() < 54u || bytes[ 0u ] != 'B' || bytes[ 1u ] != 'M' )
        {
            throw std::runtime_error( "Embedded GMO BMP header is invalid" );
        }

        const std::uint32_t pixel_offset = read_le32( bytes, 10u );
        const std::uint32_t dib_size = read_le32( bytes, 14u );

        if( dib_size < 40u || 14u + dib_size > bytes.size() )
        {
            throw std::runtime_error( "Embedded GMO BMP DIB header is unsupported" );
        }

        const std::int32_t signed_width = read_le_i32( bytes, 18u );
        const std::int32_t signed_height = read_le_i32( bytes, 22u );
        const std::uint16_t planes = read_le16( bytes, 26u );
        const std::uint16_t bits_per_pixel = read_le16( bytes, 28u );
        const std::uint32_t compression = read_le32( bytes, 30u );

        if( signed_width <= 0 || signed_height == 0 || planes != 1u || compression != 0u )
        {
            throw std::runtime_error( "Embedded GMO BMP layout is unsupported" );
        }

        if( bits_per_pixel != 4u && bits_per_pixel != 8u && bits_per_pixel != 24u && bits_per_pixel != 32u )
        {
            throw std::runtime_error( "Embedded GMO BMP pixel format is unsupported" );
        }

        const std::uint32_t width = static_cast<std::uint32_t>( signed_width );
        const std::uint32_t height = static_cast<std::uint32_t>(
            signed_height < 0 ? -static_cast<std::int64_t>( signed_height ) : signed_height );

        const std::uint64_t row_bits = static_cast<std::uint64_t>( width ) * bits_per_pixel;
        const std::uint64_t row_stride = ( ( row_bits + 31u ) / 32u ) * 4u;
        const std::uint64_t pixel_bytes = row_stride * height;

        if( pixel_offset > bytes.size() || pixel_bytes > bytes.size() - pixel_offset )
        {
            throw std::runtime_error( "Embedded GMO BMP pixel data is truncated" );
        }

        std::vector<std::array<std::uint8_t, 4>> palette;

        if( bits_per_pixel == 4u || bits_per_pixel == 8u )
        {
            const std::size_t palette_offset = 14u + dib_size;

            if( palette_offset > pixel_offset )
            {
                throw std::runtime_error( "Embedded GMO BMP palette offset is invalid" );
            }

            const std::uint32_t declared_colors = dib_size >= 40u ? read_le32( bytes, 46u ) : 0u;
            const std::size_t available_colors = ( pixel_offset - palette_offset ) / 4u;
            const std::size_t maximum_colors = bits_per_pixel == 4u ? 16u : 256u;
            const std::size_t requested_colors = declared_colors == 0u ? maximum_colors : declared_colors;
            const std::size_t palette_size = std::min( requested_colors, available_colors );

            if( palette_size == 0u )
            {
                throw std::runtime_error( "Embedded GMO BMP palette is empty" );
            }

            palette.resize( palette_size );

            for( std::size_t index = 0; index < palette_size; ++index )
            {
                const std::size_t offset = palette_offset + index * 4u;
                palette[ index ] = {
                    bytes[ offset + 2u ],
                    bytes[ offset + 1u ],
                    bytes[ offset ],
                    255u
                };
            }
        }

        Image image{};
        image.name = std::move( name );

        image.source_name = std::move( source_name );

        image.uri = std::move( uri );

        image.width = width;

        image.height = height;
        image.rgba.resize( static_cast<std::size_t>( width ) * height * 4u );
        const bool top_down = signed_height < 0;

        for( std::uint32_t y = 0; y < height; ++y )
        {
            const std::uint32_t source_y = top_down ? y : height - 1u - y;
            const std::size_t source_row = pixel_offset + static_cast<std::size_t>( row_stride * source_y );

            for( std::uint32_t x = 0; x < width; ++x )
            {
                const std::size_t destination = ( static_cast<std::size_t>( y ) * width + x ) * 4u;

                if( bits_per_pixel == 4u || bits_per_pixel == 8u )
                {
                    std::uint8_t palette_index = 0u;

                    if( bits_per_pixel == 4u )
                    {
                        const std::uint8_t packed = bytes[ source_row + x / 2u ];
                        palette_index = ( x & 1u ) == 0u ? packed >> 4u : packed & 0x0Fu;
                    }
                    else
                    {
                        palette_index = bytes[ source_row + x ];
                    }

                    if( palette_index >= palette.size() )
                    {
                        throw std::runtime_error( "Embedded GMO BMP palette index is invalid" );
                    }

                    const auto &color = palette[ palette_index ];
                    std::copy( color.begin(),
                        color.end(),
                        image.rgba.begin() + static_cast<std::ptrdiff_t>( destination ) );
                }
                else
                {
                    const std::size_t source = source_row + static_cast<std::size_t>( x ) * ( bits_per_pixel / 8u );
                    image.rgba[ destination ] = bytes[ source + 2u ];

                    image.rgba[ destination + 1u ] = bytes[ source + 1u ];

                    image.rgba[ destination + 2u ] = bytes[ source ];

                    image.rgba[ destination + 3u ] = bits_per_pixel == 32u ? bytes[ source + 3u ] : 255u;
                }
            }
        }

        return image;
    }

    Image decode_tga_image( std::span<const std::uint8_t> bytes,
        std::string name,
        std::string source_name,
        std::string uri )
    {
        if( bytes.size() < 18u )
        {
            throw std::runtime_error( "Embedded GMO TGA header is truncated" );
        }

        const std::uint8_t id_length = bytes[ 0u ];
        const std::uint8_t color_map_type = bytes[ 1u ];
        const std::uint8_t image_type = bytes[ 2u ];
        const std::uint16_t color_map_first = read_le16( bytes, 3u );
        const std::uint16_t color_map_length = read_le16( bytes, 5u );
        const std::uint8_t color_map_depth = bytes[ 7u ];
        const std::uint16_t width = read_le16( bytes, 12u );
        const std::uint16_t height = read_le16( bytes, 14u );
        const std::uint8_t pixel_depth = bytes[ 16u ];
        const std::uint8_t descriptor = bytes[ 17u ];

        if( width == 0u || height == 0u )
        {
            throw std::runtime_error( "Embedded GMO TGA dimensions are invalid" );
        }

        if( image_type != 1u && image_type != 2u )
        {
            throw std::runtime_error( "Embedded GMO TGA image type is unsupported" );
        }

        if( image_type == 1u && ( color_map_type != 1u || pixel_depth != 8u || color_map_depth != 32u ) )
        {
            throw std::runtime_error( "Embedded GMO indexed TGA format is unsupported" );
        }

        if( image_type == 2u && ( color_map_type != 0u || ( pixel_depth != 24u && pixel_depth != 32u ) ) )
        {
            throw std::runtime_error( "Embedded GMO true-color TGA format is unsupported" );
        }

        std::size_t offset = 18u + id_length;
        std::vector<std::array<std::uint8_t, 4>> palette;

        if( image_type == 1u )
        {
            const std::size_t palette_bytes = static_cast<std::size_t>( color_map_length ) * 4u;

            if( offset > bytes.size() || palette_bytes > bytes.size() - offset )
            {
                throw std::runtime_error( "Embedded GMO TGA palette is truncated" );
            }

            palette.resize( color_map_length );

            for( std::size_t index = 0; index < palette.size(); ++index )
            {
                const std::size_t source = offset + index * 4u;
                palette[ index ] = {
                    bytes[ source + 2u ],
                    bytes[ source + 1u ],
                    bytes[ source ],
                    bytes[ source + 3u ]
                };
            }

            offset += palette_bytes;
        }

        const std::size_t source_pixel_size = pixel_depth / 8u;
        const std::size_t source_bytes = static_cast<std::size_t>( width ) * height * source_pixel_size;

        if( offset > bytes.size() || source_bytes > bytes.size() - offset )
        {
            throw std::runtime_error( "Embedded GMO TGA pixels are truncated" );
        }

        Image image{};
        image.name = std::move( name );

        image.source_name = std::move( source_name );

        image.uri = std::move( uri );

        image.width = width;

        image.height = height;
        image.rgba.resize( static_cast<std::size_t>( width ) * height * 4u );
        const bool top_origin = ( descriptor & 0x20u ) != 0u;
        const bool right_origin = ( descriptor & 0x10u ) != 0u;

        for( std::uint32_t source_y = 0; source_y < height; ++source_y )
        {
            for( std::uint32_t source_x = 0; source_x < width; ++source_x )
            {
                const std::uint32_t x = right_origin ? width - 1u - source_x : source_x;
                const std::uint32_t y = top_origin ? source_y : height - 1u - source_y;
                const std::size_t source =
                    offset + ( static_cast<std::size_t>( source_y ) * width + source_x ) * source_pixel_size;

                const std::size_t destination = ( static_cast<std::size_t>( y ) * width + x ) * 4u;

                if( image_type == 1u )
                {
                    const std::uint16_t palette_index = bytes[ source ];
                    const std::size_t palette_offset = static_cast<std::size_t>( palette_index ) - color_map_first;

                    if( palette_index < color_map_first || palette_offset >= palette.size() )
                    {
                        throw std::runtime_error( "Embedded GMO TGA palette index is invalid" );
                    }

                    const auto &color = palette[ palette_offset ];
                    std::copy( color.begin(),
                        color.end(),
                        image.rgba.begin() + static_cast<std::ptrdiff_t>( destination ) );
                }
                else
                {
                    image.rgba[ destination ] = bytes[ source + 2u ];

                    image.rgba[ destination + 1u ] = bytes[ source + 1u ];

                    image.rgba[ destination + 2u ] = bytes[ source ];

                    image.rgba[ destination + 3u ] = pixel_depth == 32u ? bytes[ source + 3u ] : 255u;
                }
            }
        }

        return image;
    }

    Image decode_embedded_image( const filetypes::gmo::Texture &texture, std::string uri )
    {
        const std::span<const std::uint8_t> bytes = embedded_image_file( texture.embedded_data );
        const std::string name = texture.name.empty() ? texture.source_name : texture.name;

        if( bytes.size() >= 2u && bytes[ 0u ] == 'B' && bytes[ 1u ] == 'M' )
        {
            return decode_bmp_image( bytes, name, texture.source_name, std::move( uri ) );
        }

        return decode_tga_image( bytes, name, texture.source_name, std::move( uri ) );
    }

    std::vector<std::string> texture_keys( std::string value )
    {
        const std::size_t slash = value.find_last_of( "/\\" );

        if( slash != std::string::npos )
        {
            value.erase( 0u, slash + 1u );
        }
        while(
            !value.empty() && ( value.back() == '\0' || std::isspace( static_cast<unsigned char>( value.back() ) ) ) )
        {
            value.pop_back();
        }

        value = piko::utils::lowercase_ascii( std::move( value ) );

        std::vector<std::string> keys;

        if( value.empty() )
        {
            return keys;
        }

        keys.push_back( value );
        const std::size_t dot = value.find_last_of( '.' );

        if( dot != std::string::npos && dot != 0u )
        {
            keys.push_back( value.substr( 0u, dot ) );
        }

        return keys;
    }

    using SharedTexture = std::shared_ptr<const std::vector<std::uint8_t>>;
    using SharedTextureMap = std::map<std::string, SharedTexture>;

    void add_shared_texture( SharedTextureMap &textures, const filetypes::gmo::Texture &texture )
    {
        if( texture.embedded_data.empty() )
        {
            return;
        }

        const SharedTexture data = std::make_shared<const std::vector<std::uint8_t>>( texture.embedded_data );

        for( const std::string &name : { texture.source_name, texture.name } )
        {
            for( std::string key : texture_keys( name ) )
            {
                textures.try_emplace( std::move( key ), data );
            }
        }
    }

    SharedTexture find_shared_texture( const SharedTextureMap &textures, const filetypes::gmo::Texture &texture )
    {
        for( const std::string &name : { texture.source_name, texture.name } )
        {
            for( const std::string &key : texture_keys( name ) )
            {
                const auto found = textures.find( key );

                if( found != textures.end() )
                {
                    return found->second;
                }
            }
        }

        return {};
    }

    template <typename T>
    std::int32_t append_accessor( Document &document,
        const std::vector<T> &values,
        std::int32_t component_type_value,
        std::string type,
        std::int32_t target,
        std::vector<double> minimum = {},
        std::vector<double> maximum = {} )
    {
        gltf::align_buffer( document.buffer );
        const std::size_t offset = document.buffer.size();
        const std::size_t byte_length = values.size() * sizeof( T );
        document.buffer.resize( offset + byte_length );

        if( byte_length != 0u )
        {
            std::memcpy( document.buffer.data() + offset, values.data(), byte_length );
        }

        const std::int32_t view_index = static_cast<std::int32_t>( document.buffer_views.size() );
        document.buffer_views.push_back( BufferView{ offset, byte_length, target } );

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
            component_type_value,
            count,
            std::move( type ),
            std::move( minimum ),
            std::move( maximum )
        } );
        return accessor_index;
    }

    std::vector<std::uint32_t> triangulate( const filetypes::gmo::DrawPrimitive &draw )
    {
        std::vector<std::uint32_t> output;

        if( draw.primitive_type == 3u )
        {
            const std::size_t count = draw.indices.size() - draw.indices.size() % 3u;
            output.assign( draw.indices.begin(), draw.indices.begin() + static_cast<std::ptrdiff_t>( count ) );
            return output;
        }

        if( draw.primitive_type == 4u )
        {
            for( std::size_t index = 2u; index < draw.indices.size(); ++index )
            {
                std::uint32_t a = draw.indices[ index - 2u ];
                std::uint32_t b = draw.indices[ index - 1u ];
                const std::uint32_t c = draw.indices[ index ];

                if( ( index & 1u ) != 0u )
                {
                    std::swap( a, b );
                }

                if( a == b || b == c || a == c )
                {
                    continue;
                }

                output.push_back( a );
                output.push_back( b );
                output.push_back( c );
            }

            return output;
        }

        if( draw.primitive_type == 5u && draw.indices.size() >= 3u )
        {
            for( std::size_t index = 2u; index < draw.indices.size(); ++index )
            {
                const std::uint32_t a = draw.indices[ 0u ];
                const std::uint32_t b = draw.indices[ index - 1u ];
                const std::uint32_t c = draw.indices[ index ];

                if( a == b || b == c || a == c )
                {
                    continue;
                }

                output.push_back( a );
                output.push_back( b );
                output.push_back( c );
            }
        }

        return output;
    }

    filetypes::gmo::Vec4 normalize_quaternion( filetypes::gmo::Vec4 value )
    {
        const float length = std::sqrt( value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w );

        if( length <= 0.000001f )
        {
            return filetypes::gmo::Vec4{};
        }

        value.x /= length;

        value.y /= length;

        value.z /= length;

        value.w /= length;
        return value;
    }

    filetypes::gmo::Vec4 quaternion_from_matrix( const std::array<float, 9> &matrix )
    {
        filetypes::gmo::Vec4 output{};
        const float trace = matrix[ 0 ] + matrix[ 4 ] + matrix[ 8 ];

        if( trace > 0.0f )
        {
            const float scale = std::sqrt( trace + 1.0f ) * 2.0f;
            output.w = 0.25f * scale;

            output.x = ( matrix[ 7 ] - matrix[ 5 ] ) / scale;

            output.y = ( matrix[ 2 ] - matrix[ 6 ] ) / scale;

            output.z = ( matrix[ 3 ] - matrix[ 1 ] ) / scale;
        }
        else if( matrix[ 0 ] > matrix[ 4 ] && matrix[ 0 ] > matrix[ 8 ] )
        {
            const float scale = std::sqrt( 1.0f + matrix[ 0 ] - matrix[ 4 ] - matrix[ 8 ] ) * 2.0f;
            output.w = ( matrix[ 7 ] - matrix[ 5 ] ) / scale;

            output.x = 0.25f * scale;

            output.y = ( matrix[ 1 ] + matrix[ 3 ] ) / scale;

            output.z = ( matrix[ 2 ] + matrix[ 6 ] ) / scale;
        }
        else if( matrix[ 4 ] > matrix[ 8 ] )
        {
            const float scale = std::sqrt( 1.0f + matrix[ 4 ] - matrix[ 0 ] - matrix[ 8 ] ) * 2.0f;
            output.w = ( matrix[ 2 ] - matrix[ 6 ] ) / scale;

            output.x = ( matrix[ 1 ] + matrix[ 3 ] ) / scale;

            output.y = 0.25f * scale;

            output.z = ( matrix[ 5 ] + matrix[ 7 ] ) / scale;
        }
        else
        {
            const float scale = std::sqrt( 1.0f + matrix[ 8 ] - matrix[ 0 ] - matrix[ 4 ] ) * 2.0f;
            output.w = ( matrix[ 3 ] - matrix[ 1 ] ) / scale;

            output.x = ( matrix[ 2 ] + matrix[ 6 ] ) / scale;

            output.y = ( matrix[ 5 ] + matrix[ 7 ] ) / scale;

            output.z = 0.25f * scale;
        }

        return normalize_quaternion( output );
    }

    void decompose_row_matrix( const float *source,
        filetypes::gmo::Vec3 &translation,
        filetypes::gmo::Vec4 &rotation,
        filetypes::gmo::Vec3 &scale )
    {
        translation = filetypes::gmo::Vec3{ source[ 12 ], source[ 13 ], source[ 14 ] };

        scale = filetypes::gmo::Vec3{
            std::sqrt( source[ 0 ] * source[ 0 ] + source[ 1 ] * source[ 1 ] + source[ 2 ] * source[ 2 ] ),
            std::sqrt( source[ 4 ] * source[ 4 ] + source[ 5 ] * source[ 5 ] + source[ 6 ] * source[ 6 ] ),
            std::sqrt( source[ 8 ] * source[ 8 ] + source[ 9 ] * source[ 9 ] + source[ 10 ] * source[ 10 ] )
        };

        if( scale.x <= 0.000001f )
        {
            scale.x = 1.0f;
        }

        if( scale.y <= 0.000001f )
        {
            scale.y = 1.0f;
        }

        if( scale.z <= 0.000001f )
        {
            scale.z = 1.0f;
        }

        const std::array<float, 9> column_matrix{
            source[ 0 ] / scale.x,
            source[ 4 ] / scale.y,
            source[ 8 ] / scale.z,
            source[ 1 ] / scale.x,
            source[ 5 ] / scale.y,
            source[ 9 ] / scale.z,
            source[ 2 ] / scale.x,
            source[ 6 ] / scale.y,
            source[ 10 ] / scale.z
        };

        rotation = quaternion_from_matrix( column_matrix );
    }

    std::int32_t append_animation_values( Document &document,
        const std::vector<float> &values,
        std::uint32_t dimensions )
    {
        if( dimensions == 3u )
        {
            return append_accessor( document, values, component_float, "VEC3", 0 );
        }

        if( dimensions == 4u )
        {
            return append_accessor( document, values, component_float, "VEC4", 0 );
        }

        throw std::runtime_error( "Unsupported GMO animation output dimension" );
    }

    void append_channel( Document &document,
        Animation &animation,
        std::int32_t node,
        std::string path,
        const std::vector<float> &times,
        const std::vector<float> &values,
        std::uint32_t dimensions )
    {
        if( times.empty() || values.size() != times.size() * dimensions )
        {
            return;
        }

        const auto [ time_minimum, time_maximum ] = gltf::min_max_scalar( times );
        const std::int32_t input =
            append_accessor( document, times, component_float, "SCALAR", 0, time_minimum, time_maximum );

        const std::int32_t output = append_animation_values( document, values, dimensions );
        const std::int32_t sampler = static_cast<std::int32_t>( animation.samplers.size() );
        animation.samplers.push_back( AnimationSampler{ input, output, "LINEAR" } );
        animation.channels.push_back( AnimationChannel{ sampler, node, std::move( path ) } );
    }

    void append_motion( Document &document, const filetypes::gmo::Motion &motion, std::vector<std::string> &warnings )
    {
        Animation animation{};
        animation.name = motion.name;

        animation.start_frame = motion.start_frame;

        animation.end_frame = motion.end_frame;

        animation.playback_rate = motion.playback_rate;

        animation.has_playback_rate = motion.has_playback_rate;

        for( const filetypes::gmo::MotionChannel &channel : motion.channels )
        {
            if( channel.node_index >= document.nodes.size() || channel.curve_index >= motion.curves.size() )
            {
                continue;
            }

            const filetypes::gmo::Curve &curve = motion.curves[ channel.curve_index ];
            std::vector<float> times = curve.times;

            for( float &time : times )
            {
                time /= frame_rate;
            }

            if( channel.target_attribute == 0x48u && curve.dimensions == 3u )
            {
                append_channel( document,
                    animation,
                    static_cast<std::int32_t>( channel.node_index ),
                    "translation",
                    times,
                    curve.values,
                    3u );
            }
            else if( channel.target_attribute == 0x4Bu && curve.dimensions == 4u )
            {
                std::vector<float> rotations = curve.values;

                for( std::size_t index = 0; index + 3u < rotations.size(); index += 4u )
                {
                    const filetypes::gmo::Vec4 value = normalize_quaternion( filetypes::gmo::Vec4{
                        rotations[ index ],
                        rotations[ index + 1u ],
                        rotations[ index + 2u ],
                        rotations[ index + 3u ]
                    } );

                    rotations[ index ] = value.x;

                    rotations[ index + 1u ] = value.y;

                    rotations[ index + 2u ] = value.z;

                    rotations[ index + 3u ] = value.w;
                }

                append_channel( document,
                    animation,
                    static_cast<std::int32_t>( channel.node_index ),
                    "rotation",
                    times,
                    rotations,
                    4u );
            }
            else if( channel.target_attribute == 0x4Cu && curve.dimensions == 3u )
            {
                append_channel( document,
                    animation,
                    static_cast<std::int32_t>( channel.node_index ),
                    "scale",
                    times,
                    curve.values,
                    3u );
            }
            else if( channel.target_attribute == 0x47u && curve.dimensions == 16u )
            {
                std::vector<float> translations;
                std::vector<float> rotations;
                std::vector<float> scales;
                translations.reserve( times.size() * 3u );
                rotations.reserve( times.size() * 4u );
                scales.reserve( times.size() * 3u );

                for( std::size_t key = 0; key < times.size(); ++key )
                {
                    filetypes::gmo::Vec3 translation{};
                    filetypes::gmo::Vec4 rotation{};
                    filetypes::gmo::Vec3 scale{};
                    decompose_row_matrix( curve.values.data() + key * 16u, translation, rotation, scale );
                    translations.insert( translations.end(), { translation.x, translation.y, translation.z } );
                    rotations.insert( rotations.end(), { rotation.x, rotation.y, rotation.z, rotation.w } );
                    scales.insert( scales.end(), { scale.x, scale.y, scale.z } );
                }

                append_channel( document,
                    animation,
                    static_cast<std::int32_t>( channel.node_index ),
                    "translation",
                    times,
                    translations,
                    3u );

                append_channel( document,
                    animation,
                    static_cast<std::int32_t>( channel.node_index ),
                    "rotation",
                    times,
                    rotations,
                    4u );

                append_channel( document,
                    animation,
                    static_cast<std::int32_t>( channel.node_index ),
                    "scale",
                    times,
                    scales,
                    3u );
            }
            else
            {
                std::ostringstream message;
                message << "Skipped animation curve target " << channel.target_attribute << " with " << curve.dimensions
                        << " values";

                warnings.push_back( message.str() );
            }
        }

        if( !animation.channels.empty() )
        {
            document.animations.push_back( std::move( animation ) );
        }
    }

    std::int32_t append_skin( Document &document,
        const filetypes::gmo::Node &source,
        std::string name,
        std::vector<std::uint16_t> &source_to_skin,
        std::vector<std::string> &warnings )
    {
        source_to_skin.clear();

        if( source.bone_nodes.empty() )
        {
            return -1;
        }

        Skin skin{};
        skin.name = std::move( name );
        std::vector<std::int32_t> node_to_joint( document.nodes.size(), -1 );
        std::vector<float> matrices;
        matrices.reserve( source.bone_nodes.size() * 16u );
        source_to_skin.reserve( source.bone_nodes.size() );

        const bool has_exact_matrices = source.inverse_bind_matrices.size() == source.bone_nodes.size();
        bool differing_duplicate_matrix = false;

        for( std::size_t source_index = 0; source_index < source.bone_nodes.size(); ++source_index )
        {
            const std::uint32_t node = source.bone_nodes[ source_index ];

            if( node >= document.nodes.size() )
            {
                warnings.push_back( "Skin contains an invalid bone node reference" );
                source_to_skin.clear();
                return -1;
            }

            std::int32_t joint_index = node_to_joint[ node ];

            if( joint_index < 0 )
            {
                joint_index = static_cast<std::int32_t>( skin.joints.size() );

                if( joint_index > static_cast<std::int32_t>( std::numeric_limits<std::uint16_t>::max() ) )
                {
                    warnings.push_back( "Skin has too many unique joints for unsigned short joint indices" );
                    source_to_skin.clear();
                    return -1;
                }

                node_to_joint[ node ] = joint_index;
                skin.joints.push_back( static_cast<std::int32_t>( node ) );

                if( has_exact_matrices )
                {
                    const auto &values = source.inverse_bind_matrices[ source_index ].values;
                    matrices.insert( matrices.end(), values.begin(), values.end() );
                }
                else
                {
                    matrices.insert( matrices.end(),
                        { 1.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            1.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            1.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            1.0f } );
                }
            }
            else if( has_exact_matrices && !differing_duplicate_matrix )
            {
                const auto &duplicate = source.inverse_bind_matrices[ source_index ].values;
                const std::size_t matrix_offset = static_cast<std::size_t>( joint_index ) * 16u;

                for( std::size_t component = 0; component < duplicate.size(); ++component )
                {
                    if( std::fabs( duplicate[ component ] - matrices[ matrix_offset + component ] ) > 0.00001f )
                    {
                        differing_duplicate_matrix = true;
                        warnings.push_back(
                            "Duplicate skin joint has a different inverse bind matrix; the first matrix was kept" );
                        break;
                    }
                }
            }

            source_to_skin.push_back( static_cast<std::uint16_t>( joint_index ) );
        }

        if( !has_exact_matrices )
        {
            warnings.push_back( "Skin is missing exact GMO inverse bind matrices; identity matrices were used" );
        }

        skin.inverse_bind_matrices = append_accessor( document, matrices, component_float, "MAT4", 0 );
        const std::int32_t index = static_cast<std::int32_t>( document.skins.size() );
        document.skins.push_back( std::move( skin ) );
        return index;
    }

    float wrap_texture_coordinate( float value )
    {
        value -= std::floor( value );
        return value < 0.0f ? value + 1.0f : value;
    }

    bool image_alpha_at_uv( const Image &image, float u, float v )
    {
        if( image.width == 0u || image.height == 0u ||
            image.rgba.size() < static_cast<std::size_t>( image.width ) * image.height * 4u )
        {
            return false;
        }

        const float wrapped_u = wrap_texture_coordinate( u );
        const float wrapped_v = wrap_texture_coordinate( v );
        const std::uint32_t x =
            std::min( static_cast<std::uint32_t>( wrapped_u * static_cast<float>( image.width ) ), image.width - 1u );

        const std::uint32_t y =
            std::min( static_cast<std::uint32_t>( wrapped_v * static_cast<float>( image.height ) ), image.height - 1u );

        for( std::uint32_t y_offset = 0u; y_offset < 2u; ++y_offset )
        {
            for( std::uint32_t x_offset = 0u; x_offset < 2u; ++x_offset )
            {
                const std::uint32_t sample_x = ( x + x_offset ) % image.width;
                const std::uint32_t sample_y = ( y + y_offset ) % image.height;
                const std::size_t alpha = ( static_cast<std::size_t>( sample_y ) * image.width + sample_x ) * 4u + 3u;

                if( image.rgba[ alpha ] != 255u )
                {
                    return true;
                }
            }
        }

        return false;
    }

    const Image *material_alpha_image( const Document &document, std::int32_t material_index )
    {
        if( material_index < 0 || static_cast<std::size_t>( material_index ) >= document.materials.size() )
        {
            return nullptr;
        }

        const Material &material = document.materials[ static_cast<std::size_t>( material_index ) ];

        if( !material.texture_contains_alpha || material.texture < 0 ||
            static_cast<std::size_t>( material.texture ) >= document.textures.size() )
        {
            return nullptr;
        }

        const std::int32_t image_index = document.textures[ static_cast<std::size_t>( material.texture ) ].source;

        if( image_index < 0 || static_cast<std::size_t>( image_index ) >= document.images.size() )
        {
            return nullptr;
        }

        return &document.images[ static_cast<std::size_t>( image_index ) ];
    }

    bool triangle_samples_texture_alpha( const Image &image,
        const filetypes::gmo::Vertex &a,
        const filetypes::gmo::Vertex &b,
        const filetypes::gmo::Vertex &c )
    {
        if( !a.has_uv || !b.has_uv || !c.has_uv )
        {
            return false;
        }

        const std::array<filetypes::gmo::Vec2, 7> samples{
            a.uv,
            b.uv,
            c.uv,
            filetypes::gmo::Vec2{ ( a.uv.x + b.uv.x ) * 0.5f, ( a.uv.y + b.uv.y ) * 0.5f },
            filetypes::gmo::Vec2{ ( b.uv.x + c.uv.x ) * 0.5f, ( b.uv.y + c.uv.y ) * 0.5f },
            filetypes::gmo::Vec2{ ( c.uv.x + a.uv.x ) * 0.5f, ( c.uv.y + a.uv.y ) * 0.5f },
            filetypes::gmo::Vec2{ ( a.uv.x + b.uv.x + c.uv.x ) / 3.0f, ( a.uv.y + b.uv.y + c.uv.y ) / 3.0f }
        };

        return std::any_of( samples.begin(),
            samples.end(),
            [ &image ]( const filetypes::gmo::Vec2 &uv )
            {
                return image_alpha_at_uv( image, uv.x, uv.y );
            } );
    }

    std::int32_t alpha_material_variant( Document &document, std::int32_t material_index )
    {
        if( material_index < 0 || static_cast<std::size_t>( material_index ) >= document.materials.size() )
        {
            return material_index;
        }

        if( document.materials[ static_cast<std::size_t>( material_index ) ].blend )
        {
            return material_index;
        }

        const std::int32_t existing = document.materials[ static_cast<std::size_t>( material_index ) ].alpha_variant;

        if( existing >= 0 )
        {
            return existing;
        }

        Material variant = document.materials[ static_cast<std::size_t>( material_index ) ];
        variant.name += "_Alpha";

        variant.uses_texture_alpha = true;

        variant.blend = true;

        variant.blend_type = 5u;

        variant.alpha_variant = -1;
        const std::int32_t variant_index = static_cast<std::int32_t>( document.materials.size() );
        document.materials.push_back( std::move( variant ) );
        document.materials[ static_cast<std::size_t>( material_index ) ].alpha_variant = variant_index;
        return variant_index;
    }

    struct ConvertedVertex
    {
        std::array<float, 3> position{};
        std::array<float, 3> normal{
            0.0f,
            1.0f,
            0.0f
        };

        std::array<float, 2> uv{};
        std::array<float, 4> color{
            1.0f,
            1.0f,
            1.0f,
            1.0f
        };

        std::array<std::uint16_t, 8> joints{};
        std::array<float, 8> weights{};
        bool has_normal = false;
        bool has_uv = false;
        bool has_color = false;
        bool has_skin = false;
        bool has_second_skin_set = false;
    };

    struct ConvertedVertexKey
    {
        std::array<std::uint32_t, 20> values{};
        std::array<std::uint16_t, 8> joints{};
        std::uint8_t flags = 0;

        auto operator<=>( const ConvertedVertexKey & ) const = default;
    };

    struct ConvertedVertexKeyHash
    {
        std::size_t operator()( const ConvertedVertexKey &key ) const noexcept
        {
            std::size_t hash = 2166136261u;

            const auto mix = [ &hash ]( std::uint32_t value )
            {
                hash ^= static_cast<std::size_t>( value );

                hash *= 16777619u;
            };

            for( const std::uint32_t value : key.values )
            {
                mix( value );
            }

            for( const std::uint16_t joint : key.joints )
            {
                mix( joint );
            }

            mix( key.flags );
            return hash;
        }
    };

    ConvertedVertex convert_vertex( const filetypes::gmo::Vertex &source,
        const filetypes::gmo::Mesh &source_mesh,
        std::span<const std::uint16_t> source_to_skin,
        bool &invalid_bone_palette )
    {
        ConvertedVertex output{};
        output.position = { source.position.x, source.position.y, source.position.z };

        output.has_normal = source.has_normal;

        output.has_uv = source.has_uv;

        output.has_color = source.has_color;

        if( source.has_normal )
        {
            output.normal = { source.normal.x, source.normal.y, source.normal.z };
        }

        if( source.has_uv )
        {
            output.uv = { source.uv.x, source.uv.y };
        }

        if( source.has_color )
        {
            output.color = { source.color.x, source.color.y, source.color.z, source.color.w };
        }

        output.has_skin = source.weight_count != 0u && !source_to_skin.empty();

        output.has_second_skin_set = output.has_skin && source.weight_count > 4u;

        if( output.has_skin )
        {
            for( std::uint32_t slot = 0; slot < source.weight_count && slot < output.joints.size(); ++slot )
            {
                output.weights[ slot ] = source.weights[ slot ];

                if( slot >= source_mesh.bone_palette.size() )
                {
                    invalid_bone_palette = true;
                    continue;
                }

                const std::uint32_t source_joint = source_mesh.bone_palette[ slot ];

                if( source_joint >= source_to_skin.size() )
                {
                    invalid_bone_palette = true;
                    continue;
                }

                output.joints[ slot ] = source_to_skin[ source_joint ];
            }
        }

        return output;
    }

    ConvertedVertexKey converted_vertex_key( const ConvertedVertex &vertex )
    {
        ConvertedVertexKey key{};
        std::size_t value = 0u;
        auto append = [ & ]( float component )
        {
            key.values[ value++ ] = std::bit_cast<std::uint32_t>( component );
        };

        for( const float component : vertex.position )
        {
            append( component );
        }

        for( const float component : vertex.normal )
        {
            append( component );
        }

        for( const float component : vertex.uv )
        {
            append( component );
        }

        for( const float component : vertex.color )
        {
            append( component );
        }

        for( const float component : vertex.weights )
        {
            append( component );
        }

        key.joints = vertex.joints;

        key.flags = static_cast<std::uint8_t>( ( vertex.has_normal ? 1u : 0u ) | ( vertex.has_uv ? 2u : 0u ) |
                                               ( vertex.has_color ? 4u : 0u ) | ( vertex.has_skin ? 8u : 0u ) |
                                               ( vertex.has_second_skin_set ? 16u : 0u ) );

        return key;
    }

    bool same_position( const filetypes::gmo::Vertex &left, const filetypes::gmo::Vertex &right )
    {
        return std::bit_cast<std::uint32_t>( left.position.x ) == std::bit_cast<std::uint32_t>( right.position.x ) &&
               std::bit_cast<std::uint32_t>( left.position.y ) == std::bit_cast<std::uint32_t>( right.position.y ) &&
               std::bit_cast<std::uint32_t>( left.position.z ) == std::bit_cast<std::uint32_t>( right.position.z );
    }

    std::vector<Primitive> append_primitive( Document &document,
        const filetypes::gmo::VertexArray &array,
        const filetypes::gmo::Mesh &source_mesh,
        const filetypes::gmo::DrawPrimitive &draw,
        std::span<const std::uint16_t> source_to_skin,
        std::vector<std::string> &warnings )
    {
        Primitive output{};
        output.material = source_mesh.material_index < document.materials.size()
                              ? static_cast<std::int32_t>( source_mesh.material_index )
                              : 0;

        output.source_primitive = draw.primitive_type;

        output.source_flags = draw.flags;

        std::vector<std::uint32_t> triangles = triangulate( draw );

        if( triangles.empty() )
        {
            std::ostringstream message;
            message << "Skipped unsupported or empty primitive type " << draw.primitive_type;
            warnings.push_back( message.str() );
            return {};
        }

        std::vector<std::uint32_t> valid_triangles;
        valid_triangles.reserve( triangles.size() );

        for( std::size_t triangle = 0u; triangle + 2u < triangles.size(); triangle += 3u )
        {
            const std::uint32_t a = triangles[ triangle ];
            const std::uint32_t b = triangles[ triangle + 1u ];
            const std::uint32_t c = triangles[ triangle + 2u ];

            if( a >= array.vertices.size() || b >= array.vertices.size() || c >= array.vertices.size() )
            {
                continue;
            }

            if( same_position( array.vertices[ a ], array.vertices[ b ] ) ||
                same_position( array.vertices[ b ], array.vertices[ c ] ) ||
                same_position( array.vertices[ c ], array.vertices[ a ] ) )
            {
                continue;
            }

            valid_triangles.insert( valid_triangles.end(), { a, b, c } );
        }

        if( valid_triangles.empty() )
        {
            return {};
        }

        const Image *alpha_image = material_alpha_image( document, output.material );
        const bool split_alpha = alpha_image != nullptr && output.material >= 0 &&
                                 static_cast<std::size_t>( output.material ) < document.materials.size() &&
                                 !document.materials[ static_cast<std::size_t>( output.material ) ].blend;

        std::vector<std::uint32_t> opaque_source_indices;
        std::vector<std::uint32_t> alpha_source_indices;

        if( split_alpha )
        {
            opaque_source_indices.reserve( valid_triangles.size() );
            alpha_source_indices.reserve( valid_triangles.size() );

            for( std::size_t triangle = 0u; triangle + 2u < valid_triangles.size(); triangle += 3u )
            {
                const std::uint32_t a = valid_triangles[ triangle ];
                const std::uint32_t b = valid_triangles[ triangle + 1u ];
                const std::uint32_t c = valid_triangles[ triangle + 2u ];
                std::vector<std::uint32_t> &target = triangle_samples_texture_alpha( *alpha_image,
                                                         array.vertices[ a ],
                                                         array.vertices[ b ],
                                                         array.vertices[ c ] )
                                                         ? alpha_source_indices
                                                         : opaque_source_indices;

                target.insert( target.end(), { a, b, c } );
            }
        }
        else
        {
            opaque_source_indices = std::move( valid_triangles );
        }

        std::vector<float> positions;
        std::vector<float> normals;
        std::vector<float> texcoords;
        std::vector<float> colors;
        std::vector<std::uint16_t> joints0;
        std::vector<float> weights0;
        std::vector<std::uint16_t> joints1;
        std::vector<float> weights1;
        std::vector<std::uint32_t> source_to_compact( array.vertices.size(),
            std::numeric_limits<std::uint32_t>::max() );

        std::unordered_map<ConvertedVertexKey, std::uint32_t, ConvertedVertexKeyHash> unique_vertices;
        unique_vertices.reserve( valid_triangles.size() );

        bool has_normals = false;
        bool has_texcoords = false;
        bool has_colors = false;
        bool has_skin = false;
        bool has_second_skin_set = false;
        bool invalid_bone_palette = false;

        auto compact_index = [ & ]( std::uint32_t source_index ) -> std::uint32_t
        {
            std::uint32_t &cached = source_to_compact[ source_index ];

            if( cached != std::numeric_limits<std::uint32_t>::max() )
            {
                return cached;
            }

            const ConvertedVertex vertex =
                convert_vertex( array.vertices[ source_index ], source_mesh, source_to_skin, invalid_bone_palette );

            const ConvertedVertexKey key = converted_vertex_key( vertex );

            const auto existing = unique_vertices.find( key );

            if( existing != unique_vertices.end() )
            {
                cached = existing->second;
                return cached;
            }

            cached = static_cast<std::uint32_t>( unique_vertices.size() );
            unique_vertices.emplace( key, cached );
            positions.insert( positions.end(), vertex.position.begin(), vertex.position.end() );
            normals.insert( normals.end(), vertex.normal.begin(), vertex.normal.end() );
            texcoords.insert( texcoords.end(), vertex.uv.begin(), vertex.uv.end() );
            colors.insert( colors.end(), vertex.color.begin(), vertex.color.end() );
            joints0.insert( joints0.end(), vertex.joints.begin(), vertex.joints.begin() + 4 );
            weights0.insert( weights0.end(), vertex.weights.begin(), vertex.weights.begin() + 4 );
            joints1.insert( joints1.end(), vertex.joints.begin() + 4, vertex.joints.end() );
            weights1.insert( weights1.end(), vertex.weights.begin() + 4, vertex.weights.end() );
            has_normals = has_normals || vertex.has_normal;

            has_texcoords = has_texcoords || vertex.has_uv;

            has_colors = has_colors || vertex.has_color;

            has_skin = has_skin || vertex.has_skin;

            has_second_skin_set = has_second_skin_set || vertex.has_second_skin_set;
            return cached;
        };

        auto remap_triangles = [ & ]( const std::vector<std::uint32_t> &source_indices )
        {
            std::vector<std::uint32_t> remapped;
            remapped.reserve( source_indices.size() );

            for( std::size_t triangle = 0u; triangle + 2u < source_indices.size(); triangle += 3u )
            {
                const std::uint32_t a = compact_index( source_indices[ triangle ] );
                const std::uint32_t b = compact_index( source_indices[ triangle + 1u ] );
                const std::uint32_t c = compact_index( source_indices[ triangle + 2u ] );

                if( a == b || b == c || c == a )
                {
                    continue;
                }

                remapped.insert( remapped.end(), { a, b, c } );
            }

            return remapped;
        };

        std::vector<std::uint32_t> opaque_triangles = remap_triangles( opaque_source_indices );
        std::vector<std::uint32_t> alpha_triangles = remap_triangles( alpha_source_indices );

        if( opaque_triangles.empty() && alpha_triangles.empty() )
        {
            return {};
        }

        if( invalid_bone_palette )
        {
            warnings.push_back( "Mesh bone palette contains an invalid skin joint reference; joint zero was used" );
        }

        const auto [ position_minimum, position_maximum ] = gltf::min_max_vec3( positions );
        output.position = append_accessor( document,
            positions,
            component_float,
            "VEC3",
            target_array_buffer,
            position_minimum,
            position_maximum );

        if( has_normals )
        {
            output.normal = append_accessor( document, normals, component_float, "VEC3", target_array_buffer );
        }

        if( has_texcoords )
        {
            output.texcoord = append_accessor( document, texcoords, component_float, "VEC2", target_array_buffer );
        }

        if( has_colors )
        {
            output.color = append_accessor( document, colors, component_float, "VEC4", target_array_buffer );
        }

        if( has_skin )
        {
            output.joints0 =
                append_accessor( document, joints0, component_unsigned_short, "VEC4", target_array_buffer );

            output.weights0 = append_accessor( document, weights0, component_float, "VEC4", target_array_buffer );

            if( has_second_skin_set )
            {
                output.joints1 =
                    append_accessor( document, joints1, component_unsigned_short, "VEC4", target_array_buffer );

                output.weights1 = append_accessor( document, weights1, component_float, "VEC4", target_array_buffer );
            }

            output.skinned = true;
        }

        std::vector<Primitive> outputs;

        if( !opaque_triangles.empty() )
        {
            Primitive opaque = output;
            opaque.indices = append_accessor( document,
                opaque_triangles,
                component_unsigned_int,
                "SCALAR",
                target_element_array_buffer );

            outputs.push_back( std::move( opaque ) );
        }

        if( !alpha_triangles.empty() )
        {
            Primitive alpha = output;
            alpha.material = alpha_material_variant( document, output.material );

            alpha.indices = append_accessor( document,
                alpha_triangles,
                component_unsigned_int,
                "SCALAR",
                target_element_array_buffer );

            outputs.push_back( std::move( alpha ) );
        }

        return outputs;
    }

    Document build_document( const filetypes::gmo::Model &model,
        std::vector<std::string> &warnings,
        std::size_t &primitive_count )
    {
        Document document{};
        std::vector<std::int32_t> texture_indices( model.textures.size(), -1 );
        std::set<std::string> used_image_names;

        for( std::size_t index = 0; index < model.textures.size(); ++index )
        {
            const filetypes::gmo::Texture &source = model.textures[ index ];

            if( source.embedded_data.empty() )
            {
                continue;
            }

            std::string stem =
                piko::utils::safe_ascii_stem( source.source_name.empty() ? source.name : source.source_name,
                    "texture_" + std::to_string( index ) );

            std::string file_name = stem + ".png";

            for( std::size_t suffix = 1u; used_image_names.contains( file_name ); ++suffix )
            {
                file_name = stem + "_" + std::to_string( suffix ) + ".png";
            }

            used_image_names.insert( file_name );

            try
            {
                Image image = decode_embedded_image( source, "textures/" + file_name );
                const std::int32_t image_index = static_cast<std::int32_t>( document.images.size() );
                document.images.push_back( std::move( image ) );
                Texture texture{};
                texture.name = source.name.empty() ? source.source_name : source.name;

                texture.source = image_index;

                texture_indices[ index ] = static_cast<std::int32_t>( document.textures.size() );
                document.textures.push_back( std::move( texture ) );
            }
            catch( const std::exception &error )
            {
                warnings.push_back( "Could not decode texture " + source.source_name + ": " + error.what() );
            }
        }

        if( model.materials.empty() )
        {
            Material material{};
            material.name = "Default";
            document.materials.push_back( std::move( material ) );
        }

        std::set<std::string> unresolved_textures;

        for( const filetypes::gmo::Material &source : model.materials )
        {
            Material material{};
            material.name = source.name;

            material.diffuse = { source.diffuse.x, source.diffuse.y, source.diffuse.z, source.diffuse.w };
            const bool material_enables_texture_alpha = std::any_of( source.states.begin(),
                source.states.end(),
                []( const auto &state )
                {
                    return state[ 0 ] == 6u && state[ 1 ] != 0u;
                } );
            if( source.texture_index < model.textures.size() )
            {
                const filetypes::gmo::Texture &texture = model.textures[ source.texture_index ];
                material.texture_name = texture.source_name.empty() ? texture.name : texture.source_name;

                material.texture = texture_indices[ source.texture_index ];

                if( material.texture < 0 && !material.texture_name.empty() &&
                    unresolved_textures.insert( material.texture_name ).second )
                {
                    warnings.push_back( "Texture reference has no embedded image: " + material.texture_name );
                }

                if( material.texture >= 0 && static_cast<std::size_t>( material.texture ) < document.textures.size() )
                {
                    const std::int32_t image_index =
                        document.textures[ static_cast<std::size_t>( material.texture ) ].source;

                    if( image_index >= 0 && static_cast<std::size_t>( image_index ) < document.images.size() )
                    {
                        const std::vector<std::uint8_t> &rgba =
                            document.images[ static_cast<std::size_t>( image_index ) ].rgba;

                        const std::size_t pixel_count = rgba.size() / 4u;
                        std::size_t transparent_pixels = 0u;
                        std::size_t non_opaque_pixels = 0u;

                        for( std::size_t alpha = 3u; alpha < rgba.size(); alpha += 4u )
                        {
                            if( rgba[ alpha ] == 0u )
                            {
                                ++transparent_pixels;
                            }

                            if( rgba[ alpha ] != 255u )
                            {
                                ++non_opaque_pixels;
                            }
                        }

                        const bool mostly_opaque =
                            pixel_count > 0u && ( pixel_count - non_opaque_pixels ) * 10u >= pixel_count * 9u;

                        const bool sparse_alpha = pixel_count > 0u && non_opaque_pixels * 100u < pixel_count;
                        material.texture_contains_alpha =
                            non_opaque_pixels > 0u && ( material_enables_texture_alpha || transparent_pixels > 0u ||
                                                          !mostly_opaque || sparse_alpha );
                    }
                }
            }

            material.shading_type = std::any_of( source.states.begin(),
                                        source.states.end(),
                                        []( const auto &state )
                                        {
                                            return state[ 0 ] == 0u && state[ 1 ] == 0u;
                                        } )
                                        ? 3u
                                        : 2u;
            material.has_gmo_blend_function = source.has_blend_function;

            material.gmo_blend_operation = source.blend_operation;

            material.gmo_blend_source = source.blend_source;

            material.gmo_blend_destination = source.blend_destination;

            if( source.has_blend_function )
            {
                material.blend = true;

                material.uses_texture_alpha = material.texture >= 0;

                if( source.blend_operation == 0u && source.blend_source == 6u && source.blend_destination == 1u )
                {
                    material.blend_type = 1u;
                }
                else if( source.blend_operation == 0u && source.blend_source == 6u && source.blend_destination == 7u )
                {
                    material.blend_type = 5u;
                }
                else
                {
                    material.blend_type = 5u;
                    std::ostringstream message;
                    message << "Unsupported GMO blend function on material " << source.name << ": "
                            << source.blend_operation << ',' << source.blend_source << ',' << source.blend_destination;

                    warnings.push_back( message.str() );
                }
            }
            else if( material.diffuse[ 3 ] < 0.999 )
            {
                material.blend = true;

                material.blend_type = 5u;
            }

            material.states = source.states;
            document.materials.push_back( std::move( material ) );
        }

        document.nodes.resize( model.nodes.size() );

        for( std::size_t index = 0; index < model.nodes.size(); ++index )
        {
            const filetypes::gmo::Node &source = model.nodes[ index ];
            Node &node = document.nodes[ index ];
            node.name = source.name;

            node.translation = { source.translation.x, source.translation.y, source.translation.z };
            const filetypes::gmo::Vec4 rotation = normalize_quaternion( source.rotation );
            node.rotation = { rotation.x, rotation.y, rotation.z, rotation.w };

            node.scale = { source.scale.x, source.scale.y, source.scale.z };

            if( source.parent_index < document.nodes.size() )
            {
                document.nodes[ source.parent_index ].children.push_back( static_cast<std::int32_t>( index ) );
            }
            else
            {
                document.scene_nodes.push_back( static_cast<std::int32_t>( index ) );
            }
        }

        for( std::size_t node_index = 0; node_index < model.nodes.size(); ++node_index )
        {
            const filetypes::gmo::Node &source_node = model.nodes[ node_index ];
            std::vector<std::uint32_t> part_indices = source_node.part_indices;

            if( part_indices.empty() && source_node.part_index != filetypes::gmo::invalid_index )
            {
                part_indices.push_back( source_node.part_index );
            }

            part_indices.erase( std::remove_if( part_indices.begin(),
                                    part_indices.end(),
                                    [ &model ]( std::uint32_t part_index )
                                    {
                                        return part_index >= model.parts.size();
                                    } ),
                part_indices.end() );
            if( part_indices.empty() )
            {
                continue;
            }

            Mesh mesh{};
            mesh.name =
                part_indices.size() == 1u ? model.parts[ part_indices.front() ].name : source_node.name + "_Parts";

            bool needs_skin = false;
            bool has_weighted_vertices = false;

            for( const std::uint32_t part_index : part_indices )
            {
                const filetypes::gmo::Part &part = model.parts[ part_index ];

                for( const filetypes::gmo::Mesh &source_mesh : part.meshes )
                {
                    for( const filetypes::gmo::DrawPrimitive &draw : source_mesh.draws )
                    {
                        if( draw.array_index >= part.arrays.size() )
                        {
                            continue;
                        }

                        const filetypes::gmo::VertexArray &array = part.arrays[ draw.array_index ];

                        if( std::any_of( array.vertices.begin(),
                                array.vertices.end(),
                                []( const filetypes::gmo::Vertex &vertex )
                                {
                                    return vertex.weight_count != 0u;
                                } ) )
                        {
                            has_weighted_vertices = true;
                            break;
                        }
                    }

                    if( has_weighted_vertices )
                    {
                        break;
                    }
                }

                if( has_weighted_vertices )
                {
                    break;
                }
            }

            std::vector<std::uint16_t> source_to_skin;
            const std::int32_t skin_index =
                has_weighted_vertices
                    ? append_skin( document, source_node, source_node.name + "_Skin", source_to_skin, warnings )
                    : -1;

            for( const std::uint32_t part_index : part_indices )
            {
                const filetypes::gmo::Part &part = model.parts[ part_index ];

                for( const filetypes::gmo::Mesh &source_mesh : part.meshes )
                {
                    for( const filetypes::gmo::DrawPrimitive &draw : source_mesh.draws )
                    {
                        if( draw.array_index >= part.arrays.size() )
                        {
                            continue;
                        }

                        const filetypes::gmo::VertexArray &array = part.arrays[ draw.array_index ];
                        std::vector<Primitive> primitives =
                            append_primitive( document, array, source_mesh, draw, source_to_skin, warnings );

                        for( Primitive &primitive : primitives )
                        {
                            if( primitive.indices < 0 )
                            {
                                continue;
                            }

                            needs_skin = needs_skin || primitive.skinned;
                            mesh.primitives.push_back( std::move( primitive ) );
                            ++primitive_count;
                        }
                    }
                }
            }

            if( mesh.primitives.empty() )
            {
                continue;
            }

            document.nodes[ node_index ].mesh = static_cast<std::int32_t>( document.meshes.size() );
            document.meshes.push_back( std::move( mesh ) );

            if( needs_skin && skin_index >= 0 )
            {
                document.nodes[ node_index ].skin = skin_index;
            }
        }

        for( const filetypes::gmo::Motion &motion : model.motions )
        {
            append_motion( document, motion, warnings );
        }

        return document;
    }

    void write_document( const Document &document,
        const filetypes::gmo::Model &model,
        const std::filesystem::path &output_directory )
    {
        std::filesystem::create_directories( output_directory );
        {
            std::ofstream buffer_file( output_directory / "model.bin", std::ios::binary );

            if( !buffer_file )
            {
                throw std::runtime_error( "Could not create GMO glTF buffer" );
            }

            if( !document.buffer.empty() )
            {
                buffer_file.write( reinterpret_cast<const char *>( document.buffer.data() ),
                    static_cast<std::streamsize>( document.buffer.size() ) );
            }
        }

        std::ofstream output( output_directory / "model.gltf", std::ios::binary );

        if( !output )
        {
            throw std::runtime_error( "Could not create GMO glTF file" );
        }

        output << "{\n";
        output << "  \"asset\":{\"version\":\"2.0\",\"generator\":\"Piko GMO converter\",\"extras\":{\"piko\":{"
               << "\"format\":\"piko-gmo-model\",\"source_model\":\""
               << json_util::escape_string( model.name, json_util::EscapeMode::Ascii )
               << "\",\"frame_rate\":" << frame_rate << ",\"animation_mode\":\"indexed-clips\"}}},\n";

        output << "  \"scene\":0,\n";
        output << "  \"scenes\":[{\"name\":\"" << json_util::escape_string( model.name, json_util::EscapeMode::Ascii )
               << "\",\"nodes\":";

        gltf::write_int_array( output, document.scene_nodes );
        output << "}],\n";

        output << "  \"nodes\":[\n";

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
            output << '}' << ( index + 1u == document.nodes.size() ? "\n" : ",\n" );
        }

        output << "  ],\n";

        output << "  \"meshes\":[\n";

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

                if( primitive.joints0 >= 0 )
                {
                    output << ",\"JOINTS_0\":" << primitive.joints0;
                }

                if( primitive.weights0 >= 0 )
                {
                    output << ",\"WEIGHTS_0\":" << primitive.weights0;
                }

                if( primitive.joints1 >= 0 )
                {
                    output << ",\"JOINTS_1\":" << primitive.joints1;
                }

                if( primitive.weights1 >= 0 )
                {
                    output << ",\"WEIGHTS_1\":" << primitive.weights1;
                }

                output << "},\"indices\":" << primitive.indices << ",\"material\":" << primitive.material
                       << ",\"mode\":" << mode_triangles
                       << ",\"extras\":{\"piko\":{\"gmo_primitive\":" << primitive.source_primitive
                       << ",\"gmo_flags\":" << primitive.source_flags
                       << ",\"skinned\":" << ( primitive.skinned ? "true" : "false" ) << "}}}";

                output << ( primitive_index + 1u == mesh.primitives.size() ? "\n" : ",\n" );
            }

            output << "    ]}" << ( mesh_index + 1u == document.meshes.size() ? "\n" : ",\n" );
        }

        output << "  ],\n";

        output << "  \"materials\":[\n";

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

            const bool transparent = material.blend_type != 0u || material.blend || material.diffuse[ 3 ] < 0.999;
            output << "},\"alphaMode\":\"" << ( transparent ? "BLEND" : "OPAQUE" ) << "\",\"doubleSided\":true";
            output << ",\"extras\":{\"piko\":{\"source_texture\":\""
                   << json_util::escape_string( material.texture_name, json_util::EscapeMode::Ascii )
                   << "\",\"blend_type\":" << material.blend_type << ",\"shading_type\":" << material.shading_type
                   << ",\"flags\":2"
                   << ",\"uses_texture_alpha\":" << ( material.uses_texture_alpha ? "true" : "false" );

            if( material.has_gmo_blend_function )
            {
                output << ",\"gmo_blend_function\":[" << material.gmo_blend_operation << ','
                       << material.gmo_blend_source << ',' << material.gmo_blend_destination << ']';
            }

            output << ",\"states\":[";

            for( std::size_t state_index = 0; state_index < material.states.size(); ++state_index )
            {
                output << '[' << material.states[ state_index ][ 0 ] << ',' << material.states[ state_index ][ 1 ]
                       << ']';

                if( state_index + 1u != material.states.size() )
                {
                    output << ',';
                }
            }

            output << "]}}}" << ( index + 1u == document.materials.size() ? "\n" : ",\n" );
        }

        output << "  ],\n";

        if( !document.images.empty() )
        {
            output << "  \"samplers\":[{\"magFilter\":9729,\"minFilter\":9729,\"wrapS\":10497,\"wrapT\":10497}],\n";
            output << "  \"images\":[\n";

            for( std::size_t index = 0; index < document.images.size(); ++index )
            {
                const Image &image = document.images[ index ];
                output << "    {\"name\":\"" << json_util::escape_string( image.name, json_util::EscapeMode::Ascii )
                       << "\",\"uri\":\"" << json_util::escape_string( image.uri, json_util::EscapeMode::Ascii )
                       << "\"}";

                output << ( index + 1u == document.images.size() ? "\n" : ",\n" );
            }

            output << "  ],\n";
            output << "  \"textures\":[\n";

            for( std::size_t index = 0; index < document.textures.size(); ++index )
            {
                const Texture &texture = document.textures[ index ];
                output << "    {\"name\":\"" << json_util::escape_string( texture.name, json_util::EscapeMode::Ascii )
                       << "\",\"sampler\":0,\"source\":" << texture.source << '}';

                output << ( index + 1u == document.textures.size() ? "\n" : ",\n" );
            }

            output << "  ],\n";
        }

        output << "  \"skins\":[\n";

        for( std::size_t index = 0; index < document.skins.size(); ++index )
        {
            const Skin &skin = document.skins[ index ];
            output << "    {\"name\":\"" << json_util::escape_string( skin.name, json_util::EscapeMode::Ascii )
                   << "\",\"inverseBindMatrices\":" << skin.inverse_bind_matrices << ",\"joints\":";

            gltf::write_int_array( output, skin.joints );
            output << '}' << ( index + 1u == document.skins.size() ? "\n" : ",\n" );
        }

        output << "  ],\n";

        output << "  \"animations\":[\n";

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

            output << "    ],\"extras\":{\"piko\":{\"start_frame\":" << animation.start_frame
                   << ",\"end_frame\":" << animation.end_frame;

            if( animation.has_playback_rate && animation.playback_rate > 0.0f )
            {
                output << ",\"timing_mode\":\"fixed-rate\""
                       << ",\"playback_rate\":" << animation.playback_rate;
            }
            else
            {
                output << ",\"timing_mode\":\"model-speed\"";
            }

            output << "}}}";
            output << ( animation_index + 1u == document.animations.size() ? "\n" : ",\n" );
        }

        output << "  ],\n";

        output << "  \"bufferViews\":[\n";

        for( std::size_t index = 0; index < document.buffer_views.size(); ++index )
        {
            const BufferView &view = document.buffer_views[ index ];
            output << "    {\"buffer\":0,\"byteOffset\":" << view.byte_offset << ",\"byteLength\":" << view.byte_length;

            if( view.target != 0 )
            {
                output << ",\"target\":" << view.target;
            }

            output << '}' << ( index + 1u == document.buffer_views.size() ? "\n" : ",\n" );
        }

        output << "  ],\n";

        output << "  \"accessors\":[\n";

        for( std::size_t index = 0; index < document.accessors.size(); ++index )
        {
            const Accessor &accessor = document.accessors[ index ];
            output << "    {\"bufferView\":" << accessor.buffer_view
                   << ",\"byteOffset\":0,\"componentType\":" << accessor.component_type
                   << ",\"count\":" << accessor.count << ",\"type\":\"" << accessor.type << "\"";

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

            output << '}' << ( index + 1u == document.accessors.size() ? "\n" : ",\n" );
        }

        output << "  ],\n";
        output << "  \"buffers\":[{\"uri\":\"model.bin\",\"byteLength\":" << document.buffer.size() << "}]\n";
        output << "}\n";

        if( !output )
        {
            throw std::runtime_error( "Could not write GMO glTF file" );
        }
    }

    void write_images( const Document &document,
        const filetypes::gmo::Model &model,
        const std::filesystem::path &output_directory )
    {
        if( model.textures.empty() )
        {
            return;
        }

        const std::filesystem::path texture_directory = output_directory / "textures";
        std::filesystem::create_directories( texture_directory );

        for( const Image &image : document.images )
        {
            write_rgba_png( output_directory / image.uri, image.width, image.height, image.rgba );
        }

        std::ofstream manifest( texture_directory / "textures.json", std::ios::binary );
        manifest << "{\n  \"format\":\"piko-gmo-textures\",\n  \"version\":2,\n  \"images\":[\n";

        for( std::size_t index = 0; index < document.images.size(); ++index )
        {
            const Image &image = document.images[ index ];
            manifest << "    {\"index\":" << index << ",\"name\":\""
                     << json_util::escape_string( image.name, json_util::EscapeMode::Ascii ) << "\",\"source_name\":\""
                     << json_util::escape_string( image.source_name, json_util::EscapeMode::Ascii ) << "\",\"file\":\""
                     << json_util::escape_string( std::filesystem::path( image.uri ).filename().generic_string(),
                            json_util::EscapeMode::Ascii )
                     << "\",\"width\":" << image.width << ",\"height\":" << image.height << '}';

            manifest << ( index + 1u == document.images.size() ? "\n" : ",\n" );
        }

        manifest << "  ],\n  \"references\":[\n";

        for( std::size_t index = 0; index < model.textures.size(); ++index )
        {
            const filetypes::gmo::Texture &texture = model.textures[ index ];
            manifest << "    {\"index\":" << index << ",\"name\":\""
                     << json_util::escape_string( texture.name, json_util::EscapeMode::Ascii )
                     << "\",\"source_name\":\""
                     << json_util::escape_string( texture.source_name, json_util::EscapeMode::Ascii )
                     << "\",\"embedded\":" << ( texture.embedded_data.empty() ? "false" : "true" ) << '}';

            manifest << ( index + 1u == model.textures.size() ? "\n" : ",\n" );
        }

        manifest << "  ]\n}\n";
    }

    GmoConversionResult convert_model_to_gltf( const filetypes::gmo::Model &model,
        const std::filesystem::path &output_directory )
    {
        GmoConversionResult result{};
        result.model_name = model.name;

        result.node_count = model.nodes.size();
        const Document document = build_document( model, result.warnings, result.primitive_count );
        result.animation_count = document.animations.size();

        result.gltf_path = output_directory / "model.gltf";
        write_document( document, model, output_directory );
        write_images( document, model, output_directory );
        return result;
    }
}

namespace piko::convert
{
    GmoConversionResult convert_gmo_to_gltf( std::span<const std::uint8_t> bytes,
        const std::filesystem::path &output_directory )
    {
        const filetypes::gmo::Model model = filetypes::gmo::decode( bytes );
        return gmo_gltf::convert_model_to_gltf( model, output_directory );
    }

    std::vector<GmoConversionResult> convert_gmo_pak_to_gltf( std::span<const std::uint8_t> bytes,
        const std::filesystem::path &output_directory,
        std::span<const std::uint32_t> selected_entries,
        std::span<const std::string> entry_aliases )
    {
        const std::vector<filetypes::gmo::PakEntry> entries = filetypes::gmo::decode_pak_entries( bytes, false );
        gmo_gltf::SharedTextureMap shared_textures;

        for( const filetypes::gmo::PakEntry &entry : entries )
        {
            const filetypes::gmo::Model model = filetypes::gmo::decode( bytes.subspan( entry.offset, entry.size ) );

            for( const filetypes::gmo::Texture &texture : model.textures )
            {
                gmo_gltf::add_shared_texture( shared_textures, texture );
            }
        }

        std::set<std::uint32_t> selected( selected_entries.begin(), selected_entries.end() );
        std::vector<GmoConversionResult> output;

        for( const filetypes::gmo::PakEntry &entry : entries )
        {
            if( !selected.empty() && !selected.contains( entry.index ) )
            {
                continue;
            }

            filetypes::gmo::Model model = filetypes::gmo::decode( bytes.subspan( entry.offset, entry.size ) );

            for( filetypes::gmo::Texture &texture : model.textures )
            {
                if( !texture.embedded_data.empty() )
                {
                    continue;
                }

                const gmo_gltf::SharedTexture found = gmo_gltf::find_shared_texture( shared_textures, texture );

                if( found )
                {
                    texture.embedded_data = *found;
                }
            }

            const std::string &directory_source =
                entry.index < entry_aliases.size() && !entry_aliases[ entry.index ].empty()
                    ? entry_aliases[ entry.index ]
                    : model.name;

            std::string directory_name =
                std::to_string( entry.index ) + "_" + piko::utils::safe_ascii_stem( directory_source, "model" );

            GmoConversionResult result = gmo_gltf::convert_model_to_gltf( model, output_directory / directory_name );
            output.push_back( std::move( result ) );
        }

        return output;
    }
}
