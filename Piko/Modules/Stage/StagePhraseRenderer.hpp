#pragma once

#include "FileTypes/CHC/CHC.hpp"
#include "Modules/Stage/StageModuleConstants.hpp"
#include "Modules/Stage/StageRibbonRenderer.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/RenderTypes.hpp"
#include "Runtime/Stage/StageGameplayInterfaceAtlas.hpp"
#include "Runtime/Stage/StageTraceBuilder.hpp"
#include "Runtime/Stage/StageView.hpp"

#include <cstdint>
#include <vector>

namespace piko::modules::stage
{
    struct PhraseNoteAppearance
    {
        float scale = 1.0f;
        float alpha = 1.0f;
        RibbonPresentation presentation{};
        rendering::Color tint{};
        bool shadow = true;
        rendering::Rect source = generated_tube_note_source;
        float height = ui::note_texture_canvas_height;
    };

    struct TubeSegmentsAppearance
    {
        rendering::Rect body_source{};
        rendering::Rect cap_source{};
        float height = ui::note_texture_canvas_height;
        bool draw_end_cap = false;
        rendering::Color tint{};
        float cap_tail_pixels = phrase_tube_end_cap_body_cut_pixels;
        float end_trim_pixels = 0.0f;
        bool cap_consumes_body = true;
        RibbonPresentation presentation{};
        RibbonPulse pulse{};
        float time_seconds = 0.0f;
        float start_cutout_radius = 0.0f;
    };

    struct PhraseTubeAppearance
    {
        bool draw_begin_head = false;
        bool draw_end_cap = false;
        float scale = 1.0f;
        float time_seconds = 0.0f;
        float start_trim_pixels = 0.0f;
        float end_trim_pixels = 0.0f;
        bool cap_consumes_body = true;
        RibbonPresentation presentation{};
        rendering::Color tint{};
        rendering::Color note_tint{};
        RibbonPulse pulse{};
        float start_cutout_radius = 0.0f;
        ui::NoteTextureRegions regions{};
    };

    constexpr std::int32_t phrase_note_color_preset_count = 8;

    std::int32_t wrap_phrase_note_color_index( std::int32_t index );

    rendering::Color phrase_note_color_preset( std::int32_t index, float alpha = 1.0f );

    rendering::Color phrase_note_pulse_tint_for( rendering::Color base, float alpha = 1.0f );

    rendering::Color sample_phrase_tube_fill_color( float path_distance, float total_distance, float time_seconds );

    void draw_phrase_tube_fill_ribbon( rendering::Renderer &renderer,
        const std::vector<PhraseTubeFrame> &frames,
        float half_width,
        float time_seconds );

    void draw_phrase_tube_disc_gradient( rendering::Renderer &renderer,
        const rendering::Vec2 &center,
        float radius,
        const rendering::Color &center_color,
        const rendering::Color &edge_color,
        std::int32_t segments = 40 );

    void draw_phrase_tube_solid_disc( rendering::Renderer &renderer,
        const rendering::Vec2 &center,
        float radius,
        const rendering::Color &color,
        std::int32_t segments = 40 );

    void draw_phrase_tube_ellipse_gradient( rendering::Renderer &renderer,
        const rendering::Vec2 &center,
        float radius_x,
        float radius_y,
        const rendering::Color &center_color,
        const rendering::Color &edge_color,
        std::int32_t segments = 40 );

    rendering::Vec2 apply_phrase_presentation_stretch( const rendering::Vec2 &point,
        const rendering::Vec2 &center,
        float scale_x,
        float scale_y );

    void draw_phrase_tube_semidisc( rendering::Renderer &renderer,
        const rendering::Vec2 &center,
        const rendering::Vec2 &tangent,
        float radius,
        const rendering::Color &center_color,
        const rendering::Color &edge_color,
        std::int32_t segments = 28 );

    void draw_phrase_note_head( rendering::Renderer &renderer,
        const rendering::Vec2 &center,
        float time_seconds,
        float dimension_scale = 1.0f,
        float alpha = 1.0f );

    void draw_textured_phrase_note( rendering::Renderer &renderer,
        const rendering::Texture *note_texture,
        const rendering::Vec2 &center,
        float time_seconds,
        const PhraseNoteAppearance &appearance = {} );

    float phrase_path_length( const std::vector<rendering::Vec2> &points );

    std::vector<rendering::Vec2> insert_phrase_path_point_at_distance( const std::vector<rendering::Vec2> &points,
        float target_distance );

