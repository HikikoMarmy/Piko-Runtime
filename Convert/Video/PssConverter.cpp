#include "Video/PssConverter.hpp"

#include "Utility/ProcessUtil.hpp"
#include "Utils/BinaryUtil.hpp"

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <array>

namespace piko::convert::pss_converter
{
    std::filesystem::path make_temp_path( const std::filesystem::path &parent, std::string_view extension )
    {
        static std::atomic<std::uint64_t> counter{ 0 };

        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        return parent / ( "piko_pss_" + std::to_string( stamp ) + "_" + std::to_string( counter.fetch_add( 1 ) ) +
                            std::string( extension ) );
    }

    void write_temp_file( const std::filesystem::path &path, std::span<const std::uint8_t> bytes )
    {
        std::ofstream file( path, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not create temporary PSS file: " + path.string() );
        }

        if( !bytes.empty() )
        {
            file.write( reinterpret_cast<const char *>( bytes.data() ), static_cast<std::streamsize>( bytes.size() ) );
        }

        if( !file )
        {
            throw std::runtime_error( "Could not write temporary PSS file: " + path.string() );
        }
    }

    std::uint16_t read_be16( std::span<const std::uint8_t> bytes, std::size_t offset )
    {
        return piko::utils::read_be_u16( bytes.data() + offset );
    }

    std::uint32_t read_le32( std::span<const std::uint8_t> bytes, std::size_t offset )
    {
        return piko::utils::read_le_u32( bytes.data() + offset );
    }

    std::optional<std::size_t> pes_payload_offset( std::span<const std::uint8_t> bytes,
        std::size_t packet_begin,
        std::size_t packet_end )
    {
        std::size_t position = packet_begin + 6u;

        if( position >= packet_end )
        {
            return std::nullopt;
        }

        if( ( bytes[ position ] & 0xC0u ) == 0x80u )
        {
            if( position + 3u > packet_end )
            {
                return std::nullopt;
            }

            const std::size_t header_size = bytes[ position + 2u ];
            position += 3u + header_size;

            if( position > packet_end )
            {
                return std::nullopt;
            }

            return position;
        }

        while( position < packet_end && bytes[ position ] == 0xFFu )
        {
            ++position;
        }

        if( position + 2u <= packet_end && ( bytes[ position ] & 0xC0u ) == 0x40u )
        {
            position += 2u;
        }

        if( position >= packet_end )
        {
            return std::nullopt;
        }

        if( ( bytes[ position ] & 0xF0u ) == 0x20u )
        {
            position += 5u;
        }
        else if( ( bytes[ position ] & 0xF0u ) == 0x30u )
        {
            position += 10u;
        }
        else if( bytes[ position ] == 0x0Fu )
        {
            ++position;
        }

        if( position > packet_end )
        {
            return std::nullopt;
        }

        return position;
    }

    struct AdsPacketLayout
    {
        std::uint8_t substream = 0u;
        std::size_t prefix_size = 0u;
        std::size_t substream_offset = 0u;
    };

    std::optional<AdsPacketLayout> find_ps2_ads_packet_layout( std::span<const std::uint8_t> bytes )
    {
        constexpr std::array<std::uint8_t, 4> sshd{
            'S',
            'S',
            'h',
            'd'
        };

        for( std::size_t offset = 0u; offset + 6u <= bytes.size(); ++offset )
        {
            if( bytes[ offset ] != 0x00u || bytes[ offset + 1u ] != 0x00u || bytes[ offset + 2u ] != 0x01u ||
                bytes[ offset + 3u ] != 0xBDu )
            {
                continue;
            }

            const std::size_t packet_length = read_be16( bytes, offset + 4u );

            if( packet_length == 0u )
            {
                continue;
            }

            const std::size_t packet_end = offset + 6u + packet_length;

            if( packet_end > bytes.size() )
            {
                continue;
            }

            const std::optional<std::size_t> payload_offset = pes_payload_offset( bytes, offset, packet_end );

            if( !payload_offset.has_value() )
            {
                continue;
            }

            const std::size_t search_end = std::min<std::size_t>( packet_end, payload_offset.value() + 32u );

            const auto sshd_position = std::search( bytes.begin() + payload_offset.value(),
                bytes.begin() + search_end,
                sshd.begin(),
                sshd.end() );

            if( sshd_position == bytes.begin() + search_end )
            {
                continue;
            }

            const std::size_t data_offset = static_cast<std::size_t>( std::distance( bytes.begin(), sshd_position ) );

            for( std::size_t candidate = payload_offset.value(); candidate < data_offset; ++candidate )
            {
                const std::uint8_t substream = bytes[ candidate ];

                if( ( substream & 0xF8u ) != 0xA0u )
                {
                    continue;
                }

                return AdsPacketLayout{
                    .substream = substream,
                    .prefix_size = data_offset - payload_offset.value(),
                    .substream_offset = candidate - payload_offset.value()
                };
            }
        }

        return std::nullopt;
    }

