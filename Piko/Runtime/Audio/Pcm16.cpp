#include "Runtime/Audio/Pcm16.hpp"

namespace piko::runtime::audio
{
    std::int64_t Pcm16::frame_count() const
    {
        if( channels <= 0 )
        {
            return 0;
        }

        return static_cast<std::int64_t>( interleaved.size() / static_cast<std::size_t>( channels ) );
    }
}
