#pragma once
#include <cstdint>

namespace piko::managers
{
    enum class ControllerOrientation : std::int32_t
    {
        TypeA,
        TypeB,
        TypeC
    };

    [[nodiscard]] constexpr ControllerOrientation controller_orientation_from_index( std::int32_t index )
    {
        return index <= 0   ? ControllerOrientation::TypeA
               : index >= 2 ? ControllerOrientation::TypeC
                            : ControllerOrientation::TypeB;
    }

    [[nodiscard]] constexpr std::int32_t controller_guard_rotation( ControllerOrientation orientation )
    {
        return orientation == ControllerOrientation::TypeB ? 3 : orientation == ControllerOrientation::TypeC ? 1 : 0;
    }

    [[nodiscard]] constexpr std::uint32_t controller_guard_symbol( std::uint32_t direction,
        ControllerOrientation orientation )
    {
        return direction < 4u
                   ? ( direction + 4u - static_cast<std::uint32_t>( controller_guard_rotation( orientation ) ) ) % 4u
                   : direction;
    }
}
