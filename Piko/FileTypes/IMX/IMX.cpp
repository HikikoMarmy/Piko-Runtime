#include "IMX.hpp"
#include "Utils/BinaryUtil.hpp"

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace piko::filetypes::imx
{
    namespace parser
    {
        constexpr std::uint32_t imx_magic = 0x00584D49u;

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

            std::uint32_t read_u32( const char *field_name )
            {
                if( remaining() < 4 )
                {
                    throw DecodeError( std::string( "Unexpected end of IMX while reading " ) + field_name );
                }

                const std::uint8_t *p = bytes_.data() + offset_;
                offset_ += 4;

                return piko::utils::read_le_u32( p );
            }

            std::span<const std::uint8_t> read_bytes( std::size_t count, const char *field_name )
            {
                if( remaining() < count )
                {
                    throw DecodeError( std::string( "Unexpected end of IMX while reading " ) + field_name );
                }

                const auto result = std::span<const std::uint8_t>( bytes_.data() + offset_, count );

                offset_ += count;

                return result;
            }

            void skip( std::size_t count, const char *field_name )
            {
                (void)read_bytes( count, field_name );
            }

        private:
            std::span<const std::uint8_t> bytes_;
            std::size_t offset_ = 0;
        };

        std::size_t checked_pixel_count( std::uint32_t width, std::uint32_t height )
        {
            const std::uint64_t pixels = static_cast<std::uint64_t>( width ) * static_cast<std::uint64_t>( height );

            if( pixels == 0 )
            {
                throw DecodeError( "IMX width/height must be non-zero" );
            }

            if( pixels > static_cast<std::uint64_t>( std::numeric_limits<std::size_t>::max() / 4 ) )
            {
                throw DecodeError( "IMX image is too large to decode safely" );
            }

            return static_cast<std::size_t>( pixels );
        }

        std::vector<PaletteEntry> decode_palette( std::span<const std::uint8_t> bytes )
        {
            if( ( bytes.size() % 4 ) != 0 )
            {
                throw DecodeError( "IMX palette size is not divisible by 4" );
            }

            std::vector<PaletteEntry> palette;
            palette.reserve( bytes.size() / 4 );

            for( std::size_t i = 0; i < bytes.size(); i += 4 )
            {
                palette.push_back( PaletteEntry{
                    bytes[ i + 0 ],
                    bytes[ i + 1 ],
                    bytes[ i + 2 ],
                    alpha80_to_alpha255( bytes[ i + 3 ] )
                } );
            }

            return palette;
        }

        void write_palette_pixel( std::vector<std::uint8_t> &rgba,
            std::size_t pixel_index,
            const std::vector<PaletteEntry> &palette,
            std::uint8_t palette_index )
        {
            if( palette_index >= palette.size() )
            {
                std::ostringstream message;
                message << "IMX palette index " << static_cast<std::uint32_t>( palette_index )
                        << " is outside palette size " << palette.size();

                throw DecodeError( message.str() );
            }

            const PaletteEntry &entry = palette[ palette_index ];

            const std::size_t out = pixel_index * 4;

            rgba[ out + 0 ] = entry.r;

            rgba[ out + 1 ] = entry.g;

            rgba[ out + 2 ] = entry.b;

            rgba[ out + 3 ] = entry.a;
        }

        void decode_indexed4( Image &image, std::span<const std::uint8_t> image_data, std::size_t pixel_count )
        {
            const std::size_t expected_bytes = ( pixel_count + 1 ) / 2;

            if( image_data.size() < expected_bytes )
            {
                throw DecodeError( "IMX 4-bit indexed image data is too small" );
            }

            if( image.palette.size() < 16 )
            {
                throw DecodeError( "IMX 4-bit indexed image does not have at least 16 palette entries" );
            }

            image.rgba.assign( pixel_count * 4, 0 );

            std::size_t pixel_index = 0;

            for( std::size_t i = 0; i < expected_bytes; ++i )
            {
                const std::uint8_t packed = image_data[ i ];

                const std::uint8_t low_index = static_cast<std::uint8_t>( packed & 0x0F );

                const std::uint8_t high_index = static_cast<std::uint8_t>( ( packed >> 4 ) & 0x0F );

                if( pixel_index < pixel_count )
                {
                    write_palette_pixel( image.rgba, pixel_index++, image.palette, low_index );
                }

                if( pixel_index < pixel_count )
                {
                    write_palette_pixel( image.rgba, pixel_index++, image.palette, high_index );
                }
            }
        }

        void decode_indexed8( Image &image, std::span<const std::uint8_t> image_data, std::size_t pixel_count )
        {
            if( image_data.size() < pixel_count )
            {
                throw DecodeError( "IMX 8-bit indexed image data is too small" );
            }

            if( image.palette.size() < 256 )
            {
                throw DecodeError( "IMX 8-bit indexed image does not have at least 256 palette entries" );
            }

            image.rgba.assign( pixel_count * 4, 0 );

            for( std::size_t i = 0; i < pixel_count; ++i )
            {
                write_palette_pixel( image.rgba, i, image.palette, image_data[ i ] );
            }
        }

        void decode_rgb888( Image &image, std::span<const std::uint8_t> image_data, std::size_t pixel_count )
        {
            const std::size_t expected_bytes = pixel_count * 3;

            if( image_data.size() < expected_bytes )
            {
                throw DecodeError( "IMX RGB888 image data is too small" );
            }

            image.rgba.assign( pixel_count * 4, 0 );

            for( std::size_t i = 0; i < pixel_count; ++i )
            {
                const std::size_t in = i * 3;
                const std::size_t out = i * 4;

                image.rgba[ out + 0 ] = image_data[ in + 0 ];

                image.rgba[ out + 1 ] = image_data[ in + 1 ];

                image.rgba[ out + 2 ] = image_data[ in + 2 ];

                image.rgba[ out + 3 ] = 255;
            }
        }

        void decode_rgba8888( Image &image, std::span<const std::uint8_t> image_data, std::size_t pixel_count )
        {
            const std::size_t expected_bytes = pixel_count * 4;

            if( image_data.size() < expected_bytes )
            {
                throw DecodeError( "IMX RGBA8888 image data is too small" );
            }

            image.rgba.assign( pixel_count * 4, 0 );

            for( std::size_t i = 0; i < pixel_count; ++i )
            {
                const std::size_t in = i * 4;
                const std::size_t out = i * 4;

                image.rgba[ out + 0 ] = image_data[ in + 0 ];

                image.rgba[ out + 1 ] = image_data[ in + 1 ];

                image.rgba[ out + 2 ] = image_data[ in + 2 ];

                image.rgba[ out + 3 ] = alpha80_to_alpha255( image_data[ in + 3 ] );
            }
        }
    }

    DecodeError::DecodeError( const std::string &message ) : std::runtime_error( message )
    {
    }

    std::uint8_t alpha80_to_alpha255( std::uint8_t alpha80 )
    {
        const std::uint32_t clamped = std::min<std::uint32_t>( alpha80, 128u );

        return static_cast<std::uint8_t>( ( clamped * 255u ) / 128u );
    }

    PixelStorageMode storage_mode_from_values( std::uint32_t value_1, std::uint32_t value_2 )
    {
        if( value_1 == 0 && value_2 == 0 )
        {
            return PixelStorageMode::Indexed4;
        }

        if( value_1 == 1 && value_2 == 1 )
        {
            return PixelStorageMode::Indexed8;
        }

        if( value_1 == 3 && value_2 == 2 )
        {
            return PixelStorageMode::Rgb888;
        }

        if( value_1 == 4 && value_2 == 2 )
        {
            return PixelStorageMode::Rgba8888;
        }

        std::ostringstream message;
        message << "Unsupported IMX pixel storage mode pair: " << value_1 << ", " << value_2;

        throw DecodeError( message.str() );
    }

    Image decode( std::span<const std::uint8_t> bytes )
    {
        parser::Reader reader( bytes );

        const std::uint32_t magic = reader.read_u32( "magic" );

        if( magic != parser::imx_magic )
        {
            throw DecodeError( "Invalid IMX magic" );
        }

        reader.skip( 16, "reserved header bytes" );

        Image image{};
        image.width = reader.read_u32( "width" );

        image.height = reader.read_u32( "height" );

        image.mode_value_1 = reader.read_u32( "pixel storage mode value 1" );

        image.mode_value_2 = reader.read_u32( "pixel storage mode value 2" );

        image.storage_mode = storage_mode_from_values( image.mode_value_1, image.mode_value_2 );

        const std::size_t pixel_count = parser::checked_pixel_count( image.width, image.height );

        const bool indexed =
            image.storage_mode == PixelStorageMode::Indexed4 || image.storage_mode == PixelStorageMode::Indexed8;

        if( indexed )
        {
            const std::uint32_t palette_size = reader.read_u32( "palette size" );

            const auto palette_bytes = reader.read_bytes( palette_size, "palette data" );

            image.palette = parser::decode_palette( palette_bytes );

            image.indexed_palette_trailer = reader.read_u32( "indexed palette trailer" );
        }

        const std::uint32_t image_data_size = reader.read_u32( "image data size" );

        const auto image_data = reader.read_bytes( image_data_size, "image data" );

        switch( image.storage_mode )
        {
            case PixelStorageMode::Indexed4:
                parser::decode_indexed4( image, image_data, pixel_count );
                break;

            case PixelStorageMode::Indexed8:
                parser::decode_indexed8( image, image_data, pixel_count );
                break;

            case PixelStorageMode::Rgb888:
                parser::decode_rgb888( image, image_data, pixel_count );
                break;

            case PixelStorageMode::Rgba8888:
                parser::decode_rgba8888( image, image_data, pixel_count );
                break;
        }

        if( reader.remaining() >= 8 )
        {
            image.footer_value_0 = reader.read_u32( "footer value 0" );

            image.footer_value_1 = reader.read_u32( "footer value 1" );
        }

        return image;
    }

    Image load_file( const std::filesystem::path &path )
    {
        std::ifstream file( path, std::ios::binary );

        if( !file )
        {
            throw DecodeError( "Could not open IMX file: " + path.string() );
        }

        file.seekg( 0, std::ios::end );

        const std::streamoff size = file.tellg();

        if( size <= 0 )
        {
            throw DecodeError( "IMX file is empty: " + path.string() );
        }

        file.seekg( 0, std::ios::beg );

        std::vector<std::uint8_t> bytes( static_cast<std::size_t>( size ) );

        file.read( reinterpret_cast<char *>( bytes.data() ), static_cast<std::streamsize>( bytes.size() ) );

        if( !file )
        {
            throw DecodeError( "Could not read full IMX file: " + path.string() );
        }

        return decode( bytes );
    }
}
