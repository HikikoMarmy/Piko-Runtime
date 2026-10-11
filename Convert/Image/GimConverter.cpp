#include "Image/GimConverter.hpp"

#include "Utils/BinaryUtil.hpp"

#include "Image/PngWriter.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace piko::convert::gim
{
    enum class Endian
    {
        Little,
        Big
    };

    struct Reader
    {
        std::span<const std::uint8_t> bytes;
        Endian endian = Endian::Little;

        std::uint16_t u16( std::size_t offset ) const
        {
            if( offset + 2u > bytes.size() )
            {
                throw std::runtime_error( "GIM read exceeds file size" );
            }

            return endian == Endian::Little ? piko::utils::read_le_u16( bytes.data() + offset )
                                            : piko::utils::read_be_u16( bytes.data() + offset );
        }

        std::uint32_t u32( std::size_t offset ) const
        {
            if( offset + 4u > bytes.size() )
            {
                throw std::runtime_error( "GIM read exceeds file size" );
            }

            return endian == Endian::Little ? piko::utils::read_le_u32( bytes.data() + offset )
                                            : piko::utils::read_be_u32( bytes.data() + offset );
        }
    };

    struct Block
    {
        std::size_t offset = 0u;
        std::uint16_t id = 0u;
        std::uint32_t size = 0u;
        std::uint32_t next = 0u;
        std::uint32_t data_offset = 0u;
    };

    struct ImageInfo
    {
        std::size_t block_offset = 0u;
        std::size_t data_offset = 0u;
        std::uint16_t format = 0u;
        std::uint16_t pixel_order = 0u;
        std::uint16_t width = 0u;
        std::uint16_t height = 0u;
        std::uint16_t bits_per_pixel = 0u;
        std::uint32_t pixels_start = 0u;
        std::uint32_t pixels_end = 0u;
    };

    Endian detect_endian( std::span<const std::uint8_t> bytes )
    {
        if( bytes.size() < 16u )
        {
            throw std::runtime_error( "GIM file is too small" );
        }

        const std::array<std::uint8_t, 4> little{
            'M',
            'I',
            'G',
            '.'
        };

        const std::array<std::uint8_t, 4> big{
            '.',
            'G',
            'I',
            'M'
        };

        if( std::equal( little.begin(), little.end(), bytes.begin() ) )
        {
            return Endian::Little;
        }

        if( std::equal( big.begin(), big.end(), bytes.begin() ) )
        {
            return Endian::Big;
        }

        throw std::runtime_error( "Unsupported GIM signature" );
    }

    Block read_block( const Reader &reader, std::size_t offset )
    {
        if( offset + 16u > reader.bytes.size() )
        {
            throw std::runtime_error( "GIM block header exceeds file size" );
        }

        Block block;
        block.offset = offset;

        block.id = reader.u16( offset );

        block.size = reader.u32( offset + 4u );

        block.next = reader.u32( offset + 8u );

        block.data_offset = reader.u32( offset + 12u );

        if( block.size < 16u || block.offset + block.size > reader.bytes.size() )
        {
            throw std::runtime_error( "GIM block has an invalid size" );
        }

        if( block.data_offset < 16u || block.data_offset > block.size )
        {
            throw std::runtime_error( "GIM block has an invalid data offset" );
        }

        return block;
    }

    void collect_picture_blocks( const Reader &reader, const Block &container, std::vector<Block> &output )
    {
        if( container.id != 0x02u && container.id != 0x03u )
        {
            return;
        }

        const std::size_t end = container.offset + container.size;
        std::size_t child_offset = container.offset + container.data_offset;
        std::size_t guard = 0u;

        while( child_offset + 16u <= end && child_offset + 16u <= reader.bytes.size() && guard++ < 256u )
        {
            const Block child = read_block( reader, child_offset );
            output.push_back( child );

            if( child.id == 0x02u || child.id == 0x03u )
            {
                collect_picture_blocks( reader, child, output );
            }

            if( child.next == 0u || child.next < 16u )
            {
                break;
            }

            const std::size_t next_offset = child.offset + child.next;

            if( next_offset <= child.offset || next_offset >= end )
            {
                break;
            }

            child_offset = next_offset;
        }
    }

    ImageInfo image_info( const Reader &reader, const Block &block )
    {
        if( block.id != 0x04u && block.id != 0x05u )
        {
            throw std::runtime_error( "GIM block is not an image or palette" );
        }

        const std::size_t data = block.offset + block.data_offset;

        if( data + 0x30u > block.offset + block.size )
        {
            throw std::runtime_error( "GIM image header is truncated" );
        }

        ImageInfo info;
        info.block_offset = block.offset;

        info.data_offset = data;

        info.format = reader.u16( data + 0x04u );

        info.pixel_order = reader.u16( data + 0x06u );

        info.width = reader.u16( data + 0x08u );

        info.height = reader.u16( data + 0x0au );

        info.bits_per_pixel = reader.u16( data + 0x0cu );

        info.pixels_start = reader.u32( data + 0x1cu );

        info.pixels_end = reader.u32( data + 0x20u );

        if( info.width == 0u || info.height == 0u )
        {
            throw std::runtime_error( "GIM image has zero dimensions" );
        }

        if( info.pixels_start >= info.pixels_end )
        {
            throw std::runtime_error( "GIM image has an invalid pixel range" );
        }

        if( data + info.pixels_end > block.offset + block.size )
        {
            throw std::runtime_error( "GIM pixel data exceeds its block" );
        }

        return info;
    }

    std::size_t format_bits_per_pixel( std::uint16_t format, std::uint16_t declared )
    {
        switch( format )
        {
            case 0u:
            case 1u:
            case 2u:
                return 16u;
            case 3u:
                return 32u;
            case 4u:
                return 4u;
            case 5u:
                return 8u;
            case 6u:
                return 16u;
            case 7u:
                return 32u;
            default:
                break;
        }

        if( declared != 0u )
        {
            return declared;
        }

        throw std::runtime_error( "Unsupported GIM image format" );
    }

    std::vector<std::uint8_t> unswizzle_psp( std::span<const std::uint8_t> source,
        std::size_t row_bytes,
        std::size_t height )
    {
        if( row_bytes < 16u || height < 8u || row_bytes % 16u != 0u || height % 8u != 0u )
        {
            return std::vector<std::uint8_t>( source.begin(), source.end() );
        }

        const std::size_t required = row_bytes * height;

        if( source.size() < required )
        {
            throw std::runtime_error( "Swizzled GIM pixel data is truncated" );
        }

        std::vector<std::uint8_t> output( required );
        const std::size_t blocks_per_row = row_bytes / 16u;
        std::size_t source_offset = 0u;

        for( std::size_t block_y = 0u; block_y < height / 8u; ++block_y )
        {
            for( std::size_t block_x = 0u; block_x < blocks_per_row; ++block_x )
            {
                for( std::size_t row = 0u; row < 8u; ++row )
                {
                    const std::size_t destination = ( block_y * 8u + row ) * row_bytes + block_x * 16u;
                    std::copy_n( source.data() + source_offset, 16u, output.data() + destination );
                    source_offset += 16u;
                }
            }
        }

        return output;
    }

    std::uint32_t read_packed_value( std::span<const std::uint8_t> bytes,
        std::size_t bit_offset,
        std::size_t bit_count )
    {
        if( bit_count == 0u || bit_count > 32u )
        {
            throw std::runtime_error( "Unsupported GIM packed pixel size" );
        }

        std::uint32_t value = 0u;

        for( std::size_t bit = 0u; bit < bit_count; ++bit )
        {
            const std::size_t absolute = bit_offset + bit;
            const std::size_t byte_index = absolute / 8u;

            if( byte_index >= bytes.size() )
            {
                throw std::runtime_error( "GIM pixel data is truncated" );
            }

            const std::uint8_t source_bit =
                static_cast<std::uint8_t>( ( bytes[ byte_index ] >> ( absolute % 8u ) ) & 1u );

            value |= static_cast<std::uint32_t>( source_bit ) << bit;
        }

        return value;
    }

    std::array<std::uint8_t, 4> decode_direct_color( std::uint16_t format, std::uint32_t value )
    {
        switch( format )
        {
            case 0u:
                return {
                    static_cast<std::uint8_t>( ( value & 0x1fu ) * 255u / 31u ),
                    static_cast<std::uint8_t>( ( ( value >> 5u ) & 0x3fu ) * 255u / 63u ),
                    static_cast<std::uint8_t>( ( ( value >> 11u ) & 0x1fu ) * 255u / 31u ),
                    255u
                };
            case 1u:
                return {
                    static_cast<std::uint8_t>( ( value & 0x1fu ) * 255u / 31u ),
                    static_cast<std::uint8_t>( ( ( value >> 5u ) & 0x1fu ) * 255u / 31u ),
                    static_cast<std::uint8_t>( ( ( value >> 10u ) & 0x1fu ) * 255u / 31u ),
                    static_cast<std::uint8_t>( ( value & 0x8000u ) != 0u ? 255u : 0u )
                };
            case 2u:
                return {
                    static_cast<std::uint8_t>( ( value & 0x0fu ) * 17u ),
                    static_cast<std::uint8_t>( ( ( value >> 4u ) & 0x0fu ) * 17u ),
                    static_cast<std::uint8_t>( ( ( value >> 8u ) & 0x0fu ) * 17u ),
                    static_cast<std::uint8_t>( ( ( value >> 12u ) & 0x0fu ) * 17u )
                };
            case 3u:
                return {
                    static_cast<std::uint8_t>( value & 0xffu ),
                    static_cast<std::uint8_t>( ( value >> 8u ) & 0xffu ),
                    static_cast<std::uint8_t>( ( value >> 16u ) & 0xffu ),
                    static_cast<std::uint8_t>( ( value >> 24u ) & 0xffu )
                };
            default:
                throw std::runtime_error( "Unsupported direct GIM color format" );
        }
    }

    std::vector<std::uint8_t> decode_palette( const Reader &reader, const Block &block )
    {
        const ImageInfo info = image_info( reader, block );

        if( info.format > 3u )
        {
            throw std::runtime_error( "Unsupported GIM palette color format" );
        }

        const std::size_t bits = format_bits_per_pixel( info.format, info.bits_per_pixel );
        const std::size_t color_count = static_cast<std::size_t>( info.width ) * info.height;
        const std::size_t row_bits = static_cast<std::size_t>( info.width ) * bits;
        const std::size_t row_bytes = ( row_bits + 7u ) / 8u;
        const std::size_t byte_count = row_bytes * info.height;
        const std::size_t pixel_offset = info.data_offset + info.pixels_start;
        std::span<const std::uint8_t> packed = reader.bytes.subspan( pixel_offset,
            std::min<std::size_t>( byte_count, info.data_offset + info.pixels_end - pixel_offset ) );

        std::vector<std::uint8_t> linear = info.pixel_order == 1u
                                               ? unswizzle_psp( packed, row_bytes, info.height )
                                               : std::vector<std::uint8_t>( packed.begin(), packed.end() );

        std::vector<std::uint8_t> rgba( color_count * 4u );

        for( std::size_t index = 0u; index < color_count; ++index )
        {
            const std::uint32_t value = read_packed_value( linear, index * bits, bits );

            const auto color = decode_direct_color( info.format, value );
            std::copy( color.begin(), color.end(), rgba.begin() + static_cast<std::ptrdiff_t>( index * 4u ) );
        }

        return rgba;
    }

    std::vector<std::uint8_t> decode_image( const Reader &reader, const Block &image_block, const Block *palette_block )
    {
        const ImageInfo info = image_info( reader, image_block );

        if( info.format >= 8u )
        {
            throw std::runtime_error( "DXT-compressed GIM images are not supported yet" );
        }

        const std::size_t bits = format_bits_per_pixel( info.format, info.bits_per_pixel );
        const std::size_t pixel_count = static_cast<std::size_t>( info.width ) * info.height;

        if( pixel_count > std::numeric_limits<std::size_t>::max() / 4u )
        {
            throw std::runtime_error( "GIM image is too large" );
        }

        const std::size_t row_bits = static_cast<std::size_t>( info.width ) * bits;
        const std::size_t row_bytes = ( row_bits + 7u ) / 8u;
        const std::size_t required_bytes = row_bytes * info.height;
        const std::size_t pixel_offset = info.data_offset + info.pixels_start;
        const std::size_t available_bytes = info.data_offset + info.pixels_end - pixel_offset;

        if( available_bytes < required_bytes )
        {
            throw std::runtime_error( "GIM pixel data is smaller than its dimensions require" );
        }

        std::span<const std::uint8_t> packed = reader.bytes.subspan( pixel_offset, required_bytes );
        std::vector<std::uint8_t> linear = info.pixel_order == 1u
                                               ? unswizzle_psp( packed, row_bytes, info.height )
                                               : std::vector<std::uint8_t>( packed.begin(), packed.end() );

        std::vector<std::uint8_t> palette;

        if( info.format >= 4u )
        {
            if( palette_block == nullptr )
            {
                throw std::runtime_error( "Indexed GIM image has no palette block" );
            }

            palette = decode_palette( reader, *palette_block );
        }

        std::vector<std::uint8_t> rgba( pixel_count * 4u );

        for( std::size_t index = 0u; index < pixel_count; ++index )
        {
            std::array<std::uint8_t, 4> color{};

            if( info.format <= 3u )
            {
                color = decode_direct_color( info.format, read_packed_value( linear, index * bits, bits ) );
            }
            else
            {
                const std::uint32_t palette_index = read_packed_value( linear, index * bits, bits );
                const std::size_t palette_offset = static_cast<std::size_t>( palette_index ) * 4u;

                if( palette_offset + 4u > palette.size() )
                {
                    throw std::runtime_error( "GIM palette index is out of range" );
                }

                std::copy_n( palette.data() + palette_offset, 4u, color.data() );
            }

            std::copy( color.begin(), color.end(), rgba.begin() + static_cast<std::ptrdiff_t>( index * 4u ) );
        }

        return rgba;
    }
}

