#include "Json.hpp"

#include <cstdint>
#include <charconv>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace piko::utils::json::parser
{
    class Parser
    {
    public:
        explicit Parser( std::string_view text ) : text_( text )
        {
        }

        Value parse_document()
        {
            skip_space();
            Value value = parse_value();
            skip_space();

            if( offset_ != text_.size() )
            {
                fail( "Unexpected data after JSON value" );
            }

            return value;
        }

    private:
        [[noreturn]] void fail( const char *message ) const
        {
            throw std::runtime_error( std::string( message ) + " at byte " + std::to_string( offset_ ) );
        }

        void skip_space()
        {
            while( offset_ < text_.size() )
            {
                const char value = text_[ offset_ ];

                if( value != ' ' && value != '\t' && value != '\r' && value != '\n' )
                {
                    break;
                }

                ++offset_;
            }
        }

        bool consume( char expected )
        {
            if( offset_ >= text_.size() || text_[ offset_ ] != expected )
            {
                return false;
            }

            ++offset_;
            return true;
        }

        char take()
        {
            if( offset_ >= text_.size() )
            {
                fail( "Unexpected end of JSON" );
            }

            return text_[ offset_++ ];
        }

        Value parse_value()
        {
            skip_space();

            if( offset_ >= text_.size() )
            {
                fail( "Expected JSON value" );
            }

            switch( text_[ offset_ ] )
            {
                case 'n':
                    return parse_null();
                case 't':
                    return parse_true();
                case 'f':
                    return parse_false();
                case '"':
                    return Value::make_string( parse_string() );
                case '[':
                    return parse_array();
                case '{':
                    return parse_object();
                default:
                    return parse_number();
            }
        }

        Value parse_null()
        {
            expect_literal( "null" );
            return Value::make_null();
        }

        Value parse_true()
        {
            expect_literal( "true" );
            return Value::make_boolean( true );
        }

        Value parse_false()
        {
            expect_literal( "false" );
            return Value::make_boolean( false );
        }

        void expect_literal( std::string_view literal )
        {
            if( text_.substr( offset_, literal.size() ) != literal )
            {
                fail( "Invalid JSON literal" );
            }

            offset_ += literal.size();
        }

        static void append_utf8( std::string &output, std::uint32_t codepoint )
        {
            if( codepoint <= 0x7fu )
            {
                output.push_back( static_cast<char>( codepoint ) );
            }
            else if( codepoint <= 0x7ffu )
            {
                output.push_back( static_cast<char>( 0xc0u | ( codepoint >> 6u ) ) );
                output.push_back( static_cast<char>( 0x80u | ( codepoint & 0x3fu ) ) );
            }
            else if( codepoint <= 0xffffu )
            {
                output.push_back( static_cast<char>( 0xe0u | ( codepoint >> 12u ) ) );
                output.push_back( static_cast<char>( 0x80u | ( ( codepoint >> 6u ) & 0x3fu ) ) );
                output.push_back( static_cast<char>( 0x80u | ( codepoint & 0x3fu ) ) );
            }
            else
            {
                output.push_back( static_cast<char>( 0xf0u | ( codepoint >> 18u ) ) );
                output.push_back( static_cast<char>( 0x80u | ( ( codepoint >> 12u ) & 0x3fu ) ) );
                output.push_back( static_cast<char>( 0x80u | ( ( codepoint >> 6u ) & 0x3fu ) ) );
                output.push_back( static_cast<char>( 0x80u | ( codepoint & 0x3fu ) ) );
            }
        }

        static std::int32_t hex_digit( char value )
        {
            if( value >= '0' && value <= '9' )
            {
                return value - '0';
            }

            if( value >= 'a' && value <= 'f' )
            {
                return value - 'a' + 10;
            }

            if( value >= 'A' && value <= 'F' )
            {
                return value - 'A' + 10;
            }

            return -1;
        }

        std::uint32_t parse_hex4()
        {
            std::uint32_t value = 0;

            for( std::int32_t i = 0; i < 4; ++i )
            {
                const std::int32_t digit = hex_digit( take() );

                if( digit < 0 )
                {
                    fail( "Invalid JSON unicode escape" );
                }

                value = ( value << 4u ) | static_cast<std::uint32_t>( digit );
            }

            return value;
        }

        std::string parse_string()
        {
            if( !consume( '"' ) )
            {
                fail( "Expected JSON string" );
            }

            std::string output;
            while( offset_ < text_.size() )
            {
                const char value = take();

                if( value == '"' )
                {
                    return output;
                }

                if( static_cast<unsigned char>( value ) < 0x20u )
                {
                    fail( "Control character in JSON string" );
                }

                if( value != '\\' )
                {
                    output.push_back( value );
                    continue;
                }

                const char escape = take();

                switch( escape )
                {
                    case '"':
                        output.push_back( '"' );
                        break;
                    case '\\':
                        output.push_back( '\\' );
                        break;
                    case '/':
                        output.push_back( '/' );
                        break;
                    case 'b':
                        output.push_back( '\b' );
                        break;
                    case 'f':
                        output.push_back( '\f' );
                        break;
                    case 'n':
                        output.push_back( '\n' );
                        break;
                    case 'r':
                        output.push_back( '\r' );
                        break;
                    case 't':
                        output.push_back( '\t' );
                        break;
                    case 'u':
                    {
                        std::uint32_t codepoint = parse_hex4();

                        if( codepoint >= 0xd800u && codepoint <= 0xdbffu )
                        {
                            if( !consume( '\\' ) || !consume( 'u' ) )
                            {
                                fail( "Missing JSON low surrogate" );
                            }

                            const std::uint32_t low = parse_hex4();

                            if( low < 0xdc00u || low > 0xdfffu )
                            {
                                fail( "Invalid JSON low surrogate" );
                            }

                            codepoint = 0x10000u + ( ( codepoint - 0xd800u ) << 10u ) + ( low - 0xdc00u );
                        }

                        append_utf8( output, codepoint );
                        break;
                    }
                    default:
                        fail( "Invalid JSON escape" );
                }
            }

            fail( "Unterminated JSON string" );
        }

        Value parse_number()
        {
            const std::size_t begin = offset_;

            if( offset_ < text_.size() && text_[ offset_ ] == '-' )
            {
                ++offset_;
            }

            if( offset_ >= text_.size() )
            {
                fail( "Invalid JSON number" );
            }

            if( text_[ offset_ ] == '0' )
            {
                ++offset_;
            }
            else
            {
                if( text_[ offset_ ] < '1' || text_[ offset_ ] > '9' )
                {
                    fail( "Invalid JSON number" );
                }
                while( offset_ < text_.size() && text_[ offset_ ] >= '0' && text_[ offset_ ] <= '9' )
                {
                    ++offset_;
                }
            }

            if( offset_ < text_.size() && text_[ offset_ ] == '.' )
            {
                ++offset_;

                if( offset_ >= text_.size() || text_[ offset_ ] < '0' || text_[ offset_ ] > '9' )
                {
                    fail( "Invalid JSON fraction" );
                }
                while( offset_ < text_.size() && text_[ offset_ ] >= '0' && text_[ offset_ ] <= '9' )
                {
                    ++offset_;
                }
            }

            if( offset_ < text_.size() && ( text_[ offset_ ] == 'e' || text_[ offset_ ] == 'E' ) )
            {
                ++offset_;

                if( offset_ < text_.size() && ( text_[ offset_ ] == '+' || text_[ offset_ ] == '-' ) )
                {
                    ++offset_;
                }

                if( offset_ >= text_.size() || text_[ offset_ ] < '0' || text_[ offset_ ] > '9' )
                {
                    fail( "Invalid JSON exponent" );
                }
                while( offset_ < text_.size() && text_[ offset_ ] >= '0' && text_[ offset_ ] <= '9' )
                {
                    ++offset_;
                }
            }

            const std::string text( text_.substr( begin, offset_ - begin ) );
            char *end = nullptr;
            const double value = std::strtod( text.c_str(), &end );

            if( end == nullptr || *end != '\0' || !std::isfinite( value ) )
            {
                fail( "Invalid JSON number" );
            }

            return Value::make_number( value );
        }

        Value parse_array()
        {
            consume( '[' );
            skip_space();
            Value::Array values;

            if( consume( ']' ) )
            {
                return Value::make_array( std::move( values ) );
            }

            for( ;; )
            {
                values.push_back( parse_value() );
                skip_space();

                if( consume( ']' ) )
                {
                    break;
                }

                if( !consume( ',' ) )
                {
                    fail( "Expected comma in JSON array" );
                }

                skip_space();
            }

            return Value::make_array( std::move( values ) );
        }

        Value parse_object()
        {
            consume( '{' );
            skip_space();
            Value::Object values;

            if( consume( '}' ) )
            {
                return Value::make_object( std::move( values ) );
            }

            for( ;; )
            {
                if( offset_ >= text_.size() || text_[ offset_ ] != '"' )
                {
                    fail( "Expected key in JSON object" );
                }

                std::string key = parse_string();
                skip_space();

                if( !consume( ':' ) )
                {
                    fail( "Expected colon in JSON object" );
                }

                skip_space();
                values.insert_or_assign( std::move( key ), parse_value() );
                skip_space();

                if( consume( '}' ) )
                {
                    break;
                }

                if( !consume( ',' ) )
                {
                    fail( "Expected comma in JSON object" );
                }

                skip_space();
            }

            return Value::make_object( std::move( values ) );
        }

    private:
        std::string_view text_;
        std::size_t offset_ = 0;
    };
}

