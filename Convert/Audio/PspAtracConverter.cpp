#include "Audio/PspAtracConverter.hpp"

#include "Utility/JsonUtil.hpp"

#include "Audio/WavWriter.hpp"
#include "Utility/ProcessUtil.hpp"
#include "Utils/BinaryUtil.hpp"
#include "Utils/StrUtil.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace piko::convert::psp_atrac
{
    constexpr std::size_t stripe_size = 0x2800u;

    struct Pcm16Stereo
    {
        std::uint32_t sample_rate = 0;
        std::vector<std::int16_t> samples;

        [[nodiscard]] std::size_t frame_count() const
        {
            return samples.size() / 2u;
        }
    };

    std::uint16_t read_u16( std::span<const std::uint8_t> bytes, std::size_t offset )
    {
        if( offset + 2u > bytes.size() )
        {
            throw std::runtime_error( "Unexpected end of audio data" );
        }

        return piko::utils::read_le_u16( bytes.data() + offset );
    }

    std::uint32_t read_u32( std::span<const std::uint8_t> bytes, std::size_t offset )
    {
        if( offset + 4u > bytes.size() )
        {
            throw std::runtime_error( "Unexpected end of audio data" );
        }

        return piko::utils::read_le_u32( bytes.data() + offset );
    }

    bool has_tag( std::span<const std::uint8_t> bytes, std::size_t offset, std::string_view tag )
    {
        if( offset + tag.size() > bytes.size() )
        {
            return false;
        }

        return std::equal( tag.begin(), tag.end(), bytes.begin() + static_cast<std::ptrdiff_t>( offset ) );
    }

    bool command_available( const std::filesystem::path &executable )
    {
        return process::run_process( executable, { std::filesystem::path( "-version" ) }, { .quiet = true } ) == 0;
    }

    std::filesystem::path make_temp_directory( const std::filesystem::path &parent )
    {
        static std::atomic<std::uint64_t> counter{ 0 };
        const std::uint64_t stamp =
            static_cast<std::uint64_t>( std::chrono::high_resolution_clock::now().time_since_epoch().count() );

        const std::filesystem::path path =
            parent / ( "piko_psp_atrac_" + std::to_string( stamp ) + "_" + std::to_string( counter.fetch_add( 1 ) ) );

        std::filesystem::create_directories( path );
        return path;
    }

    void write_bytes( const std::filesystem::path &path, std::span<const std::uint8_t> bytes )
    {
        std::ofstream file( path, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not create temporary ATRAC file: " + path.string() );
        }

        file.write( reinterpret_cast<const char *>( bytes.data() ), static_cast<std::streamsize>( bytes.size() ) );

        if( !file )
        {
            throw std::runtime_error( "Could not write temporary ATRAC file: " + path.string() );
        }
    }

    std::vector<std::vector<std::uint8_t>> split_streams( std::span<const std::uint8_t> bytes )
    {
        std::size_t stream_count = 0;
        while( stream_count < 8u )
        {
            const std::size_t offset = stream_count * stripe_size;

            if( !has_tag( bytes, offset, "RIFF" ) || !has_tag( bytes, offset + 8u, "WAVE" ) )
            {
                break;
            }

            ++stream_count;
        }

        if( stream_count == 0u )
        {
            throw std::runtime_error( "PSP audio file has no striped RIFF streams" );
        }

        if( stream_count > 3u )
        {
            throw std::runtime_error( "PSP audio file contains more than three streams" );
        }

        std::vector<std::vector<std::uint8_t>> streams( stream_count );

        for( std::size_t stream_index = 0; stream_index < stream_count; ++stream_index )
        {
            const std::size_t header_offset = stream_index * stripe_size;
            const std::uint64_t expected_u64 = static_cast<std::uint64_t>( read_u32( bytes, header_offset + 4u ) ) + 8u;

            if( expected_u64 > std::numeric_limits<std::size_t>::max() )
            {
                throw std::runtime_error( "PSP RIFF stream is too large" );
            }

            const std::size_t expected = static_cast<std::size_t>( expected_u64 );
            streams[ stream_index ].reserve( expected );

            for( std::size_t offset = header_offset; offset < bytes.size() && streams[ stream_index ].size() < expected;
                offset += stripe_size * stream_count )
            {
                const std::size_t available = std::min( stripe_size, bytes.size() - offset );
                const std::size_t remaining = expected - streams[ stream_index ].size();
                const std::size_t count = std::min( available, remaining );
                streams[ stream_index ].insert( streams[ stream_index ].end(),
                    bytes.begin() + static_cast<std::ptrdiff_t>( offset ),
                    bytes.begin() + static_cast<std::ptrdiff_t>( offset + count ) );
            }

            if( streams[ stream_index ].size() != expected )
            {
                throw std::runtime_error( "PSP striped RIFF stream is truncated" );
            }

            if( read_u16( streams[ stream_index ], 20u ) != 0x0270u )
            {
                throw std::runtime_error( "PSP RIFF stream is not ATRAC3" );
            }
        }

        return streams;
    }

    EncodedAudioInfo read_pcm16_wav_info( const std::filesystem::path &path )
    {
        std::ifstream file( path, std::ios::binary | std::ios::ate );

        if( !file )
        {
            throw std::runtime_error( "Could not read decoded WAV: " + path.string() );
        }

        const std::streamsize size = file.tellg();

        if( size < 12 )
        {
            throw std::runtime_error( "Decoded WAV is too small: " + path.string() );
        }

        file.seekg( 0 );
        std::vector<std::uint8_t> bytes( static_cast<std::size_t>( size ) );
        file.read( reinterpret_cast<char *>( bytes.data() ), size );

        if( !file )
        {
            throw std::runtime_error( "Could not read decoded WAV: " + path.string() );
        }

        if( !has_tag( bytes, 0u, "RIFF" ) || !has_tag( bytes, 8u, "WAVE" ) )
        {
            throw std::runtime_error( "Decoded audio is not a WAV file" );
        }

        std::uint16_t format = 0u;
        std::uint16_t channels = 0u;
        std::uint16_t bits = 0u;
        std::uint16_t block_align = 0u;
        std::uint32_t sample_rate = 0u;
        std::uint32_t data_size = 0u;
        bool found_format = false;
        bool found_data = false;

        std::size_t offset = 12u;
        while( offset + 8u <= bytes.size() )
        {
            const std::uint32_t chunk_size = read_u32( bytes, offset + 4u );
            const std::size_t data_offset = offset + 8u;

            if( data_offset + chunk_size > bytes.size() )
            {
                throw std::runtime_error( "Decoded WAV contains a truncated chunk" );
            }

            if( has_tag( bytes, offset, "fmt " ) )
            {
                if( chunk_size < 16u )
                {
                    throw std::runtime_error( "Decoded WAV format chunk is too small" );
                }

                format = read_u16( bytes, data_offset );

                channels = read_u16( bytes, data_offset + 2u );

                sample_rate = read_u32( bytes, data_offset + 4u );

                block_align = read_u16( bytes, data_offset + 12u );

                bits = read_u16( bytes, data_offset + 14u );

                if( format == 0xfffeu && chunk_size >= 40u )
                {
                    format = read_u16( bytes, data_offset + 24u );
                }

                found_format = true;
            }
            else if( has_tag( bytes, offset, "data" ) && !found_data )
            {
                data_size = chunk_size;

                found_data = true;
            }

            offset = data_offset + chunk_size + ( chunk_size & 1u );
        }

        if( !found_format || !found_data || format != 1u || bits != 16u || channels == 0u || sample_rate == 0u ||
            block_align != channels * sizeof( std::int16_t ) || data_size % block_align != 0u )
        {
            throw std::runtime_error( "Decoded WAV has an unsupported PCM format" );
        }

        return EncodedAudioInfo{
            .sample_rate = sample_rate,
            .channel_count = channels,
            .frame_count = static_cast<std::int64_t>( data_size / block_align )
        };
    }

    Pcm16Stereo read_pcm16_stereo_wav( const std::filesystem::path &path )
    {
        std::ifstream file( path, std::ios::binary | std::ios::ate );

        if( !file )
        {
            throw std::runtime_error( "Could not read decoded WAV: " + path.string() );
        }

        const std::streamsize size = file.tellg();

        if( size < 12 )
        {
            throw std::runtime_error( "Decoded WAV is too small: " + path.string() );
        }

        file.seekg( 0 );
        std::vector<std::uint8_t> bytes( static_cast<std::size_t>( size ) );
        file.read( reinterpret_cast<char *>( bytes.data() ), size );

        if( !file )
        {
            throw std::runtime_error( "Could not read decoded WAV: " + path.string() );
        }

        if( !has_tag( bytes, 0u, "RIFF" ) || !has_tag( bytes, 8u, "WAVE" ) )
        {
            throw std::runtime_error( "Decoded audio is not a WAV file" );
        }

        std::uint16_t format = 0;
        std::uint16_t channels = 0;
        std::uint16_t bits = 0;
        std::uint32_t sample_rate = 0;
        std::span<const std::uint8_t> sample_bytes;

        std::size_t offset = 12u;
        while( offset + 8u <= bytes.size() )
        {
            const std::uint32_t chunk_size = read_u32( bytes, offset + 4u );
            const std::size_t data_offset = offset + 8u;

            if( data_offset + chunk_size > bytes.size() )
            {
                throw std::runtime_error( "Decoded WAV contains a truncated chunk" );
            }

            if( has_tag( bytes, offset, "fmt " ) )
            {
                if( chunk_size < 16u )
                {
                    throw std::runtime_error( "Decoded WAV format chunk is too small" );
                }

                format = read_u16( bytes, data_offset );

                channels = read_u16( bytes, data_offset + 2u );

                sample_rate = read_u32( bytes, data_offset + 4u );

                bits = read_u16( bytes, data_offset + 14u );

                if( format == 0xfffeu && chunk_size >= 40u )
                {
                    format = read_u16( bytes, data_offset + 24u );
                }
            }
            else if( has_tag( bytes, offset, "data" ) )
            {
                sample_bytes = std::span<const std::uint8_t>( bytes ).subspan( data_offset, chunk_size );
            }

            offset = data_offset + chunk_size + ( chunk_size & 1u );
        }

        if( format != 1u || channels != 2u || bits != 16u || sample_rate == 0u || sample_bytes.empty() )
        {
            throw std::runtime_error( "Decoded ATRAC WAV is not stereo PCM16" );
        }

        if( sample_bytes.size() % 4u != 0u )
        {
            throw std::runtime_error( "Decoded ATRAC WAV is not frame aligned" );
        }

        Pcm16Stereo result;
        result.sample_rate = sample_rate;
        result.samples.resize( sample_bytes.size() / 2u );

        for( std::size_t index = 0; index < result.samples.size(); ++index )
        {
            result.samples[ index ] = static_cast<std::int16_t>( read_u16( sample_bytes, index * 2u ) );
        }

        return result;
    }

    Pcm16Stereo decode_stream( std::span<const std::uint8_t> stream,
        const std::filesystem::path &ffmpeg_executable,
        const std::filesystem::path &temp_directory,
        std::size_t stream_index )
    {
        const std::filesystem::path input = temp_directory / ( "stream_" + std::to_string( stream_index ) + ".at3" );
        const std::filesystem::path output = temp_directory / ( "stream_" + std::to_string( stream_index ) + ".wav" );
        write_bytes( input, stream );

        const std::vector<std::filesystem::path>
            arguments{
            "-nostdin",
            "-v",
            "error",
            "-y",
            "-i",
            input,
            "-ar",
            "48000",
            "-c:a",
            "pcm_s16le",
            output
        };

        if( process::run_process( ffmpeg_executable, arguments ) != 0 )
        {
            throw std::runtime_error( "FFmpeg could not decode PSP ATRAC3 audio" );
        }

        return read_pcm16_stereo_wav( output );
    }

    std::string safe_name( std::string value, std::uint32_t index )
    {
        value = piko::utils::uppercase_ascii( std::move( value ) );

        for( char &character : value )
        {
            const unsigned char byte = static_cast<unsigned char>( character );

            if( ( byte >= 'A' && byte <= 'Z' ) || ( byte >= '0' && byte <= '9' ) || character == '_' ||
                character == '-' )
            {
                continue;
            }

            character = '_';
        }

        if( value.empty() )
        {
            value = "CUE_" + std::to_string( index );
        }

        return value;
    }

    std::vector<std::int16_t> combine_streams( const std::vector<Pcm16Stereo> &streams )
    {
        if( streams.empty() || streams.size() > 3u )
        {
            throw std::runtime_error( "Unsupported PSP ATRAC stream count" );
        }

        const std::uint32_t sample_rate = streams.front().sample_rate;
        std::size_t frames = 0;

        for( const Pcm16Stereo &stream : streams )
        {
            if( stream.sample_rate != sample_rate )
            {
                throw std::runtime_error( "Decoded PSP streams use different sample rates" );
            }

            frames = std::max( frames, stream.frame_count() );
        }

        const std::size_t channels = streams.size() * 2u;
        std::vector<std::int16_t> output( frames * channels, 0 );

        for( std::size_t frame = 0; frame < frames; ++frame )
        {
            const auto sample = [ & ]( std::size_t stream, std::size_t channel ) -> std::int16_t
            {
                if( stream >= streams.size() || frame >= streams[ stream ].frame_count() )
                {
                    return 0;
                }

                return streams[ stream ].samples[ frame * 2u + channel ];
            };

            if( streams.size() == 1u )
            {
                output[ frame * 2u ] = sample( 0u, 0u );

                output[ frame * 2u + 1u ] = sample( 0u, 1u );
            }
            else if( streams.size() == 2u )
            {
                output[ frame * 4u ] = sample( 0u, 0u );

                output[ frame * 4u + 1u ] = sample( 0u, 1u );

                output[ frame * 4u + 2u ] = sample( 1u, 0u );

                output[ frame * 4u + 3u ] = sample( 1u, 1u );
            }
            else
            {
                output[ frame * 6u ] = sample( 0u, 0u );

                output[ frame * 6u + 1u ] = sample( 0u, 1u );

                output[ frame * 6u + 2u ] = sample( 1u, 0u );

                output[ frame * 6u + 3u ] = sample( 2u, 0u );

                output[ frame * 6u + 4u ] = sample( 1u, 1u );

                output[ frame * 6u + 5u ] = sample( 2u, 1u );
            }
        }

        return output;
    }

    void write_manifest( const std::filesystem::path &path,
        const std::vector<ConvertedAudioCue> &cues,
        AudioCompression compression )
    {
        std::ofstream file( path, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not create PSP audio manifest: " + path.string() );
        }

        file << "{\n";
        file << "  \"format\": \"piko-audio\",\n";
        file << "  \"version\": 1,\n";
        file << "  \"layout\": \"interleaved-voices\",\n";
        file << "  \"cues\": [\n";

        for( std::size_t index = 0; index < cues.size(); ++index )
        {
            const ConvertedAudioCue &cue = cues[ index ];
            file << "    {\n";
            file << "      \"index\": " << cue.index << ",\n";
            file << "      \"name\": \"" << json_util::escape_string( cue.name, json_util::EscapeMode::Minimal )
                 << "\",\n";

            file << "      \"file\": \"" << json_util::escape_string( cue.file_name, json_util::EscapeMode::Minimal )
                 << "\",\n";

            file << "      \"codec\": \"" << audio_compression_codec( compression ) << "\",\n";
            file << "      \"sample_rate\": " << cue.sample_rate << ",\n";
            file << "      \"channels\": " << cue.channel_count << ",\n";
            file << "      \"frames\": " << cue.frame_count << ",\n";
            file << "      \"load_mode\": 0,\n";
            file << "      \"unknown_1\": 0,\n";
            file << "      \"unknown_2\": 0\n";
            file << "    }" << ( index + 1u == cues.size() ? "\n" : ",\n" );
        }

        file << "  ]\n";
        file << "}\n";

        if( !file )
        {
            throw std::runtime_error( "Could not write PSP audio manifest: " + path.string() );
        }
    }
}

