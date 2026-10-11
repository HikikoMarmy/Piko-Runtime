#include "Utils/FileUtil.hpp"

#include <system_error>

namespace piko::utils
{
    std::filesystem::path path_from_utf8( std::string_view value )
    {
        return std::filesystem::path( std::u8string( value.begin(), value.end() ) );
    }

    std::string path_to_utf8( const std::filesystem::path &value )
    {
        const auto utf8 = value.generic_u8string();
        return std::string( utf8.begin(), utf8.end() );
    }

    bool path_exists( const std::filesystem::path &path )
    {
        if( path.empty() )
        {
            return false;
        }

        std::error_code error;
        return std::filesystem::exists( path, error );
    }

    bool regular_file( const std::filesystem::path &path )
    {
        if( path.empty() )
        {
            return false;
        }

        std::error_code error;
        return std::filesystem::is_regular_file( path, error );
    }

    bool directory( const std::filesystem::path &path )
    {
        if( path.empty() )
        {
            return false;
        }

        std::error_code error;
        return std::filesystem::is_directory( path, error );
    }

    std::filesystem::path find_resource_file( const std::filesystem::path &application_root,
        const std::filesystem::path &resource_relative )
    {
        if( application_root.empty() || resource_relative.empty() || resource_relative.is_absolute() )
        {
            return {};
        }

        const auto path = application_root / "GAME_DATA/PIKO" / resource_relative;
        return regular_file( path ) ? path.lexically_normal() : std::filesystem::path{};
    }

}
