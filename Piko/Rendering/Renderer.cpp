#include "Renderer.hpp"
#include "Localization/Language.hpp"

#include "Rendering/RenderUtil.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#ifndef GL_DEPTH_BITS
#define GL_DEPTH_BITS 0x0D56
#endif

#ifndef GL_STENCIL_BITS
#define GL_STENCIL_BITS 0x0D57
#endif

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

namespace piko::rendering
{
    LogicalViewport make_logical_viewport( const Renderer &renderer, float logical_width, float logical_height )
    {
        return make_logical_viewport( renderer.output_width(),
            renderer.output_height(),
            logical_width,
            logical_height );
    }

    void RendererFrameStats::reset()
    {
        colored_draw_calls = 0;

        textured_draw_calls = 0;

        text_draw_calls = 0;

        glyph_quads = 0;

        vertices = 0;
    }

    const char *Renderer::gl_string( std::uint32_t name )
    {
        const GLubyte *text = glGetString( name );

        if( text == nullptr )
        {
            return "Unknown";
        }

        return reinterpret_cast<const char *>( text );
    }

    const std::uint8_t *Renderer::glyph_rows( char c )
    {
        switch( c )
        {
            case 'A':
            {
                static constexpr std::uint8_t rows[]{
                    0x0E,
                    0x11,
                    0x11,
                    0x1F,
                    0x11,
                    0x11,
                    0x11
                };

                return rows;
            }
            case 'B':
            {
                static constexpr std::uint8_t rows[]{
                    0x1E,
                    0x11,
                    0x11,
                    0x1E,
                    0x11,
                    0x11,
                    0x1E
                };

                return rows;
            }
            case 'C':
            {
                static constexpr std::uint8_t rows[]{
                    0x0E,
                    0x11,
                    0x10,
                    0x10,
                    0x10,
                    0x11,
                    0x0E
                };

                return rows;
            }
            case 'D':
            {
                static constexpr std::uint8_t rows[]{
                    0x1E,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x1E
                };

                return rows;
            }
            case 'E':
            {
                static constexpr std::uint8_t rows[]{
                    0x1F,
                    0x10,
                    0x10,
                    0x1E,
                    0x10,
                    0x10,
                    0x1F
                };

                return rows;
            }
            case 'F':
            {
                static constexpr std::uint8_t rows[]{
                    0x1F,
                    0x10,
                    0x10,
                    0x1E,
                    0x10,
                    0x10,
                    0x10
                };

                return rows;
            }
            case 'G':
            {
                static constexpr std::uint8_t rows[]{
                    0x0E,
                    0x11,
                    0x10,
                    0x17,
                    0x11,
                    0x11,
                    0x0E
                };

                return rows;
            }
            case 'H':
            {
                static constexpr std::uint8_t rows[]{
                    0x11,
                    0x11,
                    0x11,
                    0x1F,
                    0x11,
                    0x11,
                    0x11
                };

                return rows;
            }
            case 'I':
            {
                static constexpr std::uint8_t rows[]{
                    0x1F,
                    0x04,
                    0x04,
                    0x04,
                    0x04,
                    0x04,
                    0x1F
                };

                return rows;
            }
            case 'J':
            {
                static constexpr std::uint8_t rows[]{
                    0x1F,
                    0x02,
                    0x02,
                    0x02,
                    0x12,
                    0x12,
                    0x0C
                };

                return rows;
            }
            case 'K':
            {
                static constexpr std::uint8_t rows[]{
                    0x11,
                    0x12,
                    0x14,
                    0x18,
                    0x14,
                    0x12,
                    0x11
                };

                return rows;
            }
            case 'L':
            {
                static constexpr std::uint8_t rows[]{
                    0x10,
                    0x10,
                    0x10,
                    0x10,
                    0x10,
                    0x10,
                    0x1F
                };

                return rows;
            }
            case 'M':
            {
                static constexpr std::uint8_t rows[]{
                    0x11,
                    0x1B,
                    0x15,
                    0x15,
                    0x11,
                    0x11,
                    0x11
                };

                return rows;
            }
            case 'N':
            {
                static constexpr std::uint8_t rows[]{
                    0x11,
                    0x11,
                    0x19,
                    0x15,
                    0x13,
                    0x11,
                    0x11
                };

                return rows;
            }
            case 'O':
            {
                static constexpr std::uint8_t rows[]{
                    0x0E,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x0E
                };

                return rows;
            }
            case 'P':
            {
                static constexpr std::uint8_t rows[]{
                    0x1E,
                    0x11,
                    0x11,
                    0x1E,
                    0x10,
                    0x10,
                    0x10
                };

                return rows;
            }
            case 'Q':
            {
                static constexpr std::uint8_t rows[]{
                    0x0E,
                    0x11,
                    0x11,
                    0x11,
                    0x15,
                    0x12,
                    0x0D
                };

                return rows;
            }
            case 'R':
            {
                static constexpr std::uint8_t rows[]{
                    0x1E,
                    0x11,
                    0x11,
                    0x1E,
                    0x14,
                    0x12,
                    0x11
                };

                return rows;
            }
            case 'S':
            {
                static constexpr std::uint8_t rows[]{
                    0x0F,
                    0x10,
                    0x10,
                    0x0E,
                    0x01,
                    0x01,
                    0x1E
                };

                return rows;
            }
            case 'T':
            {
                static constexpr std::uint8_t rows[]{
                    0x1F,
                    0x04,
                    0x04,
                    0x04,
                    0x04,
                    0x04,
                    0x04
                };

                return rows;
            }
            case 'U':
            {
                static constexpr std::uint8_t rows[]{
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x0E
                };

                return rows;
            }
            case 'V':
            {
                static constexpr std::uint8_t rows[]{
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x11,
                    0x0A,
                    0x04
                };

                return rows;
            }
            case 'W':
            {
                static constexpr std::uint8_t rows[]{
                    0x11,
                    0x11,
                    0x11,
                    0x15,
                    0x15,
                    0x15,
                    0x0A
                };

                return rows;
            }
            case 'X':
            {
                static constexpr std::uint8_t rows[]{
                    0x11,
                    0x11,
                    0x0A,
                    0x04,
                    0x0A,
                    0x11,
                    0x11
                };

                return rows;
            }
            case 'Y':
            {
                static constexpr std::uint8_t rows[]{
                    0x11,
                    0x11,
                    0x0A,
                    0x04,
                    0x04,
                    0x04,
                    0x04
                };

                return rows;
            }
            case 'Z':
            {
                static constexpr std::uint8_t rows[]{
                    0x1F,
                    0x01,
                    0x02,
                    0x04,
                    0x08,
                    0x10,
                    0x1F
                };

                return rows;
            }
            case '%':
            {
                static constexpr std::uint8_t rows[]{
                    0x18,
                    0x19,
                    0x02,
                    0x04,
                    0x08,
                    0x13,
                    0x03
                };

                return rows;
            }
            case '0':
            {
                static constexpr std::uint8_t rows[]{
                    0x0E,
                    0x11,
                    0x13,
                    0x15,
                    0x19,
                    0x11,
                    0x0E
                };

                return rows;
            }
            case '1':
            {
                static constexpr std::uint8_t rows[]{
                    0x04,
                    0x0C,
                    0x04,
                    0x04,
                    0x04,
                    0x04,
                    0x0E
                };

                return rows;
            }
            case '2':
            {
                static constexpr std::uint8_t rows[]{
                    0x0E,
                    0x11,
                    0x01,
                    0x02,
                    0x04,
                    0x08,
                    0x1F
                };

                return rows;
            }
            case '3':
            {
                static constexpr std::uint8_t rows[]{
                    0x1E,
                    0x01,
                    0x01,
                    0x0E,
                    0x01,
                    0x01,
                    0x1E
                };

                return rows;
            }
            case '4':
            {
                static constexpr std::uint8_t rows[]{
                    0x02,
                    0x06,
                    0x0A,
                    0x12,
                    0x1F,
                    0x02,
                    0x02
                };

                return rows;
            }
            case '5':
            {
                static constexpr std::uint8_t rows[]{
                    0x1F,
                    0x10,
                    0x10,
                    0x1E,
                    0x01,
                    0x01,
                    0x1E
                };

                return rows;
            }
            case '6':
            {
                static constexpr std::uint8_t rows[]{
                    0x0E,
                    0x10,
                    0x10,
                    0x1E,
                    0x11,
                    0x11,
                    0x0E
                };

                return rows;
            }
            case '7':
            {
                static constexpr std::uint8_t rows[]{
                    0x1F,
                    0x01,
                    0x02,
                    0x04,
                    0x08,
                    0x08,
                    0x08
                };

                return rows;
            }
            case '8':
            {
                static constexpr std::uint8_t rows[]{
                    0x0E,
                    0x11,
                    0x11,
                    0x0E,
                    0x11,
                    0x11,
                    0x0E
                };

                return rows;
            }
            case '9':
            {
                static constexpr std::uint8_t rows[]{
                    0x0E,
                    0x11,
                    0x11,
                    0x0F,
                    0x01,
                    0x01,
                    0x0E
                };

                return rows;
            }
            case ':':
            {
                static constexpr std::uint8_t rows[]{
                    0x00,
                    0x04,
                    0x04,
                    0x00,
                    0x04,
                    0x04,
                    0x00
                };

                return rows;
            }
            case '.':
            {
                static constexpr std::uint8_t rows[]{
                    0x00,
                    0x00,
                    0x00,
                    0x00,
                    0x00,
                    0x0C,
                    0x0C
                };

                return rows;
            }
            case '-':
            {
                static constexpr std::uint8_t rows[]{
                    0x00,
                    0x00,
                    0x00,
                    0x1F,
                    0x00,
                    0x00,
                    0x00
                };

                return rows;
            }
            case '=':
            {
                static constexpr std::uint8_t rows[]{
                    0x00,
                    0x1F,
                    0x00,
                    0x00,
                    0x1F,
                    0x00,
                    0x00
                };

                return rows;
            }
            case '/':
            {
                static constexpr std::uint8_t rows[]{
                    0x01,
                    0x01,
                    0x02,
                    0x04,
                    0x08,
                    0x10,
                    0x10
                };

                return rows;
            }
            case '(':
            {
                static constexpr std::uint8_t rows[]{
                    0x02,
                    0x04,
                    0x08,
                    0x08,
                    0x08,
                    0x04,
                    0x02
                };

                return rows;
            }
            case ')':
            {
                static constexpr std::uint8_t rows[]{
                    0x08,
                    0x04,
                    0x02,
                    0x02,
                    0x02,
                    0x04,
                    0x08
                };

                return rows;
            }
            case '!':
            {
                static constexpr std::uint8_t rows[]{
                    0x04,
                    0x04,
                    0x04,
                    0x04,
                    0x04,
                    0x00,
                    0x04
                };

                return rows;
            }
            case ' ':
            {
                static constexpr std::uint8_t rows[]{
                    0x00,
                    0x00,
                    0x00,
                    0x00,
                    0x00,
                    0x00,
                    0x00
                };

                return rows;
            }
            default:
            {
                static constexpr std::uint8_t rows[]{
                    0x1F,
                    0x01,
                    0x02,
                    0x04,
                    0x00,
                    0x04,
                    0x00
                };

                return rows;
            }
        }
    }

