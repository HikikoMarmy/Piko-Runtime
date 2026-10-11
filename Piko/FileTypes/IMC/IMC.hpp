#pragma once

#include "Runtime/Audio/Pcm16.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace piko::filetypes::imc
{
    enum class LoadMode : std::uint32_t
    {
        Stream = 0,
        Entire = 2
    };

    struct TableEntry
    {
        std::int32_t index = 0;

        std::string name;
        std::uint32_t offset = 0;
        std::uint32_t unknown_1 = 0;
        std::uint32_t unknown_2 = 0;
        LoadMode load_mode = LoadMode::Stream;
    };

    struct SubsongHeader
    {
        std::uint32_t channels = 0;
        std::uint32_t sample_rate = 0;
        std::uint32_t frames_per_block = 0;
        std::uint32_t total_blocks = 0;
    };

    struct LoopPoints
    {
        std::int64_t start_frame = -1;
        std::int64_t end_frame = -1;
    };

    struct Subsong
    {
        TableEntry table_entry{};
        std::vector<std::uint8_t> raw_subsong_data;
    };

    struct Container
    {
        std::vector<Subsong> subsongs;

        [[nodiscard]] const Subsong *find_subsong( std::string_view name ) const;
    };

    using Pcm16 = runtime::audio::Pcm16;

    Container decode( std::span<const std::uint8_t> bytes );
    Container load_file( const std::filesystem::path &path );

    SubsongHeader parse_subsong_header( std::span<const std::uint8_t> raw_subsong );
    LoopPoints find_loop_points( std::span<const std::uint8_t> raw_subsong );
    Pcm16 decode_subsong_to_pcm16( std::span<const std::uint8_t> raw_subsong );
    Pcm16 ensure_stereo( const Pcm16 &input );
}
