#include "StageAudioController.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <sstream>

namespace piko::runtime::stage
{
    bool StageAudioController::initialize( managers::AudioManager &audio,
        const filetypes::chc::Song &song,
        const std::filesystem::path &audio_bank_path,
        const std::filesystem::path &fallback_audio_bank_path,
        StageRuntime &runtime,
        bool stereo_phrases )
    {
        shutdown( audio );

        song_ = &song;

        failed_ = false;

        paused_ = false;

        result_cue_played_ = false;

        stream_global_sample_base_ = runtime.state().global_sample - runtime.state().section_sample;

        queued_until_global_sample_ = stream_global_sample_base_;
        last_queued_section_index_.reset();
        last_audio_event_serial_ = runtime.audio_event_state().serial;

        player_stem_target_gain_ = runtime.chart_style() == gamedata::ChartStyle::GitarooMan ? 0.0f : 1.0f;

        phrase_stem_mode_ =
            stereo_phrases ? managers::AudioPhraseStemMode::StereoCh2Ch4 : managers::AudioPhraseStemMode::MonoCh2Center;

        win_result_cue_name_.clear();
        lose_result_cue_name_.clear();
        result_cue_status_.clear();
        status_line_.clear();
        section_durations_.clear();

        if( audio_bank_path.empty() )
        {
            failed_ = true;

            status_line_ = "Stage audio: no converted audio bank path";
            return false;
        }

        if( !audio.load_audio_bank( audio_bank_path ) )
        {
            failed_ = true;

            status_line_ = std::string( "Stage audio load failed: " ) + audio.last_error();
            return false;
        }

        if( !fallback_audio_bank_path.empty() &&
            fallback_audio_bank_path.lexically_normal() != audio_bank_path.lexically_normal() )
        {
            audio.merge_audio_bank( fallback_audio_bank_path );
        }

        section_durations_.reserve( song.sections.size() );

        for( std::size_t section_index = 0; section_index < song.sections.size(); ++section_index )
        {
            std::int64_t duration = static_cast<std::int64_t>( song.sections[ section_index ].duration_samples );

            if( runtime.section_action_mode( section_index ) == StageActionMode::End )
            {
                const std::string cue_name = cue_name_for_section( section_index );
                const std::int64_t cue_frames = audio.subsong_frame_count( cue_name );

                if( cue_frames > 0 )
                {
                    duration = cue_frames;
                }
                else if( const filetypes::chc::Cue *cue = cue_for_section( section_index ); cue != nullptr )
                {
                    const double visual_frames =
                        std::abs( static_cast<double>( cue->last_frame - cue->first_frame ) ) + 1.0;

                    const std::int64_t visual_duration = static_cast<std::int64_t>(
                        std::llround( visual_frames * static_cast<double>( samples_per_second ) / 30.0 ) );

                    duration = std::max( duration, visual_duration );
                }

                runtime.set_section_duration_override( section_index, duration );
            }

            section_durations_.push_back( duration );
        }

        channel_ = audio.claim_channel( "Stage audio" );

        if( !channel_ )
        {
            failed_ = true;

            status_line_ = "Stage audio: could not claim channel";
            return false;
        }

        audio.set_channel_phrase_stem_mode( channel_, phrase_stem_mode_ );
        audio.set_channel_stage_mix( channel_, song_->audio_mix, runtime.chart_style(), stereo_phrases );
        reset_instrument_controls( audio, runtime );
        result_channel_ = audio.claim_channel( "Stage result" );

        if( result_channel_ )
        {
            prepare_result_cues( audio );
        }
        else
        {
            result_cue_status_ = "channel unavailable";
        }

        initialized_ = start_current_section( audio, runtime );
        request_preload_candidates( audio, runtime );
        update_status( audio, runtime );
        return initialized_;
    }

    void StageAudioController::shutdown( managers::AudioManager &audio )
    {
        if( result_channel_ )
        {
            audio.release_channel( result_channel_ );
        }

        if( channel_ )
        {
            audio.release_channel( channel_ );
        }

        channel_ = {};

        result_channel_ = {};

        song_ = nullptr;

        initialized_ = false;

        failed_ = false;

        paused_ = false;

        result_cue_played_ = false;

        stream_global_sample_base_ = 0;

        queued_until_global_sample_ = 0;
        last_queued_section_index_.reset();
        last_audio_event_serial_ = 0;

        player_stem_target_gain_ = 1.0f;

        phrase_stem_mode_ = managers::AudioPhraseStemMode::StereoCh2Ch4;
        win_result_cue_name_.clear();
        lose_result_cue_name_.clear();
        result_cue_status_.clear();
        status_line_.clear();
        section_durations_.clear();
    }

