#pragma once

#include <cstdint>
#include "Managers/TextureManager.hpp"

namespace piko
{
    class App;
}

namespace piko::rendering
{
    class Renderer;
    struct Texture;
}

namespace piko::modules
{
    class TitleBackgroundRenderer;

    class TitleScreenBackdrop
    {
    public:
        [[nodiscard]] static managers::TextureHandle create_circle_texture( App &app, const char *name );

        static void draw( rendering::Renderer &renderer,
            TitleBackgroundRenderer &background,
            std::int32_t output_width,
            std::int32_t output_height,
            float time_seconds,
            const rendering::Texture *circle_texture = nullptr );

    private:
        static void draw_fallback( rendering::Renderer &renderer,
            std::int32_t output_width,
            std::int32_t output_height,
            float time_seconds );

        static void draw_circle_field( rendering::Renderer &renderer,
            const rendering::Texture &circle_texture,
            std::int32_t output_width,
            std::int32_t output_height,
            float time_seconds );
    };
}
