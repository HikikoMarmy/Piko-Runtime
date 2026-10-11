#pragma once

#include <cstdint>
#include <vector>

namespace piko::runtime::audio
{
    struct Pcm16
    {
        std::int32_t sample_rate = 0;
        std::int32_t channels = 0;
        std::vector<std::int16_t> interleaved;

        [[nodiscard]] std::int64_t frame_count() const;
    };
}
