#include "Modules/Stage/StagePhraseRenderer.hpp"
#include "Rendering/RenderUtil.hpp"
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include "Utils/MathUtil.hpp"

namespace piko::modules::stage
{
    std::int32_t wrap_phrase_note_color_index( std::int32_t index )
    {
        std::int32_t wrapped = index % phrase_note_color_preset_count;

        if( wrapped < 0 )
        {
            wrapped += phrase_note_color_preset_count;
        }

        return wrapped;
    }

    rendering::Color phrase_note_color_preset( std::int32_t index, float alpha )
    {
        switch( wrap_phrase_note_color_index( index ) )
        {
            case 0:
                return rendering::Color{ 1.00f, 150.0f / 255.0f, 50.0f / 255.0f, alpha };
            case 1:
                return rendering::Color{ 0.14f, 0.58f, 1.00f, alpha };
            case 2:
                return rendering::Color{ 0.20f, 0.94f, 0.58f, alpha };
            case 3:
                return rendering::Color{ 1.00f, 0.30f, 0.56f, alpha };
            case 4:
                return rendering::Color{ 0.66f, 0.36f, 1.00f, alpha };
            case 5:
                return rendering::Color{ 0.14f, 0.92f, 1.00f, alpha };
            case 6:
                return rendering::Color{ 1.00f, 0.92f, 0.58f, alpha };
            case 7:
                return rendering::Color{ 1.00f, 0.22f, 0.16f, alpha };
            default:
                return rendering::Color{ 1.00f, 0.56f, 0.16f, alpha };
        }
    }

    rendering::Color phrase_note_pulse_tint_for( rendering::Color base, float alpha )
    {
        return rendering::Color{
            std::clamp( base.r * 1.22f + 0.10f, 0.0f, 1.0f ),
            std::clamp( base.g * 1.22f + 0.10f, 0.0f, 1.0f ),
            std::clamp( base.b * 1.22f + 0.10f, 0.0f, 1.0f ),
            alpha * base.a
        };
    }

    rendering::Color sample_phrase_tube_fill_color( float path_distance, float total_distance, float time_seconds )
    {
        (void)total_distance;
        constexpr float tau = 6.2831855f;

        const float repeat = std::max( 8.0f, phrase_tube_gradient_repeat_pixels );
        const float phase = time_seconds * phrase_tube_gradient_scroll_pixels_per_second;
        const float wave = std::sin( ( ( path_distance + phase ) / repeat ) * tau );
        const float t = 0.5f + ( ( 0.5f + 0.5f * wave ) - 0.5f ) * phrase_tube_gradient_strength;

        const rendering::Color a{ 0.67f, 0.58f, 0.31f, 0.54f };
        const rendering::Color b{ 0.86f, 0.76f, 0.43f, 0.58f };
        return rendering::lerp_color( a, b, t );
    }

    void draw_phrase_tube_fill_ribbon( rendering::Renderer &renderer,
        const std::vector<PhraseTubeFrame> &frames,
        float half_width,
        float time_seconds,
        float alpha )
    {
        if( frames.size() < 2 || half_width <= 0.0f || alpha <= 0.0f )
        {
            return;
        }

        std::vector<rendering::VertexColor2D> vertices;
        vertices.reserve( ( frames.size() - 1u ) * 12u );

        const float total_distance = frames.back().distance;

        for( std::size_t i = 0; i + 1 < frames.size(); ++i )
        {
            rendering::Vec2 left0{};
            rendering::Vec2 right0{};
            rendering::Vec2 left1{};
            rendering::Vec2 right1{};

            phrase_tube_ribbon_edges_at( frames, i, half_width, phrase_tube_miter_limit, left0, right0 );
            phrase_tube_ribbon_edges_at( frames, i + 1, half_width, phrase_tube_miter_limit, left1, right1 );

            const rendering::Vec2 mid0 = frames[ i ].point;
            const rendering::Vec2 mid1 = frames[ i + 1 ].point;

            const rendering::Color base0 = rendering::scale_color_alpha(
                sample_phrase_tube_fill_color( frames[ i ].distance, total_distance, time_seconds ), alpha );

            const rendering::Color base1 = rendering::scale_color_alpha(
                sample_phrase_tube_fill_color( frames[ i + 1 ].distance, total_distance, time_seconds ), alpha );

            const rendering::Color edge0 =
                rendering::scale_color_rgb( rendering::scale_color_alpha( base0, 0.92f ), 0.92f );

            const rendering::Color edge1 =
                rendering::scale_color_rgb( rendering::scale_color_alpha( base1, 0.92f ), 0.92f );

            const rendering::Color center0 = rendering::scale_color_rgb( base0, 1.02f );
            const rendering::Color center1 = rendering::scale_color_rgb( base1, 1.02f );

            push_colored_triangle( vertices, left0, edge0, mid0, center0, left1, edge1 );
            push_colored_triangle( vertices, mid0, center0, mid1, center1, left1, edge1 );

            push_colored_triangle( vertices, mid0, center0, right0, edge0, mid1, center1 );
            push_colored_triangle( vertices, right0, edge0, right1, edge1, mid1, center1 );
        }

        renderer.draw_colored_triangles( vertices.data(), vertices.size() );
    }

    void draw_phrase_tube_disc_gradient( rendering::Renderer &renderer,
        const rendering::Vec2 &center,
        float radius,
        const rendering::Color &center_color,
        const rendering::Color &edge_color,
        std::int32_t segments )
    {
        if( radius <= 0.0f || segments < 8 )
        {
            return;
        }

        std::vector<rendering::VertexColor2D> vertices;
        vertices.reserve( static_cast<std::size_t>( segments ) * 3u );

        constexpr float tau = 6.28318530717958647692f;

        for( std::int32_t i = 0; i < segments; ++i )
        {
            const float a0 = ( static_cast<float>( i ) / static_cast<float>( segments ) ) * tau;
            const float a1 = ( static_cast<float>( i + 1 ) / static_cast<float>( segments ) ) * tau;

            const rendering::Vec2 p0{ center.x + std::cos( a0 ) * radius, center.y + std::sin( a0 ) * radius };
            const rendering::Vec2 p1{ center.x + std::cos( a1 ) * radius, center.y + std::sin( a1 ) * radius };

            push_colored_triangle( vertices, center, center_color, p0, edge_color, p1, edge_color );
        }

        renderer.draw_colored_triangles( vertices.data(), vertices.size() );
    }

    void draw_phrase_tube_solid_disc( rendering::Renderer &renderer,
        const rendering::Vec2 &center,
        float radius,
        const rendering::Color &color,
        std::int32_t segments )
    {
        draw_phrase_tube_disc_gradient( renderer, center, radius, color, color, segments );
    }

