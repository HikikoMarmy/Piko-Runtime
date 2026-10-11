#include "Utility/PathNormalize.hpp"
#include "Utils/StrUtil.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace piko::convert
{
    std::string normalize_virtual_path( std::string_view path )
    {
        std::string value = piko::utils::normalize_slashes( std::string( path ) );

        std::vector<std::string> parts;
        std::stringstream stream( value );
        std::string part;

        while( std::getline( stream, part, '/' ) )
        {
            if( part.empty() || part == "." )
            {
                continue;
            }

            const std::size_t semicolon = part.find( ';' );

            if( semicolon != std::string::npos )
            {
                part.erase( semicolon );
            }

            while( !part.empty() && part.back() == '.' )
            {
                part.pop_back();
            }

            if( part.empty() || part == "." )
            {
                continue;
            }

            if( part == ".." )
            {
                if( !parts.empty() )
                {
                    parts.pop_back();
                }
                continue;
            }

            parts.push_back( piko::utils::uppercase_ascii( part ) );
        }

        std::string result;

        for( std::size_t i = 0; i < parts.size(); ++i )
        {
            if( i != 0 )
            {
                result.push_back( '/' );
            }

            result += parts[ i ];
        }

        return result;
    }

    std::vector<std::string> split_virtual_path( std::string_view path )
    {
        std::vector<std::string> parts;
        std::stringstream stream{ std::string( path ) };
        std::string part;

        while( std::getline( stream, part, '/' ) )
        {
            if( !part.empty() )
            {
                parts.push_back( std::move( part ) );
            }
        }

        return parts;
    }

    std::string virtual_parent_path( std::string_view normalized_path )
    {
        const std::string path( normalized_path );
        const std::size_t slash = path.find_last_of( '/' );

        if( slash == std::string::npos )
        {
            return {};
        }

        return path.substr( 0, slash );
    }

    std::string virtual_filename( std::string_view normalized_path )
    {
        const std::string path( normalized_path );
        const std::size_t slash = path.find_last_of( '/' );

        if( slash == std::string::npos )
        {
            return path;
        }

        return path.substr( slash + 1 );
    }

    bool virtual_path_starts_with( std::string_view normalized_path, std::string_view normalized_prefix )
    {
        const std::string path( normalized_path );
        const std::string prefix( normalized_prefix );

        if( prefix.empty() )
        {
            return true;
        }

        if( path == prefix )
        {
            return true;
        }

        if( path.size() <= prefix.size() )
        {
            return false;
        }

        return path.compare( 0, prefix.size(), prefix ) == 0 && path[ prefix.size() ] == '/';
    }
}
