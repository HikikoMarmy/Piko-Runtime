#include "Utils/EnumUtil.hpp"
#pragma once

#include "FileTypes/CHC/CHC.hpp"
#include "GameData/StageCatalog.hpp"
#include "Runtime/Stage/StageConditionResolver.hpp"
#include "Runtime/Stage/StagePlayerState.hpp"
#include "Runtime/Stage/StageResultData.hpp"
#include "Runtime/Stage/StageTraceBuilder.hpp"
#include "Runtime/Stage/StageInstrumentState.hpp"
#include "Runtime/Audio/InstrumentPlayback.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <optional>
#include <string>
#include <vector>
#include <span>

namespace piko::runtime::stage
{
    struct RuntimeChart
    {
        std::size_t chart_index = 0;
        const filetypes::chc::Chart *chart = nullptr;
        std::vector<TraceSample> trace_samples;

        std::int64_t event_start_section_sample = 0;
        std::int64_t event_end_section_sample = 0;
    };

    struct RuntimeSection
    {
        std::size_t section_index = 0;
        const filetypes::chc::SongSection *section = nullptr;
        std::vector<RuntimeChart> charts;

        bool has_guard_notes = false;
        std::int64_t first_guard_section_sample = -1;
        std::int64_t last_guard_section_sample = -1;
    };

    enum class StageInputEventKind : std::uint8_t
    {
        ActionPress,
        ActionRelease,
        GuardPress
    };

    struct StageInputEvent
    {
        StageInputEventKind kind = StageInputEventKind::ActionPress;
        std::uint32_t guard_button = 0;
        float frame_position = 1.0f;
    };

    struct AimWindow
    {
        float half_angle_degrees = 31.5f;
        float minimum_alignment = 0.0f;
    };

    [[nodiscard]] AimWindow aim_window( float snap_half_angle_degrees, bool holding_phrase );

    struct StageInputSnapshot
    {
        bool automatic_notes = false;

        bool action_down = false;
        bool action_pressed = false;
        bool action_released = false;

        std::array<bool, 4> guard_down{};
        std::array<bool, 4> guard_pressed{};
        std::vector<StageInputEvent> events{};

        float aim_x = 0.0f;
        float aim_y = 0.0f;
        float aim_strength = 0.0f;
        float aim_lock_half_angle_degrees = 31.5f;
        float aim_target_scale_x = 1.0f;
        float aim_target_scale_y = 1.0f;
    };

    enum class StageNoteJudgementKind
    {
        None,
        PhraseStartOk,
        PhraseEndOk,
        PhraseMiss,
        GuardOk,
        GuardMiss
    };

    enum class StageAudioEventKind
    {
        None,
        InstrumentMuted,
        InstrumentRecovered
    };

    struct StageAudioEventState
    {
        StageAudioEventKind kind = StageAudioEventKind::None;
        std::uint64_t serial = 0;
        std::int64_t global_sample = 0;
        std::size_t source_player_index = 0;
    };

    struct StagePhraseVisualState
    {
        std::size_t source_player_index = 0;
        std::size_t chart_index = 0;
        std::int64_t start_section_sample = 0;
        std::int64_t end_section_sample = 0;
        bool darkened = false;
        bool held = false;
    };

    struct StagePhraseHitVisualState
    {
        std::size_t source_player_index = 0;
        std::size_t chart_index = 0;
        std::int64_t start_section_sample = 0;
        std::int64_t hit_section_sample = 0;
        float direction_x = 1.0f;
        float direction_y = 0.0f;
    };

    struct StageGuardHitVisualState
    {
        std::size_t source_player_index = 0;
        std::size_t chart_index = 0;
        std::int64_t start_section_sample = 0;
        std::uint32_t button = 0;
        std::int64_t hit_section_sample = 0;
    };

    struct StageJudgementDebugState
    {
        bool enabled = false;
        std::string last_event;
        std::int64_t last_event_sample = 0;

        std::string center_event;
        std::string center_detail;
        std::int64_t center_event_sample = 0;

        std::size_t section_index = 0;
        std::int64_t section_sample = 0;
        std::size_t phrase_note_count = 0;
        std::size_t guard_note_count = 0;
        std::size_t pending_phrase_count = 0;
        std::size_t pending_guard_count = 0;

        std::string last_event_kind;
        std::int64_t last_timing_delta_samples = 0;

