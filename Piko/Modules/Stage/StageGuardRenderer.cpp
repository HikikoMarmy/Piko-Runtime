#include "Modules/Stage/StageGuardRenderer.hpp"
#include "Managers/ControllerOrientation.hpp"
#include "Utils/StrUtil.hpp"
#include <cstdint>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <system_error>

namespace piko::modules::stage
{
    std::vector<std::string> guard_icon_set_names( const std::filesystem::path &resource_root )
    {
        std::vector<std::string> names;

        if( resource_root.empty() )
        {
            return names;
        }

        const std::filesystem::path guard_root = resource_root / "ui" / "battle" / "guard";
        std::error_code error;
        std::filesystem::directory_iterator entries( guard_root, error );

        if( error )
        {
            return names;
        }

        constexpr std::array<const char *, 4>
            filenames{
            "btn_square.png",
            "btn_cross.png",
            "btn_circle.png",
            "btn_triangle.png"
        };

        for( const std::filesystem::directory_entry &entry : entries )
        {
            if( entry.is_symlink( error ) || error )
            {
                error.clear();
                continue;
            }

            if( !entry.is_directory( error ) || error )
            {
                error.clear();
                continue;
            }

            bool complete = true;

            for( const char *filename : filenames )
            {
                if( !std::filesystem::is_regular_file( entry.path() / filename, error ) || error )
                {
                    error.clear();
                    complete = false;
                    break;
                }
            }

            if( complete )
            {
                names.push_back( entry.path().filename().string() );
            }
        }

        std::sort( names.begin(), names.end() );
        return names;
    }

    void unload_guard_icon_set( managers::TextureManager &textures, GuardIconHandles &handles )
    {
        for( managers::TextureHandle &handle : handles )
        {
            textures.unload( handle );
            handle = {};
        }
    }

    bool load_guard_icon_set( managers::TextureManager &textures,
        const std::filesystem::path &resource_root,
        const std::string &set_name,
        const std::string &texture_prefix,
        GuardIconHandles &handles )
    {
        unload_guard_icon_set( textures, handles );

        if( set_name.empty() )
        {
            return false;
        }

        const std::vector<std::string> names = guard_icon_set_names( resource_root );

        const auto selected = std::find_if( names.begin(),
            names.end(),
            [ & ]( const auto &name )
            {
                return utils::lowercase_ascii( name ) == utils::lowercase_ascii( set_name );
            } );

        if( selected == names.end() )
        {
            return false;
        }

        constexpr std::array<const char *, 4>
            filenames{
            "btn_square.png",
            "btn_cross.png",
            "btn_circle.png",
            "btn_triangle.png"
        };

        try
        {
            for( std::size_t index = 0; index < filenames.size(); ++index )
            {
                handles[ index ] = textures.load_png( texture_prefix + std::to_string( index ),
                    resource_root / "ui" / "battle" / "guard" / *selected / filenames[ index ] );
            }
        }
        catch( const std::exception & )
        {
            unload_guard_icon_set( textures, handles );
            return false;
        }

        return true;
    }

    GuardIconTextures guard_icon_textures( const managers::TextureManager &textures, const GuardIconHandles &handles )
    {
        GuardIconTextures result{};

        for( std::size_t index = 0; index < result.size(); ++index )
        {
            result[ index ] = textures.get( handles[ index ] );
        }

        return result;
    }

    rendering::Vec2 guard_direction( std::uint32_t button )
    {
        switch( button )
        {
            case 0:
                return rendering::Vec2{ -1.0f, 0.0f };
            case 1:
                return rendering::Vec2{ 0.0f, 1.0f };
            case 2:
                return rendering::Vec2{ 1.0f, 0.0f };
            case 3:
                return rendering::Vec2{ 0.0f, -1.0f };
            default:
                return rendering::Vec2{};
        }
    }

    float guard_pixels_per_sample( const runtime::stage::StageGameplayView &view,
        float gameplay_speed_factor,
        std::uint32_t button )
    {
        if( !std::isfinite( gameplay_speed_factor ) || gameplay_speed_factor <= 0.0f )
        {
            gameplay_speed_factor = 240.0f;
        }

        const rendering::Vec2 direction = guard_direction( button );
        const float x_scale = std::abs( view.zoom * view.scale_x * view.presentation_scale_x );
        const float y_scale = std::abs( view.zoom * view.scale_y * view.presentation_scale_y );
        const float directional_scale =
            std::sqrt( direction.x * direction.x * x_scale * x_scale + direction.y * direction.y * y_scale * y_scale );

        return std::max( directional_scale * view.note_speed / gameplay_speed_factor, 0.000001f );
    }

