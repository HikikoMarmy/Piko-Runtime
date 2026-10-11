#pragma once

#include "FileTypes/IMX/IMX.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace piko::filetypes::xgm
{
    struct TextureMetadata
    {
        std::string original_path;
        std::string filename;

        std::uint32_t index = 0;
        std::uint32_t size = 0;
        std::uint32_t cumulative_size_before = 0;
        std::uint32_t usage_hint = 0;
        std::uint32_t unknown_120 = 0;

        std::size_t entry_offset = 0;
        std::size_t data_offset = 0;
    };

    struct AnimationEntry
    {
        float playback_length = 0.0f;
        float keyframe_interval = 0.0f;
        float start_keyframe = 0.0f;

        std::uint32_t speed_mode = 0;
    };

    struct ModelMetadata
    {
        std::string original_path;
        std::string filename;

        std::uint32_t index = 0;
        std::uint32_t size = 0;
        std::uint32_t animation_count = 0;
        std::uint32_t unknown_11C = 0;

        std::size_t entry_offset = 0;
        std::size_t data_offset = 0;

        std::vector<AnimationEntry> animations;
    };

    struct Metadata
    {
        std::uint32_t texture_count = 0;
        std::uint32_t model_count = 0;

        std::vector<TextureMetadata> textures;
        std::vector<ModelMetadata> models;
    };

    struct Texture
    {
        TextureMetadata metadata;
        imx::Image image;
    };

    class DecodeError final : public std::runtime_error
    {
    public:
        explicit DecodeError( const std::string &message );
    };

    std::vector<std::uint8_t> read_file_bytes( const std::filesystem::path &path );

    Metadata decode_metadata( std::span<const std::uint8_t> bytes );
    Texture decode_texture( std::span<const std::uint8_t> bytes, std::size_t texture_index );
    std::vector<std::uint8_t> decode_texture_bytes( std::span<const std::uint8_t> bytes, std::size_t texture_index );

    Metadata load_metadata( const std::filesystem::path &path );
    Texture load_texture( const std::filesystem::path &path, std::size_t texture_index );
    std::vector<std::uint8_t> load_texture_bytes( const std::filesystem::path &path, std::size_t texture_index );

    class Package
    {
    public:
        Package() = default;
        explicit Package( std::filesystem::path path );

        bool load( const std::filesystem::path &path );
        void clear();

        [[nodiscard]] bool loaded() const;
        [[nodiscard]] const std::filesystem::path &path() const;
        [[nodiscard]] const Metadata &metadata() const;

        [[nodiscard]] std::size_t texture_count() const;
        [[nodiscard]] std::size_t model_count() const;

        [[nodiscard]] bool has_texture( std::string_view texture_name ) const;
        [[nodiscard]] std::optional<std::size_t> find_texture_index( std::string_view texture_name ) const;

        [[nodiscard]] const TextureMetadata *texture_metadata_by_index( std::size_t texture_index ) const;
        [[nodiscard]] const TextureMetadata *texture_metadata_by_name( std::string_view texture_name ) const;

        [[nodiscard]] Texture load_texture_by_index( std::size_t texture_index ) const;
        [[nodiscard]] Texture load_texture( std::string_view texture_name ) const;

        [[nodiscard]] std::vector<std::uint8_t> load_texture_bytes_by_index( std::size_t texture_index ) const;
        [[nodiscard]] std::vector<std::uint8_t> load_texture_bytes( std::string_view texture_name ) const;

        [[nodiscard]] std::vector<std::string> texture_names() const;

    private:
        static std::string normalize_texture_key( std::string_view texture_name );
        void rebuild_texture_lookup();

        bool loaded_ = false;
        std::filesystem::path path_{};
        Metadata metadata_{};
        std::unordered_map<std::string, std::size_t> texture_by_name_;
    };
}