        std::int32_t active_phrase_index = -1;
        std::int64_t active_phrase_start_sample = 0;
        std::int64_t active_phrase_end_sample = 0;
        std::uint32_t active_phrase_action_descriptor = 0;
        bool active_phrase_started = false;
        bool active_phrase_completed = false;
        bool active_phrase_missed = false;

        std::uint64_t ssq_action_result_serial = 0;
        std::uint32_t ssq_action_result_state = 0;
        std::uint32_t ssq_action_result_descriptor = 0;
        float ssq_action_result_value = 0.0f;
        bool ssq_action_result_flag4 = true;
        std::string ssq_action_result_kind;

        std::int32_t next_phrase_index = -1;
        std::size_t next_phrase_source_player_index = 0;
        std::size_t next_phrase_chart_index = 0;
        std::int64_t next_phrase_chart_start_sample = 0;
        std::int64_t next_phrase_start_sample = 0;
        bool next_phrase_window_open = false;

        std::int32_t next_guard_index = -1;
        std::int64_t next_guard_sample = 0;
        std::int32_t next_guard_button = -1;
        std::int32_t last_guard_button = -1;
        bool next_guard_window_open = false;

        float aim_x = 0.0f;
        float aim_y = 0.0f;
        float aim_strength = 0.0f;
        float aim_alignment = 0.0f;

        std::string phrase_tail_status;
        float last_result_value = 0.0f;
        float last_result_hold_value = 0.0f;
        float last_result_delta = 0.0f;
        float last_result_meter_delta = 0.0f;
        float last_result_press_factor = 0.0f;
        float last_result_release_factor = 0.0f;
        std::size_t last_result_side_index = static_cast<std::size_t>( -1 );
        bool last_result_energy_applied = false;
        float last_result_energy_before = 0.0f;
        float last_result_energy_after = 0.0f;
    };

    struct RuntimeSong
    {
        const filetypes::chc::Song *song = nullptr;
        std::vector<RuntimeSection> sections;
    };

    enum class StageActionMode
    {
        Intro,
        Charge,
        Attack,
        Guard,
        Harmony,
        End,
        Unknown
    };

    struct ActiveChart
    {
        std::size_t source_player_index = 0;
        std::size_t chart_index = 0;
        const filetypes::chc::Chart *chart = nullptr;
        const std::vector<TraceSample> *trace_samples = nullptr;
        std::int64_t chart_sample = 0;
    };

    struct StageActorActionState
    {
        std::size_t source_player_index = 0;
        std::size_t chart_index = 0;
        bool chart_active = false;
        bool has_aim = false;
        float aim_x = 1.0f;
        float aim_y = 0.0f;
        bool phrase_active = false;
        bool phrase_started = false;
        std::int64_t phrase_start_section_sample = 0;
        std::int64_t phrase_end_section_sample = 0;
        std::int64_t phrase_authored_duration_samples = 0;
        std::uint32_t action_descriptor = 0;
        std::uint64_t action_serial = 0;
        std::uint64_t phrase_token = 0;
        StageActionMode action_mode = StageActionMode::Unknown;
        std::uint32_t battle_phase = 0xFFFFFFFFu;
    };

    enum class StageDuetSide
    {
        Auto,
        Primary,
        Partner,
        Both,
        Combined
    };

    struct StageRuntimeConfig
    {
        std::size_t player_index = 0;

        bool use_unavailable_partner_charts = true;
        bool filter_charts_by_time = false;
        bool duet_stage = false;
        StageDuetSide duet_side = StageDuetSide::Auto;

        float transition_route_lock_fraction = 0.5f;

        std::int64_t chart_preroll_samples = 48000 * 8;
        std::int64_t chart_postroll_samples = 48000 * 2;
    };

    struct StageTransitionPlan
    {
        float latched_random_value = 0.0f;

        bool prediction_valid = false;
        StageSectionTransition predicted_transition{};

        bool committed = false;
        StageSectionTransition committed_transition{};

        std::int64_t route_lock_sample = 0;
    };

    struct StageVisualState
    {
        StageActionMode mode = StageActionMode::Unknown;
        bool guard_phase = false;
        bool vision_enabled = true;
        bool has_guard_notes = false;
        std::int64_t first_guard_section_sample = -1;
        std::int64_t last_guard_section_sample = -1;
    };

    struct StageRuntimeState
    {
        std::size_t section_index = 0;
        std::int64_t section_sample = 0;
        std::int64_t global_sample = 0;
        bool song_finished = false;

        std::vector<ActiveChart> active_charts;
        std::array<StageActorActionState, 4> actor_actions{};
    };

    [[nodiscard]] const char *stage_action_mode_name( StageActionMode mode );