namespace piko::convert
{
    ConvertedTexture convert_gim_to_png( std::span<const std::uint8_t> bytes,
        const std::filesystem::path &output_path,
        std::string source_name,
        const std::filesystem::path &centre_directory )
    {
        const gim::Reader reader{ bytes, gim::detect_endian( bytes ) };
        const gim::Block root = gim::read_block( reader, 16u );

        if( root.id != 0x02u )
        {
            throw std::runtime_error( "GIM root block is missing" );
        }

        std::vector<gim::Block> blocks;
        gim::collect_picture_blocks( reader, root, blocks );

        const gim::Block *image = nullptr;
        const gim::Block *palette = nullptr;

        for( const gim::Block &block : blocks )
        {
            if( block.id == 0x05u && palette == nullptr )
            {
                palette = &block;
            }
            else if( block.id == 0x04u && image == nullptr )
            {
                image = &block;
            }
        }

        if( image == nullptr )
        {
            throw std::runtime_error( "GIM contains no image block" );
        }

        const gim::ImageInfo info = gim::image_info( reader, *image );
        const std::vector<std::uint8_t> rgba = gim::decode_image( reader, *image, palette );
        write_rgba_png( output_path, info.width, info.height, rgba );
        export_battle_sprites( rgba, info.width, info.height, output_path, centre_directory );

        ConvertedTexture converted;
        converted.source_name = std::move( source_name );

        converted.file_name = output_path.filename().string();

        converted.width = info.width;

        converted.height = info.height;

        converted.storage_mode = "gim";
        return converted;
    }
}
