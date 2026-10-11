#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

namespace piko::convert
{
    void convert_chc_to_json( std::span<const std::uint8_t> bytes, const std::filesystem::path &output_path );
}
