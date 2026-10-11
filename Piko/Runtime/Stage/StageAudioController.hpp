#pragma once

#include "FileTypes/CHC/CHC.hpp"
#include "Managers/AudioManager.hpp"
#include "Runtime/Stage/StageRuntime.hpp"
#include "Runtime/Audio/InstrumentAudio.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace piko::runtime::stage
{
    class StageAudioController
    {
    public:
        bool initialize( managers::AudioManager &audio,
            const filetypes::chc::Song &song,
            const std::filesystem::path &audio_bank_path,
            const std::filesystem::path &fallback_audio_bank_path,
            StageRuntime &runtime,
            bool stereo_phrases );

        void shutdown( managers::AudioManager &audio );

        void update( managers::AudioManager &audio, StageRuntime &runtime );

        void set_paused( managers::AudioManager &audio, bool paused );
        void restart_from_runtime( managers::AudioManager &audio, const StageRuntime &runtime );
        bool play_result_cue( managers::AudioManager &audio, bool won );
        void stop_song( managers::AudioManager &audio );

        [[nodiscard]] bool active() const;
        [[nodiscard]] bool using_audio_clock() const;
        [[nodiscard]] std::int64_t audio_global_sample( const managers::AudioManager &audio ) const;
        [[nodiscard]] std::int64_t queued_until_global_sample() const;
        [[nodiscard]] const std::string &status_line() const;
        [[nodiscard]] managers::AudioChannelMixDebug audio_mix_debug( const managers::AudioManager &audio ) const;

        void flush_runtime_audio_events( managers::AudioManager &audio, StageRuntime &runtime );

    private:
        static constexpr std::int64_t samples_per_second = 48000;
        static constexpr std::int64_t async_preload_horizon_samples = samples_per_second * 8;
        static constexpr std::int64_t queue_ready_lead_samples = samples_per_second * 3;
        static constexpr std::int64_t forced_commit_queue_lead_samples = samples_per_second * 3;

        [[nodiscard]] const filetypes::chc::Cue *cue_for_section( std::size_t section_index ) const;
        [[nodiscard]] std::string cue_name_for_section( std::size_t section_index ) const;
        [[nodiscard]] std::int64_t section_duration( std::size_t section_index ) const;
        [[nodiscard]] std::int64_t section_playback_limit( std::size_t section_index ) const;
        [[nodiscard]] std::int64_t current_audio_sample( const managers::AudioManager &audio ) const;
        [[nodiscard]] std::string resolve_result_cue_name( const managers::AudioManager &audio, bool won ) const;

        bool prepare_result_cues( managers::AudioManager &audio );

        bool start_current_section( managers::AudioManager &audio, const StageRuntime &runtime );

        bool queue_section_if_ready( managers::AudioManager &audio,
            std::size_t section_index,
            bool allow_sync_fallback );

        void request_preload_for_section( managers::AudioManager &audio, std::size_t section_index );

        void request_preload_candidates( managers::AudioManager &audio, const StageRuntime &runtime );

        void handle_runtime_audio_events( managers::AudioManager &audio, const StageRuntime &runtime );

        [[nodiscard]] std::uint32_t instrument_voice_mask( const StageRuntime &runtime, std::size_t source ) const;

        void reset_instrument_controls( managers::AudioManager &audio, const StageRuntime &runtime );
        void publish_instrument_volumes( managers::AudioManager &audio );

        void update_status( const managers::AudioManager &audio, const StageRuntime &runtime );

    private:
        const filetypes::chc::Song *song_ = nullptr;
        managers::AudioChannelHandle channel_{};
        managers::AudioChannelHandle result_channel_{};

        bool initialized_ = false;
        bool failed_ = false;
        bool paused_ = false;
        bool result_cue_played_ = false;

        std::int64_t stream_global_sample_base_ = 0;
        std::int64_t queued_until_global_sample_ = 0;
        std::optional<std::size_t> last_queued_section_index_{};
        std::uint64_t last_audio_event_serial_ = 0;
        float player_stem_target_gain_ = 1.0f;
        std::array<runtime::audio::InstrumentVolume, 8> instrument_volumes_{};
        std::int64_t next_instrument_update_sample_ = 0;
        std::uint32_t controlled_voice_mask_ = 0;
        StageDuetSide instrument_duet_side_ = StageDuetSide::Auto;
        managers::AudioPhraseStemMode phrase_stem_mode_ = managers::AudioPhraseStemMode::StereoCh2Ch4;

        std::string win_result_cue_name_;
        std::string lose_result_cue_name_;
        std::string result_cue_status_;
        std::string status_line_;
        std::vector<std::int64_t> section_durations_{};
    };
}
