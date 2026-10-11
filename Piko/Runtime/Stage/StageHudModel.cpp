#include "StageHudModel.hpp"

#include <cstdint>
#include <algorithm>

namespace piko::runtime::stage
{
    StageHudModel build_stage_hud_model( const StageRuntime &runtime )
    {
        StageHudModel model{};
        model.score = runtime.score();

        model.phase = runtime.current_action_mode();

        const StageSideArray &sides = runtime.sides();

        for( std::size_t index = 0; index < model.sides.size(); ++index )
        {
            const StageSideState &source = sides[ index ];
            StageHudSideModel &target = model.sides[ index ];
            target.active = source.active;

            target.local = source.local;

            target.enemy = source.role == StageSideRole::Enemy;

            target.health_ratio = std::clamp( source.health_ratio(), 0.0f, 1.0f );

            target.energy_ratio = std::clamp( source.energy_ratio(), 0.0f, 1.0f );

            target.pending_energy_delta = source.pending_energy_delta;

            target.pending_gain_ticks = source.pending_gain_ticks;

            target.pending_loss_ticks = source.pending_loss_ticks;
        }

        const StageJudgementDebugState &judgement = runtime.judgement_debug_state();

        if( !judgement.center_event.empty() )
        {
            if( judgement.center_event == "MISS" )
            {
                model.judgement = StageHudJudgement::Miss;
            }
            else if( judgement.center_event == "OK" )
            {
                model.judgement = StageHudJudgement::Ok;
            }
            else if( judgement.center_event == "GOOD" )
            {
                model.judgement = StageHudJudgement::Good;
            }
            else if( judgement.center_event == "GREAT" )
            {
                model.judgement = StageHudJudgement::Great;
            }

            model.judgement_age_samples = runtime.state().section_sample - judgement.center_event_sample;
        }

        return model;
    }

    rendering::Rect retail_score_digit_source( std::int32_t digit )
    {
        const std::int32_t clamped = std::clamp( digit, 0, 9 );
        return rendering::Rect{ 293.0f + static_cast<float>( clamped ) * 22.0f, 1.0f, 21.0f, 22.0f };
    }

    void update_phase_announcement( PhaseAnnouncementState &state, StageActionMode phase, float delta_seconds )
    {
        const bool battle_switch = ( phase == StageActionMode::Attack && state.phase == StageActionMode::Guard ) ||
                                   ( phase == StageActionMode::Guard && state.phase == StageActionMode::Attack );

        const bool final_switch = ( phase == StageActionMode::Harmony && state.phase == StageActionMode::End ) ||
                                  ( phase == StageActionMode::End && state.phase == StageActionMode::Harmony );

        const bool active = phase_announcement_animation( state.seconds ).visible;

        if( phase != StageActionMode::Intro && phase != StageActionMode::Unknown && phase != state.phase &&
            !battle_switch && !final_switch && !active )
        {
            state.phase = phase;

            state.seconds = 0.0f;
        }

        state.seconds += std::max( 0.0f, delta_seconds );
    }

    std::int32_t phase_announcement_texture_index( StageActionMode phase )
    {
        // Announcement slots follow the CHC phase value.
        switch( phase )
        {
            case StageActionMode::Charge:
                return 0;
            case StageActionMode::Attack:
                return 1;
            case StageActionMode::Guard:
                return 2;
            case StageActionMode::Harmony:
                return 3;
            default:
                return -1;
        }
    }

    PhaseAnnouncementAnimation phase_announcement_animation( float seconds )
    {
        enum class Motion
        {
            SlideIn,
            Hold,
            SlideOut
        };

        if( seconds < 0.0f )
        {
            return {};
        }

        PhaseAnnouncementAnimation result{ 100.0f, 0.0f, true };
        Motion motion = Motion::SlideIn;
        float progress = 1.0f;

        const auto ticks = static_cast<std::int32_t>( std::clamp( seconds * 60.0f + 0.0001f, 0.0f, 81.0f ) );

        for( std::int32_t tick = 0; tick < ticks; ++tick )
        {
            if( motion == Motion::SlideIn )
            {
                if( progress > 0.0f )
                {
                    progress -= 0.1f;

                    result.offset_x = 100.0f * progress;

                    result.alpha = 1.0f - progress;
                    continue;
                }

                result.offset_x = 0.0f;

                result.alpha = 1.0f;

                progress = 60.0f;

                motion = Motion::Hold;
            }

            if( motion == Motion::Hold )
            {
                if( progress > 0.0f )
                {
                    progress -= 1.0f;
                    continue;
                }

                progress = 0.0f;

                motion = Motion::SlideOut;
            }

            if( progress < 0.9999989867210388f )
            {
                progress += 0.1f;

                result.offset_x = -100.0f * progress;

                result.alpha = 1.0f - progress;
            }
            else
            {
                return {};
            }
        }

        return result;
    }