    void draw_phrase_tube_ellipse_gradient( rendering::Renderer &renderer,
        const rendering::Vec2 &center,
        float radius_x,
        float radius_y,
        const rendering::Color &center_color,
        const rendering::Color &edge_color,
        std::int32_t segments )
    {
        if( radius_x <= 0.0f || radius_y <= 0.0f || segments < 8 )
        {
            return;
        }

        std::vector<rendering::VertexColor2D> vertices;
        vertices.reserve( static_cast<std::size_t>( segments ) * 3u );

        constexpr float tau = 6.28318530717958647692f;

        for( std::int32_t i = 0; i < segments; ++i )
        {
            const float a0 = ( static_cast<float>( i ) / static_cast<float>( segments ) ) * tau;
            const float a1 = ( static_cast<float>( i + 1 ) / static_cast<float>( segments ) ) * tau;

            const rendering::Vec2 p0{ center.x + std::cos( a0 ) * radius_x, center.y + std::sin( a0 ) * radius_y };
            const rendering::Vec2 p1{ center.x + std::cos( a1 ) * radius_x, center.y + std::sin( a1 ) * radius_y };

            push_colored_triangle( vertices, center, center_color, p0, edge_color, p1, edge_color );
        }

        renderer.draw_colored_triangles( vertices.data(), vertices.size() );
    }

    rendering::Vec2 apply_phrase_presentation_stretch( const rendering::Vec2 &point,
        const rendering::Vec2 &center,
        float scale_x,
        float scale_y )
    {
        return rendering::Vec2{
            center.x + ( point.x - center.x ) * scale_x,
            center.y + ( point.y - center.y ) * scale_y
        };
    }

    void draw_phrase_tube_semidisc( rendering::Renderer &renderer,
        const rendering::Vec2 &center,
        const rendering::Vec2 &tangent,
        float radius,
        const rendering::Color &center_color,
        const rendering::Color &edge_color,
        std::int32_t segments )
    {
        if( radius <= 0.0f || segments < 6 )
        {
            return;
        }

        const rendering::Vec2 safe_tangent = tube_normalize_or( tangent, rendering::Vec2{ 1.0f, 0.0f } );

        constexpr float tau = 6.28318530717958647692f;
        const float base_angle = std::atan2( safe_tangent.y, safe_tangent.x );
        const float start_angle = base_angle - tau * 0.25f;
        const float end_angle = base_angle + tau * 0.25f;

        std::vector<rendering::VertexColor2D> vertices;
        vertices.reserve( static_cast<std::size_t>( segments ) * 3u );

        for( std::int32_t i = 0; i < segments; ++i )
        {
            const float t0 = static_cast<float>( i ) / static_cast<float>( segments );
            const float t1 = static_cast<float>( i + 1 ) / static_cast<float>( segments );
            const float a0 = std::lerp( start_angle, end_angle, t0 );
            const float a1 = std::lerp( start_angle, end_angle, t1 );

            const rendering::Vec2 p0{ center.x + std::cos( a0 ) * radius, center.y + std::sin( a0 ) * radius };
            const rendering::Vec2 p1{ center.x + std::cos( a1 ) * radius, center.y + std::sin( a1 ) * radius };

            push_colored_triangle( vertices, center, center_color, p0, edge_color, p1, edge_color );
        }

        renderer.draw_colored_triangles( vertices.data(), vertices.size() );
    }

    void draw_phrase_note_head( rendering::Renderer &renderer,
        const rendering::Vec2 &center,
        float time_seconds,
        float dimension_scale,
        float alpha )
    {
        dimension_scale = std::max( 0.25f, dimension_scale );

        alpha = std::clamp( alpha, 0.0f, 1.0f );

        if( alpha <= 0.0f )
        {
            return;
        }

        const float outer_radius =
            std::max( 1.0f, phrase_tube_outer_radius * phrase_tube_note_head_scale * dimension_scale );

        const float inner_radius =
            std::max( 1.0f, outer_radius - std::max( 0.0f, phrase_tube_note_head_border_thickness ) );

        const rendering::Color fill = sample_phrase_tube_fill_color( 0.0f, 1.0f, time_seconds );
        const rendering::Color brighter_fill = rendering::scale_color_rgb( fill, 1.02f );
        const rendering::Color edge_fill = rendering::scale_color_rgb( fill, 0.92f );

        const rendering::Color border{ 1.00f, 0.74f, 0.00f, 1.00f * alpha };
        const rendering::Color glass{ 1.00f, 0.96f, 0.72f, 0.11f * alpha };
        const rendering::Color highlight{ 1.00f, 1.00f, 0.92f, 0.18f * alpha };

        draw_phrase_tube_solid_disc( renderer, center, outer_radius, border );

        rendering::Color scaled_bright_fill = brighter_fill;
        rendering::Color scaled_edge_fill = edge_fill;
        scaled_bright_fill.a *= alpha;

        scaled_edge_fill.a *= alpha;

        draw_phrase_tube_disc_gradient( renderer, center, inner_radius, scaled_bright_fill, scaled_edge_fill );
        draw_phrase_tube_solid_disc( renderer, center, inner_radius * phrase_tube_head_glass_scale, glass, 40 );

        const float highlight_offset = inner_radius * phrase_tube_head_highlight_offset;
        draw_phrase_tube_solid_disc( renderer,
            rendering::Vec2{ center.x - highlight_offset, center.y - highlight_offset },
            inner_radius * phrase_tube_head_highlight_scale,
            highlight,
            24 );
    }

