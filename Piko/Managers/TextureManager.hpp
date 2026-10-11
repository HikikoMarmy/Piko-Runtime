#pragma once

#include "Rendering/RenderTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace piko::rendering
{
    class Renderer;
}

namespace piko::managers
{
    struct TextureHandle
    {
        std::uint32_t id = 0;

        [[nodiscard]] bool valid() const;
        explicit operator bool() const;
    };

    struct TextureInfo
    {
        TextureHandle handle{};
        std::string name;
        std::filesystem::path path;
        std::int32_t width = 0;
        std::int32_t height = 0;
        std::uint32_t mode_value_1 = 0;
        std::uint32_t mode_value_2 = 0;
        std::uint8_t alpha_min = 255;
        std::uint8_t alpha_max = 255;
        std::uint64_t pixel_count = 0;
        std::uint64_t alpha_zero_pixels = 0;
        std::uint64_t alpha_full_pixels = 0;
        std::uint64_t alpha_partial_pixels = 0;
    };

    class TextureManager
    {
    public:
        TextureManager() = default;
        ~TextureManager();

        TextureManager( const TextureManager & ) = delete;
        TextureManager &operator=( const TextureManager & ) = delete;

        bool initialize( rendering::Renderer &renderer );
        void shutdown();

        [[nodiscard]] bool initialized() const;

        TextureHandle create_rgba( std::string_view name, const void *pixels, std::int32_t width, std::int32_t height );

        TextureHandle load_png( std::string_view name, const std::filesystem::path &path );

        [[nodiscard]] TextureHandle find( std::string_view name ) const;

        [[nodiscard]] const rendering::Texture *get( TextureHandle handle ) const;
        [[nodiscard]] const rendering::Texture *get( std::string_view name ) const;

        [[nodiscard]] const TextureInfo *info( TextureHandle handle ) const;
        [[nodiscard]] const TextureInfo *info( std::string_view name ) const;

        [[nodiscard]] bool contains( std::string_view name ) const;

        void unload( TextureHandle handle );
        void unload( std::string_view name );
        void clear();

    private:
        struct Entry
        {
            TextureInfo info{};
            rendering::Texture texture{};
        };

        static std::string normalize_name( std::string_view name );
        static std::string default_name_for_path( const std::filesystem::path &path );

        TextureHandle make_handle_for_index( std::size_t index ) const;
        Entry *entry_for( TextureHandle handle );
        const Entry *entry_for( TextureHandle handle ) const;

        TextureHandle store_texture( std::string_view name,
            const std::filesystem::path &path,
            rendering::Texture &&texture );

    private:
        rendering::Renderer *renderer_ = nullptr;
        bool initialized_ = false;

        std::vector<Entry> entries_;
        std::unordered_map<std::string, TextureHandle> by_name_;
    };
}
