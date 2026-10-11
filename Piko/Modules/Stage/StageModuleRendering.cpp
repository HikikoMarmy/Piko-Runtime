#include "Modules/Stage/StageModuleRendering.hpp"
#include "Modules/Stage/StagePhraseRenderer.hpp"
#include "Modules/Stage/StageModuleConstants.hpp"
#include "Runtime/Stage/StageGameplayInterfaceAtlas.hpp"
#include "Utils/FileUtil.hpp"

#include <cstdint>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <SDL3/SDL.h>

namespace piko::modules::stage
{
    rendering::Vec2 gameplay_center( const rendering::Rect &area, const rendering::Vec2 &offset )
    {
        const float scale =
            std::max( 0.001f, std::min( area.w / gameplay_logical_width, area.h / gameplay_logical_height ) );

        return { area.x + area.w * 0.5f + offset.x * scale, area.y + area.h * 0.5f + offset.y * scale };
    }

    ui::BattleImage load_battle_image( const std::filesystem::path &path, ui::BattleImage fallback )
    {
        if( !utils::regular_file( path ) )
        {
            return fallback;
        }

        using Surface = std::unique_ptr<SDL_Surface, decltype( &SDL_DestroySurface )>;
        const Surface loaded( SDL_LoadPNG( utils::path_to_utf8( path ).c_str() ), SDL_DestroySurface );
        const Surface rgba( loaded ? SDL_ConvertSurface( loaded.get(), SDL_PIXELFORMAT_RGBA32 ) : nullptr,
            SDL_DestroySurface );

        if( !rgba || rgba->w <= 0 || rgba->h <= 0 || rgba->w > 4096 || rgba->h > 4096 )
        {
            std::cerr << "[Piko UI] Could not load note image: " << path << '\n';
            return fallback;
        }

        ui::BattleImage image{
            rgba->w,
            rgba->h,
            std::vector<std::uint8_t>( static_cast<std::size_t>( rgba->w ) * rgba->h * 4u )
        };

        const auto *pixels = static_cast<const std::uint8_t *>( rgba->pixels );

        for( std::int32_t row = 0; row < image.height; ++row )
        {
            std::copy_n( pixels + static_cast<std::size_t>( row ) * rgba->pitch,
                static_cast<std::size_t>( image.width ) * 4u,
                image.pixels.data() + static_cast<std::size_t>( row ) * image.width * 4u );
        }

        return image;
    }

    std::filesystem::path note_style_directory( const std::filesystem::path &game_data_root, std::string_view style )
    {
        const auto selected = utils::path_from_utf8( style );
        const bool valid = !selected.empty() && !selected.has_parent_path() && selected != "." && selected != "..";
        return game_data_root / "PIKO/ui/battle/notes" / ( valid ? selected : std::filesystem::path( "default" ) );
    }

    managers::TextureHandle load_note_textures( managers::TextureManager &textures,
        const std::filesystem::path &game_data_root,
        ui::NoteTextureRegions &regions,
        const char *name,
        std::string_view style )
    {
        const auto directory = note_style_directory( game_data_root, style );
        auto images = ui::generated_note_images();
        std::array<ui::BattleImage, 3> size_reference;
        const std::array<const char *, 3> filenames{
            "ribbon.png",
            "cap.png",
            "note.png"
        };

        for( std::size_t index = 0; index < images.size(); ++index )
        {
            size_reference[ index ] =
                load_battle_image( game_data_root / "PIKO/ui/battle/notes/default" / filenames[ index ], {} );

            if( size_reference[ index ].pixels.empty() && style == "Piko" )
            {
                size_reference[ index ] = images[ index ];
            }

            if( style != "Piko" )
            {
                images[ index ] = size_reference[ index ];
            }

            images[ index ] = load_battle_image( directory / filenames[ index ], std::move( images[ index ] ) );

            if( images[ index ].pixels.empty() )
            {
                return {};
            }
        }

        ui::NoteTextureAtlas atlas;

        try
        {
            atlas = ui::pack_note_images( images, size_reference );
        }
        catch( const std::exception &error )
        {
            std::cerr << "[Piko UI] Note images failed: " << error.what() << '\n';
            return {};
        }

        const auto handle =
            textures.create_rgba( name, atlas.image.pixels.data(), atlas.image.width, atlas.image.height );

        regions = atlas.regions;
        return handle;
    }

