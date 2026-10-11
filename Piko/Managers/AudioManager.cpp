#include "AudioManager.hpp"
#include "Runtime/Audio/InstrumentAudio.hpp"
#include "Utils/StrUtil.hpp"

#include "Audio/PspAtracConverter.hpp"
#include "Utils/Json.hpp"
#include "Utils/BinaryUtil.hpp"

#include <cstdint>
#include <cstddef>
#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <fstream>
#include <limits>
#include <cstring>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace piko::managers
{
    bool AudioChannelHandle::valid() const
    {
        return id != 0;
    }

    AudioChannelHandle::operator bool() const
    {
        return valid();
    }

    [[nodiscard]] float AudioManager::clamp_gain( float value )
    {
        return std::clamp( value, 0.0f, 1.0f );
    }

    [[nodiscard]] std::int16_t AudioManager::clamp_s16_from_double( double value )
    {
        value = std::clamp( value, -32768.0, 32767.0 );
        return static_cast<std::int16_t>( std::lrint( value ) );
    }

    [[nodiscard]] std::array<bool, 4> AudioManager::instrument_channels_with_signal( const runtime::audio::Pcm16 &pcm )
    {
        std::array<bool, 4> present{};
        constexpr std::int16_t silence_threshold = 96;
        const std::int64_t frames = pcm.frame_count();

        for( std::int64_t frame = 0; frame < frames; ++frame )
        {
            const std::size_t base = static_cast<std::size_t>( frame ) * static_cast<std::size_t>( pcm.channels );

            for( std::int32_t channel = 2; channel < std::min( pcm.channels, 6 ); ++channel )
            {
                const std::size_t index = static_cast<std::size_t>( channel - 2 );

                if( !present[ index ] &&
                    std::abs( static_cast<std::int32_t>( pcm.interleaved[ base + channel ] ) ) > silence_threshold )
                {
                    present[ index ] = true;
                }
            }

            bool all_present = true;

            for( std::int32_t channel = 2; channel < std::min( pcm.channels, 6 ); ++channel )
            {
                all_present = all_present && present[ static_cast<std::size_t>( channel - 2 ) ];
            }

            if( all_present )
            {
                break;
            }
        }

        return present;
    }

    [[nodiscard]] AudioStemGains AudioManager::clamp_gains( AudioStemGains gains )
    {
        gains.backing = clamp_gain( gains.backing );

        gains.player = clamp_gain( gains.player );

        gains.enemy = clamp_gain( gains.enemy );

        gains.fx = clamp_gain( gains.fx );
        return gains;
    }

    [[nodiscard]] AudioStemGains AudioManager::gain_step_for( const AudioStemGains &from,
        const AudioStemGains &to,
        std::int64_t fade_samples )
    {
        if( fade_samples <= 0 )
        {
            return AudioStemGains{};
        }

        return AudioStemGains{
            ( to.backing - from.backing ) / static_cast<float>( fade_samples ),
            ( to.player - from.player ) / static_cast<float>( fade_samples ),
            ( to.enemy - from.enemy ) / static_cast<float>( fade_samples ),
            ( to.fx - from.fx ) / static_cast<float>( fade_samples )
        };
    }

    [[nodiscard]] float AudioManager::source_channel_gain_for( std::int32_t source_channel,
        std::int32_t source_channel_count,
        AudioPhraseStemMode phrase_stem_mode,
        const AudioStemGains &gains )
    {
        if( source_channel_count <= 2 )
        {
            return gains.backing;
        }

        switch( source_channel )
        {
            case 0:
            case 1:
                return gains.backing;

            case 2:
                return gains.player;

            case 4:
                return phrase_stem_mode == AudioPhraseStemMode::StereoCh2Ch4 ? gains.player : gains.backing;

            case 3:
            case 5:
                return gains.enemy;

            case 6:
            case 7:
                return gains.fx;

            default:
                return gains.backing;
        }
    }

    bool AudioManager::initialize()
    {
        if( const char *base_path = SDL_GetBasePath(); base_path != nullptr )
        {
            application_root_ = std::filesystem::path( base_path ).lexically_normal();
        }
        else
        {
            application_root_ = std::filesystem::current_path();
        }

        return true;
    }

    void AudioManager::shutdown()
    {
        release_all_channels();
        unload_audio_bank();
        stop_async_worker();
        close_audio_device();
    }

    void AudioManager::update()
    {
        consume_async_preload_results();

        for( Channel &channel : channels_storage_ )
        {
            if( channel.claimed )
            {
                update_channel( channel );
            }
        }
    }

    void AudioManager::set_stereo_output( bool stereo )
    {
        stereo_output_ = stereo;
    }

    void AudioManager::set_background_fx_enabled( bool enabled )
    {
        background_fx_enabled_ = enabled;
    }

    void AudioManager::set_instrument_routing( std::int32_t routing )
    {
        instrument_routing_ = std::clamp( routing, 0, 2 );
    }

    bool AudioManager::stereo_output() const
    {
        return stereo_output_;
    }

    bool AudioManager::load_audio_bank( const std::filesystem::path &path )
    {
        stop_all_channels();
        stop_async_worker();

        try
        {
            runtime_audio_cues_.clear();
            decoded_cache_.clear();

            std::error_code ec{};

            if( !std::filesystem::is_directory( path, ec ) )
            {
                throw std::runtime_error( "Converted audio bank directory not found: " + path.string() );
            }

            const std::filesystem::path manifest_path = path / "audio.json";

            if( !std::filesystem::is_regular_file( manifest_path, ec ) )
            {
                throw std::runtime_error( "Converted audio manifest not found: " + manifest_path.string() );
            }

            if( !load_runtime_audio_directory( path, true ) )
            {
                return false;
            }

            audio_bank_loaded_ = true;

            loaded_audio_bank_path_ = path;
            last_error_.clear();

            std::cout << "[Piko Runtime] Audio source: Converted audio " << path
                      << " cues=" << runtime_audio_cues_.size() << '\n';

            return true;
        }
        catch( const std::exception &e )
        {
            last_error_ = e.what();
            std::cerr << "Converted audio bank load failed: " << last_error_ << '\n';

            audio_bank_loaded_ = false;
            runtime_audio_cues_.clear();
            loaded_audio_bank_path_.clear();
            decoded_cache_.clear();
            return false;
        }
    }

    bool AudioManager::merge_audio_bank( const std::filesystem::path &path,
        std::string_view replace_cue,
        std::string_view cue_prefix )
    {
        try
        {
            std::error_code ec{};

            if( !std::filesystem::is_directory( path, ec ) )
            {
                throw std::runtime_error( "Converted audio bank directory not found: " + path.string() );
            }

            const std::filesystem::path manifest_path = path / "audio.json";

            if( !std::filesystem::is_regular_file( manifest_path, ec ) )
            {
                throw std::runtime_error( "Converted audio manifest not found: " + manifest_path.string() );
            }

            const std::size_t previous_count = runtime_audio_cues_.size();

            if( !load_runtime_audio_directory( path, !replace_cue.empty(), replace_cue, cue_prefix ) )
            {
                return false;
            }

            audio_bank_loaded_ = true;
            last_error_.clear();
            std::cout << "[Piko Runtime] Additional audio source: " << path
                      << " cues=" << runtime_audio_cues_.size() - previous_count << '\n';

            return true;
        }
        catch( const std::exception &e )
        {
            last_error_ = e.what();
            std::cerr << "Converted audio bank merge failed: " << last_error_ << '\n';
            return false;
        }
    }

    void AudioManager::unload_audio_bank()
    {
        stop_all_channels();
        stop_async_worker();

        audio_bank_loaded_ = false;
        runtime_audio_cues_.clear();
        loaded_audio_bank_path_.clear();
        decoded_cache_.clear();
    }

    bool AudioManager::preload_subsong( std::string_view name )
    {
        if( !audio_bank_loaded_ )
        {
            return false;
        }

        const std::string key( name );

        if( decoded_cache_.find( key ) != decoded_cache_.end() )
        {
            return true;
        }

        const RuntimeAudioCue *cue = runtime_audio_cue( key );

        if( cue == nullptr )
        {
            last_error_ = "Audio cue not found: " + key;
            std::cerr << last_error_ << '\n';
            return false;
        }

        try
        {
            runtime::audio::Pcm16 pcm = load_runtime_audio( *cue );

            std::cout << "Preloaded runtime audio cue: " << key << " rate=" << pcm.sample_rate
                      << " channels=" << pcm.channels << " samples=" << pcm.frame_count() << '\n';

            decoded_cache_[ key ] = std::move( pcm );
            last_error_.clear();
            return true;
        }
        catch( const std::exception &e )
        {
            last_error_ = "Runtime audio preload failed for " + key + ": " + e.what();
            std::cerr << last_error_ << '\n';
            return false;
        }
    }

    bool AudioManager::preload_subsongs( const std::vector<std::string> &names )
    {
        bool all_ok = true;

        for( const std::string &name : names )
        {
            if( !name.empty() && !preload_subsong( name ) )
            {
                all_ok = false;
            }
        }

        return all_ok;
    }

    bool AudioManager::preload_subsong_async( std::string_view name )
    {
        if( !audio_bank_loaded_ )
        {
            return false;
        }

        const std::string key( name );

        if( key.empty() )
        {
            return false;
        }

        if( decoded_cache_.find( key ) != decoded_cache_.end() )
        {
            return true;
        }

        {
            std::scoped_lock lock( async_mutex_ );

            if( async_pending_names_.find( key ) != async_pending_names_.end() )
            {
                return true;
            }
        }

        const RuntimeAudioCue *cue = runtime_audio_cue( key );

        if( cue == nullptr )
        {
            last_error_ = "Audio cue not found: " + key;
            std::cerr << last_error_ << '\n';
            return false;
        }

        if( !ensure_async_worker_started() )
        {
            return false;
        }

        {
            std::scoped_lock lock( async_mutex_ );

            if( decoded_cache_.find( key ) != decoded_cache_.end() ||
                async_pending_names_.find( key ) != async_pending_names_.end() )
            {
                return true;
            }

            AsyncDecodeJob job{};
            job.name = key;

            job.runtime_audio = *cue;

            async_pending_names_.insert( key );
            async_jobs_.push_back( std::move( job ) );
        }

        async_cv_.notify_one();
        return true;
    }

    bool AudioManager::preload_subsongs_async( const std::vector<std::string> &names )
    {
        bool all_ok = true;

        for( const std::string &name : names )
        {
            if( !name.empty() && !preload_subsong_async( name ) )
            {
                all_ok = false;
            }
        }

        return all_ok;
    }

    bool AudioManager::is_subsong_preloaded( std::string_view name ) const
    {
        return cached_pcm( name ) != nullptr;
    }

    bool AudioManager::is_subsong_preload_pending( std::string_view name ) const
    {
        const std::string key( name );
        std::scoped_lock lock( async_mutex_ );
        return async_pending_names_.find( key ) != async_pending_names_.end();
    }

    AudioChannelHandle AudioManager::claim_channel( std::string_view debug_name, float buffer_seconds )
    {
        for( std::size_t i = 0; i < channels_storage_.size(); ++i )
        {
            Channel &channel = channels_storage_[ i ];

            if( channel.claimed )
            {
                continue;
            }

            channel.claimed = true;

            channel.debug_name = std::string( debug_name );

            channel.stage_mix_enabled = false;

            channel.buffer_seconds = std::clamp( buffer_seconds, 0.1f, 1.0f );
            reset_channel_playback_state( channel );
            channel.phrase_stem_mode = AudioPhraseStemMode::StereoCh2Ch4;

            return AudioChannelHandle{ static_cast<std::uint32_t>( i + 1u ), channel.generation };
        }

        if( channels_storage_.size() >= max_channels )
        {
            last_error_ = "AudioManager has no free channels";
            std::cerr << last_error_ << '\n';
            return {};
        }

        Channel channel{};
        channel.claimed = true;

        channel.debug_name = std::string( debug_name );

        channel.buffer_seconds = std::clamp( buffer_seconds, 0.1f, 1.0f );

        channel.phrase_stem_mode = AudioPhraseStemMode::StereoCh2Ch4;
        channels_storage_.push_back( std::move( channel ) );

        return AudioChannelHandle{
            static_cast<std::uint32_t>( channels_storage_.size() ),
            channels_storage_.back().generation
        };
    }

    void AudioManager::release_channel( AudioChannelHandle handle )
    {
        Channel *channel = channel_for( handle );

        if( channel == nullptr )
        {
            return;
        }

        destroy_stream( *channel );
        channel->stage_mix_enabled = false;

        channel->claimed = false;
        channel->debug_name.clear();
        reset_channel_playback_state( *channel );
        channel->phrase_stem_mode = AudioPhraseStemMode::StereoCh2Ch4;

        ++channel->generation;

        if( channel->generation == 0 )
        {
            channel->generation = 1;
        }
    }

    void AudioManager::release_all_channels()
    {
        for( Channel &channel : channels_storage_ )
        {
            destroy_stream( channel );

            if( channel.claimed )
            {
                ++channel.generation;

                if( channel.generation == 0 )
                {
                    channel.generation = 1;
                }
            }

            channel.claimed = false;
            channel.debug_name.clear();
            reset_channel_playback_state( channel );
            channel.phrase_stem_mode = AudioPhraseStemMode::StereoCh2Ch4;
        }
    }

    bool AudioManager::play_subsong( AudioChannelHandle handle,
        std::string_view name,
        bool loop,
        std::int64_t frame_limit,
        std::int64_t fade_in_samples )
    {
        Channel *channel = channel_for( handle );

        if( channel == nullptr )
        {
            last_error_ = "AudioManager::play_subsong called with an invalid channel";
            std::cerr << last_error_ << '\n';
            return false;
        }

        if( !preload_subsong( name ) )
        {
            return false;
        }

        const runtime::audio::Pcm16 *pcm = cached_pcm( name );

        if( pcm == nullptr )
        {
            return false;
        }

        if( !ensure_device_for_pcm( *pcm ) )
        {
            return false;
        }

        destroy_stream( *channel );

        if( !ensure_stream_for_pcm( *channel, *pcm ) )
        {
            return false;
        }

        channel->subsong_name = std::string( name );

        channel->looping = loop;
        const RuntimeAudioCue *runtime_cue = runtime_audio_cue( name );
        channel->queued_subsongs.push_back( Channel::QueuedSubsong{
            std::string( name ),
            0,
            frame_limit,
            runtime_cue != nullptr ? runtime_cue->loop_start_frame : -1,
            runtime_cue != nullptr ? runtime_cue->loop_end_frame : -1
        } );

        channel->submitted_samples = 0;

        if( fade_in_samples > 0 )
        {
            channel->current_stem_gains = AudioStemGains{ 0.0f, 0.0f, 0.0f, 0.0f };

            channel->target_stem_gains = AudioStemGains{};

            channel->stem_fade_samples_remaining = fade_in_samples;

            channel->stem_gain_step = AudioStemGains{
                1.0f / static_cast<float>( fade_in_samples ),
                1.0f / static_cast<float>( fade_in_samples ),
                1.0f / static_cast<float>( fade_in_samples ),
                1.0f / static_cast<float>( fade_in_samples )
            };
        }

        feed_channel_until_target( *channel );

        if( channel->stage_mix_enabled && !channel->voice_streams_bound )
        {
            destroy_stream( *channel );
            return false;
        }

        resume();
        last_error_.clear();
        return true;
    }

    bool AudioManager::play_pcm( AudioChannelHandle handle,
        std::string_view name,
        runtime::audio::Pcm16 pcm,
        bool loop )
    {
        Channel *channel = channel_for( handle );

        if( channel == nullptr )
        {
            last_error_ = "AudioManager::play_pcm called with an invalid channel";
            return false;
        }

        if( name.empty() || pcm.frame_count() <= 0 || pcm.sample_rate <= 0 || pcm.channels <= 0 )
        {
            last_error_ = "AudioManager::play_pcm called with invalid PCM data";
            return false;
        }

        const std::string cache_name( name );
        decoded_cache_[ cache_name ] = std::move( pcm );
        const runtime::audio::Pcm16 *cached = cached_pcm( cache_name );

        if( cached == nullptr || !ensure_device_for_pcm( *cached ) )
        {
            return false;
        }

        destroy_stream( *channel );
        reset_channel_playback_state( *channel );

        if( !ensure_stream_for_pcm( *channel, *cached ) )
        {
            return false;
        }

        channel->subsong_name = cache_name;

        channel->looping = loop;
        channel->queued_subsongs.push_back( Channel::QueuedSubsong{ cache_name, 0, -1, -1, -1 } );
        channel->submitted_samples = 0;
        feed_channel_until_target( *channel );

        if( channel->stage_mix_enabled && !channel->voice_streams_bound )
        {
            destroy_stream( *channel );
            return false;
        }

        resume();
        last_error_.clear();
        return true;
    }

    bool AudioManager::queue_subsong( AudioChannelHandle handle, std::string_view name, std::int64_t frame_limit )
    {
        Channel *channel = channel_for( handle );

        if( channel == nullptr )
        {
            last_error_ = "AudioManager::queue_subsong called with an invalid channel";
            std::cerr << last_error_ << '\n';
            return false;
        }

        const runtime::audio::Pcm16 *pcm = cached_pcm( name );

        if( pcm == nullptr )
        {
            last_error_ = "Audio cue is not preloaded for queue: " + std::string( name );
            std::cerr << last_error_ << '\n';
            return false;
        }

        if( !ensure_device_for_pcm( *pcm ) )
        {
            return false;
        }

        if( !ensure_stream_for_pcm( *channel, *pcm ) )
        {
            return false;
        }

        if( channel->subsong_name.empty() )
        {
            channel->subsong_name = std::string( name );
        }

        channel->looping = false;
        const RuntimeAudioCue *runtime_cue = runtime_audio_cue( name );
        channel->queued_subsongs.push_back( Channel::QueuedSubsong{
            std::string( name ),
            0,
            frame_limit,
            runtime_cue != nullptr ? runtime_cue->loop_start_frame : -1,
            runtime_cue != nullptr ? runtime_cue->loop_end_frame : -1
        } );

        feed_channel_until_target( *channel );

        resume();
        last_error_.clear();
        return true;
    }

    void AudioManager::stop_channel( AudioChannelHandle handle )
    {
        Channel *channel = channel_for( handle );

        if( channel == nullptr )
        {
            return;
        }

        destroy_stream( *channel );
    }

    void AudioManager::stop_all_channels()
    {
        for( Channel &channel : channels_storage_ )
        {
            destroy_stream( channel );
        }
    }

    void AudioManager::stop_all()
    {
        stop_all_channels();
        close_audio_device();
    }

    void AudioManager::set_channel_stem_gains( AudioChannelHandle handle,
        const AudioStemGains &gains,
        std::int64_t fade_samples )
    {
        Channel *channel = channel_for( handle );

        if( channel == nullptr )
        {
            return;
        }

        channel->target_stem_gains = clamp_gains( gains );

        if( fade_samples <= 0 )
        {
            channel->current_stem_gains = channel->target_stem_gains;

            channel->stem_gain_step = {};

            channel->stem_fade_samples_remaining = 0;
            return;
        }

        channel->stem_fade_samples_remaining = fade_samples;

        channel->stem_gain_step =
            gain_step_for( channel->current_stem_gains, channel->target_stem_gains, fade_samples );
    }

    void AudioManager::set_channel_player_stem_gain( AudioChannelHandle handle, float gain, std::int64_t fade_samples )
    {
        Channel *channel = channel_for( handle );

        if( channel == nullptr )
        {
            return;
        }

        AudioStemGains gains = channel->target_stem_gains;
        gains.player = clamp_gain( gain );
        set_channel_stem_gains( handle, gains, fade_samples );
    }

    void AudioManager::set_channel_stage_mix( AudioChannelHandle handle,
        const filetypes::chc::AudioMix &mix,
        gamedata::ChartStyle style,
        bool stereo_phrases )
    {
        Channel *channel = channel_for( handle );

        if( channel == nullptr )
        {
            return;
        }

        destroy_stream( *channel );
        channel->stage_mix_enabled = true;

        channel->stage_mix_style = style;

        channel->stage_mix = runtime::audio::playback_mix( mix, style, stereo_phrases );

        for( std::size_t index = 0; index < channel->voice_volumes.size(); ++index )
        {
            channel->voice_volumes[ index ] = channel->stage_mix.channels[ index ].volume;
        }
    }

    void AudioManager::set_channel_voice_volumes( AudioChannelHandle handle,
        const std::array<std::int32_t, 8> &volumes )
    {
        if( Channel *channel = channel_for( handle ) )
        {
            channel->voice_volumes = volumes;

            channel->current_stem_gains.player =
                static_cast<float>( std::max( volumes[ 2 ], volumes[ 4 ] ) ) / 32767.0f;

            channel->current_stem_gains.enemy = static_cast<float>( std::max( volumes[ 3 ], volumes[ 5 ] ) ) / 32767.0f;

            channel->target_stem_gains = channel->current_stem_gains;
            update_voice_stream_gains( *channel );
        }
    }

    void AudioManager::update_voice_stream_gains( Channel &channel )
    {
        channel.phrase_layout_stereo_pair = channel.stage_mix_style == gamedata::ChartStyle::GitarooManLives ||
                                            channel.phrase_stem_mode == AudioPhraseStemMode::StereoCh2Ch4;

        for( std::size_t index = 0; index < channel.voice_streams.size(); ++index )
        {
            const auto &voice = channel.voice_streams[ index ];

            if( voice[ 0 ] == nullptr || voice[ 1 ] == nullptr )
            {
                continue;
            }

            runtime::audio::StereoGain gain;

            if( channel.stage_mix_style == gamedata::ChartStyle::GitarooManLives )
            {
                const float volume = std::clamp( channel.voice_volumes[ index ], 0, 32767 ) / 32768.0f;
                const bool left = index == 0 || index == 2 || index == 3 || index == 6;
                gain = left ? runtime::audio::StereoGain{ volume, 0.0f } : runtime::audio::StereoGain{ 0.0f, volume };
            }
            else
            {
                gain = runtime::audio::ps2_channel_gain( channel.voice_volumes[ index ],
                    channel.stage_mix.channels[ index ].pan,
                    true );
            }

            if( index >= 2 && index <= 5 )
            {
                if( instrument_routing_ == 1 )
                {
                    std::swap( gain.left, gain.right );
                }
                else if( instrument_routing_ == 2 )
                {
                    gain.left = gain.right = ( gain.left + gain.right ) * 0.5f;
                }
            }

            if( !stereo_output_ )
            {
                gain.left = gain.right = ( gain.left + gain.right ) * 0.5f;
            }

            if( index >= 6 && !background_fx_enabled_ )
            {
                gain = {};
            }

            SDL_LockAudioStream( voice[ 0 ] );
            SDL_LockAudioStream( voice[ 1 ] );
            SDL_SetAudioStreamGain( voice[ 0 ], gain.left * static_cast<float>( output_mix_gain ) );
            SDL_SetAudioStreamGain( voice[ 1 ], gain.right * static_cast<float>( output_mix_gain ) );
            SDL_UnlockAudioStream( voice[ 1 ] );
            SDL_UnlockAudioStream( voice[ 0 ] );
            channel.last_source_channel_gains[ index ] = std::max( gain.left, gain.right );

            channel.last_source_channel_post_gain_peaks[ index ] =
                channel.last_source_channel_raw_peaks[ index ] * channel.last_source_channel_gains[ index ];
        }
    }

    bool AudioManager::bind_voice_streams( Channel &channel )
    {
        if( channel.voice_streams_bound )
        {
            return true;
        }

        std::array<SDL_AudioStream *, 16> streams{};
        std::size_t count = 0;

        for( const auto &voice : channel.voice_streams )
        {
            for( SDL_AudioStream *stream : voice )
            {
                streams[ count++ ] = stream;
            }
        }

        SDL_PauseAudioDevice( device_ );

        if( !SDL_BindAudioStreams( device_, streams.data(), static_cast<std::int32_t>( count ) ) )
        {
            last_error_ = SDL_GetError();
            destroy_stream( channel );

            if( playing_ )
            {
                SDL_ResumeAudioDevice( device_ );
            }

            return false;
        }

        channel.voice_streams_bound = true;

        if( playing_ )
        {
            SDL_ResumeAudioDevice( device_ );
        }

        return true;
    }

    bool AudioManager::put_voice_pcm_data( Channel &channel,
        const runtime::audio::Pcm16 &pcm,
        std::int64_t source_frame,
        std::int64_t frame_count )
    {
        std::array<std::vector<std::int16_t>, 8> voices;

        for( std::size_t index = 0; index < voices.size(); ++index )
        {
            auto &samples = voices[ index ];
            samples.resize( static_cast<std::size_t>( frame_count ) * 2, 0 );
            float peak = 0.0f;

            for( std::int64_t frame = 0; frame < frame_count; ++frame )
            {
                const auto source = source_frame + frame;
                auto voice = pcm.channels == 1 && index <= 1 ? 0 : static_cast<std::int32_t>( index );

                if( channel.stage_mix_style == gamedata::ChartStyle::GitarooManLives && pcm.channels == 4 )
                {
                    voice = index == 4 ? 3 : ( index == 3 ? 4 : voice );
                }

                if( source < pcm.frame_count() && voice < pcm.channels )
                {
                    const auto sample = pcm.interleaved[ static_cast<std::size_t>( source ) * pcm.channels + voice ];
                    samples[ static_cast<std::size_t>( frame ) * 2 ] = sample;

                    samples[ static_cast<std::size_t>( frame ) * 2 + 1 ] = sample;

                    peak = std::max( peak, std::abs( static_cast<float>( sample ) ) / 32768.0f );
                }
            }

            channel.last_source_channel_raw_peaks[ index ] = peak;

            channel.last_source_channel_post_gain_peaks[ index ] = peak * channel.last_source_channel_gains[ index ];

            channel.last_source_channel_mirrored_from[ index ] = -1;
        }

        channel.last_source_channel_count = std::min( pcm.channels, 8 );

        for( const auto &voice : channel.voice_streams )
        {
            for( SDL_AudioStream *stream : voice )
            {
                SDL_LockAudioStream( stream );
            }
        }

        bool success = true;

        for( std::size_t index = 0; index < voices.size(); ++index )
        {
            for( SDL_AudioStream *stream : channel.voice_streams[ index ] )
            {
                success = SDL_PutAudioStreamData( stream,
                              voices[ index ].data(),
                              static_cast<std::int32_t>( voices[ index ].size() * sizeof( std::int16_t ) ) ) &&
                          success;
            }
        }

        for( auto voice = channel.voice_streams.rbegin(); voice != channel.voice_streams.rend(); ++voice )
        {
            for( auto stream = voice->rbegin(); stream != voice->rend(); ++stream )
            {
                SDL_UnlockAudioStream( *stream );
            }
        }

        if( !success )
        {
            last_error_ = SDL_GetError();
        }

        return success;
    }

    void AudioManager::set_channel_phrase_stem_mode( AudioChannelHandle handle, AudioPhraseStemMode mode )
    {
        Channel *channel = channel_for( handle );

        if( channel == nullptr )
        {
            return;
        }

        channel->phrase_stem_mode = mode;
    }

    AudioStemGains AudioManager::channel_stem_gains( AudioChannelHandle handle ) const
    {
        const Channel *channel = channel_for( handle );
        return channel != nullptr ? channel->current_stem_gains : AudioStemGains{};
    }

    AudioChannelMixDebug AudioManager::channel_mix_debug( AudioChannelHandle handle ) const
    {
        AudioChannelMixDebug debug{};

        const Channel *channel = channel_for( handle );

        if( channel == nullptr )
        {
            return debug;
        }

        debug.valid = true;

        debug.debug_name = channel->debug_name;

        debug.subsong_name = channel->subsong_name;

        debug.source_channel_count = channel->last_source_channel_count;

        debug.phrase_stem_mode = channel->phrase_stem_mode;

        debug.phrase_layout_stereo_pair = channel->phrase_layout_stereo_pair;

        debug.current_gains = channel->current_stem_gains;

        debug.target_gains = channel->target_stem_gains;

        debug.fade_samples_remaining = channel->stem_fade_samples_remaining;

        const std::int32_t debug_channel_count = std::clamp( channel->last_source_channel_count,
            0,
            static_cast<std::int32_t>( audio_debug_source_channel_count ) );

        for( std::int32_t i = 0; i < debug_channel_count; ++i )
        {
            AudioSourceChannelDebug &source = debug.source_channels[ static_cast<std::size_t>( i ) ];
            source.index = i;

            source.gain = channel->last_source_channel_gains[ static_cast<std::size_t>( i ) ];

            source.raw_peak = channel->last_source_channel_raw_peaks[ static_cast<std::size_t>( i ) ];

            source.post_gain_peak = channel->last_source_channel_post_gain_peaks[ static_cast<std::size_t>( i ) ];

            source.mirrored_from_channel = channel->last_source_channel_mirrored_from[ static_cast<std::size_t>( i ) ];
        }

        return debug;
    }

    void AudioManager::pause()
    {
        if( device_ != 0 )
        {
            SDL_PauseAudioDevice( device_ );
        }

        playing_ = false;
    }

    void AudioManager::resume()
    {
        if( device_ != 0 )
        {
            SDL_ResumeAudioDevice( device_ );
        }

        playing_ = true;
    }

    bool AudioManager::has_audio_bank_loaded() const
    {
        return audio_bank_loaded_;
    }

    bool AudioManager::has_subsong( std::string_view name ) const
    {
        return audio_bank_loaded_ && runtime_audio_cue( name ) != nullptr;
    }

    std::int64_t AudioManager::subsong_frame_count( std::string_view name ) const
    {
        if( const runtime::audio::Pcm16 *pcm = cached_pcm( name ); pcm != nullptr )
        {
            return pcm->frame_count();
        }

        const RuntimeAudioCue *cue = runtime_audio_cue( name );
        return cue != nullptr ? cue->frame_count : 0;
    }

    bool AudioManager::is_playing() const
    {
        return playing_;
    }

    std::int32_t AudioManager::sample_rate() const
    {
        return sample_rate_;
    }

    std::int32_t AudioManager::channels() const
    {
        return channels_;
    }

    bool AudioManager::is_channel_claimed( AudioChannelHandle handle ) const
    {
        return channel_for( handle ) != nullptr;
    }

    bool AudioManager::is_channel_playing( AudioChannelHandle handle ) const
    {
        const Channel *channel = channel_for( handle );
        return channel != nullptr && channel->stream != nullptr;
    }

    std::int64_t AudioManager::channel_sample( AudioChannelHandle handle ) const
    {
        const std::int64_t submitted = channel_submitted_samples( handle );
        return std::clamp<std::int64_t>( submitted - queued_channel_samples( handle ), 0, submitted );
    }

    std::int64_t AudioManager::channel_submitted_samples( AudioChannelHandle handle ) const
    {
        const Channel *channel = channel_for( handle );
        return channel != nullptr ? channel->submitted_samples : 0;
    }

    std::int64_t AudioManager::queued_channel_samples( AudioChannelHandle handle ) const
    {
        const Channel *channel = channel_for( handle );

        if( channel == nullptr || channel->stream == nullptr || channels_ <= 0 )
        {
            return 0;
        }

        const std::int32_t queued_bytes = SDL_GetAudioStreamQueued( channel->stream );

        const std::int64_t bytes_per_frame = static_cast<std::int64_t>( channel->stream_source_channels ) *
                                             static_cast<std::int64_t>( sizeof( std::int16_t ) );

        if( bytes_per_frame <= 0 )
        {
            return 0;
        }

        return static_cast<std::int64_t>( queued_bytes ) / bytes_per_frame;
    }

    const std::string *AudioManager::channel_subsong_name( AudioChannelHandle handle ) const
    {
        const Channel *channel = channel_for( handle );
        return channel != nullptr && !channel->subsong_name.empty() ? &channel->subsong_name : nullptr;
    }

    const std::string *AudioManager::channel_debug_name( AudioChannelHandle handle ) const
    {
        const Channel *channel = channel_for( handle );
        return channel != nullptr ? &channel->debug_name : nullptr;
    }

    const std::filesystem::path &AudioManager::loaded_audio_bank_path() const
    {
        return loaded_audio_bank_path_;
    }

    const std::string &AudioManager::last_error() const
    {
        return last_error_;
    }

    void AudioManager::close_audio_device()
    {
        if( device_ != 0 )
        {
            SDL_CloseAudioDevice( device_ );
            device_ = 0;
        }

        sample_rate_ = 0;

        channels_ = 0;

        playing_ = false;
    }

    void AudioManager::destroy_stream( Channel &channel )
    {
        if( channel.stage_mix_enabled )
        {
            for( auto &voice : channel.voice_streams )
            {
                for( auto &stream : voice )
                {
                    if( stream != nullptr )
                    {
                        SDL_DestroyAudioStream( stream );
                        stream = nullptr;
                    }
                }
            }

            channel.stream = nullptr;

            channel.voice_streams_bound = false;
        }
        else if( channel.stream != nullptr )
        {
            SDL_DestroyAudioStream( channel.stream );
            channel.stream = nullptr;
        }

        reset_channel_playback_state( channel );
    }

    void AudioManager::reset_channel_playback_state( Channel &channel )
    {
        channel.subsong_name.clear();
        channel.looping = false;

        channel.submitted_samples = 0;
        channel.queued_subsongs.clear();
        channel.stream_source_sample_rate = 0;

        channel.stream_source_channels = 2;

        channel.current_stem_gains = AudioStemGains{};

        channel.target_stem_gains = AudioStemGains{};

        channel.stem_gain_step = AudioStemGains{};

        channel.stem_fade_samples_remaining = 0;

        channel.phrase_layout_pcm = nullptr;

        channel.phrase_layout_ch2_has_signal = false;

        channel.phrase_layout_ch3_has_signal = false;

        channel.phrase_layout_ch4_has_signal = false;

        channel.phrase_layout_ch5_has_signal = false;

        channel.phrase_layout_stereo_pair = false;

        channel.last_source_channel_count = 0;
        channel.last_source_channel_gains.fill( 0.0f );
        channel.last_source_channel_raw_peaks.fill( 0.0f );
        channel.last_source_channel_post_gain_peaks.fill( 0.0f );
        channel.last_source_channel_mirrored_from.fill( -1 );
    }

    void AudioManager::update_channel( Channel &channel )
    {
        if( channel.stream == nullptr )
        {
            return;
        }

        if( channel.stage_mix_enabled )
        {
            update_voice_stream_gains( channel );
        }

        feed_channel_until_target( channel );

        if( channel.stream == nullptr )
        {
            return;
        }

        const std::int64_t queued = [ & ]() -> std::int64_t
        {
            if( channels_ <= 0 )
            {
                return 0;
            }

            const std::int32_t queued_bytes = SDL_GetAudioStreamQueued( channel.stream );
            const std::int64_t bytes_per_frame = static_cast<std::int64_t>( channel.stream_source_channels ) *
                                                 static_cast<std::int64_t>( sizeof( std::int16_t ) );

            return bytes_per_frame > 0 ? static_cast<std::int64_t>( queued_bytes ) / bytes_per_frame : 0;
        }();

        if( queued <= 0 && channel.queued_subsongs.empty() )
        {
            if( channel.stage_mix_enabled )
            {
                for( auto &voice : channel.voice_streams )
                {
                    for( auto &stream : voice )
                    {
                        if( stream != nullptr )
                        {
                            SDL_DestroyAudioStream( stream );
                            stream = nullptr;
                        }
                    }
                }

                channel.voice_streams_bound = false;
            }
            else
            {
                SDL_DestroyAudioStream( channel.stream );
            }

            channel.stream = nullptr;

            channel.stream_source_sample_rate = 0;

            channel.looping = false;
        }
    }

    void AudioManager::feed_channel_until_target( Channel &channel )
    {
        if( channel.stream == nullptr || sample_rate_ <= 0 || channels_ <= 0 )
        {
            return;
        }

        constexpr std::int64_t minimum_chunk_frames = 256;
        const std::int64_t target_queued_frames = std::max<std::int64_t>(
            static_cast<std::int64_t>( static_cast<float>( sample_rate_ ) * channel.buffer_seconds ),
            minimum_chunk_frames );

        const std::int64_t chunk_frames = std::max<std::int64_t>( sample_rate_ / 50, minimum_chunk_frames );

        for( ;; )
        {
            const std::int32_t queued_bytes = SDL_GetAudioStreamQueued( channel.stream );
            const std::int64_t bytes_per_frame = static_cast<std::int64_t>( channel.stream_source_channels ) *
                                                 static_cast<std::int64_t>( sizeof( std::int16_t ) );

            const std::int64_t queued_frames =
                bytes_per_frame > 0 ? static_cast<std::int64_t>( queued_bytes ) / bytes_per_frame : 0;

            if( queued_frames >= target_queued_frames )
            {
                if( channel.stage_mix_enabled )
                {
                    bind_voice_streams( channel );
                }

                return;
            }

            if( channel.queued_subsongs.empty() )
            {
                if( channel.stage_mix_enabled )
                {
                    bind_voice_streams( channel );
                }

                return;
            }

            Channel::QueuedSubsong &front = channel.queued_subsongs.front();
            const runtime::audio::Pcm16 *pcm = cached_pcm( front.name );

            if( pcm == nullptr || pcm->frame_count() <= 0 )
            {
                channel.queued_subsongs.pop_front();
                continue;
            }

            if( !ensure_stream_for_pcm( channel, *pcm ) )
            {
                return;
            }

            const std::int64_t subsong_frame_count = front.frame_limit >= 0 ? front.frame_limit : pcm->frame_count();
            const bool use_loop_region = channel.looping && channel.queued_subsongs.size() == 1 &&
                                         front.loop_start_frame >= 0 && front.loop_end_frame > front.loop_start_frame &&
                                         front.loop_end_frame <= subsong_frame_count;

            const std::int64_t playback_end_frame = use_loop_region ? front.loop_end_frame : subsong_frame_count;
            const std::int64_t remaining_frames = playback_end_frame - front.frame_offset;

            if( remaining_frames <= 0 )
            {
                if( channel.looping && channel.queued_subsongs.size() == 1 )
                {
                    front.frame_offset = use_loop_region ? front.loop_start_frame : 0;
                    continue;
                }

                channel.queued_subsongs.pop_front();

                if( !channel.queued_subsongs.empty() )
                {
                    channel.subsong_name = channel.queued_subsongs.front().name;
                }
                continue;
            }

            const std::int64_t frames_to_mix = std::min( chunk_frames, remaining_frames );
            const std::int64_t source_frames_remaining =
                std::max<std::int64_t>( 0, pcm->frame_count() - front.frame_offset );

            const std::int64_t source_frames_to_mix = std::min( frames_to_mix, source_frames_remaining );

            if( channel.stage_mix_enabled )
            {
                if( !put_voice_pcm_data( channel, *pcm, front.frame_offset, frames_to_mix ) )
                {
                    destroy_stream( channel );
                    return;
                }

                front.frame_offset += frames_to_mix;

                channel.submitted_samples += frames_to_mix;
                continue;
            }

            std::vector<std::int16_t> stereo;
            std::int64_t frames_mixed = 0;

            if( source_frames_to_mix > 0 )
            {
                frames_mixed =
                    mix_subsong_frames_to_stereo( channel, *pcm, front.frame_offset, source_frames_to_mix, stereo );

                if( frames_mixed != source_frames_to_mix )
                {
                    return;
                }
            }

            const std::int64_t silent_frames = frames_to_mix - frames_mixed;

            if( silent_frames > 0 )
            {
                if( !front.padding_reported )
                {
                    std::cerr << "Stage audio padding " << front.name << " from " << pcm->frame_count() << " to "
                              << subsong_frame_count << " samples\n";

                    front.padding_reported = true;
                }

                stereo.resize( static_cast<std::size_t>( frames_to_mix ) * 2u, 0 );
                frames_mixed = frames_to_mix;
            }

            if( frames_mixed <= 0 || stereo.empty() )
            {
                return;
            }

            if( !put_stereo_pcm_data( channel.stream, stereo ) )
            {
                return;
            }

            front.frame_offset += frames_mixed;

            channel.submitted_samples += frames_mixed;

            if( front.frame_offset >= playback_end_frame )
            {
                if( channel.looping && channel.queued_subsongs.size() == 1 )
                {
                    front.frame_offset = use_loop_region ? front.loop_start_frame : 0;
                }
                else
                {
                    channel.queued_subsongs.pop_front();

                    if( !channel.queued_subsongs.empty() )
                    {
                        channel.subsong_name = channel.queued_subsongs.front().name;
                    }
                }
            }
        }
    }

    bool AudioManager::ensure_stream_for_pcm( Channel &channel, const runtime::audio::Pcm16 &pcm )
    {
        if( !ensure_device_for_pcm( pcm ) )
        {
            return false;
        }

        if( channel.stream != nullptr )
        {
            if( channel.stream_source_sample_rate == pcm.sample_rate )
            {
                return true;
            }

            last_error_ = "AudioManager cannot queue cues with different sample rates on one stream";
            std::cerr << last_error_ << '\n';
            return false;
        }

        if( channel.stage_mix_enabled )
        {
            SDL_AudioSpec source_spec{ SDL_AUDIO_S16LE, 2, pcm.sample_rate };
            SDL_AudioSpec device_spec{ SDL_AUDIO_S16LE, output_channels, sample_rate_ };

            for( auto &voice : channel.voice_streams )
            {
                for( std::size_t side = 0; side < voice.size(); ++side )
                {
                    voice[ side ] = SDL_CreateAudioStream( &source_spec, &device_spec );
                    const std::array<std::int32_t, 2> map{
                        side == 0 ? 0 : -1,
                        side == 1 ? 1 : -1
                    };

                    if( voice[ side ] == nullptr || !SDL_SetAudioStreamInputChannelMap( voice[ side ],
                                                        map.data(),
                                                        static_cast<std::int32_t>( map.size() ) ) )
                    {
                        last_error_ = SDL_GetError();
                        destroy_stream( channel );
                        return false;
                    }
                }
            }

            channel.stream = channel.voice_streams[ 0 ][ 0 ];

            channel.stream_source_channels = 2;

            channel.stream_source_sample_rate = pcm.sample_rate;
            update_voice_stream_gains( channel );
            return true;
        }

        channel.stream = create_bound_stream_for_pcm( pcm );

        if( channel.stream == nullptr )
        {
            return false;
        }

        channel.stream_source_sample_rate = pcm.sample_rate;
        return true;
    }

    bool AudioManager::load_runtime_audio_directory( const std::filesystem::path &path,
        bool replace_existing,
        std::string_view replace_cue,
        std::string_view cue_prefix )
    {
        const utils::json::Value root = utils::json::load_file( path / "audio.json" );

        if( root.require( "format" ).string() != "piko-audio" || root.require( "version" ).integer_or( 0 ) != 1 )
        {
            throw std::runtime_error( "Unsupported runtime audio manifest: " + path.string() );
        }

        for( const utils::json::Value &entry : root.require( "cues" ).array() )
        {
            const std::string source_name = entry.require( "name" ).string();
            const std::string name = std::string( cue_prefix ) + source_name;
            const std::filesystem::path audio_path = ( path / entry.require( "file" ).string() ).lexically_normal();
            std::error_code ec{};

            if( name.empty() || !std::filesystem::is_regular_file( audio_path, ec ) )
            {
                throw std::runtime_error( "Runtime audio cue is missing its file: " + name );
            }

            std::string codec;

            if( const utils::json::Value *codec_entry = entry.find( "codec" ); codec_entry != nullptr )
            {
                codec = codec_entry->string();
            }

            if( codec.empty() )
            {
                codec = audio_path.extension().string();

                if( !codec.empty() && codec.front() == '.' )
                {
                    codec.erase( codec.begin() );
                }
            }

            codec = piko::utils::lowercase_ascii( std::move( codec ) );

            if( codec != "wav" && codec != "flac" && codec != "opus" )
            {
                throw std::runtime_error( "Unsupported runtime audio codec: " + codec );
            }

            if( codec != "wav" && ffmpeg_executable_.empty() )
            {
                ffmpeg_executable_ = convert::find_ffmpeg_executable( application_root_ );
            }

            RuntimeAudioCue cue{};
            cue.path = audio_path;

            cue.codec = codec;

            cue.frame_count = entry.require( "frames" ).integer_or( 0 );

            if( const utils::json::Value *value = entry.find( "loop_start_frame" ); value != nullptr )
            {
                cue.loop_start_frame = value->integer_or( -1 );
            }

            if( const utils::json::Value *value = entry.find( "loop_end_frame" ); value != nullptr )
            {
                cue.loop_end_frame = value->integer_or( -1 );
            }

            cue.sample_rate = static_cast<std::int32_t>( entry.require( "sample_rate" ).integer_or( 0 ) );

            cue.channel_count = static_cast<std::int32_t>( entry.require( "channels" ).integer_or( 0 ) );

            if( replace_existing && ( replace_cue.empty() || source_name == replace_cue ) )
            {
                decoded_cache_.erase( name );
                runtime_audio_cues_.insert_or_assign( name, std::move( cue ) );
            }
            else
            {
                runtime_audio_cues_.try_emplace( name, std::move( cue ) );
            }
        }

        if( runtime_audio_cues_.empty() )
        {
            throw std::runtime_error( "Runtime audio manifest contains no cues: " + path.string() );
        }

        return true;
    }

    runtime::audio::Pcm16 AudioManager::load_runtime_audio( const RuntimeAudioCue &cue ) const
    {
        runtime::audio::Pcm16 pcm{};

        if( cue.codec == "wav" )
        {
            pcm = load_runtime_wav( cue.path );
        }
        else
        {
            static std::atomic<std::uint64_t> counter{ 0u };
            const std::uint64_t stamp =
                static_cast<std::uint64_t>( std::chrono::high_resolution_clock::now().time_since_epoch().count() );

            const std::filesystem::path temp_directory =
                application_root_ / "Cache" /
                ( "piko_audio_decode_" + std::to_string( stamp ) + "_" + std::to_string( counter.fetch_add( 1u ) ) );

            const std::filesystem::path temp_wav = temp_directory / "decoded.wav";

            try
            {
                convert::decode_audio_to_wav( cue.path, temp_wav, ffmpeg_executable_ );
                pcm = load_runtime_wav( temp_wav );
            }
            catch( ... )
            {
                std::filesystem::remove_all( temp_directory );
                throw;
            }

            std::filesystem::remove_all( temp_directory );
        }

        if( cue.sample_rate > 0 && pcm.sample_rate != cue.sample_rate )
        {
            throw std::runtime_error( "Runtime audio sample rate does not match its manifest: " + cue.path.string() );
        }

        if( cue.channel_count > 0 && pcm.channels != cue.channel_count )
        {
            throw std::runtime_error( "Runtime audio channel count does not match its manifest: " + cue.path.string() );
        }

        if( cue.frame_count > 0 && pcm.channels > 0 )
        {
            const std::uint64_t expected_samples =
                static_cast<std::uint64_t>( cue.frame_count ) * static_cast<std::uint64_t>( pcm.channels );

            if( expected_samples > std::numeric_limits<std::size_t>::max() )
            {
                throw std::runtime_error( "Runtime audio sample count is too large: " + cue.path.string() );
            }

            pcm.interleaved.resize( static_cast<std::size_t>( expected_samples ), 0 );
        }

        return pcm;
    }

    runtime::audio::Pcm16 AudioManager::load_runtime_wav( const std::filesystem::path &path ) const
    {
        std::ifstream file( path, std::ios::binary | std::ios::ate );

        if( !file )
        {
            throw std::runtime_error( "Could not open runtime WAV: " + path.string() );
        }

        const std::streamsize size = file.tellg();

        if( size < 12 )
        {
            throw std::runtime_error( "Runtime WAV is too small: " + path.string() );
        }

        file.seekg( 0, std::ios::beg );
        std::vector<std::uint8_t> bytes( static_cast<std::size_t>( size ) );

        if( !file.read( reinterpret_cast<char *>( bytes.data() ), size ) )
        {
            throw std::runtime_error( "Could not read runtime WAV: " + path.string() );
        }

        const auto read_u16 = [ &bytes, &path ]( std::size_t offset ) -> std::uint16_t
        {
            if( offset + 2u > bytes.size() )
            {
                throw std::runtime_error( "Runtime WAV read exceeded file bounds: " + path.string() );
            }

            return piko::utils::read_le_u16( bytes.data() + offset );
        };

        const auto read_u32 = [ &bytes, &path ]( std::size_t offset ) -> std::uint32_t
        {
            if( offset + 4u > bytes.size() )
            {
                throw std::runtime_error( "Runtime WAV read exceeded file bounds: " + path.string() );
            }

            return piko::utils::read_le_u32( bytes.data() + offset );
        };

        const auto has_tag = [ &bytes ]( std::size_t offset, const char *tag )
        {
            return offset + 4u <= bytes.size() && std::memcmp( bytes.data() + offset, tag, 4u ) == 0;
        };

        if( !has_tag( 0u, "RIFF" ) || !has_tag( 8u, "WAVE" ) )
        {
            throw std::runtime_error( "Runtime audio file is not a RIFF WAV: " + path.string() );
        }

        std::uint16_t format_tag = 0u;
        std::uint16_t channels = 0u;
        std::uint32_t sample_rate = 0u;
        std::uint16_t block_align = 0u;
        std::uint16_t bits_per_sample = 0u;
        std::size_t data_offset = 0u;
        std::size_t data_size = 0u;
        bool found_format = false;
        bool found_data = false;

        std::size_t offset = 12u;
        while( offset + 8u <= bytes.size() )
        {
            const std::uint32_t chunk_size = read_u32( offset + 4u );
            const std::size_t payload = offset + 8u;

            if( chunk_size > bytes.size() - payload )
            {
                throw std::runtime_error( "Runtime WAV contains a truncated chunk: " + path.string() );
            }

            if( has_tag( offset, "fmt " ) )
            {
                if( chunk_size < 16u )
                {
                    throw std::runtime_error( "Runtime WAV has an invalid format chunk: " + path.string() );
                }

                format_tag = read_u16( payload + 0u );

                channels = read_u16( payload + 2u );

                sample_rate = read_u32( payload + 4u );

                block_align = read_u16( payload + 12u );

                bits_per_sample = read_u16( payload + 14u );

                if( format_tag == 0xfffeu )
                {
                    if( chunk_size < 40u || read_u16( payload + 16u ) < 22u )
                    {
                        throw std::runtime_error( "Runtime WAV has an invalid extensible format: " + path.string() );
                    }

                    format_tag = static_cast<std::uint16_t>( read_u32( payload + 24u ) );
                }

                found_format = true;
            }
            else if( has_tag( offset, "data" ) && !found_data )
            {
                data_offset = payload;

                data_size = chunk_size;

                found_data = true;
            }

            const std::size_t padded_size = static_cast<std::size_t>( chunk_size ) + ( chunk_size & 1u );

            if( padded_size > bytes.size() - payload )
            {
                break;
            }

            offset = payload + padded_size;
        }

        if( !found_format || !found_data )
        {
            throw std::runtime_error( "Runtime WAV is missing its format or sample data: " + path.string() );
        }

        if( format_tag != 1u || bits_per_sample != 16u )
        {
            throw std::runtime_error( "Runtime WAV is not signed 16-bit PCM: " + path.string() );
        }

        if( channels == 0u || sample_rate == 0u || block_align != channels * sizeof( std::int16_t ) )
        {
            throw std::runtime_error( "Runtime WAV has an invalid channel layout: " + path.string() );
        }

        if( data_size % block_align != 0u )
        {
            throw std::runtime_error( "Runtime WAV sample data is not frame aligned: " + path.string() );
        }

        runtime::audio::Pcm16 pcm{};
        pcm.sample_rate = static_cast<std::int32_t>( sample_rate );

        pcm.channels = static_cast<std::int32_t>( channels );
        pcm.interleaved.resize( data_size / sizeof( std::int16_t ) );

        for( std::size_t index = 0; index < pcm.interleaved.size(); ++index )
        {
            const std::uint16_t raw = read_u16( data_offset + index * 2u );
            const std::int32_t signed_value =
                raw >= 0x8000u ? static_cast<std::int32_t>( raw ) - 0x10000 : static_cast<std::int32_t>( raw );

            pcm.interleaved[ index ] = static_cast<std::int16_t>( signed_value );
        }

        return pcm;
    }

    const AudioManager::RuntimeAudioCue *AudioManager::runtime_audio_cue( std::string_view name ) const
    {
        const auto exact = runtime_audio_cues_.find( std::string( name ) );

        if( exact != runtime_audio_cues_.end() )
        {
            return &exact->second;
        }

        const std::string target = piko::utils::uppercase_ascii( std::string( name ) );

        for( const auto &[ cue_name, cue ] : runtime_audio_cues_ )
        {
            const std::string candidate = piko::utils::uppercase_ascii( cue_name );

            if( candidate == target )
            {
                return &cue;
            }
        }

        return nullptr;
    }

    bool AudioManager::ensure_async_worker_started()
    {
        std::scoped_lock lock( async_mutex_ );

        if( async_worker_running_ )
        {
            return true;
        }

        async_worker_stop_ = false;

        try
        {
            async_worker_ = std::thread(
                [ this ]()
                {
                    async_worker_main();
                } );
        }
        catch( const std::exception &e )
        {
            last_error_ = std::string( "Could not start audio decode worker: " ) + e.what();
            std::cerr << last_error_ << '\n';
            return false;
        }

        async_worker_running_ = true;
        return true;
    }

    void AudioManager::stop_async_worker()
    {
        {
            std::scoped_lock lock( async_mutex_ );
            async_worker_stop_ = true;
            async_jobs_.clear();
            async_pending_names_.clear();
        }

        async_cv_.notify_all();

        if( async_worker_.joinable() )
        {
            async_worker_.join();
        }

        {
            std::scoped_lock lock( async_mutex_ );
            async_worker_running_ = false;

            async_worker_stop_ = false;
            async_jobs_.clear();
            async_results_.clear();
            async_pending_names_.clear();
        }
    }

    void AudioManager::async_worker_main()
    {
        for( ;; )
        {
            AsyncDecodeJob job{};

            {
                std::unique_lock lock( async_mutex_ );
                async_cv_.wait( lock,
                    [ this ]()
                    {
                        return async_worker_stop_ || !async_jobs_.empty();
                    } );

                if( async_worker_stop_ && async_jobs_.empty() )
                {
                    return;
                }

                job = std::move( async_jobs_.front() );
                async_jobs_.pop_front();
            }

            AsyncDecodeResult result{};
            result.name = job.name;

            try
            {
                result.pcm = load_runtime_audio( job.runtime_audio );

                result.ok = true;
            }
            catch( const std::exception &e )
            {
                result.ok = false;

                result.error = e.what();
            }

            {
                std::scoped_lock lock( async_mutex_ );
                async_results_.push_back( std::move( result ) );
            }
        }
    }

    void AudioManager::consume_async_preload_results()
    {
        std::deque<AsyncDecodeResult> results;

        {
            std::scoped_lock lock( async_mutex_ );
            results.swap( async_results_ );
        }

        for( AsyncDecodeResult &result : results )
        {
            {
                std::scoped_lock lock( async_mutex_ );
                async_pending_names_.erase( result.name );
            }

            if( result.ok )
            {
                if( decoded_cache_.find( result.name ) == decoded_cache_.end() )
                {
                    std::cout << "Async preloaded runtime audio cue: " << result.name
                              << " rate=" << result.pcm.sample_rate << " channels=" << result.pcm.channels
                              << " samples=" << result.pcm.frame_count() << '\n';

                    decoded_cache_[ result.name ] = std::move( result.pcm );
                }

                last_error_.clear();
            }
            else
            {
                last_error_ = "Async runtime audio preload failed for " + result.name + ": " + result.error;
                std::cerr << last_error_ << '\n';
            }
        }
    }

    bool AudioManager::ensure_device_for_pcm( const runtime::audio::Pcm16 &pcm )
    {
        if( pcm.sample_rate <= 0 || pcm.channels <= 0 || pcm.interleaved.empty() )
        {
            return false;
        }

        if( device_ != 0 )
        {
            return true;
        }

        sample_rate_ = pcm.sample_rate;

        channels_ = output_channels;

        SDL_AudioSpec device_spec{};
        device_spec.format = SDL_AUDIO_S16LE;

        device_spec.channels = channels_;

        device_spec.freq = sample_rate_;

        device_ = SDL_OpenAudioDevice( SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &device_spec );

        if( device_ == 0 )
        {
            last_error_ = std::string( "SDL_OpenAudioDevice failed: " ) + SDL_GetError();
            std::cerr << last_error_ << '\n';
            sample_rate_ = 0;

            channels_ = 0;
            return false;
        }

        return true;
    }

    SDL_AudioStream *AudioManager::create_bound_stream_for_pcm( const runtime::audio::Pcm16 &pcm )
    {
        if( device_ == 0 || sample_rate_ <= 0 || channels_ <= 0 )
        {
            last_error_ = "Audio stream requested before opening playback device";
            std::cerr << last_error_ << '\n';
            return nullptr;
        }

        SDL_AudioSpec source_spec{};
        source_spec.format = SDL_AUDIO_S16LE;

        source_spec.channels = output_channels;

        source_spec.freq = pcm.sample_rate;

        SDL_AudioSpec device_spec{};
        device_spec.format = SDL_AUDIO_S16LE;

        device_spec.channels = channels_;

        device_spec.freq = sample_rate_;

        SDL_AudioStream *stream = SDL_CreateAudioStream( &source_spec, &device_spec );

        if( stream == nullptr )
        {
            last_error_ = std::string( "SDL_CreateAudioStream failed: " ) + SDL_GetError();
            std::cerr << last_error_ << '\n';
            return nullptr;
        }

        if( !SDL_BindAudioStream( device_, stream ) )
        {
            last_error_ = std::string( "SDL_BindAudioStream failed: " ) + SDL_GetError();
            std::cerr << last_error_ << '\n';
            SDL_DestroyAudioStream( stream );
            return nullptr;
        }

        return stream;
    }

    bool AudioManager::put_stereo_pcm_data( SDL_AudioStream *stream, const std::vector<std::int16_t> &stereo )
    {
        if( stream == nullptr || stereo.empty() )
        {
            return false;
        }

        const std::size_t byte_count_size = stereo.size() * sizeof( std::int16_t );

        if( byte_count_size > static_cast<std::size_t>( std::numeric_limits<std::int32_t>::max() ) )
        {
            last_error_ = "PCM buffer is too large for SDL_PutAudioStreamData";
            std::cerr << last_error_ << '\n';
            return false;
        }

        const std::int32_t byte_count = static_cast<std::int32_t>( byte_count_size );

        if( !SDL_PutAudioStreamData( stream, stereo.data(), byte_count ) )
        {
            last_error_ = std::string( "SDL_PutAudioStreamData failed: " ) + SDL_GetError();
            std::cerr << last_error_ << '\n';
            return false;
        }

        return true;
    }

    std::int64_t AudioManager::mix_subsong_frames_to_stereo( Channel &channel,
        const runtime::audio::Pcm16 &pcm,
        std::int64_t source_frame,
        std::int64_t frame_count,
        std::vector<std::int16_t> &out_stereo )
    {
        if( pcm.channels <= 0 || pcm.sample_rate <= 0 || source_frame < 0 || frame_count <= 0 )
        {
            return 0;
        }

        const std::int64_t total_frames = pcm.frame_count();

        if( source_frame >= total_frames )
        {
            return 0;
        }

        const std::int64_t frames_to_mix = std::min( frame_count, total_frames - source_frame );
        out_stereo.clear();
        out_stereo.resize( static_cast<std::size_t>( frames_to_mix ) * output_channels );

        const std::int32_t debug_source_channel_count =
            std::clamp( pcm.channels, 0, static_cast<std::int32_t>( audio_debug_source_channel_count ) );

        std::array<float, audio_debug_source_channel_count> debug_gains{};
        std::array<float, audio_debug_source_channel_count> raw_peaks{};
        std::array<float, audio_debug_source_channel_count> post_gain_peaks{};
        std::array<std::int32_t, audio_debug_source_channel_count> mirrored_from{};
        mirrored_from.fill( -1 );

        if( channel.phrase_layout_pcm != &pcm )
        {
            channel.phrase_layout_pcm = &pcm;
            const std::array<bool, 4> present = instrument_channels_with_signal( pcm );
            channel.phrase_layout_ch2_has_signal = present[ 0 ];

            channel.phrase_layout_ch3_has_signal = present[ 1 ];

            channel.phrase_layout_ch4_has_signal = present[ 2 ];

            channel.phrase_layout_ch5_has_signal = present[ 3 ];

            channel.phrase_layout_stereo_pair =
                channel.phrase_layout_ch2_has_signal && channel.phrase_layout_ch4_has_signal;
        }

        for( std::int64_t frame = 0; frame < frames_to_mix; ++frame )
        {
            const std::size_t base =
                static_cast<std::size_t>( source_frame + frame ) * static_cast<std::size_t>( pcm.channels );

            double left = 0.0;
            double right = 0.0;

            const auto sample_at = [ & ]( std::int32_t channel_index ) -> double
            {
                if( channel_index < 0 || channel_index >= pcm.channels )
                {
                    return 0.0;
                }

                return static_cast<double>( pcm.interleaved[ base + static_cast<std::size_t>( channel_index ) ] );
            };

            const auto record_source_channel = [ & ]( std::int32_t channel_index,
                                                   float gain,
                                                   double raw_sample,
                                                   double mixed_sample,
                                                   std::int32_t mirrored_from_channel = -1 )
            {
                if( channel_index < 0 || channel_index >= debug_source_channel_count )
                {
                    return;
                }

                const std::size_t index = static_cast<std::size_t>( channel_index );
                debug_gains[ index ] = gain;

                if( mirrored_from_channel >= 0 )
                {
                    mirrored_from[ index ] = mirrored_from_channel;
                }

                const float raw_peak = static_cast<float>( std::fabs( raw_sample ) / 32768.0 );
                const float post_peak = static_cast<float>( std::fabs( mixed_sample * gain ) / 32768.0 );

                raw_peaks[ index ] = std::max( raw_peaks[ index ], raw_peak );

                post_gain_peaks[ index ] = std::max( post_gain_peaks[ index ], post_peak );
            };

            const AudioStemGains gains = channel.current_stem_gains;
            const bool stage_music = channel.debug_name == "Stage audio";
            const std::int32_t instrument_routing = stage_music ? instrument_routing_ : 0;

            if( pcm.channels == 1 )
            {
                const float gain = gains.backing;
                const double sample = sample_at( 0 );
                record_source_channel( 0, gain, sample, sample );
                left += sample * gain;

                right += sample * gain;
            }
            else if( pcm.channels == 2 )
            {
                const float left_gain = gains.backing;
                const float right_gain = gains.backing;
                const double left_sample = sample_at( 0 );
                const double right_sample = sample_at( 1 );
                record_source_channel( 0, left_gain, left_sample, left_sample );
                record_source_channel( 1, right_gain, right_sample, right_sample );
                left += left_sample * left_gain;

                right += right_sample * right_gain;
            }
            else
            {

                const auto mix_left = [ & ]( std::int32_t source_channel, float gain )
                {
                    const double sample = sample_at( source_channel );
                    record_source_channel( source_channel, gain, sample, sample );
                    left += sample * gain;
                };

                const auto mix_right = [ & ]( std::int32_t source_channel, float gain )
                {
                    const double sample = sample_at( source_channel );
                    record_source_channel( source_channel, gain, sample, sample );
                    right += sample * gain;
                };

                const auto mix_center = [ & ]( std::int32_t source_channel, float gain )
                {
                    const double sample = sample_at( source_channel );
                    record_source_channel( source_channel, gain, sample, sample );
                    left += sample * gain;

                    right += sample * gain;
                };

                const auto mix_right_from =
                    [ & ]( std::int32_t target_channel, std::int32_t source_channel, float gain )
                {
                    const double sample = sample_at( source_channel );
                    record_source_channel( target_channel, gain, sample_at( target_channel ), sample, source_channel );
                    right += sample * gain;
                };

                mix_left( 0, gains.backing );
                mix_right( 1, gains.backing );

                const bool mirror_mono_instruments =
                    stage_music && pcm.channels > 5 &&
                    ( channel.phrase_layout_ch2_has_signal || channel.phrase_layout_ch3_has_signal ) &&
                    !channel.phrase_layout_ch4_has_signal && !channel.phrase_layout_ch5_has_signal;

                const bool center_channel_2_phrase = channel.phrase_stem_mode == AudioPhraseStemMode::MonoCh2Center &&
                                                     !channel.phrase_layout_stereo_pair;

                if( mirror_mono_instruments )
                {
                    mix_left( 2, gains.player );
                    mix_right_from( 5, 2, gains.player );
                    mix_left( 3, gains.enemy );
                    mix_right_from( 4, 3, gains.enemy );
                }
                else
                {
                    if( center_channel_2_phrase )
                    {
                        mix_center( 2, gains.player );
                        mix_right( 4, gains.backing );
                    }
                    else if( channel.phrase_layout_ch2_has_signal != channel.phrase_layout_ch4_has_signal )
                    {
                        mix_center( 2, gains.player );
                        mix_center( 4, gains.player );
                    }
                    else if( instrument_routing == 1 )
                    {
                        mix_right( 2, gains.player );
                        mix_left( 4, gains.player );
                    }
                    else if( instrument_routing == 2 )
                    {
                        mix_center( 2, gains.player * 0.5f );
                        mix_center( 4, gains.player * 0.5f );
                    }
                    else
                    {
                        mix_left( 2, gains.player );
                        mix_right( 4, gains.player );
                    }

                    if( channel.phrase_layout_ch3_has_signal != channel.phrase_layout_ch5_has_signal )
                    {
                        mix_center( 3, gains.enemy );
                        mix_center( 5, gains.enemy );
                    }
                    else if( instrument_routing == 1 )
                    {
                        mix_right( 3, gains.enemy );
                        mix_left( 5, gains.enemy );
                    }
                    else if( instrument_routing == 2 )
                    {
                        mix_center( 3, gains.enemy * 0.5f );
                        mix_center( 5, gains.enemy * 0.5f );
                    }
                    else
                    {
                        mix_left( 3, gains.enemy );
                        mix_right( 5, gains.enemy );
                    }
                }

                if( !stage_music || background_fx_enabled_ )
                {
                    mix_left( 6, gains.fx );
                    mix_right( 7, gains.fx );
                }

                for( std::int32_t source_channel = 8; source_channel < pcm.channels; ++source_channel )
                {
                    const float gain =
                        source_channel_gain_for( source_channel, pcm.channels, channel.phrase_stem_mode, gains );

                    if( ( source_channel & 1 ) == 0 )
                    {
                        mix_left( source_channel, gain );
                    }
                    else
                    {
                        mix_right( source_channel, gain );
                    }
                }
            }

            if( !stereo_output_ )
            {
                const double mono = ( left + right ) * 0.5;
                left = mono;

                right = mono;
            }

            left *= output_mix_gain;

            right *= output_mix_gain;

            const std::size_t dst = static_cast<std::size_t>( frame ) * output_channels;
            out_stereo[ dst + 0 ] = clamp_s16_from_double( left );

            out_stereo[ dst + 1 ] = clamp_s16_from_double( right );

            if( channel.stem_fade_samples_remaining > 0 )
            {
                channel.current_stem_gains.backing += channel.stem_gain_step.backing;

                channel.current_stem_gains.player += channel.stem_gain_step.player;

                channel.current_stem_gains.enemy += channel.stem_gain_step.enemy;

                channel.current_stem_gains.fx += channel.stem_gain_step.fx;
                --channel.stem_fade_samples_remaining;

                if( channel.stem_fade_samples_remaining <= 0 )
                {
                    channel.current_stem_gains = channel.target_stem_gains;

                    channel.stem_gain_step = {};
                }
            }
        }

        for( std::int32_t source_channel = 0; source_channel < debug_source_channel_count; ++source_channel )
        {
            const std::size_t index = static_cast<std::size_t>( source_channel );

            if( debug_gains[ index ] == 0.0f )
            {
                debug_gains[ index ] = channel.debug_name == "Stage audio" && !background_fx_enabled_ &&
                                               ( source_channel == 6 || source_channel == 7 )
                                           ? 0.0f
                                           : source_channel_gain_for( source_channel,
                                                 pcm.channels,
                                                 channel.phrase_stem_mode,
                                                 channel.current_stem_gains );
            }
        }

        channel.last_source_channel_count = debug_source_channel_count;

        channel.last_source_channel_gains = debug_gains;

        channel.last_source_channel_raw_peaks = raw_peaks;

        channel.last_source_channel_post_gain_peaks = post_gain_peaks;

        channel.last_source_channel_mirrored_from = mirrored_from;

        return frames_to_mix;
    }

    AudioManager::Channel *AudioManager::channel_for( AudioChannelHandle handle )
    {
        if( handle.id == 0 )
        {
            return nullptr;
        }

        const std::size_t index = static_cast<std::size_t>( handle.id - 1u );

        if( index >= channels_storage_.size() )
        {
            return nullptr;
        }

        Channel &channel = channels_storage_[ index ];

        if( !channel.claimed || channel.generation != handle.generation )
        {
            return nullptr;
        }

        return &channel;
    }

    const AudioManager::Channel *AudioManager::channel_for( AudioChannelHandle handle ) const
    {
        if( handle.id == 0 )
        {
            return nullptr;
        }

        const std::size_t index = static_cast<std::size_t>( handle.id - 1u );

        if( index >= channels_storage_.size() )
        {
            return nullptr;
        }

        const Channel &channel = channels_storage_[ index ];

        if( !channel.claimed || channel.generation != handle.generation )
        {
            return nullptr;
        }

        return &channel;
    }

    const runtime::audio::Pcm16 *AudioManager::cached_pcm( std::string_view name ) const
    {
        const auto it = decoded_cache_.find( std::string( name ) );
        return it != decoded_cache_.end() ? &it->second : nullptr;
    }

    runtime::audio::Pcm16 *AudioManager::cached_pcm( std::string_view name )
    {
        const auto it = decoded_cache_.find( std::string( name ) );
        return it != decoded_cache_.end() ? &it->second : nullptr;
    }
}
