#pragma once

#include "RenderTypes.hpp"
#include "Rendering/TrueTypeText.hpp"
#include <unordered_map>
#include "Shader.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace piko::rendering
{
    enum class TextureFilterMode
    {
        Nearest = 0,
        Linear = 1,
        LinearMipmap = 2,
        Anisotropic = 3
    };

    struct TextureSamplingState
    {
        TextureFilterMode mode = TextureFilterMode::LinearMipmap;
        float anisotropy = 4.0f;
    };

    struct StagePostProcessSettings
    {
        bool bloom_enabled = false;
        float bloom_threshold = 1.05f;
        float bloom_intensity = 0.35f;
        std::int32_t bloom_iterations = 3;
        float bloom_radius = 1.5f;
    };

    struct CrtOverlaySettings
    {
        bool enabled = false;
        float scanline_strength = 0.16f;
        float vignette_strength = 0.18f;
        float noise_strength = 0.025f;
        float time_seconds = 0.0f;
    };

    struct RendererFrameStats
    {
        std::uint64_t colored_draw_calls = 0;
        std::uint64_t textured_draw_calls = 0;
        std::uint64_t text_draw_calls = 0;
        std::uint64_t glyph_quads = 0;
        std::uint64_t vertices = 0;

        void reset();
    };

    class Renderer
    {
    public:
        Renderer() = default;
        ~Renderer();

        Renderer( const Renderer & ) = delete;
        Renderer &operator=( const Renderer & ) = delete;

        bool initialize( SDL_Window *window, const std::filesystem::path &shader_root = {} );
        void shutdown();

        void begin_frame();
        void set_msaa_enabled( bool enabled );
        void set_stage_render_scale( float scale );
        void set_stage_msaa_samples( std::int32_t samples );
        void set_texture_sampling( const TextureSamplingState &sampling );
        [[nodiscard]] const TextureSamplingState &texture_sampling() const;

        [[nodiscard]] bool begin_stage_post_scene( const StagePostProcessSettings &settings );
        void end_stage_post_scene_and_composite( const StagePostProcessSettings &settings );
        void draw_crt_overlay( const CrtOverlaySettings &settings );

        void reset_frame_stats();
        [[nodiscard]] const RendererFrameStats &frame_stats() const;
        void clear( float r, float g, float b, float a );
        void end_frame();

        void push_clip_rect( const Rect &rect );
        void pop_clip_rect();

        std::int32_t output_width() const;
        std::int32_t output_height() const;

        void draw_filled_rect( const Rect &rect, const Color &color );
        void draw_filled_ellipse( const Rect &rect, const Color &color );
        void draw_line( const Vec2 &a, const Vec2 &b, float thickness, const Color &color );
        void draw_line( float x0, float y0, float x1, float y1, float thickness, const Color &color );
        void draw_colored_triangles( const VertexColor2D *vertices, std::size_t vertex_count );
        void draw_textured_triangles( const Texture &texture,
            const VertexTexture2D *vertices,
            std::size_t vertex_count );

        void draw_textured_triangles_single_coverage( const Texture &texture,
            const VertexTexture2D *vertices,
            std::size_t vertex_count );

        void draw_text( float x, float y, const char *text, const Color &color, float scale = 1.0f );

        bool create_texture_rgba( const void *pixels, std::int32_t width, std::int32_t height, Texture &out_texture );
        bool update_texture_rgba( Texture &texture, const void *pixels, std::int32_t width, std::int32_t height );
        void destroy_texture( Texture &texture );

        void draw_texture( const Texture &texture,
            float x,
            float y,
            float width,
            float height,
            const Color &tint = Color{} );

        void draw_texture_opaque( const Texture &texture, float x, float y, float width, float height );
        void draw_texture_alpha_mask( const Texture &texture, float x, float y, float width, float height );
        void draw_texture_rotated( const Texture &texture,
            float origin_x,
            float origin_y,
            float width,
            float height,
            float anchor_x,
            float anchor_y,
            float angle_degrees,
            const Color &tint = Color{} );

        void draw_texture_region( const Texture &texture,
            float src_x,
            float src_y,
            float src_w,
            float src_h,
            float dst_x,
            float dst_y,
            float dst_w,
            float dst_h,
            const Color &tint = Color{} );

        void draw_texture_region_rotated( const Texture &texture,
            float src_x,
            float src_y,
            float src_w,
            float src_h,
            float origin_x,
            float origin_y,
            float dst_w,
            float dst_h,
            float anchor_x,
            float anchor_y,
            float angle_degrees,
            const Color &tint = Color{} );

        void draw_texture_region_rotated_additive( const Texture &texture,
            float src_x,
            float src_y,
            float src_w,
            float src_h,
            float origin_x,
            float origin_y,
            float dst_w,
            float dst_h,
            float anchor_x,
            float anchor_y,
            float angle_degrees,
            const Color &tint = Color{} );

    private:
        static constexpr float bloom_reference_height_ = 720.0f;
        static constexpr std::size_t ellipse_segment_count_ = 256;
        static constexpr float ellipse_edge_width_ = 0.75f;

        static constexpr std::uint32_t quad_indices_[ 6 ] = {
            0,
            1,
            2,
            2,
            3,
            0
        };

        static const char *gl_string( std::uint32_t name );
        static const std::uint8_t *glyph_rows( char c );

        void clear_text_cache();
        bool draw_unicode_text( float x, float y, const char *text, const Color &color, float scale );

        bool initialize_shaders();
        [[nodiscard]] std::filesystem::path shader_path( const char *filename ) const;
        bool initialize_geometry();
        bool initialize_post_processing();
        void shutdown_geometry();
        void shutdown_post_processing();

        void update_screen_uniforms();
        void apply_clip_rect();
        void apply_texture_sampling_to_bound_texture() const;
        bool ensure_post_resources( std::int32_t width, std::int32_t height, std::int32_t samples );
        void draw_fullscreen_texture( GLuint texture, const Shader &shader );
        void draw_fullscreen_pass( const Shader &shader );
        void draw_colored_quad( const VertexColor2D *vertices );
        void draw_textured_quad( const VertexTexture2D *vertices );

    private:
        TrueTypeFont body_font_{};
        std::unordered_map<std::string, Texture> text_cache_{};
        std::uint32_t text_revision_ = 0;

        SDL_Window *window_ = nullptr;
        bool initialized_ = false;
        std::filesystem::path shader_root_{};

        Shader color_shader_;
        Shader texture_shader_;
        Shader bloom_extract_shader_;
        Shader bloom_blur_shader_;
        Shader stage_composite_shader_;
        Shader crt_overlay_shader_;

        GLuint color_vao_ = 0;
        GLuint color_vbo_ = 0;
        GLuint color_ebo_ = 0;

        GLuint texture_vao_ = 0;
        GLuint texture_vbo_ = 0;
        GLuint texture_ebo_ = 0;

        GLuint fullscreen_vao_ = 0;
        GLuint fullscreen_vbo_ = 0;

        GLuint stage_scene_fbo_ = 0;
        GLuint stage_scene_color_ = 0;
        GLuint stage_scene_depth_stencil_ = 0;
        GLuint stage_msaa_fbo_ = 0;
        GLuint stage_msaa_color_ = 0;
        GLuint stage_msaa_depth_stencil_ = 0;
        GLuint bloom_fbo_[ 2 ] = {
            0,
            0
        };

        GLuint bloom_color_[ 2 ] = {
            0,
            0
        };

        std::int32_t post_width_ = 0;
        std::int32_t post_height_ = 0;
        bool stage_post_active_ = false;
        float stage_render_scale_ = 1.0f;
        std::int32_t stage_msaa_samples_ = 4;
        std::int32_t post_msaa_samples_ = 0;

        bool msaa_enabled_ = true;
        TextureSamplingState texture_sampling_{};
        float max_texture_anisotropy_ = 1.0f;

        RendererFrameStats frame_stats_{};
        std::vector<Rect> clip_rect_stack_{};
    };
}