    [[nodiscard]] const char *stage_duet_side_name( StageDuetSide side );

    [[nodiscard]] StageActionMode stage_action_mode_from_phase( std::uint32_t phase );

    [[nodiscard]] RuntimeSong build_runtime_song( const filetypes::chc::Song &song,
        const StageTraceBuildOptions &trace_options = {} );

    class StageRuntime
    {
    public:
        [[nodiscard]] gamedata::ChartStyle chart_style() const
        {
            return chart_style_;
        }

        StageRuntime();

        void initialize( const gamedata::PlayableStageEntry &stage,
            const filetypes::chc::Song *song,
            bool mirror_chart = false );

        void start( std::size_t section_index = 0, std::int64_t global_sample = 0 );
        void update( double delta_seconds );
        void update_from_global_sample( std::int64_t global_sample );

        void set_paused( bool paused );
        void toggle_paused();
        [[nodiscard]] bool paused() const;

        void set_duet_side( StageDuetSide side );
        [[nodiscard]] StageDuetSide duet_side() const;
        [[nodiscard]] bool duet_stage() const;

        void seek_relative_samples( std::int64_t sample_delta );
        void notify_condition_state_changed();
        void update_judgement( const StageInputSnapshot &input );
        [[nodiscard]] bool holding_phrase_for_chart( const ActiveChart &chart ) const;
        void set_local_energy_immediate( float value );
        void set_charge_recovery_enabled( bool enabled );
        void set_instrument_playback_mode( audio::InstrumentPlaybackMode mode );
        void set_game_modifiers( bool no_death,
            bool only_aim,
            bool all_sections,
            float player_damage_multiplier = 1.0f,
            float enemy_damage_multiplier = 1.0f );

        void set_forced_section_loop( std::optional<std::size_t> section_index, bool reset_energy );

        [[nodiscard]] bool has_song() const;
        [[nodiscard]] const RuntimeSong &runtime_song() const;

        [[nodiscard]] const StageRuntimeState &state() const;
        [[nodiscard]] std::uint32_t score() const;
        [[nodiscard]] StageResultData result_data( bool won ) const;
        [[nodiscard]] const StageTransitionPlan &transition_plan() const;
        [[nodiscard]] StageVisualState visual_state() const;
        [[nodiscard]] StageActionMode current_action_mode() const;
        [[nodiscard]] StageActionMode section_action_mode( std::size_t section_index ) const;

        [[nodiscard]] const StageSideArray &sides() const;
        [[nodiscard]] StageSideArray &sides();
        [[nodiscard]] std::size_t active_side_count() const;

        [[nodiscard]] StageConditionState condition_state() const;
        [[nodiscard]] const StageJudgementDebugState &judgement_debug_state() const;
        [[nodiscard]] std::vector<StagePhraseVisualState> phrase_visual_states() const;
        [[nodiscard]] std::vector<StagePhraseHitVisualState> phrase_hit_visual_states() const;
        [[nodiscard]] std::vector<StageGuardHitVisualState> guard_hit_visual_states() const;
        [[nodiscard]] StageAudioEventState audio_event_state() const;

        void discard_audio_events_through( std::uint64_t serial );

        [[nodiscard]] std::span<const StageAudioEventState> audio_events() const
        {
            return audio_events_;
        }

        [[nodiscard]] std::uint32_t instrument_voice_mask( std::size_t source, bool stereo ) const;
        [[nodiscard]] std::uint32_t instrument_controlled_voices( bool stereo ) const;
        [[nodiscard]] float instrument_gain_scale( std::size_t source ) const;
        [[nodiscard]] bool instrument_secondary_source( std::size_t source ) const;

        [[nodiscard]] std::vector<std::int64_t> timing_cue_samples() const;

        void set_section_duration_override( std::size_t section_index, std::int64_t duration_samples );
        [[nodiscard]] std::int64_t current_section_duration_samples() const;
        [[nodiscard]] std::int64_t current_section_remaining_samples() const;

        [[nodiscard]] std::vector<std::size_t> collect_possible_next_sections() const;
        [[nodiscard]] std::vector<ActiveChart> collect_preview_charts_for_section( std::size_t section_index,
            std::int64_t preview_section_sample = 0 ) const;

        [[nodiscard]] const StageSectionTransition &predicted_transition();
        const StageSectionTransition &commit_current_transition();
        [[nodiscard]] bool transition_is_committed() const;
        [[nodiscard]] std::int64_t transition_route_lock_sample() const;