    managers::TextureHandle load_trace_texture( managers::TextureManager &textures,
        const std::filesystem::path &game_data_root,
        const char *name,
        std::string_view style )
    {
        ui::BattleImage image;

        if( style == "Piko" )
        {
            image = {
                generated_trace_texture_width,
                generated_trace_texture_height,
                ui::make_generated_trace_texture_pixels()
            };
        }
        else
        {
            image = load_battle_image( game_data_root / "PIKO/ui/battle/notes/default/trace.png", {} );
        }

        image = load_battle_image( note_style_directory( game_data_root, style ) / "trace.png", std::move( image ) );

        if( image.pixels.empty() )
        {
            return {};
        }

        return textures.create_rgba( name, image.pixels.data(), image.width, image.height );
    }

    managers::TextureHandle load_aim_sprite( managers::TextureManager &textures,
        const std::filesystem::path &resource_root,
        const char *name,
        const char *filename,
        std::string_view style )
    {
        const auto directory = resource_root / "ui" / "battle" / "aim";

        const auto selected = utils::path_from_utf8( style );
        const bool valid_style =
            !selected.empty() && !selected.has_parent_path() && selected != "." && selected != "..";

        const char *sprite = std::string_view( filename ) == "WingArrow.png" ? "wing.png" : "aim.png";
        const std::array<std::filesystem::path, 2> candidates{
            directory / ( valid_style ? selected : std::filesystem::path( "default" ) ) / sprite,
            directory / "default" / sprite
        };

        for( const auto &path : candidates )
        {
            if( !std::filesystem::is_regular_file( path ) )
            {
                continue;
            }

            try
            {
                return textures.load_png( name, path );
            }
            catch( const std::exception &exception )
            {
                std::cerr << "[Piko UI] Could not load aim sprite: " << exception.what() << '\n';
            }
        }

        return {};
    }

    managers::TextureHandle load_centre_sprite( managers::TextureManager &textures,
        const std::filesystem::path &game_data_root,
        const char *name,
        const managers::GameplayVisualStyle &style,
        bool pressed )
    {
        const auto directory = game_data_root / "PIKO/ui/battle/centre";

        const auto selected = utils::path_from_utf8( style.centre_icon_set );
        const bool valid_style =
            !selected.empty() && !selected.has_parent_path() && selected != "." && selected != "..";

        const char *filename = pressed ? "pressed.png" : "normal.png";
        const std::array<std::filesystem::path, 2> candidates{
            directory / ( valid_style ? selected : std::filesystem::path( "default" ) ) / filename,
            directory / "default" / filename
        };

        for( const auto &path : candidates )
        {
            if( !utils::regular_file( path ) )
            {
                continue;
            }

            try
            {
                return textures.load_png( name, path );
            }
            catch( const std::exception &exception )
            {
                std::cerr << "[Piko UI] Could not load centre icon: " << exception.what() << '\n';
            }
        }

        return {};
    }

    std::vector<std::string> note_style_names( const std::filesystem::path &resource_root )
    {
        std::vector<std::string> names{
            "default"
        };

        std::error_code error;

        const auto directory = resource_root / "ui/battle/notes";

        for( std::filesystem::directory_iterator iterator( directory, error ), end; !error && iterator != end;
            iterator.increment( error ) )
        {
            const auto &entry = *iterator;

            if( entry.path().filename() != "default" && entry.is_directory( error ) && !entry.is_symlink( error ) &&
                ( utils::regular_file( entry.path() / "note.png" ) ||
                    utils::regular_file( entry.path() / "ribbon.png" ) ||
                    utils::regular_file( entry.path() / "trace.png" ) ||
                    utils::regular_file( entry.path() / "cap.png" ) ) )
            {
                names.push_back( utils::path_to_utf8( entry.path().filename() ) );
            }
        }

        std::sort( names.begin() + 1, names.end() );
        return names;
    }

