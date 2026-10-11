#pragma once

#include "Rendering/RenderTypes.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace piko::ui
{
    constexpr rendering::Rect generated_tube_body_source{ 8.0f, 72.0f, 256.0f, 128.0f };
    constexpr rendering::Rect generated_tube_cap_source{ 280.0f, 72.0f, 128.0f, 128.0f };
    constexpr rendering::Rect generated_tube_note_source{ 424.0f, 8.0f, 256.0f, 256.0f };
    constexpr rendering::Rect generated_trace_line_source{ 0.0f, 0.0f, 64.0f, 24.0f };

    constexpr std::int32_t generated_tube_texture_width = 688;
    constexpr std::int32_t generated_tube_texture_height = 272;
    constexpr std::int32_t generated_trace_texture_width = 64;
    constexpr std::int32_t generated_trace_texture_height = 24;

    struct BattleImage
    {
        std::int32_t width = 0;
        std::int32_t height = 0;
        std::vector<std::uint8_t> pixels;
    };

    constexpr float note_texture_canvas_height = 28.0f;

    struct NoteTextureRegions
    {
        rendering::Rect body = generated_tube_body_source;
        rendering::Rect cap = generated_tube_cap_source;
        rendering::Rect note = generated_tube_note_source;
        float body_height = note_texture_canvas_height;
        float note_height = note_texture_canvas_height;
    };

    struct NoteTextureAtlas
    {
        BattleImage image;
        NoteTextureRegions regions;
    };

    std::vector<std::uint8_t> make_generated_note_tube_texture_pixels();

    std::vector<std::uint8_t> make_generated_trace_texture_pixels();

    BattleImage crop_battle_image( const BattleImage &image, const rendering::Rect &source );

    std::array<BattleImage, 3> generated_note_images();

    NoteTextureAtlas pack_note_images( const std::array<BattleImage, 3> &images,
        const std::array<BattleImage, 3> &size_reference );
}