    private:
        struct RuntimePhraseGroup
        {
            std::int64_t start_sample = 0;
            std::int64_t end_sample = 0;
            std::int64_t authored_duration_samples = 0;
            std::uint32_t character_animation = 0;
        };

        struct SourceNoteCounts
        {
            std::size_t phrases = 0;
            std::size_t guards = 0;

            [[nodiscard]] std::size_t total() const;
        };

        struct JudgementQuality
        {
            const char *label = "ok";
            float value = 0.75f;
        };

        struct OfficialNoteResult;
        struct PhraseJudgementNote;
        struct GuardJudgementNote;

        [[nodiscard]] static const filetypes::chc::EnergyDamageFactors &factors_for_phase(
            const filetypes::chc::PlayerEnergyDamageFactors &player,
            std::uint32_t phase );

        [[nodiscard]] static float sanitized_meter_value( float value );
        [[nodiscard]] static float meter_delta_from_raw_delta( float maximum, float delta );
        [[nodiscard]] static float step_towards( float current, float target, float step );
        [[nodiscard]] static float safe_factor( float value );
        [[nodiscard]] static std::uint32_t score_points_from_float( float value );
        [[nodiscard]] static std::uint32_t score_press_points( float judgement_value );
        [[nodiscard]] static std::uint32_t score_tail_points( float hold_value,
            float judgement_value,
            float release_factor );

        [[nodiscard]] static std::int64_t abs_i64( std::int64_t value );
        [[nodiscard]] static JudgementQuality timing_judgement_quality( std::int64_t actual_sample,
            std::int64_t target_sample,
            std::int64_t ok_window_samples,
            std::int64_t great_window_samples );

        [[nodiscard]] static JudgementQuality phrase_judgement_quality( std::int64_t actual_sample,
            std::int64_t target_sample,
            std::int64_t ok_window_samples,
            std::int64_t great_window_samples,
            float alignment,
            float minimum_alignment );

        [[nodiscard]] static std::vector<RuntimePhraseGroup> build_runtime_phrase_groups(
            const filetypes::chc::Chart &chart );

        [[nodiscard]] static std::size_t local_side_for_source_player( std::size_t source_player_index,
            std::size_t configured_player_index );

        [[nodiscard]] static std::size_t opponent_for_side( std::size_t side_index );
        [[nodiscard]] static bool is_attack_like_mode( StageActionMode mode );

        void initialize_sides( const gamedata::PlayableStageEntry &stage );
        void advance_sections_if_needed( std::int64_t global_sample );
        void rebuild_active_charts();
        void rebuild_actor_actions();
        void reset_actor_actions();
        void update_automated_duet_contributions();
        void finalize_automated_duet_contributions();
        [[nodiscard]] bool is_automated_duet_source( std::size_t source_player_index ) const;
        void apply_automated_duet_phrase_start( const StageActorActionState &action );
        void apply_automated_duet_phrase_end( const StageActorActionState &action );

        void add_player_chart_block( const RuntimeSection &runtime_section,
            std::size_t source_player_index,
            std::int64_t section_sample,
            std::vector<ActiveChart> &out_charts ) const;

        void add_selected_chart_blocks( const RuntimeSection &runtime_section,
            std::int64_t section_sample,
            std::vector<ActiveChart> &out_charts ) const;

        [[nodiscard]] std::array<std::size_t, 2> selected_chart_sources( std::size_t &source_count ) const;

        void selected_guard_range( const RuntimeSection &runtime_section,
            std::int64_t section_sample,
            bool &has_guard_notes,
            std::int64_t &first_guard_section_sample,
            std::int64_t &last_guard_section_sample,
            bool &guard_phase ) const;

        [[nodiscard]] bool should_include_chart( const RuntimeChart &runtime_chart, std::int64_t section_sample ) const;

        [[nodiscard]] bool should_include_duet_chart( const RuntimeSection &runtime_section,
            std::size_t source_player_index,
            std::size_t chart_index ) const;

        void enter_section( std::size_t section_index );
        void rebuild_judgement_notes_for_current_section();
        void set_side_energy_immediate( std::size_t side_index, float value );
        void update_visible_energy_gauges( std::int64_t elapsed_samples );

        struct EnergyApplyResult
        {
            std::size_t side_index = static_cast<std::size_t>( -1 );
            bool attempted = false;
            bool applied = false;
            bool side_active = false;
            float requested_delta = 0.0f;
            float applied_delta = 0.0f;
            float before_logical = 0.0f;
            float after_logical = 0.0f;
            float before_visible = 0.0f;
            float after_visible = 0.0f;
            float before_pending = 0.0f;
            float after_pending = 0.0f;
            const char *skip_reason = "not_attempted";
        };

