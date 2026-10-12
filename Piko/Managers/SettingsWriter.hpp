#pragma once

#include "Utils/EnumUtil.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string_view>
#include <type_traits>
#include <vector>

namespace piko::managers
{
    class SettingsWriter
    {
    public:
        explicit SettingsWriter( std::ostream &stream );

        void begin_object();
        void begin_object( std::string_view key );
        void end_object();

        template<typename T>
        void write( std::string_view key, const T &value )
        {
            write_key( key );
            write_value( value );
        }

    private:
        static constexpr std::size_t indent_width = 4;
        static constexpr std::int32_t decimal_places = 6;
        static constexpr std::string_view hex_digits = "0123456789abcdef";

        void write_key( std::string_view key );
        void write_value( std::string_view value );

        template<typename T> requires std::is_arithmetic_v<T>
        void write_value( T value )
        {
            stream_ << value;
        }

        template<typename T> requires std::is_enum_v<T>
        void write_value( T value )
        {
            write_value( utils::to_int( value ) );
        }

        template<typename T, std::size_t Size>
        void write_value( const std::array<T, Size> &values )
        {
            stream_ << '[';

            for( std::size_t index = 0; index < values.size(); ++index )
            {
                if( index > 0 )
                {
                    stream_ << ", ";
                }

                write_value( values[ index ] );
            }

            stream_ << ']';
        }

        std::ostream &stream_;
        std::vector<std::size_t> field_counts_{};
    };
}
