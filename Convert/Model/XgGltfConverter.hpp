#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace piko::convert
{
    struct XgConversionResult
    {
        std::vector<std::string> converted_models;
        std::vector<std::string> warnings;
    };

    XgConversionResult convert_xgm_models_to_gltf( std::span<const std::uint8_t> xgm_bytes,
        const std::filesystem::path &output_root );
}