    std::vector<std::uint8_t> extract_ps2_ads_audio( std::span<const std::uint8_t> bytes )
    {
        const std::optional<AdsPacketLayout> layout = find_ps2_ads_packet_layout( bytes );

        if( !layout.has_value() )
        {
            return {};
        }

        std::vector<std::uint8_t> audio;

        for( std::size_t offset = 0u; offset + 6u <= bytes.size(); ++offset )
        {
            if( bytes[ offset ] != 0x00u || bytes[ offset + 1u ] != 0x00u || bytes[ offset + 2u ] != 0x01u ||
                bytes[ offset + 3u ] != 0xBDu )
            {
                continue;
            }

            const std::size_t packet_length = read_be16( bytes, offset + 4u );

            if( packet_length == 0u )
            {
                continue;
            }

            const std::size_t packet_end = offset + 6u + packet_length;

            if( packet_end > bytes.size() )
            {
                continue;
            }

            const std::optional<std::size_t> payload_offset = pes_payload_offset( bytes, offset, packet_end );

            if( !payload_offset.has_value() || payload_offset.value() + layout->prefix_size > packet_end ||
                payload_offset.value() + layout->substream_offset >= packet_end )
            {
                continue;
            }

            if( bytes[ payload_offset.value() + layout->substream_offset ] != layout->substream )
            {
                continue;
            }

            const std::size_t data_begin = payload_offset.value() + layout->prefix_size;
            audio.insert( audio.end(), bytes.begin() + data_begin, bytes.begin() + packet_end );
            offset = packet_end - 1u;
        }

        if( audio.empty() )
        {
            return {};
        }

        constexpr std::array<std::uint8_t, 4> sshd{
            'S',
            'S',
            'h',
            'd'
        };

        const auto sshd_position = std::search( audio.begin(), audio.end(), sshd.begin(), sshd.end() );

        if( sshd_position == audio.end() )
        {
            return {};
        }

        if( sshd_position != audio.begin() )
        {
            audio.erase( audio.begin(), sshd_position );
        }

        if( audio.size() < 40u || audio[ 32u ] != 'S' || audio[ 33u ] != 'S' || audio[ 34u ] != 'b' ||
            audio[ 35u ] != 'd' )
        {
            return {};
        }

        const std::uint32_t type = read_le32( audio, 8u );
        const std::uint32_t sample_rate = read_le32( audio, 12u );
        const std::uint32_t channels = read_le32( audio, 16u );
        const std::uint32_t data_size = read_le32( audio, 36u );

        if( type > 2u || sample_rate == 0u || channels == 0u || channels > 8u )
        {
            return {};
        }

        const std::size_t expected_size = 40u + static_cast<std::size_t>( data_size );

        if( expected_size > audio.size() )
        {
            return {};
        }

        audio.resize( expected_size );
        return audio;
    }

