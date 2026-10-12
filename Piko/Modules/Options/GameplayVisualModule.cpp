#include "Utils/EnumUtil.hpp"
#include "Localization/Language.hpp"
#include "Localization/LabelTexture.hpp"
#include "GameplayVisualModule.hpp"

#include "App/piko.hpp"
#include "Managers/GameSettings.hpp"
#include "Modules/Options/OptionsModule.hpp"
#include "Modules/MainMenu/TitleScreenBackdrop.hpp"
#include "Modules/Stage/StageModuleRendering.hpp"
#include "Modules/Stage/StageGeneratedTextures.hpp"
#include "Modules/Stage/StageGuardRenderer.hpp"
#include "Modules/Stage/StagePhraseRenderer.hpp"
#include "Modules/Stage/StageTraceRenderer.hpp"
#include "Runtime/Stage/StageView.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/RenderUtil.hpp"
#include "Rendering/MenuTheme.hpp"
#include "Utils/MathUtil.hpp"
#include "Utils/FileUtil.hpp"
#include "Utils/StrUtil.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <charconv>
#include <utility>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <mutex>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace piko::modules
{
    namespace
    {
        void match_style_name( const std::vector<std::string> &names, std::string &selected )
        {
            const auto key = utils::lowercase_ascii( selected );

            const auto found = std::find_if( names.begin(),
                names.end(),
                [ & ]( const auto &name )
                {
                    return utils::lowercase_ascii( name ) == key;
                } );

            if( found != names.end() )
            {
                selected = *found;
            }
        }
    }

    GameplayVisualModule::GameplayVisualModule( std::shared_ptr<OptionsPresentation> presentation )
        : presentation_( std::move( presentation ) )
    {
    }

    rendering::Rect GameplayVisualModule::settings_panel( std::int32_t output_width,
        std::int32_t output_height,
        bool full_width )
    {
        const rendering::LogicalViewport layout =
            rendering::make_logical_viewport( output_width, output_height, 1280.0f, 720.0f );

        return layout.rect( rendering::Rect{ 126.0f, 34.0f, full_width ? 1028.0f : 620.0f, 652.0f } );
    }

    rendering::Rect GameplayVisualModule::preview_panel( std::int32_t output_width, std::int32_t output_height )
    {
        const rendering::LogicalViewport layout =
            rendering::make_logical_viewport( output_width, output_height, 1280.0f, 720.0f );

        return layout.rect( rendering::Rect{ 780.0f, 174.0f, 374.0f, 446.0f } );
    }

    float GameplayVisualModule::row_height_for( std::int32_t output_width, std::int32_t output_height )
    {
        return row_height * rendering::make_logical_viewport( output_width, output_height, 1280.0f, 720.0f ).scale;
    }

    float GameplayVisualModule::row_gap_for( std::int32_t output_width, std::int32_t output_height )
    {
        return row_gap * rendering::make_logical_viewport( output_width, output_height, 1280.0f, 720.0f ).scale;
    }

    rendering::Rect GameplayVisualModule::row_rect( std::int32_t index,
        std::int32_t output_width,
        std::int32_t output_height ) const
    {
        const rendering::Rect panel = GameplayVisualModule::settings_panel( output_width, output_height, full_width() );

        const float scale = rendering::make_logical_viewport( output_width, output_height, 1280.0f, 720.0f ).scale;
        const float height = row_height_for( output_width, output_height );
        const float gap = row_gap_for( output_width, output_height );
        const float extra_gap = ( section_gap - row_gap ) * scale;
        float section_offset = 0.0f;

        if( view_ == View::Menu )
        {
            switch( row_kind( index ) )
            {
                case SettingRow::SaveProfileEntry:
                case SettingRow::LoadProfileEntry:
                    section_offset = extra_gap;
                    break;
                case SettingRow::Back:
                    section_offset = extra_gap * 2.0f;
                    break;
                default:
                    break;
            }
        }

        return rendering::Rect{
            panel.x + 8.0f * scale,
            panel.y + ( row_top - 4.0f ) * scale + section_offset +
                static_cast<float>( index - first_visible_row_ ) * ( height + gap ),
            panel.w - 16.0f * scale,
            height
        };
    }

    std::int32_t GameplayVisualModule::visible_row_count() const
    {
        const rendering::Rect panel =
            GameplayVisualModule::settings_panel( last_output_width_, last_output_height_, full_width() );

        const float scale =
            rendering::make_logical_viewport( last_output_width_, last_output_height_, 1280.0f, 720.0f ).scale;

        const float section_space = view_ == View::Menu ? ( section_gap - row_gap ) * 2.0f : 0.0f;
        const float available_height = panel.h - ( row_top + 20.0f + section_space ) * scale;
        const float height = row_height_for( last_output_width_, last_output_height_ );
        const float gap = row_gap_for( last_output_width_, last_output_height_ );
        return std::clamp( static_cast<std::int32_t>( std::floor( ( available_height + gap ) / ( height + gap ) ) ),
            1,
            row_count() );
    }

    void GameplayVisualModule::ensure_selection_visible()
    {
        const std::int32_t visible = visible_row_count();
        first_visible_row_ = std::clamp( first_visible_row_, 0, row_count() - visible );

        if( selected_row_ < first_visible_row_ )
        {
            first_visible_row_ = selected_row_;
        }
        else if( selected_row_ >= first_visible_row_ + visible )
        {
            first_visible_row_ = selected_row_ - visible + 1;
        }
    }

    void GameplayVisualModule::scroll_rows( std::int32_t delta )
    {
        const std::int32_t visible = visible_row_count();
        first_visible_row_ = std::clamp( first_visible_row_ + delta, 0, row_count() - visible );

        selected_row_ = std::clamp( selected_row_, first_visible_row_, first_visible_row_ + visible - 1 );
    }

    rendering::Color GameplayVisualModule::array_color( const std::array<float, 4> &value )
    {
        return rendering::Color{ value[ 0 ], value[ 1 ], value[ 2 ], value[ 3 ] };
    }

    std::string GameplayVisualModule::format_float( float value, std::int32_t precision )
    {
        char buffer[ 32 ]{};
        std::snprintf( buffer, sizeof( buffer ), precision == 1 ? "%.1f" : "%.2f", value );
        return buffer;
    }

    std::string GameplayVisualModule::hex_color( const std::array<float, 4> &value )
    {
        const auto byte = []( float channel )
        {
            return static_cast<std::uint32_t>( std::clamp( channel, 0.0f, 1.0f ) * 255.0f + 0.5f );
        };

        char buffer[ 16 ]{};
        std::snprintf( buffer,
            sizeof( buffer ),
            "#%02X%02X%02X%02X",
            byte( value[ 0 ] ),
            byte( value[ 1 ] ),
            byte( value[ 2 ] ),
            byte( value[ 3 ] ) );

        return buffer;
    }

    std::optional<std::array<float, 4>> GameplayVisualModule::parse_hex_color( std::string_view text, float alpha )
    {
        const auto first = text.find_first_not_of( " \t\r\n" );

        if( first == std::string_view::npos )
        {
            return std::nullopt;
        }

        text = text.substr( first, text.find_last_not_of( " \t\r\n" ) - first + 1 );

        if( text.front() == '#' )
        {
            text.remove_prefix( 1 );
        }

        if( text.size() != 6 && text.size() != 8 )
        {
            return std::nullopt;
        }

        std::uint32_t packed = 0;

        const auto parsed = std::from_chars( text.data(), text.data() + text.size(), packed, 16 );

        if( parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() )
        {
            return std::nullopt;
        }

        if( text.size() == 8 )
        {
            alpha = static_cast<float>( packed & 255u ) / 255.0f;
            packed >>= 8;
        }

        return std::array<float, 4>{
            static_cast<float>( ( packed >> 16 ) & 255u ) / 255.0f,
            static_cast<float>( ( packed >> 8 ) & 255u ) / 255.0f,
            static_cast<float>( packed & 255u ) / 255.0f,
            alpha
        };
    }

    std::array<float, 3> GameplayVisualModule::rgb_to_hsv( const std::array<float, 4> &color )
    {
        const float maximum = std::max( color[ 0 ], std::max( color[ 1 ], color[ 2 ] ) );
        const float minimum = std::min( color[ 0 ], std::min( color[ 1 ], color[ 2 ] ) );
        const float delta = maximum - minimum;
        float hue = 0.0f;

        if( delta > 0.00001f )
        {
            if( maximum == color[ 0 ] )
            {
                hue = std::fmod( ( color[ 1 ] - color[ 2 ] ) / delta, 6.0f );
            }
            else if( maximum == color[ 1 ] )
            {
                hue = ( color[ 2 ] - color[ 0 ] ) / delta + 2.0f;
            }
            else
            {
                hue = ( color[ 0 ] - color[ 1 ] ) / delta + 4.0f;
            }

            hue /= 6.0f;

            if( hue < 0.0f )
            {
                hue += 1.0f;
            }
        }

        const float saturation = maximum <= 0.00001f ? 0.0f : delta / maximum;
        return { hue, saturation, maximum };
    }

    rendering::Color GameplayVisualModule::hsv_to_color( float hue, float saturation, float value, float alpha )
    {
        hue -= std::floor( hue );

        saturation = std::clamp( saturation, 0.0f, 1.0f );

        value = std::clamp( value, 0.0f, 1.0f );
        const float chroma = value * saturation;
        const float sector = hue * 6.0f;
        const float x = chroma * ( 1.0f - std::fabs( std::fmod( sector, 2.0f ) - 1.0f ) );
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;

        if( sector < 1.0f )
        {
            r = chroma;

            g = x;
        }
        else if( sector < 2.0f )
        {
            r = x;

            g = chroma;
        }
        else if( sector < 3.0f )
        {
            g = chroma;

            b = x;
        }
        else if( sector < 4.0f )
        {
            g = x;

            b = chroma;
        }
        else if( sector < 5.0f )
        {
            r = x;

            b = chroma;
        }
        else
        {
            r = chroma;

            b = x;
        }

        const float match = value - chroma;
        return rendering::Color{ r + match, g + match, b + match, alpha };
    }

    void GameplayVisualModule::push_quad( std::vector<rendering::VertexColor2D> &vertices,
        const rendering::Rect &rect,
        const rendering::Color &top_left,
        const rendering::Color &top_right,
        const rendering::Color &bottom_right,
        const rendering::Color &bottom_left )
    {
        const rendering::Vec2 p0{ rect.x, rect.y };
        const rendering::Vec2 p1{ rect.x + rect.w, rect.y };
        const rendering::Vec2 p2{ rect.x + rect.w, rect.y + rect.h };
        const rendering::Vec2 p3{ rect.x, rect.y + rect.h };
        vertices.push_back( { p0, top_left } );
        vertices.push_back( { p1, top_right } );
        vertices.push_back( { p2, bottom_right } );
        vertices.push_back( { p0, top_left } );
        vertices.push_back( { p2, bottom_right } );
        vertices.push_back( { p3, bottom_left } );
    }

    rendering::Rect GameplayVisualModule::picker_panel( std::int32_t output_width, std::int32_t output_height )
    {
        const auto layout = rendering::make_logical_viewport( output_width, output_height, 1280.0f, 720.0f );
        return {
            ( output_width - picker_width * layout.scale ) * 0.5f,
            ( output_height - picker_height * layout.scale ) * 0.5f,
            picker_width * layout.scale,
            picker_height * layout.scale
        };
    }

    rendering::Rect GameplayVisualModule::picker_rect( const rendering::Rect &panel,
        float x,
        float y,
        float w,
        float h )
    {
        const float scale = panel.w / picker_width;
        return { panel.x + x * scale, panel.y + y * scale, w * scale, h * scale };
    }

    rendering::Rect GameplayVisualModule::picker_sv_rect( const rendering::Rect &panel )
    {
        return picker_rect( panel, 24.0f, 80.0f, 300.0f, 300.0f );
    }

    rendering::Rect GameplayVisualModule::picker_hue_rect( const rendering::Rect &panel )
    {
        return picker_rect( panel, 338.0f, 80.0f, 22.0f, 300.0f );
    }

    rendering::Rect GameplayVisualModule::picker_code_rect( const rendering::Rect &panel )
    {
        return picker_rect( panel, 24.0f, 396.0f, 224.0f, 36.0f );
    }

    rendering::Rect GameplayVisualModule::picker_copy_rect( const rendering::Rect &panel )
    {
        return picker_rect( panel, 268.0f, 396.0f, 96.0f, 36.0f );
    }

    rendering::Rect GameplayVisualModule::picker_paste_rect( const rendering::Rect &panel )
    {
        return picker_rect( panel, 374.0f, 396.0f, 102.0f, 36.0f );
    }

    rendering::Rect GameplayVisualModule::picker_done_rect( const rendering::Rect &panel )
    {
        return picker_rect( panel, 134.0f, 444.0f, 108.0f, 28.0f );
    }

    rendering::Rect GameplayVisualModule::picker_cancel_rect( const rendering::Rect &panel )
    {
        return picker_rect( panel, 258.0f, 444.0f, 108.0f, 28.0f );
    }

    rendering::Rect GameplayVisualModule::picker_preset_rect( const rendering::Rect &panel, std::int32_t index )
    {
        return picker_rect( panel, 408.0f, 82.0f + static_cast<float>( index ) * 38.0f, 32.0f, 32.0f );
    }

    void GameplayVisualModule::draw_rounded_panel( rendering::Renderer &renderer, const rendering::Rect &rect )
    {
        const auto rounded_fill = [ & ]( const rendering::Rect &box, float radius, const rendering::Color &color )
        {
            renderer.draw_filled_rect( rendering::Rect{ box.x + radius, box.y, box.w - radius * 2.0f, box.h }, color );
            renderer.draw_filled_rect( rendering::Rect{ box.x, box.y + radius, box.w, box.h - radius * 2.0f }, color );

            for( std::int32_t y = 0; y < static_cast<std::int32_t>( radius ); ++y )
            {
                const float distance = radius - static_cast<float>( y ) - 0.5f;
                const float inset = radius - std::sqrt( radius * radius - distance * distance );
                renderer.draw_filled_rect(
                    rendering::Rect{ box.x + inset, box.y + static_cast<float>( y ), box.w - inset * 2.0f, 1.0f },
                    color );

                renderer.draw_filled_rect(
                    rendering::Rect{
                        box.x + inset,
                        box.y + box.h - static_cast<float>( y ) - 1.0f,
                        box.w - inset * 2.0f,
                        1.0f
                    },
                    color );
            }
        };

        rounded_fill( rect, 14.0f, rendering::Color{ 1.0f, 1.0f, 1.0f, 0.90f } );
        rounded_fill( rendering::Rect{ rect.x + 2.0f, rect.y + 2.0f, rect.w - 4.0f, rect.h - 4.0f },
            12.0f,
            rendering::Color{ 0.013f, 0.028f, 0.085f, 0.81f } );
    }

    std::string GameplayVisualModule::cached_text_name( const std::string &value,
        float pixel_height,
        TextStyle style,
        bool selected )
    {
        std::string name = "options/visual-editor/" +
                           std::to_string( static_cast<std::int32_t>( std::lround( pixel_height * 1000.0f ) ) ) + "/" +
                           std::to_string( utils::to_int( style ) ) + ( selected ? "/selected/" : "/normal/" );

        constexpr char hex[] = "0123456789ABCDEF";

        for( unsigned char byte : value )
        {
            name += hex[ byte >> 4 ];

            name += hex[ byte & 15u ];
        }

        return name;
    }

    void GameplayVisualModule::prepare_text( App &app,
        const std::string &value,
        float pixel_height,
        TextStyle style,
        TextEntry &entry )
    {
        constexpr float raster_scale = 3.0f;
        const float ui_scale =
            rendering::make_logical_viewport( last_output_width_, last_output_height_, 1280.0f, 720.0f ).scale;

        if( entry.text != value || entry.pixel_height != pixel_height || entry.style != style )
        {
            if( !entry.reusable )
            {
                app.texture_manager().unload( entry.normal );
                app.texture_manager().unload( entry.selected );
            }

            entry.normal = {};

            entry.selected = {};

            entry.text = value;

            entry.pixel_height = pixel_height;

            entry.style = style;

            entry.reusable = !value.empty() && value.front() != '#';
        }

        for( bool selected : { false, true } )
        {
            managers::TextureHandle &handle = selected ? entry.selected : entry.normal;

            if( !handle && title_font_.loaded() )
            {
                const std::string name = entry.reusable
                                             ? cached_text_name( value, pixel_height, style, selected )
                                             : "options/visual-editor-text-" +
                                                   std::to_string( reinterpret_cast<std::uintptr_t>( &entry ) ) +
                                                   ( selected ? "-selected" : "-normal" );

                if( entry.reusable )
                {
                    handle = app.texture_manager().find( localization::texture_key( name ) );
                }

                if( !handle )
                {
                    const rendering::Color face = style == TextStyle::Section ? rendering::color_from_rgb( 0x8E2943u )
                                                  : style == TextStyle::Heading
                                                      ? rendering::color_from_rgb( 0xEFBB4Bu )
                                                      : rendering::color_from_rgb( selected ? 0x19C615u : 0xEFBB4Bu );

                    const rendering::StrokedTextStyle text_style{
                        face,
                        rendering::color_from_rgb(
                            style == TextStyle::Section || ( style == TextStyle::Row && selected ) ? 0xFEF67Au
                                                                                                   : 0x0D005Au ),
                        rendering::color_from_rgb(
                            style == TextStyle::Section || ( style == TextStyle::Row && selected ) ? 0xFFC2FAu
                                                                                                   : 0x4287DDu ),
                        std::max( 1, static_cast<std::int32_t>( std::lround( 8.0f * ui_scale ) ) ),
                        std::max( 1, static_cast<std::int32_t>( std::lround( 17.0f * ui_scale ) ) ),
                        1.5f
                    };

                    const rendering::RasterizedText raster =
                        title_font_.rasterize_stroked( value, pixel_height * raster_scale, text_style );

                    if( raster.valid() )
                    {
                        handle = app.texture_manager().create_rgba( localization::texture_key( name ),
                            raster.rgba.data(),
                            raster.width,
                            raster.height );
                    }
                }

                if( entry.reusable && handle )
                {
                    presentation_->retain_texture( handle );
                }
            }
        }
    }

    void GameplayVisualModule::prepare_row_text( App &app, float text_scale )
    {
        const std::int32_t first = view_ == View::FontList ? std::max( 0, first_visible_row_ - 2 ) : 0;
        const std::int32_t last = view_ == View::FontList
                                      ? std::min( row_count(), first_visible_row_ + visible_row_count() + 2 )
                                      : row_count();

        const float label_height = label_text_size * text_scale;

        for( std::int32_t index = first; index < last; ++index )
        {
            const auto slot = static_cast<std::size_t>( index );
            prepare_text( app, row_label( index ), label_height, TextStyle::Row, label_text_[ slot ] );

            const std::string value = row_value( app, index );

            if( !value.empty() )
            {
                prepare_text( app, value, value_text_size * text_scale, TextStyle::Row, value_text_[ slot ] );
            }
        }
    }

    float GameplayVisualModule::draw_stroked_text( App &app,
        rendering::Renderer &renderer,
        const std::string &value,
        float x,
        float center_y,
        float pixel_height,
        bool selected,
        TextEntry &entry,
        bool right_aligned,
        TextStyle style,
        float maximum_width,
        bool centered )
    {
        constexpr float raster_scale = 3.0f;
        prepare_text( app, value, pixel_height, style, entry );
        const managers::TextureHandle handle = selected ? entry.selected : entry.normal;

        if( const rendering::Texture *texture = app.texture_manager().get( handle ) )
        {
            const float natural_width = static_cast<float>( texture->width ) / raster_scale;
            const float fit = maximum_width > 0.0f ? std::min( 1.0f, maximum_width / natural_width ) : 1.0f;
            const float width = natural_width * fit;
            const float height = static_cast<float>( texture->height ) / raster_scale * fit;
            const float left = centered ? x - width * 0.5f : right_aligned ? x - width : x;
            renderer.draw_texture( *texture, left, center_y - height * 0.5f, width, height );
            return width;
        }

        return 0.0f;
    }

    void GameplayVisualModule::draw_checkerboard( rendering::Renderer &renderer,
        const rendering::Rect &rect,
        float cell_size )
    {
        const std::int32_t columns = std::max( 1, static_cast<std::int32_t>( std::ceil( rect.w / cell_size ) ) );
        const std::int32_t rows = std::max( 1, static_cast<std::int32_t>( std::ceil( rect.h / cell_size ) ) );

        for( std::int32_t y = 0; y < rows; ++y )
        {
            for( std::int32_t x = 0; x < columns; ++x )
            {
                const float cell_x = rect.x + static_cast<float>( x ) * cell_size;
                const float cell_y = rect.y + static_cast<float>( y ) * cell_size;
                renderer.draw_filled_rect(
                    rendering::Rect{
                        cell_x,
                        cell_y,
                        std::min( cell_size, rect.x + rect.w - cell_x ),
                        std::min( cell_size, rect.y + rect.h - cell_y )
                    },
                    ( ( x + y ) & 1 ) == 0 ? rendering::Color{ 0.22f, 0.22f, 0.24f, 1.0f }
                                           : rendering::Color{ 0.42f, 0.42f, 0.45f, 1.0f } );
            }
        }
    }

    void GameplayVisualModule::draw_preview_cone_geometry( rendering::Renderer &renderer,
        const managers::GameSettings &settings,
        const rendering::Vec2 &marker,
        float cone_degrees,
        float geometry_scale,
        float length_scale,
        float maximum_length,
        float off_center_mix,
        const rendering::Texture *arrow_texture,
        const rendering::Texture *wing_texture )
    {
        const managers::GameplayVisualStyle &style = settings.gameplay_visual;
        const float cone_radians = cone_degrees / stage::radians_to_degrees;
        const float display_half_angle = style.cone_angle * 0.5f;

        const float half_angle_radians = display_half_angle / stage::radians_to_degrees;
        const float half_angle_cos = std::cos( half_angle_radians );
        const float half_angle_sin = std::sin( half_angle_radians );
        const float cone_length = std::min( style.cone_length * length_scale, maximum_length );
        const rendering::Vec2 axis{ std::cos( cone_radians ), std::sin( cone_radians ) };
        const rendering::Vec2 perpendicular{ -axis.y, axis.x };
        const rendering::Vec2 upper_direction{
            axis.x * half_angle_cos - perpendicular.x * half_angle_sin,
            axis.y * half_angle_cos - perpendicular.y * half_angle_sin
        };

        const rendering::Vec2 lower_direction{
            axis.x * half_angle_cos + perpendicular.x * half_angle_sin,
            axis.y * half_angle_cos + perpendicular.y * half_angle_sin
        };

        const auto starts =
            stage::view_cone_start_points( marker, perpendicular, style.cone_inner_diameter * geometry_scale );

        const rendering::Vec2 upper_start = starts.upper;
        const rendering::Vec2 lower_start = starts.lower;

        const rendering::Vec2 upper_end{
            marker.x + upper_direction.x * cone_length,
            marker.y + upper_direction.y * cone_length
        };

        const rendering::Vec2 lower_end{
            marker.x + lower_direction.x * cone_length,
            marker.y + lower_direction.y * cone_length
        };

        const auto mix_color =
            [ off_center_mix ]( const std::array<float, 4> &on_target, const std::array<float, 4> &off_target )
        {
            return rendering::Color{
                std::lerp( on_target[ 0 ], off_target[ 0 ], off_center_mix ),
                std::lerp( on_target[ 1 ], off_target[ 1 ], off_center_mix ),
                std::lerp( on_target[ 2 ], off_target[ 2 ], off_center_mix ),
                std::lerp( on_target[ 3 ], off_target[ 3 ], off_center_mix )
            };
        };

        const rendering::Color fill_color =
            mix_color( settings.view_cone_on_target_fill_color, settings.view_cone_off_target_fill_color );

        const rendering::Color line_color =
            mix_color( settings.view_cone_on_target_line_color, settings.view_cone_off_target_line_color );

        stage::draw_view_cone_gradient( renderer,
            marker,
            upper_start,
            lower_start,
            cone_radians,
            half_angle_radians,
            cone_length,
            fill_color );

        stage::draw_view_cone_border( renderer,
            upper_start,
            upper_end,
            style.cone_line_width * geometry_scale,
            line_color );

        stage::draw_view_cone_border( renderer,
            lower_start,
            lower_end,
            style.cone_line_width * geometry_scale,
            line_color );

        stage::draw_view_cone_arrows( renderer,
            marker,
            axis,
            perpendicular,
            1.0f,
            1.0f,
            cone_length * half_angle_sin,
            geometry_scale,
            style,
            arrow_texture,
            wing_texture );
    }

    struct GameplayVisualModule::PreviewTraceLine
    {
        std::vector<runtime::stage::TraceSample> samples;
        runtime::stage::TracePosition current_position{};
        std::vector<runtime::stage::TraceRenderPoint> render_points;
    };

    GameplayVisualModule::PreviewTraceLine GameplayVisualModule::make_preview_trace_line(
        const runtime::stage::StageGameplayView &view,
        float line_length_pixels,
        float wave_amplitude_pixels,
        std::int64_t current_chart_sample,
        std::int64_t total_chart_samples )
    {
        constexpr std::int64_t lookahead_samples = stage::trace_lookahead_samples;
        const std::int32_t sample_count = total_chart_samples > 0 ? 192 : 72;
        const std::int64_t chart_span = total_chart_samples > 0 ? total_chart_samples : lookahead_samples;

        PreviewTraceLine line{};
        line.samples.reserve( sample_count );

        const float trace_world_length = line_length_pixels / std::max( view.zoom * view.scale_x, 0.0001f );

        for( std::int32_t index = 0; index < sample_count; ++index )
        {
            const float t = static_cast<float>( index ) / static_cast<float>( sample_count - 1 );
            const float sample_position = static_cast<float>( chart_span ) * t;
            const float wave =
                wave_amplitude_pixels * std::sin( sample_position / static_cast<float>( lookahead_samples ) * tau );

            const float world_wave = wave / std::max( view.zoom * view.scale_y * view.presentation_scale_y, 0.0001f );
            line.samples.push_back( runtime::stage::TraceSample{
                .chart_sample = static_cast<std::int64_t>( sample_position ),
                .x = trace_world_length * sample_position / static_cast<float>( lookahead_samples ),
                .y = world_wave,
                .angle = 0.0f
            } );
        }

        for( std::size_t index = 0; index < line.samples.size(); ++index )
        {
            const std::size_t before = index == 0 ? index : index - 1;
            const std::size_t after = std::min( index + 1, line.samples.size() - 1 );
            line.samples[ index ].angle = std::atan2( line.samples[ after ].y - line.samples[ before ].y,
                line.samples[ after ].x - line.samples[ before ].x );
        }

        line.current_position = runtime::stage::visual_trace_position_at( line.samples, current_chart_sample );

        line.render_points = runtime::stage::make_gameplay_trace_render_points( line.samples,
            line.current_position,
            current_chart_sample,
            view,
            lookahead_samples,
            stage::trace_fade_start_samples,
            stage::trace_min_alpha,
            stage::trace_max_alpha );

        return line;
    }

    void GameplayVisualModule::draw_preview_trace_line( rendering::Renderer &renderer,
        const rendering::Texture *trace_texture,
        const managers::GameplayVisualStyle &style,
        const runtime::stage::StageGameplayView &view,
        const PreviewTraceLine &line,
        const rendering::Color &trace_color )
    {
        const float trace_dimension_scale = std::max( 0.25f, view.zoom ) * stage::trace_line_visual_scale;

        if( trace_texture != nullptr && trace_texture->valid() )
        {
            if( style.trace_scale > 0.001f )
            {
                stage::draw_trace_render_points( renderer,
                    line.render_points,
                    stage::trace_line_glow_width * style.trace_scale * trace_dimension_scale,
                    rendering::Color{
                        trace_color.r,
                        trace_color.g,
                        trace_color.b,
                        stage::trace_line_glow_alpha * trace_color.a
                    },
                    1.0f,
                    &view );
            }

            if( stage::draw_generated_trace_render_points( renderer,
                    *trace_texture,
                    line.render_points,
                    stage::trace_line_glow_width * style.trace_scale * trace_dimension_scale,
                    trace_color,
                    1.0f,
                    rendering::Vec2{ view.presentation_center_x, view.presentation_center_y },
                    view.presentation_scale_x,
                    view.presentation_scale_y ) )
            {
                return;
            }
        }

        stage::draw_trace_render_points( renderer,
            line.render_points,
            stage::trace_line_glow_width * style.trace_scale * trace_dimension_scale,
            rendering::Color{ 0.32f, 0.72f, 1.0f, stage::trace_line_glow_alpha },
            1.0f,
            &view );

        stage::draw_trace_render_points( renderer,
            line.render_points,
            ( stage::trace_line_width + 2.0f ) * style.trace_scale * trace_dimension_scale,
            rendering::Color{ 0.10f, 0.32f, 0.70f, 0.58f },
            1.0f,
            &view );

        stage::draw_trace_render_points( renderer,
            line.render_points,
            stage::trace_line_width * style.trace_scale * trace_dimension_scale,
            trace_color,
            1.0f,
            &view );
    }

    void GameplayVisualModule::draw_preview( rendering::Renderer &renderer,
        const rendering::Rect &panel,
        View view,
        const managers::GameSettings &settings,
        const rendering::Texture *trace_texture,
        const rendering::Texture *tube_texture,
        const rendering::Texture *guard_texture,
        const rendering::Texture *arrow_texture,
        const rendering::Texture *wing_texture,
        const rendering::Texture *normal_nub_texture,
        const rendering::Texture *pressed_nub_texture,
        const stage::GuardIconTextures *custom_guard_icons,
        float time_seconds,
        const ui::NoteTextureRegions &texture_regions )
    {
        const managers::GameplayVisualStyle &style = settings.gameplay_visual;
        const float preview_scale = panel.w / 374.0f;
        const float window_height = ( panel.h - 18.0f * preview_scale ) * 0.5f;
        const rendering::Rect idle_window{ panel.x, panel.y, panel.w, window_height };
        const rendering::Rect
            locked_window{ panel.x, panel.y + window_height + 18.0f * preview_scale, panel.w, window_height };

        const rendering::Rect content{
            locked_window.x + 22.0f * preview_scale,
            locked_window.y + 42.0f * preview_scale,
            std::max( 1.0f, locked_window.w - 44.0f * preview_scale ),
            std::max( 1.0f, locked_window.h - 52.0f * preview_scale )
        };

        const float logical_scale =
            rendering::make_logical_viewport( renderer.output_width(), renderer.output_height(), 1280.0f, 720.0f )
                .scale *
            ( 720.0f / stage::gameplay_logical_height );

        const float gameplay_zoom = logical_scale * stage::default_debug_zoom_multiplier;
        const float aim_visual_scale = logical_scale * stage::aim_marker_visual_scale;

        const rendering::Vec2 idle_marker{
            idle_window.x + 94.0f * preview_scale,
            idle_window.y + idle_window.h * 0.50f
        };

        const rendering::Vec2 locked_marker{
            locked_window.x + 94.0f * preview_scale,
            locked_window.y + locked_window.h * 0.50f
        };

        const float line_right = panel.x + panel.w - 12.0f * preview_scale;

        runtime::stage::StageGameplayView idle_view{};
        idle_view.center_x = idle_marker.x;

        idle_view.center_y = idle_marker.y;

        idle_view.zoom = gameplay_zoom;

        idle_view.note_speed = style.note_speed;

        idle_view.scale_x = stage::gameplay_trace_scale_x;

        idle_view.scale_y = stage::gameplay_trace_scale_y;

        idle_view.presentation_center_x = idle_marker.x;

        idle_view.presentation_center_y = idle_marker.y;

        idle_view.presentation_scale_x = stage::gameplay_presentation_scale_x;

        idle_view.presentation_scale_y = stage::gameplay_presentation_scale_y;

        const float state_time = std::fmod( time_seconds, 2.0f );
        constexpr float turn_seconds = 0.18f;
        float off_range_amount = 0.0f;

        if( state_time < turn_seconds )
        {
            off_range_amount = 1.0f - piko::utils::smoothstep( state_time / turn_seconds );
        }
        else if( state_time >= 1.0f && state_time < 1.0f + turn_seconds )
        {
            off_range_amount = piko::utils::smoothstep( ( state_time - 1.0f ) / turn_seconds );
        }
        else if( state_time >= 1.0f + turn_seconds )
        {
            off_range_amount = 1.0f;
        }

        const rendering::Color locked_trace = GameplayVisualModule::array_color( style.trace_locked_color );
        const rendering::Color lost_trace = GameplayVisualModule::array_color( style.trace_lost_color );
        const rendering::Color idle_trace_color{
            std::lerp( locked_trace.r, lost_trace.r, off_range_amount ),
            std::lerp( locked_trace.g, lost_trace.g, off_range_amount ),
            std::lerp( locked_trace.b, lost_trace.b, off_range_amount ),
            std::lerp( locked_trace.a, lost_trace.a, off_range_amount )
        };

        const float snap_half_angle =
            settings.snap_half_angle_for( managers::AimDifficulty::Normal, settings.chart_style );

        const float idle_cone_degrees = -( snap_half_angle + 10.0f ) * off_range_amount;
        const float idle_line_length = std::max( 1.0f, line_right - idle_marker.x );
        const PreviewTraceLine idle_trace =
            GameplayVisualModule::make_preview_trace_line( idle_view, idle_line_length );

        const float preview_cone_length = 190.0f * logical_scale;

        if( view == View::Guard )
        {
            renderer.push_clip_rect( rendering::Rect{
                panel.x + 3.0f,
                panel.y + 3.0f,
                std::max( 0.0f, panel.w - 6.0f ),
                std::max( 0.0f, panel.h - 6.0f )
            } );

            runtime::stage::StageGameplayView guard_view = idle_view;
            guard_view.center_x = panel.x + panel.w * 0.5f;

            guard_view.center_y = panel.y + panel.h * 0.55f;

            guard_view.zoom = gameplay_zoom;
            stage::draw_guard_section_rails( renderer,
                guard_view,
                panel,
                2.0f,
                style.guard_rail_scale,
                array_color( style.guard_rail_color ) );

            constexpr std::int64_t guard_loop_samples = stage::sample_rate * 4;
            constexpr std::array<std::uint32_t, 4> guard_order{
                3u,
                1u,
                0u,
                2u
            };

            const std::int64_t motion_sample =
                static_cast<std::int64_t>( std::fmod( static_cast<double>( time_seconds ) * stage::sample_rate,
                    static_cast<double>( guard_loop_samples ) ) );

            filetypes::chc::Chart guard_chart{};
            std::vector<stage::GuardHitVisualOverride> hits;

            for( std::size_t index = 0; index < guard_order.size(); ++index )
            {
                const std::uint32_t button = guard_order[ index ];
                std::int64_t start_sample =
                    18000 + static_cast<std::int64_t>( index ) * stage::sample_rate - motion_sample;
                while( start_sample <
                       -static_cast<std::int64_t>( stage::guard_visual_post_hit_samples * style.guard_hit_fade_scale ) )
                {
                    start_sample += guard_loop_samples;
                }

                guard_chart.guards.push_back( filetypes::chc::GuardFragment{
                    .start_sample = static_cast<std::int32_t>( start_sample ),
                    .button = button
                } );

                if( start_sample < 0 )
                {
                    hits.push_back( stage::GuardHitVisualOverride{
                        .start_sample = start_sample,
                        .button = button,
                        .hit_sample = start_sample
                    } );
                }
            }

            stage::draw_guard_fragments( renderer,
                guard_chart,
                0,
                guard_view,
                240.0f,
                guard_texture,
                &hits,
                false,
                style.guard_note_scale,
                style.guard_hit_pop_scale,
                style.guard_hit_fade_scale,
                style.guard_icon_set.empty() ? nullptr : custom_guard_icons );

            renderer.pop_clip_rect();
            return;
        }

        renderer.push_clip_rect( rendering::Rect{
            idle_window.x + 3.0f,
            idle_window.y + 3.0f,
            std::max( 0.0f, idle_window.w - 6.0f ),
            std::max( 0.0f, idle_window.h - 6.0f )
        } );

        if( view == View::Cone || view == View::AimArrow || view == View::Nub || view == View::Line ||
            view == View::Notes )
        {
            GameplayVisualModule::draw_preview_cone_geometry( renderer,
                settings,
                idle_marker,
                view == View::Line ? idle_cone_degrees : 0.0f,
                aim_visual_scale,
                logical_scale,
                preview_cone_length,
                1.0f,
                arrow_texture,
                wing_texture );
        }

        if( view == View::Line || view == View::Notes )
        {
            GameplayVisualModule::draw_preview_trace_line( renderer,
                trace_texture,
                style,
                idle_view,
                idle_trace,
                view == View::Line ? GameplayVisualModule::array_color( style.trace_idle_color ) : idle_trace_color );
        }

        stage::draw_view_cone_center_nub( renderer,
            idle_marker,
            false,
            aim_visual_scale,
            style.center_nub_scale,
            style.pressed_nub_scale,
            normal_nub_texture,
            pressed_nub_texture,
            guard_texture );

        renderer.pop_clip_rect();

        runtime::stage::StageGameplayView gameplay_view{};
        gameplay_view.center_x = locked_marker.x;

        gameplay_view.center_y = locked_marker.y;

        gameplay_view.zoom = gameplay_zoom;

        gameplay_view.note_speed = style.note_speed;

        gameplay_view.scale_x = stage::gameplay_trace_scale_x;

        gameplay_view.scale_y = stage::gameplay_trace_scale_y;

        gameplay_view.presentation_center_x = locked_marker.x;

        gameplay_view.presentation_center_y = locked_marker.y;

        gameplay_view.presentation_scale_x = stage::gameplay_presentation_scale_x;

        gameplay_view.presentation_scale_y = stage::gameplay_presentation_scale_y;

        const float line_length_pixels = std::max( 1.0f, line_right - locked_marker.x );
        constexpr std::int64_t long_loop_samples = 200000;
        const std::int64_t current_chart_sample =
            view == View::Notes ? static_cast<std::int64_t>( std::fmod( static_cast<double>( time_seconds ) * 32000.0,
                                      static_cast<double>( long_loop_samples ) ) )
                                : 0;

        const PreviewTraceLine preview_trace = GameplayVisualModule::make_preview_trace_line( gameplay_view,
            line_length_pixels,
            view == View::Notes ? 28.0f * preview_scale : 0.0f,
            current_chart_sample,
            view == View::Notes ? long_loop_samples + stage::trace_lookahead_samples : 0 );

        const float note_tangent_degrees =
            view == View::Notes ? stage::trace_tangent_degrees_at( preview_trace.current_position, gameplay_view )
                                : 0.0f;

        const float note_angle_delta = std::abs( note_tangent_degrees );
        const bool note_locked = note_angle_delta <= snap_half_angle;
        const float note_fade_t =
            std::clamp( ( note_angle_delta - 5.0f ) / std::max( snap_half_angle + 5.0f, 0.001f ), 0.0f, 1.0f );

        const float note_off_center_mix = piko::utils::smoothstep( note_fade_t );

        renderer.push_clip_rect( rendering::Rect{
            locked_window.x + 3.0f,
            locked_window.y + 3.0f,
            std::max( 0.0f, locked_window.w - 6.0f ),
            std::max( 0.0f, locked_window.h - 6.0f )
        } );

        if( view == View::Cone || view == View::AimArrow || view == View::Nub || view == View::Line ||
            view == View::Notes )
        {
            GameplayVisualModule::draw_preview_cone_geometry( renderer,
                settings,
                locked_marker,
                view == View::Line                   ? idle_cone_degrees
                : view == View::Notes && note_locked ? note_tangent_degrees
                                                     : 0.0f,
                aim_visual_scale,
                logical_scale,
                preview_cone_length,
                view == View::Notes  ? note_off_center_mix
                : view == View::Line ? off_range_amount
                                     : 0.0f,
                arrow_texture,
                wing_texture );
        }

        if( view == View::Line || view == View::Notes )
        {
            GameplayVisualModule::draw_preview_trace_line( renderer,
                trace_texture,
                style,
                gameplay_view,
                preview_trace,
                view == View::Line                    ? idle_trace_color
                : view == View::Notes && !note_locked ? lost_trace
                                                      : locked_trace );
        }

        if( view != View::Notes )
        {
            stage::draw_view_cone_center_nub( renderer,
                locked_marker,
                true,
                aim_visual_scale,
                style.center_nub_scale,
                style.pressed_nub_scale,
                normal_nub_texture,
                pressed_nub_texture,
                guard_texture );

            renderer.pop_clip_rect();
            return;
        }

        constexpr std::int64_t long_start_sample = 32000;
        constexpr std::uint32_t long_duration_samples = 142000;

        filetypes::chc::Chart long_chart{};
        long_chart.start_sample = 0;

        long_chart.end_sample = static_cast<std::uint32_t>( long_loop_samples );
        long_chart.phrases.push_back( filetypes::chc::PhraseFragment{
            .start_sample = static_cast<std::int32_t>( long_start_sample ),
            .duration_samples = long_duration_samples,
            .phrase_start = 1,
            .phrase_end = 1
        } );

        const bool simulated_button_down = current_chart_sample >= long_start_sample &&
                                           current_chart_sample < long_start_sample + long_duration_samples;

        const std::vector<stage::PhraseVisualOverride> long_overrides{
            stage::PhraseVisualOverride{ .start_sample = long_start_sample, .end_sample = long_start_sample + long_duration_samples, .darkened = false, .held = simulated_button_down }
        };

        stage::draw_phrase_fragments( renderer,
            long_chart,
            preview_trace.samples,
            preview_trace.current_position,
            current_chart_sample,
            gameplay_view,
            content,
            time_seconds,
            tube_texture,
            nullptr,
            stage::phrase_tube_visual_trim_pixels,
            stage::phrase_tube_cap_consumes_body_default,
            &long_overrides,
            false,
            settings.phrase_note_color_index,
            settings.phrase_note_held_color_index,
            false,
            120.0f,
            false,
            nullptr,
            style.note_scale,
            GameplayVisualModule::array_color( style.note_color ),
            GameplayVisualModule::array_color( style.held_note_color ),
            GameplayVisualModule::array_color( style.note_body_color ),
            texture_regions );

        renderer.pop_clip_rect();
        renderer.push_clip_rect( rendering::Rect{
            idle_window.x + 3.0f,
            idle_window.y + 3.0f,
            std::max( 0.0f, idle_window.w - 6.0f ),
            std::max( 0.0f, idle_window.h - 6.0f )
        } );

        constexpr std::int64_t single_loop_samples = 110000;
        const std::int64_t single_motion_sample = static_cast<std::int64_t>(
            std::fmod( static_cast<double>( time_seconds ) * 22000.0, static_cast<double>( single_loop_samples ) ) );

        for( std::int32_t index = 0; index < 3; ++index )
        {
            std::int64_t start_sample = 13000 + index * 33000 - single_motion_sample;
            while( start_sample < 0 )
            {
                start_sample += single_loop_samples;
            }

            if( start_sample > stage::trace_lookahead_samples )
            {
                continue;
            }

            const runtime::stage::TracePosition note_position =
                runtime::stage::visual_trace_position_at( idle_trace.samples, start_sample );

            const rendering::Vec2 position = runtime::stage::gameplay_world_to_screen( note_position.x,
                note_position.y,
                idle_trace.current_position,
                idle_view );

            stage::draw_textured_phrase_note( renderer,
                tube_texture,
                position,
                time_seconds,
                stage::PhraseNoteAppearance{
                    .scale = std::max( 0.25f, idle_view.zoom ) * stage::phrase_note_visual_scale * style.note_scale,
                    .alpha = 1.0f,
                    .presentation =
                        stage::RibbonPresentation{
                            .center =
                                rendering::Vec2{ idle_view.presentation_center_x, idle_view.presentation_center_y },
                            .scale_x = idle_view.presentation_scale_x,
                            .scale_y = idle_view.presentation_scale_y
                        },
                    .tint = GameplayVisualModule::array_color( style.note_color ),
                    .shadow = true,
                    .source = texture_regions.note,
                    .height = texture_regions.note_height
                } );
        }

        stage::draw_view_cone_center_nub( renderer,
            idle_marker,
            false,
            aim_visual_scale,
            style.center_nub_scale,
            style.pressed_nub_scale,
            normal_nub_texture,
            pressed_nub_texture,
            guard_texture );

        renderer.pop_clip_rect();
        renderer.push_clip_rect( rendering::Rect{
            locked_window.x + 3.0f,
            locked_window.y + 3.0f,
            std::max( 0.0f, locked_window.w - 6.0f ),
            std::max( 0.0f, locked_window.h - 6.0f )
        } );

        stage::draw_view_cone_center_nub( renderer,
            locked_marker,
            simulated_button_down,
            aim_visual_scale,
            style.center_nub_scale,
            style.pressed_nub_scale,
            normal_nub_texture,
            pressed_nub_texture,
            guard_texture );

        renderer.pop_clip_rect();
    }

    bool GameplayVisualModule::full_width() const
    {
        return view_ == View::Menu || view_ == View::AimMenu;
    }

    void GameplayVisualModule::apply_interface_style( App &app )
    {
        localization::set_interface_style( app.settings().interface_styles[ localization::language_id() ] );
        title_font_.clear();
        localization::load_menu_font( title_font_ );
        font_preview_path_.clear();

        const auto clear_text = [ & ]( TextEntry &entry )
        {
            if( !entry.reusable )
            {
                app.texture_manager().unload( entry.normal );
                app.texture_manager().unload( entry.selected );
            }

            entry = {};
        };

        for( auto &entry : label_text_ )
        {
            clear_text( entry );
        }

        for( auto &entry : value_text_ )
        {
            clear_text( entry );
        }

        for( auto &entry : heading_text_ )
        {
            clear_text( entry );
        }

        managers::save_game_settings( app.settings() );
    }

    void GameplayVisualModule::open_font_list( const App &app, managers::InterfaceFont target )
    {
        font_return_view_ = view_;

        font_target_ = target;

        font_files_ = localization::font_files();
        set_view( View::FontList );
        label_text_.resize( std::max( label_text_.size(), static_cast<std::size_t>( row_count() ) ) );
        value_text_.resize( label_text_.size() );

        const auto &chosen =
            app.settings().interface_styles.at( localization::language_id() ).fonts[ utils::enum_index( target ) ];

        for( std::size_t index = 0; index < font_files_.size(); ++index )
        {
            if( font_files_[ index ].path == chosen )
            {
                selected_row_ = static_cast<std::int32_t>( index ) + 1;
                break;
            }
        }

        ensure_selection_visible();
    }

    void GameplayVisualModule::close_submenu()
    {
        if( view_ == View::FontList )
        {
            set_view( font_return_view_ );
        }
        else if( view_ == View::Cone || view_ == View::AimArrow || view_ == View::Nub )
        {
            set_view( View::AimMenu );
        }
        else
        {
            set_view( View::Menu );
        }
    }

    void GameplayVisualModule::draw_interface_preview( App &app,
        rendering::Renderer &renderer,
        const rendering::Rect &panel )
    {
        draw_rounded_panel( renderer, panel );

        if( row_kind( selected_row_ ) == SettingRow::Back )
        {
            return;
        }

        auto path = font_target_ == managers::InterfaceFont::Menu ? localization::default_font_path()
                                                                  : localization::font_path();

        if( selected_row_ > 0 && selected_row_ < row_count() - 1 )
        {
            path =
                app.game_data().game_data_root() / font_files_.at( static_cast<std::size_t>( selected_row_ - 1 ) ).path;
        }

        if( font_preview_path_ != path.string() )
        {
            app.texture_manager().unload( font_preview_ );
            font_preview_ = {};

            font_preview_path_ = path.string();
            rendering::TrueTypeFont font;

            if( font.load( path ) )
            {
                localization::add_menu_font_fallbacks( font );

                const auto style = localization::label_style( "great" );

                const auto raster =
                    font.rasterize_stroked( localization::text( "@ui.interface.sample" ), 90.0f, style.stroke );

                if( raster.valid() )
                {
                    font_preview_ = app.texture_manager().create_rgba( "options/font-preview",
                        raster.rgba.data(),
                        raster.width,
                        raster.height );
                }
            }
        }

        const rendering::Rect
            bounds{ panel.x + panel.w * 0.08f, panel.y + panel.h * 0.15f, panel.w * 0.84f, panel.h * 0.7f };

        localization::draw_label( renderer, app.texture_manager().get( font_preview_ ), bounds );
    }

    struct GameplayVisualModule::FileDialogState
    {
        std::mutex mutex;
        bool complete = false;
        FileDialogAction action = FileDialogAction::LoadProfile;
        std::string path;
        std::string error;
    };

    void GameplayVisualModule::file_dialog_callback( void *userdata, const char *const *files, std::int32_t )
    {
        std::unique_ptr<std::shared_ptr<FileDialogState>> holder(
            static_cast<std::shared_ptr<FileDialogState> *>( userdata ) );

        const auto state = *holder;
        std::scoped_lock lock( state->mutex );

        if( files == nullptr )
        {
            state->error = SDL_GetError();
        }
        else if( files[ 0 ] != nullptr )
        {
            state->path = files[ 0 ];
        }

        state->complete = true;
    }

    void GameplayVisualModule::open_file_dialog( App &app, FileDialogAction action )
    {
        if( file_dialog_ )
        {
            return;
        }

        const bool saving = action == FileDialogAction::SaveProfile;

        const auto directory = managers::visual_profile_directory();

        std::error_code error;
        std::filesystem::create_directories( directory, error );
        file_dialog_ = std::make_shared<FileDialogState>();

        file_dialog_->action = action;
        auto *holder = new std::shared_ptr<FileDialogState>( file_dialog_ );
        const SDL_DialogFileFilter filters[]{
            {
                localization::text( "@ui.interface.profile_files" ),
                "json"
            }
        };

        const auto location_utf8 = ( saving ? directory / "style.json" : directory ).u8string();
        const std::string location( reinterpret_cast<const char *>( location_utf8.data() ), location_utf8.size() );

        if( saving )
        {
            SDL_ShowSaveFileDialog( file_dialog_callback, holder, app.window(), filters, 1, location.c_str() );
        }
        else
        {
            SDL_ShowOpenFileDialog( file_dialog_callback, holder, app.window(), filters, 1, location.c_str(), false );
        }
    }

    void GameplayVisualModule::consume_file_dialog( App &app )
    {
        const auto state = file_dialog_;

        if( !state )
        {
            return;
        }

        std::scoped_lock lock( state->mutex );

        if( !state->complete )
        {
            return;
        }

        file_dialog_.reset();

        if( state->path.empty() )
        {
            profile_status_ = state->error;
            return;
        }

        auto path = utils::path_from_utf8( state->path );

        const bool saving = state->action == FileDialogAction::SaveProfile;

        if( saving && path.extension().empty() )
        {
            path += ".json";
        }

        std::string error;
        const bool success = saving ? managers::save_visual_profile( app.settings(), path, &error )
                                    : managers::load_visual_profile( app.settings(), path, &error );

        if( success && !saving )
        {
            refresh_guard_icons( app );
            refresh_aim_styles( app );
            refresh_nub_textures( app );
            refresh_note_textures( app );
            apply_interface_style( app );
        }

        profile_status_ = success ? localization::text( saving ? "@ui.profile_saved" : "@ui.profile_loaded" ) : error;

        play_menu_sound( app, success ? "OK" : "SELECT" );
    }

    void GameplayVisualModule::refresh_note_textures( App &app )
    {
        app.texture_manager().unload( preview_trace_texture_ );
        app.texture_manager().unload( preview_tube_texture_ );
        preview_trace_texture_ = {};

        preview_tube_texture_ = {};

        note_style_names_ = stage::note_style_names( app.game_data().resource_root() );
        auto &selected = app.settings().gameplay_visual.note_style;
        match_style_name( note_style_names_, selected );

        try
        {
            preview_trace_texture_ = stage::load_trace_texture( app.texture_manager(),
                app.game_data().game_data_root(),
                "options/trace-preview",
                selected );

            preview_tube_texture_ = stage::load_note_textures( app.texture_manager(),
                app.game_data().game_data_root(),
                preview_note_regions_,
                "options/note-preview",
                selected );
        }
        catch( const std::exception &error )
        {
            profile_status_ = error.what();
        }
    }

    void GameplayVisualModule::refresh_nub_textures( App &app )
    {
        app.texture_manager().unload( preview_normal_nub_texture_ );
        app.texture_manager().unload( preview_pressed_nub_texture_ );
        centre_style_names_ = stage::centre_style_names( app.game_data().resource_root() );

        match_style_name( centre_style_names_, app.settings().gameplay_visual.centre_icon_set );

        preview_normal_nub_texture_ = stage::load_centre_sprite( app.texture_manager(),
            app.game_data().game_data_root(),
            "interface/centre",
            app.settings().gameplay_visual,
            false );

        preview_pressed_nub_texture_ = stage::load_centre_sprite( app.texture_manager(),
            app.game_data().game_data_root(),
            "interface/centre-pressed",
            app.settings().gameplay_visual,
            true );
    }

    void GameplayVisualModule::refresh_aim_styles( App &app )
    {
        aim_style_names_ = stage::aim_style_names( app.game_data().resource_root() );

        match_style_name( aim_style_names_, app.settings().gameplay_visual.aim_style );
        app.texture_manager().unload( preview_arrow_texture_ );
        app.texture_manager().unload( preview_wing_texture_ );
        preview_arrow_texture_ = stage::load_aim_sprite( app.texture_manager(),
            app.game_data().resource_root(),
            "interface/aim",
            "AimArrow.png",
            app.settings().gameplay_visual.aim_style );

        preview_wing_texture_ = stage::load_aim_sprite( app.texture_manager(),
            app.game_data().resource_root(),
            "interface/wing",
            "WingArrow.png",
            app.settings().gameplay_visual.aim_style );
    }

    void GameplayVisualModule::refresh_guard_icons( App &app )
    {
        guard_icon_set_names_ = stage::guard_icon_set_names( app.game_data().resource_root() );

        match_style_name( guard_icon_set_names_, app.settings().gameplay_visual.guard_icon_set );

        stage::load_guard_icon_set( app.texture_manager(),
            app.game_data().resource_root(),
            app.settings().gameplay_visual.guard_icon_set,
            "options/custom-guard-icon-",
            preview_custom_guard_handles_ );
    }

    void GameplayVisualModule::enter( App &app )
    {
        view_ = View::Menu;

        navigation_ = &app.menu_navigation();
        profile_status_.clear();

        const auto position = navigation_->restore( managers::MenuLocation::Interface, row_count() );
        selected_row_ = position.selected_row;

        first_visible_row_ = position.first_visible_row;
        ensure_selection_visible();

        scrollbar_dragging_ = false;

        elapsed_seconds_ = 0.0;

        color_target_ = ColorTarget::None;

        picker_drag_ = PickerDrag::None;
        presentation_->initialize( app );
        app.settings().interface_styles.try_emplace( localization::language_id() );
        guard_icon_set_names_ = stage::guard_icon_set_names( app.game_data().resource_root() );
        refresh_guard_icons( app );
        refresh_aim_styles( app );

        const std::filesystem::path font_path = piko::utils::find_resource_file( app.game_data().application_root(),
            std::filesystem::path( "fonts" ) / "Anak Paud.ttf" );

        if( !font_path.empty() )
        {
            localization::load_menu_font( title_font_, font_path );
        }

        preview_guard_texture_ = app.texture_manager().find( "options/guard-preview" );

        if( !preview_guard_texture_ )
        {
            try
            {
                for( std::int32_t stage_number = 0; stage_number <= 12 && !preview_guard_texture_; ++stage_number )
                {
                    char group[ 16 ]{};
                    std::snprintf( group, sizeof( group ), "STAGE%02d", stage_number );
                    std::filesystem::path guard_path =
                        app.game_data().runtime_stage_texture( stage_number, group, "ITFC.png" );

                    if( guard_path.empty() && ( stage_number == 11 || stage_number == 12 ) )
                    {
                        char filename[ 16 ]{};
                        std::snprintf( filename, sizeof( filename ), "ST%02d.png", stage_number );
                        guard_path = app.game_data().runtime_stage_texture( stage_number, "HUD", filename );
                    }

                    if( !guard_path.empty() )
                    {
                        preview_guard_texture_ = app.texture_manager().load_png( "options/guard-preview", guard_path );
                    }
                }
            }
            catch( const std::exception & )
            {
                preview_guard_texture_ = {};
            }
        }

        refresh_nub_textures( app );

        refresh_note_textures( app );
    }

    void GameplayVisualModule::leave( App &app )
    {
        focus_picker_code( app, false );
        navigation_->remember( menu_locations_[ utils::enum_index( view_ ) ], selected_row_, first_visible_row_ );

        file_dialog_.reset();
        app.texture_manager().unload( font_preview_ );
        font_preview_ = {};
        font_preview_path_.clear();

        app.texture_manager().unload( preview_arrow_texture_ );
        app.texture_manager().unload( preview_wing_texture_ );
        app.texture_manager().unload( preview_normal_nub_texture_ );
        app.texture_manager().unload( preview_pressed_nub_texture_ );
        preview_normal_nub_texture_ = {};

        preview_pressed_nub_texture_ = {};

        preview_arrow_texture_ = {};

        preview_wing_texture_ = {};

        stage::unload_guard_icon_set( app.texture_manager(), preview_custom_guard_handles_ );
        guard_icon_set_names_.clear();

        centre_style_names_.clear();
        note_style_names_.clear();

        aim_style_names_.clear();

        if( presentation_.use_count() == 1 )
        {
            presentation_->shutdown( app );
        }

        title_font_.clear();

        const auto unload_text = [ & ]( TextEntry &entry )
        {
            if( !entry.reusable )
            {
                app.texture_manager().unload( entry.normal );
                app.texture_manager().unload( entry.selected );
            }

            entry = {};
        };

        for( TextEntry &entry : label_text_ )
        {
            unload_text( entry );
        }

        for( TextEntry &entry : value_text_ )
        {
            unload_text( entry );
        }

        for( TextEntry &entry : heading_text_ )
        {
            unload_text( entry );
        }

        for( TextEntry &entry : picker_text_ )
        {
            unload_text( entry );
        }

        if( presentation_.use_count() > 1 )
        {
            presentation_->retain_texture( preview_trace_texture_ );
            presentation_->retain_texture( preview_tube_texture_ );
            presentation_->retain_texture( preview_guard_texture_ );
        }
        else
        {
            app.texture_manager().unload( preview_trace_texture_ );
            app.texture_manager().unload( preview_tube_texture_ );
            app.texture_manager().unload( preview_guard_texture_ );
        }

        preview_trace_texture_ = {};

        preview_tube_texture_ = {};

        preview_guard_texture_ = {};
        save_game_settings( app.settings() );
    }

    void GameplayVisualModule::handle_event( App &app, const SDL_Event &event )
    {
        if( file_dialog_ )
        {
            return;
        }

        if( color_target_ != ColorTarget::None )
        {
            handle_picker_event( app, event );
            return;
        }

        if( event.type != SDL_EVENT_MOUSE_WHEEL )
        {
            return;
        }

        if( !rendering::point_inside( settings_panel( last_output_width_, last_output_height_, full_width() ),
                event.wheel.mouse_x,
                event.wheel.mouse_y ) )
        {
            return;
        }

        const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
        const std::int32_t old_selection = selected_row_;

        if( direction != 0.0f )
        {
            scroll_rows( direction > 0.0f ? -1 : 1 );
        }

        if( selected_row_ != old_selection )
        {
            play_menu_sound( app, "SELECT" );
        }
    }

    std::int32_t GameplayVisualModule::row_count() const
    {
        switch( view_ )
        {
            case View::Menu:
                return 9;
            case View::AimMenu:
                return 4;
            case View::FontList:
                return static_cast<std::int32_t>( font_files_.size() ) + 2;
            case View::Cone:
                return 10;
            case View::Nub:
                return 5;
            case View::AimArrow:
                return 10;
            case View::Line:
                return 6;
            case View::Notes:
                return 7;
            case View::Guard:
                return 9;
        }

        return 1;
    }

    GameplayVisualModule::SettingRow GameplayVisualModule::row_kind( std::int32_t index ) const
    {
        static constexpr std::array menu{
            SettingRow::MenuFont,
            SettingRow::HideBattleUi,
            SettingRow::AimEntry,
            SettingRow::TraceLineEntry,
            SettingRow::NotesEntry,
            SettingRow::GuardsEntry,
            SettingRow::SaveProfileEntry,
            SettingRow::LoadProfileEntry,
            SettingRow::Back
        };

        static constexpr std::array
            aiming{ SettingRow::VisionConeEntry, SettingRow::AimArrowEntry, SettingRow::NubEntry, SettingRow::Back };

        static constexpr std::array cone{
            SettingRow::ConeLength,
            SettingRow::ConeAngle,
            SettingRow::ConeInnerDiameter,
            SettingRow::ConeLineWidth,
            SettingRow::ConeIdleLine,
            SettingRow::ConeIdleFill,
            SettingRow::ConeLockedLine,
            SettingRow::ConeLockedFill,
            SettingRow::ResetDefault,
            SettingRow::Back
        };

        static constexpr std::array nub{
            SettingRow::CentreStyle,
            SettingRow::CenterNub,
            SettingRow::PressedNub,
            SettingRow::ResetDefault,
            SettingRow::Back
        };

        static constexpr std::array aim_arrow{
            SettingRow::WingStyle,
            SettingRow::ArrowScale,
            SettingRow::ArrowWidth,
            SettingRow::ArrowOffsetX,
            SettingRow::WingScale,
            SettingRow::WingWidth,
            SettingRow::WingSpread,
            SettingRow::WingOffsetX,
            SettingRow::ResetDefault,
            SettingRow::Back
        };

        static constexpr std::array line{
            SettingRow::TraceScale,
            SettingRow::TraceIdle,
            SettingRow::TraceLocked,
            SettingRow::TraceLost,
            SettingRow::ResetDefault,
            SettingRow::Back
        };

        static constexpr std::array notes{
            SettingRow::NoteStyle,
            SettingRow::NoteScale,
            SettingRow::Note,
            SettingRow::HeldNote,
            SettingRow::NoteBody,
            SettingRow::ResetDefault,
            SettingRow::Back
        };

        static constexpr std::array guard{
            SettingRow::GuardIcons,
            SettingRow::GuardSize,
            SettingRow::GuardPop,
            SettingRow::GuardFade,
            SettingRow::GuardRails,
            SettingRow::GuardRailColor,
            SettingRow::GuardDpad,
            SettingRow::ResetDefault,
            SettingRow::Back
        };

        switch( view_ )
        {
            case View::AimMenu:
                return aiming.at( static_cast<std::size_t>( index ) );
            case View::FontList:
                return index == row_count() - 1 ? SettingRow::Back : SettingRow::FontChoice;
            case View::Menu:
                return menu.at( static_cast<std::size_t>( index ) );
            case View::Cone:
                return cone.at( static_cast<std::size_t>( index ) );
            case View::AimArrow:
                return aim_arrow.at( static_cast<std::size_t>( index ) );
            case View::Nub:
                return nub.at( static_cast<std::size_t>( index ) );
            case View::Line:
                return line.at( static_cast<std::size_t>( index ) );
            case View::Notes:
                return notes.at( static_cast<std::size_t>( index ) );
            case View::Guard:
                return guard.at( static_cast<std::size_t>( index ) );
        }

        return SettingRow::Back;
    }

    const char *GameplayVisualModule::view_name( View view )
    {
        switch( view )
        {
            case View::Menu:
                return localization::text( "@ui.interface.customize" );
            case View::AimMenu:
                return localization::text( "@ui.interface.aiming_upper" );
            case View::FontList:
                return localization::text( "@ui.interface.choose_font" );
            case View::Cone:
                return localization::text( "@ui.vision_cone_upper" );
            case View::AimArrow:
                return localization::text( "@ui.interface.aim_arrow_upper" );
            case View::Nub:
                return localization::text( "@ui.centre_nub_upper" );
            case View::Line:
                return localization::text( "@ui.trace_line_upper" );
            case View::Notes:
                return localization::text( "@ui.notes_upper" );
            case View::Guard:
                return localization::text( "@ui.guards_upper" );
        }

        return "";
    }

    void GameplayVisualModule::play_menu_sound( App &app, const char *name )
    {
        presentation_->play_sound( app, name );
    }

    void GameplayVisualModule::move_selection( App &app, std::int32_t delta )
    {
        const std::int32_t count = row_count();
        selected_row_ = ( selected_row_ + delta ) % count;

        if( selected_row_ < 0 )
        {
            selected_row_ += count;
        }

        ensure_selection_visible();
        play_menu_sound( app, "SELECT" );
    }

    void GameplayVisualModule::set_selection( App &app, std::int32_t index )
    {
        if( index >= 0 && index < row_count() && index != selected_row_ )
        {
            selected_row_ = index;
            ensure_selection_visible();
            play_menu_sound( app, "SELECT" );
        }
    }

    void GameplayVisualModule::set_view( View view )
    {
        if( navigation_ )
        {
            navigation_->remember( menu_locations_[ utils::enum_index( view_ ) ], selected_row_, first_visible_row_ );
        }

        view_ = view;

        const auto position = navigation_
                                  ? navigation_->restore( menu_locations_[ utils::enum_index( view_ ) ], row_count() )
                                  : managers::MenuPosition{};

        selected_row_ = position.selected_row;

        first_visible_row_ = position.first_visible_row;
        ensure_selection_visible();

        scrollbar_dragging_ = false;
    }

    const char *GameplayVisualModule::row_label( std::int32_t index ) const
    {
        switch( row_kind( index ) )
        {
            case SettingRow::AimEntry:
                return localization::text( "@ui.interface.aiming" );
            case SettingRow::MenuFont:
                return localization::text( "@ui.interface.menu_font" );
            case SettingRow::HideBattleUi:
                return localization::text( "@ui.interface.hide_battle_ui" );
            case SettingRow::FontChoice:
                return index == 0 ? localization::text( "@ui.interface.language_default" )
                                  : font_files_.at( static_cast<std::size_t>( index - 1 ) ).name.c_str();
            case SettingRow::VisionConeEntry:
                return localization::text( "@ui.vision_cone" );
            case SettingRow::AimArrowEntry:
                return localization::text( "@ui.interface.aim_arrow" );
            case SettingRow::NubEntry:
                return localization::text( "@ui.centre_nub" );
            case SettingRow::TraceLineEntry:
                return localization::text( "@ui.trace_line" );
            case SettingRow::NotesEntry:
                return localization::text( "@ui.notes" );
            case SettingRow::GuardsEntry:
                return localization::text( "@ui.guards" );
            case SettingRow::SaveProfileEntry:
                return localization::text( "@ui.save_visual_profile" );
            case SettingRow::LoadProfileEntry:
                return localization::text( "@ui.load_visual_profile" );
            case SettingRow::TraceScale:
                return localization::text( "@ui.trace_scale" );
            case SettingRow::NoteScale:
                return localization::text( "@ui.note_and_tube_size" );
            case SettingRow::NoteStyle:
                return localization::text( "@ui.interface.note_style" );
            case SettingRow::CentreStyle:
                return localization::text( "@ui.interface.centre_style" );
            case SettingRow::CenterNub:
                return localization::text( "@ui.centre_nub_size" );
            case SettingRow::PressedNub:
                return localization::text( "@ui.pressed_nub_size" );
            case SettingRow::WingScale:
                return localization::text( "@ui.interface.wing_size" );
            case SettingRow::WingWidth:
                return localization::text( "@ui.interface.wing_width" );
            case SettingRow::ArrowOffsetX:
                return localization::text( "@ui.interface.arrow_offset_x" );
            case SettingRow::WingOffsetX:
                return localization::text( "@ui.interface.wing_offset_x" );
            case SettingRow::ConeAngle:
                return localization::text( "@ui.vision_cone_angle" );
            case SettingRow::ConeLength:
                return localization::text( "@ui.vision_cone_length" );
            case SettingRow::ConeInnerDiameter:
                return localization::text( "@ui.vision_cone_inner_diameter" );
            case SettingRow::ConeLineWidth:
                return localization::text( "@ui.vision_cone_border" );
            case SettingRow::ArrowScale:
                return localization::text( "@ui.aim_arrow_size" );
            case SettingRow::ArrowWidth:
                return localization::text( "@ui.aim_arrow_width" );
            case SettingRow::WingStyle:
                return localization::text( "@ui.interface.wing_style" );
            case SettingRow::WingSpread:
                return localization::text( "@ui.wing_arrow_spread" );
            case SettingRow::TraceIdle:
                return localization::text( "@ui.trace_normal" );
            case SettingRow::TraceLocked:
                return localization::text( "@ui.trace_locked" );
            case SettingRow::TraceLost:
                return localization::text( "@ui.trace_out_of_range" );
            case SettingRow::Note:
                return localization::text( "@ui.note_head" );
            case SettingRow::HeldNote:
                return localization::text( "@ui.note_on_hit_hold" );
            case SettingRow::NoteBody:
                return localization::text( "@ui.note_body" );
            case SettingRow::ConeIdleLine:
                return localization::text( "@ui.normal_border" );
            case SettingRow::ConeIdleFill:
                return localization::text( "@ui.normal_fill" );
            case SettingRow::ConeLockedLine:
                return localization::text( "@ui.on_aim_border" );
            case SettingRow::ConeLockedFill:
                return localization::text( "@ui.on_aim_fill" );
            case SettingRow::GuardDpad:
                return localization::text( "@ui.guard_accepts_d_pad" );
            case SettingRow::GuardSize:
                return localization::text( "@ui.guard_note_size" );
            case SettingRow::GuardIcons:
                return localization::text( "@ui.face_button_style" );
            case SettingRow::GuardPop:
                return localization::text( "@ui.hit_pop_size" );
            case SettingRow::GuardFade:
                return localization::text( "@ui.hit_fade_duration" );
            case SettingRow::GuardRails:
                return localization::text( "@ui.guard_rail_width" );
            case SettingRow::GuardRailColor:
                return localization::text( "@ui.guard_rail_colour" );
            case SettingRow::ResetDefault:
                return localization::text( "@ui.reset_default" );
            case SettingRow::Back:
                return localization::text( "@ui.back" );
        }

        return "";
    }

    std::string GameplayVisualModule::row_value( const App &app, std::int32_t index ) const
    {
        const managers::GameplayVisualStyle &style = app.settings().gameplay_visual;

        const auto &interface = app.settings().interface_styles.at( localization::language_id() );

        switch( row_kind( index ) )
        {
            case SettingRow::MenuFont:
            {
                managers::InterfaceFont role = managers::InterfaceFont::Menu;

                const auto &path = interface.fonts[ utils::enum_index( role ) ];
                return path.empty() ? localization::text( "@ui.interface.language_default" )
                                    : std::filesystem::path( path ).filename().string();
            }
            case SettingRow::FontChoice:
            {
                const auto &path = interface.fonts[ utils::enum_index( font_target_ ) ];
                const bool chosen =
                    index == 0 ? path.empty() : path == font_files_.at( static_cast<std::size_t>( index - 1 ) ).path;

                return chosen ? localization::text( "@ui.interface.selected" ) : "";
            }
            case SettingRow::AimEntry:
            case SettingRow::VisionConeEntry:
            case SettingRow::AimArrowEntry:
            case SettingRow::NubEntry:
            case SettingRow::TraceLineEntry:
            case SettingRow::NotesEntry:
            case SettingRow::GuardsEntry:
            case SettingRow::SaveProfileEntry:
            case SettingRow::LoadProfileEntry:
                return {};
            case SettingRow::TraceScale:
                return format_float( style.trace_scale );
            case SettingRow::NoteScale:
                return format_float( style.note_scale );
            case SettingRow::GuardSize:
                return format_float( style.guard_note_scale );
            case SettingRow::GuardIcons:
                return style.guard_icon_set.empty() ? localization::text( "@ui.original_upper" ) : style.guard_icon_set;
            case SettingRow::GuardPop:
                return format_float( style.guard_hit_pop_scale );
            case SettingRow::GuardFade:
                return format_float( style.guard_hit_fade_scale );
            case SettingRow::GuardRails:
                return format_float( style.guard_rail_scale );
            case SettingRow::NoteStyle:
                return style.note_style == "default" ? localization::text( "@ui.original_upper" ) : style.note_style;
            case SettingRow::CentreStyle:
                return style.centre_icon_set.empty() ? localization::text( "@ui.original_upper" )
                                                     : style.centre_icon_set;
            case SettingRow::CenterNub:
                return format_float( style.center_nub_scale );
            case SettingRow::PressedNub:
                return format_float( style.pressed_nub_scale );
            case SettingRow::WingScale:
                return format_float( style.cone_wing_scale );
            case SettingRow::WingWidth:
                return format_float( style.cone_wing_width_scale );
            case SettingRow::ArrowOffsetX:
                return format_float( style.arrow_offset_x );
            case SettingRow::WingOffsetX:
                return format_float( style.wing_offset_x );
            case SettingRow::ConeAngle:
                return format_float( style.cone_angle, 1 );
            case SettingRow::ConeLength:
                return format_float( style.cone_length, 1 );
            case SettingRow::ConeInnerDiameter:
                return format_float( style.cone_inner_diameter, 1 );
            case SettingRow::ConeLineWidth:
                return format_float( style.cone_line_width );
            case SettingRow::ArrowScale:
                return format_float( style.cone_arrow_scale );
            case SettingRow::ArrowWidth:
                return format_float( style.cone_arrow_width_scale );
            case SettingRow::WingStyle:
                return style.aim_style;
            case SettingRow::WingSpread:
                return format_float( style.cone_wing_spread_scale );
            case SettingRow::HideBattleUi:
                return app.settings().hide_battle_ui ? localization::text( "@ui.on_upper" )
                                                     : localization::text( "@ui.off_upper" );
            case SettingRow::GuardDpad:
                return app.settings().guard_notes_accept_dpad ? localization::text( "@ui.on_upper" )
                                                              : localization::text( "@ui.off_upper" );
            case SettingRow::ResetDefault:
            case SettingRow::Back:
                return {};
            default:
                break;
        }

        const auto color_for = [ & ]( ColorTarget target ) -> const std::array<float, 4> *
        {
            switch( target )
            {
                case ColorTarget::TraceIdle:
                    return &style.trace_idle_color;
                case ColorTarget::TraceLocked:
                    return &style.trace_locked_color;
                case ColorTarget::TraceLost:
                    return &style.trace_lost_color;
                case ColorTarget::Note:
                    return &style.note_color;
                case ColorTarget::HeldNote:
                    return &style.held_note_color;
                case ColorTarget::GuardRailColor:
                    return &style.guard_rail_color;
                case ColorTarget::NoteBody:
                    return &style.note_body_color;
                case ColorTarget::ConeIdleLine:
                    return &app.settings().view_cone_off_target_line_color;
                case ColorTarget::ConeIdleFill:
                    return &app.settings().view_cone_off_target_fill_color;
                case ColorTarget::ConeLockedLine:
                    return &app.settings().view_cone_on_target_line_color;
                case ColorTarget::ConeLockedFill:
                    return &app.settings().view_cone_on_target_fill_color;
                case ColorTarget::None:
                    break;
            }

            return nullptr;
        };

        if( const std::array<float, 4> *color = color_for( row_color_target( index ) ); color != nullptr )
        {
            return hex_color( *color );
        }

        return {};
    }

    GameplayVisualModule::ColorTarget GameplayVisualModule::row_color_target( std::int32_t index ) const
    {
        switch( row_kind( index ) )
        {
            case SettingRow::TraceIdle:
                return ColorTarget::TraceIdle;
            case SettingRow::TraceLocked:
                return ColorTarget::TraceLocked;
            case SettingRow::TraceLost:
                return ColorTarget::TraceLost;
            case SettingRow::Note:
                return ColorTarget::Note;
            case SettingRow::HeldNote:
                return ColorTarget::HeldNote;
            case SettingRow::GuardRailColor:
                return ColorTarget::GuardRailColor;
            case SettingRow::NoteBody:
                return ColorTarget::NoteBody;
            case SettingRow::ConeIdleLine:
                return ColorTarget::ConeIdleLine;
            case SettingRow::ConeIdleFill:
                return ColorTarget::ConeIdleFill;
            case SettingRow::ConeLockedLine:
                return ColorTarget::ConeLockedLine;
            case SettingRow::ConeLockedFill:
                return ColorTarget::ConeLockedFill;
            default:
                return ColorTarget::None;
        }
    }

    std::array<float, 4> *GameplayVisualModule::target_color( App &app )
    {
        managers::GameplayVisualStyle &style = app.settings().gameplay_visual;

        switch( color_target_ )
        {
            case ColorTarget::TraceIdle:
                return &style.trace_idle_color;
            case ColorTarget::TraceLocked:
                return &style.trace_locked_color;
            case ColorTarget::TraceLost:
                return &style.trace_lost_color;
            case ColorTarget::Note:
                return &style.note_color;
            case ColorTarget::HeldNote:
                return &style.held_note_color;
            case ColorTarget::GuardRailColor:
                return &style.guard_rail_color;
            case ColorTarget::NoteBody:
                return &style.note_body_color;
            case ColorTarget::ConeIdleLine:
                return &app.settings().view_cone_off_target_line_color;
            case ColorTarget::ConeIdleFill:
                return &app.settings().view_cone_off_target_fill_color;
            case ColorTarget::ConeLockedLine:
                return &app.settings().view_cone_on_target_line_color;
            case ColorTarget::ConeLockedFill:
                return &app.settings().view_cone_on_target_fill_color;
            case ColorTarget::None:
                break;
        }

        return nullptr;
    }

    const std::array<float, 4> *GameplayVisualModule::target_color( const App &app ) const
    {
        const managers::GameplayVisualStyle &style = app.settings().gameplay_visual;

        switch( color_target_ )
        {
            case ColorTarget::TraceIdle:
                return &style.trace_idle_color;
            case ColorTarget::TraceLocked:
                return &style.trace_locked_color;
            case ColorTarget::TraceLost:
                return &style.trace_lost_color;
            case ColorTarget::Note:
                return &style.note_color;
            case ColorTarget::HeldNote:
                return &style.held_note_color;
            case ColorTarget::GuardRailColor:
                return &style.guard_rail_color;
            case ColorTarget::NoteBody:
                return &style.note_body_color;
            case ColorTarget::ConeIdleLine:
                return &app.settings().view_cone_off_target_line_color;
            case ColorTarget::ConeIdleFill:
                return &app.settings().view_cone_off_target_fill_color;
            case ColorTarget::ConeLockedLine:
                return &app.settings().view_cone_on_target_line_color;
            case ColorTarget::ConeLockedFill:
                return &app.settings().view_cone_on_target_fill_color;
            case ColorTarget::None:
                break;
        }

        return nullptr;
    }

    void GameplayVisualModule::adjust_selected( App &app, std::int32_t direction )
    {
        const SettingRow row = row_kind( selected_row_ );

        if( row == SettingRow::NoteStyle )
        {
            if( !note_style_names_.empty() )
            {
                auto &selected = app.settings().gameplay_visual.note_style;

                const auto found = std::find( note_style_names_.begin(), note_style_names_.end(), selected );

                const auto current = static_cast<std::int32_t>( std::distance( note_style_names_.begin(), found ) );

                const auto count = static_cast<std::int32_t>( note_style_names_.size() );
                selected = note_style_names_[ ( current + direction % count + count ) % count ];
                refresh_note_textures( app );
                play_menu_sound( app, "SELECT" );
            }

            return;
        }

        if( row == SettingRow::CentreStyle )
        {
            auto &style = app.settings().gameplay_visual;
            std::int32_t current = 0;

            const auto found =
                std::find( centre_style_names_.begin(), centre_style_names_.end(), style.centre_icon_set );

            if( found != centre_style_names_.end() )
            {
                current = static_cast<std::int32_t>( std::distance( centre_style_names_.begin(), found ) ) + 1;
            }

            const auto count = static_cast<std::int32_t>( centre_style_names_.size() ) + 1;
            const std::int32_t next = ( current + direction % count + count ) % count;
            style.centre_icon_set = next > 0 ? centre_style_names_[ next - 1 ] : std::string{};

            refresh_nub_textures( app );
            play_menu_sound( app, "SELECT" );
            return;
        }

        if( row == SettingRow::WingStyle )
        {
            if( !aim_style_names_.empty() )
            {
                auto &selected = app.settings().gameplay_visual.aim_style;

                const auto found = std::find( aim_style_names_.begin(), aim_style_names_.end(), selected );

                const auto current = static_cast<std::int32_t>( std::distance( aim_style_names_.begin(), found ) );

                const auto count = static_cast<std::int32_t>( aim_style_names_.size() );
                selected = aim_style_names_[ ( current + direction % count + count ) % count ];
                refresh_aim_styles( app );
                play_menu_sound( app, "SELECT" );
            }

            return;
        }

        if( row == SettingRow::GuardIcons )
        {
            managers::GameplayVisualStyle &style = app.settings().gameplay_visual;
            std::int32_t current = 0;

            if( !style.guard_icon_set.empty() )
            {
                const auto found =
                    std::find( guard_icon_set_names_.begin(), guard_icon_set_names_.end(), style.guard_icon_set );

                if( found != guard_icon_set_names_.end() )
                {
                    current = static_cast<std::int32_t>( std::distance( guard_icon_set_names_.begin(), found ) ) + 1;
                }
            }

            const std::int32_t count = static_cast<std::int32_t>( guard_icon_set_names_.size() ) + 1;
            const std::int32_t next = ( current + direction % count + count ) % count;

            style.guard_icon_set = next >= 1 ? guard_icon_set_names_[ next - 1 ] : std::string{};
            refresh_guard_icons( app );
            play_menu_sound( app, "SELECT" );
            return;
        }

        if( row == SettingRow::HideBattleUi )
        {
            app.settings().hide_battle_ui = !app.settings().hide_battle_ui;
            play_menu_sound( app, "SELECT" );
            return;
        }

        if( row == SettingRow::GuardDpad )
        {
            app.settings().guard_notes_accept_dpad = !app.settings().guard_notes_accept_dpad;
            play_menu_sound( app, "SELECT" );
            return;
        }

        const float delta = static_cast<float>( direction );
        managers::GameplayVisualStyle &style = app.settings().gameplay_visual;

        switch( row )
        {
            case SettingRow::TraceScale:
                style.trace_scale = std::clamp( style.trace_scale + delta * 0.05f, 0.40f, 2.00f );
                break;
            case SettingRow::NoteScale:
                style.note_scale = std::clamp( style.note_scale + delta * 0.05f, 0.50f, 1.80f );
                break;
            case SettingRow::GuardSize:
                style.guard_note_scale = std::clamp( style.guard_note_scale + delta * 0.05f, 0.50f, 1.50f );
                break;
            case SettingRow::GuardPop:
                style.guard_hit_pop_scale = std::clamp( style.guard_hit_pop_scale + delta * 0.05f, 0.50f, 2.00f );
                break;
            case SettingRow::GuardFade:
                style.guard_hit_fade_scale = std::clamp( style.guard_hit_fade_scale + delta * 0.05f, 0.50f, 2.00f );
                break;
            case SettingRow::GuardRails:
                style.guard_rail_scale = std::clamp( style.guard_rail_scale + delta * 0.05f, 0.35f, 2.00f );
                break;
            case SettingRow::CenterNub:
                style.center_nub_scale = managers::GameplayVisualStyle::snap_icon_scale(
                    style.center_nub_scale + delta * managers::GameplayVisualStyle::icon_scale_step );
                break;
            case SettingRow::PressedNub:
                style.pressed_nub_scale = managers::GameplayVisualStyle::snap_icon_scale(
                    style.pressed_nub_scale + delta * managers::GameplayVisualStyle::icon_scale_step );
                break;
            case SettingRow::ConeAngle:
                style.cone_angle = std::clamp( style.cone_angle + delta, 10.0f, 160.0f );
                break;
            case SettingRow::ConeLength:
                style.cone_length = std::clamp( style.cone_length + delta * 5.0f, 60.0f, 240.0f );
                break;
            case SettingRow::ConeInnerDiameter:
                style.cone_inner_diameter = std::clamp( style.cone_inner_diameter + delta, 0.0f, 40.0f );
                break;
            case SettingRow::ConeLineWidth:
                style.cone_line_width = std::clamp( style.cone_line_width + delta * 0.25f, 1.0f, 8.0f );
                break;
            case SettingRow::ArrowScale:
                style.cone_arrow_scale = std::clamp( style.cone_arrow_scale + delta * 0.05f, 0.50f, 1.80f );
                break;
            case SettingRow::ArrowWidth:
                style.cone_arrow_width_scale = std::clamp( style.cone_arrow_width_scale + delta * 0.05f, 0.35f, 1.50f );
                break;
            case SettingRow::WingScale:
                style.cone_wing_scale = std::clamp( style.cone_wing_scale + delta * 0.05f, 0.50f, 1.80f );
                break;
            case SettingRow::WingWidth:
                style.cone_wing_width_scale = std::clamp( style.cone_wing_width_scale + delta * 0.05f, 0.35f, 1.50f );
                break;
            case SettingRow::ArrowOffsetX:
                style.arrow_offset_x = std::clamp( style.arrow_offset_x + delta * 1.00f, -60.00f, 60.00f );
                break;
            case SettingRow::WingOffsetX:
                style.wing_offset_x = std::clamp( style.wing_offset_x + delta * 1.00f, -60.00f, 60.00f );
                break;
            case SettingRow::WingSpread:
                style.cone_wing_spread_scale = std::clamp( style.cone_wing_spread_scale + delta * 0.05f, 0.45f, 1.60f );
                break;
            default:
                return;
        }

        play_menu_sound( app, "SELECT" );
    }

    void GameplayVisualModule::confirm_selection( App &app )
    {
        const SettingRow row = row_kind( selected_row_ );

        if( row == SettingRow::HideBattleUi )
        {
            adjust_selected( app, 1 );
            return;
        }

        if( row == SettingRow::MenuFont )
        {
            const auto target = managers::InterfaceFont::Menu;

            open_font_list( app, target );
            play_menu_sound( app, "OK" );
            return;
        }

        if( row == SettingRow::FontChoice )
        {
            const std::string path =
                selected_row_ == 0 ? "" : font_files_.at( static_cast<std::size_t>( selected_row_ - 1 ) ).path;

            rendering::TrueTypeFont font;

            if( !path.empty() && !font.load( app.game_data().game_data_root() / path ) )
            {
                profile_status_ = localization::text( "@ui.interface.invalid_font" );
                return;
            }

            app.settings().interface_styles[ localization::language_id() ].fonts[ utils::enum_index( font_target_ ) ] =
                path;

            apply_interface_style( app );
            close_submenu();
            play_menu_sound( app, "OK" );
            return;
        }

        if( row == SettingRow::ResetDefault )
        {
            managers::GameSettings &settings = app.settings();
            managers::GameplayVisualStyle &style = settings.gameplay_visual;
            const managers::GameSettings defaults{};
            const managers::GameplayVisualStyle &standard = defaults.gameplay_visual;

            switch( view_ )
            {
                case View::Cone:
                    style.cone_angle = standard.cone_angle;

                    style.cone_length = standard.cone_length;

                    style.cone_inner_diameter = standard.cone_inner_diameter;

                    style.cone_line_width = standard.cone_line_width;

                    settings.view_cone_off_target_line_color = defaults.view_cone_off_target_line_color;

                    settings.view_cone_off_target_fill_color = defaults.view_cone_off_target_fill_color;

                    settings.view_cone_on_target_line_color = defaults.view_cone_on_target_line_color;

                    settings.view_cone_on_target_fill_color = defaults.view_cone_on_target_fill_color;
                    break;
                case View::Nub:
                    style.centre_icon_set.clear();

                    style.center_nub_scale = standard.center_nub_scale;

                    style.pressed_nub_scale = standard.pressed_nub_scale;

                    refresh_nub_textures( app );
                    break;
                case View::AimArrow:
                    style.cone_arrow_scale = standard.cone_arrow_scale;

                    style.cone_arrow_width_scale = standard.cone_arrow_width_scale;

                    style.cone_wing_spread_scale = standard.cone_wing_spread_scale;

                    style.cone_wing_scale = standard.cone_wing_scale;

                    style.cone_wing_width_scale = standard.cone_wing_width_scale;

                    style.arrow_offset_x = standard.arrow_offset_x;

                    style.wing_offset_x = standard.wing_offset_x;

                    style.aim_style = standard.aim_style;
                    refresh_aim_styles( app );
                    break;
                case View::Line:

                    style.trace_scale = standard.trace_scale;

                    style.trace_idle_color = standard.trace_idle_color;

                    style.trace_locked_color = standard.trace_locked_color;

                    style.trace_lost_color = standard.trace_lost_color;
                    break;
                case View::Notes:

                    style.note_style = standard.note_style;
                    refresh_note_textures( app );

                    style.note_scale = standard.note_scale;

                    style.note_color = standard.note_color;

                    style.held_note_color = standard.held_note_color;

                    style.note_body_color = standard.note_body_color;
                    break;
                case View::Guard:

                    style.guard_icon_set = standard.guard_icon_set;

                    style.guard_note_scale = standard.guard_note_scale;

                    style.guard_hit_pop_scale = standard.guard_hit_pop_scale;

                    style.guard_hit_fade_scale = standard.guard_hit_fade_scale;

                    style.guard_rail_scale = standard.guard_rail_scale;

                    style.guard_rail_color = standard.guard_rail_color;

                    settings.guard_notes_accept_dpad = defaults.guard_notes_accept_dpad;
                    refresh_guard_icons( app );
                    break;
                default:
                    break;
            }

            play_menu_sound( app, "OK" );
            return;
        }

        if( row == SettingRow::Back )
        {
            play_menu_sound( app, "OK" );

            if( view_ == View::Menu )
            {
                app.change_module( std::make_unique<OptionsModule>( presentation_ ) );
            }
            else
            {
                close_submenu();
            }

            return;
        }

        if( view_ == View::Menu || view_ == View::AimMenu )
        {
            View next = view_;

            switch( row )
            {
                case SettingRow::AimEntry:
                    next = View::AimMenu;
                    break;
                case SettingRow::VisionConeEntry:
                    next = View::Cone;
                    break;
                case SettingRow::AimArrowEntry:
                    next = View::AimArrow;
                    break;
                case SettingRow::NubEntry:
                    next = View::Nub;
                    break;
                case SettingRow::TraceLineEntry:
                    next = View::Line;
                    break;
                case SettingRow::NotesEntry:
                    next = View::Notes;
                    break;
                case SettingRow::GuardsEntry:
                    next = View::Guard;
                    break;
                case SettingRow::SaveProfileEntry:
                    open_file_dialog( app, FileDialogAction::SaveProfile );
                    break;
                case SettingRow::LoadProfileEntry:
                    open_file_dialog( app, FileDialogAction::LoadProfile );
                    break;
                default:
                    break;
            }

            if( next != view_ )
            {
                set_view( next );
                play_menu_sound( app, "OK" );
            }

            return;
        }

        if( const ColorTarget target = row_color_target( selected_row_ ); target != ColorTarget::None )
        {
            play_menu_sound( app, "OK" );
            open_color_picker( app, target );
            return;
        }

        adjust_selected( app, 1 );
    }

    std::int32_t GameplayVisualModule::hit_test_row( float x, float y ) const
    {
        for( std::int32_t index = first_visible_row_; index < first_visible_row_ + visible_row_count(); ++index )
        {
            if( rendering::point_inside( row_rect( index, last_output_width_, last_output_height_ ), x, y ) )
            {
                return index;
            }
        }

        return -1;
    }

    bool GameplayVisualModule::row_adjustable( std::int32_t index ) const
    {
        switch( row_kind( index ) )
        {
            case SettingRow::TraceScale:
            case SettingRow::NoteStyle:
            case SettingRow::NoteScale:
            case SettingRow::CentreStyle:
            case SettingRow::CenterNub:
            case SettingRow::PressedNub:
            case SettingRow::ConeLength:
            case SettingRow::ConeAngle:
            case SettingRow::ConeInnerDiameter:
            case SettingRow::ConeLineWidth:
            case SettingRow::ArrowScale:
            case SettingRow::ArrowWidth:
            case SettingRow::WingStyle:
            case SettingRow::WingSpread:
            case SettingRow::WingScale:
            case SettingRow::WingWidth:
            case SettingRow::ArrowOffsetX:
            case SettingRow::WingOffsetX:
            case SettingRow::HideBattleUi:
            case SettingRow::GuardDpad:
            case SettingRow::GuardSize:
            case SettingRow::GuardIcons:
            case SettingRow::GuardPop:
            case SettingRow::GuardFade:
            case SettingRow::GuardRails:
                return true;
            default:
                return false;
        }
    }

    void GameplayVisualModule::set_picker_color( App &app, const std::array<float, 4> &color )
    {
        const auto hsv = rgb_to_hsv( color );
        picker_hue_ = hsv[ 0 ];

        picker_saturation_ = hsv[ 1 ];

        picker_value_ = hsv[ 2 ];

        picker_alpha_ = color[ 3 ];

        if( auto *target = target_color( app ) )
        {
            *target = color;
        }
    }

    void GameplayVisualModule::focus_picker_code( App &app, bool focused )
    {
        if( focused == picker_code_focused_ )
        {
            return;
        }

        picker_code_focused_ = focused;

        picker_code_selected_ = focused;

        if( focused )
        {
            SDL_StartTextInput( app.window() );
        }
        else
        {
            SDL_StopTextInput( app.window() );
        }
    }

    void GameplayVisualModule::edit_picker_code( App &app, std::string text )
    {
        picker_hex_text_ = text.substr( 0, 32 );

        const auto color = parse_hex_color( text, picker_alpha_ );
        picker_hex_valid_ = color.has_value();

        if( color )
        {
            set_picker_color( app, *color );
        }
    }

    void GameplayVisualModule::paste_picker_code( App &app )
    {
        char *text = SDL_GetClipboardText();

        if( text != nullptr )
        {
            edit_picker_code( app, text );
            SDL_free( text );
            picker_code_selected_ = true;
            focus_picker_code( app, true );
        }
    }

    void GameplayVisualModule::handle_picker_event( App &app, const SDL_Event &event )
    {
        if( event.type == SDL_EVENT_TEXT_INPUT && picker_code_focused_ )
        {
            std::string text = picker_code_selected_ ? std::string{} : picker_hex_text_;
            bool changed = false;

            for( const char *next = event.text.text; next != nullptr && *next != '\0'; ++next )
            {
                char digit = *next;

                if( digit >= 'a' && digit <= 'f' )
                {
                    digit = static_cast<char>( digit - 'a' + 'A' );
                }

                if( text.size() < 9 && ( ( digit >= '0' && digit <= '9' ) || ( digit >= 'A' && digit <= 'F' ) ||
                                           ( digit == '#' && text.empty() ) ) )
                {
                    text += digit;

                    changed = true;
                }
            }

            if( changed )
            {
                picker_code_selected_ = false;
                edit_picker_code( app, std::move( text ) );
            }

            picker_key_consumed_ = true;
            return;
        }

        std::int32_t hue_direction = 0;

        if( event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN )
        {
            if( event.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER )
            {
                hue_direction = -1;
            }
            else if( event.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER )
            {
                hue_direction = 1;
            }
        }
        else if( event.type == SDL_EVENT_KEY_DOWN )
        {
            const bool control = ( event.key.mod & SDL_KMOD_CTRL ) != 0;

            if( control && event.key.key == SDLK_C )
            {
                SDL_SetClipboardText( picker_hex_text_.c_str() );
                picker_key_consumed_ = true;
            }
            else if( control && event.key.key == SDLK_V )
            {
                paste_picker_code( app );
                picker_key_consumed_ = true;
            }
            else if( picker_code_focused_ )
            {
                if( control && event.key.key == SDLK_A )
                {
                    picker_code_selected_ = true;

                    picker_key_consumed_ = true;
                }
                else if( event.key.key == SDLK_BACKSPACE || event.key.key == SDLK_DELETE )
                {
                    std::string text = picker_code_selected_ ? std::string{} : picker_hex_text_;

                    if( !text.empty() )
                    {
                        text.pop_back();
                    }

                    picker_code_selected_ = false;
                    edit_picker_code( app, std::move( text ) );
                    picker_key_consumed_ = true;
                }
                else if( event.key.key == SDLK_TAB )
                {
                    focus_picker_code( app, false );
                    picker_key_consumed_ = true;
                }
            }
            else if( event.key.key == SDLK_TAB )
            {
                picker_hue_focused_ = !picker_hue_focused_;
            }
            else if( event.key.key == SDLK_PAGEUP || event.key.key == SDLK_PAGEDOWN )
            {
                hue_direction = event.key.key == SDLK_PAGEUP ? -1 : 1;
            }
        }

        if( hue_direction != 0 && !picker_code_focused_ )
        {
            picker_hue_ += static_cast<float>( hue_direction ) / 180.0f;

            picker_hue_ -= std::floor( picker_hue_ );
            apply_picker_color( app );
        }
    }

    void GameplayVisualModule::open_color_picker( App &app, ColorTarget target )
    {
        color_target_ = target;

        if( auto *color = target_color( app ) )
        {
            picker_original_color_ = *color;
            set_picker_color( app, *color );
            picker_hex_text_ = hex_color( *color );

            picker_hex_valid_ = true;

            picker_key_consumed_ = false;

            picker_hue_focused_ = false;

            picker_drag_ = PickerDrag::None;
        }
        else
        {
            color_target_ = ColorTarget::None;
        }
    }

    void GameplayVisualModule::close_color_picker( App &app, bool accept )
    {
        focus_picker_code( app, false );

        if( !accept )
        {
            if( auto *color = target_color( app ) )
            {
                *color = picker_original_color_;
            }
        }

        color_target_ = ColorTarget::None;

        picker_drag_ = PickerDrag::None;
    }

    void GameplayVisualModule::apply_picker_color( App &app )
    {
        if( auto *color = target_color( app ) )
        {
            const auto converted = hsv_to_color( picker_hue_, picker_saturation_, picker_value_, picker_alpha_ );
            *color = { converted.r, converted.g, converted.b, converted.a };
            picker_hex_text_ = hex_color( *color );

            picker_hex_valid_ = true;
        }
    }

    void GameplayVisualModule::update_color_picker( App &app )
    {
        const auto &input = app.input();

        const auto &mouse = input.mouse();

        const auto panel = picker_panel( last_output_width_, last_output_height_ );

        const auto sv = picker_sv_rect( panel );

        const auto hue = picker_hue_rect( panel );
        const bool key_consumed = std::exchange( picker_key_consumed_, false );

        if( mouse.left_clicked )
        {
            if( rendering::point_inside( picker_done_rect( panel ), mouse.x, mouse.y ) )
            {
                if( picker_hex_valid_ )
                {
                    play_menu_sound( app, "OK" );
                    close_color_picker( app, true );
                }
                else
                {
                    focus_picker_code( app, true );
                }

                return;
            }

            if( rendering::point_inside( picker_cancel_rect( panel ), mouse.x, mouse.y ) )
            {
                play_menu_sound( app, "SELECT" );
                close_color_picker( app, false );
                return;
            }

            if( rendering::point_inside( picker_code_rect( panel ), mouse.x, mouse.y ) )
            {
                focus_picker_code( app, true );
                picker_code_selected_ = true;
                return;
            }

            if( rendering::point_inside( picker_copy_rect( panel ), mouse.x, mouse.y ) )
            {
                SDL_SetClipboardText( picker_hex_text_.c_str() );
                play_menu_sound( app, "SELECT" );
                return;
            }

            if( rendering::point_inside( picker_paste_rect( panel ), mouse.x, mouse.y ) )
            {
                paste_picker_code( app );
                play_menu_sound( app, "SELECT" );
                return;
            }

            focus_picker_code( app, false );

            for( std::int32_t index = 0; index < static_cast<std::int32_t>( picker_presets.size() ); ++index )
            {
                if( rendering::point_inside( picker_preset_rect( panel, index ), mouse.x, mouse.y ) )
                {
                    set_picker_color( app, picker_presets[ index ] );
                    picker_hex_text_ = hex_color( picker_presets[ index ] );

                    picker_hex_valid_ = true;
                    play_menu_sound( app, "SELECT" );
                    return;
                }
            }

            if( rendering::point_inside( sv, mouse.x, mouse.y ) )
            {
                picker_drag_ = PickerDrag::SaturationValue;

                picker_hue_focused_ = false;
            }
            else if( rendering::point_inside( hue, mouse.x, mouse.y ) )
            {
                picker_drag_ = PickerDrag::Hue;

                picker_hue_focused_ = true;
            }
        }

        if( !mouse.left_down )
        {
            picker_drag_ = PickerDrag::None;
        }

        if( picker_drag_ == PickerDrag::SaturationValue )
        {
            picker_saturation_ = std::clamp( ( mouse.x - sv.x ) / sv.w, 0.0f, 1.0f );

            picker_value_ = 1.0f - std::clamp( ( mouse.y - sv.y ) / sv.h, 0.0f, 1.0f );
            apply_picker_color( app );
        }
        else if( picker_drag_ == PickerDrag::Hue )
        {
            picker_hue_ = std::clamp( ( mouse.y - hue.y ) / hue.h, 0.0f, 1.0f );
            apply_picker_color( app );
        }

        if( !picker_code_focused_ && !key_consumed )
        {
            const float horizontal = static_cast<float>( input.pressed( managers::InputAction::MenuRight ) ) -
                                     static_cast<float>( input.pressed( managers::InputAction::MenuLeft ) );

            const float vertical = static_cast<float>( input.pressed( managers::InputAction::MenuUp ) ) -
                                   static_cast<float>( input.pressed( managers::InputAction::MenuDown ) );

            if( horizontal != 0.0f || vertical != 0.0f )
            {
                if( picker_hue_focused_ )
                {
                    picker_hue_ += ( horizontal - vertical ) / 180.0f;

                    picker_hue_ -= std::floor( picker_hue_ );
                }
                else
                {
                    picker_saturation_ = std::clamp( picker_saturation_ + horizontal * 0.02f, 0.0f, 1.0f );

                    picker_value_ = std::clamp( picker_value_ + vertical * 0.02f, 0.0f, 1.0f );
                }

                apply_picker_color( app );
            }
        }

        if( !key_consumed && input.pressed( managers::InputAction::Confirm ) && !mouse.left_clicked &&
            picker_hex_valid_ )
        {
            play_menu_sound( app, "OK" );
            close_color_picker( app, true );
        }
        else if( ( !key_consumed && input.pressed( managers::InputAction::Back ) ) || mouse.right_clicked )
        {
            play_menu_sound( app, "SELECT" );
            close_color_picker( app, false );
        }
    }

    void GameplayVisualModule::update( App &app, double delta_seconds )
    {
        const bool dialog_was_open = static_cast<bool>( file_dialog_ );
        consume_file_dialog( app );

        if( dialog_was_open )
        {
            return;
        }

        elapsed_seconds_ += delta_seconds;
        presentation_->update( app, delta_seconds );

        if( color_target_ != ColorTarget::None )
        {
            update_color_picker( app );
            return;
        }

        const managers::InputManager &input = app.input();

        if( input.pressed( managers::InputAction::MenuUp ) )
        {
            move_selection( app, -1 );
        }

        if( input.pressed( managers::InputAction::MenuDown ) )
        {
            move_selection( app, 1 );
        }

        if( input.pressed( managers::InputAction::MenuLeft ) )
        {
            adjust_selected( app, -1 );
        }

        if( input.pressed( managers::InputAction::MenuRight ) )
        {
            adjust_selected( app, 1 );
        }

        const managers::MouseState &mouse = input.mouse();
        const std::int32_t visible = visible_row_count();
        const float row_height_pixels = row_height_for( last_output_width_, last_output_height_ );
        const float row_gap_pixels = row_gap_for( last_output_width_, last_output_height_ );

        if( visible < row_count() )
        {
            const rendering::Rect panel = settings_panel( last_output_width_, last_output_height_, full_width() );

            const float track_height =
                static_cast<float>( visible ) * ( row_height_pixels + row_gap_pixels ) - row_gap_pixels;

            const rendering::Rect track{
                panel.x + panel.w - 20.0f,
                panel.y + row_top * row_height_pixels / row_height,
                12.0f,
                track_height
            };

            if( mouse.left_clicked && rendering::point_inside( track, mouse.x, mouse.y ) )
            {
                scrollbar_dragging_ = true;
            }

            if( scrollbar_dragging_ && mouse.left_down )
            {
                const float thumb_height =
                    track_height * static_cast<float>( visible ) / static_cast<float>( row_count() );

                const float progress = std::clamp( ( mouse.y - track.y - thumb_height * 0.5f ) /
                                                       std::max( 1.0f, track_height - thumb_height ),
                    0.0f,
                    1.0f );

                const std::int32_t first =
                    static_cast<std::int32_t>( std::lround( progress * static_cast<float>( row_count() - visible ) ) );

                const std::int32_t old_selection = selected_row_;
                scroll_rows( first - first_visible_row_ );

                if( selected_row_ != old_selection && mouse.left_clicked )
                {
                    play_menu_sound( app, "SELECT" );
                }
            }
        }

        if( !mouse.left_down )
        {
            scrollbar_dragging_ = false;
        }

        if( mouse.moved )
        {
            const std::int32_t hovered = hit_test_row( mouse.x, mouse.y );

            if( hovered >= 0 )
            {
                set_selection( app, hovered );
            }
        }

        if( mouse.left_clicked )
        {
            const std::int32_t clicked = hit_test_row( mouse.x, mouse.y );

            if( clicked >= 0 )
            {
                set_selection( app, clicked );

                const auto box = row_rect( clicked, last_output_width_, last_output_height_ );

                const auto layout =
                    rendering::make_logical_viewport( last_output_width_, last_output_height_, 1280.0f, 720.0f );

                const auto direction =
                    rendering::menu_theme::adjustment_direction( box, layout.scale, mouse.x, mouse.y );

                if( row_adjustable( clicked ) && direction != 0 )
                {
                    adjust_selected( app, direction );
                }
                else
                {
                    confirm_selection( app );
                }
            }
        }
        else if( input.pressed( managers::InputAction::Confirm ) )
        {
            confirm_selection( app );
        }

        if( input.pressed( managers::InputAction::Back ) && !mouse.right_clicked )
        {
            play_menu_sound( app, "OK" );

            if( view_ == View::Menu )
            {
                app.change_module( std::make_unique<OptionsModule>( presentation_ ) );
            }
            else
            {
                close_submenu();
            }
        }
    }

    void GameplayVisualModule::render( App &app, rendering::Renderer &renderer )
    {
        last_output_width_ = std::max( renderer.output_width(), 1 );

        last_output_height_ = std::max( renderer.output_height(), 1 );
        ensure_selection_visible();
        const float pulse = 0.5f + 0.5f * std::sin( static_cast<float>( elapsed_seconds_ ) * 3.0f );
        const rendering::LogicalViewport layout =
            rendering::make_logical_viewport( last_output_width_, last_output_height_, 1280.0f, 720.0f );

        const float text_scale = layout.scale;
        const float heading_scale = layout.scale;
        prepare_row_text( app, text_scale );

        presentation_->draw( app, renderer );

        const rendering::Rect settings_panel =
            GameplayVisualModule::settings_panel( last_output_width_, last_output_height_, full_width() );

        const rendering::Rect preview_panel =
            GameplayVisualModule::preview_panel( last_output_width_, last_output_height_ );

        const bool show_preview = view_ == View::Cone || view_ == View::AimArrow || view_ == View::Line ||
                                  view_ == View::Nub || view_ == View::Notes || view_ == View::Guard;

        const float window_height = ( preview_panel.h - 18.0f * heading_scale ) * 0.5f;
        const rendering::Rect idle_window{ preview_panel.x, preview_panel.y, preview_panel.w, window_height };
        const rendering::Rect locked_window{
            preview_panel.x,
            preview_panel.y + window_height + 18.0f * heading_scale,
            preview_panel.w,
            window_height
        };

        if( show_preview )
        {
            if( view_ == View::Guard )
            {
                draw_rounded_panel( renderer, preview_panel );
            }
            else
            {
                draw_rounded_panel( renderer, idle_window );
                draw_rounded_panel( renderer, locked_window );
            }
        }

        draw_stroked_text( app,
            renderer,
            localization::text( "@ui.interface.title" ),
            layout.to_x( 1230.0f ),
            layout.to_y( 61.0f ),
            48.0f * heading_scale,
            true,
            heading_text_[ 0 ],
            true,
            TextStyle::Heading );

        const float section_y = layout.to_y( 142.0f );
        const float section_width = draw_stroked_text( app,
            renderer,
            view_name( view_ ),
            layout.to_x( 132.0f ),
            section_y,
            31.0f * heading_scale,
            true,
            heading_text_[ 1 ],
            false,
            TextStyle::Section );

        renderer.draw_filled_rect(
            rendering::Rect{
                layout.to_x( 132.0f ) + section_width + 20.0f * text_scale,
                section_y - text_scale,
                std::max( 0.0f,
                    settings_panel.x + settings_panel.w - layout.to_x( 132.0f ) - section_width - 20.0f * text_scale ),
                2.0f * text_scale
            },
            rendering::Color{ 0.86f, 0.94f, 1.0f, 0.34f } );

        if( view_ == View::Menu )
        {
            for( std::int32_t index = 1; index < row_count(); ++index )
            {
                const SettingRow row = row_kind( index );

                if( row != SettingRow::SaveProfileEntry && row != SettingRow::Back )
                {
                    continue;
                }

                const rendering::Rect previous = row_rect( index - 1, last_output_width_, last_output_height_ );
                const rendering::Rect next = row_rect( index, last_output_width_, last_output_height_ );
                renderer.draw_filled_rect(
                    rendering::Rect{
                        previous.x + 12.0f * text_scale,
                        ( previous.y + previous.h + next.y ) * 0.5f - text_scale,
                        previous.w - 24.0f * text_scale,
                        2.0f * text_scale
                    },
                    rendering::Color{ 0.86f, 0.94f, 1.0f, 0.34f } );
            }
        }

        const std::int32_t visible = visible_row_count();

        if( color_target_ == ColorTarget::None )
        {
            for( std::int32_t index = first_visible_row_; index < first_visible_row_ + visible; ++index )
            {
                const bool selected = index == selected_row_;
                const rendering::Rect row_box = row_rect( index, last_output_width_, last_output_height_ );
                renderer.draw_filled_rect( row_box,
                    selected ? rendering::Color{ 0.04f, 0.18f, 0.34f, 0.40f + pulse * 0.10f }
                             : rendering::Color{ 0.0f, 0.02f, 0.06f, 0.18f } );
                draw_stroked_text( app,
                    renderer,
                    row_label( index ),
                    row_box.x + 12.0f * text_scale,
                    row_box.y + row_box.h * 0.5f,
                    label_text_size * text_scale,
                    selected,
                    label_text_[ static_cast<std::size_t>( index ) ],
                    false,
                    TextStyle::Row,
                    row_box.w * ( view_ == View::FontList ? 0.72f : 0.56f ) );

                const std::string value = row_value( app, index );
                const ColorTarget target = row_color_target( index );
                const bool adjustable = row_adjustable( index );

                if( adjustable )
                {
                    rendering::menu_theme::draw_adjustment_buttons( renderer, row_box, text_scale, selected );
                }

                if( !value.empty() )
                {
                    const auto bounds = rendering::menu_theme::adjustment_value_rect( row_box, text_scale );
                    const float value_x = adjustable ? bounds.x + bounds.w * 0.5f
                                                     : row_box.x + row_box.w -
                                                           ( target == ColorTarget::None ? 12.0f : 48.0f ) * text_scale;

                    draw_stroked_text( app,
                        renderer,
                        value,
                        value_x,
                        row_box.y + row_box.h * 0.5f,
                        value_text_size * text_scale,
                        selected,
                        value_text_[ static_cast<std::size_t>( index ) ],
                        !adjustable,
                        TextStyle::Row,
                        adjustable ? bounds.w : row_box.w * ( view_ == View::FontList ? 0.22f : 0.38f ),
                        adjustable );
                }

                if( target != ColorTarget::None )
                {
                    const auto color_for = [ & ]( ColorTarget color_target ) -> const std::array<float, 4> *
                    {
                        const managers::GameplayVisualStyle &style = app.settings().gameplay_visual;

                        switch( color_target )
                        {
                            case ColorTarget::TraceIdle:
                                return &style.trace_idle_color;
                            case ColorTarget::TraceLocked:
                                return &style.trace_locked_color;
                            case ColorTarget::TraceLost:
                                return &style.trace_lost_color;
                            case ColorTarget::Note:
                                return &style.note_color;
                            case ColorTarget::HeldNote:
                                return &style.held_note_color;
                            case ColorTarget::GuardRailColor:
                                return &style.guard_rail_color;
                            case ColorTarget::NoteBody:
                                return &style.note_body_color;
                            case ColorTarget::ConeIdleLine:
                                return &app.settings().view_cone_off_target_line_color;
                            case ColorTarget::ConeIdleFill:
                                return &app.settings().view_cone_off_target_fill_color;
                            case ColorTarget::ConeLockedLine:
                                return &app.settings().view_cone_on_target_line_color;
                            case ColorTarget::ConeLockedFill:
                                return &app.settings().view_cone_on_target_fill_color;
                            case ColorTarget::None:
                                break;
                        }

                        return nullptr;
                    };

                    if( const std::array<float, 4> *color = color_for( target ); color != nullptr )
                    {
                        const rendering::Rect swatch{
                            row_box.x + row_box.w - 36.0f * text_scale,
                            row_box.y + ( row_box.h - 19.0f * text_scale ) * 0.5f,
                            24.0f * text_scale,
                            19.0f * text_scale
                        };

                        draw_checkerboard( renderer, swatch, 6.0f * text_scale );
                        renderer.draw_filled_rect( swatch, array_color( *color ) );
                    }
                }
            }
        }

        if( color_target_ == ColorTarget::None && visible < row_count() )
        {
            const float track_y = settings_panel.y + row_top * text_scale;
            const float gap = row_gap_for( last_output_width_, last_output_height_ );
            const float track_height =
                static_cast<float>( visible ) * ( row_height_for( last_output_width_, last_output_height_ ) + gap ) -
                gap;

            const float thumb_height = track_height * static_cast<float>( visible ) / static_cast<float>( row_count() );
            const float progress =
                static_cast<float>( first_visible_row_ ) / static_cast<float>( row_count() - visible );

            const float track_x = settings_panel.x + settings_panel.w - 17.0f * text_scale;
            renderer.draw_filled_rect( rendering::Rect{ track_x, track_y, 5.0f, track_height },
                rendering::Color{ 0.12f, 0.18f, 0.32f, 0.84f } );

            renderer.draw_filled_rect(
                rendering::Rect{ track_x, track_y + progress * ( track_height - thumb_height ), 5.0f, thumb_height },
                rendering::Color{ 0.74f, 0.84f, 1.0f, 0.96f } );
        }

        if( view_ == View::FontList )
        {
            draw_interface_preview( app, renderer, preview_panel );
        }

        if( show_preview )
        {
            const stage::GuardIconTextures custom_guard_icons =
                stage::guard_icon_textures( app.texture_manager(), preview_custom_guard_handles_ );

            draw_preview( renderer,
                preview_panel,
                view_,
                app.settings(),
                app.texture_manager().get( preview_trace_texture_ ),
                app.texture_manager().get( preview_tube_texture_ ),
                app.texture_manager().get( preview_guard_texture_ ),
                app.texture_manager().get( preview_arrow_texture_ ),
                app.texture_manager().get( preview_wing_texture_ ),
                app.texture_manager().get( preview_normal_nub_texture_ ),
                app.texture_manager().get( preview_pressed_nub_texture_ ),
                &custom_guard_icons,
                static_cast<float>( elapsed_seconds_ ),
                preview_note_regions_ );

            const char *top_label = view_ == View::Guard      ? localization::text( "@ui.visual.guards_rails" )
                                    : view_ == View::Line     ? localization::text( "@ui.unpressed_upper" )
                                    : view_ == View::Notes    ? localization::text( "@ui.single_notes_upper" )
                                    : view_ == View::Nub      ? localization::text( "@ui.normal_nub_upper" )
                                    : view_ == View::AimArrow ? localization::text( "@ui.interface.aim_arrow_upper" )
                                                              : localization::text( "@ui.vision_cone_upper" );

            const char *bottom_label = view_ == View::Line    ? localization::text( "@ui.visual.lock_range" )
                                       : view_ == View::Notes ? localization::text( "@ui.visual.long_notes" )
                                       : view_ == View::Nub   ? localization::text( "@ui.pressed_nub_upper" )
                                                              : localization::text( "@ui.on_aim_upper" );

            const rendering::Rect &top_window = view_ == View::Guard ? preview_panel : idle_window;
            draw_stroked_text( app,
                renderer,
                top_label,
                top_window.x + top_window.w - preview_heading_margin * heading_scale,
                top_window.y + ( preview_heading_margin + preview_heading_height * 0.5f ) * heading_scale,
                preview_heading_height * heading_scale,
                true,
                heading_text_[ 2 ],
                true,
                TextStyle::Section );

            if( view_ != View::Guard )
            {
                draw_stroked_text( app,
                    renderer,
                    bottom_label,
                    locked_window.x + locked_window.w - preview_heading_margin * heading_scale,
                    locked_window.y + ( preview_heading_margin + preview_heading_height * 0.5f ) * heading_scale,
                    preview_heading_height * heading_scale,
                    true,
                    heading_text_[ 3 ],
                    true,
                    TextStyle::Section );
            }
        }

        if( !profile_status_.empty() )
        {
            draw_stroked_text( app,
                renderer,
                profile_status_,
                layout.to_x( 1148.0f ),
                layout.to_y( 647.0f ),
                20.0f * heading_scale,
                false,
                heading_text_[ 4 ],
                true );
        }

        if( color_target_ == ColorTarget::None )
        {
            return;
        }

        const rendering::Rect picker = picker_panel( last_output_width_, last_output_height_ );
        const float picker_scale = picker.w / picker_width;
        const rendering::Rect sv = picker_sv_rect( picker );
        const rendering::Rect hue = picker_hue_rect( picker );

        renderer.draw_filled_rect(
            rendering::Rect{
                picker.x - 3.0f * picker_scale,
                picker.y - 3.0f * picker_scale,
                picker.w + 6.0f * picker_scale,
                picker.h + 6.0f * picker_scale
            },
            rendering::Color{ 0.30f, 0.48f, 0.90f, 0.72f } );

        renderer.draw_filled_rect( picker, rendering::Color{ 0.018f, 0.024f, 0.052f, 1.0f } );
        draw_stroked_text( app,
            renderer,
            localization::text( "@ui.colour_picker_upper" ),
            picker.x + 28.0f * picker_scale,
            picker.y + 36.0f * picker_scale,
            29.0f * picker_scale,
            true,
            picker_text_[ 0 ],
            false,
            TextStyle::Heading,
            picker.w - 48.0f * picker_scale );

        std::vector<rendering::VertexColor2D> vertices;
        constexpr std::int32_t sv_steps = 16;
        vertices.reserve( sv_steps * sv_steps * 6 );

        for( std::int32_t y = 0; y < sv_steps; ++y )
        {
            for( std::int32_t x = 0; x < sv_steps; ++x )
            {
                const float s0 = static_cast<float>( x ) / static_cast<float>( sv_steps );
                const float s1 = static_cast<float>( x + 1 ) / static_cast<float>( sv_steps );
                const float v0 = 1.0f - static_cast<float>( y ) / static_cast<float>( sv_steps );
                const float v1 = 1.0f - static_cast<float>( y + 1 ) / static_cast<float>( sv_steps );
                const rendering::Rect cell{
                    sv.x + sv.w * s0,
                    sv.y + sv.h * ( 1.0f - v0 ),
                    sv.w / static_cast<float>( sv_steps ) + 0.5f,
                    sv.h / static_cast<float>( sv_steps ) + 0.5f
                };

                push_quad( vertices,
                    cell,
                    hsv_to_color( picker_hue_, s0, v0 ),
                    hsv_to_color( picker_hue_, s1, v0 ),
                    hsv_to_color( picker_hue_, s1, v1 ),
                    hsv_to_color( picker_hue_, s0, v1 ) );
            }
        }

        renderer.draw_colored_triangles( vertices.data(), vertices.size() );

        vertices.clear();
        constexpr std::int32_t hue_steps = 24;
        vertices.reserve( hue_steps * 6 );

        for( std::int32_t index = 0; index < hue_steps; ++index )
        {
            const float h0 = static_cast<float>( index ) / static_cast<float>( hue_steps );
            const float h1 = static_cast<float>( index + 1 ) / static_cast<float>( hue_steps );
            push_quad( vertices,
                rendering::Rect{ hue.x, hue.y + hue.h * h0, hue.w, hue.h / static_cast<float>( hue_steps ) + 0.5f },
                hsv_to_color( h0, 1.0f, 1.0f ),
                hsv_to_color( h0, 1.0f, 1.0f ),
                hsv_to_color( h1, 1.0f, 1.0f ),
                hsv_to_color( h1, 1.0f, 1.0f ) );
        }

        renderer.draw_colored_triangles( vertices.data(), vertices.size() );

        const float sv_marker_x = sv.x + picker_saturation_ * sv.w;
        const float sv_marker_y = sv.y + ( 1.0f - picker_value_ ) * sv.h;
        renderer.draw_line( sv_marker_x - 7.0f,
            sv_marker_y,
            sv_marker_x + 7.0f,
            sv_marker_y,
            2.0f,
            rendering::Color{ 0.0f, 0.0f, 0.0f, 1.0f } );

        renderer.draw_line( sv_marker_x,
            sv_marker_y - 7.0f,
            sv_marker_x,
            sv_marker_y + 7.0f,
            2.0f,
            rendering::Color{ 0.0f, 0.0f, 0.0f, 1.0f } );

        renderer.draw_line( sv_marker_x - 5.0f,
            sv_marker_y,
            sv_marker_x + 5.0f,
            sv_marker_y,
            1.0f,
            rendering::Color{ 1.0f, 1.0f, 1.0f, 1.0f } );

        renderer.draw_line( sv_marker_x,
            sv_marker_y - 5.0f,
            sv_marker_x,
            sv_marker_y + 5.0f,
            1.0f,
            rendering::Color{ 1.0f, 1.0f, 1.0f, 1.0f } );

        const float hue_marker_y = hue.y + picker_hue_ * hue.h;
        renderer.draw_filled_rect( rendering::Rect{ hue.x - 3.0f, hue_marker_y - 2.0f, hue.w + 6.0f, 4.0f },
            rendering::Color{ 1.0f, 1.0f, 1.0f, 1.0f } );

        const auto code = picker_code_rect( picker );

        const auto current_swatch = picker_rect( picker, 28.0f, 400.0f, 28.0f, 28.0f );

        const auto code_border =
            picker_hex_valid_ ? rendering::menu_theme::focus_accent : rendering::Color{ 1.0f, 0.30f, 0.30f, 1.0f };

        renderer.draw_filled_rect( code, code_border );
        renderer.draw_filled_rect(
            rendering::Rect{
                code.x + picker_scale,
                code.y + picker_scale,
                code.w - 2.0f * picker_scale,
                code.h - 2.0f * picker_scale
            },
            picker_code_focused_ && picker_code_selected_ ? rendering::menu_theme::active_card
                                                          : rendering::Color{ 0.03f, 0.07f, 0.13f, 1.0f } );
        draw_checkerboard( renderer, current_swatch, 7.0f * picker_scale );
        renderer.draw_filled_rect( current_swatch,
            hsv_to_color( picker_hue_, picker_saturation_, picker_value_, picker_alpha_ ) );

        draw_stroked_text( app,
            renderer,
            picker_hex_text_,
            code.x + 44.0f * picker_scale,
            code.y + code.h * 0.5f,
            17.0f * picker_scale,
            false,
            picker_text_[ 1 ],
            false,
            TextStyle::Row,
            code.w - 52.0f * picker_scale );

        draw_stroked_text( app,
            renderer,
            localization::text( "@ui.presets_upper" ),
            picker.x + 424.0f * picker_scale,
            picker.y + 64.0f * picker_scale,
            16.0f * picker_scale,
            false,
            picker_text_[ 2 ],
            false,
            TextStyle::Section,
            104.0f * picker_scale,
            true );

        for( std::int32_t index = 0; index < static_cast<std::int32_t>( picker_presets.size() ); ++index )
        {
            const rendering::Rect swatch = picker_preset_rect( picker, index );
            renderer.draw_filled_rect(
                rendering::Rect{
                    swatch.x - 2.0f * picker_scale,
                    swatch.y - 2.0f * picker_scale,
                    swatch.w + 4.0f * picker_scale,
                    swatch.h + 4.0f * picker_scale
                },
                rendering::Color{ 0.24f, 0.31f, 0.55f, 1.0f } );

            renderer.draw_filled_rect( swatch, array_color( picker_presets[ index ] ) );
        }

        const auto draw_button = [ & ]( const rendering::Rect &rect, const char *key, TextEntry &text )
        {
            renderer.draw_filled_rect( rect, rendering::menu_theme::selected_card );
            draw_stroked_text( app,
                renderer,
                localization::text( key ),
                rect.x + rect.w * 0.5f,
                rect.y + rect.h * 0.5f,
                18.0f * picker_scale,
                false,
                text,
                false,
                TextStyle::Row,
                rect.w - 12.0f * picker_scale,
                true );
        };

        draw_button( picker_copy_rect( picker ), "@ui.copy_upper", picker_text_[ 3 ] );
        draw_button( picker_paste_rect( picker ), "@ui.paste_upper", picker_text_[ 4 ] );
        draw_button( picker_done_rect( picker ), "@ui.done_upper", picker_text_[ 5 ] );
        draw_button( picker_cancel_rect( picker ), "@ui.cancel_upper", picker_text_[ 6 ] );
    }
}
