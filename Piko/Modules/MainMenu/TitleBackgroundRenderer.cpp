#include "TitleBackgroundRenderer.hpp"

#include "App/piko.hpp"
#include "GameData/GameDataRoot.hpp"
#include "Rendering/Renderer.hpp"
#include "Utils/FileUtil.hpp"
#include "Utils/Json.hpp"

#include <cstdint>
#include <glad/gl.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>

namespace piko::modules
{
    TitleBackgroundRenderer::~TitleBackgroundRenderer()
    {
        shutdown();
    }

    bool TitleBackgroundRenderer::initialize( App &app, const char *settings_path )
    {
        shutdown();
        last_error_.clear();

        const std::filesystem::path config_path = app.game_data().resolve_resource_path(
            settings_path != nullptr ? settings_path : "ui/title/title_background.json" );

        if( !load_settings( config_path ) && settings_path != nullptr )
        {
            last_error_ = "Missing or invalid background config: " + config_path.string();
            return false;
        }

        if( !settings_.enabled )
        {
            last_error_ = "Title topology background disabled by config.";
            return false;
        }

        const std::filesystem::path vertex_path =
            app.game_data().resolve_resource_path( normalize_shader_relative_path( settings_.vertex_shader ) );

        const std::filesystem::path fragment_path =
            app.game_data().resolve_resource_path( normalize_shader_relative_path( settings_.fragment_shader ) );

        if( !piko::utils::regular_file( vertex_path ) || !piko::utils::regular_file( fragment_path ) )
        {
            last_error_ = "Missing title background shader files. Expected: " + vertex_path.string() + " and " +
                          fragment_path.string();

            std::cerr << last_error_ << '\n';
            return false;
        }

        std::string shader_error;

        if( !shader_.create_from_files( vertex_path, fragment_path, &shader_error ) )
        {
            last_error_ = shader_error;
            std::cerr << "Title background shader failed: " << last_error_ << '\n';
            return false;
        }

        if( !initialize_geometry() )
        {
            last_error_ = "Could not create title background fullscreen geometry.";
            shader_.destroy();
            return false;
        }

        initialized_ = true;
        return true;
    }

    void TitleBackgroundRenderer::shutdown()
    {
        destroy_geometry();
        shader_.destroy();
        initialized_ = false;
    }

    void TitleBackgroundRenderer::set_palette( const rendering::Color &base,
        const rendering::Color &fill,
        const rendering::Color &line )
    {
        settings_.base_color[ 0 ] = base.r;

        settings_.base_color[ 1 ] = base.g;

        settings_.base_color[ 2 ] = base.b;

        settings_.fill_color[ 0 ] = fill.r;

        settings_.fill_color[ 1 ] = fill.g;

        settings_.fill_color[ 2 ] = fill.b;

        settings_.line_color[ 0 ] = line.r;

        settings_.line_color[ 1 ] = line.g;

        settings_.line_color[ 2 ] = line.b;
    }