    std::vector<std::filesystem::path> make_ffmpeg_arguments( const std::filesystem::path &input,
        const std::filesystem::path &audio_input,
        const std::filesystem::path &output,
        bool force_mpeg,
        bool use_h264 )
    {
        std::vector<std::filesystem::path> arguments{
            "-nostdin",
            "-y",
            "-hide_banner",
            "-loglevel",
            "error"
        };

        if( force_mpeg )
        {
            arguments.push_back( "-f" );
            arguments.push_back( "mpeg" );
        }

        arguments.push_back( "-i" );
        arguments.push_back( input );

        if( !audio_input.empty() )
        {
            arguments.push_back( "-f" );
            arguments.push_back( "ads" );
            arguments.push_back( "-i" );
            arguments.push_back( audio_input );
        }

        arguments.push_back( "-map" );
        arguments.push_back( "0:v:0" );
        arguments.push_back( "-map" );
        arguments.push_back( audio_input.empty() ? "0:a:0?" : "1:a:0" );
        arguments.push_back( "-c:v" );

        if( use_h264 )
        {
            arguments.push_back( "libx264" );
            arguments.push_back( "-preset" );
            arguments.push_back( "medium" );
            arguments.push_back( "-crf" );
            arguments.push_back( "18" );
        }
        else
        {
            arguments.push_back( "mpeg4" );
            arguments.push_back( "-q:v" );
            arguments.push_back( "2" );
        }

        arguments.push_back( "-pix_fmt" );
        arguments.push_back( "yuv420p" );
        arguments.push_back( "-c:a" );
        arguments.push_back( "aac" );
        arguments.push_back( "-b:a" );
        arguments.push_back( "192k" );
        arguments.push_back( "-ar" );
        arguments.push_back( "48000" );

        if( !audio_input.empty() )
        {
            arguments.push_back( "-af" );
            arguments.push_back( "apad" );
            arguments.push_back( "-shortest" );
        }

        arguments.push_back( "-movflags" );
        arguments.push_back( "+faststart" );
        arguments.push_back( output );
        return arguments;
    }

    bool try_convert( const std::filesystem::path &ffmpeg_executable,
        const std::filesystem::path &input,
        const std::filesystem::path &audio_input,
        const std::filesystem::path &output,
        bool force_mpeg,
        bool use_h264 )
    {
        std::error_code error;
        std::filesystem::remove( output, error );

        const std::int32_t result = process::run_process( ffmpeg_executable,
            make_ffmpeg_arguments( input, audio_input, output, force_mpeg, use_h264 ) );

        return result == 0 && std::filesystem::is_regular_file( output );
    }
}

namespace piko::convert
{
    void convert_pss_to_mp4( std::span<const std::uint8_t> bytes,
        const std::filesystem::path &output_path,
        const std::filesystem::path &ffmpeg_executable )
    {
        if( bytes.empty() )
        {
            throw std::runtime_error( "Cannot convert an empty PSS movie" );
        }

        if( output_path.has_parent_path() )
        {
            std::filesystem::create_directories( output_path.parent_path() );
        }

        const std::filesystem::path temp_path = pss_converter::make_temp_path( output_path.parent_path(), ".pss" );
        const std::filesystem::path audio_path = pss_converter::make_temp_path( output_path.parent_path(), ".ads" );
        pss_converter::write_temp_file( temp_path, bytes );

        const std::vector<std::uint8_t> audio = pss_converter::extract_ps2_ads_audio( bytes );
        constexpr std::array<std::uint8_t, 4> sshd{
            'S',
            'S',
            'h',
            'd'
        };

        const bool source_contains_ads =
            std::search( bytes.begin(), bytes.end(), sshd.begin(), sshd.end() ) != bytes.end();

        if( source_contains_ads && audio.empty() )
        {
            std::error_code cleanup_error;
            std::filesystem::remove( temp_path, cleanup_error );
            throw std::runtime_error(
                "PSS movie contains Sony ADS audio, but Piko could not extract it: " + output_path.string() );
        }

        if( !audio.empty() )
        {
            pss_converter::write_temp_file( audio_path, audio );
        }

        bool converted = pss_converter::try_convert( ffmpeg_executable,
            temp_path,
            audio.empty() ? std::filesystem::path{} : audio_path,
            output_path,
            false,
            true );

        if( !converted )
        {
            converted = pss_converter::try_convert( ffmpeg_executable,
                temp_path,
                audio.empty() ? std::filesystem::path{} : audio_path,
                output_path,
                true,
                true );
        }

        if( !converted )
        {
            converted = pss_converter::try_convert( ffmpeg_executable,
                temp_path,
                audio.empty() ? std::filesystem::path{} : audio_path,
                output_path,
                false,
                false );
        }

        if( !converted )
        {
            converted = pss_converter::try_convert( ffmpeg_executable,
                temp_path,
                audio.empty() ? std::filesystem::path{} : audio_path,
                output_path,
                true,
                false );
        }

        std::error_code error;
        std::filesystem::remove( temp_path, error );
        std::filesystem::remove( audio_path, error );

        if( !converted )
        {
            std::filesystem::remove( output_path, error );
            throw std::runtime_error( "FFmpeg failed to convert PSS movie: " + output_path.string() );
        }
    }
}
