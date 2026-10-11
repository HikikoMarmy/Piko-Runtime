#include "Runtime/Stage/StageTutorialTimeline.hpp"
#include "Runtime/Stage/StageTutorialData.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace piko::runtime::stage
{
    void StageTutorialTimeline::initialize( TutorialRegion region )
    {
        auto data = make_tutorial_data( region );
        sprites_ = std::move( data.sprites );

        centre_ = std::move( data.centre );

        pointer_ = std::move( data.pointer );

        automatic_ = std::move( data.demonstration );

        meter_display_ = std::move( data.meter_display );

        meter_events_ = std::move( data.meter_events );

        initialized_ = true;
        reset();
    }

    void StageTutorialTimeline::reset()
    {
        sample_ = 0;

        last_tick_ = -1;

        centre_cursor_ = 0;

        meter_cursor_ = 0;

        meter_value_ = 0.0f;
        states_.clear();

        for( const auto &sprite : sprites_ )
        {
            TutorialSpriteState state;
            state.source = sprite.source;

            state.position = sprite.position;

            state.colour = sprite.colour;
            states_.push_back( state );
        }
    }

    bool StageTutorialTimeline::contains( const TutorialInterval &interval, std::int64_t sample )
    {
        return sample >= interval.start && sample <= interval.end;
    }

    bool StageTutorialTimeline::in_intervals( const std::vector<TutorialInterval> &intervals ) const
    {
        return std::any_of( intervals.begin(),
            intervals.end(),
            [ this ]( const auto &interval )
            {
                return contains( interval, sample_ );
            } );
    }

    void StageTutorialTimeline::update( std::int64_t sample )
    {
        sample = std::clamp<std::int64_t>( sample, 0, maximum_sample_ );

        if( sample < sample_ )
        {
            reset();
        }

        const auto target_tick = sample / samples_per_tick_;

        for( auto tick_index = last_tick_ + 1; tick_index <= target_tick; ++tick_index )
        {
            tick( tick_index * samples_per_tick_ );
        }

        last_tick_ = target_tick;

        sample_ = sample;
    }

    void StageTutorialTimeline::tick( std::int64_t sample )
    {
        if( centre_cursor_ < centre_.size() && sample > centre_[ centre_cursor_ ].interval.end )
        {
            ++centre_cursor_;
        }

        while( meter_cursor_ < meter_events_.size() && meter_events_[ meter_cursor_ ].sample <= sample )
        {
            meter_value_ = std::clamp( meter_value_ + meter_events_[ meter_cursor_ ].value, 0.0f, 1.0f );
            ++meter_cursor_;
        }

        for( std::size_t index = 0; index < sprites_.size(); ++index )
        {
            const auto &sprite = sprites_[ index ];
            auto &state = states_[ index ];
            state.visible = contains( sprite.interval, sample );

            if( !state.visible )
            {
                continue;
            }

            if( sprite.kind == TutorialSpriteKind::SpeechBubble )
            {
                if( state.animation_phase == TutorialAnimationPhase::Entering )
                {
                    state.colour.a += bubble_alpha_step_;

                    if( state.colour.a > 1.0f )
                    {
                        state.colour.a = 1.0f;

                        state.animation_phase = TutorialAnimationPhase::Holding;
                    }
                }
                else if( state.animation_phase == TutorialAnimationPhase::Holding &&
                         sample >= sprite.interval.end - transition_lead_ )
                {
                    state.animation_phase = TutorialAnimationPhase::Leaving;
                }
                else if( state.animation_phase == TutorialAnimationPhase::Leaving )
                {
                    state.colour.a = std::max( 0.0f, state.colour.a - bubble_alpha_step_ );
                }
            }
            else if( sprite.kind == TutorialSpriteKind::SlidingSprite )
            {
                if( state.animation_phase == TutorialAnimationPhase::Entering )
                {
                    state.slide_progress -= state.slide_step;

                    state.slide_step += slide_acceleration_;

                    if( state.slide_progress < 0.0f )
                    {
                        state.slide_progress = 0.0f;

                        state.animation_phase = TutorialAnimationPhase::Holding;
                    }
                }
                else if( state.animation_phase == TutorialAnimationPhase::Holding &&
                         sample >= sprite.interval.end - transition_lead_ )
                {
                    state.animation_phase = TutorialAnimationPhase::Leaving;
                }
                else if( state.animation_phase == TutorialAnimationPhase::Leaving )
                {
                    state.slide_progress += state.slide_step;

                    state.slide_step -= slide_acceleration_;

                    if( state.slide_progress > 1.0f )
                    {
                        state.slide_progress = 1.0f;

                        state.animation_phase = TutorialAnimationPhase::Finished;
                    }
                }

                state.position = {
                    std::lerp( sprite.slide_to.x, sprite.slide_from.x, state.slide_progress ),
                    std::lerp( sprite.slide_to.y, sprite.slide_from.y, state.slide_progress )
                };
            }

            if( !sprite.frames.empty() )
            {
                const auto cycle = sprite.frames.back().interval.end;

                const auto local_sample = ( sample - sprite.interval.start ) % std::max<std::int64_t>( cycle, 1 );

                for( const auto &frame : sprite.frames )
                {
                    if( contains( frame.interval, local_sample ) )
                    {
                        state.source = frame.source;
                        break;
                    }
                }
            }

            for( const auto &motion : sprite.motion )
            {
                const auto local_sample = sample - sprite.interval.start;

                if( contains( motion.interval, local_sample ) )
                {
                    const float progress =
                        static_cast<float>( local_sample - motion.interval.start ) /
                        static_cast<float>( std::max<std::int64_t>( motion.interval.end - motion.interval.start, 1 ) );

                    state.position = {
                        std::lerp( motion.from.x, motion.to.x, progress ),
                        std::lerp( motion.from.y, motion.to.y, progress )
                    };
                    break;
                }
            }
        }

        // Move the bubble tails with their parents.
        for( std::size_t index = 0; index < sprites_.size(); ++index )
        {
            const auto &sprite = sprites_[ index ];

            if( sprite.linked_sprite < 0 )
            {
                continue;
            }

            auto &linked = states_[ static_cast<std::size_t>( sprite.linked_sprite ) ];
            linked.visible = states_[ index ].visible;

            if( sprite.kind == TutorialSpriteKind::SpeechBubble )
            {
                linked.colour.a = states_[ index ].colour.a;
            }
            else if( sprite.kind == TutorialSpriteKind::SlidingSprite )
            {
                linked.position = states_[ index ].position;
            }
        }
    }

    bool StageTutorialTimeline::valid() const
    {
        return initialized_;
    }

    bool StageTutorialTimeline::automatic_notes() const
    {
        return initialized_ && in_intervals( automatic_ );
    }

    bool StageTutorialTimeline::pointer_active() const
    {
        return initialized_ && in_intervals( pointer_ );
    }

    bool StageTutorialTimeline::example_meter_visible() const
    {
        return initialized_ && in_intervals( meter_display_ );
    }

    float StageTutorialTimeline::example_meter_value() const
    {
        return meter_value_;
    }

    const std::vector<TutorialSprite> &StageTutorialTimeline::sprites() const
    {
        return sprites_;
    }

    const std::vector<TutorialSpriteState> &StageTutorialTimeline::states() const
    {
        return states_;
    }

    std::int64_t StageTutorialTimeline::sample() const
    {
        return sample_;
    }

    rendering::Vec2 StageTutorialTimeline::centre_offset() const
    {
        return initialized_ && centre_cursor_ < centre_.size() ? centre_[ centre_cursor_ ].offset : rendering::Vec2{};
    }

    bool StageTutorialTimeline::aim_visible() const
    {
        const auto offset = centre_offset();
        const float x = reference_width_ * 0.5f + offset.x;
        const float y = reference_height_ * 0.5f + offset.y;
        return x >= 0.0f && x < reference_width_ && y >= 0.0f && y < reference_height_;
    }
}
