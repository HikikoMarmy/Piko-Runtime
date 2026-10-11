#pragma once

#include <cstdint>

namespace piko::rendering
{
    struct TextureHandle
    {
        std::uint32_t id = 0;

        constexpr bool valid() const
        {
            return id != 0;
        }

        constexpr explicit operator bool() const
        {
            return valid();
        }
    };
}
