#include "Utils/MathUtil.hpp"

#include <algorithm>

namespace piko::utils
{
    float clamp01( float value )
    {
        return std::clamp( value, 0.0f, 1.0f );
    }

    float ratio01( float value, float maximum )
    {
        if( maximum <= 0.0001f )
        {
            return 0.0f;
        }

        return clamp01( value / maximum );
    }

    float smoothstep( float value )
    {
        value = clamp01( value );
        return value * value * ( 3.0f - 2.0f * value );
    }

    float smoothstep( float edge0, float edge1, float value )
    {
        if( edge0 == edge1 )
        {
            return value < edge0 ? 0.0f : 1.0f;
        }

        const float t = clamp01( ( value - edge0 ) / ( edge1 - edge0 ) );
        return t * t * ( 3.0f - 2.0f * t );
    }
}
