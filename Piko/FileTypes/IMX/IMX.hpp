#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace piko::filetypes::imx
{
    enum class PixelStorageMode
    {
        Indexed4,
        Indexed8,
        Rgb888,
        Rgba8888
    };

    struct PaletteEntry
    {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        std::uint8_t a = 0;
    };

    struct Image
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        std::uint32_t mode_value_1 = 0;
        std::uint32_t mode_value_2 = 0;

        PixelStorageMode storage_mode = PixelStorageMode::Rgba8888;

        std::vector<PaletteEntry> palette;

        std::vector<std::uint8_t> rgba;

        std::uint32_t indexed_palette_trailer = 0;
        std::uint32_t footer_value_0 = 0;
        std::uint32_t footer_value_1 = 0;
    };

    class DecodeError final : public std::runtime_error
    {
    public:
        explicit DecodeError( const std::string &message );
    };

    std::uint8_t alpha80_to_alpha255( std::uint8_t alpha80 );

    PixelStorageMode storage_mode_from_values( std::uint32_t value_1, std::uint32_t value_2 );

    Image decode( std::span<const std::uint8_t> bytes );
    Image load_file( const std::filesystem::path &path );
}
