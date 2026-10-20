#pragma once

#include "RenderTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace piko::rendering
{
    struct StrokedTextStyle
    {
        Color fill{};
        Color inner_border{};
        Color outer_border{};
        std::int32_t inner_border_pixels = 3;
        std::int32_t outer_border_pixels = 7;
        float tracking_pixels = 0.0f;
        std::vector<Color> glyph_fill_colors{};
    };

    struct RasterizedText
    {
        std::int32_t width = 0;
        std::int32_t height = 0;
        std::vector<std::uint8_t> rgba;

        [[nodiscard]] bool valid() const;
    };

    class TrueTypeFont
    {
    public:
        TrueTypeFont();
        ~TrueTypeFont();

        TrueTypeFont( TrueTypeFont && ) noexcept;
        TrueTypeFont &operator=( TrueTypeFont && ) noexcept;

        TrueTypeFont( const TrueTypeFont & ) = delete;
        TrueTypeFont &operator=( const TrueTypeFont & ) = delete;

        bool load( const std::filesystem::path &path, std::string *error = nullptr );
        bool add_fallback( const std::filesystem::path &path, std::string *error = nullptr );
        void clear();

        [[nodiscard]] bool loaded() const;
        [[nodiscard]] bool supports_text( std::string_view text ) const;
        [[nodiscard]] RasterizedText rasterize_stroked( std::string_view text,
            float pixel_height,
            const StrokedTextStyle &style,
            std::string *error = nullptr ) const;

    private:
        struct GlyphPlacement;
        struct Impl;

        bool load_face( const std::filesystem::path &path, std::string *error );
        [[nodiscard]] RasterizedText rasterize_shaped(
            std::string_view text,
            float pixel_height,
            const StrokedTextStyle &style,
            bool rtl_base,
            std::string *error ) const;
        [[nodiscard]] static RasterizedText compose_stroked(
            std::int32_t width,
            std::int32_t height,
            const std::vector<std::uint8_t> &fill_mask,
            const std::vector<Color> &fill_colors,
            const StrokedTextStyle &style );

        static void set_error( std::string *error, std::string message );
        static bool decode_utf8_codepoint( std::string_view text, std::size_t &offset, std::int32_t &codepoint );
        static std::vector<std::uint8_t> dilate_mask( const std::vector<std::uint8_t> &source,
            std::int32_t width,
            std::int32_t height,
            std::int32_t radius );

        static void composite_layer( const Color &color,
            float coverage,
            float &premultiplied_r,
            float &premultiplied_g,
            float &premultiplied_b,
            float &alpha );

        static std::uint8_t to_byte( float value );

        std::unique_ptr<Impl> impl_;
    };
}
