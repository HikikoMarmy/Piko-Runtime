#include "LabelTexture.hpp"

#include "Localization/Language.hpp"
#include "Utils/EnumUtil.hpp"

#include <algorithm>
#include <iostream>

namespace piko::localization
{
    managers::TextureHandle load_image( managers::TextureManager &textures, const std::filesystem::path &relative )
    {
        const auto path = asset_path( relative );

        if( path.empty() )
        {
            return {};
        }

        try
        {
            return textures.load_png( texture_key( "localized/image/" + relative.generic_string() ), path );
        }
        catch( const std::exception &error )
        {
            std::cerr << "[Piko UI] Language image failed: " << path << ": " << error.what() << '\n';
            return {};
        }
    }

    managers::TextureHandle make_label( managers::TextureManager &textures,
        std::string_view key,
        std::string_view style_name )
    {
        const std::string name = texture_key( "localized/" + std::string( key ) + "/" + std::string( style_name ) );

        if( const auto cached = textures.find( name ) )
        {
            return cached;
        }

        rendering::TrueTypeFont font;

        if( !load_menu_font( font, label_font_path( style_name ) ) )
        {
            return {};
        }

        const LabelStyle style = label_style( style_name );

        const auto raster = font.rasterize_stroked( translate( key ), style.height * 3.0f, style.stroke );

        if( !raster.valid() )
        {
            return {};
        }

        return textures.create_rgba( name, raster.rgba.data(), raster.width, raster.height );
    }

    void draw_label( rendering::Renderer &renderer,
        const rendering::Texture *texture,
        const rendering::Rect &bounds,
        const rendering::Color &tint,
        bool centered )
    {
        if( texture == nullptr || bounds.w <= 0.0f || bounds.h <= 0.0f )
        {
            return;
        }

        const float scale = std::min( bounds.w / static_cast<float>( texture->width ),
            bounds.h / static_cast<float>( texture->height ) );

        const float width = static_cast<float>( texture->width ) * scale;
        const float height = static_cast<float>( texture->height ) * scale;
        renderer.draw_texture( *texture,
            bounds.x + ( centered ? ( bounds.w - width ) * 0.5f : 0.0f ),
            bounds.y + ( bounds.h - height ) * 0.5f,
            width,
            height,
            tint );
    }
}
