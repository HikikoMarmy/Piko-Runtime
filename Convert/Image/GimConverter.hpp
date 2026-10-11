#pragma once

#include "Image/ImageConverter.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace piko::convert
{
    ConvertedTexture convert_gim_to_png( std::span<const std::uint8_t> bytes,
        const std::filesystem::path &output_path,
        std::string source_name,
        const std::filesystem::path &centre_directory = {} );
}
