#pragma once

#include "TextureHandle.hpp"

#include <cstdint>

namespace piko::rendering
{
    struct Texture
    {
        TextureHandle handle{};
        std::int32_t width = 0;
        std::int32_t height = 0;
    };
}
