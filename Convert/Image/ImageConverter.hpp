#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace piko::filetypes::imx
{
    struct Image;
}

namespace piko::convert
{
    struct ImageProcessingOptions
    {
        bool smoothing = true;
        bool alpha_cleanup = false;
        float strength_scale = 1.0f;
        float threshold_scale = 1.0f;
        std::uint8_t transparent_cutoff = 15;
        std::uint8_t opaque_cutoff = 252;
    };

    [[nodiscard]] std::vector<std::uint8_t> process_texture_rgba( const filetypes::imx::Image &image,
        const ImageProcessingOptions &options = {} );

    struct ConvertedTexture
    {
        std::string source_name;
        std::string file_name;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::string storage_mode;
        std::uint32_t source_index = 0;
        std::uint32_t usage_hint = 0;
    };

    void export_battle_sprites( std::span<const std::uint8_t> rgba,
        std::uint32_t width,
        std::uint32_t height,
        const std::filesystem::path &atlas_path,
        const std::filesystem::path &centre_directory );

    ConvertedTexture convert_imx_to_png( std::span<const std::uint8_t> bytes,
        const std::filesystem::path &output_path,
        std::string source_name,
        const std::filesystem::path &centre_directory = {} );

    std::vector<ConvertedTexture> convert_xgm_textures_to_png( std::span<const std::uint8_t> bytes,
        const std::filesystem::path &output_directory,
        const std::filesystem::path &centre_directory = {} );
}
