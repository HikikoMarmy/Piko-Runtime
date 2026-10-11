#include "Image/PngWriter.hpp"

#include <cstdint>
#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace piko::convert::png
{
    std::uint32_t crc32( std::span<const std::uint8_t> bytes )
    {
        std::uint32_t crc = 0xffffffffu;

        for( const std::uint8_t byte : bytes )
        {
            crc ^= byte;

            for( std::int32_t bit = 0; bit < 8; ++bit )
            {
                crc = ( crc >> 1 ) ^ ( 0xedb88320u & ( 0u - ( crc & 1u ) ) );
            }
        }

        return crc ^ 0xffffffffu;
    }

    std::uint32_t adler32( std::span<const std::uint8_t> bytes )
    {
        constexpr std::uint32_t modulus = 65521u;
        std::uint32_t a = 1u;
        std::uint32_t b = 0u;

        for( const std::uint8_t byte : bytes )
        {
            a = ( a + byte ) % modulus;

            b = ( b + a ) % modulus;
        }

        return ( b << 16 ) | a;
    }

    void append_be32( std::vector<std::uint8_t> &output, std::uint32_t value )
    {
        output.push_back( static_cast<std::uint8_t>( ( value >> 24 ) & 0xffu ) );
        output.push_back( static_cast<std::uint8_t>( ( value >> 16 ) & 0xffu ) );
        output.push_back( static_cast<std::uint8_t>( ( value >> 8 ) & 0xffu ) );
        output.push_back( static_cast<std::uint8_t>( value & 0xffu ) );
    }

    void append_le16( std::vector<std::uint8_t> &output, std::uint16_t value )
    {
        output.push_back( static_cast<std::uint8_t>( value & 0xffu ) );
        output.push_back( static_cast<std::uint8_t>( ( value >> 8 ) & 0xffu ) );
    }

    void append_chunk( std::vector<std::uint8_t> &png,
        const std::array<std::uint8_t, 4> &type,
        std::span<const std::uint8_t> data )
    {
        if( data.size() > std::numeric_limits<std::uint32_t>::max() )
        {
            throw std::runtime_error( "PNG chunk is too large" );
        }

        append_be32( png, static_cast<std::uint32_t>( data.size() ) );
        const std::size_t crc_begin = png.size();
        png.insert( png.end(), type.begin(), type.end() );
        png.insert( png.end(), data.begin(), data.end() );
        append_be32( png, crc32( std::span<const std::uint8_t>( png.data() + crc_begin, png.size() - crc_begin ) ) );
    }

    std::vector<std::uint8_t> make_scanlines( std::uint32_t width,
        std::uint32_t height,
        std::span<const std::uint8_t> rgba )
    {
        const std::uint64_t row_bytes = static_cast<std::uint64_t>( width ) * 4u;
        const std::uint64_t expected = row_bytes * height;

        if( expected != rgba.size() )
        {
            throw std::runtime_error( "PNG RGBA byte count does not match dimensions" );
        }

        const std::uint64_t output_size = ( row_bytes + 1u ) * height;

        if( output_size > std::numeric_limits<std::size_t>::max() )
        {
            throw std::runtime_error( "PNG image is too large" );
        }

        std::vector<std::uint8_t> scanlines;
        scanlines.resize( static_cast<std::size_t>( output_size ) );

        for( std::uint32_t y = 0; y < height; ++y )
        {
            const std::size_t destination =
                static_cast<std::size_t>( y ) * ( static_cast<std::size_t>( row_bytes ) + 1u );

            const std::size_t source = static_cast<std::size_t>( y ) * static_cast<std::size_t>( row_bytes );
            scanlines[ destination ] = 0;
            std::copy_n( rgba.data() + source,
                static_cast<std::size_t>( row_bytes ),
                scanlines.data() + destination + 1u );
        }

        return scanlines;
    }

    std::vector<std::uint8_t> make_zlib_stream( std::span<const std::uint8_t> bytes )
    {
        std::vector<std::uint8_t> output;
        output.reserve( bytes.size() + bytes.size() / 65535u * 5u + 16u );
        output.push_back( 0x78u );
        output.push_back( 0x01u );

        std::size_t offset = 0;
        while( offset < bytes.size() )
        {
            const std::size_t remaining = bytes.size() - offset;
            const std::uint16_t block_size = static_cast<std::uint16_t>( std::min<std::size_t>( remaining, 65535u ) );
            const bool final_block = offset + block_size == bytes.size();
            output.push_back( final_block ? 0x01u : 0x00u );
            append_le16( output, block_size );
            append_le16( output, static_cast<std::uint16_t>( ~block_size ) );
            output.insert( output.end(),
                bytes.begin() + static_cast<std::ptrdiff_t>( offset ),
                bytes.begin() + static_cast<std::ptrdiff_t>( offset + block_size ) );

            offset += block_size;
        }

        if( bytes.empty() )
        {
            output.push_back( 0x01u );
            append_le16( output, 0u );
            append_le16( output, 0xffffu );
        }

        append_be32( output, adler32( bytes ) );
        return output;
    }
}

namespace piko::convert
{
    void write_rgba_png( const std::filesystem::path &path,
        std::uint32_t width,
        std::uint32_t height,
        std::span<const std::uint8_t> rgba )
    {
        if( width == 0 || height == 0 )
        {
            throw std::runtime_error( "PNG dimensions cannot be zero" );
        }

        const std::vector<std::uint8_t> scanlines = png::make_scanlines( width, height, rgba );
        const std::vector<std::uint8_t> compressed = png::make_zlib_stream( scanlines );

        std::vector<std::uint8_t> png{
            0x89u,
            0x50u,
            0x4eu,
            0x47u,
            0x0du,
            0x0au,
            0x1au,
            0x0au
        };

        std::vector<std::uint8_t> ihdr;
        ihdr.reserve( 13u );
        png::append_be32( ihdr, width );
        png::append_be32( ihdr, height );
        ihdr.push_back( 8u );
        ihdr.push_back( 6u );
        ihdr.push_back( 0u );
        ihdr.push_back( 0u );
        ihdr.push_back( 0u );

        png::append_chunk( png, { 'I', 'H', 'D', 'R' }, ihdr );
        png::append_chunk( png, { 'I', 'D', 'A', 'T' }, compressed );
        png::append_chunk( png, { 'I', 'E', 'N', 'D' }, {} );

        if( path.has_parent_path() )
        {
            std::filesystem::create_directories( path.parent_path() );
        }

        std::ofstream file( path, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not create PNG file: " + path.string() );
        }

        file.write( reinterpret_cast<const char *>( png.data() ), static_cast<std::streamsize>( png.size() ) );

        if( !file )
        {
            throw std::runtime_error( "Could not write PNG file: " + path.string() );
        }
    }
}
