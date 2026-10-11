#pragma once

#include "Rendering/RenderTypes.hpp"
#include "Utils/BattleTexturePixels.hpp"

#include <cstdint>

namespace piko::modules::stage
{
    constexpr std::int32_t sample_rate = 48000;
    constexpr float trace_line_width = 2.25f;
    constexpr float trace_line_glow_width = 5.0f;
    constexpr float trace_line_glow_alpha = 0.22f;

    constexpr float trace_line_visual_scale = 0.875f;
    constexpr float phrase_note_visual_scale = 1.00f;

    constexpr float aim_marker_visual_scale = 1.00f;
    constexpr float gameplay_zoom = 1.55f;
    constexpr float default_debug_zoom_multiplier = 0.90f;

    constexpr float gameplay_logical_width = 640.0f;
    constexpr float gameplay_logical_height = 448.0f;
    constexpr float gameplay_trace_scale_x = 1.00f;
    constexpr float gameplay_trace_scale_y = 1.00f;

    constexpr float gameplay_presentation_scale_x = 1.42f;
    constexpr float gameplay_presentation_scale_y = 1.22f;
    constexpr float gameplay_presentation_scale_debug_step = 0.02f;
    constexpr float gameplay_presentation_scale_debug_min = 0.50f;
    constexpr float gameplay_presentation_scale_debug_max = 1.80f;

    constexpr float phrase_tube_visual_trim_pixels = 10.0f;
    constexpr float phrase_tube_visual_trim_debug_step = 1.0f;
    constexpr float phrase_tube_visual_trim_debug_min = 0.0f;
    constexpr float phrase_tube_visual_trim_debug_max = 32.0f;
    constexpr bool phrase_tube_cap_consumes_body_default = false;

    constexpr std::int64_t trace_lookahead_samples = sample_rate * 28 / 10;
    constexpr std::int64_t trace_fade_start_samples = sample_rate * 1050 / 1000;
    constexpr std::int64_t next_section_visual_preview_samples = sample_rate * 30;
    constexpr float trace_min_alpha = 28.0f / 255.0f;
    constexpr float trace_max_alpha = 235.0f / 255.0f;

    constexpr std::int64_t phrase_debug_window_samples = 3800;
    constexpr std::int64_t guard_debug_window_samples = 3800;

    constexpr const char *you_win_texture_name = "stage/you-win.png";
    constexpr const char *you_lose_texture_name = "stage/you-lose.png";
    constexpr float view_cone_length = 118.0f;
    constexpr float view_cone_line_width = 2.4375f;
    constexpr float off_center_indicator_radius = 16.0f;
    constexpr float off_center_indicator_fade_start_degrees = 1.5f;
    constexpr float off_center_indicator_fade_end_degrees = 10.0f;
    constexpr float radians_to_degrees = 57.29577951308232f;

    constexpr const char *itfc_texture_name = "stage/itfc.png";
    constexpr const char *generated_score_digit_texture_name = "stage/generated-score-digits";

    using ui::generated_tube_body_source;
    using ui::generated_tube_cap_source;
    using ui::generated_tube_note_source;
    using ui::generated_trace_line_source;
    using ui::generated_tube_texture_width;
    using ui::generated_tube_texture_height;
    using ui::generated_trace_texture_width;
    using ui::generated_trace_texture_height;

    constexpr std::int32_t score_digit_texture_digit_width = 64;
    constexpr std::int32_t score_digit_texture_digit_height = 80;
    constexpr std::int32_t score_digit_texture_digit_count = 10;
    constexpr std::int32_t score_digit_texture_width =
        score_digit_texture_digit_width * score_digit_texture_digit_count;

    constexpr std::int32_t score_digit_texture_height = score_digit_texture_digit_height;

    constexpr std::int64_t trace_center_clip_samples = 0;

    constexpr float phrase_tube_outer_radius = 14.0f;
    constexpr float phrase_tube_border_thickness = 2.5f;
    constexpr float phrase_tube_note_head_border_thickness = 3.0f;
    constexpr float phrase_tube_max_segment_pixels = 3.5f;
    constexpr float phrase_tube_miter_limit = 1.65f;
    constexpr float phrase_tube_soft_edge_pixels = 1.35f;
    constexpr float phrase_tube_glass_center_scale = 0.46f;
    constexpr float phrase_tube_head_glass_scale = 0.56f;
    constexpr float phrase_tube_head_highlight_scale = 0.21f;
    constexpr float phrase_tube_head_highlight_offset = 0.28f;
    constexpr float phrase_tube_end_cap_scale = 1.00f;
    constexpr float phrase_tube_note_head_scale = 1.00f;
    constexpr float phrase_tube_note_only_length_scale = 0.82f;

    constexpr float phrase_tube_note_head_body_cut_ratio = 0.0f;
    constexpr float phrase_tube_note_head_cutout_radius_scale = 0.96f;
    constexpr std::int32_t phrase_tube_note_head_cutout_bands = 18;
    constexpr float phrase_tube_shadow_scale = 1.10f;
    constexpr float phrase_tube_shadow_alpha = 0.16f;
    constexpr float phrase_tube_head_shadow_scale = 1.13f;
    constexpr float phrase_tube_head_shadow_alpha = 0.24f;
    constexpr float phrase_tube_texture_tint_alpha = 0.90f;
    constexpr float phrase_tube_gradient_repeat_pixels = 112.0f;
    constexpr float phrase_tube_gradient_scroll_pixels_per_second = 34.0f;
    constexpr float phrase_tube_gradient_strength = 0.35f;
    constexpr float itfc_phrase_tube_height = 28.0f;
    constexpr float itfc_path_segment_overlap_pixels = 0.0f;
    constexpr float phrase_tube_end_cap_body_cut_pixels = 10.0f;

    constexpr float piko_ribbon_phrase_max_segment_pixels = 1.25f;
    constexpr float piko_ribbon_trace_max_segment_pixels = 1.35f;

    constexpr std::int64_t guard_visual_post_hit_samples = sample_rate / 8;
    constexpr float guard_visual_start_distance = 1050.0f;
    constexpr float guard_icon_scale = 0.65f;
    constexpr float guard_fallback_radius = 16.25f;

    // sub_10CC10 / sub_10DDA0
    constexpr float retail_hit_pop_timer_step = 0.05f;
    constexpr std::int32_t retail_hit_pop_tick_count = 20;
    constexpr std::int64_t retail_update_samples = sample_rate / 60;
}
