#pragma once

#include "Managers/TextureManager.hpp"
#include "Managers/InterfaceStyle.hpp"
#include "Rendering/Renderer.hpp"

#include <string_view>
#include <filesystem>

namespace piko::localization
{
    [[nodiscard]] managers::TextureHandle load_image( managers::TextureManager &textures,
        const std::filesystem::path &relative );

    [[nodiscard]] managers::TextureHandle make_label( managers::TextureManager &textures,
        std::string_view key,
        std::string_view style_name );

    void draw_label( rendering::Renderer &renderer,
        const rendering::Texture *texture,
        const rendering::Rect &bounds,
        const rendering::Color &tint = {},
        bool centered = true );
}
