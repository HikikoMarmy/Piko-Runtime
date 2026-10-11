#pragma once

#include "Managers/ControllerOrientation.hpp"

#include "FileTypes/CHC/CHC.hpp"
#include "Managers/TextureManager.hpp"
#include "Modules/Stage/StageModuleConstants.hpp"
#include "Modules/Stage/StageRibbonRenderer.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/RenderTypes.hpp"
#include "Runtime/Stage/StageGameplayInterfaceAtlas.hpp"
#include "Runtime/Stage/StageTraceBuilder.hpp"
#include "Runtime/Stage/StageView.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace piko::modules::stage
{
    using GuardIconHandles = std::array<managers::TextureHandle, 4>;
    using GuardIconTextures = std::array<const rendering::Texture *, 4>;

    std::vector<std::string> guard_icon_set_names( const std::filesystem::path &resource_root );
    bool load_guard_icon_set( managers::TextureManager &textures,
        const std::filesystem::path &resource_root,
        const std::string &set_name,
        const std::string &texture_prefix,
        GuardIconHandles &handles );

    void unload_guard_icon_set( managers::TextureManager &textures, GuardIconHandles &handles );
    GuardIconTextures guard_icon_textures( const managers::TextureManager &textures, const GuardIconHandles &handles );

    rendering::Vec2 guard_direction( std::uint32_t button );

    float guard_pixels_per_sample( const runtime::stage::StageGameplayView &view,
        float gameplay_speed_factor,
        std::uint32_t button );

    float guard_visual_start_distance_for_speed( float pixels_per_sample );

    const char *guard_button_text( std::uint32_t button );

    bool chart_has_active_phrase_body( const filetypes::chc::Chart &chart, std::int64_t current_chart_sample );

    bool guard_rails_should_draw( const filetypes::chc::Chart &chart,
        const std::vector<runtime::stage::TraceSample> &trace_samples,
        std::int64_t current_chart_sample );

    void draw_guard_section_rails( rendering::Renderer &renderer,
        const runtime::stage::StageGameplayView &view,
        const rendering::Rect &area,
        float alpha_scale = 1.0f,
        float rail_scale = 1.0f,
        rendering::Color rail_color = rendering::Color{ 0.005f, 0.035f, 0.16f, 0.30f } );

    rendering::Color guard_button_color( std::uint32_t button, float alpha );

    const rendering::Rect &itfc_guard_rect_for_button( std::uint32_t button );

    void draw_guard_icon( rendering::Renderer &renderer,
        const rendering::Texture *itfc_texture,
        const rendering::Vec2 &position,
        std::uint32_t button,
        float alpha,
        float scale,
        float brightness = 1.0f,
        const GuardIconTextures *custom_icons = nullptr );

    struct GuardHitVisualOverride
    {
        std::int64_t start_sample = 0;
        std::uint32_t button = 0;
        std::int64_t hit_sample = 0;
    };

    void draw_guard_fragments( rendering::Renderer &renderer,
        const filetypes::chc::Chart &chart,
        std::int64_t current_chart_sample,
        const runtime::stage::StageGameplayView &view,
        float gameplay_speed_factor,
        const rendering::Texture *itfc_texture,
        const std::vector<GuardHitVisualOverride> *hit_overrides = nullptr,
        bool mirror = false,
        float note_scale = 1.0f,
        float hit_pop_scale = 1.0f,
        float hit_fade_scale = 1.0f,
        const GuardIconTextures *custom_icons = nullptr,
        managers::ControllerOrientation controller_orientation = managers::ControllerOrientation::TypeA );
}