    void draw_textured_phrase_note( rendering::Renderer &renderer,
        const rendering::Texture *note_texture,
        const rendering::Vec2 &center,
        float time_seconds,
        const PhraseNoteAppearance &appearance )
    {
        const float dimension_scale = std::max( 0.25f, appearance.scale );

        const float alpha = std::clamp( appearance.alpha * appearance.tint.a, 0.0f, 1.0f );

        if( alpha <= 0.0f )
        {
            return;
        }

        const rendering::Vec2 stretched_center = apply_phrase_presentation_stretch( center,
            appearance.presentation.center,
            appearance.presentation.scale_x,
            appearance.presentation.scale_y );

        const float diameter = appearance.height * phrase_tube_note_head_scale * dimension_scale;
        const float dst_h = diameter;
        const float dst_w = diameter * appearance.source.w / std::max( appearance.source.h, 1.0f );

        if( note_texture != nullptr && note_texture->valid() )
        {
            if( appearance.shadow && phrase_tube_head_shadow_alpha > 0.0f )
            {
                const float shadow_w = dst_w * phrase_tube_head_shadow_scale;
                const float shadow_h = dst_h * phrase_tube_head_shadow_scale;
                renderer.draw_texture_region( *note_texture,
                    appearance.source.x,
                    appearance.source.y,
                    appearance.source.w,
                    appearance.source.h,
                    stretched_center.x - shadow_w * 0.5f,
                    stretched_center.y - shadow_h * 0.5f,
                    shadow_w,
                    shadow_h,
                    rendering::Color{ 0.0f, 0.0f, 0.0f, alpha * phrase_tube_head_shadow_alpha } );
            }

            renderer.draw_texture_region( *note_texture,
                appearance.source.x,
                appearance.source.y,
                appearance.source.w,
                appearance.source.h,
                stretched_center.x - dst_w * 0.5f,
                stretched_center.y - dst_h * 0.5f,
                dst_w,
                dst_h,
                rendering::Color{
                    appearance.tint.r,
                    appearance.tint.g,
                    appearance.tint.b,
                    alpha
                } );

            return;
        }

        if( std::abs( appearance.tint.r - 1.0f ) > 0.001f || std::abs( appearance.tint.g - 1.0f ) > 0.001f ||
            std::abs( appearance.tint.b - 1.0f ) > 0.001f )
        {
            draw_phrase_tube_ellipse_gradient( renderer,
                stretched_center,
                dst_w * 0.5f,
                dst_h * 0.5f,
                rendering::Color{
                    std::clamp( appearance.tint.r * 1.08f + 0.04f, 0.0f, 1.0f ),
                    std::clamp( appearance.tint.g * 1.08f + 0.06f, 0.0f, 1.0f ),
                    std::clamp( appearance.tint.b * 1.08f + 0.04f, 0.0f, 1.0f ),
                    alpha * 0.48f
                },
                rendering::Color{
                    std::clamp( appearance.tint.r * 0.94f + 0.04f, 0.0f, 1.0f ),
                    std::clamp( appearance.tint.g * 0.94f + 0.04f, 0.0f, 1.0f ),
                    std::clamp( appearance.tint.b * 0.94f + 0.02f, 0.0f, 1.0f ),
                    alpha * 0.90f
                },
                40 );
            return;
        }

        draw_phrase_note_head( renderer, stretched_center, time_seconds, dimension_scale, alpha );
    }

    float phrase_path_length( const std::vector<rendering::Vec2> &points )
    {
        if( points.size() < 2 )
        {
            return 0.0f;
        }

        float distance = 0.0f;

        for( std::size_t i = 1; i < points.size(); ++i )
        {
            distance += tube_length( tube_sub( points[ i ], points[ i - 1 ] ) );
        }

        return distance;
    }

    std::vector<rendering::Vec2> insert_phrase_path_point_at_distance( const std::vector<rendering::Vec2> &points,
        float target_distance )
    {
        if( points.size() < 2 )
        {
            return points;
        }

        const float total_distance = phrase_path_length( points );
        target_distance = std::clamp( target_distance, 0.0f, total_distance );

        std::vector<rendering::Vec2> out;
        out.reserve( points.size() + 1u );
        out.push_back( points.front() );

        float distance = 0.0f;
        bool inserted = false;

        for( std::size_t i = 1; i < points.size(); ++i )
        {
            const rendering::Vec2 a = points[ i - 1 ];
            const rendering::Vec2 b = points[ i ];
            const rendering::Vec2 delta = tube_sub( b, a );
            const float segment_length = tube_length( delta );

            if( !inserted && segment_length > 0.0001f && target_distance > distance + 0.001f &&
                target_distance < distance + segment_length - 0.001f )
            {
                const float t = ( target_distance - distance ) / segment_length;

                out.push_back( rendering::Vec2{ a.x + delta.x * t, a.y + delta.y * t } );

                inserted = true;
            }

            out.push_back( b );
            distance += segment_length;
        }

        return remove_near_duplicate_points( out );
    }

    bool draw_textured_tube_segments( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const std::vector<PikoRibbonSample> &input_samples,
        const TubeSegmentsAppearance &appearance )
    {
        if( input_samples.size() < 2 )
        {
            return false;
        }

        std::vector<PikoRibbonSample> samples = piko_ribbon_remove_near_duplicate_samples( input_samples );
        samples = piko_ribbon_densify_samples( samples, piko_ribbon_phrase_max_segment_pixels );

        samples = piko_ribbon_remove_near_duplicate_samples( samples );

        if( samples.size() < 2 )
        {
            return false;
        }

        piko_ribbon_apply_presentation_stretch( samples,
            appearance.presentation.center,
            appearance.presentation.scale_x,
            appearance.presentation.scale_y );

        const float total_distance = samples.back().distance;
        const float body_end_distance = total_distance - std::max( 0.0f, appearance.end_trim_pixels );
        float cap_start_distance = body_end_distance;
        float draw_end_distance = body_end_distance;
        const rendering::Rect *cap_source_ptr = nullptr;

        if( appearance.draw_end_cap )
        {
            const float cap_length = std::max( 1.0f, appearance.cap_tail_pixels );

            if( appearance.cap_consumes_body )
            {
                cap_start_distance -= cap_length;
            }
            else
            {
                draw_end_distance += cap_length;
            }

            cap_source_ptr = &appearance.cap_source;
        }

        if( draw_end_distance <= 0.5f )
        {
            return false;
        }

        if( draw_end_distance < total_distance - 0.001f )
        {
            samples = piko_ribbon_slice_by_distance( samples, 0.0f, draw_end_distance );
        }
        else if( draw_end_distance > total_distance + 0.001f )
        {
            piko_ribbon_append_cap_tail( samples, draw_end_distance - total_distance );
        }

        if( samples.size() < 2 )
        {
            return false;
        }

        // Clip the cap instead of shrinking it.
        if( appearance.draw_end_cap && cap_start_distance > 0.0f )
        {
            piko_ribbon_insert_sample_at_distance( samples, cap_start_distance );
        }

        return draw_textured_ribbon( renderer,
            texture,
            samples,
            TexturedRibbonAppearance{
                .height = appearance.height,
                .body_source = appearance.body_source,
                .cap_source = cap_source_ptr,
                .cap_start_distance = cap_start_distance,
                .tint = appearance.tint,
                .pulse = appearance.pulse,
                .time_seconds = appearance.time_seconds,
                .start_cutout_radius = appearance.start_cutout_radius
            } );
    }

    bool draw_textured_tube_segments( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const std::vector<rendering::Vec2> &points,
        const TubeSegmentsAppearance &appearance )
    {
        auto opaque = appearance;
        opaque.tint.a = 1.0f;
        return draw_textured_tube_segments( renderer,
            texture,
            piko_ribbon_samples_from_points( points, appearance.tint.a ),
            opaque );
    }