    Renderer::~Renderer()
    {
        shutdown();
    }

    bool Renderer::initialize( SDL_Window *window, const std::filesystem::path &shader_root )
    {
        if( initialized_ )
        {
            return true;
        }

        if( window == nullptr )
        {
            std::cerr << "Renderer::initialize failed: window is null.\n";
            return false;
        }

        window_ = window;

        shader_root_ = shader_root.empty()
                           ? ( std::filesystem::current_path() / "GAME_DATA" / "PIKO" / "shaders" ).lexically_normal()
                           : shader_root.lexically_normal();

        if( !gladLoadGL( reinterpret_cast<GLADloadfunc>( SDL_GL_GetProcAddress ) ) )
        {
            std::cerr << "Renderer::initialize failed: gladLoadGL failed.\n";
            window_ = nullptr;
            return false;
        }

        glEnable( GL_BLEND );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

        set_msaa_enabled( msaa_enabled_ );

        if( const char *extensions = reinterpret_cast<const char *>( glGetString( GL_EXTENSIONS ) );
            extensions != nullptr )
        {
            if( std::string( extensions ).find( "GL_EXT_texture_filter_anisotropic" ) != std::string::npos )
            {
                glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_texture_anisotropy_ );
                max_texture_anisotropy_ = std::max( 1.0f, max_texture_anisotropy_ );
            }
        }

        glDisable( GL_DEPTH_TEST );
        glDisable( GL_CULL_FACE );

        if( !initialize_shaders() )
        {
            shutdown();
            return false;
        }

        if( !initialize_geometry() )
        {
            shutdown();
            return false;
        }

        if( !initialize_post_processing() )
        {
            shutdown();
            return false;
        }

        GLint depth_bits = 0;
        GLint stencil_bits = 0;
        glGetIntegerv( GL_DEPTH_BITS, &depth_bits );
        glGetIntegerv( GL_STENCIL_BITS, &stencil_bits );

        std::cout << "OpenGL Vendor:   " << gl_string( GL_VENDOR ) << '\n';
        std::cout << "OpenGL Renderer: " << gl_string( GL_RENDERER ) << '\n';
        std::cout << "OpenGL Version:  " << gl_string( GL_VERSION ) << '\n';
        std::cout << "OpenGL Depth:    " << depth_bits << " bits, stencil: " << stencil_bits << " bits\n";