    float guard_visual_start_distance_for_speed( float pixels_per_sample )
    {
        return std::max( 1.0f, pixels_per_sample * static_cast<float>( trace_lookahead_samples ) );
    }

    const char *guard_button_text( std::uint32_t button )
    {
        switch( button )
        {
            case 0:
                return "S";
            case 1:
                return "X";
            case 2:
                return "O";
            case 3:
                return "T";
            default:
                return "?";
        }
    }

    bool chart_has_active_phrase_body( const filetypes::chc::Chart &chart, std::int64_t current_chart_sample )
    {
        for( const filetypes::chc::PhraseFragment &phrase : chart.phrases )
        {
            const std::int64_t start_sample = static_cast<std::int64_t>( phrase.start_sample );
            const std::int64_t end_sample = start_sample + static_cast<std::int64_t>( phrase.duration_samples );

            if( end_sample <= start_sample )
            {
                continue;
            }

            if( current_chart_sample >= start_sample && current_chart_sample < end_sample )
            {
                return true;
            }
        }

        return false;
    }

    bool guard_rails_should_draw( const filetypes::chc::Chart &chart,
        const std::vector<runtime::stage::TraceSample> &trace_samples,
        std::int64_t current_chart_sample )
    {
        if( chart.guards.empty() )
        {
            return false;
        }

        if( chart_has_active_phrase_body( chart, current_chart_sample ) )
        {
            return false;
        }

        const float pixels_per_sample = gameplay_zoom / 240.0f;
        const float guard_start_distance = guard_visual_start_distance_for_speed( pixels_per_sample );

        bool recent_guard_section = false;
        bool next_guard_section_near = false;
        constexpr std::int64_t guard_rail_bridge_tail_samples = sample_rate * 9 / 2;

        for( const filetypes::chc::GuardFragment &guard : chart.guards )
        {
            const std::int64_t time_to_hit = static_cast<std::int64_t>( guard.start_sample ) - current_chart_sample;

            if( time_to_hit >= -guard_visual_post_hit_samples )
            {
                const float distance_from_center =
                    time_to_hit <= 0 ? 0.0f : static_cast<float>( time_to_hit ) * pixels_per_sample;

                if( distance_from_center <= guard_start_distance )
                {
                    return true;
                }
            }

            if( time_to_hit < 0 && time_to_hit >= -guard_rail_bridge_tail_samples )
            {
                recent_guard_section = true;
            }
            else if( time_to_hit > 0 && time_to_hit <= trace_lookahead_samples )
            {
                next_guard_section_near = true;
            }
        }

        return recent_guard_section && next_guard_section_near;
    }

    void draw_guard_section_rails( rendering::Renderer &renderer,
        const runtime::stage::StageGameplayView &view,
        const rendering::Rect &area,
        float alpha_scale,
        float rail_scale,
        rendering::Color rail_color )
    {
        alpha_scale = std::clamp( alpha_scale, 0.0f, 2.0f );

        if( alpha_scale <= 0.001f )
        {
            return;
        }

        const rendering::Vec2 center{ view.center_x, view.center_y };
        const std::array<rendering::Vec2, 4> directions = {
            rendering::Vec2{ -1.0f, 0.0f },
            rendering::Vec2{ 1.0f, 0.0f },
            rendering::Vec2{ 0.0f, -1.0f },
            rendering::Vec2{ 0.0f, 1.0f }
        };

        const auto distance_to_edge = [ & ]( const rendering::Vec2 &direction ) -> float
        {
            if( direction.x < 0.0f )
            {
                return std::max( 0.0f, center.x - area.x );
            }

            if( direction.x > 0.0f )
            {
                return std::max( 0.0f, area.x + area.w - center.x );
            }

            if( direction.y < 0.0f )
            {
                return std::max( 0.0f, center.y - area.y );
            }

            return std::max( 0.0f, area.y + area.h - center.y );
        };

        const float view_scale = std::max( 0.01f, view.zoom );
        const float start_offset = 34.0f * view_scale;

        for( const rendering::Vec2 &direction : directions )
        {
            const float length = std::max( start_offset + 1.0f, distance_to_edge( direction ) );

            renderer.draw_line( center.x + direction.x * start_offset,
                center.y + direction.y * start_offset,
                center.x + direction.x * length,
                center.y + direction.y * length,
                7.0f * view_scale * rail_scale,
                rendering::Color{
                    rail_color.r,
                    rail_color.g,
                    rail_color.b,
                    std::clamp( rail_color.a * alpha_scale, 0.0f, 1.0f )
                } );
        }
    }

