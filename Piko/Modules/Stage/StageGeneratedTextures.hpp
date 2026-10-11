#pragma once

#include "Modules/Stage/StageModuleConstants.hpp"

#include <cstdint>
#include <vector>

namespace piko::modules::stage
{
    using ui::make_generated_note_tube_texture_pixels;
    using ui::make_generated_trace_texture_pixels;

    std::vector<std::uint8_t> make_generated_score_digit_texture_pixels();
}
