#include "FrameStats.hpp"

#include <algorithm>
#include <numeric>

namespace piko::managers
{
    void FrameStats::reset()
    {
        samples_.fill( 0.0 );
        sample_count_ = 0;

        next_sample_ = 0;

        frame_seconds_ = 0.0;

        average_frame_seconds_ = 0.0;

        worst_frame_seconds_ = 0.0;

        profile_ = {};
    }

    void FrameStats::record_frame( double delta_seconds )
    {
        if( delta_seconds < 0.0 )
        {
            delta_seconds = 0.0;
        }

        frame_seconds_ = delta_seconds;

        samples_[ next_sample_ ] = delta_seconds;

        next_sample_ = ( next_sample_ + 1 ) % samples_.size();

        if( sample_count_ < samples_.size() )
        {
            ++sample_count_;
        }

        if( sample_count_ == 0 )
        {
            average_frame_seconds_ = 0.0;

            worst_frame_seconds_ = 0.0;
            return;
        }

        double total = 0.0;
        double worst = 0.0;

        for( std::size_t i = 0; i < sample_count_; ++i )
        {
            total += samples_[ i ];

            worst = std::max( worst, samples_[ i ] );
        }

        average_frame_seconds_ = total / static_cast<double>( sample_count_ );

        worst_frame_seconds_ = worst;
    }

    double FrameStats::frame_seconds() const
    {
        return frame_seconds_;
    }

    double FrameStats::frame_ms() const
    {
        return frame_seconds_ * 1000.0;
    }

    double FrameStats::instantaneous_fps() const
    {
        return frame_seconds_ > 0.0 ? 1.0 / frame_seconds_ : 0.0;
    }

    double FrameStats::average_frame_seconds() const
    {
        return average_frame_seconds_;
    }

    double FrameStats::average_frame_ms() const
    {
        return average_frame_seconds_ * 1000.0;
    }

    double FrameStats::average_fps() const
    {
        return average_frame_seconds_ > 0.0 ? 1.0 / average_frame_seconds_ : 0.0;
    }

    double FrameStats::worst_frame_seconds() const
    {
        return worst_frame_seconds_;
    }

    double FrameStats::worst_frame_ms() const
    {
        return worst_frame_seconds_ * 1000.0;
    }

    std::size_t FrameStats::sample_count() const
    {
        return sample_count_;
    }

    void FrameStats::record_profile( const FrameProfile &profile )
    {
        profile_ = profile;
    }

    const FrameProfile &FrameStats::profile() const
    {
        return profile_;
    }
}