    bool draw_textured_phrase_tube( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const rendering::Texture *note_texture,
        const std::vector<runtime::stage::TraceScreenPoint> &raw_points,
        const PhraseTubeAppearance &appearance )
    {
        if( raw_points.empty() )
        {
            return false;
        }

        const float dimension_scale = std::max( 0.25f, appearance.scale );

        std::vector<PikoRibbonSample> samples = piko_ribbon_samples_from_trace_screen_points( raw_points );

        if( samples.empty() )
        {
            return false;
        }

        const rendering::Vec2 real_start_center = samples.front().point;

        if( samples.size() == 1 )
        {
            if( appearance.draw_begin_head )
            {
                draw_textured_phrase_note( renderer,
                    note_texture,
                    real_start_center,
                    appearance.time_seconds,
                    PhraseNoteAppearance{
                        .scale = dimension_scale,
                        .alpha = 0.96f,
                        .presentation = appearance.presentation,
                        .tint = appearance.note_tint,
                        .shadow = true,
                        .source = appearance.regions.note,
                        .height = appearance.regions.note_height
                    } );
            }

            return true;
        }

        piko_ribbon_recompute_distances( samples );

        const float start_trim = std::max( 0.0f, appearance.start_trim_pixels );

        if( start_trim > 0.001f )
        {
            samples = piko_ribbon_slice_by_distance( samples, start_trim, samples.back().distance );

            if( samples.size() < 2 )
            {
                if( appearance.draw_begin_head )
                {
                    draw_textured_phrase_note( renderer,
                        note_texture,
                        real_start_center,
                        appearance.time_seconds,
                        PhraseNoteAppearance{
                            .scale = dimension_scale,
                            .alpha = 0.96f,
                            .presentation = appearance.presentation,
                            .tint = appearance.note_tint,
                            .shadow = true,
                            .source = appearance.regions.note,
                            .height = appearance.regions.note_height
                        } );
                }

                return appearance.draw_begin_head;
            }
        }

        const rendering::Vec2 start_center = real_start_center;

        if( phrase_tube_shadow_alpha > 0.0f )
        {
            draw_textured_tube_segments( renderer,
                texture,
                samples,
                TubeSegmentsAppearance{
                    .body_source = appearance.regions.body,
                    .cap_source = appearance.regions.cap,
                    .height = appearance.regions.body_height * dimension_scale * phrase_tube_shadow_scale,
                    .draw_end_cap = appearance.draw_end_cap,
                    .tint = rendering::Color{ 0.0f, 0.0f, 0.0f, phrase_tube_shadow_alpha * appearance.tint.a },
                    .cap_tail_pixels = phrase_tube_end_cap_body_cut_pixels * dimension_scale *
                                       appearance.regions.body_height / ui::note_texture_canvas_height,
                    .end_trim_pixels = appearance.end_trim_pixels,
                    .cap_consumes_body = appearance.cap_consumes_body,
                    .presentation = appearance.presentation,
                    .pulse =
                        RibbonPulse{ .enabled = false, .tint = appearance.pulse.tint, .bpm = appearance.pulse.bpm },
                    .time_seconds = appearance.time_seconds,
                    .start_cutout_radius = appearance.start_cutout_radius
                } );
        }

        const bool drew_any = draw_textured_tube_segments( renderer,
            texture,
            samples,
            TubeSegmentsAppearance{
                .body_source = appearance.regions.body,
                .cap_source = appearance.regions.cap,
                .height = appearance.regions.body_height * dimension_scale,
                .draw_end_cap = appearance.draw_end_cap,
                .tint =
                    rendering::Color{
                        appearance.tint.r,
                        appearance.tint.g,
                        appearance.tint.b,
                        phrase_tube_texture_tint_alpha * appearance.tint.a
                    },
                .cap_tail_pixels = phrase_tube_end_cap_body_cut_pixels * dimension_scale *
                                   appearance.regions.body_height / ui::note_texture_canvas_height,
                .end_trim_pixels = appearance.end_trim_pixels,
                .cap_consumes_body = appearance.cap_consumes_body,
                .presentation = appearance.presentation,
                .pulse = appearance.pulse,
                .time_seconds = appearance.time_seconds,
                .start_cutout_radius = appearance.start_cutout_radius
            } );

        if( appearance.draw_begin_head )
        {
            draw_textured_phrase_note( renderer,
                note_texture,
                start_center,
                appearance.time_seconds,
                PhraseNoteAppearance{
                    .scale = dimension_scale,
                    .alpha = 0.98f,
                    .presentation = appearance.presentation,
                    .tint = appearance.note_tint,
                    .shadow = true,
                    .source = appearance.regions.note,
                    .height = appearance.regions.note_height
                } );
        }

        return drew_any || appearance.draw_begin_head || appearance.draw_end_cap;
    }

    void draw_phrase_tube_path( rendering::Renderer &renderer,
        const std::vector<rendering::Vec2> &raw_points,
        float time_seconds,
        bool draw_begin_head,
        bool draw_end_cap,
        float dimension_scale,
        float note_alpha,
        float body_alpha )
    {
        note_alpha = std::clamp( note_alpha, 0.0f, 1.0f );
        body_alpha = std::clamp( body_alpha, 0.0f, 1.0f );

        if( raw_points.empty() )
        {
            return;
        }

        std::vector<rendering::Vec2> points = remove_near_duplicate_points( raw_points );
        points = densify_path( points, phrase_tube_max_segment_pixels );

        points = remove_near_duplicate_points( points );

        if( points.empty() )
        {
            return;
        }

        if( points.size() < 2 )
        {
            if( draw_begin_head || draw_end_cap )
            {
                draw_phrase_note_head( renderer, points.front(), time_seconds, dimension_scale, note_alpha );
            }

            return;
        }

        const std::vector<PhraseTubeFrame> frames = build_phrase_tube_frames( points );

        if( frames.size() < 2 )
        {
            return;
        }

        const float outer_radius = std::max( 1.0f, phrase_tube_outer_radius * dimension_scale );
        const float inner_radius =
            std::max( 1.0f, outer_radius - std::max( 0.0f, phrase_tube_border_thickness * dimension_scale ) );

        const float glass_half_width = std::max( 1.0f, inner_radius * phrase_tube_glass_center_scale );

        const rendering::Color soft_border{ 1.00f, 0.60f, 0.00f, 0.20f * body_alpha };
        const rendering::Color border{ 1.00f, 0.74f, 0.00f, body_alpha };
        const rendering::Color glass{ 1.00f, 0.96f, 0.72f, 0.11f * body_alpha };

        const rendering::Vec2 cap_center = frames.back().point;
        const rendering::Vec2 cap_tangent = frames.back().tangent;

        draw_phrase_tube_outer_ribbon( renderer,
            frames,
            outer_radius + phrase_tube_soft_edge_pixels * dimension_scale,
            soft_border );

        if( draw_end_cap )
        {
            draw_phrase_tube_semidisc( renderer,
                cap_center,
                cap_tangent,
                ( outer_radius + phrase_tube_soft_edge_pixels * dimension_scale ) * phrase_tube_end_cap_scale,
                soft_border,
                soft_border,
                24 );
        }

        draw_phrase_tube_outer_ribbon( renderer, frames, outer_radius, border );

        if( draw_end_cap )
        {
            draw_phrase_tube_semidisc( renderer,
                cap_center,
                cap_tangent,
                outer_radius * phrase_tube_end_cap_scale,
                border,
                border,
                24 );
        }

        draw_phrase_tube_fill_ribbon( renderer, frames, inner_radius, time_seconds, body_alpha );

        if( draw_end_cap )
        {
            const rendering::Color cap_fill = rendering::scale_color_alpha(
                sample_phrase_tube_fill_color( frames.back().distance, frames.back().distance, time_seconds ),
                body_alpha );

            draw_phrase_tube_semidisc( renderer,
                cap_center,
                cap_tangent,
                inner_radius * phrase_tube_end_cap_scale,
                rendering::scale_color_rgb( cap_fill, 1.02f ),
                rendering::scale_color_rgb( cap_fill, 0.92f ) );
        }

        draw_phrase_tube_outer_ribbon( renderer, frames, glass_half_width, glass );

        if( draw_end_cap )
        {
            draw_phrase_tube_semidisc( renderer,
                cap_center,
                cap_tangent,
                glass_half_width * phrase_tube_end_cap_scale,
                glass,
                glass,
                14 );
        }

        if( draw_begin_head )
        {
            draw_phrase_note_head( renderer, frames.front().point, time_seconds, dimension_scale, note_alpha );
        }
    }

