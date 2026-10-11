#include "TrueTypeText.hpp"
#include "Localization/Language.hpp"

#include "Utils/MathUtil.hpp"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <imstb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <utility>

namespace piko::rendering
{
    bool RasterizedText::valid() const
    {
        return width > 0 && height > 0 &&
               rgba.size() == static_cast<std::size_t>( width ) * static_cast<std::size_t>( height ) * 4u;
    }

    struct TrueTypeFont::GlyphPlacement
    {
        std::int32_t codepoint = 0;
        std::size_t face_index = 0;
        float scale = 0.0f;
        float pen_x = 0.0f;
    };

    void TrueTypeFont::set_error( std::string *error, std::string message )
    {
        if( error != nullptr )
        {
            *error = std::move( message );
        }
    }

    bool TrueTypeFont::decode_utf8_codepoint( std::string_view text, std::size_t &offset, std::int32_t &codepoint )
    {
        if( offset >= text.size() )
        {
            return false;
        }

        const std::uint8_t first = static_cast<std::uint8_t>( text[ offset++ ] );

        if( first < 0x80u )
        {
            codepoint = static_cast<std::int32_t>( first );
            return true;
        }

        std::int32_t continuation_count = 0;
        std::uint32_t value = 0;

        if( ( first & 0xE0u ) == 0xC0u )
        {
            continuation_count = 1;

            value = first & 0x1Fu;
        }
        else if( ( first & 0xF0u ) == 0xE0u )
        {
            continuation_count = 2;

            value = first & 0x0Fu;
        }
        else if( ( first & 0xF8u ) == 0xF0u )
        {
            continuation_count = 3;

            value = first & 0x07u;
        }
        else
        {
            codepoint = '?';
            return true;
        }

        if( offset + static_cast<std::size_t>( continuation_count ) > text.size() )
        {
            codepoint = '?';

            offset = text.size();
            return true;
        }

        for( std::int32_t i = 0; i < continuation_count; ++i )
        {
            const std::uint8_t continuation = static_cast<std::uint8_t>( text[ offset++ ] );

            if( ( continuation & 0xC0u ) != 0x80u )
            {
                codepoint = '?';
                return true;
            }

            value = ( value << 6u ) | ( continuation & 0x3Fu );
        }

        if( value > 0x10FFFFu || ( value >= 0xD800u && value <= 0xDFFFu ) )
        {
            value = static_cast<std::uint32_t>( '?' );
        }

        codepoint = static_cast<std::int32_t>( value );
        return true;
    }

    std::vector<std::uint8_t> TrueTypeFont::dilate_mask( const std::vector<std::uint8_t> &source,
        std::int32_t width,
        std::int32_t height,
        std::int32_t radius )
    {
        if( radius <= 0 )
        {
            return source;
        }

        std::vector<std::uint8_t> output( source.size(), 0u );
        const std::int32_t radius_squared = radius * radius;
        std::vector<std::int32_t> window( static_cast<std::size_t>( width ) );

        for( std::int32_t dy = -radius; dy <= radius; ++dy )
        {
            const std::int32_t extent =
                static_cast<std::int32_t>( std::sqrt( static_cast<double>( radius_squared - dy * dy ) ) );

            for( std::int32_t source_y = 0; source_y < height; ++source_y )
            {
                const std::int32_t target_y = source_y - dy;

                if( target_y < 0 || target_y >= height )
                {
                    continue;
                }

                const std::size_t source_row = static_cast<std::size_t>( source_y ) * static_cast<std::size_t>( width );
                const std::size_t target_row = static_cast<std::size_t>( target_y ) * static_cast<std::size_t>( width );
                std::int32_t front = 0;
                std::int32_t back = 0;
                std::int32_t next = 0;

                for( std::int32_t x = 0; x < width; ++x )
                {
                    const std::int32_t right = std::min( x + extent, width - 1 );
                    while( next <= right )
                    {
                        while( back > front && source[ source_row + static_cast<std::size_t>( window[ back - 1 ] ) ] <=
                                                   source[ source_row + static_cast<std::size_t>( next ) ] )
                        {
                            --back;
                        }

                        window[ back++ ] = next++;
                    }
                    while( front < back && window[ front ] < x - extent )
                    {
                        ++front;
                    }

                    std::uint8_t &target = output[ target_row + static_cast<std::size_t>( x ) ];
                    target = std::max( target, source[ source_row + static_cast<std::size_t>( window[ front ] ) ] );
                }
            }
        }

        return output;
    }

