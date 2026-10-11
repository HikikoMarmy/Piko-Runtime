#pragma once

#include "FileTypes/CHC/CHC.hpp"

#include <cstdint>
#include <vector>

namespace piko::runtime::stage
{
    struct TraceSample
    {
        std::int64_t chart_sample = 0;
        float x = 0.0f;
        float y = 0.0f;
        float angle = 0.0f;
    };

    struct TracePosition
    {
        float x = 0.0f;
        float y = 0.0f;
        float angle = 0.0f;
    };

    struct StageTraceBuildOptions
    {
        float speed_factor = 240.0f;
        std::int32_t sample_step = 720;
    };

    [[nodiscard]] std::vector<TraceSample> build_trace_samples( const filetypes::chc::Chart &chart,
        const StageTraceBuildOptions &options );

    [[nodiscard]] std::vector<TraceSample> build_trace_samples( const filetypes::chc::Chart &chart,
        float speed_factor,
        std::int32_t sample_step = 720 );

    [[nodiscard]] TracePosition trace_position_at( const std::vector<TraceSample> &samples, std::int64_t chart_sample );
}