    bool TitleBackgroundRenderer::render( rendering::Renderer &renderer, float time_seconds )
    {
        if( !initialized_ || !settings_.enabled || !shader_.valid() || vao_ == 0 )
        {
            return false;
        }

        const std::int32_t width = renderer.output_width();
        const std::int32_t height = renderer.output_height();

        if( width <= 0 || height <= 0 )
        {
            return false;
        }

        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
        glViewport( 0, 0, width, height );
        glDisable( GL_DEPTH_TEST );
        glDepthMask( GL_FALSE );
        glDisable( GL_CULL_FACE );
        glDisable( GL_STENCIL_TEST );
        glDisable( GL_SCISSOR_TEST );
        glDisable( GL_BLEND );

        shader_.use();
        shader_.set_vec2( "u_resolution", static_cast<float>( width ), static_cast<float>( height ) );
        shader_.set_float( "u_time", time_seconds );
        shader_.set_vec3( "u_base_color",
            settings_.base_color[ 0 ],
            settings_.base_color[ 1 ],
            settings_.base_color[ 2 ] );

        shader_.set_vec3( "u_fill_color",
            settings_.fill_color[ 0 ],
            settings_.fill_color[ 1 ],
            settings_.fill_color[ 2 ] );

        shader_.set_vec3( "u_line_color",
            settings_.line_color[ 0 ],
            settings_.line_color[ 1 ],
            settings_.line_color[ 2 ] );

        if( settings_.banded_palette )
        {
            shader_.set_vec3( "u_band_color_0",
                settings_.band_colors[ 0 ][ 0 ],
                settings_.band_colors[ 0 ][ 1 ],
                settings_.band_colors[ 0 ][ 2 ] );

            shader_.set_vec3( "u_band_color_1",
                settings_.band_colors[ 1 ][ 0 ],
                settings_.band_colors[ 1 ][ 1 ],
                settings_.band_colors[ 1 ][ 2 ] );

            shader_.set_vec3( "u_band_color_2",
                settings_.band_colors[ 2 ][ 0 ],
                settings_.band_colors[ 2 ][ 1 ],
                settings_.band_colors[ 2 ][ 2 ] );

            shader_.set_vec3( "u_band_color_3",
                settings_.band_colors[ 3 ][ 0 ],
                settings_.band_colors[ 3 ][ 1 ],
                settings_.band_colors[ 3 ][ 2 ] );
        }

        shader_.set_float( "u_speed", settings_.speed );
        shader_.set_float( "u_contour_scale", settings_.contour_scale );
        shader_.set_float( "u_contour_count", settings_.contour_count );
        shader_.set_float( "u_line_width", settings_.line_width );
        shader_.set_float( "u_line_softness", settings_.line_softness );
        shader_.set_float( "u_line_alpha", settings_.line_alpha );
        shader_.set_float( "u_glow", settings_.glow );
        shader_.set_float( "u_vignette", settings_.vignette );
        shader_.set_float( "u_grain", settings_.grain );

        glBindVertexArray( vao_ );
        glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
        glBindVertexArray( 0 );
        glUseProgram( 0 );

        glDepthMask( GL_TRUE );
        glEnable( GL_BLEND );
        glBlendEquation( GL_FUNC_ADD );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

        return true;
    }

    bool TitleBackgroundRenderer::valid() const
    {
        return initialized_ && shader_.valid();
    }

    const std::string &TitleBackgroundRenderer::last_error() const
    {
        return last_error_;
    }

