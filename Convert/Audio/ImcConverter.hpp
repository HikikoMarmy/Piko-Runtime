#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace piko::convert
{
    enum class AudioCompression
    {
        Flac = 0,
        Opus = 1
    };

    struct ConvertedAudioCue
    {
        std::uint32_t index = 0;
        std::string name;
        std::string file_name;
        std::uint32_t sample_rate = 0;
        std::uint32_t channel_count = 0;
        std::int64_t frame_count = 0;
        std::int64_t loop_start_frame = -1;
        std::int64_t loop_end_frame = -1;
        std::uint32_t load_mode = 0;
        std::uint32_t unknown_1 = 0;
        std::uint32_t unknown_2 = 0;
    };

    struct FailedAudioCue
    {
        std::uint32_t index = 0;
        std::string name;
        std::string error;
    };

    struct ImcAudioConversionResult
    {
        std::vector<ConvertedAudioCue> cues;
        std::vector<FailedAudioCue> failures;
    };

    ImcAudioConversionResult convert_imc_audio( std::span<const std::uint8_t> bytes,
        const std::filesystem::path &output_directory,
        AudioCompression compression,
        const std::filesystem::path &ffmpeg_executable );
}