    rendering::Color guard_button_color( std::uint32_t button, float alpha )
    {
        switch( button )
        {
            case 0:
                return rendering::Color{ 1.00f, 0.42f, 0.86f, alpha };
            case 1:
                return rendering::Color{ 0.35f, 0.67f, 1.00f, alpha };
            case 2:
                return rendering::Color{ 1.00f, 0.42f, 0.32f, alpha };
            case 3:
                return rendering::Color{ 0.35f, 0.94f, 0.52f, alpha };
            default:
                return rendering::Color{ 0.65f, 0.65f, 0.75f, alpha };
        }
    }

    const rendering::Rect &itfc_guard_rect_for_button( std::uint32_t button )
    {
        switch( button )
        {
            case 0:
                return runtime::stage::gameplay_interface_atlas::guard_square;
            case 1:
                return runtime::stage::gameplay_interface_atlas::guard_cross;
            case 2:
                return runtime::stage::gameplay_interface_atlas::guard_circle;
            case 3:
                return runtime::stage::gameplay_interface_atlas::guard_triangle;
            default:
                return runtime::stage::gameplay_interface_atlas::guard_square;
        }
    }

    void draw_guard_icon( rendering::Renderer &renderer,
        const rendering::Texture *itfc_texture,
        const rendering::Vec2 &position,
        std::uint32_t button,
        float alpha,
        float scale,
        float brightness,
        const GuardIconTextures *custom_icons )
    {
        alpha = std::clamp( alpha, 0.0f, 1.0f );

        brightness = std::clamp( brightness, 0.0f, 1.0f );

        if( alpha <= 0.0f )
        {
            return;
        }

        if( custom_icons != nullptr && button < custom_icons->size() )
        {
            const rendering::Texture *icon = ( *custom_icons )[ button ];

            if( icon != nullptr && icon->valid() && icon->width > 0 && icon->height > 0 )
            {
                const rendering::Rect &source = itfc_guard_rect_for_button( button );
                const float extent = std::max( source.w, source.h ) * guard_icon_scale * scale;
                const float aspect = static_cast<float>( icon->width ) / static_cast<float>( icon->height );
                const float width = aspect >= 1.0f ? extent : extent * aspect;
                const float height = aspect <= 1.0f ? extent : extent / aspect;
                renderer.draw_texture( *icon,
                    position.x - width * 0.5f,
                    position.y - height * 0.5f,
                    width,
                    height,
                    rendering::Color{ brightness, brightness, brightness, alpha } );

                return;
            }
        }

        if( itfc_texture != nullptr && itfc_texture->valid() )
        {
            const rendering::Rect &source = itfc_guard_rect_for_button( button );
            const float final_scale = guard_icon_scale * scale;
            const float w = source.w * final_scale;
            const float h = source.h * final_scale;

            renderer.draw_texture_region( *itfc_texture,
                source.x,
                source.y,
                source.w,
                source.h,
                position.x - w * 0.5f,
                position.y - h * 0.5f,
                w,
                h,
                rendering::Color{ brightness, brightness, brightness, alpha } );

            return;
        }

        draw_filled_disc( renderer,
            position,
            guard_fallback_radius * scale,
            guard_button_color( button, std::min( alpha, 0.72f ) ) );

        draw_disc_outline( renderer,
            position,
            guard_fallback_radius * scale,
            2.5f * scale,
            rendering::Color{ 1.0f, 1.0f, 1.0f, alpha } );

        renderer.draw_text( position.x - 5.0f * scale,
            position.y - 5.0f * scale,
            guard_button_text( button ),
            rendering::Color{ 1.0f, 1.0f, 1.0f, alpha },
            scale );
    }

