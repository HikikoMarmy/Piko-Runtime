#include "Utility/JsonUtil.hpp"

namespace piko::convert::json_util
{
    std::string escape_string( std::string_view value, EscapeMode mode )
    {
        constexpr char lower_hex[] = "0123456789abcdef";
        constexpr char upper_hex[] = "0123456789ABCDEF";

        std::string output;
        output.reserve( value.size() + 8u );

        for( const unsigned char character : value )
        {
            switch( character )
            {
                case '"':
                    output += "\\\"";
                    break;
                case '\\':
                    output += "\\\\";
                    break;
                case '\b':
                    if( mode == EscapeMode::Json || mode == EscapeMode::Ascii )
                    {
                        output += "\\b";
                    }
                    else
                    {
                        output.push_back( static_cast<char>( character ) );
                    }
                    break;
                case '\f':
                    if( mode == EscapeMode::Json || mode == EscapeMode::Ascii )
                    {
                        output += "\\f";
                    }
                    else
                    {
                        output.push_back( static_cast<char>( character ) );
                    }
                    break;
                case '\n':
                    if( mode == EscapeMode::Minimal )
                    {
                        output.push_back( static_cast<char>( character ) );
                    }
                    else
                    {
                        output += "\\n";
                    }
                    break;
                case '\r':
                    if( mode == EscapeMode::Minimal )
                    {
                        output.push_back( static_cast<char>( character ) );
                    }
                    else
                    {
                        output += "\\r";
                    }
                    break;
                case '\t':
                    if( mode == EscapeMode::Minimal )
                    {
                        output.push_back( static_cast<char>( character ) );
                    }
                    else
                    {
                        output += "\\t";
                    }
                    break;
                default:
                    if( ( mode == EscapeMode::Json || mode == EscapeMode::Ascii ) && character < 0x20u )
                    {
                        const char *hex = mode == EscapeMode::Ascii ? upper_hex : lower_hex;
                        output += "\\u00";
                        output.push_back( hex[ character >> 4u ] );
                        output.push_back( hex[ character & 0x0fu ] );
                    }
                    else if( mode == EscapeMode::Ascii && character >= 0x80u )
                    {
                        output += "\\u00";
                        output.push_back( upper_hex[ character >> 4u ] );
                        output.push_back( upper_hex[ character & 0x0fu ] );
                    }
                    else
                    {
                        output.push_back( static_cast<char>( character ) );
                    }
                    break;
            }
        }

        return output;
    }
}
