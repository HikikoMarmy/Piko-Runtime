#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace piko::convert
{
    struct GmoConversionResult
    {
        std::string model_name;
        std::filesystem::path gltf_path;
        std::size_t node_count = 0;
        std::size_t primitive_count = 0;
        std::size_t animation_count = 0;
        std::vector<std::string> warnings;
    };

    GmoConversionResult convert_gmo_to_gltf( std::span<const std::uint8_t> bytes,
        const std::filesystem::path &output_directory );

    std::vector<GmoConversionResult> convert_gmo_pak_to_gltf( std::span<const std::uint8_t> bytes,
        const std::filesystem::path &output_directory,
        std::span<const std::uint32_t> selected_entries = {},
        std::span<const std::string> entry_aliases = {} );
}
