#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

namespace piko::convert
{
    void convert_pss_to_mp4( std::span<const std::uint8_t> bytes,
        const std::filesystem::path &output_path,
        const std::filesystem::path &ffmpeg_executable );
}