    float phrase_hit_pixels_per_sample( const std::vector<runtime::stage::TraceSample> &trace_samples,
        const runtime::stage::StageGameplayView &view )
    {
        float total_distance = 0.0f;
        std::int64_t total_samples = 0;

        for( std::size_t i = 1; i < trace_samples.size(); ++i )
        {
            const std::int64_t sample_delta = trace_samples[ i ].chart_sample - trace_samples[ i - 1 ].chart_sample;

            if( sample_delta <= 0 )
            {
                continue;
            }

            const float dx = trace_samples[ i ].x - trace_samples[ i - 1 ].x;
            const float dy = trace_samples[ i ].y - trace_samples[ i - 1 ].y;
            const float distance = std::sqrt( dx * dx + dy * dy );

            if( distance <= 0.0f || !std::isfinite( distance ) )
            {
                continue;
            }

            total_distance += distance;

            total_samples += sample_delta;
        }

        if( total_samples > 0 && total_distance > 0.0f )
        {
            const float world_units_per_sample = total_distance / static_cast<float>( total_samples );
            return std::max( world_units_per_sample * view.zoom, gameplay_zoom / 240.0f ) * view.note_speed;
        }

        return gameplay_zoom / 240.0f * view.note_speed;
    }

    rendering::Color phrase_note_normal_tint( float alpha )
    {
        return phrase_note_color_preset( 0, alpha );
    }

    rendering::Color phrase_note_lost_tint( float alpha )
    {
        return rendering::Color{ 0.04f, 0.17f, 0.45f, alpha };
    }

    rendering::Color phrase_note_length_tint( float alpha )
    {
        return rendering::Color{ 210.0f / 255.0f, 110.0f / 255.0f, 110.0f / 255.0f, alpha };
    }

    bool phrase_group_is_darkened( const PhraseDrawGroup &group, const std::vector<PhraseVisualOverride> *overrides )
    {
        if( overrides == nullptr )
        {
            return false;
        }

        for( const PhraseVisualOverride &override_state : *overrides )
        {
            if( !override_state.darkened )
            {
                continue;
            }

            const std::int64_t overlap_start = std::max( group.start_sample, override_state.start_sample );
            const std::int64_t overlap_end = std::min( group.end_sample, override_state.end_sample );

            if( overlap_end > overlap_start )
            {
                return true;
            }
        }

        return false;
    }

    bool phrase_group_is_held( const PhraseDrawGroup &group, const std::vector<PhraseVisualOverride> *overrides )
    {
        if( overrides == nullptr )
        {
            return false;
        }

        for( const PhraseVisualOverride &override_state : *overrides )
        {
            if( !override_state.held )
            {
                continue;
            }

            const std::int64_t overlap_start = std::max( group.start_sample, override_state.start_sample );
            const std::int64_t overlap_end = std::min( group.end_sample, override_state.end_sample );

            if( overlap_end > overlap_start )
            {
                return true;
            }
        }

        return false;
    }

    void draw_darkened_phrase_overlay( rendering::Renderer &renderer,
        const std::vector<rendering::Vec2> &raw_points,
        const runtime::stage::StageGameplayView &view,
        bool draw_begin_icon,
        float note_alpha,
        float body_alpha )
    {
        note_alpha = std::clamp( note_alpha, 0.0f, 1.0f );
        body_alpha = std::clamp( body_alpha, 0.0f, 1.0f );

        if( raw_points.empty() )
        {
            return;
        }

        std::vector<rendering::Vec2> points;
        points.reserve( raw_points.size() );

        for( const rendering::Vec2 &point : raw_points )
        {
            points.push_back( apply_phrase_presentation_stretch( point,
                rendering::Vec2{ view.presentation_center_x, view.presentation_center_y },
                view.presentation_scale_x,
                view.presentation_scale_y ) );
        }

        if( points.size() >= 2 && body_alpha > 0.0f )
        {
            const float dimension_scale = std::max( 0.25f, view.zoom ) * phrase_note_visual_scale;

            for( std::size_t i = 1; i < points.size(); ++i )
            {
                renderer.draw_line( points[ i - 1 ],
                    points[ i ],
                    itfc_phrase_tube_height * dimension_scale * 1.05f,
                    rendering::Color{ 0.015f, 0.055f, 0.16f, 0.78f * body_alpha } );

                renderer.draw_line( points[ i - 1 ],
                    points[ i ],
                    itfc_phrase_tube_height * dimension_scale * 0.46f,
                    rendering::Color{ 0.05f, 0.18f, 0.40f, 0.72f * body_alpha } );
            }
        }

        if( draw_begin_icon && !points.empty() && note_alpha > 0.0f )
        {
            const float dimension_scale = std::max( 0.25f, view.zoom ) * phrase_note_visual_scale;
            draw_filled_disc( renderer,
                points.front(),
                itfc_phrase_tube_height * 0.60f * dimension_scale,
                rendering::Color{ 0.02f, 0.08f, 0.22f, 0.82f * note_alpha } );

            draw_disc_outline( renderer,
                points.front(),
                itfc_phrase_tube_height * 0.62f * dimension_scale,
                2.5f,
                rendering::Color{ 0.12f, 0.34f, 0.72f, 0.82f * note_alpha } );
        }
    }