namespace piko::convert
{
    std::filesystem::path find_ffmpeg_executable( const std::filesystem::path &application_root )
    {
#ifdef _WIN32
        const std::array<std::filesystem::path, 3> candidates{
            application_root / "ffmpeg" / "ffmpeg.exe",
            application_root / "ffmpeg.exe",
            std::filesystem::path( "ffmpeg.exe" )
        };
#else
        const std::array<std::filesystem::path, 3> candidates{
            application_root / "ffmpeg" / "ffmpeg",
            application_root / "ffmpeg",
            std::filesystem::path( "ffmpeg" )
        };
#endif

        for( const std::filesystem::path &candidate : candidates )
        {
            if( psp_atrac::command_available( candidate ) )
            {
                return candidate;
            }
        }

        throw std::runtime_error(
            "Game data import requires FFmpeg. Restore the ffmpeg folder from the Piko release download." );
    }

    const char *audio_compression_codec( AudioCompression compression )
    {
        return compression == AudioCompression::Opus ? "opus" : "flac";
    }

    const char *audio_compression_extension( AudioCompression compression )
    {
        return compression == AudioCompression::Opus ? ".opus" : ".flac";
    }

    EncodedAudioInfo encode_pcm16_audio( const std::filesystem::path &output_path,
        std::uint32_t sample_rate,
        std::uint16_t channel_count,
        std::span<const std::int16_t> interleaved_samples,
        AudioCompression compression,
        const std::filesystem::path &ffmpeg_executable )
    {
        if( sample_rate == 0u || channel_count == 0u )
        {
            throw std::runtime_error( "Cannot encode audio with an invalid format" );
        }

        if( interleaved_samples.size() % channel_count != 0u )
        {
            throw std::runtime_error( "Cannot encode audio with incomplete sample frames" );
        }

        const std::size_t frame_count = interleaved_samples.size() / channel_count;

        if( frame_count > static_cast<std::size_t>( std::numeric_limits<std::int64_t>::max() ) )
        {
            throw std::runtime_error( "Cannot encode an audio stream with too many frames" );
        }

        EncodedAudioInfo encoded{
            .sample_rate = sample_rate,
            .channel_count = channel_count,
            .frame_count = static_cast<std::int64_t>( frame_count )
        };

        const std::filesystem::path temp = psp_atrac::make_temp_directory( output_path.parent_path() );
        const std::filesystem::path input_wav = temp / "source.wav";

        try
        {
            std::filesystem::create_directories( output_path.parent_path() );
            const std::span<const std::uint8_t> sample_bytes(
                reinterpret_cast<const std::uint8_t *>( interleaved_samples.data() ),
                interleaved_samples.size_bytes() );

            write_interleaved_wav( input_wav,
                WavWriteOptions{
                    .sample_rate = sample_rate,
                    .channel_count = channel_count,
                    .bits_per_sample = 16u,
                    .format = WavFormatTag::Pcm,
                    .channel_mask = 0u,
                    .use_extensible = true
                },
                sample_bytes );

            std::vector<std::filesystem::path>
                arguments{
                "-y",
                "-hide_banner",
                "-loglevel",
                "error",
                "-i",
                input_wav,
                "-map_metadata",
                "-1",
                "-vn"
            };

            if( compression == AudioCompression::Opus )
            {
                const std::uint32_t bitrate_kbps = std::max<std::uint32_t>( 160u, channel_count * 64u );
                arguments.insert( arguments.end(),
                    { "-c:a",
                        "libopus",
                        "-application",
                        "audio",
                        "-vbr",
                        "on",
                        "-compression_level",
                        "10",
                        "-b:a",
                        std::to_string( bitrate_kbps ) + "k" } );

                if( channel_count > 2u )
                {
                    arguments.push_back( "-mapping_family" );
                    arguments.push_back( "255" );
                }
            }
            else
            {
                arguments.insert( arguments.end(), { "-c:a", "flac", "-compression_level", "8" } );
            }

            arguments.push_back( output_path );
            std::filesystem::remove( output_path );

            if( process::run_process( ffmpeg_executable, arguments ) != 0 ||
                !std::filesystem::is_regular_file( output_path ) )
            {
                throw std::runtime_error( std::string( "FFmpeg failed to encode " ) +
                                          audio_compression_codec( compression ) + " audio: " + output_path.string() );
            }

            if( compression == AudioCompression::Opus )
            {
                const std::filesystem::path verification_wav = temp / "verification.wav";
                decode_audio_to_wav( output_path, verification_wav, ffmpeg_executable );
                encoded = psp_atrac::read_pcm16_wav_info( verification_wav );
            }
        }
        catch( ... )
        {
            std::filesystem::remove_all( temp );
            throw;
        }

        std::filesystem::remove_all( temp );
        return encoded;
    }