namespace piko::utils::json
{
    Type Value::type() const
    {
        return type_;
    }

    bool Value::is_null() const
    {
        return type_ == Type::Null;
    }

    bool Value::is_boolean() const
    {
        return type_ == Type::Boolean;
    }

    bool Value::is_number() const
    {
        return type_ == Type::Number;
    }

    bool Value::is_string() const
    {
        return type_ == Type::String;
    }

    bool Value::is_array() const
    {
        return type_ == Type::Array;
    }

    bool Value::is_object() const
    {
        return type_ == Type::Object;
    }

    bool Value::boolean() const
    {
        if( !is_boolean() )
        {
            throw std::runtime_error( "JSON value is not a boolean" );
        }

        return boolean_;
    }

    double Value::number() const
    {
        if( !is_number() )
        {
            throw std::runtime_error( "JSON value is not a number" );
        }

        return number_;
    }

    const std::string &Value::string() const
    {
        if( !is_string() )
        {
            throw std::runtime_error( "JSON value is not a string" );
        }

        return string_;
    }

    const Value::Array &Value::array() const
    {
        if( !is_array() )
        {
            throw std::runtime_error( "JSON value is not an array" );
        }

        return array_;
    }

    const Value::Object &Value::object() const
    {
        if( !is_object() )
        {
            throw std::runtime_error( "JSON value is not an object" );
        }

        return object_;
    }

