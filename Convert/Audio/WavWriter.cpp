#include "Audio/WavWriter.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace piko::convert::wav
{
    void write_u16( std::ofstream &file, std::uint16_t value )
    {
        const std::array<char, 2> bytes{
            static_cast<char>( value & 0xffu ),
            static_cast<char>( ( value >> 8 ) & 0xffu )
        };

        file.write( bytes.data(), static_cast<std::streamsize>( bytes.size() ) );
    }

    void write_u32( std::ofstream &file, std::uint32_t value )
    {
        const std::array<char, 4> bytes{
            static_cast<char>( value & 0xffu ),
            static_cast<char>( ( value >> 8 ) & 0xffu ),
            static_cast<char>( ( value >> 16 ) & 0xffu ),
            static_cast<char>( ( value >> 24 ) & 0xffu )
        };

        file.write( bytes.data(), static_cast<std::streamsize>( bytes.size() ) );
    }

    void write_subformat_guid( std::ofstream &file, WavFormatTag format )
    {
        write_u32( file, static_cast<std::uint32_t>( format ) );
        write_u16( file, 0u );
        write_u16( file, 0x0010u );
        const std::array<std::uint8_t, 8> tail{
            0x80u,
            0x00u,
            0x00u,
            0xaau,
            0x00u,
            0x38u,
            0x9bu,
            0x71u
        };

        file.write( reinterpret_cast<const char *>( tail.data() ), static_cast<std::streamsize>( tail.size() ) );
    }
}

namespace piko::convert
{
    void write_interleaved_wav( const std::filesystem::path &path,
        const WavWriteOptions &options,
        std::span<const std::uint8_t> interleaved_sample_bytes )
    {
        if( options.channel_count == 0 )
        {
            throw std::runtime_error( "WAV channel count cannot be zero" );
        }

        if( options.sample_rate == 0 )
        {
            throw std::runtime_error( "WAV sample rate cannot be zero" );
        }

        if( options.bits_per_sample == 0 || options.bits_per_sample % 8u != 0 )
        {
            throw std::runtime_error( "WAV bits per sample must be a nonzero multiple of 8" );
        }

        if( interleaved_sample_bytes.size() > std::numeric_limits<std::uint32_t>::max() )
        {
            throw std::runtime_error( "WAV sample data exceeds RIFF size limits" );
        }

        const std::uint32_t bytes_per_sample = options.bits_per_sample / 8u;
        const std::uint64_t block_align_u64 = static_cast<std::uint64_t>( options.channel_count ) * bytes_per_sample;

        if( block_align_u64 > std::numeric_limits<std::uint16_t>::max() )
        {
            throw std::runtime_error( "WAV block alignment exceeds format limits" );
        }

        const std::uint16_t block_align = static_cast<std::uint16_t>( block_align_u64 );

        if( interleaved_sample_bytes.size() % block_align != 0u )
        {
            throw std::runtime_error( "WAV sample data is not frame aligned" );
        }

        const std::uint64_t byte_rate_u64 = static_cast<std::uint64_t>( options.sample_rate ) * block_align;

        if( byte_rate_u64 > std::numeric_limits<std::uint32_t>::max() )
        {
            throw std::runtime_error( "WAV byte rate exceeds format limits" );
        }

        const bool extensible = options.use_extensible && options.channel_count > 2u;
        const std::uint32_t fmt_size = extensible ? 40u : 16u;
        const std::uint32_t data_size = static_cast<std::uint32_t>( interleaved_sample_bytes.size() );
        const std::uint32_t data_padding = data_size & 1u;
        const std::uint64_t riff_size_u64 = 4u + 8u + fmt_size + 8u + data_size + data_padding;

        if( riff_size_u64 > std::numeric_limits<std::uint32_t>::max() )
        {
            throw std::runtime_error( "WAV RIFF size exceeds format limits" );
        }

        if( path.has_parent_path() )
        {
            std::filesystem::create_directories( path.parent_path() );
        }

        std::ofstream file( path, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not create WAV file: " + path.string() );
        }

        file.write( "RIFF", 4 );
        wav::write_u32( file, static_cast<std::uint32_t>( riff_size_u64 ) );
        file.write( "WAVE", 4 );
        file.write( "fmt ", 4 );
        wav::write_u32( file, fmt_size );
        wav::write_u16( file, extensible ? 0xfffeu : static_cast<std::uint16_t>( options.format ) );
        wav::write_u16( file, options.channel_count );
        wav::write_u32( file, options.sample_rate );
        wav::write_u32( file, static_cast<std::uint32_t>( byte_rate_u64 ) );
        wav::write_u16( file, block_align );
        wav::write_u16( file, options.bits_per_sample );

        if( extensible )
        {
            wav::write_u16( file, 22u );
            wav::write_u16( file, options.bits_per_sample );
            wav::write_u32( file, options.channel_mask );
            wav::write_subformat_guid( file, options.format );
        }

        file.write( "data", 4 );
        wav::write_u32( file, data_size );

        if( !interleaved_sample_bytes.empty() )
        {
            file.write( reinterpret_cast<const char *>( interleaved_sample_bytes.data() ),
                static_cast<std::streamsize>( interleaved_sample_bytes.size() ) );
        }

        if( data_padding != 0u )
        {
            file.put( '\0' );
        }

        if( !file )
        {
            throw std::runtime_error( "Could not write WAV file: " + path.string() );
        }
    }
}