    bool draw_textured_tube_segments( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const std::vector<PikoRibbonSample> &input_samples,
        const TubeSegmentsAppearance &appearance );

    bool draw_textured_tube_segments( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const std::vector<rendering::Vec2> &points,
        const TubeSegmentsAppearance &appearance );

    bool draw_textured_phrase_tube( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const rendering::Texture *note_texture,
        const std::vector<runtime::stage::TraceScreenPoint> &raw_points,
        const PhraseTubeAppearance &appearance );

    void draw_phrase_tube_path( rendering::Renderer &renderer,
        const std::vector<rendering::Vec2> &raw_points,
        float time_seconds,
        bool draw_begin_head,
        bool draw_end_cap,
        float dimension_scale );

    struct PhraseDrawSpan
    {
        std::int64_t start_sample = 0;
        std::int64_t end_sample = 0;
    };

    struct PhraseDrawGroup
    {
        std::int64_t start_sample = 0;
        std::int64_t end_sample = 0;
        std::int64_t authored_duration_samples = 0;
        bool has_begin_icon = false;
        bool has_end_cap = false;
        std::vector<PhraseDrawSpan> spans;
    };

    struct PhraseVisualOverride
    {
        std::int64_t start_sample = 0;
        std::int64_t end_sample = 0;
        bool darkened = false;
        bool held = false;
    };

    struct PhraseHitVisualOverride
    {
        std::int64_t start_sample = 0;
        std::int64_t hit_sample = 0;
        float direction_x = 1.0f;
        float direction_y = 0.0f;
    };

    float phrase_hit_pixels_per_sample( const std::vector<runtime::stage::TraceSample> &trace_samples,
        const runtime::stage::StageGameplayView &view );

    rendering::Color phrase_note_normal_tint( float alpha = 1.0f );

    rendering::Color phrase_note_lost_tint( float alpha = 1.0f );

    rendering::Color phrase_note_length_tint( float alpha = 1.0f );

    bool phrase_group_is_darkened( const PhraseDrawGroup &group, const std::vector<PhraseVisualOverride> *overrides );

    bool phrase_group_is_held( const PhraseDrawGroup &group, const std::vector<PhraseVisualOverride> *overrides );

    void draw_darkened_phrase_overlay( rendering::Renderer &renderer,
        const std::vector<rendering::Vec2> &raw_points,
        const runtime::stage::StageGameplayView &view,
        bool draw_begin_icon );

    void apply_phrase_tube_wobble( std::vector<runtime::stage::TraceScreenPoint> &points,
        std::int64_t group_start_sample,
        std::int64_t group_end_sample,
        float time_seconds,
        float wobble_bpm );

    std::vector<PhraseDrawSpan> phrase_draw_runs( const PhraseDrawGroup &group );

    std::vector<PhraseDrawGroup> build_phrase_draw_groups( const filetypes::chc::Chart &chart );

    rendering::Vec2 phrase_window_screen_point( const std::vector<runtime::stage::TraceSample> &trace_samples,
        const runtime::stage::TracePosition &current_position,
        std::int64_t chart_sample,
        const runtime::stage::StageGameplayView &view );

    void draw_phrase_open_window_marker( rendering::Renderer &renderer,
        const std::vector<runtime::stage::TraceSample> &trace_samples,
        const runtime::stage::TracePosition &current_position,
        std::int64_t current_chart_sample,
        std::int64_t phrase_start_sample,
        const runtime::stage::StageGameplayView &view,
        const rendering::Rect &view_area );

    float estimate_chart_visual_bpm( const filetypes::chc::Chart &chart );

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
        float phrase_tube_visual_trim_pixels = 0.0f,
        bool phrase_cap_consumes_body = true,
        const std::vector<PhraseVisualOverride> *visual_overrides = nullptr,
        bool phrase_tube_wobble_enabled = false,
        std::int32_t phrase_note_color_index = 0,
        std::int32_t phrase_note_held_color_index = 0,
        bool phrase_tube_gradient_pulse_enabled = false,
        float visual_bpm = 120.0f,
        bool show_phrase_timing_markers = false,
        const std::vector<PhraseHitVisualOverride> *hit_visual_overrides = nullptr,
        float visual_size_scale = 1.0f,
        rendering::Color custom_note_color = phrase_note_color_preset( 0 ),
        rendering::Color custom_held_note_color = phrase_note_color_preset( 0 ),
        rendering::Color custom_note_body_color = phrase_note_length_tint(),
        const ui::NoteTextureRegions &texture_regions = ui::NoteTextureRegions{} );
}