        struct PhraseHoldResult
        {
            float value = 0.0f;
            float tail_scalar = 0.0f;
        };

        EnergyApplyResult apply_energy_delta( std::size_t side_index, float delta );
        void add_score_points( std::uint32_t points );
        void apply_note_result_score( const OfficialNoteResult &result,
            const filetypes::chc::EnergyDamageFactors &factors,
            StageActionMode mode,
            std::size_t local_side );

        void apply_note_result_damage( const OfficialNoteResult &result );
        [[nodiscard]] PhraseHoldResult phrase_hold_result_for_end_sample( const PhraseJudgementNote &note,
            std::int64_t actual_end_sample ) const;

        void finalize_active_phrases_at_section_sample( std::int64_t section_sample );
        void apply_phrase_start_success( const PhraseJudgementNote &note );
        void apply_phrase_end_success( const PhraseJudgementNote &note, const PhraseHoldResult &hold_result );

        void apply_phrase_miss( const PhraseJudgementNote &note );
        void emit_audio_event( StageAudioEventKind kind, std::size_t source_player_index );
        [[nodiscard]] bool is_local_phrase_source( std::size_t source_player_index ) const;
        void mute_instrument_controls( std::int64_t global_sample );
        void rebuild_instrument_charts();
        void update_instrument_control( const StageInputSnapshot &input, std::int64_t frame_start_sample );
        void update_instrument_sample( std::int64_t section_sample, const StageInputSnapshot &input );

        void record_guard_hit_visual( const GuardJudgementNote &note );
        void record_phrase_hit_visual( const PhraseJudgementNote &note,
            std::int64_t hit_section_sample,
            const StageInputSnapshot &input );

        void prune_phrase_hit_visuals();
        void prune_guard_hit_visuals();

        void apply_guard_success( const GuardJudgementNote &note, float judgement_value );
        void apply_guard_miss( const GuardJudgementNote &note );
        void record_ssq_action_result( std::uint32_t state,
            std::uint32_t descriptor,
            float value,
            bool flag4,
            const char *kind );

        void set_center_judgement( const char *label,
            const char *kind = nullptr,
            std::int64_t timing_delta_samples = 0 );

        [[nodiscard]] float aim_alignment_for_chart_sample( const std::vector<TraceSample> &samples,
            std::int64_t chart_sample,
            const StageInputSnapshot &input ) const;

        void reset_transition_plan();
        void invalidate_transition_prediction();
        void ensure_transition_prediction();
        [[nodiscard]] std::size_t branch_merge_section( const std::vector<std::size_t> &routes ) const;
        [[nodiscard]] std::size_t next_unplayed_section( std::size_t start ) const;
        void lock_transition_if_needed();
        [[nodiscard]] std::int64_t section_duration_samples( std::size_t section_index ) const;
        [[nodiscard]] bool route_can_still_change() const;
        [[nodiscard]] std::int64_t calculate_route_lock_sample() const;
        [[nodiscard]] std::int64_t last_guard_section_sample( std::size_t section_index ) const;

        [[nodiscard]] bool player3_available() const;
        [[nodiscard]] bool player4_available() const;

        [[nodiscard]] float roll_transition_random();

    private:
        static constexpr std::int64_t sample_rate = 48000;
        static constexpr std::int64_t instrument_update_samples = sample_rate / 60;
        static constexpr float psp_secondary_gain = 0.8f;
        static constexpr std::int64_t condition_setup_lead_samples = sample_rate * 3;

        enum class OfficialNoteResultKind
        {
            None = 0,

            ChargePress = 1,
            ChargeHoldOrRelease = 2,
            AttackPress = 3,
            AttackHoldOrRelease = 4,
            GuardSuccess = 5,
            GuardMiss = 6,

            AttackPhraseMiss
        };

        struct OfficialNoteResult
        {
            OfficialNoteResultKind kind = OfficialNoteResultKind::None;
            std::size_t source_player_index = 0;
            float value = 0.0f;
            float hold_value = 0.0f;

            StageActionMode action_mode = StageActionMode::Unknown;
            std::uint32_t battle_phase = 0xFFFFFFFFu;
            bool award_score = true;
        };

        enum class PhraseJudgementStatus
        {
            Pending,
            Active,
            Completed,
            Missed
        };

