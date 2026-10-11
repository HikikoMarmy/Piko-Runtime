#pragma once

#include <cstddef>
#include <type_traits>

namespace piko::utils
{
    template <typename Enum> [[nodiscard]] constexpr auto to_int( Enum value ) noexcept -> std::underlying_type_t<Enum>
    {
        static_assert( std::is_enum_v<Enum>, "to_int requires an enum type" );
        return static_cast<std::underlying_type_t<Enum>>( value );
    }

    template <typename Enum> [[nodiscard]] constexpr std::size_t enum_index( Enum value ) noexcept
    {
        return static_cast<std::size_t>( to_int( value ) );
    }

    template <typename Enum, typename Integer> [[nodiscard]] constexpr Enum to_enum( Integer value ) noexcept
    {
        static_assert( std::is_enum_v<Enum>, "to_enum requires an enum type" );
        static_assert( std::is_integral_v<Integer>, "to_enum requires an integer value" );
        return static_cast<Enum>( value );
    }
}
