#include "Utils/StrUtil.hpp"

#include <algorithm>
#include <utility>

namespace piko::utils
{
    std::string lowercase_ascii( std::string value )
    {
        for( char &character : value )
        {
            if( character >= 'A' && character <= 'Z' )
            {
                character = static_cast<char>( character - 'A' + 'a' );
            }
        }

        return value;
    }

    std::string uppercase_ascii( std::string value )
    {
        for( char &character : value )
        {
            if( character >= 'a' && character <= 'z' )
            {
                character = static_cast<char>( character - 'a' + 'A' );
            }
        }

        return value;
    }

    std::string normalize_slashes( std::string value )
    {
        std::replace( value.begin(), value.end(), '\\', '/' );
        return value;
    }

    std::string lowercase_filename( std::string value )
    {
        value = normalize_slashes( std::move( value ) );
        const std::size_t slash = value.find_last_of( '/' );

        if( slash != std::string::npos )
        {
            value.erase( 0, slash + 1 );
        }

        return lowercase_ascii( std::move( value ) );
    }

    std::vector<std::string> split_lines( std::string_view text )
    {
        std::vector<std::string> lines;
        std::size_t begin = 0u;

        while( begin < text.size() )
        {
            std::size_t end = text.find_first_of( "\r\n", begin );

            if( end == std::string_view::npos )
            {
                end = text.size();
            }

            if( end > begin )
            {
                lines.emplace_back( text.substr( begin, end - begin ) );
            }

            begin = end;
            while( begin < text.size() && ( text[ begin ] == '\r' || text[ begin ] == '\n' ) )
            {
                ++begin;
            }
        }

        return lines;
    }

    std::string safe_ascii_stem( std::string value, std::string fallback, bool lowercase )
    {
        const std::size_t slash = value.find_last_of( "/\\" );

        if( slash != std::string::npos )
        {
            value.erase( 0u, slash + 1u );
        }

        const std::size_t dot = value.find_last_of( '.' );

        if( dot != std::string::npos )
        {
            value.erase( dot );
        }

        for( char &character : value )
        {
            const unsigned char byte = static_cast<unsigned char>( character );
            const bool alpha_numeric =
                ( byte >= 'A' && byte <= 'Z' ) || ( byte >= 'a' && byte <= 'z' ) || ( byte >= '0' && byte <= '9' );

            if( !alpha_numeric && character != '_' && character != '-' )
            {
                character = '_';
            }
        }

        if( value.empty() )
        {
            value = std::move( fallback );
        }

        return lowercase ? lowercase_ascii( std::move( value ) ) : value;
    }

}
