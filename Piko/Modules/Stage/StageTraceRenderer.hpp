#pragma once

#include "Modules/Stage/StageModuleConstants.hpp"
#include "Modules/Stage/StageRibbonRenderer.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/RenderTypes.hpp"
#include "Runtime/Stage/StageTraceBuilder.hpp"
#include "Runtime/Stage/StageView.hpp"

#include <vector>

namespace piko::modules::stage
{
    float trace_tangent_degrees_at( const runtime::stage::TracePosition &current_position,
        const runtime::stage::StageGameplayView &view );

    void draw_trace_render_points( rendering::Renderer &renderer,
        const std::vector<runtime::stage::TraceRenderPoint> &points,
        float width,
        const rendering::Color &base_color,
        float alpha_scale = 1.0f,
        const runtime::stage::StageGameplayView *presentation_view = nullptr );

    struct TraceRibbonSample
    {
        rendering::Vec2 point{};
        float alpha = 1.0f;
    };

    std::vector<TraceRibbonSample> densify_trace_ribbon_samples(
        const std::vector<runtime::stage::TraceRenderPoint> &points,
        float max_segment_pixels );

    bool draw_generated_trace_render_points( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const std::vector<runtime::stage::TraceRenderPoint> &points,
        float height,
        const rendering::Color &base_color,
        float alpha_scale,
        rendering::Vec2 presentation_center = rendering::Vec2{ 0.0f, 0.0f },
        float presentation_scale_x = 1.0f,
        float presentation_scale_y = 1.0f );

}
