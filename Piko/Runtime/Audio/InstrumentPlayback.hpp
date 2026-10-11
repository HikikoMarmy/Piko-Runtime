#pragma once

#include <cstdint>

namespace piko::runtime::audio
{
    enum class InstrumentPlaybackMode : std::int32_t
    {
        Original,
        TraceEnd,
        Count
    };
}
