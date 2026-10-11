#include "Utils/EnumUtil.hpp"
#include "Stage3DRenderer.hpp"
#include "Stage3DUtil.hpp"

#include "App/piko.hpp"
#include "FileTypes/CHC/CHC.hpp"
#include "FileTypes/CHC/CHCJson.hpp"
#include "FileTypes/SSQ/SSQJson.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/RenderUtil.hpp"
#include "Utils/MathUtil.hpp"
#include "Utils/StrUtil.hpp"

#include <cstdint>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace piko::modules
{
    filetypes::xg::Matrix Stage3DRenderer::make_model_transform( const filetypes::ssq::ModelEntry &entry ) const
    {
        return filetypes::xg::make_trs( entry.position, entry.rotation, entry.scale );
    }

    bool Stage3DRenderer::make_linked_effect_transform( const filetypes::ssq::ModelEntry &entry,
        float frame,
        filetypes::xg::Matrix &transform ) const
    {
        if( !entry.has_effect_control || entry.effect_control.source_model_name.empty() ||
            entry.effect_control.target_model_name.empty() || !std::isfinite( entry.effect_control.reference_length ) ||
            entry.effect_control.reference_length <= 0.0001f )
        {
            return false;
        }

        const std::string source_name = piko::utils::lowercase_filename( entry.effect_control.source_model_name );
        const std::string target_name = piko::utils::lowercase_filename( entry.effect_control.target_model_name );
        const filetypes::ssq::ModelEntry *source_entry = nullptr;
        const filetypes::ssq::ModelEntry *target_entry = nullptr;

        for( const filetypes::ssq::ModelEntry &candidate : scene_.models )
        {
            const std::string candidate_name = piko::utils::lowercase_filename( candidate.xg_name );

            if( source_entry == nullptr && candidate_name == source_name )
            {
                source_entry = &candidate;
            }

            if( target_entry == nullptr && candidate_name == target_name )
            {
                target_entry = &candidate;
            }

            if( source_entry != nullptr && target_entry != nullptr )
            {
                break;
            }
        }

        if( source_entry == nullptr || target_entry == nullptr )
        {
            return false;
        }

        const filetypes::ssq::ModelEntry source = sample_model_entry( *source_entry, frame );
        const filetypes::ssq::ModelEntry target = sample_model_entry( *target_entry, frame );
        const filetypes::xg::Vec3 one{ 1.0f, 1.0f, 1.0f };
        const filetypes::xg::Matrix source_pose = filetypes::xg::make_trs( source.position, source.rotation, one );
        const filetypes::xg::Matrix target_pose = filetypes::xg::make_trs( target.position, target.rotation, one );

        // sub_1A5C70
        // sub_1A5C80
        const filetypes::xg::Vec3 source_point =
            stage3d::transform_point( source_pose, entry.effect_control.source_offset );

        const filetypes::xg::Vec3 target_point =
            stage3d::transform_point( target_pose, entry.effect_control.target_offset );

        const filetypes::xg::Vec3 delta{
            target_point.x - source_point.x,
            target_point.y - source_point.y,
            target_point.z - source_point.z
        };

        const float distance = std::sqrt( delta.x * delta.x + delta.y * delta.y + delta.z * delta.z );

        if( !std::isfinite( distance ) || distance <= 0.0001f )
        {
            return false;
        }

        const filetypes::xg::Vec3 forward{ delta.x / distance, delta.y / distance, delta.z / distance };
        filetypes::xg::Vec3 right = stage3d::cross_vec3( filetypes::xg::Vec3{ 0.0f, 1.0f, 0.0f }, forward );
        const float right_length_sq = right.x * right.x + right.y * right.y + right.z * right.z;

        if( right_length_sq <= 0.000001f )
        {
            right = stage3d::cross_vec3( filetypes::xg::Vec3{ 1.0f, 0.0f, 0.0f }, forward );
        }

        right = filetypes::xg::normalize( right );
        const filetypes::xg::Vec3 up = filetypes::xg::normalize( stage3d::cross_vec3( forward, right ) );

        // sub_1A5C90
        const float length_scale = distance / entry.effect_control.reference_length;
        transform = filetypes::xg::identity_matrix();

        transform.m[ 0 ] = right.x * entry.scale.x;

        transform.m[ 1 ] = right.y * entry.scale.x;

        transform.m[ 2 ] = right.z * entry.scale.x;

        transform.m[ 4 ] = up.x * entry.scale.y;

        transform.m[ 5 ] = up.y * entry.scale.y;

        transform.m[ 6 ] = up.z * entry.scale.y;

        transform.m[ 8 ] = forward.x * length_scale;

        transform.m[ 9 ] = forward.y * length_scale;

        transform.m[ 10 ] = forward.z * length_scale;

        transform.m[ 12 ] = source_point.x;

        transform.m[ 13 ] = source_point.y;

        transform.m[ 14 ] = source_point.z;
        return true;
    }

    bool Stage3DRenderer::make_snake_linked_transforms( const filetypes::ssq::ModelEntry &entry,
        float frame,
        std::vector<filetypes::xg::Matrix> &chain_transforms,
        filetypes::xg::Matrix &end_transform ) const
    {
        if( entry.model_type != 9u )
        {
            return false;
        }

        const filetypes::ssq::ModelEntry *parent_entry = nullptr;
        const filetypes::ssq::ModelEntry *end_entry = nullptr;

        for( const filetypes::ssq::ModelEntry &candidate : scene_.models )
        {
            const std::string name = piko::utils::lowercase_filename( candidate.xg_name );

            if( name == "woofa_f.xg" )
            {
                parent_entry = &candidate;
            }
            else if( name == "woofa_b.xg" )
            {
                end_entry = &candidate;
            }
        }

        if( parent_entry == nullptr || end_entry == nullptr )
        {
            return false;
        }

        constexpr std::size_t segment_count = 6u;
        constexpr float segment_spacing = 40.0f;
        const filetypes::ssq::ModelEntry parent = sample_model_entry( *parent_entry, frame );
        const filetypes::ssq::ModelEntry end = sample_model_entry( *end_entry, frame );
        const filetypes::xg::Matrix parent_pose =
            filetypes::xg::make_trs( parent.position, parent.rotation, filetypes::xg::Vec3{ 1.0f, 1.0f, 1.0f } );

        chain_transforms.clear();
        chain_transforms.reserve( segment_count );

        for( std::size_t segment = 0; segment < segment_count; ++segment )
        {
            const filetypes::xg::Vec3 position = stage3d::transform_point( parent_pose,
                filetypes::xg::Vec3{ 0.0f, 0.0f, -segment_spacing * static_cast<float>( segment ) } );

            chain_transforms.push_back( filetypes::xg::make_trs( position, parent.rotation, entry.scale ) );
        }

        const filetypes::xg::Vec3 end_position = stage3d::transform_point( parent_pose,
            filetypes::xg::Vec3{ 0.0f, 0.0f, -segment_spacing * static_cast<float>( segment_count ) } );

        end_transform =
            filetypes::xg::make_trs( end_position, filetypes::xg::Quat{ 0.0f, 0.0f, 0.0f, 1.0f }, end.scale );

        return true;
    }

    filetypes::ssq::ModelEntry Stage3DRenderer::sample_model_entry( const filetypes::ssq::ModelEntry &entry,
        float frame ) const
    {
        filetypes::ssq::ModelEntry sampled = entry;
        sampled.position = stage3d::sample_vec3_keys( entry.position_keys, entry.position, frame );

        sampled.raw_rotation = stage3d::sample_quat_keys( entry.rotation_keys, entry.raw_rotation, frame, true );

        sampled.rotation = convert_model_rotation( sampled.raw_rotation );

        sampled.scale = stage3d::sample_scale_keys( entry.scale_keys, entry.scale, frame );

        if( const filetypes::ssq::ModelAnimKey *anim = active_model_anim_key( entry, frame ) )
        {
            sampled.visible = !anim->hidden;
        }

        return sampled;
    }

    const filetypes::ssq::ModelAnimKey *Stage3DRenderer::active_model_anim_key( const filetypes::ssq::ModelEntry &entry,
        float frame ) const
    {
        if( entry.anim_keys.empty() )
        {
            return nullptr;
        }

        // sub_1B9B70
        const float period = entry.anim_keys.back().frame;

        if( period != 0.0f && std::isfinite( period ) )
        {
            if( frame == 0.0f )
            {
                frame = 0.0f;
            }
            else if( static_cast<std::int32_t>( frame / period ) != 0 )
            {
                const float multiple = static_cast<float>( static_cast<std::int32_t>( frame / period ) ) * period;
                frame = multiple == frame ? 0.0f : std::abs( frame - multiple );
            }
        }

        if( entry.anim_keys.size() == 1 || frame <= entry.anim_keys.front().frame )
        {
            return &entry.anim_keys.front();
        }

        for( std::size_t i = 0; i + 1 < entry.anim_keys.size(); ++i )
        {
            if( frame < entry.anim_keys[ i + 1 ].frame )
            {
                return &entry.anim_keys[ i ];
            }
        }

        return &entry.anim_keys.back();
    }

    bool Stage3DRenderer::angle_in_controllable_range( float angle, float min_angle, float max_angle ) const
    {
        if( !std::isfinite( angle ) || !std::isfinite( min_angle ) || !std::isfinite( max_angle ) )
        {
            return false;
        }

        constexpr float two_pi = 6.28318530717958647692f;
        return ( angle >= min_angle && angle <= max_angle ) ||
               ( angle - two_pi >= min_angle && angle - two_pi <= max_angle ) ||
               ( angle + two_pi >= min_angle && angle + two_pi <= max_angle );
    }

    bool Stage3DRenderer::controllable_matches_action_with( const filetypes::ssq::Controllable &controllable,
        std::uint32_t flags,
        std::uint32_t descriptor,
        bool has_aim_angle,
        float aim_angle_radians,
        bool *event_match,
        bool *descriptor_match,
        bool *angle_match ) const
    {

        // sub_1A6DC0
        const bool event_ok = ( ( flags & controllable.event_flag ) != 0u ) || controllable.event_flag == flags;
        const bool descriptor_ok = controllable.descriptor == descriptor;
        const bool angle_ok =
            angle_in_controllable_range( aim_angle_radians, controllable.min_angle, controllable.max_angle );

        (void)has_aim_angle;

        if( event_match != nullptr )
        {
            *event_match = event_ok;
        }

        if( descriptor_match != nullptr )
        {
            *descriptor_match = descriptor_ok;
        }

        if( angle_match != nullptr )
        {
            *angle_match = angle_ok;
        }

        return event_ok && descriptor_ok && angle_ok;
    }

    bool Stage3DRenderer::controllable_matches_action( const filetypes::ssq::Controllable &controllable,
        bool *event_match,
        bool *descriptor_match,
        bool *angle_match ) const
    {
        return controllable_matches_action_with( controllable,
            effective_action_event_flags(),
            action_input_.descriptor,
            action_input_.has_aim_angle,
            action_input_.aim_angle_radians,
            event_match,
            descriptor_match,
            angle_match );
    }

    bool Stage3DRenderer::controllables_share_directional_playback( const filetypes::ssq::Controllable &current,
        const filetypes::ssq::Controllable &next ) const
    {
        return next.event_flag == current.event_flag && next.descriptor == current.descriptor &&
               next.loop_playback == current.loop_playback;
    }

    const Stage3DRenderer::ActionAnimationInput &Stage3DRenderer::action_input_for_model_type(
        std::uint32_t model_type ) const
    {
        if( model_type >= 1u && model_type <= actor_action_inputs_.size() )
        {
            return actor_action_inputs_[ model_type - 1u ];
        }

        return action_input_;
    }

    void Stage3DRenderer::update_player_pose_aim( const ActionAnimationInput &input )
    {
        if( !input.has_aim_angle )
        {
            player_pose_aim_active_ = false;
            return;
        }

        const float target = stage3d::normalize_angle_radians( input.aim_angle_radians );

        if( !player_pose_aim_active_ )
        {
            player_pose_aim_angle_radians_ = target;

            player_pose_aim_active_ = true;
            return;
        }

        constexpr float two_pi = 6.28318530717958647692f;
        constexpr float response_seconds = 0.060f;
        constexpr float max_speed_radians_per_second = 12.56637061435917295384f;

        const float step = static_cast<float>( frame_delta_seconds_ );
        const float angle_delta = std::remainder( target - player_pose_aim_angle_radians_, two_pi );
        const float factor = response_seconds > 0.0f ? 1.0f - std::exp( -step / response_seconds ) : 1.0f;
        const float max_step = max_speed_radians_per_second * step;
        const float angle_step = std::clamp( angle_delta * factor, -max_step, max_step );

        player_pose_aim_angle_radians_ =
            stage3d::normalize_angle_radians( player_pose_aim_angle_radians_ + angle_step );
    }

    bool Stage3DRenderer::controllable_is_looping_directional_aim(
        const filetypes::ssq::Controllable &controllable ) const
    {
        return controllable.event_flag == 0x200u && controllable.descriptor == 0u && controllable.loop_playback;
    }

    void Stage3DRenderer::select_xgm_animation( const DrawModel &model,
        ActionAnimationState &state,
        std::uint32_t animation_index,
        bool use_current_time_origin,
        bool loop,
        bool transition,
        bool start_guard,
        float current_time,
        float transition_duration,
        float model_animation_speed,
        bool force_restart )
    {
        if( model.animations.empty() || animation_index >= model.animations.size() )
        {
            return;
        }

        // PSP sub_896B010 seeks model-speed GMO motions from the song phase,
        // including one-shot actions. Fixed-rate motions advance by their authored
        // FPS (sub_8948928 -> sub_89B1D74), regardless of the loop flag.
        const bool portable_song_clock = portable_visuals_ && state.portable_stage_driven_model &&
                                         model.animations[ animation_index ].speed_mode == 2u;

        if( force_restart || animation_index != state.selected_animation_index ||
            portable_song_clock != state.portable_song_clock )
        {
            const float previous_frame = state.evaluated_frame;
            const std::uint32_t previous_animation_index = state.selected_animation_index;
            const bool pose_transition = transition && state.initialized && transition_duration > 0.0f;

            if( pose_transition )
            {
                state.transition_source_origin_frame = state.animation_origin_frame;

                state.transition_source_reciprocal_interval = state.reciprocal_interval;

                state.transition_source_duration = state.duration;

                state.transition_source_start_keyframe = state.start_keyframe;

                state.transition_source_base_speed = state.base_speed;

                state.transition_source_speed_mode = state.speed_mode;

                state.transition_source_portable_song_clock = state.portable_song_clock;

                state.transition_source_forward = state.forward;

                state.transition_source_loop = state.loop;
            }

            const runtime::model::AnimationTiming &animation = model.animations[ animation_index ];
            const float interval =
                std::abs( animation.keyframe_interval ) > 0.0001f ? animation.keyframe_interval : 1.0f;

            state.portable_song_clock = portable_song_clock;

            if( portable_song_clock )
            {
                const float tempo = std::isfinite( stage_tempo_ ) && stage_tempo_ > 0.0f ? stage_tempo_ : 120.0f;
                const float source_frames_per_stage_frame = tempo * 8.0f / 1800.0f;
                state.reciprocal_interval = 1.0f;

                state.duration = std::max( 1.0f, animation.playback_length - 1.0f );

                state.stage_duration = source_frames_per_stage_frame > 0.0001f
                                           ? state.duration / source_frames_per_stage_frame
                                           : state.duration;

                state.base_speed = 0.0f;

                state.speed_mode = 4u;
            }
            else if( animation.speed_mode == 2u || animation.speed_mode == 3u )
            {
                state.reciprocal_interval = 2.0f / interval;

                state.duration = std::max( 0.0f, animation.playback_length );

                state.stage_duration =
                    state.reciprocal_interval > 0.0001f ? state.duration / state.reciprocal_interval : state.duration;

                state.base_speed = 0.0f;

                state.speed_mode = 1u;
            }
            else
            {
                state.reciprocal_interval = 1.0f / interval;

                state.duration = std::max( 0.0f, animation.playback_length / interval );

                state.stage_duration = state.duration;

                state.base_speed = 4.0f / ( 900.0f * interval );

                state.speed_mode = animation.speed_mode;
            }

            state.start_keyframe = animation.start_keyframe;

            state.selected_animation_index = animation_index;

            state.start_guard = start_guard;

            state.transition_pending = false;

            state.transition_active = pose_transition;

            state.transition_duration = transition_duration;

            state.transition_start_frame = pose_transition ? current_time : 0.0f;

            state.transition_from_animation_index =
                previous_animation_index < model.animations.size() ? previous_animation_index : animation_index;

            state.transition_to_animation_index = animation_index;

            state.transition_from_frame = static_cast<float>( static_cast<std::int32_t>( previous_frame ) );
        }

        state.loop = loop;

        state.animation_origin_frame =
            use_current_time_origin ? ( state.portable_song_clock ? portable_song_animation_frame_ : current_time )
                                    : 0.0f;

        state.active = true;

        state.initialized = true;
    }

    float Stage3DRenderer::evaluate_xgm_animation( ActionAnimationState &state, float global_frame ) const
    {
        if( !state.initialized )
        {
            return 0.0f;
        }

        const float previous_evaluated_frame = state.evaluated_frame;

        auto retail_remainder = []( float value, float duration )
        {
            if( duration == 0.0f || value == 0.0f )
            {
                return 0.0f;
            }

            const std::int32_t quotient = static_cast<std::int32_t>( value / duration );

            if( quotient == 0 )
            {
                return value;
            }

            const float multiple = static_cast<float>( quotient ) * duration;

            if( multiple == value )
            {
                return 0.0f;
            }

            return multiple > value ? multiple - value : value - multiple;
        };

        auto evaluate_playback = [ & ]( bool portable_song_clock,
                                     float animation_origin_frame,
                                     std::uint32_t speed_mode,
                                     float base_speed,
                                     float reciprocal_interval,
                                     float start_keyframe,
                                     float duration,
                                     bool forward,
                                     bool loop,
                                     bool *active )
        {
            float elapsed = 0.0f;

            if( portable_song_clock )
            {
                const float tempo = std::isfinite( stage_tempo_ ) && stage_tempo_ > 0.0f ? stage_tempo_ : 120.0f;
                const float projected_phase =
                    portable_song_animation_frame_ + ( global_frame - stage_frame_ ) * tempo * 8.0f / 1800.0f;

                elapsed = projected_phase - animation_origin_frame;
            }
            else if( speed_mode != 0 )
            {
                elapsed = global_frame - animation_origin_frame;
            }
            else
            {
                elapsed = stage_tempo_ * base_speed * ( global_frame - animation_origin_frame );
            }

            float result = start_keyframe;

            if( forward )
            {
                if( loop )
                {
                    const float value =
                        speed_mode != 0 ? elapsed * reciprocal_interval : elapsed + 8.0f * reciprocal_interval;

                    result += retail_remainder( value, duration );
                }
                else
                {
                    if( speed_mode != 0 )
                    {
                        elapsed *= reciprocal_interval;
                    }

                    if( elapsed > duration )
                    {
                        elapsed = duration;

                        if( active != nullptr )
                        {
                            *active = false;
                        }
                    }

                    result += elapsed;
                }
            }
            else if( loop )
            {
                if( speed_mode != 0 )
                {
                    elapsed = retail_remainder( elapsed * reciprocal_interval, duration );

                    result += duration - elapsed;
                }
                else
                {
                    elapsed = retail_remainder( elapsed + 8.0f * reciprocal_interval, duration );

                    result += duration - elapsed;
                }
            }
            else
            {
                if( speed_mode != 0 )
                {
                    elapsed *= reciprocal_interval;
                }

                float value = duration - elapsed;

                if( value <= 0.0f )
                {
                    value = 0.0f;

                    if( active != nullptr )
                    {
                        *active = false;
                    }
                }

                result += value;
            }

            return result;
        };

        float result = evaluate_playback( state.portable_song_clock,
            state.animation_origin_frame,
            state.speed_mode,
            state.base_speed,
            state.reciprocal_interval,
            state.start_keyframe,
            state.duration,
            state.forward,
            state.loop,
            &state.active );

        if( state.start_guard && !state.transition_pending )
        {
            const float local = result - state.start_keyframe;

            if( local > 0.5f || local > state.duration - 0.5f )
            {
                result = state.start_keyframe;
            }

            state.start_guard = false;
        }

        if( state.transition_pending )
        {
            // sub_102AA0
            const float integer_frame = static_cast<float>( static_cast<std::int32_t>( previous_evaluated_frame ) );
            const float fraction = previous_evaluated_frame - integer_frame;

            if( fraction >= 0.1f && fraction <= 0.89999998f )
            {
                state.evaluated_frame = previous_evaluated_frame + 0.1f;
                return state.evaluated_frame;
            }

            if( fraction > 0.89999998f )
            {
                state.transition_from_frame += 1.0f;
            }

            state.transition_pending = false;

            state.transition_active = true;

            state.transition_start_frame = global_frame;

            if( state.animation_origin_frame != 0.0f )
            {
                state.animation_origin_frame =
                    state.portable_song_clock ? portable_song_animation_frame_ : global_frame;
            }

            // sub_102AA0
            ActionAnimationState target_state = state;
            target_state.transition_active = false;

            target_state.transition_pending = false;

            state.transition_to_frame =
                evaluate_xgm_animation( target_state, global_frame + state.transition_duration );

            state.start_guard = false;
        }

        if( state.transition_active )
        {
            const float elapsed_transition = global_frame - state.transition_start_frame;
            state.transition_from_frame = evaluate_playback( state.transition_source_portable_song_clock,
                state.transition_source_origin_frame,
                state.transition_source_speed_mode,
                state.transition_source_base_speed,
                state.transition_source_reciprocal_interval,
                state.transition_source_start_keyframe,
                state.transition_source_duration,
                state.transition_source_forward,
                state.transition_source_loop,
                nullptr );

            state.transition_to_frame = result;

            if( elapsed_transition < state.transition_duration )
            {
                state.transition_pose_weight =
                    std::clamp( elapsed_transition / std::max( 0.0001f, state.transition_duration ), 0.0f, 1.0f );
            }
            else
            {
                state.transition_active = false;

                state.transition_pose_weight = 0.0f;
            }
        }

        state.evaluated_frame = result;
        return result;
    }

    void Stage3DRenderer::update_controllable_player( const DrawModel &model,
        const filetypes::ssq::ModelEntry &entry,
        ActionAnimationState &state,
        float global_frame )
    {
        if( entry.controllables.empty() )
        {
            return;
        }

        state.current_controllable = std::min( state.current_controllable, entry.controllables.size() - 1u );
        state.hold_counters.resize( entry.controllables.size(), 0u );

        const ActionAnimationInput &actor_input = action_input_for_model_type( entry.model_type );
        const std::uint64_t strum_serial = actor_input.successful_strum_result_serial;
        const bool fresh_strum_result = strum_serial != 0u && strum_serial != state.last_seen_strum_result_serial;

        if( strum_serial != 0u )
        {
            state.last_seen_strum_result_serial = strum_serial;
        }

        const std::uint32_t effective_flags_initial =
            entry.model_type == 1u ? effective_action_event_flags() : actor_input.event_flags;

        std::uint32_t effective_flags = effective_flags_initial;
        std::uint32_t effective_descriptor = actor_input.descriptor;
        const bool effective_has_aim_angle = actor_input.has_aim_angle;
        const float effective_aim_angle = actor_input.aim_angle_radians;

        if( entry.model_type == 1u && effective_flags == 0x002u )
        {
            effective_flags = effective_has_aim_angle ? 0x200u : 0u;

            effective_descriptor = 0u;
        }

        auto matches_action = [ & ]( const filetypes::ssq::Controllable &controllable,
                                  bool *out_event = nullptr,
                                  bool *out_descriptor = nullptr,
                                  bool *out_angle = nullptr )
        {
            return controllable_matches_action_with( controllable,
                effective_flags,
                effective_descriptor,
                effective_has_aim_angle,
                effective_aim_angle,
                out_event,
                out_descriptor,
                out_angle );
        };

        if( entry.model_type >= 1u && entry.model_type <= 4u && fresh_strum_result )
        {
            for( std::size_t candidate_index = 0; candidate_index < entry.controllables.size(); ++candidate_index )
            {
                const filetypes::ssq::Controllable &candidate = entry.controllables[ candidate_index ];

                if( candidate.loop_playback || candidate.descriptor == 0u )
                {
                    continue;
                }

                bool candidate_event = false;
                bool candidate_descriptor = false;
                bool candidate_angle = false;

                if( !matches_action( candidate, &candidate_event, &candidate_descriptor, &candidate_angle ) )
                {
                    continue;
                }

                state.current_controllable = candidate_index;

                state.hold_counters[ candidate_index ] = 0u;
                state.latched_event_flags.resize( entry.controllables.size(), 0u );
                state.latched_event_flags[ candidate_index ] = effective_flags;
                select_xgm_animation( model,
                    state,
                    candidate.animation_index,
                    candidate.use_current_global_time_origin,
                    candidate.loop_playback,
                    true,
                    true,
                    global_frame,
                    2.0f,
                    entry.xg_animation_speed,
                    true );

                state.forward = candidate.forward_playback;

                state.last_event_flags = effective_flags;

                state.last_descriptor = effective_descriptor;

                state.used_default = false;

                state.used_connection = true;

                state.event_matched = candidate_event;

                state.descriptor_matched = candidate_descriptor;

                state.angle_matched = candidate_angle;

                state.selected_connection_index = candidate_index;
                return;
            }
        }

        const filetypes::ssq::Controllable &current = entry.controllables[ state.current_controllable ];
        std::size_t selected = state.current_controllable;
        bool have_selection = false;
        bool used_default = false;
        bool used_connection = false;
        bool searched_connections = false;
        bool event_match = false;
        bool descriptor_match = false;
        bool angle_match = false;

        auto scan_connections = [ & ]()
        {
            searched_connections = true;

            for( const std::uint32_t raw_connection : current.connections )
            {
                if( raw_connection >= entry.controllables.size() )
                {
                    continue;
                }

                bool candidate_event = false;
                bool candidate_descriptor = false;
                bool candidate_angle = false;

                if( !matches_action( entry.controllables[ raw_connection ],
                        &candidate_event,
                        &candidate_descriptor,
                        &candidate_angle ) )
                {
                    continue;
                }

                selected = raw_connection;

                have_selection = true;

                used_connection = true;

                event_match = candidate_event;

                descriptor_match = candidate_descriptor;

                angle_match = candidate_angle;
                state.latched_event_flags.resize( entry.controllables.size(), 0u );
                state.latched_event_flags[ selected ] = effective_flags;
                break;
            }
        };

        if( current.random_connection && !current.connections.empty() )
        {
            const std::uint32_t candidate =
                current.connections[ static_cast<std::size_t>( std::rand() ) % current.connections.size() ];

            if( candidate < entry.controllables.size() )
            {
                selected = candidate;

                have_selection = true;
            }
        }
        else if( current.loop_playback )
        {
            const std::uint32_t flags = effective_flags;
            state.latched_event_flags.resize( entry.controllables.size(), 0u );
            event_match = ( current.event_flag & flags ) != 0u;

            descriptor_match = current.descriptor == effective_descriptor;
            const float aim_hysteresis =
                current.event_flag == 0x200u && current.descriptor == 0u ? 0.08726646259971647f : 0.0f;

            angle_match = angle_in_controllable_range( effective_aim_angle,
                current.min_angle - aim_hysteresis,
                current.max_angle + aim_hysteresis );

            const bool latched_match = state.latched_event_flags[ state.current_controllable ] == flags;
            const bool continuous_directional_aim = entry.model_type >= 1u && entry.model_type <= 4u &&
                                                    effective_has_aim_angle &&
                                                    controllable_is_looping_directional_aim( current );

            if( !( event_match && descriptor_match && latched_match && ( angle_match || continuous_directional_aim ) ) )
            {
                scan_connections();
            }
        }
        else if( !state.active )
        {
            std::uint32_t &hold_counter = state.hold_counters[ state.current_controllable ];

            if( hold_counter < current.hold_last_frame_count )
            {
                state.active = true;

                state.animation_origin_frame += state.portable_song_clock ? state.duration : state.stage_duration;
                ++hold_counter;
                return;
            }

            hold_counter = 0;
            scan_connections();
        }

        if( searched_connections && !have_selection && effective_has_aim_angle )
        {
            for( std::size_t candidate_index = 0; candidate_index < entry.controllables.size(); ++candidate_index )
            {
                bool candidate_event = false;
                bool candidate_descriptor = false;
                bool candidate_angle = false;

                if( !matches_action( entry.controllables[ candidate_index ],
                        &candidate_event,
                        &candidate_descriptor,
                        &candidate_angle ) )
                {
                    continue;
                }

                selected = candidate_index;

                have_selection = true;

                used_connection = true;

                event_match = candidate_event;

                descriptor_match = candidate_descriptor;

                angle_match = candidate_angle;
                state.latched_event_flags.resize( entry.controllables.size(), 0u );
                state.latched_event_flags[ selected ] = effective_flags;
                break;
            }
        }

        if( searched_connections && !have_selection && current.default_event >= 0 &&
            static_cast<std::size_t>( current.default_event ) < entry.controllables.size() )
        {
            selected = static_cast<std::size_t>( current.default_event );

            have_selection = true;

            used_default = true;
        }

        if( have_selection && selected != state.current_controllable )
        {
            state.current_controllable = selected;
            const filetypes::ssq::Controllable &next = entry.controllables[ selected ];
            const bool preserve_directional_playback = entry.model_type >= 1u && entry.model_type <= 4u &&
                                                       effective_has_aim_angle &&
                                                       controllables_share_directional_playback( current, next );

            if( !preserve_directional_playback &&
                ( next.animation_index != state.selected_animation_index || next.loop_playback != state.loop ) )
            {
                select_xgm_animation( model,
                    state,
                    next.animation_index,
                    next.use_current_global_time_origin,
                    next.loop_playback,
                    true,
                    true,
                    global_frame,
                    entry.model_type == 1u ? 2.0f : 4.0f,
                    entry.xg_animation_speed );

                state.forward = next.forward_playback;
            }
        }

        state.last_event_flags = effective_flags;

        state.last_descriptor = effective_descriptor;

        state.used_default = used_default;

        state.used_connection = used_connection;

        state.event_matched = event_match;

        state.descriptor_matched = descriptor_match;

        state.angle_matched = angle_match;

        state.selected_connection_index = have_selection ? selected : static_cast<std::size_t>( -1 );
    }

    void Stage3DRenderer::apply_directional_pose_blend( const DrawModel &model,
        const filetypes::ssq::ModelEntry &entry,
        const ActionAnimationState &state,
        ResolvedAnimation &resolved ) const
    {
        if( entry.model_type < 1u || entry.model_type > 4u || state.transition_active ||
            state.current_controllable >= entry.controllables.size() ||
            state.selected_animation_index >= model.animations.size() )
        {
            return;
        }

        const ActionAnimationInput &actor_input = action_input_for_model_type( entry.model_type );

        if( !actor_input.has_aim_angle )
        {
            return;
        }

        const filetypes::ssq::Controllable &current = entry.controllables[ state.current_controllable ];

        struct DirectionalCandidate
        {
            float center = 0.0f;
            std::uint32_t animation_index = 0;
        };

        std::vector<DirectionalCandidate> candidates;
        candidates.reserve( entry.controllables.size() );

        for( const filetypes::ssq::Controllable &candidate : entry.controllables )
        {
            if( candidate.event_flag != current.event_flag || candidate.descriptor != current.descriptor ||
                candidate.loop_playback != current.loop_playback ||
                candidate.animation_index >= model.animations.size() )
            {
                continue;
            }

            const float center =
                stage3d::normalize_angle_radians( ( candidate.min_angle + candidate.max_angle ) * 0.5f );

            const bool duplicate = std::any_of( candidates.begin(),
                candidates.end(),
                [ center ]( const DirectionalCandidate &existing )
                {
                    return std::abs( existing.center - center ) < 0.0001f;
                } );
            if( !duplicate )
            {
                candidates.push_back( DirectionalCandidate{ center, candidate.animation_index } );
            }
        }

        if( candidates.size() < 2u )
        {
            return;
        }

        std::sort( candidates.begin(),
            candidates.end(),
            []( const DirectionalCandidate &a, const DirectionalCandidate &b )
            {
                return a.center < b.center;
            } );

        constexpr float two_pi = 6.28318530717958647692f;
        const float aim = entry.model_type == 1u && player_pose_aim_active_
                              ? player_pose_aim_angle_radians_
                              : stage3d::normalize_angle_radians( actor_input.aim_angle_radians );

        auto upper = std::upper_bound( candidates.begin(),
            candidates.end(),
            aim,
            []( float angle, const DirectionalCandidate &candidate )
            {
                return angle < candidate.center;
            } );
        const std::size_t upper_index =
            upper == candidates.end() ? 0u : static_cast<std::size_t>( std::distance( candidates.begin(), upper ) );

        const std::size_t lower_index = upper_index == 0u ? candidates.size() - 1u : upper_index - 1u;
        const DirectionalCandidate &lower = candidates[ lower_index ];
        const DirectionalCandidate &higher = candidates[ upper_index ];
        const float span = std::fmod( higher.center - lower.center + two_pi, two_pi );

        if( span <= 0.0001f )
        {
            return;
        }

        const float offset = std::fmod( aim - lower.center + two_pi, two_pi );
        const float weight = std::clamp( offset / span, 0.0f, 1.0f );

        const auto animation_span = [ & ]( std::uint32_t animation_index )
        {
            const runtime::model::AnimationTiming &animation = model.animations[ animation_index ];

            if( state.portable_song_clock )
            {
                return std::max( 1.0f, animation.playback_length - 1.0f );
            }

            const float interval =
                std::abs( animation.keyframe_interval ) > 0.0001f ? animation.keyframe_interval : 1.0f;

            return animation.speed_mode == 2u || animation.speed_mode == 3u
                       ? std::max( 0.0f, animation.playback_length )
                       : std::max( 0.0f, animation.playback_length / interval );
        };

        float phase =
            state.duration > 0.0001f ? ( state.evaluated_frame - state.start_keyframe ) / state.duration : 0.0f;

        if( state.loop )
        {
            phase -= std::floor( phase );
        }
        else
        {
            phase = std::clamp( phase, 0.0f, 1.0f );
        }

        const auto frame_for = [ & ]( const DirectionalCandidate &candidate )
        {
            return model.animations[ candidate.animation_index ].start_keyframe +
                   phase * animation_span( candidate.animation_index );
        };

        resolved.pose_blend_active = true;

        resolved.pose_blend_source_animation_index = lower.animation_index;

        resolved.pose_blend_target_animation_index = higher.animation_index;

        resolved.pose_blend_source_frame = frame_for( lower );

        resolved.pose_blend_target_frame = frame_for( higher );

        resolved.pose_blend_weight = weight;
    }

    Stage3DRenderer::ResolvedAnimation Stage3DRenderer::resolve_animation_for_instance( const DrawModel &model,
        const filetypes::ssq::ModelEntry &entry,
        std::size_t model_setup_index,
        float frame )
    {
        ResolvedAnimation resolved{};
        const filetypes::ssq::ModelAnimKey *key = active_model_anim_key( entry, frame );

        if( model_setup_index >= action_states_.size() )
        {
            action_states_.resize( model_setup_index + 1u );
        }

        ActionAnimationState &state = action_states_[ model_setup_index ];
        state.portable_stage_driven_model =
            portable_visuals_ && ( !entry.anim_keys.empty() || !entry.controllables.empty() );

        const float global_frame = animation_global_frame();

        if( entry.model_type >= 5u && entry.model_type <= 8u && model_setup_index < effect_states_.size() &&
            effect_states_[ model_setup_index ].active && effect_states_[ model_setup_index ].activation_serial != 0u &&
            effect_states_[ model_setup_index ].activation_serial != state.last_effect_activation_serial &&
            key != nullptr )
        {
            select_xgm_animation( model,
                state,
                key->animation_index,
                true,
                key->loop,
                false,
                true,
                global_frame,
                0.0f,
                entry.xg_animation_speed,
                true );

            state.forward = true;

            state.selected_scheduled_key = static_cast<std::size_t>( key - entry.anim_keys.data() );

            state.last_effect_activation_serial = effect_states_[ model_setup_index ].activation_serial;
        }

        // Reuse the pose between render passes.
        if( state.initialized && std::abs( state.last_global_frame - global_frame ) <= 0.0001f &&
            std::abs( state.last_scene_frame - frame ) <= 0.0001f )
        {
            resolved.animation_index = state.selected_animation_index;

            resolved.evaluated_frame = state.evaluated_frame;

            resolved.pose_blend_active = state.transition_active;

            resolved.pose_blend_source_animation_index = state.transition_from_animation_index;

            resolved.pose_blend_target_animation_index = state.transition_to_animation_index;

            resolved.pose_blend_source_frame = state.transition_from_frame;

            resolved.pose_blend_target_frame = state.transition_to_frame;

            resolved.pose_blend_weight = state.transition_pose_weight;
            apply_directional_pose_blend( model, entry, state, resolved );
            return resolved;
        }

        if( !state.initialized )
        {
            state.current_controllable = 0;
            state.latched_event_flags.assign( entry.controllables.size(), 0u );
            state.hold_counters.assign( entry.controllables.size(), 0u );

            if( !entry.controllables.empty() )
            {
                const filetypes::ssq::Controllable &initial = entry.controllables.front();
                // sub_1A7290
                select_xgm_animation( model,
                    state,
                    initial.animation_index,
                    initial.use_current_global_time_origin,
                    initial.loop_playback,
                    false,
                    true,
                    0.0f,
                    0.0f,
                    entry.xg_animation_speed );

                state.forward = initial.forward_playback;
            }
            else
            {
                select_xgm_animation( model,
                    state,
                    0u,
                    false,
                    true,
                    false,
                    true,
                    0.0f,
                    0.0f,
                    entry.xg_animation_speed );

                state.forward = true;
            }
        }

        const bool state_driven = key != nullptr && key->state_driven;

        if( !entry.controllables.empty() && state_driven != state.previous_state_driven )
        {
            // sub_1A73A0
            state.current_controllable = 0;
            const filetypes::ssq::Controllable &initial = entry.controllables.front();
            select_xgm_animation( model,
                state,
                initial.animation_index,
                initial.use_current_global_time_origin,
                initial.loop_playback,
                false,
                true,
                0.0f,
                0.0f,
                entry.xg_animation_speed );

            state.previous_state_driven = state_driven;
        }

        if( state_driven && !entry.controllables.empty() )
        {
            update_controllable_player( model, entry, state, global_frame );
        }
        else if( key != nullptr )
        {
            const std::size_t key_index = static_cast<std::size_t>( key - entry.anim_keys.data() );

            if( key_index != state.selected_scheduled_key )
            {
                state.selected_scheduled_key = key_index;
                select_xgm_animation( model,
                    state,
                    key->animation_index,
                    key->use_current_player_time_origin,
                    key->loop,
                    key->transition,
                    true,
                    global_frame,
                    4.0f,
                    entry.xg_animation_speed );

                state.forward = true;
            }
        }

        state.last_global_frame = global_frame;

        state.last_scene_frame = frame;

        state.evaluated_frame = evaluate_xgm_animation( state, global_frame );

        resolved.animation_index = state.selected_animation_index;

        resolved.evaluated_frame = state.evaluated_frame;

        resolved.pose_blend_active = state.transition_active;

        resolved.pose_blend_source_animation_index = state.transition_from_animation_index;

        resolved.pose_blend_target_animation_index = state.transition_to_animation_index;

        resolved.pose_blend_source_frame = state.transition_from_frame;

        resolved.pose_blend_target_frame = state.transition_to_frame;

        resolved.pose_blend_weight = state.transition_pose_weight;
        apply_directional_pose_blend( model, entry, state, resolved );
        return resolved;
    }
}