    void draw_guard_fragments( rendering::Renderer &renderer,
        const filetypes::chc::Chart &chart,
        std::int64_t current_chart_sample,
        const runtime::stage::StageGameplayView &view,
        float gameplay_speed_factor,
        const rendering::Texture *itfc_texture,
        const std::vector<GuardHitVisualOverride> *hit_overrides,
        bool mirror,
        float note_scale,
        float hit_pop_scale,
        float hit_fade_scale,
        const GuardIconTextures *custom_icons,
        managers::ControllerOrientation controller_orientation )
    {
        const auto find_hit_visual = [ hit_overrides ](
                                         const filetypes::chc::GuardFragment &guard ) -> const GuardHitVisualOverride *
        {
            if( hit_overrides == nullptr )
            {
                return nullptr;
            }

            for( const GuardHitVisualOverride &override_state : *hit_overrides )
            {
                if( override_state.button == guard.button &&
                    std::llabs( override_state.start_sample - static_cast<std::int64_t>( guard.start_sample ) ) <=
                        sample_rate / 1000 )
                {
                    return &override_state;
                }
            }

            return nullptr;
        };

        for( const filetypes::chc::GuardFragment &guard : chart.guards )
        {
            const std::uint32_t display_button = mirror ? ( guard.button + 2u ) % 4u : guard.button;
            const GuardHitVisualOverride *hit_visual = find_hit_visual( guard );

            if( hit_visual != nullptr )
            {
                continue;
            }

            const float pixels_per_sample = guard_pixels_per_sample( view, gameplay_speed_factor, display_button );
            const float guard_start_distance = guard_visual_start_distance_for_speed( pixels_per_sample );

            const std::int64_t time_to_hit = static_cast<std::int64_t>( guard.start_sample ) - current_chart_sample;

            if( time_to_hit < -static_cast<std::int64_t>( guard_visual_post_hit_samples * hit_fade_scale ) )
            {
                continue;
            }

            const float distance_from_center =
                time_to_hit <= 0 ? 0.0f : static_cast<float>( time_to_hit ) * pixels_per_sample;

            if( distance_from_center > guard_start_distance )
            {
                continue;
            }

            float alpha = 1.0f;
            float visual_distance_from_center = distance_from_center;
            float visual_scale = 1.25f;

            if( time_to_hit < 0 )
            {
                const std::int64_t elapsed_after_hit = -time_to_hit;
                const float fade =
                    1.0f - ( static_cast<float>( elapsed_after_hit ) /
                               ( static_cast<float>( guard_visual_post_hit_samples ) * hit_fade_scale ) );

                alpha = std::clamp( fade, 0.0f, 1.0f );
            }
            else
            {
                const float fade_in = 1.0f - ( distance_from_center / guard_start_distance );
                alpha = std::lerp( 80.0f / 255.0f, 1.0f, std::clamp( fade_in, 0.0f, 1.0f ) );
            }

            if( alpha <= 0.001f )
            {
                continue;
            }

            const rendering::Vec2 direction = guard_direction( display_button );
            const rendering::Vec2 position{
                view.center_x + direction.x * visual_distance_from_center,
                view.center_y + direction.y * visual_distance_from_center
            };

            draw_guard_icon( renderer,
                itfc_texture,
                position,
                managers::controller_guard_symbol( display_button, controller_orientation ),
                alpha,
                visual_scale * view.zoom * note_scale,
                1.0f,
                custom_icons );
        }

        if( hit_overrides == nullptr )
        {
            return;
        }

        for( const GuardHitVisualOverride &hit_visual : *hit_overrides )
        {
            const std::uint32_t display_button = mirror ? ( hit_visual.button + 2u ) % 4u : hit_visual.button;
            const std::int64_t elapsed_after_hit = current_chart_sample - hit_visual.hit_sample;

            if( elapsed_after_hit < 0 )
            {
                continue;
            }

            const std::int32_t retail_tick = static_cast<std::int32_t>(
                elapsed_after_hit / ( static_cast<float>( retail_update_samples ) * hit_fade_scale ) );

            if( retail_tick < 0 || retail_tick >= retail_hit_pop_tick_count )
            {
                continue;
            }

            const float remaining = 1.0f - retail_hit_pop_timer_step * static_cast<float>( retail_tick );
            const float progress = 1.0f - remaining;
            const float pixels_per_sample = guard_pixels_per_sample( view, gameplay_speed_factor, display_button );

            // sub_10DDA0: scale = base * (1 + 3 * exp(progress)).
            // sub_10DCD0: normal travel uses +1.0; a hit reverses it with -2.5.
            const float visual_distance_from_center =
                static_cast<float>( current_chart_sample - hit_visual.start_sample ) * pixels_per_sample * 2.5f;

            const float visual_scale = 1.25f * ( 1.0f + 3.0f * std::exp( progress ) * hit_pop_scale );
            const float alpha = remaining;

            if( alpha <= 0.001f )
            {
                continue;
            }

            const rendering::Vec2 direction = guard_direction( display_button );
            const rendering::Vec2 position{
                view.center_x + direction.x * visual_distance_from_center,
                view.center_y + direction.y * visual_distance_from_center
            };

            draw_guard_icon( renderer,
                itfc_texture,
                position,
                managers::controller_guard_symbol( display_button, controller_orientation ),
                alpha,
                visual_scale * view.zoom * note_scale,
                0.70f,
                custom_icons );
        }
    }
}