    void StageAudioController::update( managers::AudioManager &audio, StageRuntime &runtime )
    {
        if( !initialized_ || failed_ || paused_ || !runtime.has_song() || runtime.state().song_finished )
        {
            return;
        }

        const std::size_t previous_section_index = runtime.state().section_index;
        const std::int64_t global_sample = audio_global_sample( audio );
        runtime.update_from_global_sample( global_sample );

        if( !runtime.state().song_finished && runtime.state().section_index != previous_section_index &&
            ( !last_queued_section_index_ || *last_queued_section_index_ != runtime.state().section_index ) )
        {
            audio.stop_channel( channel_ );
            stream_global_sample_base_ = runtime.state().global_sample - runtime.state().section_sample;

            queued_until_global_sample_ = stream_global_sample_base_;
            last_queued_section_index_.reset();
            initialized_ = start_current_section( audio, runtime );

            if( !initialized_ )
            {
                return;
            }
        }

        if( !runtime.state().song_finished && runtime.current_action_mode() == StageActionMode::End &&
            !audio.is_channel_playing( channel_ ) )
        {
            runtime.update_from_global_sample(
                runtime.state().global_sample + runtime.current_section_remaining_samples() );
        }

        request_preload_candidates( audio, runtime );

        if( runtime.state().song_finished )
        {
            update_status( audio, runtime );
            return;
        }

        const std::int64_t remaining = runtime.current_section_remaining_samples();

        const StageTransitionPlan &plan = runtime.transition_plan();

        StageSectionTransition route{};
        bool route_committed_for_audio = false;

        if( plan.committed )
        {
            route = plan.committed_transition;

            route_committed_for_audio = true;
        }
        else if( remaining <= forced_commit_queue_lead_samples )
        {
            route = runtime.commit_current_transition();

            route_committed_for_audio = true;
        }

        if( route_committed_for_audio && !route.song_finished && remaining <= queue_ready_lead_samples )
        {
            const bool allow_sync_fallback = remaining <= forced_commit_queue_lead_samples;
            queue_section_if_ready( audio, route.next_section_index, allow_sync_fallback );
        }

        update_status( audio, runtime );
    }

    void StageAudioController::set_paused( managers::AudioManager &audio, bool paused )
    {
        paused_ = paused;

        if( !initialized_ || failed_ )
        {
            return;
        }

        if( paused_ )
        {
            audio.pause();
        }
        else
        {
            audio.resume();
        }
    }

    void StageAudioController::restart_from_runtime( managers::AudioManager &audio, const StageRuntime &runtime )
    {
        if( !song_ || !runtime.has_song() )
        {
            return;
        }

        if( channel_ )
        {
            audio.stop_channel( channel_ );
        }

        stream_global_sample_base_ = runtime.state().global_sample - runtime.state().section_sample;

        queued_until_global_sample_ = stream_global_sample_base_;
        last_queued_section_index_.reset();
        last_audio_event_serial_ = runtime.audio_event_state().serial;

        reset_instrument_controls( audio, runtime );

        result_cue_played_ = false;

        if( result_channel_ )
        {
            audio.stop_channel( result_channel_ );
        }

        audio.set_channel_phrase_stem_mode( channel_, phrase_stem_mode_ );

        initialized_ = start_current_section( audio, runtime );
        request_preload_candidates( audio, runtime );
        update_status( audio, runtime );
    }

    bool StageAudioController::play_result_cue( managers::AudioManager &audio, bool won )
    {
        if( result_cue_played_ )
        {
            return true;
        }

        if( !result_channel_ )
        {
            result_cue_status_ = "channel unavailable";
            return false;
        }

        const std::string &cue_name = won ? win_result_cue_name_ : lose_result_cue_name_;

        if( cue_name.empty() )
        {
            result_cue_status_ = won ? "WIN unavailable" : "LOSE unavailable";
            return false;
        }

        if( !audio.play_subsong( result_channel_, cue_name, false ) )
        {
            result_cue_status_ = "playback failed: " + audio.last_error();
            return false;
        }

        result_cue_played_ = true;

        result_cue_status_ = "playing " + cue_name;
        return true;
    }

    void StageAudioController::stop_song( managers::AudioManager &audio )
    {
        if( channel_ )
        {
            audio.stop_channel( channel_ );
        }
    }

