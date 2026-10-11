#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

namespace piko::convert
{
    enum class WavFormatTag : std::uint16_t
    {
        Pcm = 1,
        IeeeFloat = 3
    };

    struct WavWriteOptions
    {
        std::uint32_t sample_rate = 48000;
        std::uint16_t channel_count = 2;
        std::uint16_t bits_per_sample = 16;
        WavFormatTag format = WavFormatTag::Pcm;
        std::uint32_t channel_mask = 0;
        bool use_extensible = true;
    };

    void write_interleaved_wav( const std::filesystem::path &path,
        const WavWriteOptions &options,
        std::span<const std::uint8_t> interleaved_sample_bytes );
}