    PhaseAnimation retail_phase_animation( float seconds )
    {
        enum class Motion
        {
            Slide,
            Expand,
            Settle,
            Ready
        };

        PhaseAnimation result{ 0.1f, 2.0f, -20.0f, 0.0f };
        Motion motion = Motion::Slide;
        float progress = 2.0f;
        float velocity = 0.1f;
        float acceleration = 0.1f;

        const auto ticks = static_cast<std::int32_t>( std::clamp( seconds * 60.0f, 0.0f, 60.0f ) );

        for( std::int32_t tick = 0; tick < ticks; ++tick )
        {
            if( motion == Motion::Slide )
            {
                progress -= velocity;

                velocity += acceleration;

                if( progress >= 0.0f )
                {
                    result.alpha = ( 2.0f - progress ) * 0.5f;

                    result.offset_y = -progress * 10.0f;
                    continue;
                }

                motion = Motion::Expand;

                progress = 0.0f;

                velocity = 0.4f;

                acceleration = 0.02f;

                result.alpha = 1.0f;

                result.offset_y = 0.0f;
            }

            if( motion == Motion::Expand )
            {
                progress += velocity;

                if( velocity > 0.01f )
                {
                    velocity -= acceleration;
                }

                if( progress <= 1.0f )
                {
                    result.width_scale = 2.0f * progress;

                    result.height_scale = 1.0f - progress + 2.0f / 27.0f;
                    continue;
                }

                motion = Motion::Settle;

                progress = 0.0f;

                velocity = 0.1f;

                acceleration = 0.1f;
            }

            if( motion == Motion::Settle )
            {
                progress += velocity;

                velocity += acceleration;

                if( progress <= 1.0f )
                {
                    result.width_scale = 2.0f - progress;

                    result.height_scale = progress;
                    continue;
                }

                result = {};

                motion = Motion::Ready;
            }
        }

        return result;
    }

    rendering::Rect retail_phase_source( StageActionMode phase )
    {
        std::int32_t index = 0;

        switch( phase )
        {
            case StageActionMode::Intro:
                index = 0;
                break;
            case StageActionMode::Charge:
                index = 1;
                break;
            case StageActionMode::Attack:
                index = 2;
                break;
            case StageActionMode::Guard:
                index = 3;
                break;
            case StageActionMode::Harmony:
                index = 4;
                break;
            case StageActionMode::End:
                index = 5;
                break;
            case StageActionMode::Unknown:
                index = 0;
                break;
        }

        return rendering::Rect{ 391.0f, 23.0f + static_cast<float>( index ) * 27.0f, 121.0f, 27.0f };
    }

    float retail_energy_warning_alpha( std::int64_t elapsed_ticks )
    {
        // US sub_1AAC50: ten 60-Hz ticks up, then ten down, by 0.1 per tick.
        if( elapsed_ticks <= 0 )
        {
            return 0.0f;
        }

        const std::int64_t phase = elapsed_ticks % 20;
        return 0.1f * static_cast<float>( phase <= 10 ? phase : 20 - phase );
    }

    StageHudJudgementAnimation retail_judgement_animation( std::int64_t age_samples, std::int64_t samples_per_tick )
    {
        StageHudJudgementAnimation animation{};

        if( age_samples < 0 || samples_per_tick <= 0 )
        {
            return animation;
        }

        animation.visible = true;

        std::int32_t state = 2;
        float factor = 0.0f;
        float acceleration = 0.1f;
        float speed = 0.1f;
        const std::int64_t ticks = age_samples / samples_per_tick;

        for( std::int64_t tick = 0; tick < ticks && animation.visible; ++tick )
        {
            bool advance_state = true;
            while( advance_state && animation.visible )
            {
                advance_state = false;

                switch( state )
                {
                    case 2:
                        factor += speed;

                        speed += acceleration;

                        if( factor <= 1.0f )
                        {
                            animation.width_scale = factor;
                        }
                        else
                        {
                            animation.width_scale = 1.0f;

                            state = 3;

                            factor = 0.5f;

                            speed = 0.1f;

                            advance_state = true;
                        }
                        break;
                    case 3:
                        factor += speed;

                        speed += acceleration;

                        if( factor <= 1.2f )
                        {
                            animation.height_scale = factor;
                        }
                        else
                        {
                            animation.height_scale = 1.2f;

                            state = 4;

                            factor = 2.0f;

                            speed = 0.1f;

                            advance_state = true;
                        }
                        break;
                    case 4:
                        factor -= speed;

                        speed += acceleration;

                        if( factor >= 1.0f )
                        {
                            animation.height_scale = factor;
                        }
                        else
                        {
                            animation.height_scale = 1.0f;

                            state = 5;

                            factor = 1.0f;

                            speed = 0.1f;

                            advance_state = true;
                        }
                        break;
                    case 5:
                        factor -= speed;

                        if( factor < 0.0f )
                        {
                            state = 6;

                            factor = 0.8f;
                        }
                        break;
                    case 6:
                        factor -= 0.1f * speed;

                        speed += acceleration;

                        animation.alpha = factor;

                        if( factor < 0.0f )
                        {
                            animation.alpha = 0.0f;

                            animation.visible = false;

                            state = 1;
                        }
                        break;
                    default:
                        animation.visible = false;
                        break;
                }
            }
        }

        animation.width_scale *= 1.3f;

        animation.height_scale *= 1.3f;

        animation.alpha = std::clamp( animation.alpha, 0.0f, 1.0f );
        return animation;
    }

    const char *stage_hud_judgement_text( StageHudJudgement judgement )
    {
        switch( judgement )
        {
            case StageHudJudgement::Miss:
                return "MISS";
            case StageHudJudgement::Ok:
                return "OK";
            case StageHudJudgement::Good:
                return "GOOD";
            case StageHudJudgement::Great:
                return "GREAT";
            case StageHudJudgement::None:
                return "";
        }

        return "";
    }
}