    std::vector<std::string> centre_style_names( const std::filesystem::path &resource_root )
    {
        std::vector<std::string> names;
        std::error_code error;

        const auto directory = resource_root / "ui/battle/centre";

        for( std::filesystem::directory_iterator iterator( directory, error ), end; !error && iterator != end;
            iterator.increment( error ) )
        {
            const auto &entry = *iterator;

            if( entry.path().filename() != "default" && entry.is_directory( error ) && !entry.is_symlink( error ) &&
                ( utils::regular_file( entry.path() / "normal.png" ) ||
                    utils::regular_file( entry.path() / "pressed.png" ) ) )
            {
                names.push_back( utils::path_to_utf8( entry.path().filename() ) );
            }
        }

        std::sort( names.begin(), names.end() );
        return names;
    }

    std::vector<std::string> aim_style_names( const std::filesystem::path &resource_root )
    {
        std::vector<std::string> names;
        std::error_code error;

        const auto directory = resource_root / "ui" / "battle" / "aim";

        for( std::filesystem::directory_iterator iterator( directory, error ), end; !error && iterator != end;
            iterator.increment( error ) )
        {
            const auto &entry = *iterator;

            if( entry.is_directory( error ) && !entry.is_symlink( error ) &&
                ( std::filesystem::is_regular_file( entry.path() / "wing.png", error ) ||
                    std::filesystem::is_regular_file( entry.path() / "aim.png", error ) ) )
            {
                const auto name = entry.path().filename().u8string();
                names.emplace_back( reinterpret_cast<const char *>( name.data() ), name.size() );
            }
        }

        std::sort( names.begin(), names.end() );
        return names;
    }

    ViewConeStartPoints view_cone_start_points( const rendering::Vec2 &marker,
        const rendering::Vec2 &perpendicular,
        float inner_diameter )
    {
        const float radius = std::max( 0.0f, inner_diameter ) * 0.5f;
        return {
            { marker.x - perpendicular.x * radius, marker.y - perpendicular.y * radius },
            { marker.x + perpendicular.x * radius, marker.y + perpendicular.y * radius }
        };
    }

