#include "StageTraceBuilder.hpp"

#include <algorithm>
#include <cmath>

namespace piko::runtime::stage
{
    std::vector<TraceSample> build_trace_samples( const filetypes::chc::Chart &chart,
        const StageTraceBuildOptions &options )
    {
        const std::vector<filetypes::chc::TraceSample> source_samples =
            filetypes::chc::build_trace_samples( chart, options.speed_factor, options.sample_step );

        std::vector<TraceSample> samples;
        samples.reserve( source_samples.size() );

        for( const filetypes::chc::TraceSample &sample : source_samples )
        {
            samples.push_back( TraceSample{
                .chart_sample = sample.chart_sample,
                .x = sample.x,
                .y = sample.y,
                .angle = sample.angle
            } );
        }

        return samples;
    }

    std::vector<TraceSample> build_trace_samples( const filetypes::chc::Chart &chart,
        float speed_factor,
        std::int32_t sample_step )
    {
        return build_trace_samples( chart,
            StageTraceBuildOptions{ .speed_factor = speed_factor, .sample_step = sample_step } );
    }

    TracePosition trace_position_at( const std::vector<TraceSample> &samples, std::int64_t chart_sample )
    {
        if( samples.empty() )
        {
            return {};
        }

        if( chart_sample <= samples.front().chart_sample )
        {
            return TracePosition{ .x = samples.front().x, .y = samples.front().y, .angle = samples.front().angle };
        }

        if( chart_sample >= samples.back().chart_sample )
        {
            return TracePosition{ .x = samples.back().x, .y = samples.back().y, .angle = samples.back().angle };
        }

        const auto upper = std::lower_bound( samples.begin(),
            samples.end(),
            chart_sample,
            []( const TraceSample &sample, std::int64_t value )
            {
                return sample.chart_sample < value;
            } );

        const TraceSample &b = *upper;
        const TraceSample &a = *( upper - 1 );

        const std::int64_t span = b.chart_sample - a.chart_sample;

        if( span <= 0 )
        {
            return TracePosition{ .x = a.x, .y = a.y, .angle = a.angle };
        }

        const float t = static_cast<float>( chart_sample - a.chart_sample ) / static_cast<float>( span );

        return TracePosition{
            .x = std::lerp( a.x, b.x, t ),
            .y = std::lerp( a.y, b.y, t ),
            .angle = std::lerp( a.angle, b.angle, t )
        };
    }
}
