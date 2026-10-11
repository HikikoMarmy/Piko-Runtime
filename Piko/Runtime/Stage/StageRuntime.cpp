#include "Utils/EnumUtil.hpp"
#include "StageRuntime.hpp"

#include "Utils/MathUtil.hpp"

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace piko::runtime::stage
{
    std::size_t StageRuntime::SourceNoteCounts::total() const
    {
        return phrases + guards;
    }

    const filetypes::chc::EnergyDamageFactors &StageRuntime::factors_for_phase(
        const filetypes::chc::PlayerEnergyDamageFactors &player,
        std::uint32_t phase )
    {
        switch( phase )
        {
            case 0:
                return player.intro;
            case 1:
                return player.charge;
            case 2:
                return player.battle;
            case 4:
                return player.harmony;
            case 5:
                return player.end;
            default:
                return player.battle;
        }
    }

    float StageRuntime::sanitized_meter_value( float value )
    {
        if( !std::isfinite( value ) )
        {
            return 0.0f;
        }

        return std::max( 0.0f, value );
    }

    float StageRuntime::meter_delta_from_raw_delta( float maximum, float delta )
    {
        if( !std::isfinite( delta ) )
        {
            return delta;
        }

        if( maximum <= 1.0001f && std::fabs( delta ) > 1.5f )
        {
            return delta * 0.01f;
        }

        return delta;
    }

    float StageRuntime::step_towards( float current, float target, float step )
    {
        if( step <= 0.0f || current == target )
        {
            return current;
        }

        if( current < target )
        {
            return std::min( target, current + step );
        }

        return std::max( target, current - step );
    }

    float StageRuntime::safe_factor( float value )
    {
        return std::isfinite( value ) ? value : 0.0f;
    }

    std::uint32_t StageRuntime::score_points_from_float( float value )
    {
        constexpr std::uint32_t score_accumulator_max = std::numeric_limits<std::uint32_t>::max();

        if( !std::isfinite( value ) || value <= 0.0f )
        {
            return 0;
        }

        const double rounded = std::round( static_cast<double>( value ) );

        if( rounded <= 0.0 )
        {
            return 0;
        }

        if( rounded >= static_cast<double>( score_accumulator_max ) )
        {
            return score_accumulator_max;
        }

        return static_cast<std::uint32_t>( rounded );
    }

    std::uint32_t StageRuntime::score_press_points( float judgement_value )
    {
        return score_points_from_float( judgement_value * 100.0f );
    }

    std::uint32_t StageRuntime::score_tail_points( float hold_value, float judgement_value, float release_factor )
    {
        return score_points_from_float(
            hold_value * judgement_value * judgement_value * 50.0f * safe_factor( release_factor ) );
    }

    std::int64_t StageRuntime::abs_i64( std::int64_t value )
    {
        return value < 0 ? -value : value;
    }

    StageRuntime::JudgementQuality StageRuntime::timing_judgement_quality( std::int64_t actual_sample,
        std::int64_t target_sample,
        std::int64_t ok_window_samples,
        std::int64_t great_window_samples )
    {
        const std::int64_t raw_delta = actual_sample - target_sample;
        const std::int64_t delta = raw_delta < 0 ? -raw_delta : raw_delta;
        static_cast<void>( great_window_samples );

        if( delta > ok_window_samples || ok_window_samples <= 0 )
        {
            return JudgementQuality{ "miss", 0.0f };
        }

        const float value =
            std::clamp( 1.0f - static_cast<float>( delta ) / static_cast<float>( ok_window_samples ), 0.0f, 1.0f );

        if( value > 0.8f )
        {
            return JudgementQuality{ "great", value };
        }

        if( value > 0.5f )
        {
            return JudgementQuality{ "good", value };
        }

        return JudgementQuality{ "ok", value };
    }

    StageRuntime::JudgementQuality StageRuntime::phrase_judgement_quality( std::int64_t actual_sample,
        std::int64_t target_sample,
        std::int64_t ok_window_samples,
        std::int64_t great_window_samples,
        float alignment,
        float minimum_alignment )
    {
        JudgementQuality quality =
            timing_judgement_quality( actual_sample, target_sample, ok_window_samples, great_window_samples );

        if( quality.value <= 0.0f || alignment < minimum_alignment )
        {
            return JudgementQuality{ "miss", 0.0f };
        }

        const float aim_t =
            piko::utils::clamp01( ( alignment - minimum_alignment ) / std::max( 0.0001f, 1.0f - minimum_alignment ) );

        const float value =
            alignment >= 0.80f ? quality.value : std::min( quality.value, std::max( 0.65f, 0.70f + aim_t * 0.20f ) );

        if( value > 0.8f )
        {
            return JudgementQuality{ "great", value };
        }

        if( value > 0.5f )
        {
            return JudgementQuality{ "good", value };
        }

        return JudgementQuality{ "ok", value };
    }

    std::vector<StageRuntime::RuntimePhraseGroup> StageRuntime::build_runtime_phrase_groups(
        const filetypes::chc::Chart &chart )
    {
        std::vector<RuntimePhraseGroup> groups;
        RuntimePhraseGroup current{};
        bool in_group = false;

        const auto flush_group = [ & ]()
        {
            if( !in_group )
            {
                return;
            }

            if( current.end_sample > current.start_sample )
            {
                groups.push_back( current );
            }

            current = {};

            in_group = false;
        };

        for( const filetypes::chc::PhraseFragment &fragment : chart.phrases )
        {
            const std::int64_t start = static_cast<std::int64_t>( fragment.start_sample );
            const std::int64_t end = start + static_cast<std::int64_t>( fragment.duration_samples );

            if( end <= start )
            {
                continue;
            }

            const bool starts_phrase = fragment.phrase_start != 0;
            const bool ends_phrase = fragment.phrase_end != 0;

            if( starts_phrase || !in_group )
            {
                flush_group();
                current = RuntimePhraseGroup{
                    start,
                    end,
                    static_cast<std::int64_t>( fragment.duration_samples ),
                    std::min<std::uint32_t>( fragment.animation, 2u )
                };

                in_group = true;
            }
            else
            {
                current.end_sample = std::max( current.end_sample, end );

                current.authored_duration_samples += static_cast<std::int64_t>( fragment.duration_samples );
            }

            if( ends_phrase )
            {
                flush_group();
            }
        }

        flush_group();
        return groups;
    }

    std::size_t StageRuntime::local_side_for_source_player( std::size_t source_player_index,
        std::size_t configured_player_index )
    {
        (void)configured_player_index;
        return std::min<std::size_t>( source_player_index, 3 ) % 2;
    }

    std::size_t StageRuntime::opponent_for_side( std::size_t side_index )
    {
        return side_index == 0 ? 1 : 0;
    }

    bool StageRuntime::is_attack_like_mode( StageActionMode mode )
    {
        return mode == StageActionMode::Attack || mode == StageActionMode::Harmony;
    }

    const char *stage_action_mode_name( StageActionMode mode )
    {
        switch( mode )
        {
            case StageActionMode::Intro:
                return "Intro";
            case StageActionMode::Charge:
                return "Charge";
            case StageActionMode::Attack:
                return "Attack";
            case StageActionMode::Guard:
                return "Guard";
            case StageActionMode::Harmony:
                return "Harmony";
            case StageActionMode::End:
                return "End";
            default:
                return "Unknown";
        }
    }

    const char *stage_duet_side_name( StageDuetSide side )
    {
        switch( side )
        {
            case StageDuetSide::Auto:
                return "Auto";
            case StageDuetSide::Primary:
                return "Primary";
            case StageDuetSide::Partner:
                return "Partner";
            case StageDuetSide::Both:
                return "Both";
            case StageDuetSide::Combined:
                return "Combined";
            default:
                return "Unknown";
        }
    }

    StageActionMode stage_action_mode_from_phase( std::uint32_t phase )
    {
        switch( phase )
        {
            case utils::to_int( filetypes::chc::BattlePhase::Intro ):
                return StageActionMode::Intro;
            case utils::to_int( filetypes::chc::BattlePhase::Charge ):
                return StageActionMode::Charge;
            case utils::to_int( filetypes::chc::BattlePhase::Battle ):
                return StageActionMode::Attack;
            case utils::to_int( filetypes::chc::BattlePhase::Guard ):
                return StageActionMode::Guard;
            case utils::to_int( filetypes::chc::BattlePhase::Harmony ):
                return StageActionMode::Harmony;
            case utils::to_int( filetypes::chc::BattlePhase::End ):
                return StageActionMode::End;
            default:
                return StageActionMode::Unknown;
        }
    }

    RuntimeSong build_runtime_song( const filetypes::chc::Song &song, const StageTraceBuildOptions &trace_options )
    {
        RuntimeSong runtime_song{};
        runtime_song.song = &song;
        runtime_song.sections.reserve( song.sections.size() );

        StageTraceBuildOptions options = trace_options;
        options.speed_factor = song.gameplay_speed_factor;

        const auto calculate_chart_event_range = []( RuntimeChart &runtime_chart )
        {
            if( runtime_chart.chart == nullptr )
            {
                return;
            }

            const filetypes::chc::Chart &chart = *runtime_chart.chart;
            const std::int64_t chart_start = static_cast<std::int64_t>( chart.start_sample );
            std::int64_t first = chart_start;
            std::int64_t last = std::max( chart_start, static_cast<std::int64_t>( chart.end_sample ) );

            for( const filetypes::chc::TraceFragment &trace : chart.traces )
            {
                const std::int64_t start = chart_start + static_cast<std::int64_t>( trace.start_sample );
                const std::int64_t end = start + static_cast<std::int64_t>( trace.duration_samples );
                first = std::min( first, start );

                last = std::max( last, end );
            }

            for( const filetypes::chc::PhraseFragment &phrase : chart.phrases )
            {
                const std::int64_t start = chart_start + static_cast<std::int64_t>( phrase.start_sample );
                const std::int64_t end = start + static_cast<std::int64_t>( phrase.duration_samples );
                first = std::min( first, start );

                last = std::max( last, end );
            }

            for( const filetypes::chc::GuardFragment &guard : chart.guards )
            {
                const std::int64_t sample = chart_start + static_cast<std::int64_t>( guard.start_sample );
                first = std::min( first, sample );

                last = std::max( last, sample );
            }

            runtime_chart.event_start_section_sample = first;

            runtime_chart.event_end_section_sample = last;
        };

        for( std::size_t section_index = 0; section_index < song.sections.size(); ++section_index )
        {
            const filetypes::chc::SongSection &source_section = song.sections[ section_index ];

            RuntimeSection runtime_section{};
            runtime_section.section_index = section_index;

            runtime_section.section = &source_section;
            runtime_section.charts.reserve( source_section.charts.size() );

            for( std::size_t chart_index = 0; chart_index < source_section.charts.size(); ++chart_index )
            {
                const filetypes::chc::Chart &source_chart = source_section.charts[ chart_index ];

                RuntimeChart runtime_chart{};
                runtime_chart.chart_index = chart_index;

                runtime_chart.chart = &source_chart;

                if( !source_chart.traces.empty() )
                {
                    runtime_chart.trace_samples = build_trace_samples( source_chart, options );
                }

                calculate_chart_event_range( runtime_chart );

                runtime_section.charts.push_back( std::move( runtime_chart ) );
            }

            for( const RuntimeChart &runtime_chart : runtime_section.charts )
            {
                if( runtime_chart.chart == nullptr )
                {
                    continue;
                }

                const filetypes::chc::Chart &chart = *runtime_chart.chart;
                const std::int64_t chart_start = static_cast<std::int64_t>( chart.start_sample );

                for( const filetypes::chc::GuardFragment &guard : chart.guards )
                {
                    const std::int64_t guard_section_sample =
                        chart_start + static_cast<std::int64_t>( guard.start_sample );

                    runtime_section.has_guard_notes = true;

                    runtime_section.first_guard_section_sample =
                        runtime_section.first_guard_section_sample < 0
                            ? guard_section_sample
                            : std::min( runtime_section.first_guard_section_sample, guard_section_sample );

                    runtime_section.last_guard_section_sample =
                        std::max( runtime_section.last_guard_section_sample, guard_section_sample );
                }
            }

            runtime_song.sections.push_back( std::move( runtime_section ) );
        }

        return runtime_song;
    }

    StageRuntime::StageRuntime() : rng_( 0xC0FFEE ), random_01_( 0.0f, 1.0f )
    {
        sides_ = make_single_player_sides();
    }

    void StageRuntime::initialize( const gamedata::PlayableStageEntry &stage,
        const filetypes::chc::Song *song,
        bool mirror_chart )
    {
        audio_event_ = {};
        audio_events_.clear();
        instrument_charts_.clear();
        instrument_result_controllers_.fill( {} );
        instrument_commands_.fill( InstrumentCommand::Keep );
        next_instrument_section_sample_ = 0;

        previous_instrument_input_ = {};

        initialize_sides( stage );

        runtime_song_ = {};

        state_ = {};

        transition_plan_ = {};
        section_duration_overrides_.clear();
        phrase_judgement_notes_.clear();
        guard_judgement_notes_.clear();
        judgement_debug_ = {};

        score_ = 0;
        result_judgement_counts_.fill( 0 );
        judgement_section_index_ = static_cast<std::size_t>( -1 );

        last_judgement_update_section_sample_ = 0;

        section_start_global_sample_ = 0;

        paused_ = false;

        charge_recovery_enabled_ = true;

        no_death_mode_ = false;

        only_aim_mode_ = false;

        all_sections_mode_ = false;

        player_damage_multiplier_ = 1.0f;

        enemy_damage_multiplier_ = 1.0f;
        section_visit_counts_.clear();
        branch_playback_.clear();
        forced_section_loop_index_.reset();
        reset_energy_on_forced_loop_ = false;
        forced_loop_energy_.fill( 0.0f );
        next_actor_action_serial_ = 1ull << 32u;
        automated_duet_contribution_active_.fill( false );
        automated_duet_contribution_actions_.fill( {} );
        reset_actor_actions();

        config_ = {};

        config_.player_index = 0;

        config_.use_unavailable_partner_charts = true;

        config_.filter_charts_by_time = true;

        config_.duet_stage = stage.duet_stage;

        chart_style_ = stage.portable_visuals ? gamedata::ChartStyle::GitarooManLives : stage.chart_style;

        config_.duet_side = StageDuetSide::Auto;

        config_.chart_preroll_samples = sample_rate * 30;

        config_.chart_postroll_samples = sample_rate * 2;

        config_.transition_route_lock_fraction = 0.5f;

        if( song != nullptr )
        {
            runtime_song_ = build_runtime_song( *song );

            if( mirror_chart )
            {
                for( RuntimeSection &section : runtime_song_.sections )
                {
                    for( RuntimeChart &chart : section.charts )
                    {
                        for( TraceSample &point : chart.trace_samples )
                        {
                            point.x = -point.x;

                            point.angle = std::numbers::pi_v<float> - point.angle;
                        }
                    }
                }
            }

            section_duration_overrides_.assign( runtime_song_.sections.size(), 0 );

            if( !song->sections.empty() )
            {
                const std::uint32_t start_phase = song->sections.front().battle_phase;

                for( std::size_t i = 0; i < sides_.size(); ++i )
                {
                    StageSideState &side = sides_[ i ];

                    if( !side.active )
                    {
                        continue;
                    }

                    const float starting_energy =
                        factors_for_phase( song->energy_damage.players[ i ], start_phase ).starting_energy;

                    set_side_energy_immediate( i, starting_energy );
                }
            }

            start( 0, 0 );
        }
    }

    void StageRuntime::initialize_sides( const gamedata::PlayableStageEntry &stage )
    {
        if( stage.mode == gamedata::PlayMode::Multiplayer )
        {
            sides_ = make_multiplayer_sides( stage.multiplayer_players );
        }
        else
        {
            sides_ = make_single_player_sides();
        }
    }

    void StageRuntime::start( std::size_t section_index, std::int64_t global_sample )
    {
        if( !has_song() )
        {
            return;
        }

        if( section_index >= runtime_song_.sections.size() )
        {
            throw std::runtime_error( "StageRuntime start section out of range" );
        }

        state_ = {};
        section_visit_counts_.assign( runtime_song_.sections.size(), 0u );
        branch_playback_.clear();
        state_.section_index = section_index;

        state_.section_sample = 0;

        state_.global_sample = global_sample;

        state_.song_finished = false;

        section_start_global_sample_ = global_sample;

        instrument_result_controllers_.fill( {} );

        judgement_section_index_ = static_cast<std::size_t>( -1 );

        last_judgement_update_section_sample_ = 0;
        phrase_hit_visuals_.clear();
        guard_hit_visuals_.clear();
        automated_duet_contribution_active_.fill( false );
        automated_duet_contribution_actions_.fill( {} );
        reset_actor_actions();

        enter_section( section_index );
        rebuild_active_charts();
        rebuild_actor_actions();
    }

    void StageRuntime::update( double delta_seconds )
    {
        if( paused_ || !has_song() || state_.song_finished )
        {
            return;
        }

        const double safe_delta = std::max( 0.0, delta_seconds );
        const std::int64_t sample_delta =
            static_cast<std::int64_t>( std::llround( safe_delta * static_cast<double>( sample_rate ) ) );

        update_from_global_sample( state_.global_sample + sample_delta );
    }

    void StageRuntime::update_from_global_sample( std::int64_t global_sample )
    {
        if( !has_song() || state_.song_finished )
        {
            return;
        }

        if( global_sample < section_start_global_sample_ )
        {
            global_sample = section_start_global_sample_;
        }

        const std::int64_t previous_global_sample = state_.global_sample;
        const std::int64_t elapsed_samples =
            global_sample > previous_global_sample ? global_sample - previous_global_sample : 0;

        state_.global_sample = global_sample;

        state_.section_sample = global_sample - section_start_global_sample_;

        update_visible_energy_gauges( elapsed_samples );

        lock_transition_if_needed();
        advance_sections_if_needed( global_sample );

        if( !state_.song_finished )
        {
            rebuild_active_charts();
            rebuild_actor_actions();
        }
        else
        {
            finalize_automated_duet_contributions();
            reset_actor_actions();
        }
    }

    void StageRuntime::set_paused( bool paused )
    {
        paused_ = paused;
    }

    void StageRuntime::toggle_paused()
    {
        paused_ = !paused_;
    }

    bool StageRuntime::paused() const
    {
        return paused_;
    }

    void StageRuntime::set_duet_side( StageDuetSide side )
    {
        if( config_.duet_side == side )
        {
            return;
        }

        mute_instrument_controls( state_.global_sample );
        config_.duet_side = side;
        rebuild_instrument_charts();

        if( has_song() && !state_.song_finished )
        {
            rebuild_judgement_notes_for_current_section();
            rebuild_active_charts();
            rebuild_actor_actions();
        }
    }

    StageDuetSide StageRuntime::duet_side() const
    {
        return config_.duet_side;
    }

    bool StageRuntime::duet_stage() const
    {
        return config_.duet_stage;
    }

    void StageRuntime::seek_relative_samples( std::int64_t sample_delta )
    {
        if( !has_song() || state_.song_finished )
        {
            return;
        }

        std::int64_t next = state_.global_sample + sample_delta;

        if( next < 0 )
        {
            next = 0;
        }

        update_from_global_sample( next );
    }

    void StageRuntime::notify_condition_state_changed()
    {
        if( route_can_still_change() )
        {
            transition_plan_.latched_random_value = roll_transition_random();
            invalidate_transition_prediction();
            ensure_transition_prediction();
        }
    }

    void StageRuntime::set_local_energy_immediate( float value )
    {
        for( std::size_t index = 0; index < sides_.size(); ++index )
        {
            if( sides_[ index ].active && sides_[ index ].local )
            {
                set_side_energy_immediate( index, value );
            }
        }
    }

    void StageRuntime::set_charge_recovery_enabled( bool enabled )
    {
        charge_recovery_enabled_ = enabled;
    }

    void StageRuntime::set_game_modifiers( bool no_death,
        bool only_aim,
        bool all_sections,
        float player_damage_multiplier,
        float enemy_damage_multiplier )
    {
        no_death_mode_ = no_death;

        only_aim_mode_ = only_aim;

        all_sections_mode_ = all_sections;

        player_damage_multiplier_ = std::clamp( player_damage_multiplier, 0.0f, 4.0f );

        enemy_damage_multiplier_ = std::clamp( enemy_damage_multiplier, 0.0f, 4.0f );
        section_visit_counts_.assign( runtime_song_.sections.size(), 0u );

        if( state_.section_index < section_visit_counts_.size() )
        {
            section_visit_counts_[ state_.section_index ] = 1u;
        }

        branch_playback_.clear();

        if( no_death_mode_ )
        {
            for( std::size_t index = 0; index < sides_.size(); ++index )
            {
                if( sides_[ index ].active && sides_[ index ].local && sides_[ index ].logical_energy < 0.01f )
                {
                    set_side_energy_immediate( index, 0.01f );
                }
            }
        }

        if( has_song() && !state_.song_finished )
        {
            reset_transition_plan();
        }
    }

    void StageRuntime::set_forced_section_loop( std::optional<std::size_t> section_index, bool reset_energy )
    {
        if( section_index.has_value() && ( !has_song() || *section_index >= runtime_song_.sections.size() ) )
        {
            throw std::runtime_error( "Forced section loop index out of range" );
        }

        forced_section_loop_index_ = section_index;

        reset_energy_on_forced_loop_ = section_index.has_value() && reset_energy;

        if( reset_energy_on_forced_loop_ )
        {
            for( std::size_t index = 0; index < sides_.size(); ++index )
            {
                forced_loop_energy_[ index ] = sides_[ index ].logical_energy;
            }
        }

        invalidate_transition_prediction();
        ensure_transition_prediction();
        transition_plan_.committed = false;

        transition_plan_.route_lock_sample = calculate_route_lock_sample();

        if( transition_plan_.route_lock_sample <= 0 )
        {
            commit_current_transition();
        }
    }

    bool StageRuntime::has_song() const
    {
        return runtime_song_.song != nullptr && !runtime_song_.sections.empty();
    }

    const RuntimeSong &StageRuntime::runtime_song() const
    {
        return runtime_song_;
    }

    const StageRuntimeState &StageRuntime::state() const
    {
        return state_;
    }

    std::uint32_t StageRuntime::score() const
    {
        return score_;
    }

    StageResultData StageRuntime::result_data( bool won ) const
    {
        StageResultData result{};
        result.score = score_;

        result.judgement_counts = result_judgement_counts_;

        result.won = won;

        const StageSideState *energy_side = nullptr;

        for( const StageSideState &side : sides_ )
        {
            if( side.active && side.local )
            {
                energy_side = &side;
                break;
            }
        }

        if( energy_side == nullptr )
        {
            for( const StageSideState &side : sides_ )
            {
                if( side.active )
                {
                    energy_side = &side;
                    break;
                }
            }
        }

        if( energy_side != nullptr )
        {
            result.final_energy = std::clamp( energy_side->logical_energy, 0.0f, 1.0f );
        }

        return result;
    }

    const StageTransitionPlan &StageRuntime::transition_plan() const
    {
        return transition_plan_;
    }

    StageVisualState StageRuntime::visual_state() const
    {
        StageVisualState visual{};
        visual.mode = current_action_mode();

        if( has_song() && state_.section_index < runtime_song_.sections.size() )
        {
            const RuntimeSection &section = runtime_song_.sections[ state_.section_index ];

            selected_guard_range( section,
                state_.section_sample,
                visual.has_guard_notes,
                visual.first_guard_section_sample,
                visual.last_guard_section_sample,
                visual.guard_phase );
        }

        visual.vision_enabled = !visual.guard_phase;

        return visual;
    }

    StageActionMode StageRuntime::current_action_mode() const
    {
        return section_action_mode( state_.section_index );
    }

    StageActionMode StageRuntime::section_action_mode( std::size_t section_index ) const
    {
        if( !has_song() || section_index >= runtime_song_.sections.size() )
        {
            return StageActionMode::Unknown;
        }

        const RuntimeSection &runtime_section = runtime_song_.sections[ section_index ];

        if( runtime_section.section == nullptr )
        {
            return StageActionMode::Unknown;
        }

        return stage_action_mode_from_phase( runtime_section.section->battle_phase );
    }

    const StageSideArray &StageRuntime::sides() const
    {
        return sides_;
    }

    StageSideArray &StageRuntime::sides()
    {
        return sides_;
    }

    std::size_t StageRuntime::active_side_count() const
    {
        return piko::runtime::stage::active_side_count( sides_ );
    }

    StageConditionState StageRuntime::condition_state() const
    {
        StageConditionState state{};

        state.left_energy = sides_[ 0 ].active ? sides_[ 0 ].logical_energy : 0.0f;

        state.right_energy = sides_[ 1 ].active ? sides_[ 1 ].logical_energy : 0.0f;

        state.player3_available = player3_available();

        state.player4_available = player4_available();

        return state;
    }

    const StageJudgementDebugState &StageRuntime::judgement_debug_state() const
    {
        return judgement_debug_;
    }

    std::vector<StagePhraseVisualState> StageRuntime::phrase_visual_states() const
    {
        std::vector<StagePhraseVisualState> states;

        if( !has_song() || state_.song_finished )
        {
            return states;
        }

        states.reserve( phrase_judgement_notes_.size() );

        for( const PhraseJudgementNote &note : phrase_judgement_notes_ )
        {
            if( note.status == PhraseJudgementStatus::Active && note.trace_loss_start_section_sample < 0 &&
                state_.section_sample <= note.end_section_sample )
            {
                states.push_back( StagePhraseVisualState{
                    .source_player_index = note.source_player_index,
                    .chart_index = note.chart_index,
                    .start_section_sample = note.start_section_sample,
                    .end_section_sample = note.end_section_sample,
                    .darkened = false,
                    .held = true
                } );
            }

            const bool phrase_never_started_miss =
                note.status == PhraseJudgementStatus::Missed && note.trace_loss_start_section_sample < 0;

            const bool phrase_lost_after_start = note.trace_loss_start_section_sample >= 0;

            if( !phrase_never_started_miss && !phrase_lost_after_start )
            {
                continue;
            }

            const std::int64_t darken_start_sample = phrase_lost_after_start
                                                         ? std::clamp( note.trace_loss_start_section_sample,
                                                               note.start_section_sample,
                                                               note.end_section_sample )
                                                         : note.start_section_sample;

            if( darken_start_sample >= note.end_section_sample )
            {
                continue;
            }

            if( state_.section_sample > note.end_section_sample + sample_rate )
            {
                continue;
            }

            states.push_back( StagePhraseVisualState{
                .source_player_index = note.source_player_index,
                .chart_index = note.chart_index,
                .start_section_sample = darken_start_sample,
                .end_section_sample = note.end_section_sample,
                .darkened = true,
                .held = false
            } );
        }

        return states;
    }

    std::vector<StageGuardHitVisualState> StageRuntime::guard_hit_visual_states() const
    {
        return guard_hit_visuals_;
    }

    std::vector<StagePhraseHitVisualState> StageRuntime::phrase_hit_visual_states() const
    {
        return phrase_hit_visuals_;
    }

    StageAudioEventState StageRuntime::audio_event_state() const
    {
        return audio_event_;
    }

    std::vector<std::int64_t> StageRuntime::timing_cue_samples() const
    {
        std::vector<std::int64_t> samples;

        if( !has_song() || state_.song_finished || state_.section_index >= runtime_song_.sections.size() )
        {
            return samples;
        }

        const RuntimeSection &runtime_section = runtime_song_.sections[ state_.section_index ];

        if( runtime_section.section == nullptr || runtime_section.section->charts_per_player == 0 )
        {
            return samples;
        }

        std::size_t source_count = 0;
        const std::array<std::size_t, 2> sources = selected_chart_sources( source_count );

        for( std::size_t source_index = 0; source_index < source_count; ++source_index )
        {
            const std::size_t source_player_index = sources[ source_index ];
            const std::size_t first_chart =
                source_player_index * static_cast<std::size_t>( runtime_section.section->charts_per_player );

            const std::size_t last_chart =
                std::min( first_chart + static_cast<std::size_t>( runtime_section.section->charts_per_player ),
                    runtime_section.charts.size() );

            for( std::size_t chart_index = first_chart; chart_index < last_chart; ++chart_index )
            {
                const RuntimeChart &runtime_chart = runtime_section.charts[ chart_index ];

                if( runtime_chart.chart == nullptr ||
                    !should_include_duet_chart( runtime_section, source_player_index, chart_index ) )
                {
                    continue;
                }

                const filetypes::chc::Chart &chart = *runtime_chart.chart;
                const std::int64_t chart_start = static_cast<std::int64_t>( chart.start_sample );

                for( const filetypes::chc::PhraseFragment &fragment : chart.phrases )
                {
                    if( fragment.phrase_start == 0 )
                    {
                        continue;
                    }

                    samples.push_back( chart_start + static_cast<std::int64_t>( fragment.start_sample ) );
                }

                for( const filetypes::chc::GuardFragment &guard : chart.guards )
                {
                    samples.push_back( chart_start + static_cast<std::int64_t>( guard.start_sample ) );
                }
            }
        }

        std::sort( samples.begin(), samples.end() );
        samples.erase( std::unique( samples.begin(), samples.end() ), samples.end() );
        return samples;
    }

    void StageRuntime::set_section_duration_override( std::size_t section_index, std::int64_t duration_samples )
    {
        if( section_index >= section_duration_overrides_.size() )
        {
            return;
        }

        section_duration_overrides_[ section_index ] = std::max<std::int64_t>( 0, duration_samples );

        if( state_.section_index == section_index && !state_.song_finished )
        {
            invalidate_transition_prediction();
        }
    }

    std::int64_t StageRuntime::section_duration_samples( std::size_t section_index ) const
    {
        if( !has_song() || section_index >= runtime_song_.sections.size() )
        {
            return 0;
        }

        if( section_index < section_duration_overrides_.size() && section_duration_overrides_[ section_index ] > 0 )
        {
            return section_duration_overrides_[ section_index ];
        }

        const filetypes::chc::SongSection *section = runtime_song_.sections[ section_index ].section;
        return section != nullptr ? static_cast<std::int64_t>( section->duration_samples ) : 0;
    }

    std::int64_t StageRuntime::current_section_duration_samples() const
    {
        if( !has_song() || state_.song_finished )
        {
            return 0;
        }

        return section_duration_samples( state_.section_index );
    }

    std::int64_t StageRuntime::current_section_remaining_samples() const
    {
        const std::int64_t duration = current_section_duration_samples();

        if( duration <= 0 )
        {
            return 0;
        }

        return std::max<std::int64_t>( 0, duration - state_.section_sample );
    }

    std::vector<std::size_t> StageRuntime::collect_possible_next_sections() const
    {
        if( !has_song() || state_.song_finished )
        {
            return {};
        }

        if( forced_section_loop_index_.has_value() )
        {
            return { *forced_section_loop_index_ };
        }

        return condition_resolver_.collect_possible_next_sections( *runtime_song_.song, state_.section_index );
    }

    std::vector<ActiveChart> StageRuntime::collect_preview_charts_for_section( std::size_t section_index,
        std::int64_t preview_section_sample ) const
    {
        std::vector<ActiveChart> charts;

        if( !has_song() || section_index >= runtime_song_.sections.size() )
        {
            return charts;
        }

        add_selected_chart_blocks( runtime_song_.sections[ section_index ], preview_section_sample, charts );

        return charts;
    }

    const StageSectionTransition &StageRuntime::predicted_transition()
    {
        ensure_transition_prediction();
        return transition_plan_.predicted_transition;
    }

    const StageSectionTransition &StageRuntime::commit_current_transition()
    {
        if( !transition_plan_.committed )
        {
            ensure_transition_prediction();
            transition_plan_.committed_transition = transition_plan_.predicted_transition;

            transition_plan_.committed = true;

            if( predicted_branch_start_.has_value() )
            {
                branch_playback_.push_back( *predicted_branch_start_ );
            }
            else
            {
                for( std::size_t count = 0; count < predicted_branch_complete_count_ && !branch_playback_.empty();
                    ++count )
                {
                    branch_playback_.pop_back();
                }

                if( predicted_branch_advance_index_.has_value() &&
                    *predicted_branch_advance_index_ < branch_playback_.size() )
                {
                    branch_playback_[ *predicted_branch_advance_index_ ].current_route =
                        predicted_branch_advance_route_index_;
                }
            }
        }

        return transition_plan_.committed_transition;
    }

    bool StageRuntime::transition_is_committed() const
    {
        return transition_plan_.committed;
    }

    std::int64_t StageRuntime::transition_route_lock_sample() const
    {
        return transition_plan_.route_lock_sample;
    }

    void StageRuntime::advance_sections_if_needed( std::int64_t global_sample )
    {
        while( !state_.song_finished )
        {
            if( state_.section_index >= runtime_song_.sections.size() )
            {
                state_.song_finished = true;
                state_.active_charts.clear();
                return;
            }

            const RuntimeSection &runtime_section = runtime_song_.sections[ state_.section_index ];

            if( runtime_section.section == nullptr )
            {
                state_.song_finished = true;
                state_.active_charts.clear();
                return;
            }

            const std::int64_t section_duration = section_duration_samples( state_.section_index );

            if( section_duration <= 0 )
            {
                state_.song_finished = true;
                state_.active_charts.clear();
                return;
            }

            if( state_.section_sample < section_duration )
            {
                return;
            }

            const StageSectionTransition transition = commit_current_transition();

            std::fprintf( stderr,
                "[StageTransition] section=%zu sample=%lld duration=%lld next=%zu terminal=%u\n",
                state_.section_index,
                static_cast<long long>( state_.section_sample ),
                static_cast<long long>( section_duration ),
                transition.next_section_index,
                transition.song_finished ? 1u : 0u );

            state_.section_sample = section_duration;
            update_instrument_control( previous_instrument_input_, section_duration );
            finalize_active_phrases_at_section_sample( section_duration );

            section_start_global_sample_ += section_duration;

            if( transition.song_finished )
            {
                mute_instrument_controls( section_start_global_sample_ );
                state_.song_finished = true;

                state_.section_index = transition.next_section_index;

                state_.section_sample = section_duration;
                state_.active_charts.clear();
                return;
            }

            state_.section_index = transition.next_section_index;

            if( state_.section_index >= runtime_song_.sections.size() )
            {
                state_.song_finished = true;
                state_.active_charts.clear();
                return;
            }

            state_.section_sample = global_sample - section_start_global_sample_;
            enter_section( state_.section_index );
        }
    }

    void StageRuntime::rebuild_active_charts()
    {
        state_.active_charts.clear();

        if( !has_song() || state_.song_finished )
        {
            return;
        }

        if( state_.section_index >= runtime_song_.sections.size() )
        {
            return;
        }

        const RuntimeSection &runtime_section = runtime_song_.sections[ state_.section_index ];

        add_selected_chart_blocks( runtime_section, state_.section_sample, state_.active_charts );
    }

    void StageRuntime::reset_actor_actions()
    {
        actor_phrase_tokens_.fill( 0 );

        for( std::size_t source_player_index = 0; source_player_index < state_.actor_actions.size();
            ++source_player_index )
        {
            state_.actor_actions[ source_player_index ] = {};

            state_.actor_actions[ source_player_index ].source_player_index = source_player_index;
        }
    }

    void StageRuntime::rebuild_actor_actions()
    {
        const std::array<std::uint64_t, 4> previous_tokens = actor_phrase_tokens_;

        for( std::size_t source_player_index = 0; source_player_index < state_.actor_actions.size();
            ++source_player_index )
        {
            state_.actor_actions[ source_player_index ] = {};

            state_.actor_actions[ source_player_index ].source_player_index = source_player_index;

            actor_phrase_tokens_[ source_player_index ] = 0;
        }

        if( !has_song() || state_.song_finished || state_.section_index >= runtime_song_.sections.size() )
        {
            update_automated_duet_contributions();
            return;
        }

        const RuntimeSection &runtime_section = runtime_song_.sections[ state_.section_index ];

        if( runtime_section.section == nullptr || runtime_section.section->charts_per_player == 0 )
        {
            update_automated_duet_contributions();
            return;
        }

        const std::size_t charts_per_player = static_cast<std::size_t>( runtime_section.section->charts_per_player );

        for( std::size_t source_player_index = 0; source_player_index < state_.actor_actions.size();
            ++source_player_index )
        {
            StageActorActionState &actor = state_.actor_actions[ source_player_index ];
            const RuntimeChart *selected_chart = nullptr;
            const RuntimePhraseGroup *selected_phrase = nullptr;
            RuntimePhraseGroup selected_phrase_value{};
            std::int64_t selected_phrase_start = -1;

            const std::size_t first_chart = source_player_index * charts_per_player;
            const std::size_t last_chart = std::min( first_chart + charts_per_player, runtime_section.charts.size() );

            for( std::size_t chart_index = first_chart; chart_index < last_chart; ++chart_index )
            {
                const RuntimeChart &runtime_chart = runtime_section.charts[ chart_index ];

                if( runtime_chart.chart == nullptr ||
                    !should_include_duet_chart( runtime_section, source_player_index, chart_index ) )
                {
                    continue;
                }

                const filetypes::chc::Chart &chart = *runtime_chart.chart;
                const std::int64_t chart_start = static_cast<std::int64_t>( chart.start_sample );
                bool trace_active = false;

                if( runtime_chart.trace_samples.size() >= 2 )
                {
                    const std::int64_t trace_start = chart_start + runtime_chart.trace_samples.front().chart_sample;
                    const std::int64_t trace_end = chart_start + runtime_chart.trace_samples.back().chart_sample;
                    trace_active = state_.section_sample >= trace_start && state_.section_sample <= trace_end;
                }

                if( selected_chart == nullptr && trace_active )
                {
                    selected_chart = &runtime_chart;
                }

                for( const RuntimePhraseGroup &phrase : build_runtime_phrase_groups( chart ) )
                {
                    const std::int64_t phrase_start = chart_start + phrase.start_sample;
                    const std::int64_t phrase_end = chart_start + phrase.end_sample;

                    if( state_.section_sample < phrase_start || state_.section_sample >= phrase_end )
                    {
                        continue;
                    }

                    if( selected_phrase == nullptr || phrase_start > selected_phrase_start )
                    {
                        selected_chart = &runtime_chart;

                        selected_phrase_value = phrase;

                        selected_phrase = &selected_phrase_value;

                        selected_phrase_start = phrase_start;
                    }
                }
            }

            if( selected_chart == nullptr || selected_chart->chart == nullptr )
            {
                continue;
            }

            actor.chart_active = true;

            actor.chart_index = selected_chart->chart_index;

            if( selected_chart->trace_samples.size() >= 2 )
            {
                const std::int64_t chart_sample =
                    state_.section_sample - static_cast<std::int64_t>( selected_chart->chart->start_sample );

                const TracePosition trace_position = trace_position_at( selected_chart->trace_samples, chart_sample );
                actor.has_aim = true;

                actor.aim_x = std::cos( -trace_position.angle );

                actor.aim_y = std::sin( -trace_position.angle );
            }

            if( selected_phrase == nullptr )
            {
                continue;
            }

            const std::int64_t chart_start = static_cast<std::int64_t>( selected_chart->chart->start_sample );
            const std::int64_t phrase_start = chart_start + selected_phrase->start_sample;
            const std::int64_t phrase_end = chart_start + selected_phrase->end_sample;
            std::uint64_t phrase_token = ( static_cast<std::uint64_t>( state_.section_index + 1u ) << 48u ) ^
                                         ( static_cast<std::uint64_t>( selected_chart->chart_index + 1u ) << 32u ) ^
                                         static_cast<std::uint64_t>( phrase_start + 1 );

            phrase_token ^= static_cast<std::uint64_t>( phrase_end ) << 1u;

            actor_phrase_tokens_[ source_player_index ] = phrase_token;

            actor.phrase_active = true;

            actor.phrase_started = previous_tokens[ source_player_index ] != phrase_token;

            actor.phrase_start_section_sample = phrase_start;

            actor.phrase_end_section_sample = phrase_end;

            actor.phrase_authored_duration_samples = selected_phrase->authored_duration_samples;

            actor.action_descriptor = selected_phrase->character_animation + 1u;

            actor.phrase_token = phrase_token;

            actor.action_mode = current_action_mode();

            actor.battle_phase = runtime_section.section->battle_phase;

            if( actor.phrase_started )
            {
                actor.action_serial = next_actor_action_serial_++;
            }
        }

        update_automated_duet_contributions();
    }

    bool StageRuntime::is_automated_duet_source( std::size_t source_player_index ) const
    {
        if( !config_.duet_stage )
        {
            return false;
        }

        const std::size_t primary_source = std::min<std::size_t>( config_.player_index, 1 );
        const std::size_t partner_source = primary_source + 2;

        switch( config_.duet_side )
        {
            case StageDuetSide::Auto:
            case StageDuetSide::Primary:
                return source_player_index == partner_source;
            case StageDuetSide::Partner:
                return source_player_index == primary_source;
            case StageDuetSide::Both:
            case StageDuetSide::Combined:
            default:
                return false;
        }
    }

    void StageRuntime::update_automated_duet_contributions()
    {
        for( std::size_t source_player_index = 0; source_player_index < state_.actor_actions.size();
            ++source_player_index )
        {
            const StageActorActionState &current = state_.actor_actions[ source_player_index ];
            bool &contribution_active = automated_duet_contribution_active_[ source_player_index ];
            StageActorActionState &contribution = automated_duet_contribution_actions_[ source_player_index ];

            if( contribution_active && ( !is_automated_duet_source( source_player_index ) || !current.phrase_active ||
                                           current.phrase_token != contribution.phrase_token ) )
            {
                apply_automated_duet_phrase_end( contribution );
                contribution_active = false;

                contribution = {};
            }

            if( is_automated_duet_source( source_player_index ) && current.phrase_active && !contribution_active )
            {
                apply_automated_duet_phrase_start( current );
                contribution = current;

                contribution_active = true;
            }
        }
    }

    void StageRuntime::finalize_automated_duet_contributions()
    {
        for( std::size_t source_player_index = 0; source_player_index < automated_duet_contribution_active_.size();
            ++source_player_index )
        {
            if( !automated_duet_contribution_active_[ source_player_index ] )
            {
                continue;
            }

            apply_automated_duet_phrase_end( automated_duet_contribution_actions_[ source_player_index ] );
            automated_duet_contribution_active_[ source_player_index ] = false;

            automated_duet_contribution_actions_[ source_player_index ] = {};
        }
    }

    void StageRuntime::apply_automated_duet_phrase_start( const StageActorActionState &action )
    {
        OfficialNoteResult result{};
        result.source_player_index = action.source_player_index;

        result.value = 1.0f;

        result.action_mode = action.action_mode;

        result.battle_phase = action.battle_phase;

        result.award_score = false;

        if( action.action_mode == StageActionMode::Charge )
        {
            result.kind = OfficialNoteResultKind::ChargePress;
        }
        else if( is_attack_like_mode( action.action_mode ) )
        {
            result.kind = OfficialNoteResultKind::AttackPress;
        }
        else
        {
            return;
        }

        apply_note_result_damage( result );
    }

    void StageRuntime::apply_automated_duet_phrase_end( const StageActorActionState &action )
    {
        const std::int64_t duration_samples =
            action.phrase_authored_duration_samples > 0
                ? action.phrase_authored_duration_samples
                : action.phrase_end_section_sample - action.phrase_start_section_sample;

        if( duration_samples <= sample_rate / 10 )
        {
            return;
        }

        const float gameplay_speed_factor = runtime_song_.song != nullptr &&
                                                    std::isfinite( runtime_song_.song->gameplay_speed_factor ) &&
                                                    runtime_song_.song->gameplay_speed_factor > 0.0f
                                                ? runtime_song_.song->gameplay_speed_factor
                                                : 240.0f;

        constexpr float hold_quality_grace_samples = 4500.0f;
        const std::int64_t elapsed_samples =
            std::max<std::int64_t>( 0, action.phrase_end_section_sample - action.phrase_start_section_sample );

        OfficialNoteResult result{};
        result.source_player_index = action.source_player_index;

        result.value = piko::utils::clamp01( static_cast<float>( elapsed_samples ) /
                                             ( static_cast<float>( duration_samples ) + hold_quality_grace_samples ) );

        result.hold_value = static_cast<float>( duration_samples ) / gameplay_speed_factor;

        result.action_mode = action.action_mode;

        result.battle_phase = action.battle_phase;

        result.award_score = false;

        if( action.action_mode == StageActionMode::Charge )
        {
            result.kind = OfficialNoteResultKind::ChargeHoldOrRelease;
        }
        else if( is_attack_like_mode( action.action_mode ) )
        {
            result.kind = OfficialNoteResultKind::AttackHoldOrRelease;
        }
        else
        {
            return;
        }

        apply_note_result_damage( result );
    }

    void StageRuntime::add_player_chart_block( const RuntimeSection &runtime_section,
        std::size_t source_player_index,
        std::int64_t section_sample,
        std::vector<ActiveChart> &out_charts ) const
    {
        if( runtime_section.section == nullptr )
        {
            return;
        }

        const filetypes::chc::SongSection &section = *runtime_section.section;

        if( section.charts_per_player == 0 )
        {
            return;
        }

        const std::size_t first_chart = source_player_index * static_cast<std::size_t>( section.charts_per_player );

        const std::size_t last_chart = first_chart + static_cast<std::size_t>( section.charts_per_player );

        const std::size_t clamped_last_chart = std::min( last_chart, runtime_section.charts.size() );

        if( first_chart >= runtime_section.charts.size() )
        {
            return;
        }

        for( std::size_t chart_index = first_chart; chart_index < clamped_last_chart; ++chart_index )
        {
            const RuntimeChart &runtime_chart = runtime_section.charts[ chart_index ];

            if( runtime_chart.chart == nullptr )
            {
                continue;
            }

            if( !should_include_duet_chart( runtime_section, source_player_index, chart_index ) )
            {
                continue;
            }

            if( !should_include_chart( runtime_chart, section_sample ) )
            {
                continue;
            }

            const filetypes::chc::Chart &chart = *runtime_chart.chart;

            ActiveChart active_chart{};
            active_chart.source_player_index = source_player_index;

            active_chart.chart_index = runtime_chart.chart_index;

            active_chart.chart = runtime_chart.chart;

            active_chart.trace_samples = &runtime_chart.trace_samples;

            active_chart.chart_sample = section_sample - static_cast<std::int64_t>( chart.start_sample );

            out_charts.push_back( active_chart );
        }
    }

    void StageRuntime::add_selected_chart_blocks( const RuntimeSection &runtime_section,
        std::int64_t section_sample,
        std::vector<ActiveChart> &out_charts ) const
    {
        std::size_t source_count = 0;
        const std::array<std::size_t, 2> sources = selected_chart_sources( source_count );

        for( std::size_t index = 0; index < source_count; ++index )
        {
            add_player_chart_block( runtime_section, sources[ index ], section_sample, out_charts );
        }
    }

    std::array<std::size_t, 2> StageRuntime::selected_chart_sources( std::size_t &source_count ) const
    {
        std::array<std::size_t, 2> sources{};
        source_count = 0;

        const std::size_t primary_source = std::min<std::size_t>( config_.player_index, 1 );
        const std::size_t partner_source = primary_source + 2;

        const auto add_source = [ & ]( std::size_t source )
        {
            if( source_count >= sources.size() )
            {
                return;
            }

            for( std::size_t index = 0; index < source_count; ++index )
            {
                if( sources[ index ] == source )
                {
                    return;
                }
            }

            sources[ source_count++ ] = source;
        };

        switch( config_.duet_side )
        {
            case StageDuetSide::Primary:
                add_source( primary_source );
                break;
            case StageDuetSide::Partner:
                add_source( partner_source );
                break;
            case StageDuetSide::Both:
            case StageDuetSide::Combined:
                add_source( primary_source );
                add_source( partner_source );
                break;
            case StageDuetSide::Auto:
            default:
                add_source( primary_source );

                if( !config_.duet_stage && config_.use_unavailable_partner_charts )
                {
                    if( primary_source == 0 && !player3_available() )
                    {
                        add_source( 2 );
                    }
                    else if( primary_source == 1 && !player4_available() )
                    {
                        add_source( 3 );
                    }
                }
                break;
        }

        return sources;
    }

    bool StageRuntime::should_include_duet_chart( const RuntimeSection &runtime_section,
        std::size_t source_player_index,
        std::size_t chart_index ) const
    {
        if( !config_.duet_stage || config_.duet_side != StageDuetSide::Combined )
        {
            return true;
        }

        if( runtime_section.section == nullptr || chart_index >= runtime_section.charts.size() )
        {
            return false;
        }

        const std::size_t primary_source = std::min<std::size_t>( config_.player_index, 1 );
        const std::size_t partner_source = primary_source + 2;

        if( source_player_index != partner_source )
        {
            return true;
        }

        const RuntimeChart &candidate_runtime_chart = runtime_section.charts[ chart_index ];

        if( candidate_runtime_chart.chart == nullptr )
        {
            return false;
        }

        const filetypes::chc::Chart &candidate = *candidate_runtime_chart.chart;
        const std::int64_t candidate_start = static_cast<std::int64_t>( candidate.start_sample );
        const std::int64_t candidate_end =
            std::max( candidate_start + 1, static_cast<std::int64_t>( candidate.end_sample ) );

        const std::size_t charts_per_player = static_cast<std::size_t>( runtime_section.section->charts_per_player );
        const std::size_t first_primary_chart = primary_source * charts_per_player;
        const std::size_t last_primary_chart =
            std::min( first_primary_chart + charts_per_player, runtime_section.charts.size() );

        for( std::size_t primary_chart_index = first_primary_chart; primary_chart_index < last_primary_chart;
            ++primary_chart_index )
        {
            const RuntimeChart &primary_runtime_chart = runtime_section.charts[ primary_chart_index ];

            if( primary_runtime_chart.chart == nullptr )
            {
                continue;
            }

            const filetypes::chc::Chart &primary = *primary_runtime_chart.chart;
            const std::int64_t primary_start = static_cast<std::int64_t>( primary.start_sample );
            const std::int64_t primary_end =
                std::max( primary_start + 1, static_cast<std::int64_t>( primary.end_sample ) );

            if( candidate_start < primary_end && primary_start < candidate_end )
            {
                return false;
            }
        }

        return true;
    }

    void StageRuntime::selected_guard_range( const RuntimeSection &runtime_section,
        std::int64_t section_sample,
        bool &has_guard_notes,
        std::int64_t &first_guard_section_sample,
        std::int64_t &last_guard_section_sample,
        bool &guard_phase ) const
    {
        has_guard_notes = false;

        first_guard_section_sample = -1;

        last_guard_section_sample = -1;

        guard_phase = false;

        if( runtime_section.section == nullptr || runtime_section.section->charts_per_player == 0 )
        {
            return;
        }

        std::size_t source_count = 0;
        const std::array<std::size_t, 2> sources = selected_chart_sources( source_count );
        std::vector<std::int64_t> guard_samples;
        std::vector<std::int64_t> attack_samples;

        for( std::size_t source_index = 0; source_index < source_count; ++source_index )
        {
            const std::size_t first_chart =
                sources[ source_index ] * static_cast<std::size_t>( runtime_section.section->charts_per_player );

            const std::size_t last_chart =
                std::min( first_chart + static_cast<std::size_t>( runtime_section.section->charts_per_player ),
                    runtime_section.charts.size() );

            if( first_chart >= runtime_section.charts.size() )
            {
                continue;
            }

            for( std::size_t chart_index = first_chart; chart_index < last_chart; ++chart_index )
            {
                const RuntimeChart &runtime_chart = runtime_section.charts[ chart_index ];

                if( runtime_chart.chart == nullptr )
                {
                    continue;
                }

                if( !should_include_duet_chart( runtime_section, sources[ source_index ], chart_index ) )
                {
                    continue;
                }

                const std::int64_t chart_start = static_cast<std::int64_t>( runtime_chart.chart->start_sample );

                if( runtime_chart.trace_samples.size() >= 2 )
                {
                    attack_samples.push_back( chart_start + runtime_chart.trace_samples.front().chart_sample );
                }

                for( const RuntimePhraseGroup &group : build_runtime_phrase_groups( *runtime_chart.chart ) )
                {
                    attack_samples.push_back( chart_start + group.start_sample );
                }

                for( const filetypes::chc::GuardFragment &guard : runtime_chart.chart->guards )
                {
                    const std::int64_t guard_section_sample =
                        chart_start + static_cast<std::int64_t>( guard.start_sample );

                    guard_samples.push_back( guard_section_sample );
                    has_guard_notes = true;

                    first_guard_section_sample = first_guard_section_sample < 0
                                                     ? guard_section_sample
                                                     : std::min( first_guard_section_sample, guard_section_sample );

                    last_guard_section_sample = std::max( last_guard_section_sample, guard_section_sample );
                }
            }
        }

        if( guard_samples.empty() )
        {
            return;
        }

        std::sort( guard_samples.begin(), guard_samples.end() );
        guard_samples.erase( std::unique( guard_samples.begin(), guard_samples.end() ), guard_samples.end() );
        std::sort( attack_samples.begin(), attack_samples.end() );
        attack_samples.erase( std::unique( attack_samples.begin(), attack_samples.end() ), attack_samples.end() );

        std::int64_t window_start = guard_samples.front();
        std::int64_t previous_guard = guard_samples.front();

        for( std::size_t index = 1; index < guard_samples.size(); ++index )
        {
            const std::int64_t next_guard = guard_samples[ index ];

            const auto attack = std::upper_bound( attack_samples.begin(), attack_samples.end(), previous_guard );
            const bool attack_before_next_guard = attack != attack_samples.end() && *attack < next_guard;

            if( attack_before_next_guard )
            {
                if( section_sample >= window_start && section_sample <= previous_guard )
                {
                    guard_phase = true;
                }

                window_start = next_guard;
            }

            previous_guard = next_guard;
        }

        if( section_sample >= window_start && section_sample <= previous_guard )
        {
            guard_phase = true;
        }
    }

    bool StageRuntime::should_include_chart( const RuntimeChart &runtime_chart, std::int64_t section_sample ) const
    {
        if( !config_.filter_charts_by_time )
        {
            return true;
        }

        if( runtime_chart.chart == nullptr )
        {
            return false;
        }

        const std::int64_t visible_start = runtime_chart.event_start_section_sample - config_.chart_preroll_samples;

        const std::int64_t visible_end = runtime_chart.event_end_section_sample + config_.chart_postroll_samples;

        return section_sample >= visible_start && section_sample <= visible_end;
    }

    void StageRuntime::enter_section( std::size_t section_index )
    {
        rebuild_instrument_charts();

        if( section_index < section_visit_counts_.size() )
        {
            ++section_visit_counts_[ section_index ];
        }

        if( reset_energy_on_forced_loop_ && forced_section_loop_index_.has_value() &&
            section_index == *forced_section_loop_index_ )
        {
            for( std::size_t index = 0; index < sides_.size(); ++index )
            {
                if( sides_[ index ].active )
                {
                    set_side_energy_immediate( index, forced_loop_energy_[ index ] );
                }
            }
        }

        rebuild_judgement_notes_for_current_section();
        reset_transition_plan();
        lock_transition_if_needed();
    }

    void StageRuntime::rebuild_judgement_notes_for_current_section()
    {
        phrase_judgement_notes_.clear();
        guard_judgement_notes_.clear();
        phrase_hit_visuals_.clear();
        guard_hit_visuals_.clear();
        judgement_debug_ = StageJudgementDebugState{ .enabled = true };

        if( !has_song() || state_.section_index >= runtime_song_.sections.size() )
        {
            return;
        }

        const RuntimeSection &runtime_section = runtime_song_.sections[ state_.section_index ];

        if( runtime_section.section == nullptr || runtime_section.section->charts_per_player == 0 )
        {
            return;
        }

        const auto add_source_player_notes =
            [ & ]( std::size_t source_player_index, bool include_phrases, bool include_guards ) -> SourceNoteCounts
        {
            SourceNoteCounts counts{};

            const std::size_t first_chart =
                source_player_index * static_cast<std::size_t>( runtime_section.section->charts_per_player );

            const std::size_t last_chart =
                std::min( first_chart + static_cast<std::size_t>( runtime_section.section->charts_per_player ),
                    runtime_section.charts.size() );

            if( first_chart >= runtime_section.charts.size() )
            {
                return counts;
            }

            for( std::size_t chart_index = first_chart; chart_index < last_chart; ++chart_index )
            {
                const RuntimeChart &runtime_chart = runtime_section.charts[ chart_index ];

                if( runtime_chart.chart == nullptr )
                {
                    continue;
                }

                if( !should_include_duet_chart( runtime_section, source_player_index, chart_index ) )
                {
                    continue;
                }

                const filetypes::chc::Chart &chart = *runtime_chart.chart;
                const std::int64_t chart_start = static_cast<std::int64_t>( chart.start_sample );

                if( include_phrases )
                {
                    for( const RuntimePhraseGroup &group : build_runtime_phrase_groups( chart ) )
                    {
                        PhraseJudgementNote note{};
                        note.source_player_index = source_player_index;

                        note.chart_index = runtime_chart.chart_index;

                        note.chart = runtime_chart.chart;

                        note.trace_samples = &runtime_chart.trace_samples;

                        note.chart_start_sample = chart_start;

                        note.start_section_sample = chart_start + group.start_sample;

                        note.end_section_sample = chart_start + group.end_sample;

                        note.authored_duration_samples = group.authored_duration_samples;

                        note.has_hold_tail = note.authored_duration_samples > sample_rate / 10;

                        note.character_animation = group.character_animation;

                        phrase_judgement_notes_.push_back( note );
                        ++counts.phrases;
                    }
                }

                if( include_guards )
                {
                    for( const filetypes::chc::GuardFragment &guard : chart.guards )
                    {
                        GuardJudgementNote note{};
                        note.source_player_index = source_player_index;

                        note.chart_index = runtime_chart.chart_index;

                        note.start_section_sample = chart_start + static_cast<std::int64_t>( guard.start_sample );

                        note.button = guard.button;

                        guard_judgement_notes_.push_back( note );
                        ++counts.guards;
                    }
                }
            }

            return counts;
        };

        std::size_t source_count = 0;
        const std::array<std::size_t, 2> sources = selected_chart_sources( source_count );
        std::array<SourceNoteCounts, 2> source_counts{};

        for( std::size_t index = 0; index < source_count; ++index )
        {
            source_counts[ index ] = add_source_player_notes( sources[ index ], true, true );
        }

        char source_debug[ 256 ]{};
        std::snprintf( source_debug,
            sizeof( source_debug ),
            "JUDGE duet=%s side=%s S%zu P%zu/G%zu S%s%zu P%zu/G%zu",
            config_.duet_stage ? "yes" : "no",
            stage_duet_side_name( config_.duet_side ),
            source_count > 0 ? sources[ 0 ] : 0,
            source_counts[ 0 ].phrases,
            source_counts[ 0 ].guards,
            source_count > 1 ? "" : "none/",
            source_count > 1 ? sources[ 1 ] : 0,
            source_counts[ 1 ].phrases,
            source_counts[ 1 ].guards );

        judgement_debug_.last_event = source_debug;

        judgement_debug_.last_event_sample = state_.section_sample;

        std::sort( phrase_judgement_notes_.begin(),
            phrase_judgement_notes_.end(),
            []( const PhraseJudgementNote &a, const PhraseJudgementNote &b )
            {
                return a.start_section_sample < b.start_section_sample;
            } );

        std::sort( guard_judgement_notes_.begin(),
            guard_judgement_notes_.end(),
            []( const GuardJudgementNote &a, const GuardJudgementNote &b )
            {
                return a.start_section_sample < b.start_section_sample;
            } );

        judgement_section_index_ = state_.section_index;

        last_judgement_update_section_sample_ = state_.section_sample;

        judgement_debug_.section_index = state_.section_index;

        judgement_debug_.section_sample = state_.section_sample;

        judgement_debug_.phrase_note_count = phrase_judgement_notes_.size();

        judgement_debug_.guard_note_count = guard_judgement_notes_.size();
    }

    void StageRuntime::set_side_energy_immediate( std::size_t side_index, float value )
    {
        if( side_index >= sides_.size() )
        {
            return;
        }

        StageSideState &side = sides_[ side_index ];
        const float sanitized = sanitized_meter_value( value );

        side.logical_energy = sanitized;

        side.logical_health = sanitized;

        side.energy = sanitized;

        side.health = sanitized;

        side.max_energy = std::max( side.max_energy, std::max( 1.0f, sanitized ) );

        side.max_health = std::max( side.max_health, side.max_energy );

        side.pending_energy_delta = 0.0f;

        side.pending_gain_ticks = 0;

        side.pending_loss_ticks = 0;
    }

    void StageRuntime::update_visible_energy_gauges( std::int64_t elapsed_samples )
    {
        if( elapsed_samples <= 0 )
        {
            return;
        }

        const std::int64_t samples_per_visual_step = sample_rate / 60;
        std::int32_t steps = static_cast<std::int32_t>( elapsed_samples / samples_per_visual_step );

        if( steps <= 0 )
        {
            steps = 1;
        }

        steps = std::clamp( steps, 1, 12 );

        const float step = 0.01f * static_cast<float>( steps );

        for( StageSideState &side : sides_ )
        {
            if( !side.active )
            {
                continue;
            }

            const float previous = side.energy;
            side.energy = step_towards( side.energy, side.logical_energy, step );

            side.health = side.energy;

            side.max_health = std::max( side.max_health, side.max_energy );

            if( side.pending_gain_ticks > 0 )
            {
                --side.pending_gain_ticks;
            }

            if( side.pending_loss_ticks > 0 )
            {
                --side.pending_loss_ticks;
            }

            if( previous != side.energy && side.energy == side.logical_energy )
            {
                side.pending_energy_delta = 0.0f;
            }
        }
    }

    StageRuntime::EnergyApplyResult StageRuntime::apply_energy_delta( std::size_t side_index, float delta )
    {
        EnergyApplyResult result{};
        result.side_index = side_index;

        result.requested_delta = delta;

        result.attempted = true;

        if( side_index >= sides_.size() )
        {
            result.skip_reason = "side_oob";
            return result;
        }

        StageSideState &side = sides_[ side_index ];
        result.side_active = side.active;

        result.before_logical = side.logical_energy;

        result.before_visible = side.energy;

        result.before_pending = side.pending_energy_delta;

        if( !side.active )
        {
            result.after_logical = side.logical_energy;

            result.after_visible = side.energy;

            result.after_pending = side.pending_energy_delta;

            result.skip_reason = "side_inactive";
            return result;
        }

        if( delta == 0.0f || !std::isfinite( delta ) )
        {
            result.after_logical = side.logical_energy;

            result.after_visible = side.energy;

            result.after_pending = side.pending_energy_delta;

            result.skip_reason = delta == 0.0f ? "zero_delta" : "nonfinite_delta";
            return result;
        }

        if( delta < 0.0f )
        {
            delta *= side.local ? player_damage_multiplier_ : enemy_damage_multiplier_;
        }

        const float meter_delta = meter_delta_from_raw_delta( std::max( 1.0f, side.max_energy ), delta );
        const float meter_max = std::max( 1.0f, side.max_energy );

        side.logical_energy =
            std::clamp( side.logical_energy + meter_delta, no_death_mode_ && side.local ? 0.01f : 0.0f, meter_max );

        const float applied_delta = side.logical_energy - result.before_logical;
        side.logical_health = side.logical_energy;

        side.max_health = std::max( side.max_health, side.max_energy );

        side.pending_energy_delta += applied_delta;

        if( applied_delta > 0.0f )
        {
            side.pending_gain_ticks = 10;
        }
        else if( applied_delta < 0.0f )
        {
            side.pending_loss_ticks = 10;
        }

        result.applied_delta = applied_delta;

        result.after_logical = side.logical_energy;

        result.after_visible = side.energy;

        result.after_pending = side.pending_energy_delta;

        result.applied = result.after_logical != result.before_logical || result.after_pending != result.before_pending;

        result.skip_reason = result.applied ? "applied" : "clamped_no_change";

        if( result.applied )
        {
            notify_condition_state_changed();
        }

        return result;
    }

    void StageRuntime::add_score_points( std::uint32_t points )
    {
        if( points == 0 )
        {
            return;
        }

        constexpr std::uint32_t maximum_score = std::numeric_limits<std::uint32_t>::max();
        const std::uint32_t remaining = score_ < maximum_score ? maximum_score - score_ : 0;

        score_ += std::min( points, remaining );
    }

    void StageRuntime::apply_note_result_score( const OfficialNoteResult &result,
        const filetypes::chc::EnergyDamageFactors &factors,
        StageActionMode mode,
        std::size_t local_side )
    {
        if( local_side >= sides_.size() || !sides_[ local_side ].active || !sides_[ local_side ].local )
        {
            return;
        }

        const float value = piko::utils::clamp01( result.value );
        const float hold_value = std::isfinite( result.hold_value ) ? std::max( 0.0f, result.hold_value ) : 0.0f;

        switch( result.kind )
        {
            case OfficialNoteResultKind::ChargePress:
                if( mode == StageActionMode::Charge )
                {
                    add_score_points( score_press_points( value ) );
                }
                break;

            case OfficialNoteResultKind::ChargeHoldOrRelease:
                if( mode == StageActionMode::Charge )
                {
                    add_score_points( score_tail_points( hold_value, value, factors.charge_gain_release ) );
                }
                break;

            case OfficialNoteResultKind::AttackPress:
                if( is_attack_like_mode( mode ) )
                {
                    add_score_points( score_press_points( value ) );
                }
                break;

            case OfficialNoteResultKind::AttackHoldOrRelease:
                if( is_attack_like_mode( mode ) )
                {
                    add_score_points( score_tail_points( hold_value, value, factors.attack_damage_release ) );
                }
                break;

            case OfficialNoteResultKind::GuardSuccess:
                add_score_points( score_press_points( value ) );
                break;

            default:
                break;
        }
    }

    void StageRuntime::apply_note_result_damage( const OfficialNoteResult &result )
    {
        if( runtime_song_.song == nullptr )
        {
            return;
        }

        const std::size_t local_side = local_side_for_source_player( result.source_player_index, config_.player_index );
        const std::size_t opponent_side = opponent_for_side( local_side );

        const StageActionMode mode =
            result.action_mode != StageActionMode::Unknown ? result.action_mode : current_action_mode();

        const std::uint32_t current_phase = has_song() && state_.section_index < runtime_song_.song->sections.size()
                                                ? runtime_song_.song->sections[ state_.section_index ].battle_phase
                                                : 0;

        const std::uint32_t phase = result.battle_phase != 0xFFFFFFFFu ? result.battle_phase : current_phase;

        const std::size_t factor_player = local_side;
        const filetypes::chc::EnergyDamageFactors &factors =
            factors_for_phase( runtime_song_.song->energy_damage.players[ factor_player ], phase );

        const float value = piko::utils::clamp01( result.value );
        const float hold_value = std::isfinite( result.hold_value ) ? std::max( 0.0f, result.hold_value ) : 0.0f;

        float delta = 0.0f;
        const char *kind_name = "none";
        bool applied = false;
        bool tail_result = false;
        EnergyApplyResult energy_apply{};

        const auto apply_delta_to_side = [ & ]( std::size_t side_index, float energy_delta )
        {
            energy_apply = apply_energy_delta( side_index, energy_delta );
            return energy_apply.applied;
        };

        if( result.award_score )
        {
            apply_note_result_score( result, factors, mode, local_side );
        }

        switch( result.kind )
        {
            case OfficialNoteResultKind::ChargePress:
                kind_name = "charge_press";

                if( charge_recovery_enabled_ && mode == StageActionMode::Charge )
                {
                    delta = value * safe_factor( factors.charge_gain_press );
                    apply_delta_to_side( local_side, delta );
                    applied = true;
                }
                break;

            case OfficialNoteResultKind::ChargeHoldOrRelease:
                kind_name = "charge_tail";

                tail_result = true;

                if( charge_recovery_enabled_ && mode == StageActionMode::Charge )
                {
                    delta = hold_value * value * safe_factor( factors.charge_gain_press ) *
                            safe_factor( factors.charge_gain_release );

                    if( delta != 0.0f )
                    {
                        apply_delta_to_side( local_side, delta );
                    }

                    applied = true;
                }
                break;

            case OfficialNoteResultKind::AttackPress:
                kind_name = "attack_press";

                if( is_attack_like_mode( mode ) )
                {
                    delta = -value * safe_factor( factors.attack_damage_press );
                    apply_delta_to_side( opponent_side, delta );
                    applied = true;
                }
                break;

            case OfficialNoteResultKind::AttackHoldOrRelease:
                kind_name = "attack_tail";

                tail_result = true;

                if( is_attack_like_mode( mode ) )
                {
                    delta = -hold_value * value * safe_factor( factors.attack_damage_press ) *
                            safe_factor( factors.attack_damage_release );

                    if( delta != 0.0f )
                    {
                        apply_delta_to_side( opponent_side, delta );
                    }

                    applied = true;
                }
                break;

            case OfficialNoteResultKind::AttackPhraseMiss:
                kind_name = "attack_miss";

                if( is_attack_like_mode( mode ) )
                {
                    delta = -safe_factor( factors.attack_miss_damage );
                    apply_delta_to_side( local_side, delta );
                    applied = true;
                }
                break;

            case OfficialNoteResultKind::GuardSuccess:
                kind_name = "guard_ok";

                delta = value * safe_factor( factors.guard_gain );
                apply_delta_to_side( local_side, delta );
                applied = true;
                break;

            case OfficialNoteResultKind::GuardMiss:
                kind_name = "guard_miss";

                delta = -safe_factor( factors.guard_miss_damage );
                apply_delta_to_side( local_side, delta );
                applied = true;
                break;

            default:
                break;
        }

        judgement_debug_.last_result_value = value;

        judgement_debug_.last_result_hold_value = hold_value;

        judgement_debug_.last_result_delta = delta;

        judgement_debug_.last_result_meter_delta = energy_apply.applied_delta;

        judgement_debug_.last_result_side_index = energy_apply.side_index;

        judgement_debug_.last_result_energy_applied = energy_apply.applied;

        judgement_debug_.last_result_energy_before = energy_apply.before_logical;

        judgement_debug_.last_result_energy_after = energy_apply.after_logical;

        if( result.kind == OfficialNoteResultKind::ChargePress ||
            result.kind == OfficialNoteResultKind::ChargeHoldOrRelease )
        {
            judgement_debug_.last_result_press_factor = safe_factor( factors.charge_gain_press );

            judgement_debug_.last_result_release_factor = safe_factor( factors.charge_gain_release );
        }
        else if( result.kind == OfficialNoteResultKind::AttackPress ||
                 result.kind == OfficialNoteResultKind::AttackHoldOrRelease )
        {
            judgement_debug_.last_result_press_factor = safe_factor( factors.attack_damage_press );

            judgement_debug_.last_result_release_factor = safe_factor( factors.attack_damage_release );
        }

        char buffer[ 384 ]{};
        const char *energy_status = energy_apply.attempted ? energy_apply.skip_reason : "no_energy_call";
        std::snprintf( buffer,
            sizeof( buffer ),
            "%s %s mode=%s phase=%u src=%zu side=%zu/%s active=%d val=%.3f hold=%.3f pf=%.4f rf=%.4f raw=%+.5f meter=%+.5f L %.2f->%.2f E %.2f->%.2f P %.2f->%.2f",
            applied ? "APPLY" : "SKIP",
            kind_name,
            stage_action_mode_name( mode ),
            phase,
            result.source_player_index,
            energy_apply.side_index,
            energy_status,
            energy_apply.side_active ? 1 : 0,
            value,
            hold_value,
            judgement_debug_.last_result_press_factor,
            judgement_debug_.last_result_release_factor,
            delta,
            energy_apply.applied_delta,
            energy_apply.before_logical,
            energy_apply.after_logical,
            energy_apply.before_visible,
            energy_apply.after_visible,
            energy_apply.before_pending,
            energy_apply.after_pending );

        judgement_debug_.phrase_tail_status = buffer;

        if( tail_result )
        {
            judgement_debug_.last_event = applied ? "tail" : "tail-skip";

            judgement_debug_.last_event_sample = state_.section_sample;
        }
    }

    void StageRuntime::record_ssq_action_result( std::uint32_t state,
        std::uint32_t descriptor,
        float value,
        bool flag4,
        const char *kind )
    {
        StageResultGrade grade = StageResultGrade::Miss;

        if( value > 0.8f )
        {
            grade = StageResultGrade::Great;
        }
        else if( value > 0.5f )
        {
            grade = StageResultGrade::Good;
        }
        else if( value > 0.0f )
        {
            grade = StageResultGrade::Ok;
        }

        ++result_judgement_counts_[ static_cast<std::size_t>( grade ) ];
        ++judgement_debug_.ssq_action_result_serial;
        judgement_debug_.ssq_action_result_state = state;

        judgement_debug_.ssq_action_result_descriptor = descriptor;

        judgement_debug_.ssq_action_result_value = value;

        judgement_debug_.ssq_action_result_flag4 = flag4;

        judgement_debug_.ssq_action_result_kind = kind != nullptr ? kind : "";
    }

    void StageRuntime::set_center_judgement( const char *label, const char *kind, std::int64_t timing_delta_samples )
    {
        if( label == nullptr )
        {
            judgement_debug_.center_event.clear();
        }
        else if( std::strcmp( label, "miss" ) == 0 || std::strcmp( label, "MISS" ) == 0 )
        {
            judgement_debug_.center_event = "MISS";
        }
        else if( std::strcmp( label, "great" ) == 0 || std::strcmp( label, "GREAT" ) == 0 )
        {
            judgement_debug_.center_event = "GREAT";
        }
        else if( std::strcmp( label, "ok" ) == 0 || std::strcmp( label, "OK" ) == 0 )
        {
            judgement_debug_.center_event = "OK";
        }
        else if( std::strcmp( label, "good" ) == 0 || std::strcmp( label, "GOOD" ) == 0 )
        {
            judgement_debug_.center_event = "GOOD";
        }
        else
        {
            judgement_debug_.center_event = label;
        }

        judgement_debug_.center_event_sample = state_.section_sample;

        judgement_debug_.last_event_kind = kind != nullptr ? kind : "";

        judgement_debug_.last_timing_delta_samples = timing_delta_samples;

        judgement_debug_.center_detail.clear();

        if( kind != nullptr && *kind != '\0' )
        {
            const long long delta_ms = static_cast<long long>( std::llround(
                static_cast<double>( timing_delta_samples ) * 1000.0 / static_cast<double>( sample_rate ) ) );

            judgement_debug_.center_detail = kind;

            judgement_debug_.center_detail += ' ';

            if( delta_ms > 0 )
            {
                judgement_debug_.center_detail += '+';
            }

            judgement_debug_.center_detail += std::to_string( delta_ms );

            judgement_debug_.center_detail += "ms";
        }
    }

    StageRuntime::PhraseHoldResult StageRuntime::phrase_hold_result_for_end_sample(
        const StageRuntime::PhraseJudgementNote &note,
        std::int64_t actual_end_sample ) const
    {
        PhraseHoldResult result{};

        const std::int64_t group_duration_samples = note.authored_duration_samples > 0
                                                        ? note.authored_duration_samples
                                                        : note.end_section_sample - note.start_section_sample;

        if( group_duration_samples <= 0 )
        {
            return result;
        }

        const std::int64_t clamped_end =
            std::clamp( actual_end_sample, note.start_section_sample, note.end_section_sample );

        constexpr std::int64_t hold_quality_grace_samples = 4500;

        const float gameplay_speed_factor = runtime_song_.song != nullptr &&
                                                    std::isfinite( runtime_song_.song->gameplay_speed_factor ) &&
                                                    runtime_song_.song->gameplay_speed_factor > 0.0f
                                                ? runtime_song_.song->gameplay_speed_factor
                                                : 240.0f;

        result.tail_scalar = static_cast<float>( group_duration_samples ) / gameplay_speed_factor;

        const std::int64_t elapsed_from_group_start =
            std::max<std::int64_t>( 0, clamped_end - note.start_section_sample );

        const float quality_denominator = static_cast<float>( group_duration_samples + hold_quality_grace_samples );

        result.value =
            quality_denominator > 0.0f
                ? piko::utils::clamp01( static_cast<float>( elapsed_from_group_start ) / quality_denominator )
                : 0.0f;

        return result;
    }

    void StageRuntime::finalize_active_phrases_at_section_sample( std::int64_t section_sample )
    {
        for( PhraseJudgementNote &note : phrase_judgement_notes_ )
        {
            if( note.status != PhraseJudgementStatus::Active )
            {
                continue;
            }

            const std::int64_t actual_end_sample =
                std::clamp( section_sample, note.actual_start_section_sample, note.end_section_sample );

            const PhraseHoldResult hold_result = phrase_hold_result_for_end_sample( note, actual_end_sample );
            note.status = PhraseJudgementStatus::Completed;
            apply_phrase_end_success( note, hold_result );

            judgement_debug_.last_event = "complete";

            judgement_debug_.last_event_sample = actual_end_sample;

            judgement_debug_.active_phrase_completed = true;
        }
    }

    void StageRuntime::emit_audio_event( StageAudioEventKind kind, std::size_t source_player_index )
    {
        if( kind == StageAudioEventKind::None )
        {
            return;
        }

        audio_event_.global_sample = state_.global_sample;

        audio_event_.source_player_index = source_player_index;

        audio_event_.kind = kind;
        ++audio_event_.serial;

        if( audio_event_.serial == 0 )
        {
            audio_event_.serial = 1;
        }

        audio_events_.push_back( audio_event_ );
    }

    void StageRuntime::discard_audio_events_through( std::uint64_t serial )
    {
        const auto first_pending = std::find_if( audio_events_.begin(),
            audio_events_.end(),
            [ serial ]( const auto &event )
            {
                return event.serial > serial;
            } );

        audio_events_.erase( audio_events_.begin(), first_pending );
    }

    bool StageRuntime::is_local_phrase_source( std::size_t source_player_index ) const
    {
        const std::size_t local_side = local_side_for_source_player( source_player_index, config_.player_index );

        return local_side < sides_.size() && sides_[ local_side ].local;
    }

    std::uint32_t StageRuntime::instrument_voice_mask( std::size_t source, bool stereo ) const
    {
        bool second = source % 2 != 0;

        if( config_.duet_stage )
        {
            second = source != 0;

            if( config_.duet_side == StageDuetSide::Partner )
            {
                second = source == 2;
            }
        }

        const std::size_t left = second ? 3 : 2;
        const std::size_t right = second ? 5 : 4;
        const bool stereo_voice = stereo || chart_style_ == gamedata::ChartStyle::GitarooManLives;

        return ( 1u << left ) | ( stereo_voice ? ( 1u << right ) : 0u );
    }

    std::uint32_t StageRuntime::instrument_controlled_voices( bool stereo ) const
    {
        if( chart_style_ == gamedata::ChartStyle::GitarooManLives )
        {
            return ( 1u << 2 ) | ( 1u << 3 ) | ( 1u << 4 ) | ( 1u << 5 );
        }

        std::uint32_t mask = 0;

        for( std::size_t side = 0; side < sides_.size(); ++side )
        {
            if( sides_[ side ].local )
            {
                mask |= instrument_voice_mask( side, stereo );
            }
        }

        return mask;
    }

    bool StageRuntime::instrument_secondary_source( std::size_t source ) const
    {
        return config_.duet_stage && source == ( config_.duet_side == StageDuetSide::Partner ? 0u : 2u );
    }

    float StageRuntime::instrument_gain_scale( std::size_t source ) const
    {
        return chart_style_ == gamedata::ChartStyle::GitarooManLives && instrument_secondary_source( source )
                   ? psp_secondary_gain
                   : 1.0f;
    }

    void StageRuntime::mute_instrument_controls( std::int64_t global_sample )
    {
        for( std::size_t source = 0; source < instrument_commands_.size(); ++source )
        {
            emit_audio_event( StageAudioEventKind::InstrumentMuted, source );
            audio_event_.global_sample = global_sample;
            audio_events_.back().global_sample = global_sample;
        }

        instrument_charts_.clear();
        instrument_result_controllers_.fill( {} );
        instrument_commands_.fill( InstrumentCommand::Fade );
    }

    void StageRuntime::rebuild_instrument_charts()
    {
        instrument_charts_.clear();
        instrument_commands_.fill( InstrumentCommand::Keep );
        next_instrument_section_sample_ = 0;

        previous_instrument_input_ = {};

        if( !has_song() || state_.section_index >= runtime_song_.sections.size() )
        {
            return;
        }

        const auto &section = runtime_song_.sections[ state_.section_index ];

        if( section.section == nullptr || section.section->charts_per_player == 0 )
        {
            return;
        }

        for( const auto &chart : section.charts )
        {
            if( chart.chart == nullptr )
            {
                continue;
            }

            const auto source = chart.chart_index / section.section->charts_per_player;

            if( source >= instrument_commands_.size() ||
                !should_include_duet_chart( section, source, chart.chart_index ) )
            {
                continue;
            }

            InstrumentChart instrument;
            instrument.chart = &chart;

            instrument.source = source;
            instrument.state.initialize( *chart.chart, chart_style_ );
            instrument_charts_.push_back( std::move( instrument ) );
        }
    }

    void StageRuntime::set_instrument_playback_mode( audio::InstrumentPlaybackMode mode )
    {
        if( instrument_playback_mode_ != mode )
        {
            instrument_playback_mode_ = mode;

            instrument_mode_changed_ = true;
            instrument_commands_.fill( InstrumentCommand::Keep );
        }
    }

    void StageRuntime::update_instrument_sample( std::int64_t sample, const StageInputSnapshot &input )
    {
        std::array<InstrumentResult, 4> results{};
        std::array<bool, 4> manual_sources_active{};
        std::array<bool, 4> traces_active{};
        std::size_t source_count = 0;

        const auto manual_sources = selected_chart_sources( source_count );

        const auto phase = runtime_song_.song->sections[ state_.section_index ].battle_phase;
        const bool attack = phase == 2 || phase == 4 || phase == 5;

        for( auto &instrument : instrument_charts_ )
        {
            const auto source = instrument.source;
            const bool manual = is_local_phrase_source( source ) &&
                                std::find( manual_sources.begin(), manual_sources.begin() + source_count, source ) !=
                                    manual_sources.begin() + source_count;

            manual_sources_active[ source ] = manual_sources_active[ source ] || manual;

            const auto &trace = instrument.chart->trace_samples;

            if( manual && trace.size() >= 2 )
            {
                const auto start = instrument.chart->chart->start_sample + trace.front().chart_sample;

                const auto end = instrument.chart->chart->start_sample + trace.back().chart_sample;
                traces_active[ source ] = traces_active[ source ] || ( sample >= start && sample < end );
            }

            const bool automatic = !manual || input.automatic_notes || only_aim_mode_;
            bool aligned = true;

            if( manual && !input.automatic_notes )
            {
                const auto &chart = *instrument.chart;

                const auto chart_sample = sample - chart.chart->start_sample;
                const float alignment = aim_alignment_for_chart_sample( chart.trace_samples, chart_sample, input );
                aligned = alignment >=
                          aim_window( input.aim_lock_half_angle_degrees, instrument.state.holding() ).minimum_alignment;
            }

            auto result = instrument.state.update( sample,
                aligned,
                input.action_pressed,
                input.action_down,
                automatic,
                attack,
                input.guard_pressed );

            if( result.note != 0 )
            {
                result.note |= static_cast<std::uint64_t>( instrument.chart->chart_index + 1 ) << 32u;
            }

            auto &combined = results[ source ];
            const bool retained = combined.retained || result.retained;
            const bool press =
                result.kind == InstrumentResultKind::ChargePress || result.kind == InstrumentResultKind::AttackPress;

            if( result.quality > combined.quality ||
                ( combined.kind == InstrumentResultKind::Idle && result.kind != InstrumentResultKind::Idle ) || press )
            {
                combined = result;
            }

            combined.retained = retained;
        }

        if( !config_.duet_stage )
        {
            for( std::size_t source = 0; source < 2; ++source )
            {
                const auto &alternate = results[ source + 2 ];
                auto &primary = results[ source ];
                const bool retained = primary.retained || alternate.retained;

                if( alternate.quality > primary.quality || primary.kind == InstrumentResultKind::Idle )
                {
                    primary = alternate;
                }

                primary.retained = retained;

                manual_sources_active[ source ] =
                    manual_sources_active[ source ] || manual_sources_active[ source + 2 ];

                traces_active[ source ] = traces_active[ source ] || traces_active[ source + 2 ];
            }
        }

        const auto count = config_.duet_stage ? 3u : 2u;

        for( std::size_t source = 0; source < count; ++source )
        {
            auto command = instrument_result_controllers_[ source ].apply( results[ source ],
                chart_style_,
                instrument_secondary_source( source ),
                config_.duet_stage );

            const bool trace_end_mode =
                instrument_playback_mode_ == audio::InstrumentPlaybackMode::TraceEnd && manual_sources_active[ source ];

            if( trace_end_mode )
            {
                command = traces_active[ source ] ? InstrumentCommand::Recover : InstrumentCommand::Fade;

                if( command == instrument_commands_[ source ] )
                {
                    continue;
                }
            }
            else if( instrument_mode_changed_ && manual_sources_active[ source ] && command == InstrumentCommand::Keep )
            {
                command = results[ source ].retained ? InstrumentCommand::Recover : InstrumentCommand::Fade;
            }

            if( command == InstrumentCommand::Keep ||
                ( command == InstrumentCommand::Fade && instrument_commands_[ source ] == InstrumentCommand::Fade ) )
            {
                continue;
            }

            instrument_commands_[ source ] = command;
            emit_audio_event( command == InstrumentCommand::Recover ? StageAudioEventKind::InstrumentRecovered
                                                                    : StageAudioEventKind::InstrumentMuted,
                source );

            audio_event_.global_sample = section_start_global_sample_ + sample;
            audio_events_.back().global_sample = audio_event_.global_sample;
        }

        // Restore the second instrument after all lane callbacks.
        const auto secondary_source = config_.duet_side == StageDuetSide::Partner ? 0u : 2u;
        const bool controlled_trace = instrument_playback_mode_ == audio::InstrumentPlaybackMode::TraceEnd &&
                                      manual_sources_active[ secondary_source ];

        if( chart_style_ == gamedata::ChartStyle::GitarooManLives && config_.duet_stage && !controlled_trace )
        {
            const auto source = secondary_source;
            instrument_commands_[ source ] = InstrumentCommand::Recover;
            emit_audio_event( StageAudioEventKind::InstrumentRecovered, source );
            audio_event_.global_sample = section_start_global_sample_ + sample;
            audio_events_.back().global_sample = audio_event_.global_sample;
        }

        instrument_mode_changed_ = false;
    }

    void StageRuntime::update_instrument_control( const StageInputSnapshot &input, std::int64_t frame_start_sample )
    {
        struct ActionEvent
        {
            std::int64_t sample = 0;
            StageInputEventKind kind = StageInputEventKind::ActionPress;
            bool controls_input = true;
        };

        std::vector<ActionEvent> actions;

        const auto frame_span = std::max<std::int64_t>( 0, state_.section_sample - frame_start_sample );

        for( const auto &event : input.events )
        {
            if( event.kind == StageInputEventKind::ActionPress || event.kind == StageInputEventKind::ActionRelease )
            {
                const auto sample =
                    frame_start_sample +
                    static_cast<std::int64_t>( std::llround(
                        static_cast<double>( frame_span ) * std::clamp( event.frame_position, 0.0f, 1.0f ) ) );

                actions.push_back( { sample, event.kind } );
            }
        }

        if( actions.empty() && ( input.action_pressed || input.action_released ) )
        {
            actions.push_back(
                { state_.section_sample,
                    input.action_pressed ? StageInputEventKind::ActionPress : StageInputEventKind::ActionRelease } );
        }

        if( instrument_playback_mode_ == audio::InstrumentPlaybackMode::TraceEnd )
        {
            for( const auto &instrument : instrument_charts_ )
            {
                const auto &trace = instrument.chart->trace_samples;

                if( trace.size() < 2 )
                {
                    continue;
                }

                for( const auto endpoint : { trace.front().chart_sample, trace.back().chart_sample } )
                {
                    const auto sample = instrument.chart->chart->start_sample + endpoint;

                    if( sample > frame_start_sample && sample <= state_.section_sample )
                    {
                        actions.push_back( { sample, StageInputEventKind::ActionPress, false } );
                    }
                }
            }
        }

        std::stable_sort( actions.begin(),
            actions.end(),
            []( const ActionEvent &left, const ActionEvent &right )
            {
                return left.sample < right.sample;
            } );

        auto current = input;
        current.action_down = previous_instrument_input_.action_down;

        current.action_pressed = false;
        current.events.clear();
        std::size_t action = 0;

        while( next_instrument_section_sample_ <= state_.section_sample || action < actions.size() )
        {
            const auto event_sample =
                action < actions.size() ? actions[ action ].sample : std::numeric_limits<std::int64_t>::max();

            const auto sample = std::min( next_instrument_section_sample_, event_sample );

            if( sample > state_.section_sample )
            {
                break;
            }

            current.action_pressed = false;

            bool dispatched = false;

            while( action < actions.size() && actions[ action ].sample == sample )
            {
                current.action_pressed =
                    actions[ action ].controls_input && actions[ action ].kind == StageInputEventKind::ActionPress;

                if( actions[ action ].controls_input )
                {
                    current.action_down = actions[ action ].kind == StageInputEventKind::ActionPress;
                }

                update_instrument_sample( sample, current );
                dispatched = true;
                ++action;
            }

            if( sample == state_.section_sample )
            {
                current.action_down = input.action_down;
            }

            if( !dispatched )
            {
                update_instrument_sample( sample, current );
            }

            if( sample == next_instrument_section_sample_ )
            {
                next_instrument_section_sample_ += instrument_update_samples;
            }
        }

        previous_instrument_input_ = input;

        previous_instrument_input_.action_pressed = false;

        previous_instrument_input_.action_released = false;
        previous_instrument_input_.events.clear();
    }

    void StageRuntime::record_guard_hit_visual( const StageRuntime::GuardJudgementNote &note )
    {
        guard_hit_visuals_.push_back( StageGuardHitVisualState{
            .source_player_index = note.source_player_index,
            .chart_index = note.chart_index,
            .start_section_sample = note.start_section_sample,
            .button = note.button,
            .hit_section_sample = state_.section_sample
        } );

        prune_guard_hit_visuals();
    }

    void StageRuntime::record_phrase_hit_visual( const StageRuntime::PhraseJudgementNote &note,
        std::int64_t hit_section_sample,
        const StageInputSnapshot &input )
    {
        float direction_x = input.aim_x;
        float direction_y = input.aim_y;
        float length = std::sqrt( direction_x * direction_x + direction_y * direction_y );

        if( length <= 0.15f && note.trace_samples != nullptr && note.trace_samples->size() >= 2 )
        {
            const std::int64_t chart_sample = note.start_section_sample - note.chart_start_sample;
            const TracePosition trace_position = trace_position_at( *note.trace_samples, chart_sample );

            // sub_10F970
            direction_x = std::cos( -trace_position.angle );

            direction_y = std::sin( -trace_position.angle );

            length = 1.0f;
        }

        if( length <= 0.0001f )
        {
            direction_x = 1.0f;

            direction_y = 0.0f;
        }
        else
        {
            direction_x /= length;

            direction_y /= length;
        }

        // sub_10CC10
        direction_x = -direction_x;

        direction_y = -direction_y;

        phrase_hit_visuals_.push_back( StagePhraseHitVisualState{
            .source_player_index = note.source_player_index,
            .chart_index = note.chart_index,
            .start_section_sample = note.start_section_sample,
            .hit_section_sample = hit_section_sample,
            .direction_x = direction_x,
            .direction_y = direction_y
        } );

        prune_phrase_hit_visuals();
    }

    void StageRuntime::prune_phrase_hit_visuals()
    {
        // sub_10CC10: 1.0 -> 0.0 in 0.05 steps.
        constexpr std::int64_t lifetime_samples = sample_rate * 21 / 60;

        phrase_hit_visuals_.erase( std::remove_if( phrase_hit_visuals_.begin(),
                                       phrase_hit_visuals_.end(),
                                       [ this ]( const StagePhraseHitVisualState &visual )
                                       {
                                           return visual.hit_section_sample > state_.section_sample ||
                                                  state_.section_sample - visual.hit_section_sample > lifetime_samples;
                                       } ),
            phrase_hit_visuals_.end() );
    }

    void StageRuntime::prune_guard_hit_visuals()
    {
        // sub_10DDA0
        constexpr std::int64_t guard_hit_visual_lifetime_samples = sample_rate * 21 / 60;

        guard_hit_visuals_.erase( std::remove_if( guard_hit_visuals_.begin(),
                                      guard_hit_visuals_.end(),
                                      [ this ]( const StageGuardHitVisualState &visual )
                                      {
                                          return visual.hit_section_sample > state_.section_sample ||
                                                 state_.section_sample - visual.hit_section_sample >
                                                     guard_hit_visual_lifetime_samples;
                                      } ),
            guard_hit_visuals_.end() );
    }

    void StageRuntime::apply_phrase_start_success( const StageRuntime::PhraseJudgementNote &note )
    {
        const StageActionMode mode =
            note.start_action_mode != StageActionMode::Unknown ? note.start_action_mode : current_action_mode();

        const std::uint32_t phase = note.start_battle_phase != 0xFFFFFFFFu
                                        ? note.start_battle_phase
                                        : ( has_song() && state_.section_index < runtime_song_.song->sections.size()
                                                  ? runtime_song_.song->sections[ state_.section_index ].battle_phase
                                                  : 0 );

        record_ssq_action_result( 1u, note.character_animation, note.start_value, false, "phrase_press" );

        if( mode == StageActionMode::Charge )
        {
            apply_note_result_damage( OfficialNoteResult{
                .kind = OfficialNoteResultKind::ChargePress,
                .source_player_index = note.source_player_index,
                .value = note.start_value,
                .hold_value = 0.0f,
                .action_mode = mode,
                .battle_phase = phase
            } );
        }
        else if( is_attack_like_mode( mode ) )
        {
            apply_note_result_damage( OfficialNoteResult{
                .kind = OfficialNoteResultKind::AttackPress,
                .source_player_index = note.source_player_index,
                .value = note.start_value,
                .hold_value = 0.0f,
                .action_mode = mode,
                .battle_phase = phase
            } );
        }
        else
        {
            char buffer[ 160 ]{};
            std::snprintf( buffer,
                sizeof( buffer ),
                "SKIP phrase_press mode=%s phase=%u val=%.3f",
                stage_action_mode_name( mode ),
                phase,
                note.start_value );

            judgement_debug_.phrase_tail_status = buffer;
        }
    }

    void StageRuntime::apply_phrase_end_success( const StageRuntime::PhraseJudgementNote &note,
        const StageRuntime::PhraseHoldResult &hold_result )
    {
        const float hold_value = std::max( 0.0f, hold_result.tail_scalar );
        const float hold_quality = piko::utils::clamp01( hold_result.value );
        const StageActionMode mode =
            note.start_action_mode != StageActionMode::Unknown ? note.start_action_mode : current_action_mode();

        const std::uint32_t phase = note.start_battle_phase != 0xFFFFFFFFu
                                        ? note.start_battle_phase
                                        : ( has_song() && state_.section_index < runtime_song_.song->sections.size()
                                                  ? runtime_song_.song->sections[ state_.section_index ].battle_phase
                                                  : 0 );

        if( hold_quality > 0.0f )
        {
            record_ssq_action_result( 3u, note.character_animation, hold_quality, false, "phrase_end" );
        }

        if( !note.has_hold_tail )
        {
            char buffer[ 192 ]{};
            std::snprintf( buffer,
                sizeof( buffer ),
                "SKIP phrase_tail short mode=%s phase=%u dur=%lld val=%.3f hold=%.3f",
                stage_action_mode_name( mode ),
                phase,
                static_cast<long long>( note.end_section_sample - note.start_section_sample ),
                note.start_value,
                hold_value );

            judgement_debug_.phrase_tail_status = buffer;
            return;
        }

        if( hold_value <= 0.0f )
        {
            char buffer[ 192 ]{};
            std::snprintf( buffer,
                sizeof( buffer ),
                "SKIP phrase_tail zero_hold mode=%s phase=%u dur=%lld val=%.3f hold=%.3f",
                stage_action_mode_name( mode ),
                phase,
                static_cast<long long>( note.end_section_sample - note.start_section_sample ),
                note.start_value,
                hold_value );

            judgement_debug_.phrase_tail_status = buffer;
            return;
        }

        if( mode == StageActionMode::Charge )
        {
            apply_note_result_damage( OfficialNoteResult{
                .kind = OfficialNoteResultKind::ChargeHoldOrRelease,
                .source_player_index = note.source_player_index,
                .value = hold_quality,
                .hold_value = hold_value,
                .action_mode = mode,
                .battle_phase = phase
            } );
        }
        else if( is_attack_like_mode( mode ) )
        {
            apply_note_result_damage( OfficialNoteResult{
                .kind = OfficialNoteResultKind::AttackHoldOrRelease,
                .source_player_index = note.source_player_index,
                .value = hold_quality,
                .hold_value = hold_value,
                .action_mode = mode,
                .battle_phase = phase
            } );
        }
        else
        {
            char buffer[ 192 ]{};
            std::snprintf( buffer,
                sizeof( buffer ),
                "SKIP phrase_tail wrong_mode mode=%s phase=%u dur=%lld val=%.3f hold=%.3f",
                stage_action_mode_name( mode ),
                phase,
                static_cast<long long>( note.end_section_sample - note.start_section_sample ),
                note.start_value,
                hold_value );

            judgement_debug_.phrase_tail_status = buffer;
        }
    }

    void StageRuntime::apply_phrase_miss( const StageRuntime::PhraseJudgementNote &note )
    {

        record_ssq_action_result( 2u, 0u, 0.0f, true, "phrase_miss" );

        apply_note_result_damage( OfficialNoteResult{
            .kind = OfficialNoteResultKind::AttackPhraseMiss,
            .source_player_index = note.source_player_index,
            .value = 1.0f,
            .hold_value = 0.0f
        } );
    }

    void StageRuntime::apply_guard_success( const StageRuntime::GuardJudgementNote &note, float judgement_value )
    {
        judgement_debug_.last_guard_button = static_cast<std::int32_t>( note.button );
        record_guard_hit_visual( note );

        record_ssq_action_result( 5u,
            std::min<std::uint32_t>( note.button, 3u ) + 1u,
            judgement_value,
            false,
            "guard_success" );

        apply_note_result_damage( OfficialNoteResult{
            .kind = OfficialNoteResultKind::GuardSuccess,
            .source_player_index = note.source_player_index,
            .value = judgement_value,
            .hold_value = 0.0f
        } );
    }

    void StageRuntime::apply_guard_miss( const StageRuntime::GuardJudgementNote &note )
    {
        judgement_debug_.last_guard_button = static_cast<std::int32_t>( note.button );

        record_ssq_action_result( 6u, 0u, 0.0f, true, "guard_miss" );

        apply_note_result_damage( OfficialNoteResult{
            .kind = OfficialNoteResultKind::GuardMiss,
            .source_player_index = note.source_player_index,
            .value = 1.0f,
            .hold_value = 0.0f
        } );
    }

    float StageRuntime::aim_alignment_for_chart_sample( const std::vector<TraceSample> &samples,
        std::int64_t chart_sample,
        const StageInputSnapshot &input ) const
    {

        if( input.automatic_notes )
        {
            return 1.0f;
        }

        if( input.aim_strength <= 0.15f || samples.size() < 2 )
        {
            return 0.0f;
        }

        const TracePosition position = trace_position_at( samples, chart_sample );
        float target_x =
            std::cos( -position.angle ) *
            std::copysign( std::max( std::abs( input.aim_target_scale_x ), 0.0001f ), input.aim_target_scale_x );

        float target_y = std::sin( -position.angle ) * std::max( std::abs( input.aim_target_scale_y ), 0.0001f );
        const float target_length = std::sqrt( target_x * target_x + target_y * target_y );
        target_x /= std::max( target_length, 0.0001f );

        target_y /= std::max( target_length, 0.0001f );
        const float inv_strength = 1.0f / std::max( input.aim_strength, 0.0001f );
        const float input_x = input.aim_x * inv_strength;
        const float input_y = input.aim_y * inv_strength;

        return std::clamp( target_x * input_x + target_y * input_y, -1.0f, 1.0f );
    }

    AimWindow aim_window( float snap_half_angle_degrees, bool holding_phrase )
    {
        constexpr float degrees_to_radians = 0.017453292519943295f;
        constexpr float release_margin_degrees = 10.0f;
        const float release_margin = holding_phrase ? release_margin_degrees : 0.0f;
        const float half_angle = std::clamp( snap_half_angle_degrees + release_margin, 0.0f, 89.0f );
        return { half_angle, std::cos( half_angle * degrees_to_radians ) };
    }

    bool StageRuntime::holding_phrase_for_chart( const ActiveChart &chart ) const
    {
        return std::any_of( phrase_judgement_notes_.begin(),
            phrase_judgement_notes_.end(),
            [ &chart ]( const PhraseJudgementNote &note )
            {
                return note.status == PhraseJudgementStatus::Active && note.chart == chart.chart &&
                       note.source_player_index == chart.source_player_index && note.chart_index == chart.chart_index;
            } );
    }

    void StageRuntime::update_judgement( const StageInputSnapshot &raw_input )
    {
        StageInputSnapshot input = raw_input;
        judgement_debug_.enabled = true;

        judgement_debug_.aim_x = input.aim_x;

        judgement_debug_.aim_y = input.aim_y;

        judgement_debug_.aim_strength = input.aim_strength;

        judgement_debug_.active_phrase_index = -1;

        judgement_debug_.active_phrase_action_descriptor = 0;

        judgement_debug_.next_phrase_index = -1;

        judgement_debug_.next_guard_index = -1;

        judgement_debug_.active_phrase_started = false;

        judgement_debug_.active_phrase_completed = false;

        judgement_debug_.active_phrase_missed = false;

        judgement_debug_.next_phrase_window_open = false;

        judgement_debug_.next_guard_window_open = false;

        if( !has_song() || state_.song_finished || paused_ )
        {
            return;
        }

        if( judgement_section_index_ != state_.section_index ||
            state_.section_sample < last_judgement_update_section_sample_ )
        {
            rebuild_judgement_notes_for_current_section();
        }

        judgement_debug_.section_index = state_.section_index;

        judgement_debug_.section_sample = state_.section_sample;

        judgement_debug_.phrase_note_count = phrase_judgement_notes_.size();

        judgement_debug_.guard_note_count = guard_judgement_notes_.size();

        judgement_debug_.pending_phrase_count = 0;

        judgement_debug_.pending_guard_count = 0;

        for( const PhraseJudgementNote &note : phrase_judgement_notes_ )
        {
            if( note.status != PhraseJudgementStatus::Completed && note.status != PhraseJudgementStatus::Missed )
            {
                ++judgement_debug_.pending_phrase_count;
            }
        }

        for( const GuardJudgementNote &note : guard_judgement_notes_ )
        {
            if( note.status == GuardJudgementStatus::Pending )
            {
                ++judgement_debug_.pending_guard_count;
            }
        }

        const std::int64_t previous_judgement_section_sample =
            std::min( last_judgement_update_section_sample_, state_.section_sample );

        const std::int64_t current_judgement_section_sample = state_.section_sample;

        if( only_aim_mode_ || input.automatic_notes )
        {
            input.events.clear();
            input.action_pressed = false;

            input.action_released = false;

            input.action_down = false;
            input.guard_pressed.fill( false );
            input.guard_down.fill( false );

            const auto frame_position_for_sample = [ & ]( std::int64_t sample )
            {
                const std::int64_t span = current_judgement_section_sample - previous_judgement_section_sample;
                return span > 0 ? std::clamp( static_cast<float>( sample - previous_judgement_section_sample ) /
                                                  static_cast<float>( span ),
                                      0.0f,
                                      1.0f )
                                : 0.0f;
            };

            for( const PhraseJudgementNote &note : phrase_judgement_notes_ )
            {
                if( note.status == PhraseJudgementStatus::Active )
                {
                    input.action_down = true;
                }

                if( note.status == PhraseJudgementStatus::Pending &&
                    note.start_section_sample >= previous_judgement_section_sample &&
                    note.start_section_sample <= current_judgement_section_sample )
                {
                    input.events.push_back( StageInputEvent{
                        StageInputEventKind::ActionPress,
                        0u,
                        frame_position_for_sample( note.start_section_sample )
                    } );

                    input.action_pressed = true;

                    input.action_down = true;
                }
            }

            for( const GuardJudgementNote &note : guard_judgement_notes_ )
            {
                if( note.status == GuardJudgementStatus::Pending &&
                    note.start_section_sample >= previous_judgement_section_sample &&
                    note.start_section_sample <= current_judgement_section_sample )
                {
                    input.events.push_back( StageInputEvent{
                        StageInputEventKind::GuardPress,
                        note.button,
                        frame_position_for_sample( note.start_section_sample )
                    } );

                    input.guard_pressed[ std::min<std::size_t>( note.button, 3u ) ] = true;
                }
            }
        }

        last_judgement_update_section_sample_ = state_.section_sample;

        const std::int64_t phrase_start_window_samples = chart_style_ == gamedata::ChartStyle::GitarooManLives
                                                             ? StageInstrumentState::psp_hit_window_samples
                                                             : StageInstrumentState::hit_window_samples;

        constexpr std::int64_t phrase_release_window_samples = sample_rate * 170 / 1000;
        constexpr std::int64_t phrase_great_window_samples = sample_rate * 42 / 1000;

        constexpr std::int64_t guard_window_samples = 3800;
        constexpr std::int64_t guard_great_window_samples = sample_rate * 42 / 1000;
        const float minimum_acquire_alignment =
            aim_window( input.aim_lock_half_angle_degrees, false ).minimum_alignment;

        const float minimum_release_alignment = aim_window( input.aim_lock_half_angle_degrees, true ).minimum_alignment;

        const auto phrase_effective_window = [ & ]( std::size_t index ) -> std::pair<std::int64_t, std::int64_t>
        {
            const PhraseJudgementNote &note = phrase_judgement_notes_[ index ];

            return {
                note.start_section_sample - phrase_start_window_samples,
                note.start_section_sample + phrase_start_window_samples
            };
        };

        const auto guard_effective_window = [ & ]( std::size_t index ) -> std::pair<std::int64_t, std::int64_t>
        {
            const GuardJudgementNote &note = guard_judgement_notes_[ index ];
            return {
                note.start_section_sample - guard_window_samples,
                note.start_section_sample + guard_window_samples
            };
        };

        const auto sample_for_frame_position = [ & ]( float frame_position )
        {
            const float position = std::clamp( frame_position, 0.0f, 1.0f );
            return previous_judgement_section_sample +
                   static_cast<std::int64_t>( std::llround(
                       static_cast<double>( current_judgement_section_sample - previous_judgement_section_sample ) *
                       static_cast<double>( position ) ) );
        };

        struct PhrasePressCandidate
        {
            std::int32_t index = -1;
            std::int64_t judgement_sample = 0;
            std::int64_t abs_delta = 0;
            JudgementQuality quality{};
            float alignment = 0.0f;
        };

        const auto find_phrase_press_candidate = [ & ]( std::int64_t input_sample,
                                                     const std::vector<bool> &reserved ) -> PhrasePressCandidate
        {
            PhrasePressCandidate best{};
            best.abs_delta = phrase_start_window_samples + 1;

            for( std::size_t i = 0; i < phrase_judgement_notes_.size(); ++i )
            {
                const PhraseJudgementNote &note = phrase_judgement_notes_[ i ];

                if( note.status != PhraseJudgementStatus::Pending || reserved[ i ] )
                {
                    continue;
                }

                const auto window = phrase_effective_window( i );

                if( window.first > window.second )
                {
                    continue;
                }

                if( input_sample < window.first || input_sample > window.second )
                {
                    continue;
                }

                const std::int64_t judgement_sample = input_sample;

                const std::int64_t abs_delta = abs_i64( judgement_sample - note.start_section_sample );
                const std::int64_t chart_sample = judgement_sample - note.chart_start_sample;
                const float alignment = note.trace_samples != nullptr
                                            ? aim_alignment_for_chart_sample( *note.trace_samples, chart_sample, input )
                                            : 1.0f;

                const JudgementQuality quality = phrase_judgement_quality( judgement_sample,
                    note.start_section_sample,
                    phrase_start_window_samples,
                    phrase_great_window_samples,
                    alignment,
                    minimum_acquire_alignment );

                if( best.index < 0 || abs_delta < best.abs_delta ||
                    ( abs_delta == best.abs_delta && quality.value > best.quality.value ) )
                {
                    best = PhrasePressCandidate{
                        .index = static_cast<std::int32_t>( i ),
                        .judgement_sample = judgement_sample,
                        .abs_delta = abs_delta,
                        .quality = quality,
                        .alignment = alignment
                    };
                }
            }

            return best;
        };

        std::vector<PhrasePressCandidate> phrase_press_candidates;
        std::vector<bool> reserved_phrase_notes( phrase_judgement_notes_.size(), false );
        bool has_action_event = false;
        bool has_action_release = false;
        std::int64_t action_release_sample = current_judgement_section_sample;

        for( const StageInputEvent &event : input.events )
        {
            if( event.kind == StageInputEventKind::ActionRelease )
            {
                has_action_event = true;

                has_action_release = true;

                action_release_sample = sample_for_frame_position( event.frame_position );
                continue;
            }

            if( event.kind != StageInputEventKind::ActionPress )
            {
                continue;
            }

            has_action_event = true;
            const PhrasePressCandidate candidate =
                find_phrase_press_candidate( sample_for_frame_position( event.frame_position ), reserved_phrase_notes );

            if( candidate.index >= 0 )
            {
                reserved_phrase_notes[ static_cast<std::size_t>( candidate.index ) ] = true;
                phrase_press_candidates.push_back( candidate );
            }
        }

        if( !has_action_event && input.action_pressed )
        {
            const PhrasePressCandidate candidate =
                find_phrase_press_candidate( current_judgement_section_sample, reserved_phrase_notes );

            if( candidate.index >= 0 )
            {
                phrase_press_candidates.push_back( candidate );
            }
        }

        for( std::size_t i = 0; i < phrase_judgement_notes_.size(); ++i )
        {
            PhraseJudgementNote &note = phrase_judgement_notes_[ i ];

            if( note.status == PhraseJudgementStatus::Completed || note.status == PhraseJudgementStatus::Missed )
            {
                continue;
            }

            const std::int64_t chart_sample = state_.section_sample - note.chart_start_sample;

            const float alignment = note.trace_samples != nullptr
                                        ? aim_alignment_for_chart_sample( *note.trace_samples, chart_sample, input )
                                        : 1.0f;

            note.last_alignment = alignment;

            judgement_debug_.aim_alignment = alignment;

            if( judgement_debug_.active_phrase_index < 0 &&
                state_.section_sample <= note.end_section_sample + phrase_release_window_samples )
            {
                judgement_debug_.active_phrase_index = static_cast<std::int32_t>( i );

                judgement_debug_.active_phrase_start_sample = note.start_section_sample;

                judgement_debug_.active_phrase_end_sample = note.end_section_sample;

                judgement_debug_.active_phrase_action_descriptor = note.character_animation + 1u;

                judgement_debug_.active_phrase_started = note.status == PhraseJudgementStatus::Active;
            }

            const auto phrase_window = phrase_effective_window( i );

            if( judgement_debug_.next_phrase_index < 0 && note.status == PhraseJudgementStatus::Pending )
            {
                judgement_debug_.next_phrase_index = static_cast<std::int32_t>( i );

                judgement_debug_.next_phrase_source_player_index = note.source_player_index;

                judgement_debug_.next_phrase_chart_index = note.chart_index;

                judgement_debug_.next_phrase_chart_start_sample = note.chart_start_sample;

                judgement_debug_.next_phrase_start_sample = note.start_section_sample;

                judgement_debug_.next_phrase_window_open = phrase_window.first <= phrase_window.second &&
                                                           state_.section_sample >= phrase_window.first &&
                                                           state_.section_sample <= phrase_window.second;
            }

            if( phrase_window.first <= phrase_window.second && state_.section_sample < phrase_window.first )
            {
                continue;
            }

            if( note.status == PhraseJudgementStatus::Pending )
            {

                const auto pressed_candidate = std::find_if( phrase_press_candidates.begin(),
                    phrase_press_candidates.end(),
                    [ i ]( const PhrasePressCandidate &candidate )
                    {
                        return candidate.index == static_cast<std::int32_t>( i );
                    } );

                if( pressed_candidate != phrase_press_candidates.end() )
                {
                    const std::int64_t judgement_sample = pressed_candidate->judgement_sample;

                    const JudgementQuality quality = pressed_candidate->quality;
                    note.last_alignment = pressed_candidate->alignment;

                    judgement_debug_.aim_alignment = pressed_candidate->alignment;

                    if( quality.value > 0.0f )
                    {
                        note.status = PhraseJudgementStatus::Active;

                        note.actual_start_section_sample =
                            std::clamp( judgement_sample, note.start_section_sample, note.end_section_sample );

                        note.trace_loss_start_section_sample = -1;

                        note.start_value = quality.value;

                        note.start_action_mode = current_action_mode();

                        note.start_battle_phase =
                            has_song() && state_.section_index < runtime_song_.song->sections.size()
                                ? runtime_song_.song->sections[ state_.section_index ].battle_phase
                                : 0;

                        record_phrase_hit_visual( note, judgement_sample, input );
                        apply_phrase_start_success( note );
                        judgement_debug_.last_event = quality.label;

                        judgement_debug_.last_event_sample = judgement_sample;

                        judgement_debug_.active_phrase_started = true;
                        set_center_judgement( quality.label, "press", judgement_sample - note.start_section_sample );
                        continue;
                    }

                    judgement_debug_.last_event = "aim";

                    judgement_debug_.last_event_sample = judgement_sample;
                    set_center_judgement( "miss", "aim", judgement_sample - note.start_section_sample );
                }

                if( phrase_window.first <= phrase_window.second && state_.section_sample > phrase_window.second )
                {
                    note.status = PhraseJudgementStatus::Missed;
                    apply_phrase_miss( note );
                    judgement_debug_.last_event = "miss";

                    judgement_debug_.last_event_sample = state_.section_sample;

                    judgement_debug_.active_phrase_missed = true;
                    set_center_judgement( "miss", "timeout", state_.section_sample - note.start_section_sample );
                }

                continue;
            }

            if( note.status == PhraseJudgementStatus::Active )
            {
                const bool reached_end = state_.section_sample >= note.end_section_sample;

                if( input.action_down && !reached_end )
                {
                    if( alignment < minimum_release_alignment )
                    {

                        const std::int64_t actual_end_sample = std::clamp( state_.section_sample,
                            note.actual_start_section_sample,
                            note.end_section_sample );

                        note.trace_loss_start_section_sample = actual_end_sample;

                        const PhraseHoldResult hold_result =
                            phrase_hold_result_for_end_sample( note, actual_end_sample );

                        note.status = PhraseJudgementStatus::Completed;

                        apply_phrase_end_success( note, hold_result );

                        judgement_debug_.last_event = "hold";

                        judgement_debug_.last_event_sample = state_.section_sample;

                        judgement_debug_.active_phrase_completed = true;
                        continue;
                    }

                    note.trace_loss_start_section_sample = -1;
                }

                const bool released_after_start =
                    has_action_release && action_release_sample >= note.actual_start_section_sample;

                const bool release_without_timestamp = !has_action_event && input.action_released;

                if( reached_end || released_after_start || release_without_timestamp )
                {
                    const std::int64_t actual_end_sample = reached_end            ? note.end_section_sample
                                                           : released_after_start ? action_release_sample
                                                                                  : state_.section_sample;

                    if( !reached_end && note.trace_loss_start_section_sample < 0 )
                    {
                        note.trace_loss_start_section_sample = actual_end_sample;
                    }

                    const PhraseHoldResult hold_result = phrase_hold_result_for_end_sample( note, actual_end_sample );

                    note.status = PhraseJudgementStatus::Completed;

                    apply_phrase_end_success( note, hold_result );

                    judgement_debug_.last_event = !reached_end ? "hold" : "complete";

                    judgement_debug_.last_event_sample = state_.section_sample;

                    judgement_debug_.active_phrase_completed = true;
                    continue;
                }
            }
        }

        for( std::size_t i = 0; i < guard_judgement_notes_.size(); ++i )
        {
            const GuardJudgementNote &note = guard_judgement_notes_[ i ];

            if( note.status != GuardJudgementStatus::Pending )
            {
                continue;
            }

            if( judgement_debug_.next_guard_index < 0 )
            {
                judgement_debug_.next_guard_index = static_cast<std::int32_t>( i );

                judgement_debug_.next_guard_sample = note.start_section_sample;

                judgement_debug_.next_guard_button = static_cast<std::int32_t>( note.button );

                const auto guard_window = guard_effective_window( i );
                judgement_debug_.next_guard_window_open = guard_window.first <= guard_window.second &&
                                                          state_.section_sample >= guard_window.first &&
                                                          state_.section_sample <= guard_window.second;
            }
        }

        struct GuardPressCandidate
        {
            std::int32_t index = -1;
            std::int64_t judgement_sample = 0;
            std::int64_t abs_delta = 0;
        };

        const auto find_nearest_guard = [ & ]( std::int32_t required_button,
                                            std::int64_t input_sample ) -> GuardPressCandidate
        {
            GuardPressCandidate best{};
            best.abs_delta = guard_window_samples + 1;

            for( std::size_t i = 0; i < guard_judgement_notes_.size(); ++i )
            {
                const GuardJudgementNote &note = guard_judgement_notes_[ i ];

                if( note.status != GuardJudgementStatus::Pending )
                {
                    continue;
                }

                const std::size_t note_button = std::min<std::size_t>( note.button, 3 );

                if( required_button >= 0 && note_button != static_cast<std::size_t>( required_button ) )
                {
                    continue;
                }

                const auto window = guard_effective_window( i );

                if( window.first > window.second )
                {
                    continue;
                }

                if( input_sample < window.first || input_sample > window.second )
                {
                    continue;
                }

                const std::int64_t judgement_sample = input_sample;

                const std::int64_t abs_delta = abs_i64( judgement_sample - note.start_section_sample );

                if( best.index < 0 || abs_delta < best.abs_delta ||
                    ( abs_delta == best.abs_delta &&
                        note.start_section_sample <
                            guard_judgement_notes_[ static_cast<std::size_t>( best.index ) ].start_section_sample ) )
                {
                    best.index = static_cast<std::int32_t>( i );

                    best.judgement_sample = judgement_sample;

                    best.abs_delta = abs_delta;
                }
            }

            return best;
        };

        const auto apply_guard_press = [ & ]( std::size_t pressed_button, std::int64_t input_sample )
        {
            const GuardPressCandidate candidate =
                find_nearest_guard( static_cast<std::int32_t>( pressed_button ), input_sample );

            if( candidate.index >= 0 )
            {
                GuardJudgementNote &note = guard_judgement_notes_[ static_cast<std::size_t>( candidate.index ) ];
                const JudgementQuality quality = timing_judgement_quality( candidate.judgement_sample,
                    note.start_section_sample,
                    guard_window_samples,
                    guard_great_window_samples );

                note.status = GuardJudgementStatus::Hit;
                apply_guard_success( note, quality.value );

                judgement_debug_.last_event = quality.label;

                judgement_debug_.last_event_sample = candidate.judgement_sample;
                set_center_judgement( quality.label, "guard", candidate.judgement_sample - note.start_section_sample );
            }
        };

        bool handled_event_guard_press = false;

        for( const StageInputEvent &event : input.events )
        {
            if( event.kind != StageInputEventKind::GuardPress )
            {
                continue;
            }

            const float position = std::clamp( event.frame_position, 0.0f, 1.0f );
            const std::int64_t input_sample =
                previous_judgement_section_sample +
                static_cast<std::int64_t>( std::llround(
                    static_cast<double>( current_judgement_section_sample - previous_judgement_section_sample ) *
                    static_cast<double>( position ) ) );

            apply_guard_press( std::min<std::size_t>( event.guard_button, 3u ), input_sample );
            handled_event_guard_press = true;
        }

        if( !handled_event_guard_press )
        {
            for( std::size_t pressed_button = 0; pressed_button < input.guard_pressed.size(); ++pressed_button )
            {
                if( input.guard_pressed[ pressed_button ] )
                {
                    apply_guard_press( pressed_button, current_judgement_section_sample );
                }
            }
        }

        for( std::size_t i = 0; i < guard_judgement_notes_.size(); ++i )
        {
            GuardJudgementNote &note = guard_judgement_notes_[ i ];

            if( note.status != GuardJudgementStatus::Pending )
            {
                continue;
            }

            const auto guard_window = guard_effective_window( i );

            if( guard_window.first <= guard_window.second && state_.section_sample > guard_window.second )
            {
                note.status = GuardJudgementStatus::Missed;
                apply_guard_miss( note );
                judgement_debug_.last_event = "miss";

                judgement_debug_.last_event_sample = state_.section_sample;
                set_center_judgement( "miss", "guard", state_.section_sample - note.start_section_sample );
            }
        }

        update_instrument_control( input, previous_judgement_section_sample );
        prune_phrase_hit_visuals();
        prune_guard_hit_visuals();
    }

    void StageRuntime::reset_transition_plan()
    {
        transition_plan_ = {};
        predicted_branch_start_.reset();
        predicted_branch_advance_index_.reset();
        predicted_branch_advance_route_index_ = 0;

        predicted_branch_complete_count_ = 0;

        transition_plan_.latched_random_value = roll_transition_random();

        transition_plan_.route_lock_sample = calculate_route_lock_sample();

        ensure_transition_prediction();

        if( transition_plan_.route_lock_sample <= 0 )
        {
            commit_current_transition();
        }
    }

    std::size_t StageRuntime::branch_merge_section( const std::vector<std::size_t> &routes ) const
    {
        const std::size_t terminal = runtime_song_.sections.size();
        std::vector<std::vector<bool>> guaranteed( terminal + 1, std::vector<bool>( terminal + 1, true ) );
        std::fill( guaranteed[ terminal ].begin(), guaranteed[ terminal ].end(), false );
        guaranteed[ terminal ][ terminal ] = true;

        for( std::size_t iteration = 0; iteration < terminal * terminal + 1; ++iteration )
        {
            bool changed = false;

            for( std::size_t section = 0; section < terminal; ++section )
            {
                std::vector<std::size_t> successors =
                    condition_resolver_.collect_possible_next_sections( *runtime_song_.song, section );

                if( successors.empty() )
                {
                    successors.push_back( terminal );
                }

                std::vector<bool> common( terminal + 1, true );

                for( const std::size_t next : successors )
                {
                    for( std::size_t candidate = 0; candidate <= terminal; ++candidate )
                    {
                        common[ candidate ] = common[ candidate ] && guaranteed[ next ][ candidate ];
                    }
                }

                common[ section ] = true;

                if( common != guaranteed[ section ] )
                {
                    guaranteed[ section ] = std::move( common );

                    changed = true;
                }
            }

            if( !changed )
            {
                break;
            }
        }

        std::vector<std::vector<std::int32_t>> distances;

        for( const std::size_t route : routes )
        {
            std::vector<std::int32_t> distance( terminal + 1, -1 );
            std::vector<std::size_t> queue{
                route
            };

            distance[ route ] = 0;

            for( std::size_t head = 0; head < queue.size(); ++head )
            {
                const std::size_t section = queue[ head ];

                if( section == terminal )
                {
                    continue;
                }

                std::vector<std::size_t> successors =
                    condition_resolver_.collect_possible_next_sections( *runtime_song_.song, section );

                if( successors.empty() )
                {
                    successors.push_back( terminal );
                }

                for( const std::size_t next : successors )
                {
                    if( next <= terminal && distance[ next ] < 0 )
                    {
                        distance[ next ] = distance[ section ] + 1;
                        queue.push_back( next );
                    }
                }
            }

            distances.push_back( std::move( distance ) );
        }

        std::size_t merge = terminal;
        std::int32_t best_distance = std::numeric_limits<std::int32_t>::max();

        for( std::size_t section = 0; section < terminal; ++section )
        {
            std::int32_t total_distance = 0;
            bool shared = true;

            for( std::size_t route_index = 0; route_index < routes.size(); ++route_index )
            {
                const std::vector<std::int32_t> &distance = distances[ route_index ];

                if( distance[ section ] < 0 )
                {
                    shared = false;
                }
                else
                {
                    total_distance += distance[ section ];
                }

                if( !guaranteed[ routes[ route_index ] ][ section ] )
                {
                    shared = false;
                }
            }

            if( shared && total_distance < best_distance )
            {
                best_distance = total_distance;

                merge = section;
            }
        }

        return merge;
    }

    std::size_t StageRuntime::next_unplayed_section( std::size_t start ) const
    {
        const std::size_t terminal = runtime_song_.sections.size();

        if( start >= terminal )
        {
            return terminal;
        }

        std::vector<bool> seen( terminal, false );
        std::vector<std::size_t> queue{
            start
        };

        seen[ start ] = true;

        for( std::size_t head = 0; head < queue.size(); ++head )
        {
            const std::size_t section = queue[ head ];

            if( section_visit_counts_[ section ] == 0u )
            {
                return section;
            }

            for( const std::size_t next :
                condition_resolver_.collect_possible_next_sections( *runtime_song_.song, section ) )
            {
                if( next < terminal && !seen[ next ] )
                {
                    seen[ next ] = true;
                    queue.push_back( next );
                }
            }
        }

        return terminal;
    }

    void StageRuntime::invalidate_transition_prediction()
    {
        transition_plan_.prediction_valid = false;
    }

    void StageRuntime::ensure_transition_prediction()
    {
        if( transition_plan_.prediction_valid )
        {
            return;
        }

        if( !has_song() )
        {
            transition_plan_.predicted_transition = StageSectionTransition{ true, 0 };

            transition_plan_.prediction_valid = true;
            return;
        }

        if( forced_section_loop_index_.has_value() )
        {
            transition_plan_.predicted_transition = StageSectionTransition{ false, *forced_section_loop_index_ };

            transition_plan_.prediction_valid = true;
            return;
        }

        StageConditionEvaluationContext context{};
        context.random_value = transition_plan_.latched_random_value;

        transition_plan_.predicted_transition = condition_resolver_.evaluate_transition( *runtime_song_.song,
            state_.section_index,
            condition_state(),
            context );

        predicted_branch_start_.reset();
        predicted_branch_advance_index_.reset();
        predicted_branch_advance_route_index_ = 0;

        predicted_branch_complete_count_ = 0;

        if( all_sections_mode_ )
        {
            StageSectionTransition &next = transition_plan_.predicted_transition;

            if( !next.song_finished )
            {
                for( const std::size_t route :
                    condition_resolver_.collect_possible_next_sections( *runtime_song_.song, state_.section_index ) )
                {
                    if( route < next.next_section_index && route < section_visit_counts_.size() &&
                        section_visit_counts_[ route ] == 0u )
                    {
                        next.next_section_index = route;
                    }
                }
            }

            if( !next.song_finished && next.next_section_index < section_visit_counts_.size() &&
                section_visit_counts_[ next.next_section_index ] != 0u )
            {
                const std::size_t unplayed = next_unplayed_section( next.next_section_index );
                next = StageSectionTransition{ unplayed >= runtime_song_.sections.size(), unplayed };
            }

            for( std::size_t depth = branch_playback_.size(); depth > 0; --depth )
            {
                const std::size_t index = depth - 1;
                const BranchPlayback &branch = branch_playback_[ index ];

                if( !next.song_finished && next.next_section_index != branch.merge_section )
                {
                    break;
                }

                std::size_t route = branch.current_route + 1;
                while( route < branch.routes.size() && section_visit_counts_[ branch.routes[ route ] ] != 0u )
                {
                    ++route;
                }

                if( route < branch.routes.size() )
                {
                    next = StageSectionTransition{ false, branch.routes[ route ] };

                    predicted_branch_advance_index_ = index;

                    predicted_branch_advance_route_index_ = route;
                    break;
                }

                ++predicted_branch_complete_count_;
            }

            if( !predicted_branch_advance_index_.has_value() && predicted_branch_complete_count_ == 0 )
            {
                std::vector<std::size_t> routes =
                    condition_resolver_.collect_possible_next_sections( *runtime_song_.song, state_.section_index );

                routes.erase( std::remove_if( routes.begin(),
                                  routes.end(),
                                  [ this ]( std::size_t route )
                                  {
                                      return route >= section_visit_counts_.size() ||
                                             section_visit_counts_[ route ] != 0u;
                                  } ),
                    routes.end() );
                const bool random_branch =
                    std::any_of( runtime_song_.song->sections[ state_.section_index ].conditions.begin(),
                        runtime_song_.song->sections[ state_.section_index ].conditions.end(),
                        []( const filetypes::chc::Condition &condition )
                        {
                            return condition.type == 3u;
                        } );
                if( random_branch && routes.size() > 1 && branch_playback_.size() < 8 )
                {
                    const std::size_t merge = branch_merge_section( routes );
                    predicted_branch_start_ = BranchPlayback{ std::move( routes ), merge, 0 };

                    transition_plan_.predicted_transition =
                        StageSectionTransition{ false, predicted_branch_start_->routes.front() };
                }
            }
        }

        transition_plan_.prediction_valid = true;
    }

    void StageRuntime::lock_transition_if_needed()
    {
        if( transition_plan_.committed )
        {
            return;
        }

        if( state_.song_finished )
        {
            return;
        }

        if( state_.section_sample >= transition_plan_.route_lock_sample )
        {
            commit_current_transition();
        }
    }

    bool StageRuntime::route_can_still_change() const
    {
        if( transition_plan_.committed || state_.song_finished )
        {
            return false;
        }

        return state_.section_sample < transition_plan_.route_lock_sample;
    }

    std::int64_t StageRuntime::calculate_route_lock_sample() const
    {
        const std::int64_t duration = current_section_duration_samples();

        if( duration <= 0 )
        {
            return 0;
        }

        return std::clamp<std::int64_t>( duration - condition_setup_lead_samples, 0, duration );
    }

    std::int64_t StageRuntime::last_guard_section_sample( std::size_t section_index ) const
    {
        if( !has_song() || section_index >= runtime_song_.sections.size() )
        {
            return -1;
        }

        return runtime_song_.sections[ section_index ].last_guard_section_sample;
    }

    bool StageRuntime::player3_available() const
    {
        return sides_[ 2 ].active;
    }

    bool StageRuntime::player4_available() const
    {
        return sides_[ 3 ].active;
    }

    float StageRuntime::roll_transition_random()
    {
        return random_01_( rng_ );
    }
}