    void draw_view_cone_gradient( rendering::Renderer &renderer,
        const rendering::Vec2 &marker,
        const rendering::Vec2 &upper_start,
        const rendering::Vec2 &lower_start,
        float cone_radians,
        float half_angle_radians,
        float cone_length,
        const rendering::Color &fill_color )
    {
        rendering::Color middle_fill = fill_color;
        middle_fill.a *= 0.62f;
        rendering::Color outer_fill = fill_color;
        outer_fill.a *= 0.05f;

        constexpr std::int32_t segment_count = 24;
        constexpr float middle_ratio = 0.68f;
        std::array<rendering::VertexColor2D, segment_count * 12> vertices{};

        for( std::int32_t segment = 0; segment < segment_count; ++segment )
        {
            const float t0 = static_cast<float>( segment ) / static_cast<float>( segment_count );
            const float t1 = static_cast<float>( segment + 1 ) / static_cast<float>( segment_count );
            const float angle0 = cone_radians - half_angle_radians + half_angle_radians * 2.0f * t0;
            const float angle1 = cone_radians - half_angle_radians + half_angle_radians * 2.0f * t1;
            const rendering::Vec2 outer0{
                marker.x + std::cos( angle0 ) * cone_length,
                marker.y + std::sin( angle0 ) * cone_length
            };

            const rendering::Vec2 outer1{
                marker.x + std::cos( angle1 ) * cone_length,
                marker.y + std::sin( angle1 ) * cone_length
            };

            const rendering::Vec2 inner0{
                upper_start.x + ( lower_start.x - upper_start.x ) * t0,
                upper_start.y + ( lower_start.y - upper_start.y ) * t0
            };

            const rendering::Vec2 inner1{
                upper_start.x + ( lower_start.x - upper_start.x ) * t1,
                upper_start.y + ( lower_start.y - upper_start.y ) * t1
            };

            const rendering::Vec2 middle0{
                inner0.x + ( outer0.x - inner0.x ) * middle_ratio,
                inner0.y + ( outer0.y - inner0.y ) * middle_ratio
            };

            const rendering::Vec2 middle1{
                inner1.x + ( outer1.x - inner1.x ) * middle_ratio,
                inner1.y + ( outer1.y - inner1.y ) * middle_ratio
            };

            const std::size_t base = static_cast<std::size_t>( segment ) * 12;

            vertices[ base + 0 ] = {
                inner0,
                fill_color
            };

            vertices[ base + 1 ] = {
                middle0,
                middle_fill
            };

            vertices[ base + 2 ] = {
                middle1,
                middle_fill
            };

            vertices[ base + 3 ] = {
                inner0,
                fill_color
            };

            vertices[ base + 4 ] = {
                middle1,
                middle_fill
            };

            vertices[ base + 5 ] = {
                inner1,
                fill_color
            };

            vertices[ base + 6 ] = {
                middle0,
                middle_fill
            };

            vertices[ base + 7 ] = {
                outer0,
                outer_fill
            };

            vertices[ base + 8 ] = {
                outer1,
                outer_fill
            };

            vertices[ base + 9 ] = {
                middle0,
                middle_fill
            };

            vertices[ base + 10 ] = {
                outer1,
                outer_fill
            };

            vertices[ base + 11 ] = {
                middle1,
                middle_fill
            };
        }

        renderer.draw_colored_triangles( vertices.data(), vertices.size() );
    }

    void draw_view_cone_border( rendering::Renderer &renderer,
        const rendering::Vec2 &marker,
        const rendering::Vec2 &end,
        float width,
        const rendering::Color &color )
    {
        constexpr float fade_start_ratio = 0.86f;
        constexpr float end_alpha_scale = 0.22f;
        const rendering::Vec2 fade_start{
            marker.x + ( end.x - marker.x ) * fade_start_ratio,
            marker.y + ( end.y - marker.y ) * fade_start_ratio
        };

        renderer.draw_line( marker, fade_start, width, color );

        const float dx = end.x - fade_start.x;
        const float dy = end.y - fade_start.y;
        const float length = std::sqrt( dx * dx + dy * dy );

        if( length <= 0.0001f )
        {
            return;
        }

        const float half_width = width * 0.5f;
        const float nx = -dy / length * half_width;
        const float ny = dx / length * half_width;
        rendering::Color end_color = color;
        end_color.a *= end_alpha_scale;

        const std::array<rendering::VertexColor2D, 6> vertices{
            rendering::VertexColor2D{ { fade_start.x + nx, fade_start.y + ny }, color },
            rendering::VertexColor2D{ { fade_start.x - nx, fade_start.y - ny }, color },
            rendering::VertexColor2D{ { end.x - nx, end.y - ny }, end_color },
            rendering::VertexColor2D{ { fade_start.x + nx, fade_start.y + ny }, color },
            rendering::VertexColor2D{ { end.x - nx, end.y - ny }, end_color },
            rendering::VertexColor2D{ { end.x + nx, end.y + ny }, end_color }
        };

        renderer.draw_colored_triangles( vertices.data(), vertices.size() );
    }

