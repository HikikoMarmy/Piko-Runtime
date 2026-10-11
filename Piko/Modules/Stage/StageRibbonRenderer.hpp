#pragma once

#include "Modules/Stage/StageModuleConstants.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/RenderTypes.hpp"
#include "Runtime/Stage/StageTraceBuilder.hpp"
#include "Runtime/Stage/StageView.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace piko::modules::stage
{
    struct RibbonPresentation
    {
        rendering::Vec2 center{};
        float scale_x = 1.0f;
        float scale_y = 1.0f;
    };

    struct RibbonPulse
    {
        bool enabled = false;
        rendering::Color tint{};
        float bpm = 120.0f;
    };

    struct TexturedRibbonAppearance
    {
        float height = ui::note_texture_canvas_height;
        rendering::Rect body_source{};
        const rendering::Rect *cap_source = nullptr;
        float cap_start_distance = 0.0f;
        rendering::Color tint{};
        RibbonPresentation presentation{};
        RibbonPulse pulse{};
        float time_seconds = 0.0f;
        float start_cutout_radius = 0.0f;
    };

    float distance_between( const rendering::Vec2 &a, const rendering::Vec2 &b );

    rendering::Vec2 lerp_point( const rendering::Vec2 &a, const rendering::Vec2 &b, float t );

    bool point_maybe_visible( const rendering::Vec2 &point, const rendering::Rect &area );

    bool path_maybe_visible( const std::vector<rendering::Vec2> &points, const rendering::Rect &area );

    void draw_filled_disc( rendering::Renderer &renderer,
        const rendering::Vec2 &center,
        float radius,
        const rendering::Color &color );

    void draw_disc_outline( rendering::Renderer &renderer,
        const rendering::Vec2 &center,
        float radius,
        float thickness,
        const rendering::Color &color );

    std::vector<rendering::Vec2> densify_path( const std::vector<rendering::Vec2> &points, float max_segment_pixels );

    std::vector<runtime::stage::TraceScreenPoint> sample_trace_screen_path(
        const std::vector<runtime::stage::TraceSample> &samples,
        const runtime::stage::TracePosition &current_position,
        std::int64_t current_chart_sample,
        std::int64_t start_sample,
        std::int64_t end_sample,
        const runtime::stage::StageGameplayView &view );

    struct PhraseTubeFrame
    {
        rendering::Vec2 point{};
        rendering::Vec2 tangent{ 1.0f, 0.0f };
        rendering::Vec2 normal{ 0.0f, -1.0f };
        float distance = 0.0f;
    };

    rendering::Vec2 tube_add( const rendering::Vec2 &a, const rendering::Vec2 &b );

    rendering::Vec2 tube_sub( const rendering::Vec2 &a, const rendering::Vec2 &b );

    rendering::Vec2 tube_mul( const rendering::Vec2 &point, float scale );

    float tube_length( const rendering::Vec2 &point );

    rendering::Vec2 tube_normalize_or( const rendering::Vec2 &point, const rendering::Vec2 &fallback );

    rendering::Vec2 tube_left_normal( const rendering::Vec2 &tangent );

    float tube_dot( const rendering::Vec2 &a, const rendering::Vec2 &b );

    void push_colored_triangle( std::vector<rendering::VertexColor2D> &vertices,
        const rendering::Vec2 &a,
        const rendering::Color &ca,
        const rendering::Vec2 &b,
        const rendering::Color &cb,
        const rendering::Vec2 &c,
        const rendering::Color &cc );

    struct TexturedTubeUv
    {
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 1.0f;
        float v1 = 1.0f;
    };

    void push_textured_triangle( std::vector<rendering::VertexTexture2D> &vertices,
        const rendering::Vec2 &a,
        const rendering::Vec2 &uva,
        const rendering::Vec2 &b,
        const rendering::Vec2 &uvb,
        const rendering::Vec2 &c,
        const rendering::Vec2 &uvc,
        const rendering::Color &color );

    TexturedTubeUv inset_source_uv( const rendering::Texture &texture, const rendering::Rect &source );

    struct PikoRibbonSample
    {
        rendering::Vec2 point{};
        float angle = 0.0f;
        float distance = 0.0f;
        float alpha = 1.0f;
    };

    float piko_ribbon_wrap_angle_delta( float from, float to );

    float piko_ribbon_lerp_angle( float a, float b, float t );

    rendering::Vec2 piko_ribbon_tangent_from_angle( float angle );

    float piko_ribbon_angle_from_points( const rendering::Vec2 &a, const rendering::Vec2 &b, float fallback );

    void piko_ribbon_recompute_distances( std::vector<PikoRibbonSample> &samples );

    std::vector<PikoRibbonSample> piko_ribbon_remove_near_duplicate_samples(
        const std::vector<PikoRibbonSample> &samples );

    void piko_ribbon_stabilize_angles( std::vector<PikoRibbonSample> &samples );

    void piko_ribbon_recompute_geometric_angles( std::vector<PikoRibbonSample> &samples );

    void piko_ribbon_apply_presentation_stretch( std::vector<PikoRibbonSample> &samples,
        const rendering::Vec2 &center,
        float scale_x,
        float scale_y );

    std::vector<PikoRibbonSample> piko_ribbon_samples_from_points( const std::vector<rendering::Vec2> &points,
        float alpha = 1.0f );

    std::vector<PikoRibbonSample> piko_ribbon_samples_from_trace_screen_points(
        const std::vector<runtime::stage::TraceScreenPoint> &points,
        float alpha = 1.0f );

    std::vector<PikoRibbonSample> piko_ribbon_samples_from_trace_render_points(
        const std::vector<runtime::stage::TraceRenderPoint> &points,
        float alpha_scale );

    std::vector<PikoRibbonSample> piko_ribbon_densify_samples( const std::vector<PikoRibbonSample> &samples,
        float max_segment_pixels );

    void piko_ribbon_insert_sample_at_distance( std::vector<PikoRibbonSample> &samples, float target_distance );

    std::vector<PikoRibbonSample> piko_ribbon_slice_by_distance( const std::vector<PikoRibbonSample> &input_samples,
        float start_distance,
        float end_distance );

    void piko_ribbon_append_cap_tail( std::vector<PikoRibbonSample> &samples, float cap_length_pixels );

    void piko_ribbon_edge_pair( const PikoRibbonSample &sample,
        float half_width,
        rendering::Vec2 &out_left,
        rendering::Vec2 &out_right );

    bool draw_textured_ribbon( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const std::vector<PikoRibbonSample> &input_samples,
        const TexturedRibbonAppearance &appearance );

    std::vector<rendering::Vec2> remove_near_duplicate_points( const std::vector<rendering::Vec2> &points );

    std::vector<PhraseTubeFrame> build_phrase_tube_frames( const std::vector<rendering::Vec2> &points );

    void phrase_tube_ribbon_edges_at( const std::vector<PhraseTubeFrame> &frames,
        std::size_t index,
        float half_width,
        float miter_limit,
        rendering::Vec2 &out_left,
        rendering::Vec2 &out_right );

    void phrase_tube_ribbon_edges_stable_at( const std::vector<PhraseTubeFrame> &frames,
        std::size_t index,
        float half_width,
        rendering::Vec2 &out_left,
        rendering::Vec2 &out_right );

    void draw_phrase_tube_outer_ribbon( rendering::Renderer &renderer,
        const std::vector<PhraseTubeFrame> &frames,
        float half_width,
        const rendering::Color &color );
}
