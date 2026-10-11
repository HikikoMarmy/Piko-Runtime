#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace piko::filetypes::albumdef
{
    struct AlbumPage
    {
        std::int32_t id = 0;
        std::string module_type;
        std::vector<std::string> args;
        std::unordered_map<std::int32_t, std::int32_t> returns;
    };

    struct AlbumDef
    {
        std::unordered_map<std::int32_t, AlbumPage> pages;
    };

    [[nodiscard]] bool is_playable_module( std::string_view module );
    [[nodiscard]] AlbumDef parse_text( std::string_view text );
    [[nodiscard]] AlbumDef load_file( const std::filesystem::path &path );
    [[nodiscard]] AlbumDef load_json( const std::filesystem::path &path );
    void save_json( const AlbumDef &album, const std::filesystem::path &path );
}
