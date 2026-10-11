#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace piko::utils
{
    [[nodiscard]] std::filesystem::path path_from_utf8( std::string_view value );
    [[nodiscard]] std::string path_to_utf8( const std::filesystem::path &value );
    bool path_exists( const std::filesystem::path &path );
    bool regular_file( const std::filesystem::path &path );
    bool directory( const std::filesystem::path &path );
    [[nodiscard]] std::filesystem::path find_resource_file( const std::filesystem::path &application_root,
        const std::filesystem::path &resource_relative );
}