    const Value *Value::find( std::string_view key ) const
    {
        if( !is_object() )
        {
            return nullptr;
        }

        const auto found = object_.find( std::string( key ) );
        return found == object_.end() ? nullptr : &found->second;
    }

    const Value &Value::require( std::string_view key ) const
    {
        const Value *value = find( key );

        if( value == nullptr )
        {
            throw std::runtime_error( "Missing JSON field: " + std::string( key ) );
        }

        return *value;
    }

    std::string Value::string_or( std::string fallback ) const
    {
        return is_string() ? string_ : std::move( fallback );
    }

    double Value::number_or( double fallback ) const
    {
        return is_number() ? number_ : fallback;
    }

    std::int64_t Value::integer_or( std::int64_t fallback ) const
    {
        if( !is_number() || number_ < static_cast<double>( std::numeric_limits<std::int64_t>::min() ) ||
            number_ > static_cast<double>( std::numeric_limits<std::int64_t>::max() ) )
        {
            return fallback;
        }

        return static_cast<std::int64_t>( number_ );
    }

    bool Value::boolean_or( bool fallback ) const
    {
        return is_boolean() ? boolean_ : fallback;
    }

    Value Value::make_null()
    {
        return Value{};
    }

    Value Value::make_boolean( bool value )
    {
        Value result;
        result.type_ = Type::Boolean;

        result.boolean_ = value;
        return result;
    }

    Value Value::make_number( double value )
    {
        Value result;
        result.type_ = Type::Number;

        result.number_ = value;
        return result;
    }

    Value Value::make_string( std::string value )
    {
        Value result;
        result.type_ = Type::String;

        result.string_ = std::move( value );
        return result;
    }

    Value Value::make_array( Array value )
    {
        Value result;
        result.type_ = Type::Array;

        result.array_ = std::move( value );
        return result;
    }

    Value Value::make_object( Object value )
    {
        Value result;
        result.type_ = Type::Object;

        result.object_ = std::move( value );
        return result;
    }

    Value parse( std::string_view text )
    {
        return parser::Parser( text ).parse_document();
    }

    Value load_file( const std::filesystem::path &path )
    {
        std::ifstream file( path, std::ios::binary | std::ios::ate );

        if( !file )
        {
            throw std::runtime_error( "Could not open JSON file: " + path.string() );
        }

        const std::streamsize size = file.tellg();

        if( size < 0 )
        {
            throw std::runtime_error( "Could not determine JSON file size: " + path.string() );
        }

        file.seekg( 0, std::ios::beg );
        std::string text( static_cast<std::size_t>( size ), '\0' );

        if( size > 0 && !file.read( text.data(), size ) )
        {
            throw std::runtime_error( "Could not read JSON file: " + path.string() );
        }

        return parse( text );
    }
}
