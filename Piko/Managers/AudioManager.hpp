#pragma once

#include "Runtime/Audio/Pcm16.hpp"
#include "FileTypes/CHC/CHC.hpp"
#include "GameData/GameDataRoot.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <array>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <string>
#include <string_view>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace piko::managers
{
    struct AudioChannelHandle
    {
        std::uint32_t id = 0;
        std::uint32_t generation = 0;

        [[nodiscard]] bool valid() const;
        explicit operator bool() const;
    };

    struct AudioStemGains
    {
        float backing = 1.0f;
        float player = 1.0f;
        float enemy = 1.0f;
        float fx = 1.0f;
    };

    enum class AudioPhraseStemMode
    {
        MonoCh2Center,
        StereoCh2Ch4
    };

    static constexpr std::size_t audio_debug_source_channel_count = 16;

    struct AudioSourceChannelDebug
    {
        std::int32_t index = 0;
        float gain = 1.0f;
        float raw_peak = 0.0f;
        float post_gain_peak = 0.0f;
        std::int32_t mirrored_from_channel = -1;
    };

    struct AudioChannelMixDebug
    {
        bool valid = false;
        std::string debug_name;
        std::string subsong_name;
        std::int32_t source_channel_count = 0;
        AudioPhraseStemMode phrase_stem_mode = AudioPhraseStemMode::StereoCh2Ch4;
        bool phrase_layout_stereo_pair = false;
        AudioStemGains current_gains{};
        AudioStemGains target_gains{};
        std::int64_t fade_samples_remaining = 0;
        std::array<AudioSourceChannelDebug, audio_debug_source_channel_count> source_channels{};
    };

    class AudioManager
    {
    public:
        bool initialize();
        void shutdown();
        void update();
        void set_stereo_output( bool stereo );
        void set_background_fx_enabled( bool enabled );
        void set_instrument_routing( std::int32_t routing );
        [[nodiscard]] bool stereo_output() const;

        bool load_audio_bank( const std::filesystem::path &path );
        bool merge_audio_bank( const std::filesystem::path &path,
            std::string_view replace_cue = {},
            std::string_view cue_prefix = {} );

        void unload_audio_bank();

        bool preload_subsong( std::string_view name );
        bool preload_subsongs( const std::vector<std::string> &names );
        bool preload_subsong_async( std::string_view name );
        bool preload_subsongs_async( const std::vector<std::string> &names );
        [[nodiscard]] bool is_subsong_preloaded( std::string_view name ) const;
        [[nodiscard]] bool is_subsong_preload_pending( std::string_view name ) const;

        [[nodiscard]] AudioChannelHandle claim_channel( std::string_view debug_name, float buffer_seconds = 0.1f );
        void release_channel( AudioChannelHandle handle );
        void release_all_channels();

        bool play_subsong( AudioChannelHandle handle,
            std::string_view name,
            bool loop,
            std::int64_t frame_limit = -1,
            std::int64_t fade_in_samples = 0 );

        bool play_pcm( AudioChannelHandle handle, std::string_view name, runtime::audio::Pcm16 pcm, bool loop = false );
        bool queue_subsong( AudioChannelHandle handle, std::string_view name, std::int64_t frame_limit = -1 );
        void stop_channel( AudioChannelHandle handle );
        void stop_all_channels();
        void stop_all();

        void set_channel_stem_gains( AudioChannelHandle handle,
            const AudioStemGains &gains,
            std::int64_t fade_samples = 0 );

        void set_channel_player_stem_gain( AudioChannelHandle handle, float gain, std::int64_t fade_samples = 0 );
        void set_channel_phrase_stem_mode( AudioChannelHandle handle, AudioPhraseStemMode mode );
        void set_channel_stage_mix( AudioChannelHandle handle,
            const filetypes::chc::AudioMix &mix,
            gamedata::ChartStyle style,
            bool stereo_phrases );

        void set_channel_voice_volumes( AudioChannelHandle handle, const std::array<std::int32_t, 8> &volumes );
        [[nodiscard]] AudioStemGains channel_stem_gains( AudioChannelHandle handle ) const;
        [[nodiscard]] AudioChannelMixDebug channel_mix_debug( AudioChannelHandle handle ) const;

        void pause();
        void resume();

        [[nodiscard]] bool has_audio_bank_loaded() const;
        [[nodiscard]] bool has_subsong( std::string_view name ) const;
        [[nodiscard]] std::int64_t subsong_frame_count( std::string_view name ) const;
        [[nodiscard]] bool is_playing() const;

        [[nodiscard]] std::int32_t sample_rate() const;
        [[nodiscard]] std::int32_t channels() const;

        [[nodiscard]] bool is_channel_claimed( AudioChannelHandle handle ) const;
        [[nodiscard]] bool is_channel_playing( AudioChannelHandle handle ) const;
        [[nodiscard]] std::int64_t channel_sample( AudioChannelHandle handle ) const;
        [[nodiscard]] std::int64_t channel_submitted_samples( AudioChannelHandle handle ) const;
        [[nodiscard]] std::int64_t queued_channel_samples( AudioChannelHandle handle ) const;
        [[nodiscard]] const std::string *channel_subsong_name( AudioChannelHandle handle ) const;
        [[nodiscard]] const std::string *channel_debug_name( AudioChannelHandle handle ) const;

        [[nodiscard]] const std::filesystem::path &loaded_audio_bank_path() const;
        [[nodiscard]] const std::string &last_error() const;

    private:
        struct Channel
        {
            std::uint32_t generation = 1;
            bool claimed = false;
            std::string debug_name;
            float buffer_seconds = 0.1f;

            struct QueuedSubsong
            {
                std::string name;
                std::int64_t frame_offset = 0;
                std::int64_t frame_limit = -1;
                std::int64_t loop_start_frame = -1;
                std::int64_t loop_end_frame = -1;
                bool padding_reported = false;
            };

            SDL_AudioStream *stream = nullptr;
            std::array<std::array<SDL_AudioStream *, 2>, 8> voice_streams{};
            bool stage_mix_enabled = false;
            bool voice_streams_bound = false;
            filetypes::chc::AudioMix stage_mix{};
            gamedata::ChartStyle stage_mix_style = gamedata::ChartStyle::GitarooMan;
            std::array<std::int32_t, 8> voice_volumes{};
            std::int32_t stream_source_channels = 2;
            std::string subsong_name;
            bool looping = false;
            std::int64_t submitted_samples = 0;
            std::deque<QueuedSubsong> queued_subsongs;
            std::int32_t stream_source_sample_rate = 0;

            AudioStemGains current_stem_gains{};
            AudioStemGains target_stem_gains{};
            AudioStemGains stem_gain_step{};
            std::int64_t stem_fade_samples_remaining = 0;
            AudioPhraseStemMode phrase_stem_mode = AudioPhraseStemMode::StereoCh2Ch4;

            const runtime::audio::Pcm16 *phrase_layout_pcm = nullptr;
            bool phrase_layout_ch2_has_signal = false;
            bool phrase_layout_ch3_has_signal = false;
            bool phrase_layout_ch4_has_signal = false;
            bool phrase_layout_ch5_has_signal = false;
            bool phrase_layout_stereo_pair = false;

            std::int32_t last_source_channel_count = 0;
            std::array<float, audio_debug_source_channel_count> last_source_channel_gains{};
            std::array<float, audio_debug_source_channel_count> last_source_channel_raw_peaks{};
            std::array<float, audio_debug_source_channel_count> last_source_channel_post_gain_peaks{};
            std::array<std::int32_t, audio_debug_source_channel_count> last_source_channel_mirrored_from{};
        };

        static constexpr std::size_t max_channels = 32;
        static constexpr std::int32_t output_channels = 2;
        static constexpr double output_mix_gain = 0.5;

        [[nodiscard]] static float clamp_gain( float value );
        [[nodiscard]] static std::int16_t clamp_s16_from_double( double value );
        [[nodiscard]] static std::array<bool, 4> instrument_channels_with_signal( const runtime::audio::Pcm16 &pcm );
        [[nodiscard]] static AudioStemGains clamp_gains( AudioStemGains gains );
        [[nodiscard]] static AudioStemGains gain_step_for( const AudioStemGains &from,
            const AudioStemGains &to,
            std::int64_t fade_samples );

        [[nodiscard]] static float source_channel_gain_for( std::int32_t source_channel,
            std::int32_t source_channel_count,
            AudioPhraseStemMode phrase_stem_mode,
            const AudioStemGains &gains );

        void close_audio_device();
        void destroy_stream( Channel &channel );
        void reset_channel_playback_state( Channel &channel );
        void update_channel( Channel &channel );
        void feed_channel_until_target( Channel &channel );
        [[nodiscard]] bool ensure_stream_for_pcm( Channel &channel, const runtime::audio::Pcm16 &pcm );
        void update_voice_stream_gains( Channel &channel );
        bool put_voice_pcm_data( Channel &channel,
            const runtime::audio::Pcm16 &pcm,
            std::int64_t source_frame,
            std::int64_t frame_count );

        bool bind_voice_streams( Channel &channel );

        struct RuntimeAudioCue
        {
            std::filesystem::path path;
            std::string codec = "wav";
            std::int64_t frame_count = 0;
            std::int64_t loop_start_frame = -1;
            std::int64_t loop_end_frame = -1;
            std::int32_t sample_rate = 0;
            std::int32_t channel_count = 0;
        };

        struct AsyncDecodeJob
        {
            std::string name;
            RuntimeAudioCue runtime_audio;
        };

        struct AsyncDecodeResult
        {
            std::string name;
            runtime::audio::Pcm16 pcm;
            bool ok = false;
            std::string error;
        };

        bool load_runtime_audio_directory( const std::filesystem::path &path,
            bool replace_existing,
            std::string_view replace_cue = {},
            std::string_view cue_prefix = {} );

        [[nodiscard]] runtime::audio::Pcm16 load_runtime_audio( const RuntimeAudioCue &cue ) const;
        [[nodiscard]] runtime::audio::Pcm16 load_runtime_wav( const std::filesystem::path &path ) const;
        [[nodiscard]] const RuntimeAudioCue *runtime_audio_cue( std::string_view name ) const;

        bool ensure_async_worker_started();
        void stop_async_worker();
        void async_worker_main();
        void consume_async_preload_results();

        bool ensure_device_for_pcm( const runtime::audio::Pcm16 &pcm );
        SDL_AudioStream *create_bound_stream_for_pcm( const runtime::audio::Pcm16 &pcm );
        bool put_stereo_pcm_data( SDL_AudioStream *stream, const std::vector<std::int16_t> &stereo );
        [[nodiscard]] std::int64_t mix_subsong_frames_to_stereo( Channel &channel,
            const runtime::audio::Pcm16 &pcm,
            std::int64_t source_frame,
            std::int64_t frame_count,
            std::vector<std::int16_t> &out_stereo );

        [[nodiscard]] Channel *channel_for( AudioChannelHandle handle );
        [[nodiscard]] const Channel *channel_for( AudioChannelHandle handle ) const;
        [[nodiscard]] const runtime::audio::Pcm16 *cached_pcm( std::string_view name ) const;
        [[nodiscard]] runtime::audio::Pcm16 *cached_pcm( std::string_view name );

    private:
        bool audio_bank_loaded_ = false;
        std::unordered_map<std::string, RuntimeAudioCue> runtime_audio_cues_;
        std::filesystem::path application_root_;
        std::filesystem::path ffmpeg_executable_;
        std::filesystem::path loaded_audio_bank_path_{};

        std::unordered_map<std::string, runtime::audio::Pcm16> decoded_cache_;

        mutable std::mutex async_mutex_;
        std::condition_variable async_cv_;
        std::thread async_worker_;
        bool async_worker_running_ = false;
        bool async_worker_stop_ = false;
        std::deque<AsyncDecodeJob> async_jobs_;
        std::deque<AsyncDecodeResult> async_results_;
        std::unordered_set<std::string> async_pending_names_;

        SDL_AudioDeviceID device_ = 0;
        std::int32_t sample_rate_ = 0;
        std::int32_t channels_ = 0;
        bool playing_ = false;
        bool stereo_output_ = true;
        bool background_fx_enabled_ = true;
        std::int32_t instrument_routing_ = 0;

        std::vector<Channel> channels_storage_;

        std::string last_error_;
    };
}
