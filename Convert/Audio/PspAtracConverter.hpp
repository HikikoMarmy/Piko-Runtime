#pragma once

#include "Audio/ImcConverter.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace piko::convert
{
    struct PspAudioSource
    {
        std::string name;
        std::vector<std::uint8_t> bytes;
    };

    [[nodiscard]] std::filesystem::path find_ffmpeg_executable( const std::filesystem::path &application_root );

    [[nodiscard]] const char *audio_compression_codec( AudioCompression compression );
    [[nodiscard]] const char *audio_compression_extension( AudioCompression compression );

    struct EncodedAudioInfo
    {
        std::uint32_t sample_rate = 0;
        std::uint16_t channel_count = 0;
        std::int64_t frame_count = 0;
    };

    [[nodiscard]] EncodedAudioInfo encode_pcm16_audio( const std::filesystem::path &output_path,
        std::uint32_t sample_rate,
        std::uint16_t channel_count,
        std::span<const std::int16_t> interleaved_samples,
        AudioCompression compression,
        const std::filesystem::path &ffmpeg_executable );

    void decode_audio_to_wav( const std::filesystem::path &input_path,
        const std::filesystem::path &output_path,
        const std::filesystem::path &ffmpeg_executable );

    std::vector<ConvertedAudioCue> convert_psp_atrac_audio( const std::vector<PspAudioSource> &sources,
        const std::filesystem::path &output_directory,
        AudioCompression compression,
        const std::filesystem::path &ffmpeg_executable );
}
