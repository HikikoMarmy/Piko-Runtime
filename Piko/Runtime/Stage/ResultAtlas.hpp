#pragma once

#include "Rendering/RenderTypes.hpp"

#include <cstdint>
#include <array>
#include <filesystem>
#include <string>

namespace piko::runtime::stage
{
    struct ResultAtlas
    {
        std::string platform;
        std::string common_texture;
        std::string stage_texture_pattern;
        std::string no_miss_texture;

        rendering::Rect background{};
        rendering::Rect common_base{};
        rendering::Rect stage_emblem{};
        rendering::Rect stage_title{};
        rendering::Rect stat_panel{};
        std::array<rendering::Rect, 4> stat_labels{};
        rendering::Rect punctuation{};
        rendering::Rect percent{};
        rendering::Rect score_label{};
        rendering::Rect digit{};
        float digit_stride = 0.0f;
        std::array<rendering::Rect, 6> ranks{};
        rendering::Rect no_miss{};
        std::array<rendering::Rect, 2> portraits{};
        rendering::Rect win{};
        rendering::Rect lose{};
    };

    [[nodiscard]] ResultAtlas load_result_atlas( const std::filesystem::path &path );
    [[nodiscard]] std::string result_atlas_stage_path( const ResultAtlas &atlas, std::int32_t stage_number );
    [[nodiscard]] const rendering::Rect &result_rank_rect( const ResultAtlas &atlas, std::int32_t rank_index );
}
