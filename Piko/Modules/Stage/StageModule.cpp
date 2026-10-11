#include "Utils/EnumUtil.hpp"
#include "StageModule.hpp"
#include "Localization/LabelTexture.hpp"
#include "Localization/Language.hpp"
#include "Modules/MainMenu/TitleScreenBackdrop.hpp"

#include "Modules/Stage/StageModuleRendering.hpp"
#include "Modules/Stage/StageDebugHud.hpp"
#include "Modules/Stage/StageGeneratedTextures.hpp"
#include "Modules/Stage/StageGuardRenderer.hpp"
#include "Modules/Stage/StagePhraseRenderer.hpp"
#include "Modules/Stage/StageRibbonRenderer.hpp"
#include "Modules/Stage/StageTraceRenderer.hpp"
#include "FileTypes/CHC/CHCJson.hpp"
#include "Runtime/Stage/StageView.hpp"
#include "Runtime/Stage/StageGameplayInterfaceAtlas.hpp"

#include "App/piko.hpp"
#include "GameData/AlbumFlow.hpp"
#include "Managers/InputManager.hpp"
#include "Managers/FrameStats.hpp"
#include "Modules/Result/ResultModule.hpp"
#include "Modules/MainMenu/MainMenuModule.hpp"
#include "Modules/Movie/MovieModule.hpp"
#include "Modules/StageSelect/StageSelectModule.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/RenderUtil.hpp"
#include "Utils/FileUtil.hpp"
#include "Utils/StrUtil.hpp"

#include <SDL3/SDL.h>

