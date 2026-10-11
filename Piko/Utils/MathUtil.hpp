#pragma once

namespace piko::utils
{
    float clamp01( float value );
    float ratio01( float value, float maximum );
    float smoothstep( float value );
    float smoothstep( float edge0, float edge1, float value );
}