    void apply_phrase_tube_wobble( std::vector<runtime::stage::TraceScreenPoint> &points,
        std::int64_t group_start_sample,
        std::int64_t group_end_sample,
        float time_seconds,
        float wobble_bpm )
    {
        if( points.size() < 3 )
        {
            return;
        }

        const std::int64_t duration_samples = group_end_sample - group_start_sample;

        if( duration_samples < sample_rate / 3 )
        {
            return;
        }

        std::vector<float> distances;
        distances.resize( points.size(), 0.0f );

        for( std::size_t i = 1; i < points.size(); ++i )
        {
            distances[ i ] = distances[ i - 1 ] + tube_length( tube_sub( points[ i ].point, points[ i - 1 ].point ) );
        }

        const float total_distance = distances.back();

        if( total_distance < 32.0f )
        {
            return;
        }

        constexpr float tau = 6.2831855f;
        constexpr float wavelength_pixels = 58.0f;
        const float scroll_cycles_per_second = std::clamp( wobble_bpm, 60.0f, 240.0f ) / 60.0f;
        const float amplitude_pixels = std::clamp( phrase_tube_outer_radius * 0.24f, 2.0f, 2.25f );

        for( std::size_t i = 1; i + 1 < points.size(); ++i )
        {
            const rendering::Vec2 prev = points[ i - 1 ].point;
            const rendering::Vec2 next = points[ i + 1 ].point;
            const rendering::Vec2 tangent = tube_sub( next, prev );
            const float tangent_length = tube_length( tangent );

            if( tangent_length <= 0.001f )
            {
                continue;
            }

            const rendering::Vec2 normal{ -tangent.y / tangent_length, tangent.x / tangent_length };

            const float path_t = distances[ i ] / std::max( total_distance, 0.001f );
            const float endpoint_taper =
                piko::utils::smoothstep( path_t / 0.18f ) * piko::utils::smoothstep( ( 1.0f - path_t ) / 0.18f );

            const float phase = ( distances[ i ] / wavelength_pixels - time_seconds * scroll_cycles_per_second ) * tau;
            const float offset = std::sin( phase ) * amplitude_pixels * endpoint_taper;

            points[ i ].point.x += normal.x * offset;

            points[ i ].point.y += normal.y * offset;
        }
    }

    std::vector<PhraseDrawSpan> phrase_draw_runs( const PhraseDrawGroup &group )
    {
        std::vector<PhraseDrawSpan> runs;
        runs.reserve( group.spans.size() );

        for( const PhraseDrawSpan &span : group.spans )
        {
            if( span.end_sample <= span.start_sample )
            {
                continue;
            }

            if( runs.empty() || span.start_sample > runs.back().end_sample )
            {
                runs.push_back( span );
                continue;
            }

            runs.back().end_sample = std::max( runs.back().end_sample, span.end_sample );
        }

        return runs;
    }

    std::vector<PhraseDrawGroup> build_phrase_draw_groups( const filetypes::chc::Chart &chart )
    {
        std::vector<PhraseDrawGroup> groups;
        groups.reserve( chart.phrases.size() );

        PhraseDrawGroup current{};
        bool in_group = false;

        const auto flush_group = [ & ]()
        {
            if( !in_group )
            {
                return;
            }

            if( current.end_sample > current.start_sample && !current.spans.empty() )
            {
                groups.push_back( current );
            }

            current = {};

            in_group = false;
        };

        for( const filetypes::chc::PhraseFragment &fragment : chart.phrases )
        {
            const std::int64_t start = static_cast<std::int64_t>( fragment.start_sample );
            const std::int64_t duration = static_cast<std::int64_t>( fragment.duration_samples );
            const std::int64_t end = start + duration;

            if( duration <= 0 )
            {
                continue;
            }

            const bool starts_phrase = fragment.phrase_start != 0;
            const bool ends_phrase = fragment.phrase_end != 0;

            if( starts_phrase || !in_group )
            {
                flush_group();
                current.start_sample = start;

                current.end_sample = end;

                current.authored_duration_samples = duration;

                current.has_begin_icon = starts_phrase;

                current.has_end_cap = ends_phrase;
                current.spans.push_back( PhraseDrawSpan{ start, end } );
                in_group = true;
            }
            else
            {
                current.end_sample = std::max( current.end_sample, end );

                current.authored_duration_samples += duration;

                current.has_end_cap = current.has_end_cap || ends_phrase;
                current.spans.push_back( PhraseDrawSpan{ start, end } );
            }

            if( ends_phrase )
            {
                flush_group();
            }
        }

        flush_group();
        return groups;
    }

    rendering::Vec2 phrase_window_screen_point( const std::vector<runtime::stage::TraceSample> &trace_samples,
        const runtime::stage::TracePosition &current_position,
        std::int64_t chart_sample,
        const runtime::stage::StageGameplayView &view )
    {
        const runtime::stage::TracePosition position =
            runtime::stage::visual_trace_position_at( trace_samples, chart_sample, true );

        rendering::Vec2 screen =
            runtime::stage::gameplay_world_to_screen( position.x, position.y, current_position, view );

        return runtime::stage::apply_gameplay_presentation_stretch( screen, view );
    }