    void draw_view_cone_arrows( rendering::Renderer &renderer,
        const rendering::Vec2 &marker,
        const rendering::Vec2 &axis,
        const rendering::Vec2 &perpendicular,
        float visual_alpha,
        float slide_progress,
        float cone_half_height,
        float scale,
        const managers::GameplayVisualStyle &style,
        const rendering::Texture *arrow_texture,
        const rendering::Texture *wing_texture )
    {
        if( visual_alpha <= 0.001f )
        {
            return;
        }

        const float t = std::clamp( slide_progress, 0.0f, 1.0f );
        const float slide = t * t * ( 3.0f - 2.0f * t );
        const float arrow_scale = scale * style.cone_arrow_scale;
        const float wing_scale = scale * style.cone_wing_scale;
        const float wing_spread =
            std::lerp( 5.0f * wing_scale, cone_half_height * 0.46f * style.cone_wing_spread_scale, slide );

        const float angle = std::atan2( axis.y, axis.x ) * 180.0f / 3.14159265f;
        const rendering::Color tint{ 1.0f, 1.0f, 1.0f, std::clamp( visual_alpha, 0.0f, 1.0f ) };

        const auto draw_arrow =
            [ & ]( const rendering::Texture *texture, float along, float across, float width, float height )
        {
            if( texture == nullptr )
            {
                return;
            }

            renderer.draw_texture_rotated( *texture,
                marker.x + axis.x * along + perpendicular.x * across,
                marker.y + axis.y * along + perpendicular.y * across,
                width,
                height,
                0.5f,
                0.5f,
                angle,
                tint );
        };

        const float wing_along = 8.0f * wing_scale + style.wing_offset_x * scale;
        const float wing_width = 14.0f * wing_scale;
        const float wing_height = 13.0f * wing_scale * style.cone_wing_width_scale;
        draw_arrow( wing_texture, wing_along, -wing_spread, wing_width, wing_height );
        draw_arrow( wing_texture, wing_along, wing_spread, wing_width, wing_height );

        draw_arrow( arrow_texture,
            16.0f * arrow_scale + style.arrow_offset_x * scale,
            0.0f,
            28.0f * arrow_scale,
            19.0f * arrow_scale * style.cone_arrow_width_scale );
    }

    float draw_view_cone_center_nub( rendering::Renderer &renderer,
        const rendering::Vec2 &marker,
        bool pressed,
        float scale,
        float normal_scale,
        float pressed_scale,
        const rendering::Texture *normal_texture,
        const rendering::Texture *pressed_texture,
        const rendering::Texture *original_texture )
    {
        const auto &icon = runtime::stage::gameplay_interface_atlas::center_icon;
        const float baseline_diameter =
            pressed ? runtime::stage::gameplay_interface_atlas::center_icon_pressed_diameter : icon.w;

        const float size = pressed ? pressed_scale : normal_scale;
        const float center_radius = baseline_diameter * size * scale * 0.5f;
        const rendering::Texture *texture = pressed ? pressed_texture : normal_texture;

        if( texture != nullptr && texture->width > 0 && texture->height > 0 )
        {
            const float diameter = center_radius * 2.0f;
            const float image_scale = diameter / static_cast<float>( std::max( texture->width, texture->height ) );
            const float width = static_cast<float>( texture->width ) * image_scale;
            const float height = static_cast<float>( texture->height ) * image_scale;
            renderer.draw_texture( *texture, marker.x - width * 0.5f, marker.y - height * 0.5f, width, height );
            return center_radius;
        }

        const auto &source = runtime::stage::gameplay_interface_atlas::center_icon;

        if( original_texture != nullptr && original_texture->valid() &&
            original_texture->width >= source.x + source.w && original_texture->height >= source.y + source.h )
        {
            const float diameter = center_radius * 2.0f;

            const auto &tint = pressed ? runtime::stage::gameplay_interface_atlas::center_icon_pressed_tint
                                       : runtime::stage::gameplay_interface_atlas::center_icon_idle_tint;

            renderer.draw_texture_region( *original_texture,
                source.x,
                source.y,
                source.w,
                source.h,
                marker.x - center_radius,
                marker.y - center_radius,
                diameter,
                diameter,
                tint );
        }

        return center_radius;
    }
}