    void decode_audio_to_wav( const std::filesystem::path &input_path,
        const std::filesystem::path &output_path,
        const std::filesystem::path &ffmpeg_executable )
    {
        std::filesystem::create_directories( output_path.parent_path() );
        std::filesystem::remove( output_path );
        const std::vector<std::filesystem::path> arguments{
            "-y",
            "-hide_banner",
            "-loglevel",
            "error",
            "-i",
            input_path,
            "-map_metadata",
            "-1",
            "-vn",
            "-c:a",
            "pcm_s16le",
            output_path
        };

        if( process::run_process( ffmpeg_executable, arguments, { .quiet = true } ) != 0 ||
            !std::filesystem::is_regular_file( output_path ) )
        {
            throw std::runtime_error( "FFmpeg failed to decode runtime audio: " + input_path.string() );
        }
    }

    std::vector<ConvertedAudioCue> convert_psp_atrac_audio( const std::vector<PspAudioSource> &sources,
        const std::filesystem::path &output_directory,
        AudioCompression compression,
        const std::filesystem::path &ffmpeg_executable )
    {
        std::filesystem::create_directories( output_directory );
        std::vector<ConvertedAudioCue> cues;
        cues.reserve( sources.size() );

        for( std::size_t cue_index = 0; cue_index < sources.size(); ++cue_index )
        {
            const PspAudioSource &source = sources[ cue_index ];
            std::cout << "converting PSP audio " << source.name << " (" << cue_index + 1u << "/" << sources.size()
                      << ")\n";

            const std::vector<std::vector<std::uint8_t>> split = psp_atrac::split_streams( source.bytes );
            const std::filesystem::path temp = psp_atrac::make_temp_directory( output_directory );

            try
            {
                std::vector<std::future<psp_atrac::Pcm16Stereo>> decode_jobs;
                decode_jobs.reserve( split.size() );

                for( std::size_t stream_index = 0; stream_index < split.size(); ++stream_index )
                {
                    decode_jobs.push_back( std::async( std::launch::async,
                        [ &, stream_index ]()
                        {
                            return psp_atrac::decode_stream( split[ stream_index ],
                                ffmpeg_executable,
                                temp,
                                stream_index );
                        } ) );
                }

                std::vector<psp_atrac::Pcm16Stereo> decoded;
                decoded.reserve( split.size() );

                for( auto &job : decode_jobs )
                {
                    decoded.push_back( job.get() );
                }

                const std::vector<std::int16_t> samples = psp_atrac::combine_streams( decoded );
                const std::uint16_t channel_count = static_cast<std::uint16_t>( split.size() * 2u );
                const std::string stem = psp_atrac::safe_name( source.name, static_cast<std::uint32_t>( cue_index ) );
                const std::string file_name = stem + audio_compression_extension( compression );
                const EncodedAudioInfo encoded = encode_pcm16_audio( output_directory / file_name,
                    48000u,
                    channel_count,
                    samples,
                    compression,
                    ffmpeg_executable );

                cues.push_back( ConvertedAudioCue{
                    .index = static_cast<std::uint32_t>( cue_index ),
                    .name = stem,
                    .file_name = file_name,
                    .sample_rate = encoded.sample_rate,
                    .channel_count = encoded.channel_count,
                    .frame_count = encoded.frame_count,
                    .load_mode = 0u,
                    .unknown_1 = 0u,
                    .unknown_2 = 0u
                } );
            }
            catch( ... )
            {
                std::filesystem::remove_all( temp );
                throw;
            }

            std::filesystem::remove_all( temp );
        }

        psp_atrac::write_manifest( output_directory / "audio.json", cues, compression );
        return cues;
    }
}
