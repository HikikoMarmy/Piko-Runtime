#pragma once

#include "Rendering/RenderTypes.hpp"
#include "Rendering/Shader.hpp"

#include <filesystem>
#include <string>

namespace piko
{
    class App;
}

namespace piko::rendering
{
    class Renderer;
}

namespace piko::modules
{
    class TitleBackgroundRenderer final
    {
    public:
        TitleBackgroundRenderer() = default;
        ~TitleBackgroundRenderer();

        TitleBackgroundRenderer( const TitleBackgroundRenderer & ) = delete;
        TitleBackgroundRenderer &operator=( const TitleBackgroundRenderer & ) = delete;

        bool initialize( App &app, const char *settings_path = nullptr );
        void shutdown();
        void set_palette( const rendering::Color &base, const rendering::Color &fill, const rendering::Color &line );

        [[nodiscard]] bool render( rendering::Renderer &renderer, float time_seconds );
        [[nodiscard]] bool valid() const;
        [[nodiscard]] const std::string &last_error() const;

    private:
        struct FullscreenVertex
        {
            float x = 0.0f;
            float y = 0.0f;
            float u = 0.0f;
            float v = 0.0f;
        };

        struct Settings
        {
            bool enabled = true;
            std::string vertex_shader = "shaders/title_topology.vert";
            std::string fragment_shader = "shaders/title_topology.frag";

            float base_color[ 3 ] = {
                0.54f,
                0.80f,
                1.00f
            };

            float fill_color[ 3 ] = {
                0.72f,
                0.92f,
                1.00f
            };

            float line_color[ 3 ] = {
                0.88f,
                0.98f,
                1.00f
            };

            float band_colors[ 4 ][ 3 ] = {};
            bool banded_palette = false;

            float speed = 0.055f;
            float contour_scale = 2.35f;
            float contour_count = 13.0f;
            float line_width = 0.050f;
            float line_softness = 0.020f;
            float line_alpha = 0.56f;
            float glow = 0.30f;
            float vignette = 0.18f;
            float grain = 0.020f;
        };

        bool load_settings( const std::filesystem::path &path );
        static std::string normalize_shader_relative_path( const std::string &path );
        bool initialize_geometry();
        void destroy_geometry();

    private:
        Settings settings_{};
        rendering::Shader shader_{};
        GLuint vao_ = 0;
        GLuint vbo_ = 0;
        bool initialized_ = false;
        std::string last_error_{};
    };
}