    bool StageAudioController::active() const
    {
        return initialized_ && !failed_ && channel_.valid();
    }

    bool StageAudioController::using_audio_clock() const
    {
        return active() && !paused_;
    }

    std::int64_t StageAudioController::audio_global_sample( const managers::AudioManager &audio ) const
    {
        if( !channel_ )
        {
            return stream_global_sample_base_;
        }

        return stream_global_sample_base_ + audio.channel_sample( channel_ );
    }

    std::int64_t StageAudioController::queued_until_global_sample() const
    {
        return queued_until_global_sample_;
    }

    const std::string &StageAudioController::status_line() const
    {
        return status_line_;
    }

    managers::AudioChannelMixDebug StageAudioController::audio_mix_debug( const managers::AudioManager &audio ) const
    {
        return audio.channel_mix_debug( channel_ );
    }

    const filetypes::chc::Cue *StageAudioController::cue_for_section( std::size_t section_index ) const
    {
        if( song_ == nullptr || section_index >= song_->cues.size() )
        {
            return nullptr;
        }

        return &song_->cues[ section_index ];
    }

    std::string StageAudioController::cue_name_for_section( std::size_t section_index ) const
    {
        const filetypes::chc::Cue *cue = cue_for_section( section_index );

        if( cue == nullptr )
        {
            return {};
        }

        return cue->audio_section_name.empty() ? cue->fallback_name : cue->audio_section_name;
    }

    std::int64_t StageAudioController::section_duration( std::size_t section_index ) const
    {
        if( section_index >= section_durations_.size() )
        {
            return 0;
        }

        return section_durations_[ section_index ];
    }

    std::int64_t StageAudioController::section_playback_limit( std::size_t section_index ) const
    {
        if( song_ == nullptr || section_index >= song_->sections.size() )
        {
            return 0;
        }

        return section_duration( section_index );
    }

    std::int64_t StageAudioController::current_audio_sample( const managers::AudioManager &audio ) const
    {
        return channel_ ? audio.channel_sample( channel_ ) : 0;
    }

    std::string StageAudioController::resolve_result_cue_name( const managers::AudioManager &audio, bool won ) const
    {
        const std::array<std::string_view, 3> win_names{
            "WIN",
            "seWin02",
            "SEWIN02"
        };

        const std::array<std::string_view, 3> lose_names{
            "LOSE",
            "seLose02",
            "SELOSE02"
        };

        const auto &names = won ? win_names : lose_names;

        for( const std::string_view name : names )
        {
            if( audio.has_subsong( name ) )
            {
                return std::string( name );
            }
        }

        return {};
    }

    bool StageAudioController::prepare_result_cues( managers::AudioManager &audio )
    {
        win_result_cue_name_ = resolve_result_cue_name( audio, true );

        lose_result_cue_name_ = resolve_result_cue_name( audio, false );

        bool ready = true;

        if( win_result_cue_name_.empty() )
        {
            result_cue_status_ = "WIN unavailable";

            ready = false;
        }
        else if( !audio.preload_subsong( win_result_cue_name_ ) )
        {
            result_cue_status_ = "WIN preload failed: " + audio.last_error();

            ready = false;
        }

        if( lose_result_cue_name_.empty() )
        {
            if( result_cue_status_.empty() )
            {
                result_cue_status_ = "LOSE unavailable";
            }

            ready = false;
        }
        else if( !audio.preload_subsong( lose_result_cue_name_ ) )
        {
            if( result_cue_status_.empty() )
            {
                result_cue_status_ = "LOSE preload failed: " + audio.last_error();
            }

            ready = false;
        }

        if( ready )
        {
            result_cue_status_ = "ready";
        }

        return ready;
    }

    bool StageAudioController::start_current_section( managers::AudioManager &audio, const StageRuntime &runtime )
    {
        if( song_ == nullptr || !channel_ || !runtime.has_song() )
        {
            return false;
        }

        const std::size_t section_index = runtime.state().section_index;
        const std::string cue_name = cue_name_for_section( section_index );

        if( cue_name.empty() )
        {
            failed_ = true;

            status_line_ = "Stage audio: missing cue for section " + std::to_string( section_index );
            return false;
        }

        if( !audio.preload_subsong( cue_name ) )
        {
            failed_ = true;

            status_line_ = "Stage audio preload failed: " + audio.last_error();
            return false;
        }

        if( !audio.play_subsong( channel_, cue_name, false, section_playback_limit( section_index ) ) )
        {
            failed_ = true;

            status_line_ = "Stage audio play failed: " + audio.last_error();
            return false;
        }

        publish_instrument_volumes( audio );

        last_queued_section_index_ = section_index;

        queued_until_global_sample_ = stream_global_sample_base_ + section_duration( section_index );

        return true;
    }