    void TrueTypeFont::composite_layer( const Color &color,
        float coverage,
        float &premultiplied_r,
        float &premultiplied_g,
        float &premultiplied_b,
        float &alpha )
    {
        const float source_alpha = piko::utils::clamp01( color.a ) * piko::utils::clamp01( coverage );
        const float inverse_source_alpha = 1.0f - source_alpha;

        premultiplied_r = piko::utils::clamp01( color.r ) * source_alpha + premultiplied_r * inverse_source_alpha;

        premultiplied_g = piko::utils::clamp01( color.g ) * source_alpha + premultiplied_g * inverse_source_alpha;

        premultiplied_b = piko::utils::clamp01( color.b ) * source_alpha + premultiplied_b * inverse_source_alpha;

        alpha = source_alpha + alpha * inverse_source_alpha;
    }

    std::uint8_t TrueTypeFont::to_byte( float value )
    {
        return static_cast<std::uint8_t>(
            std::clamp( std::lround( piko::utils::clamp01( value ) * 255.0f ), 0l, 255l ) );
    }

    struct TrueTypeFont::Impl
    {
        struct Face
        {
            std::filesystem::path path;
            std::vector<std::uint8_t> bytes;
            stbtt_fontinfo font{};
        };

        std::vector<std::unique_ptr<Face>> faces;

        [[nodiscard]] std::size_t face_index( std::int32_t codepoint ) const
        {
            for( std::size_t index = 0; index < faces.size(); ++index )
            {
                if( stbtt_FindGlyphIndex( &faces[ index ]->font, codepoint ) != 0 )
                {
                    return index;
                }
            }

            return 0;
        }

        [[nodiscard]] float face_scale( std::size_t index, float pixel_height ) const
        {
            const auto &primary = faces.front()->font;

            const auto &font = faces[ index ]->font;
            const float scale = stbtt_ScaleForPixelHeight( &font, pixel_height );

            if( index == 0 )
            {
                return scale;
            }

            std::int32_t primary_bottom = 0;
            std::int32_t primary_top = 0;
            std::int32_t fallback_bottom = 0;
            std::int32_t fallback_top = 0;

            // Keep fallback letters the same height.
            if( stbtt_GetCodepointBox( &primary, 'x', nullptr, &primary_bottom, nullptr, &primary_top ) &&
                stbtt_GetCodepointBox( &font, 'x', nullptr, &fallback_bottom, nullptr, &fallback_top ) &&
                primary_top > primary_bottom && fallback_top > fallback_bottom )
            {
                const float primary_height = static_cast<float>( primary_top - primary_bottom ) *
                                             stbtt_ScaleForPixelHeight( &primary, pixel_height );

                return primary_height / static_cast<float>( fallback_top - fallback_bottom );
            }

            return scale;
        }
    };

    TrueTypeFont::TrueTypeFont() : impl_( std::make_unique<Impl>() )
    {
    }

    TrueTypeFont::~TrueTypeFont() = default;
    TrueTypeFont::TrueTypeFont( TrueTypeFont && ) noexcept = default;
    TrueTypeFont &TrueTypeFont::operator=( TrueTypeFont && ) noexcept = default;

    bool TrueTypeFont::load( const std::filesystem::path &path, std::string *error )
    {
        clear();
        return load_face( path, error );
    }

    bool TrueTypeFont::add_fallback( const std::filesystem::path &path, std::string *error )
    {
        if( !loaded() )
        {
            return load( path, error );
        }

        const auto normalized = path.lexically_normal();

        for( const auto &face : impl_->faces )
        {
            if( face->path == normalized )
            {
                if( error != nullptr )
                {
                    error->clear();
                }

                return true;
            }
        }

        return load_face( path, error );
    }

    bool TrueTypeFont::load_face( const std::filesystem::path &path, std::string *error )
    {

        if( path.empty() )
        {
            set_error( error, "TrueType font path is empty" );
            return false;
        }

        auto face = std::make_unique<Impl::Face>();
        face->path = path.lexically_normal();

        std::ifstream stream( path, std::ios::binary | std::ios::ate );

        if( !stream )
        {
            set_error( error, "Could not open TrueType font: " + path.string() );
            return false;
        }

        const std::streamoff size = stream.tellg();

        if( size <= 0 )
        {
            set_error( error, "TrueType font has an invalid size: " + path.string() );
            return false;
        }

        face->bytes.resize( static_cast<std::size_t>( size ) );
        stream.seekg( 0, std::ios::beg );
        stream.read( reinterpret_cast<char *>( face->bytes.data() ),
            static_cast<std::streamsize>( face->bytes.size() ) );

        if( !stream )
        {
            set_error( error, "Could not read TrueType font: " + path.string() );
            return false;
        }

        const std::int32_t font_offset = stbtt_GetFontOffsetForIndex( face->bytes.data(), 0 );

        if( font_offset < 0 || !stbtt_InitFont( &face->font, face->bytes.data(), font_offset ) )
        {
            set_error( error, "Could not parse TrueType font: " + path.string() );
            return false;
        }

        impl_->faces.push_back( std::move( face ) );

        if( error != nullptr )
        {
            error->clear();
        }

        return true;
    }