#if defined( _DEBUG ) && __has_include( <imgui.h>)
#define PIKO_STAGE_HAS_IMGUI 1
#include <imgui.h>
#else
#define PIKO_STAGE_HAS_IMGUI 0
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <system_error>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace piko::modules
{

    std::optional<StageLoadData> load_true_gitaroo_stage( App &app, std::int32_t stage_number, float carried_health )
    {
        try
        {
            const std::vector<gamedata::PlayableStageEntry> stages =
                gamedata::load_playable_stage_catalog( app.game_data(), app.settings().chart_style );

            const auto match = std::find_if( stages.begin(),
                stages.end(),
                [ stage_number ]( const gamedata::PlayableStageEntry &stage )
                {
                    return !stage.bonus_content && stage.mode == gamedata::PlayMode::SinglePlayer &&
                           stage.stage_number == stage_number &&
                           gamedata::difficulty_from_chc_path( stage.chc_path ) == "Normal";
                } );
            if( match == stages.end() || match->chc_path.empty() || !match->chc_exists )
            {
                return std::nullopt;
            }

            StageLoadData load_data{};
            load_data.stage = *match;

            load_data.song = filetypes::chc::load_json_file( match->chc_path );

            load_data.chc_loaded = true;

            load_data.session_mode = StageSessionMode::TrueGitaroo;

            load_data.carried_health = std::clamp( carried_health, 0.0f, 1.0f );
            return load_data;
        }
        catch( const std::exception &error )
        {
            std::cerr << "[Piko Runtime] True Gitaroo stage load failed: " << error.what() << '\n';
            return std::nullopt;
        }
    }

    StageModule::StageModule( gamedata::PlayableStageEntry stage )
    {
        load_data_.stage = std::move( stage );
    }

    StageModule::StageModule( StageLoadData load_data ) : load_data_( std::move( load_data ) )
    {
    }

    void StageModule::enter( App &app )
    {
        active_modifier_text_ = modifier_text( app.settings() );
        app.input().set_battle_input( app.window(), true, app.settings().mouse_controls_enabled );

        elapsed_seconds_ = 0.0;

        preview_timing_valid_ = false;
        energy_warning_active_.fill( false );
        energy_warning_tick_phase_.fill( 0.0 );
        stage_result_active_ = false;

        stage_result_won_ = true;

        stage_result_audio_started_ = false;

        stage_result_handoff_started_ = false;

        stage_result_elapsed_frame_ = 0.0f;

        lose_choice_pending_ = false;

        lose_choice_delay_frames_ = 0.0f;

        lose_pre_duration_frames_ = 150.0f;

        if( load_data_.chc_loaded )
        {
            const filetypes::chc::SceneFrameRange &range = load_data_.song.lose_pre;

            if( std::isfinite( range.first_frame ) && std::isfinite( range.last_frame ) &&
                range.last_frame > range.first_frame )
            {
                lose_pre_duration_frames_ = range.last_frame - range.first_frame + 1.0f;
            }
        }

        hud_last_phase_ = runtime::stage::StageActionMode::Unknown;

        hud_phase_change_seconds_ = 99.0f;

        hud_outgoing_phase_ = runtime::stage::StageActionMode::Unknown;

        phase_announcement_ = {};

        current_ssq_action_record_ = {};

        next_ssq_action_record_ = {};

        last_latched_ssq_result_serial_ = 0;

        previous_phrase_action_down_ = false;

        debug_zoom_multiplier_ = stage::default_debug_zoom_multiplier;
        debug_message_.clear();
        initialize_runtime( app );

        if( tutorial_session() )
        {
            runtime_.set_game_modifiers( false, false, false );
        }
        else
        {
            runtime_.set_game_modifiers( app.settings().no_death_mode,
                app.settings().only_aim_mode,
                app.settings().all_sections_mode,
                app.settings().player_damage_multiplier,
                app.settings().enemy_damage_multiplier );
        }

        tutorial_completion_active_ = false;

        tutorial_retry_selected_ = true;

        tutorial_choice_ = TutorialChoice::None;

        tutorial_choice_ready_seconds_ = 0.0;

        tutorial_scene_start_sample_ = 0;

        if( tutorial_session() && !teaching_tutorial() && !load_data_.song.cues.empty() )
        {
            tutorial_scene_start_sample_ =
                static_cast<std::int64_t>( std::llround( load_data_.song.cues.front().first_frame *
                                                         stage3d_audio_sample_rate / stage3d_ssq_frames_per_second ) );
        }

        if( runtime_.duet_stage() )
        {
            switch( app.settings().duet_chart_mode )
            {
                case 1:
                    runtime_.set_duet_side( runtime::stage::StageDuetSide::Partner );
                    break;
                case 2:
                    runtime_.set_duet_side( runtime::stage::StageDuetSide::Both );
                    break;
                case 3:
                    runtime_.set_duet_side( runtime::stage::StageDuetSide::Combined );
                    break;
                default:
                    runtime_.set_duet_side( runtime::stage::StageDuetSide::Auto );
                    break;
            }
        }

        load_stage_textures( app );
        load_modifier_texture( app );

        if( tutorial_session() )
        {
            tutorial_.load( app, teaching_tutorial() );
        }

        if( !app.settings().gameplay_visual.guard_icon_set.empty() &&
            !stage::load_guard_icon_set( app.texture_manager(),
                app.game_data().resource_root(),
                app.settings().gameplay_visual.guard_icon_set,
                "stage/custom-guard-icon-",
                guard_icon_handles_ ) )
        {
            std::cerr << "[Piko Runtime] Guard icon set unavailable: " << app.settings().gameplay_visual.guard_icon_set
                      << '\n';
        }

        std::string stage3d_status;
        stage3d_.load( app, load_data_.stage, &stage3d_status );

        initialize_stage_audio( app );

        if( !stage3d_status.empty() )
        {
            debug_message_ = debug_message_.empty() ? stage3d_status : debug_message_ + " | " + stage3d_status;
        }
    }

    void StageModule::leave( App &app )
    {
        app.input().set_battle_input( app.window(), false, false );

        app.audio().release_channel( result_menu_sound_channel_ );
        result_menu_sound_channel_ = {};
        shutdown_stage_audio( app );
        tutorial_.unload( app );
        stage3d_.unload( app );
        unload_stage_textures( app );
        stage::unload_guard_icon_set( app.texture_manager(), guard_icon_handles_ );
        app.texture_manager().unload( result_menu_texture_ );
        result_menu_texture_ = {};
        result_background_.shutdown();
    }

    void StageModule::initialize_runtime( App &app )
    {
        const filetypes::chc::Song *runtime_song = load_data_.chc_loaded ? &load_data_.song : nullptr;

        if( runtime_song != nullptr && app.settings().mirror_mode && !tutorial_session() )
        {
            mirrored_song_ = load_data_.song;

            for( filetypes::chc::SongSection &section : mirrored_song_.sections )
            {
                for( filetypes::chc::Chart &chart : section.charts )
                {
                    for( filetypes::chc::GuardFragment &guard : chart.guards )
                    {
                        guard.button = ( guard.button + 2u ) % 4u;
                    }
                }
            }

            runtime_song = &mirrored_song_;
        }
        else
        {
            mirrored_song_ = {};
        }

        runtime_.initialize( load_data_.stage, runtime_song, app.settings().mirror_mode && !tutorial_session() );

        practice_section_global_sample_ = 0;

        practice_last_timing_sample_ = -1;

        practice_last_timing_section_ = static_cast<std::size_t>( -1 );

        if( load_data_.session_mode == StageSessionMode::TrueGitaroo )
        {
            runtime_.set_charge_recovery_enabled( false );
            runtime_.set_local_energy_immediate( load_data_.carried_health );
        }
        else if( load_data_.session_mode == StageSessionMode::Practice && runtime_.has_song() )
        {
            const std::size_t section_index =
                std::min( load_data_.practice_section_index, runtime_.runtime_song().sections.size() - 1u );

            load_data_.practice_section_index = section_index;

            practice_section_global_sample_ = practice_section_global_sample();
            runtime_.set_local_energy_immediate( 1.0f );
            runtime_.set_forced_section_loop( section_index, true );
            runtime_.start( section_index, practice_section_global_sample_ );
        }

        runtime_initialized_ = true;
    }

    std::int32_t StageModule::runtime_player_count() const
    {
        return static_cast<std::int32_t>( runtime_.active_side_count() );
    }

    void StageModule::update( App &app, double delta_seconds )
    {
        elapsed_seconds_ += delta_seconds;

        last_frame_delta_seconds_ = delta_seconds > 0.0 ? delta_seconds : ( 1.0 / 60.0 );

        if( !stage_result_active_ && app.input().pressed( managers::InputAction::StageExit ) )
        {
            return_to_stage_select( app );
            return;
        }

        if( !stage_result_active_ && !tutorial_completion_active_ &&
            app.input().pressed( managers::InputAction::Pause ) )
        {
            if( load_data_.session_mode == StageSessionMode::Practice )
            {
                restart_practice_section( app );
            }
            else
            {
                runtime_.toggle_paused();
                stage_audio_.set_paused( app.audio(), runtime_.paused() );
            }
        }

        update_mouse_capture( app );

        if( runtime_.paused() && !stage_result_active_ )
        {
            last_frame_delta_seconds_ = 0.0;
            stage3d_.update( 0.0 );
            return;
        }

        stage3d_.update( delta_seconds );

        if( stage3d_.debug_camera_animating() )
        {
            debug_message_ = stage3d_.debug_camera_line();
        }

        if( !stage_result_active_ )
        {
            if( stage_audio_.using_audio_clock() )
            {
                stage_audio_.update( app.audio(), runtime_ );
            }
            else
            {
                runtime_.update( delta_seconds );
            }
        }

        if( update_tutorial_session( app ) )
        {
            return;
        }

        const runtime::stage::StageActionMode current_hud_phase = runtime_.current_action_mode();
        update_energy_warning( delta_seconds );

        if( hud_last_phase_ == runtime::stage::StageActionMode::Unknown )
        {
            hud_last_phase_ = current_hud_phase;

            hud_phase_change_seconds_ = 99.0f;

            hud_outgoing_phase_ = runtime::stage::StageActionMode::Unknown;
        }
        else if( current_hud_phase != hud_last_phase_ )
        {
            hud_outgoing_phase_ = hud_last_phase_;

            hud_last_phase_ = current_hud_phase;

            hud_phase_change_seconds_ = 0.0f;
        }
        else if( !runtime_.paused() && !stage_result_active_ )
        {
            hud_phase_change_seconds_ += static_cast<float>( std::max( 0.0, delta_seconds ) );
        }

        if( !runtime_.paused() && !stage_result_active_ && !tutorial_session() )
        {
            runtime::stage::update_phase_announcement( phase_announcement_,
                current_hud_phase,
                static_cast<float>( delta_seconds ) );
        }

        const runtime::stage::StageRuntimeState &stage_state_for_3d = runtime_.state();
        const std::int64_t stage3d_global_sample =
            load_data_.session_mode == StageSessionMode::Practice
                ? practice_section_global_sample_ + std::max<std::int64_t>( 0, stage_state_for_3d.section_sample )
                : tutorial_scene_start_sample_ + std::max<std::int64_t>( 0, stage_state_for_3d.global_sample );

        const double raw_ssq_global_frame =
            static_cast<double>( stage3d_global_sample ) * stage3d_ssq_frames_per_second / stage3d_audio_sample_rate;

        const double raw_ssq_section_frame =
            static_cast<double>( std::max<std::int64_t>( 0, stage_state_for_3d.section_sample ) ) *
            stage3d_ssq_frames_per_second / stage3d_audio_sample_rate;

        const bool local_side_defeated = std::any_of( runtime_.sides().begin(),
            runtime_.sides().end(),
            []( const runtime::stage::StageSideState &side )
            {
                return side.active && side.local && side.logical_health <= 0.0f;
            } );
        if( !tutorial_session() && ( local_side_defeated || stage_state_for_3d.song_finished || stage_result_active_ ) )
        {
            if( !stage_result_active_ )
            {
                stage_result_active_ = true;

                stage_result_elapsed_frame_ = 0.0f;

                lose_retry_selected_ = true;

                lose_choice_pending_ = false;

                lose_choice_delay_frames_ = 0.0f;
                stage_audio_.stop_song( app.audio() );
                const bool local_side_survived = std::any_of( runtime_.sides().begin(),
                    runtime_.sides().end(),
                    []( const runtime::stage::StageSideState &side )
                    {
                        return side.active && side.local && side.logical_health > 0.0f;
                    } );
                const bool opponent_side_defeated = std::any_of( runtime_.sides().begin(),
                    runtime_.sides().end(),
                    []( const runtime::stage::StageSideState &side )
                    {
                        return side.active && !side.local && side.logical_health <= 0.0f;
                    } );
                stage_result_won_ = !local_side_defeated && ( local_side_survived || opponent_side_defeated );

                if( !stage_result_won_ )
                {
                    result_background_.initialize( app );
                    const std::filesystem::path menu_path =
                        app.game_data().resolve_runtime_asset( "MAINMENU/TEXTURES/MAINMENU/MENU.png" );

                    if( !menu_path.empty() )
                    {
                        try
                        {
                            result_menu_texture_ =
                                app.texture_manager().load_png( "stage/result-title-menu", menu_path );
                        }
                        catch( const std::exception & )
                        {
                            result_menu_texture_ = {};
                        }
                    }
                }

                stage_result_audio_started_ = false;

                stage_result_handoff_started_ = false;
            }
            else
            {
                stage_result_elapsed_frame_ +=
                    static_cast<float>( std::max( 0.0, delta_seconds ) * stage3d_result_frames_per_second );
            }
        }
        else
        {
            stage_result_active_ = false;

            stage_result_audio_started_ = false;

            stage_result_handoff_started_ = false;

            stage_result_elapsed_frame_ = 0.0f;
        }

        update_mouse_capture( app );

        if( stage_result_active_ && !stage_result_audio_started_ )
        {
            stage_result_audio_started_ = stage_audio_.play_result_cue( app.audio(), stage_result_won_ );
        }

        if( stage_result_active_ && stage_result_won_ && stage_result_elapsed_frame_ >= 120.0f &&
            !stage_result_handoff_started_ )
        {
            stage_result_handoff_started_ = true;

            if( load_data_.session_mode == StageSessionMode::TrueGitaroo )
            {
                const float health = runtime_.result_data( true ).final_energy;

                if( load_data_.stage.stage_number < 10 )
                {
                    if( std::optional<StageLoadData> next =
                            load_true_gitaroo_stage( app, load_data_.stage.stage_number + 1, health );
                        next.has_value() )
                    {
                        app.change_module( std::make_unique<StageModule>( std::move( *next ) ) );
                    }
                    else
                    {
                        app.change_module( std::make_unique<MainMenuModule>() );
                    }
                }
                else
                {
                    app.change_module( std::make_unique<MainMenuModule>() );
                }

                return;
            }

            ResultLoadData result_load{};
            result_load.stage = load_data_.stage;

            result_load.song = load_data_.song;

            result_load.chc_loaded = load_data_.chc_loaded;

            result_load.result = runtime_.result_data( true );
            const std::filesystem::path current_audio = resolve_stage_audio_path( app );

            if( !current_audio.empty() )
            {
                result_load.audio_bank_candidates.push_back( current_audio );
            }

            const std::filesystem::path ps2_audio = app.game_data().runtime_stage_audio( load_data_.stage.stage_number,
                gamedata::ChartStyle::GitarooMan,
                load_data_.stage.chc_path.filename().string() );

            if( !ps2_audio.empty() && ps2_audio.lexically_normal() != current_audio.lexically_normal() )
            {
                result_load.audio_bank_candidates.push_back( ps2_audio );
            }

            app.change_module( std::make_unique<ResultModule>( std::move( result_load ) ) );
            return;
        }

        if( stage_result_active_ && !stage_result_won_ && stage_result_elapsed_frame_ >= lose_pre_duration_frames_ )
        {
            if( lose_choice_pending_ )
            {
                lose_choice_delay_frames_ -=
                    static_cast<float>( std::max( 0.0, delta_seconds ) * stage3d_result_frames_per_second );

                if( lose_choice_delay_frames_ <= 0.0f )
                {
                    if( lose_pending_retry_ )
                    {
                        app.change_module( std::make_unique<StageModule>( load_data_ ) );
                    }
                    else if( load_data_.session_mode == StageSessionMode::TrueGitaroo )
                    {
                        app.change_module( std::make_unique<MainMenuModule>() );
                    }
                    else
                    {
                        return_to_stage_select( app );
                    }
                }

                return;
            }

            const managers::InputManager &input = app.input();

            if( input.pressed( managers::InputAction::MenuLeft ) || input.pressed( managers::InputAction::MenuRight ) ||
                input.pressed( managers::InputAction::MenuUp ) || input.pressed( managers::InputAction::MenuDown ) )
            {
                lose_retry_selected_ = !lose_retry_selected_;

                if( result_menu_sound_channel_ && app.audio().has_subsong( "SELECT" ) )
                {
                    app.audio().play_subsong( result_menu_sound_channel_, "SELECT", false );
                }
            }

            if( input.pressed( managers::InputAction::Back ) )
            {
                lose_retry_selected_ = false;
            }

            if( input.pressed( managers::InputAction::Confirm ) || input.pressed( managers::InputAction::Back ) )
            {
                lose_pending_retry_ = lose_retry_selected_;

                lose_choice_pending_ = true;

                lose_choice_delay_frames_ = 8.0f;

                if( result_menu_sound_channel_ && app.audio().has_subsong( "OK" ) )
                {
                    app.audio().play_subsong( result_menu_sound_channel_, "OK", false );
                }

                return;
            }
        }

        const float ssq_global_frame = static_cast<float>( raw_ssq_global_frame );
        const std::size_t ssq_section_index = stage_state_for_3d.section_index;
        const float ssq_section_frame = static_cast<float>( raw_ssq_section_frame );

        const std::int64_t timing_global_sample = static_cast<std::int64_t>( std::llround(
            static_cast<double>( ssq_global_frame ) * stage3d_audio_sample_rate / stage3d_ssq_frames_per_second ) );

        const std::int64_t timing_section_sample = static_cast<std::int64_t>( std::llround(
            static_cast<double>( ssq_section_frame ) * stage3d_audio_sample_rate / stage3d_ssq_frames_per_second ) );

        const bool portable_visuals = load_data_.stage.portable_visuals;
        const float stage_animation_tempo =
            portable_visuals ? song_tempo_at( ssq_section_index, timing_section_sample ) : load_data_.stage.tempo;

        const float portable_animation_frame =
            portable_song_animation_frame_at( ssq_section_index, timing_section_sample, timing_global_sample );

        stage3d_.set_portable_song_animation_frame( portable_animation_frame );

        // Remember the displayed frame
        preview_timing_ =
            { ssq_global_frame, ssq_section_index, ssq_section_frame, stage_animation_tempo, portable_animation_frame };

        preview_timing_valid_ = true;

        // sub_19F130
        stage3d_.set_stage_timing( ssq_global_frame, ssq_section_index, ssq_section_frame, stage_animation_tempo );
        stage3d_.set_stage_result_timing( stage_result_active_, stage_result_won_, stage_result_elapsed_frame_ );

        if( stage_result_active_ )
        {
            current_ssq_action_record_ = {};

            next_ssq_action_record_ = {};

            previous_phrase_action_down_ = false;
            stage3d_.set_action_animation_input( {} );

            for( std::uint32_t model_type = 2u; model_type <= 4u; ++model_type )
            {
                stage3d_.set_actor_action_animation_input( model_type, {} );
            }

            if( stage_audio_.using_audio_clock() )
            {
                stage_audio_.flush_runtime_audio_events( app.audio(), runtime_ );
            }

            return;
        }

        const runtime::stage::StageVisualState stage_visual = runtime_.visual_state();
        latest_aim_x_ = stage_visual.guard_phase ? 0.0f : app.input().left_stick_x();

        latest_aim_y_ = stage_visual.guard_phase ? 0.0f : app.input().left_stick_y();

        latest_aim_strength_ = stage_visual.guard_phase ? 0.0f : app.input().left_stick_strength();

        runtime::stage::StageInputSnapshot stage_input{};
        std::array<bool, input_sources_.size()> source_down{};

        for( std::size_t index = 0; index < input_sources_.size(); ++index )
        {
            const InputSource &source = input_sources_[ index ];
            const bool enabled = !source.optional_dpad || app.settings().guard_notes_accept_dpad;
            source_down[ index ] = enabled && app.input().down_at_frame_start( source.action );
        }

        const std::uint64_t frame_start_ns = app.input().frame_start_timestamp_ns();
        const std::uint64_t frame_end_ns = std::max( frame_start_ns + 1u, SDL_GetTicksNS() );

        const auto frame_position_for = [ frame_start_ns, frame_end_ns ]( std::uint64_t timestamp_ns )
        {
            if( timestamp_ns <= frame_start_ns )
            {
                return 0.0f;
            }

            if( timestamp_ns >= frame_end_ns )
            {
                return 1.0f;
            }

            return static_cast<float>( timestamp_ns - frame_start_ns ) /
                   static_cast<float>( frame_end_ns - frame_start_ns );
        };

        const auto any_source_down = [ &source_down ]()
        {
            return std::any_of( source_down.begin(),
                source_down.end(),
                []( bool down )
                {
                    return down;
                } );
        };

        for( const managers::InputTransition &transition : app.input().transitions() )
        {
            const auto source = std::find_if( input_sources_.begin(),
                input_sources_.end(),
                [ &transition ]( const InputSource &candidate )
                {
                    return candidate.action == transition.action;
                } );

            if( source == input_sources_.end() || ( source->optional_dpad && !app.settings().guard_notes_accept_dpad ) )
            {
                continue;
            }

            const auto source_index = static_cast<std::size_t>( source - input_sources_.begin() );
            const std::int32_t guard_button = utils::to_int( source->guard );
            const bool pressed = transition.kind == managers::InputTransitionKind::Pressed;
            source_down[ source_index ] = pressed;
            const float frame_position = frame_position_for( transition.timestamp_ns );

            const std::uint32_t chart_guard_button = static_cast<std::uint32_t>( std::max( guard_button, 0 ) );

            if( pressed )
            {
                stage_input.events.push_back( runtime::stage::StageInputEvent{
                    runtime::stage::StageInputEventKind::ActionPress,
                    0,
                    frame_position
                } );

                stage_input.action_pressed = true;

                if( guard_button >= 0 )
                {
                    stage_input.events.push_back( runtime::stage::StageInputEvent{
                        runtime::stage::StageInputEventKind::GuardPress,
                        chart_guard_button,
                        frame_position
                    } );

                    stage_input.guard_pressed[ chart_guard_button ] = true;
                }
                continue;
            }

            if( !any_source_down() )
            {
                stage_input.events.push_back( runtime::stage::StageInputEvent{
                    runtime::stage::StageInputEventKind::ActionRelease,
                    0,
                    frame_position
                } );

                stage_input.action_released = true;
            }
        }

        for( std::size_t index = 0; index < input_sources_.size(); ++index )
        {
            const GuardButton guard = input_sources_[ index ].guard;

            if( guard != GuardButton::None )
            {
                stage_input.guard_down[ utils::enum_index( guard ) ] |= source_down[ index ];
            }
        }

        stage_input.action_down = any_source_down();

        if( tutorial_session() || !app.settings().only_aim_mode )
        {
            previous_phrase_action_down_ = stage_input.action_down;
        }

        stage_input.aim_x = latest_aim_x_;

        stage_input.aim_y = latest_aim_y_;

        stage_input.aim_strength = latest_aim_strength_;

        const auto difficulty =
            managers::aim_difficulty( gamedata::difficulty_from_chc_path( load_data_.stage.chc_path ) );

        stage_input.aim_lock_half_angle_degrees =
            app.settings().snap_half_angle_for( difficulty, load_data_.stage.chart_style );

        stage_input.aim_target_scale_x = stage::gameplay_trace_scale_x * debug_presentation_scale_x_;

        stage_input.aim_target_scale_y = stage::gameplay_trace_scale_y * debug_presentation_scale_y_;

        stage_input.automatic_notes = teaching_tutorial() && tutorial_.timeline().automatic_notes();

        if( stage_input.automatic_notes )
        {
            const auto &actor = runtime_.state().actor_actions[ 0 ];

            if( actor.has_aim )
            {
                const float target_x = actor.aim_x * stage_input.aim_target_scale_x;
                const float target_y = actor.aim_y * stage_input.aim_target_scale_y;
                const float length = std::max( std::hypot( target_x, target_y ), 0.0001f );
                latest_aim_x_ = target_x / length;

                latest_aim_y_ = target_y / length;

                latest_aim_strength_ = 1.0f;

                stage_input.aim_x = latest_aim_x_;

                stage_input.aim_y = latest_aim_y_;

                stage_input.aim_strength = 1.0f;
            }
        }

        runtime_.set_instrument_playback_mode( app.settings().instrument_playback );
        runtime_.update_judgement( stage_input );

        if( stage_input.automatic_notes || ( !tutorial_session() && app.settings().only_aim_mode ) )
        {
            const runtime::stage::StageJudgementDebugState &auto_judgement = runtime_.judgement_debug_state();
            stage_input.events.clear();
            stage_input.guard_down.fill( false );
            stage_input.guard_pressed.fill( false );
            stage_input.action_down = auto_judgement.active_phrase_started && !auto_judgement.active_phrase_completed &&
                                      !auto_judgement.active_phrase_missed;

            stage_input.action_pressed = stage_input.action_down && !previous_phrase_action_down_;

            previous_phrase_action_down_ = stage_input.action_down;
        }

        latest_center_pressed_ = stage_input.action_down;

        SsqPlayerActionRecord next_action{};
        next_action.action_down = stage_input.action_down;

        next_action.guard_down = std::any_of( stage_input.guard_down.begin(),
            stage_input.guard_down.end(),
            []( bool down )
            {
                return down;
            } );
        next_action.guard_pressed = std::any_of( stage_input.guard_pressed.begin(),
            stage_input.guard_pressed.end(),
            []( bool pressed )
            {
                return pressed;
            } );

        if( latest_aim_strength_ > 0.20f )
        {
            next_action.has_angle = true;

            next_action.angle_radians = ssq_action_angle_from_aim( latest_aim_x_, latest_aim_y_ );
        }

        const runtime::stage::StageActionMode action_mode = runtime_.current_action_mode();
        const runtime::stage::StageJudgementDebugState &judgement_debug = runtime_.judgement_debug_state();

        if( stage_input.action_down && judgement_debug.active_phrase_started &&
            !judgement_debug.active_phrase_completed && !judgement_debug.active_phrase_missed )
        {
            next_action.effect_hold_owner_model_type = 1u;

            next_action.effect_hold_owner_model_mask |= 1u;
        }

        bool wrote_judgement_action = false;

        const bool new_official_result = judgement_debug.ssq_action_result_serial != 0 &&
                                         judgement_debug.ssq_action_result_serial != last_latched_ssq_result_serial_;

        if( new_official_result )
        {
            last_latched_ssq_result_serial_ = judgement_debug.ssq_action_result_serial;

            const bool player_took_damage = judgement_debug.last_result_energy_applied &&
                                            judgement_debug.last_result_side_index == 0 &&
                                            judgement_debug.last_result_meter_delta < 0.0f;

            switch( judgement_debug.ssq_action_result_state )
            {
                case 1:
                    next_action.descriptor = judgement_debug.ssq_action_result_descriptor + 1u;

                    next_action.event_mask = ssq_event_press_start;

                    next_action.successful_strum_result_serial = judgement_debug.ssq_action_result_serial;
                    // sub_19FC40
                    next_action.effect_owner_model_type = 1u;

                    next_action.effect_result_serial = judgement_debug.ssq_action_result_serial;

                    if( !next_action.has_angle && current_ssq_action_record_.has_angle )
                    {
                        next_action.has_angle = true;

                        next_action.angle_radians = current_ssq_action_record_.angle_radians;
                    }

                    wrote_judgement_action = true;
                    break;

                case 2:
                    next_action.descriptor = 0;

                    next_action.event_mask = ssq_event_bad_or_no_hit;

                    next_action.has_angle = false;

                    wrote_judgement_action = true;
                    break;

                case 3:
                    next_action.descriptor = judgement_debug.ssq_action_result_descriptor + 1u;

                    next_action.event_mask = ssq_event_end_success;

                    next_action.effect_owner_model_type = 1u;

                    next_action.effect_result_serial = judgement_debug.ssq_action_result_serial;

                    if( !next_action.has_angle && current_ssq_action_record_.has_angle )
                    {
                        next_action.has_angle = true;

                        next_action.angle_radians = current_ssq_action_record_.angle_radians;
                    }

                    wrote_judgement_action = true;
                    break;

                case 4:
                    next_action.descriptor = 0;

                    next_action.event_mask =
                        ssq_event_drop_or_miss | ( player_took_damage ? ssq_event_forced_damage : 0u );

                    next_action.has_angle = false;

                    wrote_judgement_action = true;
                    break;

                case 5:
                    next_action.descriptor = judgement_debug.ssq_action_result_descriptor;

                    next_action.event_mask = ssq_event_success_action;
                    // sub_19FC40
                    next_action.effect_owner_model_type = 2u;

                    next_action.effect_result_serial = judgement_debug.ssq_action_result_serial;

                    next_action.has_angle = false;

                    wrote_judgement_action = true;
                    break;

                case 6:
                    next_action.descriptor = 0;

                    next_action.event_mask = ( judgement_debug.ssq_action_result_serial & 1u ) != 0u
                                                 ? ssq_event_alt_damage_a
                                                 : ssq_event_alt_damage_b;

                    next_action.effect_owner_model_type = 2u;

                    next_action.effect_result_serial = judgement_debug.ssq_action_result_serial;

                    next_action.has_angle = false;

                    wrote_judgement_action = true;
                    break;

                default:
                    break;
            }
        }

        if( !wrote_judgement_action )
        {
            if( stage_input.action_down && judgement_debug.active_phrase_started &&
                !judgement_debug.active_phrase_completed && !judgement_debug.active_phrase_missed &&
                judgement_debug.active_phrase_action_descriptor != 0 )
            {

                // sub_19DA20
                next_action.descriptor = judgement_debug.active_phrase_action_descriptor;

                next_action.event_mask = ssq_event_hold;
                // sub_1A5C90
                next_action.effect_hold_owner_model_type = 1u;

                next_action.effect_hold_owner_model_mask |= 1u;

                if( !next_action.has_angle && current_ssq_action_record_.has_angle )
                {
                    next_action.has_angle = true;

                    next_action.angle_radians = current_ssq_action_record_.angle_radians;
                }
            }
            else if( next_action.has_angle && action_mode != runtime::stage::StageActionMode::Guard )
            {

                next_action.descriptor = 0;

                next_action.event_mask = ssq_event_aim_or_pending;
            }
            else
            {
                next_action.descriptor = 0;

                next_action.event_mask = 0;

                next_action.has_angle = false;
            }
        }

        next_ssq_action_record_ = next_action;

        current_ssq_action_record_ = next_ssq_action_record_;

        Stage3DRenderer::ActionAnimationInput action_animation{};
        action_animation.event_flags = current_ssq_action_record_.event_mask;

        action_animation.descriptor = current_ssq_action_record_.descriptor;

        action_animation.aim_angle_radians = current_ssq_action_record_.angle_radians;

        action_animation.has_aim_angle = current_ssq_action_record_.has_angle;

        action_animation.action_down = current_ssq_action_record_.action_down;

        action_animation.guard_down = current_ssq_action_record_.guard_down;

        action_animation.guard_pressed = current_ssq_action_record_.guard_pressed;

        action_animation.successful_strum_result_serial = current_ssq_action_record_.successful_strum_result_serial;

        action_animation.always_retrigger_player_strum = app.settings().always_retrigger_player_strum;

        action_animation.effect_owner_model_type = current_ssq_action_record_.effect_owner_model_type;

        action_animation.effect_result_serial = current_ssq_action_record_.effect_result_serial;

        action_animation.effect_hold_owner_model_type = current_ssq_action_record_.effect_hold_owner_model_type;

        action_animation.effect_hold_owner_model_mask = current_ssq_action_record_.effect_hold_owner_model_mask;

        if( action_animation.effect_hold_owner_model_type >= 1u && action_animation.effect_hold_owner_model_type <= 4u )
        {
            action_animation.effect_hold_owner_model_mask |= 1u
                                                             << ( action_animation.effect_hold_owner_model_type - 1u );
        }

        action_animation.fixed_sprite_active_model_mask = action_animation.effect_hold_owner_model_mask;
        stage3d_.set_action_animation_input( action_animation );

        for( std::size_t source_player_index = 1; source_player_index < 4; ++source_player_index )
        {
            const std::uint32_t model_type = static_cast<std::uint32_t>( source_player_index + 1u );
            const runtime::stage::StageActorActionState &actor = runtime_.state().actor_actions[ source_player_index ];

            const auto ai_animation = make_actor_animation_input( actor, model_type, runtime_.duet_stage() );
            stage3d_.set_actor_action_animation_input( model_type, ai_animation );
        }

        if( stage_audio_.using_audio_clock() )
        {
            stage_audio_.flush_runtime_audio_events( app.audio(), runtime_ );
        }
    }

    void StageModule::render( App &app, rendering::Renderer &renderer )
    {
        const std::int32_t output_w = renderer.output_width();
        const std::int32_t output_h = renderer.output_height();
        const float width = static_cast<float>( output_w );
        const float height = static_cast<float>( output_h );
        const float pulse = 0.5f + 0.5f * std::sin( static_cast<float>( elapsed_seconds_ ) * 2.0f );

        renderer.set_stage_render_scale( static_cast<float>( app.settings().render_resolution_percent ) / 100.0f );
        renderer.set_stage_msaa_samples( app.settings().msaa_samples );

        rendering::TextureSamplingState sampling{};
        sampling.mode = renderer_filter_mode_from_settings( app.settings().texture_filtering_mode );

        sampling.anisotropy = app.settings().texture_anisotropy;
        renderer.set_texture_sampling( sampling );
        renderer.set_msaa_enabled( app.settings().msaa_enabled && app.settings().render_resolution_percent <= 100 );
        stage3d_.set_lighting_backend_index( app.settings().per_pixel_lighting ? 0 : 1 );

        const auto bloom = app.settings().bloom_settings();
        const rendering::StagePostProcessSettings
            post_settings{ bloom.enabled, bloom.threshold, bloom.intensity, bloom.iterations, bloom.radius };

        const bool using_stage_post = renderer.begin_stage_post_scene( post_settings );

        const bool rendered_stage_3d = stage3d_.render( app, renderer );

        if( !rendered_stage_3d )
        {
            renderer.draw_filled_rect( rendering::Rect{ 0.0f, 0.0f, width, height },
                rendering::Color{ 0.008f, 0.012f, 0.030f, 1.0f } );

            for( std::int32_t i = 0; i < 16; ++i )
            {
                const float t = static_cast<float>( i ) / 15.0f;
                renderer.draw_filled_rect( rendering::Rect{ 0.0f, t * height, width, height / 15.0f + 2.0f },
                    rendering::Color{ 0.010f, 0.016f + 0.014f * t, 0.040f + 0.060f * t, 1.0f } );
            }
        }

        if( using_stage_post )
        {
            renderer.end_stage_post_scene_and_composite( post_settings );
        }

        const rendering::Rect trace_area{ 0.0f, 0.0f, width, height };
        const rendering::Texture *itfc_texture = app.texture_manager().get( itfc_texture_ );
        const rendering::Texture *tube_texture = app.texture_manager().get( generated_tube_texture_ );
        const rendering::Texture *trace_texture = app.texture_manager().get( generated_trace_texture_ );
        const stage::GuardIconTextures custom_guard_icons =
            stage::guard_icon_textures( app.texture_manager(), guard_icon_handles_ );

        if( !stage_result_active_ )
        {
            render_trace_runtime( renderer,
                itfc_texture,
                tube_texture,
                trace_texture,
                app.texture_manager().get( aim_arrow_texture_ ),
                app.texture_manager().get( wing_arrow_texture_ ),
                app.texture_manager().get( center_nub_texture_ ),
                app.texture_manager().get( pressed_nub_texture_ ),
                custom_guard_icons,
                trace_area,
                app.settings(),
                pulse );

            render_mouse_tether( app, renderer, width, height );
            render_bpm_edge_glow( app, renderer, width, height );
            render_fullscreen_hud( app, renderer, width, height, pulse );
        }

        if( tutorial_session() )
        {
            if( teaching_tutorial() && !tutorial_completion_active_ )
            {
                tutorial_.render( app, renderer );
            }

            if( tutorial_completion_active_ )
            {
                tutorial_.render_completion( app,
                    renderer,
                    tutorial_retry_selected_,
                    static_cast<float>( elapsed_seconds_ ) );
            }
        }
        else
        {
            render_stage_result( app, renderer, width, height );
        }

        const rendering::CrtOverlaySettings crt_settings{
            app.settings().crt_filter_enabled,
            app.settings().crt_scanline_strength,
            app.settings().crt_vignette_strength,
            app.settings().crt_noise_strength,
            static_cast<float>( elapsed_seconds_ )
        };

        renderer.draw_crt_overlay( crt_settings );
#if defined( _DEBUG )
        render_preview_timing( renderer, width, height );
        render_imgui_debug( app, renderer );
#endif
    }
}
