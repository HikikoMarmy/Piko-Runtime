#pragma once

#include "GameData/GameDataRoot.hpp"
#include "FileTypes/CHC/CHC.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace piko::runtime::audio
{
    struct StereoGain
    {
        float left = 0.0f;
        float right = 0.0f;
    };

    [[nodiscard]] inline filetypes::chc::AudioMix playback_mix( const filetypes::chc::AudioMix &source,
        gamedata::ChartStyle style,
        bool stereo_phrases )
    {
        filetypes::chc::AudioMix mix = source;

        if( source.authored && style == gamedata::ChartStyle::GitarooManLives )
        {
            static constexpr std::array<std::size_t, 8> channels{
                0,
                1,
                2,
                4,
                3,
                5,
                6,
                7
            };

            for( std::size_t index = 0; index < channels.size(); ++index )
            {
                mix.channels[ index ] = source.channels[ channels[ index ] ];
            }
        }
        else if( !source.authored )
        {
            mix.channels[ 0 ].pan = 0;

            mix.channels[ 1 ].pan = 32767;

            mix.channels[ 6 ].pan = 0;

            mix.channels[ 7 ].pan = 32767;

            if( stereo_phrases || style == gamedata::ChartStyle::GitarooManLives )
            {
                mix.channels[ 2 ].pan = 0;

                mix.channels[ 3 ].pan = 0;

                mix.channels[ 4 ].pan = 32767;

                mix.channels[ 5 ].pan = 32767;
            }
            else
            {
                mix.channels[ 4 ].volume = 0;

                mix.channels[ 5 ].volume = 0;
            }
        }

        return mix;
    }

    class InstrumentVolume
    {
    public:
        static constexpr std::int32_t maximum_volume = 32767;
        static constexpr std::int32_t psp_volume_step = 3000;
        static constexpr float ps2_envelope_step = 0.05f;
        static constexpr std::int64_t samples_per_update = 48000 / 60;

        void reset( gamedata::ChartStyle style, std::int32_t volume, bool playing )
        {
            style_ = style;

            maximum_ = std::clamp( volume, 0, maximum_volume );

            current_ = playing ? maximum_ : 0;

            target_ = current_;

            envelope_ = 0.0f;

            armed_ = playing;
        }

        void recover()
        {
            current_ = maximum_;

            target_ = maximum_;

            envelope_ = 0.0f;

            armed_ = true;
        }

        bool fade()
        {
            if( !armed_ || current_ == 0 )
            {
                return false;
            }

            armed_ = false;

            target_ = 0;

            envelope_ = 1.0f;
            return true;
        }

        void update()
        {
            if( style_ == gamedata::ChartStyle::GitarooManLives )
            {
                current_ = std::max( target_, current_ - psp_volume_step );
            }
            else if( envelope_ > 0.0f )
            {
                envelope_ -= ps2_envelope_step;

                current_ = std::max( 0, static_cast<std::int32_t>( std::nearbyint( maximum_ * envelope_ ) ) );
            }
        }

        [[nodiscard]] std::int32_t volume() const
        {
            return current_;
        }

    private:
        gamedata::ChartStyle style_ = gamedata::ChartStyle::GitarooMan;
        std::int32_t maximum_ = 0;
        std::int32_t current_ = 0;
        std::int32_t target_ = 0;
        float envelope_ = 0.0f;
        bool armed_ = false;
    };

    [[nodiscard]] inline StereoGain ps2_channel_gain( std::int32_t volume, std::int32_t pan, bool stereo )
    {
        constexpr std::int32_t full_voice_volume = 16383;
        constexpr std::int32_t full_channel_volume = 32767;
        volume = std::clamp( volume, 0, full_channel_volume );

        pan = std::clamp( pan, 0, full_channel_volume );
        std::int32_t left = 0;
        std::int32_t right = 0;

        if( !stereo )
        {
            const std::int32_t distance = std::abs( pan - full_voice_volume );
            const std::int32_t percent = distance * 100 / full_voice_volume;
            left = std::max( 0, ( volume - percent * 13107 / 100 ) / 2 );

            right = left;
        }
        else if( pan < 16384 )
        {
            left = volume / 2;

            right = std::max( 0, pan * 2 - ( full_channel_volume - volume ) ) / 2;
        }
        else
        {
            left = std::max( 0, volume - pan ) / 2;

            right = volume / 2;
        }

        return { static_cast<float>( left ) / full_voice_volume, static_cast<float>( right ) / full_voice_volume };
    }
}