    void TrueTypeFont::clear()
    {
        if( impl_ == nullptr )
        {
            impl_ = std::make_unique<Impl>();
        }

        impl_->faces.clear();
    }

    bool TrueTypeFont::loaded() const
    {
        return impl_ != nullptr && !impl_->faces.empty();
    }

    bool TrueTypeFont::supports_text( std::string_view text ) const
    {
        if( !loaded() )
        {
            return false;
        }

        for( std::size_t offset = 0; offset < text.size(); )
        {
            std::int32_t codepoint = 0;

            if( !decode_utf8_codepoint( text, offset, codepoint ) ||
                ( codepoint > 32 &&
                    stbtt_FindGlyphIndex( &impl_->faces[ impl_->face_index( codepoint ) ]->font, codepoint ) == 0 ) )
            {
                return false;
            }
        }

        return true;
    }

    RasterizedText TrueTypeFont::rasterize_stroked( std::string_view text,
        float pixel_height,
        const StrokedTextStyle &style,
        std::string *error ) const
    {
        const std::string translated = localization::translate( text );
        text = translated;

        RasterizedText output{};

        if( !loaded() )
        {
            set_error( error, "TrueType font is not loaded" );
            return output;
        }

        if( text.empty() || pixel_height <= 0.0f )
        {
            set_error( error, "Text and pixel height must be non-empty" );
            return output;
        }

        const std::int32_t inner_radius = std::max( style.inner_border_pixels, 0 );
        const std::int32_t outer_radius = std::max( style.outer_border_pixels, inner_radius );
        const std::int32_t padding = outer_radius + 2;

        std::vector<std::int32_t> codepoints;

        for( std::size_t offset = 0; offset < text.size(); )
        {
            std::int32_t codepoint = 0;

            if( !decode_utf8_codepoint( text, offset, codepoint ) )
            {
                break;
            }

            if( codepoint == '\n' || codepoint == '\r' )
            {
                codepoint = ' ';
            }

            codepoints.push_back( codepoint );
        }

        if( codepoints.empty() )
        {
            set_error( error, "Text contains no renderable codepoints" );
            return output;
        }

        std::vector<GlyphPlacement> placements;
        placements.reserve( codepoints.size() );

        float pen_x = 0.0f;
        std::int32_t min_x = std::numeric_limits<std::int32_t>::max();
        std::int32_t min_y = std::numeric_limits<std::int32_t>::max();
        std::int32_t max_x = std::numeric_limits<std::int32_t>::min();
        std::int32_t max_y = std::numeric_limits<std::int32_t>::min();
        bool has_bitmap = false;

        for( std::size_t i = 0; i < codepoints.size(); ++i )
        {
            const std::int32_t codepoint = codepoints[ i ];
            const std::size_t face_index = impl_->face_index( codepoint );

            const auto &font = impl_->faces[ face_index ]->font;
            const float scale = impl_->face_scale( face_index, pixel_height );
            placements.push_back( GlyphPlacement{ codepoint, face_index, scale, pen_x } );

            std::int32_t x0 = 0;
            std::int32_t y0 = 0;
            std::int32_t x1 = 0;
            std::int32_t y1 = 0;
            stbtt_GetCodepointBitmapBox( &font, codepoint, scale, scale, &x0, &y0, &x1, &y1 );

            if( x1 > x0 && y1 > y0 )
            {
                const std::int32_t rounded_pen = static_cast<std::int32_t>( std::lround( pen_x ) );
                min_x = std::min( min_x, rounded_pen + x0 );

                min_y = std::min( min_y, y0 );

                max_x = std::max( max_x, rounded_pen + x1 );

                max_y = std::max( max_y, y1 );

                has_bitmap = true;
            }

            std::int32_t advance_width = 0;
            std::int32_t left_side_bearing = 0;
            stbtt_GetCodepointHMetrics( &font, codepoint, &advance_width, &left_side_bearing );
            (void)left_side_bearing;
            pen_x += static_cast<float>( advance_width ) * scale;

            if( i + 1u < codepoints.size() )
            {
                if( impl_->face_index( codepoints[ i + 1u ] ) == face_index )
                {
                    pen_x +=
                        static_cast<float>( stbtt_GetCodepointKernAdvance( &font, codepoint, codepoints[ i + 1u ] ) ) *
                        scale;
                }

                pen_x += style.tracking_pixels;
            }
        }

        if( !has_bitmap )
        {
            set_error( error, "Text produced no visible glyphs" );
            return output;
        }

        max_x = std::max( max_x, static_cast<std::int32_t>( std::ceil( pen_x ) ) );

        output.width = std::max( max_x - min_x + padding * 2, 1 );

        output.height = std::max( max_y - min_y + padding * 2, 1 );

        std::vector<std::uint8_t> fill_mask( static_cast<std::size_t>( output.width ) *
                                                 static_cast<std::size_t>( output.height ),
            0u );

        std::vector<Color> fill_colors;

        if( !style.glyph_fill_colors.empty() )
        {
            fill_colors.resize( fill_mask.size(), style.fill );
        }

        for( std::size_t glyph_index = 0; glyph_index < placements.size(); ++glyph_index )
        {
            const GlyphPlacement &placement = placements[ glyph_index ];
            std::int32_t glyph_width = 0;
            std::int32_t glyph_height = 0;
            std::int32_t x_offset = 0;
            std::int32_t y_offset = 0;

            const auto &font = impl_->faces[ placement.face_index ]->font;
            unsigned char *glyph = stbtt_GetCodepointBitmap( &font,
                placement.scale,
                placement.scale,
                placement.codepoint,
                &glyph_width,
                &glyph_height,
                &x_offset,
                &y_offset );

            if( glyph == nullptr || glyph_width <= 0 || glyph_height <= 0 )
            {
                if( glyph != nullptr )
                {
                    stbtt_FreeBitmap( glyph, nullptr );
                }
                continue;
            }

            const std::int32_t destination_x =
                static_cast<std::int32_t>( std::lround( placement.pen_x ) ) + x_offset - min_x + padding;

            const std::int32_t destination_y = y_offset - min_y + padding;

            for( std::int32_t glyph_y = 0; glyph_y < glyph_height; ++glyph_y )
            {
                const std::int32_t target_y = destination_y + glyph_y;

                if( target_y < 0 || target_y >= output.height )
                {
                    continue;
                }

                for( std::int32_t glyph_x = 0; glyph_x < glyph_width; ++glyph_x )
                {
                    const std::int32_t target_x = destination_x + glyph_x;

                    if( target_x < 0 || target_x >= output.width )
                    {
                        continue;
                    }

                    const std::uint8_t coverage =
                        glyph[ static_cast<std::size_t>( glyph_y ) * static_cast<std::size_t>( glyph_width ) +
                               static_cast<std::size_t>( glyph_x ) ];

                    std::uint8_t &target =
                        fill_mask[ static_cast<std::size_t>( target_y ) * static_cast<std::size_t>( output.width ) +
                                   static_cast<std::size_t>( target_x ) ];

                    if( !fill_colors.empty() && coverage >= target )
                    {
                        fill_colors[ static_cast<std::size_t>( target_y ) * output.width + target_x ] =
                            style.glyph_fill_colors[ glyph_index % style.glyph_fill_colors.size() ];
                    }

                    target = std::max( target, coverage );
                }
            }

            stbtt_FreeBitmap( glyph, nullptr );
        }

        const std::vector<std::uint8_t> inner_mask =
            dilate_mask( fill_mask, output.width, output.height, inner_radius );

        const std::vector<std::uint8_t> outer_mask =
            dilate_mask( fill_mask, output.width, output.height, outer_radius );

        output.rgba.resize( static_cast<std::size_t>( output.width ) * static_cast<std::size_t>( output.height ) * 4u,
            0u );

        for( std::size_t pixel = 0; pixel < fill_mask.size(); ++pixel )
        {
            float premultiplied_r = 0.0f;
            float premultiplied_g = 0.0f;
            float premultiplied_b = 0.0f;
            float alpha = 0.0f;

            composite_layer( style.outer_border,
                static_cast<float>( outer_mask[ pixel ] ) / 255.0f,
                premultiplied_r,
                premultiplied_g,
                premultiplied_b,
                alpha );

            composite_layer( style.inner_border,
                static_cast<float>( inner_mask[ pixel ] ) / 255.0f,
                premultiplied_r,
                premultiplied_g,
                premultiplied_b,
                alpha );

            composite_layer( fill_colors.empty() ? style.fill : fill_colors[ pixel ],
                static_cast<float>( fill_mask[ pixel ] ) / 255.0f,
                premultiplied_r,
                premultiplied_g,
                premultiplied_b,
                alpha );

            const std::size_t output_index = pixel * 4u;

            if( alpha > 0.0f )
            {
                output.rgba[ output_index + 0u ] = to_byte( premultiplied_r / alpha );

                output.rgba[ output_index + 1u ] = to_byte( premultiplied_g / alpha );

                output.rgba[ output_index + 2u ] = to_byte( premultiplied_b / alpha );

                output.rgba[ output_index + 3u ] = to_byte( alpha );
            }
        }

        if( error != nullptr )
        {
            error->clear();
        }

        return output;
    }
}
