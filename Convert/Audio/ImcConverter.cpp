#include "Audio/ImcConverter.hpp"

#include "Utility/JsonUtil.hpp"

#include "Audio/PspAtracConverter.hpp"
#include "FileTypes/IMC/IMC.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <unordered_set>

namespace piko::convert::imc_convert
{
    std::string safe_file_stem( std::string value, std::uint32_t index )
    {
        for( char &character : value )
        {
            const unsigned char byte = static_cast<unsigned char>( character );

            if( std::isalnum( byte ) )
            {
                character = static_cast<char>( std::toupper( byte ) );
            }
            else if( character != '_' && character != '-' )
            {
                character = '_';
            }
        }

        if( value.empty() )
        {
            value = "CUE_" + std::to_string( index );
        }

        return value;
    }

    void write_manifest( const std::filesystem::path &path,
        const std::vector<ConvertedAudioCue> &cues,
        const std::vector<FailedAudioCue> &failures,
        AudioCompression compression )
    {
        const std::filesystem::path temporary_path = path.string() + ".tmp";
        std::ofstream file( temporary_path, std::ios::binary | std::ios::trunc );

        if( !file )
        {
            throw std::runtime_error( "Could not create audio manifest: " + temporary_path.string() );
        }

        file << "{\n";
        file << "  \"format\": \"piko-audio\",\n";
        file << "  \"version\": 1,\n";
        file << "  \"layout\": \"interleaved-voices\",\n";
        file << "  \"cues\": [\n";

        for( std::size_t cue_index = 0; cue_index < cues.size(); ++cue_index )
        {
            const ConvertedAudioCue &cue = cues[ cue_index ];
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
            file << "      \"loop_start_frame\": " << cue.loop_start_frame << ",\n";
            file << "      \"loop_end_frame\": " << cue.loop_end_frame << ",\n";
            file << "      \"load_mode\": " << cue.load_mode << ",\n";
            file << "      \"unknown_1\": " << cue.unknown_1 << ",\n";
            file << "      \"unknown_2\": " << cue.unknown_2 << "\n";
            file << "    }" << ( cue_index + 1u == cues.size() ? "\n" : ",\n" );
        }

        file << "  ],\n";
        file << "  \"failed_cues\": [\n";

        for( std::size_t failure_index = 0; failure_index < failures.size(); ++failure_index )
        {
            const FailedAudioCue &failure = failures[ failure_index ];
            file << "    {\n";
            file << "      \"index\": " << failure.index << ",\n";
            file << "      \"name\": \"" << json_util::escape_string( failure.name, json_util::EscapeMode::Minimal )
                 << "\",\n";

            file << "      \"error\": \"" << json_util::escape_string( failure.error, json_util::EscapeMode::Minimal )
                 << "\"\n";

            file << "    }" << ( failure_index + 1u == failures.size() ? "\n" : ",\n" );
        }

        file << "  ]\n";
        file << "}\n";
        file.close();

        if( !file )
        {
            std::error_code remove_error{};
            std::filesystem::remove( temporary_path, remove_error );
            throw std::runtime_error( "Could not write audio manifest: " + temporary_path.string() );
        }

        std::error_code remove_error{};
        std::filesystem::remove( path, remove_error );
        std::error_code rename_error{};
        std::filesystem::rename( temporary_path, path, rename_error );

        if( rename_error )
        {
            std::filesystem::remove( temporary_path, remove_error );
            throw std::runtime_error(
                "Could not finalize audio manifest: " + path.string() + ": " + rename_error.message() );
        }
    }
}

namespace piko::convert
{
    ImcAudioConversionResult convert_imc_audio( std::span<const std::uint8_t> bytes,
        const std::filesystem::path &output_directory,
        AudioCompression compression,
        const std::filesystem::path &ffmpeg_executable )
    {
        const filetypes::imc::Container container = filetypes::imc::decode( bytes );
        ImcAudioConversionResult result;
        result.cues.reserve( container.subsongs.size() );
        result.failures.reserve( container.subsongs.size() );
        std::unordered_set<std::string> used_file_names;

        std::filesystem::remove_all( output_directory );
        std::filesystem::create_directories( output_directory );

        for( const filetypes::imc::Subsong &subsong : container.subsongs )
        {
            const std::uint32_t cue_index = static_cast<std::uint32_t>( subsong.table_entry.index );
            const std::string cue_name = subsong.table_entry.name;
            std::string stem = imc_convert::safe_file_stem( cue_name, cue_index );
            const std::string extension = audio_compression_extension( compression );
            std::string file_name = stem + extension;

            for( std::uint32_t suffix = 1; used_file_names.contains( file_name ); ++suffix )
            {
                file_name = stem + "_" + std::to_string( suffix ) + extension;
            }

            used_file_names.insert( file_name );
            const std::filesystem::path output_path = output_directory / file_name;

            try
            {
                const filetypes::imc::Pcm16 pcm = filetypes::imc::decode_subsong_to_pcm16( subsong.raw_subsong_data );
                const filetypes::imc::LoopPoints loop_points =
                    filetypes::imc::find_loop_points( subsong.raw_subsong_data );

                const EncodedAudioInfo encoded = encode_pcm16_audio( output_path,
                    static_cast<std::uint32_t>( pcm.sample_rate ),
                    static_cast<std::uint16_t>( pcm.channels ),
                    pcm.interleaved,
                    compression,
                    ffmpeg_executable );

                result.cues.push_back( ConvertedAudioCue{
                    .index = cue_index,
                    .name = cue_name,
                    .file_name = file_name,
                    .sample_rate = encoded.sample_rate,
                    .channel_count = encoded.channel_count,
                    .frame_count = encoded.frame_count,
                    .loop_start_frame = loop_points.start_frame,
                    .loop_end_frame = loop_points.end_frame,
                    .load_mode = static_cast<std::uint32_t>( subsong.table_entry.load_mode ),
                    .unknown_1 = subsong.table_entry.unknown_1,
                    .unknown_2 = subsong.table_entry.unknown_2
                } );
            }
            catch( const std::exception &error )
            {
                std::error_code remove_error{};
                std::filesystem::remove( output_path, remove_error );
                result.failures.push_back(
                    FailedAudioCue{ .index = cue_index, .name = cue_name, .error = error.what() } );
            }
            catch( ... )
            {
                std::error_code remove_error{};
                std::filesystem::remove( output_path, remove_error );
                result.failures.push_back(
                    FailedAudioCue{ .index = cue_index, .name = cue_name, .error = "Unknown audio conversion error" } );
            }
        }

        if( result.cues.empty() )
        {
            const std::string reason =
                result.failures.empty() ? "IMC contained no audio cues" : result.failures.front().error;

            throw std::runtime_error( "IMC conversion produced no playable cues: " + reason );
        }

        imc_convert::write_manifest( output_directory / "audio.json", result.cues, result.failures, compression );
        return result;
    }
}
