#pragma once

#include "Utils/EnumUtil.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace piko::managers
{
    enum class InterfaceFont : std::int32_t
    {
        Menu,
        Count
    };

    struct InterfaceStyle
    {
        std::array<std::string, utils::enum_index( InterfaceFont::Count )> fonts{};

        bool operator==( const InterfaceStyle & ) const = default;
    };
}