    bool TitleBackgroundRenderer::load_settings( const std::filesystem::path &path )
    {
        settings_.banded_palette = false;

        if( path.empty() || !piko::utils::regular_file( path ) )
        {
            return false;
        }

        try
        {
            const piko::utils::json::Value root = piko::utils::json::load_file( path );

            if( !root.is_object() )
            {
                return false;
            }

            if( const auto *value = root.find( "enabled" ) )
            {
                settings_.enabled = value->boolean_or( settings_.enabled );
            }

            if( const auto *value = root.find( "vertex_shader" ) )
            {
                settings_.vertex_shader = value->string_or( settings_.vertex_shader );
            }

            if( const auto *value = root.find( "fragment_shader" ) )
            {
                settings_.fragment_shader = value->string_or( settings_.fragment_shader );
            }

            const auto load_color = [ & ]( const char *key, float out[ 3 ] )
            {
                const auto *value = root.find( key );

                if( value == nullptr || !value->is_array() || value->array().size() < 3u )
                {
                    return;
                }

                for( std::size_t i = 0; i < 3u; ++i )
                {
                    out[ i ] = static_cast<float>( value->array()[ i ].number_or( out[ i ] ) );
                }
            };

            const auto load_float = [ & ]( const char *key, float &out )
            {
                if( const auto *value = root.find( key ) )
                {
                    out = static_cast<float>( value->number_or( out ) );
                }
            };

            load_color( "base_color", settings_.base_color );
            load_color( "fill_color", settings_.fill_color );
            load_color( "line_color", settings_.line_color );

            if( const auto *bands = root.find( "band_colors" );
                bands != nullptr && bands->is_array() && bands->array().size() == 4u )
            {
                bool valid = true;

                for( std::size_t band = 0; band < 4u; ++band )
                {
                    const auto &entry = bands->array()[ band ];

                    if( !entry.is_array() || entry.array().size() != 3u )
                    {
                        valid = false;
                        break;
                    }

                    for( std::size_t channel = 0; channel < 3u; ++channel )
                    {
                        settings_.band_colors[ band ][ channel ] =
                            std::clamp( static_cast<float>( entry.array()[ channel ].number_or( 0.0 ) ), 0.0f, 1.0f );
                    }
                }

                settings_.banded_palette = valid;
            }

            load_float( "speed", settings_.speed );
            load_float( "contour_scale", settings_.contour_scale );
            load_float( "contour_count", settings_.contour_count );
            load_float( "line_width", settings_.line_width );
            load_float( "line_softness", settings_.line_softness );
            load_float( "line_alpha", settings_.line_alpha );
            load_float( "glow", settings_.glow );
            load_float( "vignette", settings_.vignette );
            load_float( "grain", settings_.grain );
        }
        catch( const std::exception & )
        {
            return false;
        }

        settings_.speed = std::clamp( settings_.speed, -2.0f, 2.0f );

        settings_.contour_scale = std::clamp( settings_.contour_scale, 0.25f, 20.0f );

        settings_.contour_count = std::clamp( settings_.contour_count, 1.0f, 80.0f );

        settings_.line_width = std::clamp( settings_.line_width, 0.001f, 0.45f );

        settings_.line_softness = std::clamp( settings_.line_softness, 0.0001f, 0.35f );

        settings_.line_alpha = std::clamp( settings_.line_alpha, 0.0f, 1.0f );

        settings_.glow = std::clamp( settings_.glow, 0.0f, 2.0f );

        settings_.vignette = std::clamp( settings_.vignette, 0.0f, 1.0f );

        settings_.grain = std::clamp( settings_.grain, 0.0f, 0.25f );

        return true;
    }

    std::string TitleBackgroundRenderer::normalize_shader_relative_path( const std::string &path )
    {
        std::string normalized;
        normalized.reserve( path.size() );

        for( const char c : path )
        {
            normalized.push_back( c == '\\' ? '/' : c );
        }

        while( !normalized.empty() && normalized.front() == '/' )
        {
            normalized.erase( normalized.begin() );
        }

        return normalized;
    }

    bool TitleBackgroundRenderer::initialize_geometry()
    {
        const FullscreenVertex vertices[ 4 ] = {
            { -1.0f, -1.0f, 0.0f, 0.0f },
            { 1.0f, -1.0f, 1.0f, 0.0f },
            { -1.0f, 1.0f, 0.0f, 1.0f },
            { 1.0f, 1.0f, 1.0f, 1.0f }
        };

        glGenVertexArrays( 1, &vao_ );
        glGenBuffers( 1, &vbo_ );

        if( vao_ == 0 || vbo_ == 0 )
        {
            destroy_geometry();
            return false;
        }

        glBindVertexArray( vao_ );
        glBindBuffer( GL_ARRAY_BUFFER, vbo_ );
        glBufferData( GL_ARRAY_BUFFER, sizeof( vertices ), vertices, GL_STATIC_DRAW );

        glEnableVertexAttribArray( 0 );
        glVertexAttribPointer( 0,
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof( FullscreenVertex ),
            reinterpret_cast<void *>( offsetof( FullscreenVertex, x ) ) );

        glEnableVertexAttribArray( 1 );
        glVertexAttribPointer( 1,
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof( FullscreenVertex ),
            reinterpret_cast<void *>( offsetof( FullscreenVertex, u ) ) );

        glBindVertexArray( 0 );
        glBindBuffer( GL_ARRAY_BUFFER, 0 );
        return true;
    }

    void TitleBackgroundRenderer::destroy_geometry()
    {
        if( vbo_ != 0 )
        {
            glDeleteBuffers( 1, &vbo_ );
            vbo_ = 0;
        }

        if( vao_ != 0 )
        {
            glDeleteVertexArrays( 1, &vao_ );
            vao_ = 0;
        }
    }
}