        struct PhraseJudgementNote
        {
            std::size_t source_player_index = 0;
            std::size_t chart_index = 0;
            const filetypes::chc::Chart *chart = nullptr;
            const std::vector<TraceSample> *trace_samples = nullptr;
            std::int64_t chart_start_sample = 0;
            std::int64_t start_section_sample = 0;
            std::int64_t end_section_sample = 0;
            std::int64_t authored_duration_samples = 0;
            std::int64_t actual_start_section_sample = 0;
            std::int64_t trace_loss_start_section_sample = -1;
            float start_value = 1.0f;
            float last_alignment = 0.0f;
            StageActionMode start_action_mode = StageActionMode::Unknown;
            std::uint32_t start_battle_phase = 0xFFFFFFFFu;
            bool has_hold_tail = false;
            std::uint32_t character_animation = 0;
            PhraseJudgementStatus status = PhraseJudgementStatus::Pending;
        };

        enum class GuardJudgementStatus
        {
            Pending,
            Hit,
            Missed
        };

        struct GuardJudgementNote
        {
            std::size_t source_player_index = 0;
            std::size_t chart_index = 0;
            std::int64_t start_section_sample = 0;
            std::uint32_t button = 0;
            GuardJudgementStatus status = GuardJudgementStatus::Pending;
        };

        RuntimeSong runtime_song_{};
        StageRuntimeConfig config_{};
        gamedata::ChartStyle chart_style_ = gamedata::ChartStyle::GitarooMan;

        StageSideArray sides_{};

        StageRuntimeState state_{};
        std::uint32_t score_ = 0;
        std::array<std::uint32_t, utils::enum_index( StageResultGrade::Count )> result_judgement_counts_{};

        bool paused_ = false;
        bool charge_recovery_enabled_ = true;
        bool no_death_mode_ = false;
        bool only_aim_mode_ = false;
        bool all_sections_mode_ = false;
        float player_damage_multiplier_ = 1.0f;
        float enemy_damage_multiplier_ = 1.0f;
        std::vector<std::uint32_t> section_visit_counts_{};

        struct BranchPlayback
        {
            std::vector<std::size_t> routes;
            std::size_t merge_section = 0;
            std::size_t current_route = 0;
        };

        std::vector<BranchPlayback> branch_playback_{};
        std::optional<BranchPlayback> predicted_branch_start_{};
        std::optional<std::size_t> predicted_branch_advance_index_{};
        std::size_t predicted_branch_advance_route_index_ = 0;
        std::size_t predicted_branch_complete_count_ = 0;
        std::optional<std::size_t> forced_section_loop_index_{};
        bool reset_energy_on_forced_loop_ = false;
        std::array<float, 4> forced_loop_energy_{};
        std::int64_t section_start_global_sample_ = 0;
        std::vector<std::int64_t> section_duration_overrides_{};

        StageConditionResolver condition_resolver_{};
        StageTransitionPlan transition_plan_{};

        std::vector<PhraseJudgementNote> phrase_judgement_notes_{};
        std::vector<GuardJudgementNote> guard_judgement_notes_{};
        std::vector<StagePhraseHitVisualState> phrase_hit_visuals_{};
        std::vector<StageGuardHitVisualState> guard_hit_visuals_{};
        StageJudgementDebugState judgement_debug_{};
        StageAudioEventState audio_event_{};
        std::vector<StageAudioEventState> audio_events_;

        struct InstrumentChart
        {
            const RuntimeChart *chart = nullptr;
            std::size_t source = 0;
            StageInstrumentState state;
        };

        audio::InstrumentPlaybackMode instrument_playback_mode_ = audio::InstrumentPlaybackMode::Original;
        bool instrument_mode_changed_ = false;
        std::vector<InstrumentChart> instrument_charts_;
        std::array<InstrumentResultController, 4> instrument_result_controllers_{};
        std::array<InstrumentCommand, 4> instrument_commands_{};
        std::int64_t next_instrument_section_sample_ = 0;
        StageInputSnapshot previous_instrument_input_{};
        std::size_t judgement_section_index_ = static_cast<std::size_t>( -1 );
        std::int64_t last_judgement_update_section_sample_ = 0;
        std::array<std::uint64_t, 4> actor_phrase_tokens_{};
        std::array<bool, 4> automated_duet_contribution_active_{};
        std::array<StageActorActionState, 4> automated_duet_contribution_actions_{};
        std::uint64_t next_actor_action_serial_ = 1ull << 32u;

        std::mt19937 rng_;
        std::uniform_real_distribution<float> random_01_;
    };
}