        initialized_ = true;
        return true;
    }

    void Renderer::shutdown()
    {
        if( window_ == nullptr )
        {
            return;
        }

        clear_text_cache();
        body_font_.clear();
        clip_rect_stack_.clear();
        glDisable( GL_SCISSOR_TEST );
        shutdown_post_processing();
        shutdown_geometry();
        color_shader_.destroy();
        texture_shader_.destroy();
        bloom_extract_shader_.destroy();
        bloom_blur_shader_.destroy();
        stage_composite_shader_.destroy();
        crt_overlay_shader_.destroy();

        initialized_ = false;

        window_ = nullptr;
    }

    void Renderer::begin_frame()
    {
        if( !initialized_ || window_ == nullptr )
        {
            return;
        }

        stage_post_active_ = false;
        clip_rect_stack_.clear();
        glDisable( GL_SCISSOR_TEST );
        set_msaa_enabled( msaa_enabled_ );
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
        glViewport( 0, 0, output_width(), output_height() );
    }

    void Renderer::push_clip_rect( const Rect &rect )
    {
        Rect clipped = rect;

        if( !clip_rect_stack_.empty() )
        {
            const Rect &parent = clip_rect_stack_.back();
            const float left = std::max( clipped.x, parent.x );
            const float top = std::max( clipped.y, parent.y );
            const float right = std::min( clipped.x + clipped.w, parent.x + parent.w );
            const float bottom = std::min( clipped.y + clipped.h, parent.y + parent.h );
            clipped = Rect{ left, top, std::max( 0.0f, right - left ), std::max( 0.0f, bottom - top ) };
        }

        clip_rect_stack_.push_back( clipped );
        apply_clip_rect();
    }

    void Renderer::pop_clip_rect()
    {
        if( clip_rect_stack_.empty() )
        {
            return;
        }

        clip_rect_stack_.pop_back();
        apply_clip_rect();
    }

    void Renderer::apply_clip_rect()
    {
        if( clip_rect_stack_.empty() )
        {
            glDisable( GL_SCISSOR_TEST );
            return;
        }

        const Rect &rect = clip_rect_stack_.back();
        const std::int32_t screen_width = output_width();
        const std::int32_t screen_height = output_height();
        const std::int32_t left = std::clamp( static_cast<std::int32_t>( std::floor( rect.x ) ), 0, screen_width );
        const std::int32_t top = std::clamp( static_cast<std::int32_t>( std::floor( rect.y ) ), 0, screen_height );
        const std::int32_t right =
            std::clamp( static_cast<std::int32_t>( std::ceil( rect.x + rect.w ) ), left, screen_width );

        const std::int32_t bottom =
            std::clamp( static_cast<std::int32_t>( std::ceil( rect.y + rect.h ) ), top, screen_height );

        glEnable( GL_SCISSOR_TEST );
        glScissor( left, screen_height - bottom, right - left, bottom - top );
    }

    void Renderer::set_msaa_enabled( bool enabled )
    {
        msaa_enabled_ = enabled;

        if( enabled )
        {
            glEnable( GL_MULTISAMPLE );
        }
        else
        {
            glDisable( GL_MULTISAMPLE );
        }
    }

    void Renderer::set_stage_render_scale( float scale )
    {
        stage_render_scale_ = std::clamp( scale, 0.25f, 2.0f );
    }

    void Renderer::set_stage_msaa_samples( std::int32_t samples )
    {
        stage_msaa_samples_ = std::clamp( samples, 0, 16 );
    }

    void Renderer::set_texture_sampling( const TextureSamplingState &sampling )
    {
        texture_sampling_.mode = sampling.mode;

        texture_sampling_.anisotropy =
            std::clamp( sampling.anisotropy, 1.0f, std::max( 1.0f, max_texture_anisotropy_ ) );
    }

    const TextureSamplingState &Renderer::texture_sampling() const
    {
        return texture_sampling_;
    }

    bool Renderer::begin_stage_post_scene( const StagePostProcessSettings &settings )
    {
        if( !initialized_ )
        {
            return false;
        }

        if( !settings.bloom_enabled && std::abs( stage_render_scale_ - 1.0f ) < 0.001f )
        {
            return false;
        }

        const std::int32_t width = static_cast<std::int32_t>( std::lround( output_width() * stage_render_scale_ ) );
        const std::int32_t height = static_cast<std::int32_t>( std::lround( output_height() * stage_render_scale_ ) );
        GLint max_samples = 0;
        glGetIntegerv( GL_MAX_SAMPLES, &max_samples );
        const std::int32_t samples = msaa_enabled_ && stage_msaa_samples_ >= 2
                                         ? std::min( stage_msaa_samples_, static_cast<std::int32_t>( max_samples ) )
                                         : 0;

        if( width <= 0 || height <= 0 || !ensure_post_resources( width, height, samples ) )
        {
            return false;
        }

        stage_post_active_ = true;
        glBindFramebuffer( GL_FRAMEBUFFER, post_msaa_samples_ >= 2 ? stage_msaa_fbo_ : stage_scene_fbo_ );
        glViewport( 0, 0, width, height );
        glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
        return true;
    }

    void Renderer::end_stage_post_scene_and_composite( const StagePostProcessSettings &settings )
    {
        if( !stage_post_active_ )
        {
            return;
        }

        stage_post_active_ = false;

        const std::int32_t width = output_width();
        const std::int32_t height = output_height();

        if( width <= 0 || height <= 0 )
        {
            glBindFramebuffer( GL_FRAMEBUFFER, 0 );
            return;
        }

        const std::int32_t bloom_width = std::max( 1, post_width_ / 2 );
        const std::int32_t bloom_height = std::max( 1, post_height_ / 2 );

        if( post_msaa_samples_ >= 2 )
        {
            glBindFramebuffer( GL_READ_FRAMEBUFFER, stage_msaa_fbo_ );
            glBindFramebuffer( GL_DRAW_FRAMEBUFFER, stage_scene_fbo_ );
            glBlitFramebuffer( 0,
                0,
                post_width_,
                post_height_,
                0,
                0,
                post_width_,
                post_height_,
                GL_COLOR_BUFFER_BIT,
                GL_NEAREST );
        }

        glDisable( GL_DEPTH_TEST );
        glDisable( GL_CULL_FACE );
        glDepthMask( GL_FALSE );
        glEnable( GL_BLEND );
        glBlendEquation( GL_FUNC_ADD );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

        GLuint input_texture = 0;

        if( settings.bloom_enabled )
        {
            glBindFramebuffer( GL_FRAMEBUFFER, bloom_fbo_[ 0 ] );
            glViewport( 0, 0, bloom_width, bloom_height );
            glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
            glClear( GL_COLOR_BUFFER_BIT );

            bloom_extract_shader_.use();
            bloom_extract_shader_.set_int( "u_scene", 0 );

            if( const GLint threshold = glGetUniformLocation( bloom_extract_shader_.program(), "u_threshold" );
                threshold >= 0 )
            {
                glUniform1f( threshold, std::max( 0.0f, settings.bloom_threshold ) );
            }

            draw_fullscreen_texture( stage_scene_color_, bloom_extract_shader_ );

            const std::int32_t iterations = std::clamp( settings.bloom_iterations, 1, 8 );
            const float radius = std::max( 0.25f, settings.bloom_radius ) *
                static_cast<float>( post_height_ ) / bloom_reference_height_;
            bool horizontal = true;
            input_texture = bloom_color_[ 0 ];

            for( std::int32_t i = 0; i < iterations * 2; ++i )
            {
                const std::int32_t target = horizontal ? 1 : 0;
                glBindFramebuffer( GL_FRAMEBUFFER, bloom_fbo_[ target ] );
                glViewport( 0, 0, bloom_width, bloom_height );
                glClear( GL_COLOR_BUFFER_BIT );

                bloom_blur_shader_.use();
                bloom_blur_shader_.set_int( "u_texture", 0 );

                if( const GLint dir = glGetUniformLocation( bloom_blur_shader_.program(), "u_texel_direction" );
                    dir >= 0 )
                {
                    glUniform2f( dir,
                        horizontal ? ( radius / static_cast<float>( bloom_width ) ) : 0.0f,
                        horizontal ? 0.0f : ( radius / static_cast<float>( bloom_height ) ) );
                }

                draw_fullscreen_texture( input_texture, bloom_blur_shader_ );
                input_texture = bloom_color_[ target ];

                horizontal = !horizontal;
            }
        }

        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
        glViewport( 0, 0, width, height );
        stage_composite_shader_.use();
        stage_composite_shader_.set_int( "u_scene", 0 );
        stage_composite_shader_.set_int( "u_bloom", 1 );

        if( const GLint enabled = glGetUniformLocation( stage_composite_shader_.program(), "u_bloom_enabled" );
            enabled >= 0 )
        {
            glUniform1i( enabled, settings.bloom_enabled ? 1 : 0 );
        }

        if( const GLint intensity = glGetUniformLocation( stage_composite_shader_.program(), "u_bloom_intensity" );
            intensity >= 0 )
        {
            glUniform1f( intensity, std::max( 0.0f, settings.bloom_intensity ) );
        }

        glActiveTexture( GL_TEXTURE1 );
        glBindTexture( GL_TEXTURE_2D, input_texture );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        glActiveTexture( GL_TEXTURE0 );
        draw_fullscreen_texture( stage_scene_color_, stage_composite_shader_ );
        glActiveTexture( GL_TEXTURE1 );
        glBindTexture( GL_TEXTURE_2D, 0 );
        glActiveTexture( GL_TEXTURE0 );

        glDepthMask( GL_TRUE );
        glBlendEquation( GL_FUNC_ADD );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    }

    void Renderer::draw_crt_overlay( const CrtOverlaySettings &settings )
    {
        if( !initialized_ || !settings.enabled || !crt_overlay_shader_.valid() )
        {
            return;
        }

        const std::int32_t width = output_width();
        const std::int32_t height = output_height();

        if( width <= 0 || height <= 0 )
        {
            return;
        }

        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
        glViewport( 0, 0, width, height );
        glDisable( GL_DEPTH_TEST );
        glDisable( GL_CULL_FACE );
        glDepthMask( GL_FALSE );
        glEnable( GL_BLEND );
        glBlendEquation( GL_FUNC_ADD );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

        crt_overlay_shader_.use();
        crt_overlay_shader_.set_vec2( "u_screen_size", static_cast<float>( width ), static_cast<float>( height ) );

        if( const GLint loc = glGetUniformLocation( crt_overlay_shader_.program(), "u_scanline_strength" ); loc >= 0 )
        {
            glUniform1f( loc, std::max( 0.0f, settings.scanline_strength ) );
        }

        if( const GLint loc = glGetUniformLocation( crt_overlay_shader_.program(), "u_vignette_strength" ); loc >= 0 )
        {
            glUniform1f( loc, std::max( 0.0f, settings.vignette_strength ) );
        }

        if( const GLint loc = glGetUniformLocation( crt_overlay_shader_.program(), "u_noise_strength" ); loc >= 0 )
        {
            glUniform1f( loc, std::max( 0.0f, settings.noise_strength ) );
        }

        if( const GLint loc = glGetUniformLocation( crt_overlay_shader_.program(), "u_time" ); loc >= 0 )
        {
            glUniform1f( loc, settings.time_seconds );
        }

        draw_fullscreen_pass( crt_overlay_shader_ );

        glDepthMask( GL_TRUE );
    }

    void Renderer::reset_frame_stats()
    {
        frame_stats_.reset();
    }

    const RendererFrameStats &Renderer::frame_stats() const
    {
        return frame_stats_;
    }

    void Renderer::clear( float r, float g, float b, float a )
    {
        glClearColor( r, g, b, a );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
    }

    void Renderer::end_frame()
    {
        if( !initialized_ || window_ == nullptr )
        {
            return;
        }

        SDL_GL_SwapWindow( window_ );
    }

    std::int32_t Renderer::output_width() const
    {
        if( stage_post_active_ )
        {
            return post_width_;
        }

        std::int32_t w = 0;
        std::int32_t h = 0;

        if( window_ != nullptr )
        {
            SDL_GetWindowSizeInPixels( window_, &w, &h );
        }

        return w;
    }

    std::int32_t Renderer::output_height() const
    {
        if( stage_post_active_ )
        {
            return post_height_;
        }

        std::int32_t w = 0;
        std::int32_t h = 0;

        if( window_ != nullptr )
        {
            SDL_GetWindowSizeInPixels( window_, &w, &h );
        }

        return h;
    }

    void Renderer::draw_filled_rect( const Rect &rect, const Color &color )
    {
        const VertexColor2D vertices[ 4 ] = {
            { { rect.x, rect.y }, color },
            { { rect.x + rect.w, rect.y }, color },
            { { rect.x + rect.w, rect.y + rect.h }, color },
            { { rect.x, rect.y + rect.h }, color }
        };

        draw_colored_quad( vertices );
    }

    void Renderer::draw_filled_ellipse( const Rect &rect, const Color &color )
    {
        if( rect.w <= 0.0f || rect.h <= 0.0f || color.a <= 0.0f )
        {
            return;
        }

        const Vec2 center{ rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f };
        const Vec2 inner_radius{
            std::max( 0.0f, rect.w * 0.5f - ellipse_edge_width_ ),
            std::max( 0.0f, rect.h * 0.5f - ellipse_edge_width_ )
        };

        const Vec2 outer_radius{ rect.w * 0.5f + ellipse_edge_width_, rect.h * 0.5f + ellipse_edge_width_ };
        Color edge_color = color;
        edge_color.a = 0.0f;

        std::array<VertexColor2D, ellipse_segment_count_ * 9> vertices{};
        constexpr float tau = 6.283185307179586f;
        Vec2 previous{ 1.0f, 0.0f };

        for( std::size_t index = 0; index < ellipse_segment_count_; ++index )
        {
            const float angle = tau * static_cast<float>( index + 1 ) / static_cast<float>( ellipse_segment_count_ );
            const Vec2 current =
                index + 1 == ellipse_segment_count_ ? Vec2{ 1.0f, 0.0f } : Vec2{ std::cos( angle ), std::sin( angle ) };

            const Vec2 inner_previous{ center.x + previous.x * inner_radius.x, center.y + previous.y * inner_radius.y };

            const Vec2 inner_current{ center.x + current.x * inner_radius.x, center.y + current.y * inner_radius.y };

            const Vec2 outer_previous{ center.x + previous.x * outer_radius.x, center.y + previous.y * outer_radius.y };

            const Vec2 outer_current{ center.x + current.x * outer_radius.x, center.y + current.y * outer_radius.y };

            const std::size_t first = index * 9;
            vertices[ first ] = {
                center,
                color
            };

            vertices[ first + 1 ] = {
                inner_previous,
                color
            };

            vertices[ first + 2 ] = {
                inner_current,
                color
            };

            vertices[ first + 3 ] = {
                inner_previous,
                color
            };

            vertices[ first + 4 ] = {
                outer_previous,
                edge_color
            };

            vertices[ first + 5 ] = {
                outer_current,
                edge_color
            };

            vertices[ first + 6 ] = {
                inner_previous,
                color
            };

            vertices[ first + 7 ] = {
                outer_current,
                edge_color
            };

            vertices[ first + 8 ] = {
                inner_current,
                color
            };

            previous = current;
        }

        draw_colored_triangles( vertices.data(), vertices.size() );
    }

    void Renderer::draw_line( const Vec2 &a, const Vec2 &b, float thickness, const Color &color )
    {
        draw_line( a.x, a.y, b.x, b.y, thickness, color );
    }

    void Renderer::draw_line( float x0, float y0, float x1, float y1, float thickness, const Color &color )
    {
        if( thickness <= 0.0f )
        {
            return;
        }

        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float length = std::sqrt( dx * dx + dy * dy );

        if( length <= 0.0001f )
        {
            return;
        }

        const float nx = -dy / length;
        const float ny = dx / length;
        const float half = thickness * 0.5f;

        const VertexColor2D vertices[ 4 ] = {
            { { x0 + nx * half, y0 + ny * half }, color },
            { { x0 - nx * half, y0 - ny * half }, color },
            { { x1 - nx * half, y1 - ny * half }, color },
            { { x1 + nx * half, y1 + ny * half }, color }
        };

        draw_colored_quad( vertices );
    }

    void Renderer::draw_colored_triangles( const VertexColor2D *vertices, std::size_t vertex_count )
    {
        if( !initialized_ || vertices == nullptr || vertex_count == 0 )
        {
            return;
        }

        update_screen_uniforms();

        color_shader_.use();
        glBindVertexArray( color_vao_ );
        glBindBuffer( GL_ARRAY_BUFFER, color_vbo_ );
        const std::size_t buffer_vertex_count = std::max<std::size_t>( vertex_count, 4 );
        glBufferData( GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>( sizeof( VertexColor2D ) * buffer_vertex_count ),
            nullptr,
            GL_DYNAMIC_DRAW );

        glBufferSubData( GL_ARRAY_BUFFER,
            0,
            static_cast<GLsizeiptr>( sizeof( VertexColor2D ) * vertex_count ),
            vertices );

        glDrawArrays( GL_TRIANGLES, 0, static_cast<GLsizei>( vertex_count ) );
        ++frame_stats_.colored_draw_calls;
        frame_stats_.vertices += static_cast<std::uint64_t>( vertex_count );
        glBindVertexArray( 0 );
    }

    void Renderer::draw_textured_triangles( const Texture &texture,
        const VertexTexture2D *vertices,
        std::size_t vertex_count )
    {
        if( !initialized_ || !texture.valid() || vertices == nullptr || vertex_count == 0 )
        {
            return;
        }

        update_screen_uniforms();

        texture_shader_.use();
        glActiveTexture( GL_TEXTURE0 );
        glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( texture.id ) );
        apply_texture_sampling_to_bound_texture();
        glBindVertexArray( texture_vao_ );
        glBindBuffer( GL_ARRAY_BUFFER, texture_vbo_ );
        glBufferData( GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>( sizeof( VertexTexture2D ) * vertex_count ),
            vertices,
            GL_DYNAMIC_DRAW );

        glDrawArrays( GL_TRIANGLES, 0, static_cast<GLsizei>( vertex_count ) );
        ++frame_stats_.textured_draw_calls;
        frame_stats_.vertices += static_cast<std::uint64_t>( vertex_count );
        glBindVertexArray( 0 );
        glBindTexture( GL_TEXTURE_2D, 0 );
    }

    void Renderer::draw_textured_triangles_single_coverage( const Texture &texture,
        const VertexTexture2D *vertices,
        std::size_t vertex_count )
    {
        if( !initialized_ || !texture.valid() || vertices == nullptr || vertex_count == 0 )
        {
            return;
        }

        glClear( GL_STENCIL_BUFFER_BIT );
        glEnable( GL_STENCIL_TEST );
        glStencilMask( 0xFFu );
        glStencilFunc( GL_EQUAL, 0, 0xFFu );
        glStencilOp( GL_KEEP, GL_KEEP, GL_INCR );

        update_screen_uniforms();

        texture_shader_.use();
        glActiveTexture( GL_TEXTURE0 );
        glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( texture.id ) );
        apply_texture_sampling_to_bound_texture();
        glBindVertexArray( texture_vao_ );
        glBindBuffer( GL_ARRAY_BUFFER, texture_vbo_ );
        glBufferData( GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>( sizeof( VertexTexture2D ) * vertex_count ),
            vertices,
            GL_DYNAMIC_DRAW );

        glDrawArrays( GL_TRIANGLES, 0, static_cast<GLsizei>( vertex_count ) );
        ++frame_stats_.textured_draw_calls;
        frame_stats_.vertices += static_cast<std::uint64_t>( vertex_count );

        glBindVertexArray( 0 );
        glBindTexture( GL_TEXTURE_2D, 0 );

        glDisable( GL_STENCIL_TEST );
        glStencilMask( 0xFFu );
        glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
    }

    void Renderer::clear_text_cache()
    {
        for( auto &[ key, texture ] : text_cache_ )
        {
            destroy_texture( texture );
        }

        text_cache_.clear();
    }

    bool Renderer::draw_unicode_text( float x, float y, const char *text, const Color &color, float scale )
    {
        if( text_revision_ != localization::revision() )
        {
            clear_text_cache();
            body_font_.clear();
            text_revision_ = localization::revision();
        }

        if( !body_font_.loaded() && !localization::load_menu_font( body_font_, "noto-sans.regular.ttf" ) )
        {
            return false;
        }

        const std::string value( text );
        std::size_t start = 0;

        do
        {
            const auto end = value.find( '\n', start );
            const std::string line = value.substr( start, end == std::string::npos ? end : end - start );

            if( !line.empty() )
            {
                auto found = text_cache_.find( line );

                if( found == text_cache_.end() )
                {
                    if( text_cache_.size() >= 256 )
                    {
                        clear_text_cache();
                    }

                    StrokedTextStyle style{};
                    style.inner_border_pixels = 0;

                    style.outer_border_pixels = 0;

                    const auto raster = body_font_.rasterize_stroked( line, 32.0f, style );
                    Texture texture{};

                    if( !raster.valid() ||
                        !create_texture_rgba( raster.rgba.data(), raster.width, raster.height, texture ) )
                    {
                        return false;
                    }

                    found = text_cache_.emplace( line, texture ).first;
                }

                const auto &texture = found->second;
                const float factor = 8.0f * scale / 32.0f;
                draw_texture( texture,
                    x,
                    y,
                    static_cast<float>( texture.width ) * factor,
                    static_cast<float>( texture.height ) * factor,
                    color );
            }

            if( end == std::string::npos )
            {
                break;
            }

            start = end + 1;

            y += 10.0f * scale;
        } while( start < value.size() );

        return true;
    }

    void Renderer::draw_text( float x, float y, const char *text, const Color &color, float scale )
    {
        if( text == nullptr || scale <= 0.0f )
        {
            return;
        }

        text = localization::text( text );
        const bool unicode = std::any_of( text,
            text + std::char_traits<char>::length( text ),
            []( unsigned char ch )
            {
                return ch >= 128;
            } );

        if( unicode && draw_unicode_text( x, y, text, color, scale ) )
        {
            return;
        }

        const float pixel = scale;
        const float glyph_advance = 6.0f * pixel;

        std::vector<VertexColor2D> vertices;
        std::size_t lit_pixels = 0;

        for( const char *p = text; *p != '\0'; ++p )
        {
            const char ch = *p;

            if( ch != '\n' )
            {
                const std::uint8_t *rows =
                    glyph_rows( ch >= 'a' && ch <= 'z' ? static_cast<char>( ch - 'a' + 'A' ) : ch );

                for( std::int32_t row = 0; row < 7; ++row )
                {
                    const std::uint8_t bits = rows[ row ];

                    for( std::int32_t col = 0; col < 5; ++col )
                    {
                        if( ( bits & ( 1u << ( 4 - col ) ) ) != 0 )
                        {
                            ++lit_pixels;
                        }
                    }
                }
            }
        }

        if( lit_pixels == 0 )
        {
            return;
        }

        vertices.reserve( lit_pixels * 6 );

        float cursor_x = x;
        float cursor_y = y;

        const auto append_quad = [ & ]( float qx, float qy )
        {
            const float x0 = qx;
            const float y0 = qy;
            const float x1 = qx + pixel;
            const float y1 = qy + pixel;

            vertices.push_back( VertexColor2D{ { x0, y0 }, color } );
            vertices.push_back( VertexColor2D{ { x1, y0 }, color } );
            vertices.push_back( VertexColor2D{ { x1, y1 }, color } );
            vertices.push_back( VertexColor2D{ { x0, y0 }, color } );
            vertices.push_back( VertexColor2D{ { x1, y1 }, color } );
            vertices.push_back( VertexColor2D{ { x0, y1 }, color } );
        };

        for( const char *p = text; *p != '\0'; ++p )
        {
            const char ch = *p;

            if( ch == '\n' )
            {
                cursor_x = x;

                cursor_y += 8.0f * pixel;
                continue;
            }

            const std::uint8_t *rows = glyph_rows( ch >= 'a' && ch <= 'z' ? static_cast<char>( ch - 'a' + 'A' ) : ch );

            for( std::int32_t row = 0; row < 7; ++row )
            {
                const std::uint8_t bits = rows[ row ];

                for( std::int32_t col = 0; col < 5; ++col )
                {
                    if( ( bits & ( 1u << ( 4 - col ) ) ) == 0 )
                    {
                        continue;
                    }

                    append_quad( cursor_x + static_cast<float>( col ) * pixel,
                        cursor_y + static_cast<float>( row ) * pixel );
                }
            }

            cursor_x += glyph_advance;
        }

        ++frame_stats_.text_draw_calls;
        frame_stats_.glyph_quads += static_cast<std::uint64_t>( lit_pixels );
        draw_colored_triangles( vertices.data(), vertices.size() );
    }

    bool Renderer::create_texture_rgba( const void *pixels,
        std::int32_t width,
        std::int32_t height,
        Texture &out_texture )
    {
        out_texture = {};

        if( pixels == nullptr || width <= 0 || height <= 0 )
        {
            return false;
        }

        GLuint texture_id = 0;
        glGenTextures( 1, &texture_id );

        if( texture_id == 0 )
        {
            return false;
        }

        glBindTexture( GL_TEXTURE_2D, texture_id );
        glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels );

        glGenerateMipmap( GL_TEXTURE_2D );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        apply_texture_sampling_to_bound_texture();

        glBindTexture( GL_TEXTURE_2D, 0 );

        out_texture.id = texture_id;

        out_texture.width = width;

        out_texture.height = height;
        return true;
    }

    bool Renderer::update_texture_rgba( Texture &texture, const void *pixels, std::int32_t width, std::int32_t height )
    {
        if( pixels == nullptr || width <= 0 || height <= 0 )
        {
            return false;
        }

        if( !texture.valid() || texture.width != width || texture.height != height )
        {
            destroy_texture( texture );
            return create_texture_rgba( pixels, width, height, texture );
        }

        glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( texture.id ) );
        glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels );
        glGenerateMipmap( GL_TEXTURE_2D );
        apply_texture_sampling_to_bound_texture();
        glBindTexture( GL_TEXTURE_2D, 0 );
        return true;
    }

    void Renderer::destroy_texture( Texture &texture )
    {
        if( texture.id != 0 )
        {
            const GLuint texture_id = static_cast<GLuint>( texture.id );
            glDeleteTextures( 1, &texture_id );
        }

        texture = {};
    }

    void Renderer::draw_texture( const Texture &texture,
        float x,
        float y,
        float width,
        float height,
        const Color &tint )
    {
        draw_texture_region( texture,
            0.0f,
            0.0f,
            static_cast<float>( texture.width ),
            static_cast<float>( texture.height ),
            x,
            y,
            width,
            height,
            tint );
    }

    void Renderer::draw_texture_opaque( const Texture &texture, float x, float y, float width, float height )
    {
        glBlendFunc( GL_ONE, GL_ZERO );
        draw_texture( texture, x, y, width, height );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    }

    void Renderer::draw_texture_alpha_mask( const Texture &texture, float x, float y, float width, float height )
    {
        draw_filled_rect( Rect{ x, y, width, height }, Color{ 1.0f, 1.0f, 1.0f, 1.0f } );
        glBlendFunc( GL_ZERO, GL_SRC_ALPHA );
        draw_texture( texture, x, y, width, height );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    }

    void Renderer::draw_texture_rotated( const Texture &texture,
        float origin_x,
        float origin_y,
        float width,
        float height,
        float anchor_x,
        float anchor_y,
        float angle_degrees,
        const Color &tint )
    {
        draw_texture_region_rotated( texture,
            0.0f,
            0.0f,
            static_cast<float>( texture.width ),
            static_cast<float>( texture.height ),
            origin_x,
            origin_y,
            width,
            height,
            anchor_x,
            anchor_y,
            angle_degrees,
            tint );
    }

    void Renderer::draw_texture_region( const Texture &texture,
        float src_x,
        float src_y,
        float src_w,
        float src_h,
        float dst_x,
        float dst_y,
        float dst_w,
        float dst_h,
        const Color &tint )
    {
        if( !texture.valid() || texture.width <= 0 || texture.height <= 0 || dst_w <= 0.0f || dst_h <= 0.0f )
        {
            return;
        }

        const float inv_w = 1.0f / static_cast<float>( texture.width );
        const float inv_h = 1.0f / static_cast<float>( texture.height );

        const float u0 = src_x * inv_w;
        const float v0 = src_y * inv_h;
        const float u1 = ( src_x + src_w ) * inv_w;
        const float v1 = ( src_y + src_h ) * inv_h;

        const VertexTexture2D vertices[ 4 ] = {
            { { dst_x, dst_y }, { u0, v0 }, tint },
            { { dst_x + dst_w, dst_y }, { u1, v0 }, tint },
            { { dst_x + dst_w, dst_y + dst_h }, { u1, v1 }, tint },
            { { dst_x, dst_y + dst_h }, { u0, v1 }, tint }
        };

        glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( texture.id ) );
        apply_texture_sampling_to_bound_texture();
        draw_textured_quad( vertices );
        glBindTexture( GL_TEXTURE_2D, 0 );
    }

    void Renderer::draw_texture_region_rotated( const Texture &texture,
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
        const Color &tint )
    {
        if( !texture.valid() || texture.width <= 0 || texture.height <= 0 || dst_w <= 0.0f || dst_h <= 0.0f )
        {
            return;
        }

        const float inv_w = 1.0f / static_cast<float>( texture.width );
        const float inv_h = 1.0f / static_cast<float>( texture.height );

        const float u0 = src_x * inv_w;
        const float v0 = src_y * inv_h;
        const float u1 = ( src_x + src_w ) * inv_w;
        const float v1 = ( src_y + src_h ) * inv_h;

        anchor_x = std::clamp( anchor_x, 0.0f, 1.0f );

        anchor_y = std::clamp( anchor_y, 0.0f, 1.0f );

        constexpr float degrees_to_radians = 0.017453292519943295f;
        const float radians = angle_degrees * degrees_to_radians;
        const float c = std::cos( radians );
        const float s = std::sin( radians );

        const float left = -anchor_x * dst_w;
        const float right = ( 1.0f - anchor_x ) * dst_w;
        const float top = -anchor_y * dst_h;
        const float bottom = ( 1.0f - anchor_y ) * dst_h;

        const auto rotate_point = [ & ]( float x, float y ) -> Vec2
        {
            return Vec2{ origin_x + x * c - y * s, origin_y + x * s + y * c };
        };

        const VertexTexture2D vertices[ 4 ] = {
            { rotate_point( left, top ), { u0, v0 }, tint },
            { rotate_point( right, top ), { u1, v0 }, tint },
            { rotate_point( right, bottom ), { u1, v1 }, tint },
            { rotate_point( left, bottom ), { u0, v1 }, tint }
        };

        glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( texture.id ) );
        apply_texture_sampling_to_bound_texture();
        draw_textured_quad( vertices );
        glBindTexture( GL_TEXTURE_2D, 0 );
    }

    void Renderer::draw_texture_region_rotated_additive( const Texture &texture,
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
        const Color &tint )
    {
        glBlendFunc( GL_SRC_ALPHA, GL_ONE );
        draw_texture_region_rotated( texture,
            src_x,
            src_y,
            src_w,
            src_h,
            origin_x,
            origin_y,
            dst_w,
            dst_h,
            anchor_x,
            anchor_y,
            angle_degrees,
            tint );

        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    }

    std::filesystem::path Renderer::shader_path( const char *filename ) const
    {
        if( filename == nullptr || *filename == '\0' )
        {
            return {};
        }

        const std::filesystem::path relative = std::filesystem::path( filename ).lexically_normal();

        if( relative.empty() || relative.is_absolute() )
        {
            return {};
        }

        const std::filesystem::path root =
            shader_root_.empty()
                ? ( std::filesystem::current_path() / "GAME_DATA" / "PIKO" / "shaders" ).lexically_normal()
                : shader_root_;

        return ( root / relative ).lexically_normal();
    }

    bool Renderer::initialize_shaders()
    {
        std::string error;

        const auto load_shader =
            [ & ]( Shader &shader, const char *label, const char *vertex_file, const char *fragment_file ) -> bool
        {
            const std::filesystem::path vertex_path = shader_path( vertex_file );
            const std::filesystem::path fragment_path = shader_path( fragment_file );

            if( !shader.create_from_files( vertex_path, fragment_path, &error ) )
            {
                std::cerr << label << " shader failed: " << error << '\n'
                          << "  vertex: " << vertex_path.string() << '\n'
                          << "  fragment: " << fragment_path.string() << '\n';

                return false;
            }

            return true;
        };

        if( !load_shader( color_shader_, "2D color", "renderer_color.vert", "renderer_color.frag" ) )
        {
            return false;
        }

        if( !load_shader( texture_shader_, "2D texture", "renderer_texture.vert", "renderer_texture.frag" ) )
        {
            return false;
        }

        texture_shader_.use();
        texture_shader_.set_int( "u_texture", 0 );

        if( !load_shader( bloom_extract_shader_, "Bloom extract", "fullscreen.vert", "bloom_extract.frag" ) )
        {
            return false;
        }

        if( !load_shader( bloom_blur_shader_, "Bloom blur", "fullscreen.vert", "bloom_blur.frag" ) )
        {
            return false;
        }

        if( !load_shader( stage_composite_shader_, "Stage composite", "fullscreen.vert", "stage_composite.frag" ) )
        {
            return false;
        }

        if( !load_shader( crt_overlay_shader_, "CRT overlay", "fullscreen.vert", "crt_overlay.frag" ) )
        {
            return false;
        }

        return true;
    }

    bool Renderer::initialize_geometry()
    {
        glGenVertexArrays( 1, &color_vao_ );
        glGenBuffers( 1, &color_vbo_ );
        glGenBuffers( 1, &color_ebo_ );

        glBindVertexArray( color_vao_ );
        glBindBuffer( GL_ARRAY_BUFFER, color_vbo_ );
        glBufferData( GL_ARRAY_BUFFER, sizeof( VertexColor2D ) * 4, nullptr, GL_DYNAMIC_DRAW );
        glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, color_ebo_ );
        glBufferData( GL_ELEMENT_ARRAY_BUFFER, sizeof( quad_indices_ ), quad_indices_, GL_STATIC_DRAW );

        glEnableVertexAttribArray( 0 );
        glVertexAttribPointer( 0,
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof( VertexColor2D ),
            reinterpret_cast<void *>( offsetof( VertexColor2D, position ) ) );

        glEnableVertexAttribArray( 1 );
        glVertexAttribPointer( 1,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof( VertexColor2D ),
            reinterpret_cast<void *>( offsetof( VertexColor2D, color ) ) );

        glBindVertexArray( 0 );

        glGenVertexArrays( 1, &texture_vao_ );
        glGenBuffers( 1, &texture_vbo_ );
        glGenBuffers( 1, &texture_ebo_ );

        glBindVertexArray( texture_vao_ );
        glBindBuffer( GL_ARRAY_BUFFER, texture_vbo_ );
        glBufferData( GL_ARRAY_BUFFER, sizeof( VertexTexture2D ) * 4, nullptr, GL_DYNAMIC_DRAW );
        glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, texture_ebo_ );
        glBufferData( GL_ELEMENT_ARRAY_BUFFER, sizeof( quad_indices_ ), quad_indices_, GL_STATIC_DRAW );

        glEnableVertexAttribArray( 0 );
        glVertexAttribPointer( 0,
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof( VertexTexture2D ),
            reinterpret_cast<void *>( offsetof( VertexTexture2D, position ) ) );

        glEnableVertexAttribArray( 1 );
        glVertexAttribPointer( 1,
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof( VertexTexture2D ),
            reinterpret_cast<void *>( offsetof( VertexTexture2D, uv ) ) );

        glEnableVertexAttribArray( 2 );
        glVertexAttribPointer( 2,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof( VertexTexture2D ),
            reinterpret_cast<void *>( offsetof( VertexTexture2D, color ) ) );

        glBindVertexArray( 0 );

        static constexpr float fullscreen_vertices[] = {
            -1.0f,
            -1.0f,
            0.0f,
            0.0f,
            1.0f,
            -1.0f,
            1.0f,
            0.0f,
            -1.0f,
            1.0f,
            0.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f
        };

        glGenVertexArrays( 1, &fullscreen_vao_ );
        glGenBuffers( 1, &fullscreen_vbo_ );
        glBindVertexArray( fullscreen_vao_ );
        glBindBuffer( GL_ARRAY_BUFFER, fullscreen_vbo_ );
        glBufferData( GL_ARRAY_BUFFER, sizeof( fullscreen_vertices ), fullscreen_vertices, GL_STATIC_DRAW );
        glEnableVertexAttribArray( 0 );
        glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, sizeof( float ) * 4, reinterpret_cast<void *>( 0 ) );
        glEnableVertexAttribArray( 1 );
        glVertexAttribPointer( 1,
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof( float ) * 4,
            reinterpret_cast<void *>( sizeof( float ) * 2 ) );

        glBindVertexArray( 0 );

        return color_vao_ != 0 && color_vbo_ != 0 && color_ebo_ != 0 && texture_vao_ != 0 && texture_vbo_ != 0 &&
               texture_ebo_ != 0 && fullscreen_vao_ != 0 && fullscreen_vbo_ != 0;
    }

    bool Renderer::initialize_post_processing()
    {

        return true;
    }

    void Renderer::shutdown_geometry()
    {
        if( fullscreen_vbo_ != 0 )
        {
            glDeleteBuffers( 1, &fullscreen_vbo_ );
            fullscreen_vbo_ = 0;
        }

        if( fullscreen_vao_ != 0 )
        {
            glDeleteVertexArrays( 1, &fullscreen_vao_ );
            fullscreen_vao_ = 0;
        }

        if( color_ebo_ != 0 )
        {
            glDeleteBuffers( 1, &color_ebo_ );
            color_ebo_ = 0;
        }

        if( color_vbo_ != 0 )
        {
            glDeleteBuffers( 1, &color_vbo_ );
            color_vbo_ = 0;
        }

        if( color_vao_ != 0 )
        {
            glDeleteVertexArrays( 1, &color_vao_ );
            color_vao_ = 0;
        }

        if( texture_ebo_ != 0 )
        {
            glDeleteBuffers( 1, &texture_ebo_ );
            texture_ebo_ = 0;
        }

        if( texture_vbo_ != 0 )
        {
            glDeleteBuffers( 1, &texture_vbo_ );
            texture_vbo_ = 0;
        }

        if( texture_vao_ != 0 )
        {
            glDeleteVertexArrays( 1, &texture_vao_ );
            texture_vao_ = 0;
        }
    }

    void Renderer::shutdown_post_processing()
    {
        if( stage_msaa_color_ != 0 )
        {
            glDeleteRenderbuffers( 1, &stage_msaa_color_ );
            stage_msaa_color_ = 0;
        }

        if( stage_msaa_depth_stencil_ != 0 )
        {
            glDeleteRenderbuffers( 1, &stage_msaa_depth_stencil_ );
            stage_msaa_depth_stencil_ = 0;
        }

        if( stage_msaa_fbo_ != 0 )
        {
            glDeleteFramebuffers( 1, &stage_msaa_fbo_ );
            stage_msaa_fbo_ = 0;
        }

        if( stage_scene_depth_stencil_ != 0 )
        {
            glDeleteRenderbuffers( 1, &stage_scene_depth_stencil_ );
            stage_scene_depth_stencil_ = 0;
        }

        if( stage_scene_color_ != 0 )
        {
            glDeleteTextures( 1, &stage_scene_color_ );
            stage_scene_color_ = 0;
        }

        for( GLuint &texture : bloom_color_ )
        {
            if( texture != 0 )
            {
                glDeleteTextures( 1, &texture );
                texture = 0;
            }
        }

        for( GLuint &fbo : bloom_fbo_ )
        {
            if( fbo != 0 )
            {
                glDeleteFramebuffers( 1, &fbo );
                fbo = 0;
            }
        }

        if( stage_scene_fbo_ != 0 )
        {
            glDeleteFramebuffers( 1, &stage_scene_fbo_ );
            stage_scene_fbo_ = 0;
        }

        post_width_ = 0;

        post_height_ = 0;

        post_msaa_samples_ = 0;

        stage_post_active_ = false;
    }

    void Renderer::apply_texture_sampling_to_bound_texture() const
    {
        switch( texture_sampling_.mode )
        {
            case TextureFilterMode::Nearest:
                glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
                glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
                break;
            case TextureFilterMode::Linear:
                glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
                glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
                break;
            case TextureFilterMode::LinearMipmap:
                glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
                glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
                break;
            case TextureFilterMode::Anisotropic:
                glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
                glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

                if( max_texture_anisotropy_ > 1.0f )
                {
                    glTexParameterf( GL_TEXTURE_2D,
                        GL_TEXTURE_MAX_ANISOTROPY_EXT,
                        std::clamp( texture_sampling_.anisotropy, 1.0f, max_texture_anisotropy_ ) );
                }
                break;
        }
    }

    bool Renderer::ensure_post_resources( std::int32_t width, std::int32_t height, std::int32_t samples )
    {
        if( width <= 0 || height <= 0 )
        {
            return false;
        }

        if( stage_scene_fbo_ != 0 && width == post_width_ && height == post_height_ && samples == post_msaa_samples_ )
        {
            return true;
        }

        shutdown_post_processing();

        post_width_ = width;

        post_height_ = height;

        post_msaa_samples_ = samples;
        const std::int32_t bloom_width = std::max( 1, width / 2 );
        const std::int32_t bloom_height = std::max( 1, height / 2 );

        glGenFramebuffers( 1, &stage_scene_fbo_ );
        glBindFramebuffer( GL_FRAMEBUFFER, stage_scene_fbo_ );

        glGenTextures( 1, &stage_scene_color_ );
        glBindTexture( GL_TEXTURE_2D, stage_scene_color_ );
        glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, stage_scene_color_, 0 );

        glGenRenderbuffers( 1, &stage_scene_depth_stencil_ );
        glBindRenderbuffer( GL_RENDERBUFFER, stage_scene_depth_stencil_ );
        glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height );
        glFramebufferRenderbuffer( GL_FRAMEBUFFER,
            GL_DEPTH_STENCIL_ATTACHMENT,
            GL_RENDERBUFFER,
            stage_scene_depth_stencil_ );

        if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
        {
            std::cerr << "Renderer post-process scene FBO incomplete.\n";
            shutdown_post_processing();
            glBindFramebuffer( GL_FRAMEBUFFER, 0 );
            return false;
        }

        if( samples >= 2 )
        {
            glGenFramebuffers( 1, &stage_msaa_fbo_ );
            glBindFramebuffer( GL_FRAMEBUFFER, stage_msaa_fbo_ );
            glGenRenderbuffers( 1, &stage_msaa_color_ );
            glBindRenderbuffer( GL_RENDERBUFFER, stage_msaa_color_ );
            glRenderbufferStorageMultisample( GL_RENDERBUFFER, samples, GL_RGBA16F, width, height );
            glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, stage_msaa_color_ );
            glGenRenderbuffers( 1, &stage_msaa_depth_stencil_ );
            glBindRenderbuffer( GL_RENDERBUFFER, stage_msaa_depth_stencil_ );
            glRenderbufferStorageMultisample( GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height );
            glFramebufferRenderbuffer( GL_FRAMEBUFFER,
                GL_DEPTH_STENCIL_ATTACHMENT,
                GL_RENDERBUFFER,
                stage_msaa_depth_stencil_ );

            if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
            {
                std::cerr << "Renderer multisampled scene FBO incomplete.\n";
                shutdown_post_processing();
                glBindFramebuffer( GL_FRAMEBUFFER, 0 );
                return false;
            }
        }

        glGenFramebuffers( 2, bloom_fbo_ );
        glGenTextures( 2, bloom_color_ );

        for( std::int32_t i = 0; i < 2; ++i )
        {
            glBindFramebuffer( GL_FRAMEBUFFER, bloom_fbo_[ i ] );
            glBindTexture( GL_TEXTURE_2D, bloom_color_[ i ] );
            glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA16F, bloom_width, bloom_height, 0, GL_RGBA, GL_FLOAT, nullptr );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
            glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloom_color_[ i ], 0 );

            if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
            {
                std::cerr << "Renderer bloom FBO incomplete.\n";
                shutdown_post_processing();
                glBindFramebuffer( GL_FRAMEBUFFER, 0 );
                return false;
            }
        }

        glBindTexture( GL_TEXTURE_2D, 0 );
        glBindRenderbuffer( GL_RENDERBUFFER, 0 );
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
        return true;
    }

    void Renderer::draw_fullscreen_texture( GLuint texture, const Shader &shader )
    {
        glActiveTexture( GL_TEXTURE0 );
        glBindTexture( GL_TEXTURE_2D, texture );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        draw_fullscreen_pass( shader );
        glBindTexture( GL_TEXTURE_2D, 0 );
    }

    void Renderer::draw_fullscreen_pass( const Shader &shader )
    {
        if( !shader.valid() || fullscreen_vao_ == 0 )
        {
            return;
        }

        shader.use();
        glBindVertexArray( fullscreen_vao_ );
        glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
        glBindVertexArray( 0 );
    }

    void Renderer::update_screen_uniforms()
    {
        const float width = static_cast<float>( std::max( output_width(), 1 ) );
        const float height = static_cast<float>( std::max( output_height(), 1 ) );

        color_shader_.use();
        color_shader_.set_vec2( "u_screen_size", width, height );

        texture_shader_.use();
        texture_shader_.set_vec2( "u_screen_size", width, height );
    }

    void Renderer::draw_colored_quad( const VertexColor2D *vertices )
    {
        if( !initialized_ || vertices == nullptr )
        {
            return;
        }

        update_screen_uniforms();

        color_shader_.use();
        glBindVertexArray( color_vao_ );
        glBindBuffer( GL_ARRAY_BUFFER, color_vbo_ );
        glBufferSubData( GL_ARRAY_BUFFER, 0, sizeof( VertexColor2D ) * 4, vertices );
        glDrawElements( GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr );
        ++frame_stats_.colored_draw_calls;
        frame_stats_.vertices += 4;
        glBindVertexArray( 0 );
    }

    void Renderer::draw_textured_quad( const VertexTexture2D *vertices )
    {
        if( !initialized_ || vertices == nullptr )
        {
            return;
        }

        update_screen_uniforms();

        texture_shader_.use();
        glActiveTexture( GL_TEXTURE0 );
        glBindVertexArray( texture_vao_ );
        glBindBuffer( GL_ARRAY_BUFFER, texture_vbo_ );
        glBufferSubData( GL_ARRAY_BUFFER, 0, sizeof( VertexTexture2D ) * 4, vertices );
        glDrawElements( GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr );
        ++frame_stats_.textured_draw_calls;
        frame_stats_.vertices += 4;
        glBindVertexArray( 0 );
    }
}
