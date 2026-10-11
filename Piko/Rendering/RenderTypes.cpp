#include "Rendering/RenderTypes.hpp"

namespace piko::rendering
{
    bool Texture::valid() const
    {
        return id != 0;
    }

    Texture::operator bool() const
    {
        return valid();
    }
}