    bool StageAudioController::queue_section_if_ready( managers::AudioManager &audio,
        std::size_t section_index,
        bool allow_sync_fallback )
    {
        if( song_ == nullptr || !channel_ )
        {
            return false;
        }

        if( section_index >= song_->sections.size() )
        {
            return false;
        }

        if( last_queued_section_index_ && *last_queued_section_index_ == section_index )
        {
            const std::int64_t playback_global_sample = audio_global_sample( audio );

            if( queued_until_global_sample_ > playback_global_sample + queue_ready_lead_samples )
            {
                return true;
            }
        }

        const std::string cue_name = cue_name_for_section( section_index );

        if( cue_name.empty() )
        {
            return false;
        }

        if( !audio.is_subsong_preloaded( cue_name ) )
        {
            request_preload_for_section( audio, section_index );

            if( !allow_sync_fallback )
            {
                return false;
            }

            if( !audio.preload_subsong( cue_name ) )
            {
                return false;
            }
        }

        if( !audio.queue_subsong( channel_, cue_name, section_playback_limit( section_index ) ) )
        {
            return false;
        }

        last_queued_section_index_ = section_index;

        queued_until_global_sample_ += section_duration( section_index );

        std::cout << "Queued stage IMC section " << section_index << " (" << cue_name << ") until global sample "
                  << queued_until_global_sample_ << '\n';

        return true;
    }

    void StageAudioController::request_preload_for_section( managers::AudioManager &audio, std::size_t section_index )
    {
        const std::string cue_name = cue_name_for_section( section_index );

        if( cue_name.empty() )
        {
            return;
        }

        if( audio.is_subsong_preloaded( cue_name ) || audio.is_subsong_preload_pending( cue_name ) )
        {
            return;
        }

        audio.preload_subsong_async( cue_name );
    }

    void StageAudioController::request_preload_candidates( managers::AudioManager &audio, const StageRuntime &runtime )
    {
        if( song_ == nullptr || !runtime.has_song() || runtime.state().song_finished )
        {
            return;
        }

        request_preload_for_section( audio, runtime.state().section_index );

        for( const std::size_t next_section : runtime.collect_possible_next_sections() )
        {
            request_preload_for_section( audio, next_section );
        }

        const StageTransitionPlan &plan = runtime.transition_plan();
        const StageSectionTransition route = plan.committed ? plan.committed_transition : plan.predicted_transition;

        if( !route.song_finished )
        {
            request_preload_for_section( audio, route.next_section_index );
        }
    }

    void StageAudioController::flush_runtime_audio_events( managers::AudioManager &audio, StageRuntime &runtime )
    {
        handle_runtime_audio_events( audio, runtime );
        runtime.discard_audio_events_through( last_audio_event_serial_ );
    }

    std::uint32_t StageAudioController::instrument_voice_mask( const StageRuntime &runtime, std::size_t source ) const
    {
        return runtime.instrument_voice_mask( source,
            phrase_stem_mode_ == managers::AudioPhraseStemMode::StereoCh2Ch4 );
    }

    void StageAudioController::reset_instrument_controls( managers::AudioManager &audio, const StageRuntime &runtime )
    {
        instrument_duet_side_ = runtime.duet_side();

        controlled_voice_mask_ =
            runtime.instrument_controlled_voices( phrase_stem_mode_ == managers::AudioPhraseStemMode::StereoCh2Ch4 );

        const auto mix = runtime::audio::playback_mix( song_->audio_mix,
            runtime.chart_style(),
            phrase_stem_mode_ == managers::AudioPhraseStemMode::StereoCh2Ch4 );

        for( std::size_t index = 0; index < instrument_volumes_.size(); ++index )
        {
            const bool controlled = ( controlled_voice_mask_ & ( 1u << index ) ) != 0;
            std::int32_t maximum = mix.channels[ index ].volume;

            if( runtime.chart_style() == gamedata::ChartStyle::GitarooManLives && runtime.duet_stage() )
            {
                const auto secondary = runtime.duet_side() == StageDuetSide::Partner ? 0u : 2u;

                if( ( runtime.instrument_voice_mask( secondary, true ) & ( 1u << index ) ) != 0 )
                {
                    maximum = static_cast<std::int32_t>(
                        static_cast<float>( maximum ) * runtime.instrument_gain_scale( secondary ) );
                }
            }

            const bool playing = !controlled || runtime.chart_style() == gamedata::ChartStyle::GitarooManLives;
            instrument_volumes_[ index ].reset( runtime.chart_style(), maximum, playing );
        }

        next_instrument_update_sample_ = runtime.state().global_sample;

        last_audio_event_serial_ = runtime.audio_event_state().serial;
        publish_instrument_volumes( audio );
    }