    void draw_phrase_open_window_marker( rendering::Renderer &renderer,
        const std::vector<runtime::stage::TraceSample> &trace_samples,
        const runtime::stage::TracePosition &current_position,
        std::int64_t current_chart_sample,
        std::int64_t phrase_start_sample,
        const runtime::stage::StageGameplayView &view,
        const rendering::Rect &view_area )
    {
        if( trace_samples.size() < 2 )
        {
            return;
        }

        const std::int64_t early_sample = phrase_start_sample - phrase_debug_window_samples;
        const std::int64_t late_sample = phrase_start_sample + phrase_debug_window_samples;

        if( late_sample < current_chart_sample - trace_fade_start_samples )
        {
            return;
        }

        if( early_sample > current_chart_sample + trace_lookahead_samples )
        {
            return;
        }

        const rendering::Vec2 early = phrase_window_screen_point( trace_samples, current_position, early_sample, view );
        const rendering::Vec2 target =
            phrase_window_screen_point( trace_samples, current_position, phrase_start_sample, view );

        const rendering::Vec2 late = phrase_window_screen_point( trace_samples, current_position, late_sample, view );

        if( !point_maybe_visible( early, view_area ) && !point_maybe_visible( target, view_area ) &&
            !point_maybe_visible( late, view_area ) )
        {
            return;
        }

        rendering::Vec2 tangent = tube_sub( late, early );
        const float tangent_length = std::sqrt( tangent.x * tangent.x + tangent.y * tangent.y );

        if( tangent_length <= 0.0001f )
        {
            tangent = rendering::Vec2{ 1.0f, 0.0f };
        }
        else
        {
            tangent = rendering::Vec2{ tangent.x / tangent_length, tangent.y / tangent_length };
        }

        const rendering::Vec2 normal{ -tangent.y, tangent.x };

        const float marker_scale = std::max( 0.01f, view.zoom );

        renderer.draw_line( early, late, 5.0f * marker_scale, rendering::Color{ 0.16f, 0.80f, 1.0f, 0.16f } );

        renderer.draw_line( early.x - normal.x * 9.0f * marker_scale,
            early.y - normal.y * 9.0f * marker_scale,
            early.x + normal.x * 9.0f * marker_scale,
            early.y + normal.y * 9.0f * marker_scale,
            2.0f * marker_scale,
            rendering::Color{ 0.42f, 0.94f, 1.0f, 0.42f } );

        renderer.draw_line( late.x - normal.x * 9.0f * marker_scale,
            late.y - normal.y * 9.0f * marker_scale,
            late.x + normal.x * 9.0f * marker_scale,
            late.y + normal.y * 9.0f * marker_scale,
            2.0f * marker_scale,
            rendering::Color{ 0.42f, 0.94f, 1.0f, 0.42f } );

        renderer.draw_line( target.x - normal.x * 16.0f * marker_scale,
            target.y - normal.y * 16.0f * marker_scale,
            target.x + normal.x * 16.0f * marker_scale,
            target.y + normal.y * 16.0f * marker_scale,
            7.0f * marker_scale,
            rendering::Color{ 0.20f, 0.86f, 1.0f, 0.24f } );

        renderer.draw_line( target.x - normal.x * 15.0f * marker_scale,
            target.y - normal.y * 15.0f * marker_scale,
            target.x + normal.x * 15.0f * marker_scale,
            target.y + normal.y * 15.0f * marker_scale,
            3.0f * marker_scale,
            rendering::Color{ 0.86f, 1.0f, 1.0f, 0.90f } );
    }

    float estimate_chart_visual_bpm( const filetypes::chc::Chart &chart )
    {
        std::vector<std::int64_t> starts;
        starts.reserve( chart.phrases.size() + chart.guards.size() );

        for( const filetypes::chc::PhraseFragment &fragment : chart.phrases )
        {
            if( fragment.phrase_start != 0 )
            {
                starts.push_back( static_cast<std::int64_t>( fragment.start_sample ) );
            }
        }

        for( const filetypes::chc::GuardFragment &guard : chart.guards )
        {
            starts.push_back( static_cast<std::int64_t>( guard.start_sample ) );
        }

        if( starts.size() < 2 )
        {
            return 120.0f;
        }

        std::sort( starts.begin(), starts.end() );
        starts.erase( std::unique( starts.begin(), starts.end() ), starts.end() );

        std::vector<float> folded_bpms;
        folded_bpms.reserve( starts.size() );

        for( std::size_t i = 1; i < starts.size(); ++i )
        {
            const std::int64_t delta_samples = starts[ i ] - starts[ i - 1 ];

            if( delta_samples < sample_rate / 20 || delta_samples > sample_rate * 4 )
            {
                continue;
            }

            float bpm = 60.0f * static_cast<float>( sample_rate ) /
                        static_cast<float>( std::max<std::int64_t>( delta_samples, 1 ) );

            while( bpm > 190.0f )
            {
                bpm *= 0.5f;
            }
            while( bpm < 75.0f )
            {
                bpm *= 2.0f;
            }

            if( bpm >= 75.0f && bpm <= 190.0f )
            {
                folded_bpms.push_back( bpm );
            }
        }

        if( folded_bpms.empty() )
        {
            return 120.0f;
        }

        std::sort( folded_bpms.begin(), folded_bpms.end() );
        return folded_bpms[ folded_bpms.size() / 2u ];
    }

