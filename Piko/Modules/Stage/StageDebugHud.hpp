#pragma once

#include "FileTypes/CHC/CHC.hpp"
#include "Modules/Stage/StageModuleConstants.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/RenderTypes.hpp"
#include "Runtime/Stage/StageConditionResolver.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace piko::modules::stage
{
    std::string format_count_line( const filetypes::chc::Song &song );

    std::string section_line( std::size_t index, const filetypes::chc::SongSection &section );

    std::string result_name( runtime::stage::StageConditionResult result );

    std::string condition_line( const filetypes::chc::Condition &condition,
        const runtime::stage::StageConditionState &state );

    void draw_labeled_bar( rendering::Renderer &renderer,
        const char *label,
        float x,
        float y,
        float width,
        float value,
        const rendering::Color &fill );

    std::string transition_text( const runtime::stage::StageSectionTransition &transition );

    std::string join_sections( const std::vector<std::size_t> &sections );
}
