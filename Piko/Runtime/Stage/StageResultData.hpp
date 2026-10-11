#include "Utils/EnumUtil.hpp"
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace piko::runtime::stage
{
    enum class StageResultGrade : std::size_t
    {
        Great = 0,
        Good = 1,
        Ok = 2,
        Miss = 3,
        Count = 4
    };

    struct StageResultData
    {
        std::uint32_t score = 0;
        std::array<std::uint32_t, utils::enum_index( StageResultGrade::Count )> judgement_counts{};
        float final_energy = 0.0f;
        bool won = false;
    };
}