    void StageAudioController::publish_instrument_volumes( managers::AudioManager &audio )
    {
        std::array<std::int32_t, 8> volumes{};

        for( std::size_t index = 0; index < volumes.size(); ++index )
        {
            volumes[ index ] = instrument_volumes_[ index ].volume();
        }

        player_stem_target_gain_ =
            static_cast<float>( volumes[ 2 ] ) / runtime::audio::InstrumentVolume::maximum_volume;

        audio.set_channel_voice_volumes( channel_, volumes );
    }

    void StageAudioController::handle_runtime_audio_events( managers::AudioManager &audio, const StageRuntime &runtime )
    {
        if( !channel_ )
        {
            return;
        }

        if( runtime.duet_side() != instrument_duet_side_ )
        {
            const auto serial = last_audio_event_serial_;
            reset_instrument_controls( audio, runtime );
            last_audio_event_serial_ = serial;
        }

        std::vector<StageAudioEventState> events;

        for( const auto &event : runtime.audio_events() )
        {
            if( event.serial > last_audio_event_serial_ )
            {
                events.push_back( event );
                last_audio_event_serial_ = event.serial;
            }
        }

        std::stable_sort( events.begin(),
            events.end(),
            []( const auto &left, const auto &right )
            {
                return left.global_sample < right.global_sample;
            } );

        const auto step = [ & ]()
        {
            for( auto &volume : instrument_volumes_ )
            {
                volume.update();
            }

            next_instrument_update_sample_ += runtime::audio::InstrumentVolume::samples_per_update;
        };

        const bool portable = runtime.chart_style() == gamedata::ChartStyle::GitarooManLives;
        std::size_t event_index = 0;

        while( event_index < events.size() )
        {
            const auto sample = events[ event_index ].global_sample;

            while( next_instrument_update_sample_ < sample )
            {
                step();
            }

            const bool on_tick = next_instrument_update_sample_ == sample;

            if( portable && on_tick )
            {
                step();
            }

            do
            {
                const auto &event = events[ event_index ];

                const auto mask = controlled_voice_mask_ & instrument_voice_mask( runtime, event.source_player_index );

                for( std::size_t index = 0; index < instrument_volumes_.size(); ++index )
                {
                    if( ( mask & ( 1u << index ) ) == 0 )
                    {
                        continue;
                    }

                    if( event.kind == StageAudioEventKind::InstrumentRecovered )
                    {
                        instrument_volumes_[ index ].recover();
                    }
                    else if( event.kind == StageAudioEventKind::InstrumentMuted )
                    {
                        instrument_volumes_[ index ].fade();
                    }
                }

                ++event_index;
            } while( event_index < events.size() && events[ event_index ].global_sample == sample );

            if( !portable && on_tick )
            {
                step();
            }
        }

        while( next_instrument_update_sample_ <= runtime.state().global_sample )
        {
            step();
        }

        publish_instrument_volumes( audio );
    }

    void StageAudioController::update_status( const managers::AudioManager &audio, const StageRuntime &runtime )
    {
        if( failed_ )
        {
            return;
        }

        std::ostringstream out;
        out << "audio " << ( initialized_ ? "on" : "off" ) << ( paused_ ? " paused" : "" ) << " ch=";

        if( const std::string *name = audio.channel_subsong_name( channel_ ); name != nullptr )
        {
            out << *name;
        }
        else
        {
            out << "-";
        }

        out << " sample=" << audio_global_sample( audio ) << " queued_until=" << queued_until_global_sample_
            << " player_stem=" << player_stem_target_gain_ << " phrase="
            << ( phrase_stem_mode_ == managers::AudioPhraseStemMode::StereoCh2Ch4 ? "stereo2/4" : "mono2" );

        if( runtime.state().song_finished )
        {
            out << " finished";
        }

        if( !result_cue_status_.empty() )
        {
            out << " result=" << result_cue_status_;
        }

        status_line_ = out.str();
    }
}