    void draw_phrase_fragments( rendering::Renderer &renderer,
        const filetypes::chc::Chart &chart,
        const std::vector<runtime::stage::TraceSample> &trace_samples,
        const runtime::stage::TracePosition &current_position,
        std::int64_t current_chart_sample,
        const runtime::stage::StageGameplayView &view,
        const rendering::Rect &view_area,
        float time_seconds,
        const rendering::Texture *tube_texture,
        const rendering::Texture *itfc_texture,
        float phrase_tube_visual_trim_pixels,
        bool phrase_cap_consumes_body,
        const std::vector<PhraseVisualOverride> *visual_overrides,
        bool phrase_tube_wobble_enabled,
        std::int32_t phrase_note_color_index,
        std::int32_t phrase_note_held_color_index,
        bool phrase_tube_gradient_pulse_enabled,
        float visual_bpm,
        bool show_phrase_timing_markers,
        const std::vector<PhraseHitVisualOverride> *hit_visual_overrides,
        float visual_size_scale,
        rendering::Color custom_note_color,
        rendering::Color custom_held_note_color,
        rendering::Color custom_note_body_color,
        const ui::NoteTextureRegions &texture_regions )
    {
        if( trace_samples.size() < 2 )
        {
            return;
        }

        const std::vector<PhraseDrawGroup> groups = build_phrase_draw_groups( chart );

        for( const PhraseDrawGroup &group : groups )
        {
            if( group.start_sample > current_chart_sample + trace_lookahead_samples )
            {
                continue;
            }

            const bool darkened_phrase = phrase_group_is_darkened( group, visual_overrides );
            const bool held_phrase = phrase_group_is_held( group, visual_overrides );
            const rendering::Color phrase_note_tint =
                darkened_phrase ? phrase_note_lost_tint( held_phrase ? custom_held_note_color.a : custom_note_color.a )
                                : ( held_phrase ? custom_held_note_color : custom_note_color );

            const rendering::Color phrase_length_tint =
                darkened_phrase ? phrase_note_lost_tint( held_phrase ? custom_held_note_color.a : custom_note_body_color.a )
                                : ( held_phrase ? custom_held_note_color : custom_note_body_color );

            const bool use_gradient_pulse = phrase_tube_gradient_pulse_enabled && held_phrase && !darkened_phrase;
            const rendering::Color pulse_tint = phrase_note_pulse_tint_for( phrase_length_tint );
            const float dimension_scale = std::max( 0.25f, view.zoom ) * phrase_note_visual_scale * visual_size_scale;
            const std::vector<PhraseDrawSpan> runs = phrase_draw_runs( group );

            for( std::size_t run_index = 0; run_index < runs.size(); ++run_index )
            {
                const PhraseDrawSpan &run = runs[ run_index ];
                const std::int64_t draw_start_sample =
                    std::max<std::int64_t>( run.start_sample, current_chart_sample + trace_center_clip_samples );

                const std::int64_t draw_end_sample =
                    std::min<std::int64_t>( run.end_sample, current_chart_sample + trace_lookahead_samples );

                if( draw_end_sample <= draw_start_sample )
                {
                    continue;
                }

                std::vector<runtime::stage::TraceScreenPoint> phrase_trace_points =
                    sample_trace_screen_path( trace_samples,
                        current_position,
                        current_chart_sample,
                        draw_start_sample,
                        draw_end_sample,
                        view );

                if( phrase_trace_points.size() < 2 )
                {
                    continue;
                }

                if( phrase_tube_wobble_enabled && held_phrase )
                {
                    apply_phrase_tube_wobble( phrase_trace_points,
                        group.start_sample,
                        group.end_sample,
                        time_seconds,
                        visual_bpm );
                }

                std::vector<rendering::Vec2> phrase_points;
                phrase_points.reserve( phrase_trace_points.size() );

                for( const runtime::stage::TraceScreenPoint &point : phrase_trace_points )
                {
                    phrase_points.push_back( point.point );
                }

                if( !path_maybe_visible( phrase_points, view_area ) )
                {
                    continue;
                }

                const bool draws_begin_icon =
                    group.has_begin_icon && run_index == 0 && draw_start_sample == group.start_sample;

                const bool draws_end_cap =
                    group.has_end_cap && run_index + 1 == runs.size() && draw_end_sample == run.end_sample;

                const float visual_end_trim = draws_end_cap ? phrase_tube_visual_trim_pixels * dimension_scale : 0.0f;

                if( tube_texture != nullptr && tube_texture->valid() )
                {
                    draw_textured_phrase_tube( renderer,
                        *tube_texture,
                        tube_texture,
                        phrase_trace_points,
                        PhraseTubeAppearance{
                            .draw_begin_head = draws_begin_icon,
                            .draw_end_cap = draws_end_cap,
                            .scale = dimension_scale,
                            .time_seconds = time_seconds,
                            .start_trim_pixels = 0.0f,
                            .end_trim_pixels = visual_end_trim,
                            .cap_consumes_body = phrase_cap_consumes_body,
                            .presentation =
                                RibbonPresentation{
                                    .center = rendering::Vec2{ view.presentation_center_x, view.presentation_center_y },
                                    .scale_x = view.presentation_scale_x,
                                    .scale_y = view.presentation_scale_y
                                },
                            .tint = phrase_length_tint,
                            .note_tint = phrase_note_tint,
                            .pulse =
                                RibbonPulse{ .enabled = use_gradient_pulse, .tint = pulse_tint, .bpm = visual_bpm },
                            .start_cutout_radius = 0.0f,
                            .regions = texture_regions
                        } );
                }
                else if( itfc_texture != nullptr && itfc_texture->valid() )
                {
                    draw_textured_phrase_tube( renderer,
                        *itfc_texture,
                        nullptr,
                        phrase_trace_points,
                        PhraseTubeAppearance{
                            .draw_begin_head = draws_begin_icon,
                            .draw_end_cap = draws_end_cap,
                            .scale = dimension_scale,
                            .time_seconds = time_seconds,
                            .start_trim_pixels = 0.0f,
                            .end_trim_pixels = visual_end_trim,
                            .cap_consumes_body = phrase_cap_consumes_body,
                            .presentation =
                                RibbonPresentation{
                                    .center = rendering::Vec2{ view.presentation_center_x, view.presentation_center_y },
                                    .scale_x = view.presentation_scale_x,
                                    .scale_y = view.presentation_scale_y
                                },
                            .tint = phrase_length_tint,
                            .note_tint = phrase_note_tint,
                            .pulse =
                                RibbonPulse{ .enabled = use_gradient_pulse, .tint = pulse_tint, .bpm = visual_bpm },
                            .start_cutout_radius = 0.0f,
                            .regions = ui::NoteTextureRegions{
                                .body = runtime::stage::gameplay_interface_atlas::phrase_tube_body,
                                .cap = runtime::stage::gameplay_interface_atlas::phrase_tube_cap
                            }
                        } );
                }
                else
                {
                    draw_phrase_tube_path( renderer,
                        phrase_points,
                        time_seconds,
                        draws_begin_icon,
                        draws_end_cap,
                        dimension_scale,
                        phrase_note_tint.a,
                        phrase_length_tint.a );
                }

                if( darkened_phrase && ( tube_texture == nullptr || !tube_texture->valid() ) &&
                    ( itfc_texture == nullptr || !itfc_texture->valid() ) )
                {
                    draw_darkened_phrase_overlay( renderer,
                        phrase_points,
                        view,
                        draws_begin_icon,
                        phrase_note_tint.a,
                        phrase_length_tint.a );
                }
            }

            if( show_phrase_timing_markers && group.has_begin_icon )
            {
                draw_phrase_open_window_marker( renderer,
                    trace_samples,
                    current_position,
                    current_chart_sample,
                    group.start_sample,
                    view,
                    view_area );
            }
        }

        if( hit_visual_overrides == nullptr )
        {
            return;
        }

        const float pixels_per_sample = phrase_hit_pixels_per_sample( trace_samples, view );
        const rendering::Color hit_tint = custom_held_note_color;

        for( const PhraseHitVisualOverride &hit_visual : *hit_visual_overrides )
        {
            const std::int64_t elapsed_since_hit = current_chart_sample - hit_visual.hit_sample;

            if( elapsed_since_hit < 0 )
            {
                continue;
            }

            const std::int32_t retail_tick = static_cast<std::int32_t>( elapsed_since_hit / retail_update_samples );

            if( retail_tick < 0 || retail_tick >= retail_hit_pop_tick_count )
            {
                continue;
            }

            const float remaining = 1.0f - retail_hit_pop_timer_step * static_cast<float>( retail_tick );
            const float progress = 1.0f - remaining;

            // sub_10CC10
            const float visual_scale = 1.0f + 3.0f * std::exp( progress );
            const float visual_distance =
                static_cast<float>( current_chart_sample - hit_visual.start_sample ) * pixels_per_sample * 1.5f;

            const rendering::Vec2 position{
                view.center_x + hit_visual.direction_x * visual_distance,
                view.center_y + hit_visual.direction_y * visual_distance
            };

            draw_textured_phrase_note( renderer,
                tube_texture,
                position,
                time_seconds,
                PhraseNoteAppearance{
                    .scale = std::max( 0.25f, view.zoom ) * phrase_note_visual_scale * visual_size_scale * visual_scale,
                    .alpha = remaining,
                    .presentation =
                        RibbonPresentation{
                            .center = rendering::Vec2{ view.presentation_center_x, view.presentation_center_y },
                            .scale_x = view.presentation_scale_x,
                            .scale_y = view.presentation_scale_y
                        },
                    .tint = hit_tint,
                    .shadow = false,
                    .source = texture_regions.note,
                    .height = texture_regions.note_height
                } );
        }
    }
}
