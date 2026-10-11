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
    std::int64_t StageModule::practice_section_global_sample() const
    {
        if( !load_data_.chc_loaded || load_data_.practice_section_index >= load_data_.song.sections.size() )
        {
            return 0;
        }

        if( load_data_.practice_section_index < load_data_.song.cues.size() )
        {
            const float first_frame = load_data_.song.cues[ load_data_.practice_section_index ].first_frame;

            if( std::isfinite( first_frame ) && first_frame > 0.0f )
            {
                return static_cast<std::int64_t>( std::llround(
                    static_cast<double>( first_frame ) * stage3d_audio_sample_rate / stage3d_ssq_frames_per_second ) );
            }
        }

        std::int64_t sample = 0;

        for( std::size_t index = 0; index < load_data_.practice_section_index; ++index )
        {
            sample += static_cast<std::int64_t>( load_data_.song.sections[ index ].duration_samples );
        }

        return sample;
    }

    void StageModule::restart_practice_section( App &app )
    {
        if( load_data_.session_mode != StageSessionMode::Practice || !runtime_.has_song() )
        {
            return;
        }

        runtime_.set_paused( false );
        stage_audio_.set_paused( app.audio(), false );
        runtime_.start( load_data_.practice_section_index, practice_section_global_sample_ );
        runtime_.set_local_energy_immediate( 1.0f );
        stage_audio_.restart_from_runtime( app.audio(), runtime_ );

        stage_result_active_ = false;

        stage_result_won_ = true;

        stage_result_audio_started_ = false;

        stage_result_handoff_started_ = false;

        stage_result_elapsed_frame_ = 0.0f;

        previous_phrase_action_down_ = false;

        current_ssq_action_record_ = {};

        next_ssq_action_record_ = {};

        last_latched_ssq_result_serial_ = 0;

        practice_last_timing_sample_ = -1;

        practice_last_timing_section_ = load_data_.practice_section_index;

        practice_timing_cues_ = runtime_.timing_cue_samples();

        practice_next_timing_cue_ = 0;
    }

    void StageModule::update_practice_timing_pulse( App &app )
    {
        if( load_data_.session_mode != StageSessionMode::Practice || !runtime_.has_song() || runtime_.paused() )
        {
            return;
        }

        const runtime::stage::StageRuntimeState &state = runtime_.state();

        if( state.song_finished )
        {
            return;
        }

        if( practice_last_timing_section_ != state.section_index ||
            state.section_sample < practice_last_timing_sample_ )
        {
            practice_last_timing_section_ = state.section_index;

            practice_last_timing_sample_ = -1;

            practice_timing_cues_ = runtime_.timing_cue_samples();

            practice_next_timing_cue_ = 0;
        }

        constexpr std::int64_t rumble_lead_samples = stage::sample_rate * 24 / 1000;
        const std::int64_t current = state.section_sample;
        const std::int64_t marker_sample = current + rumble_lead_samples;

        while( practice_next_timing_cue_ < practice_timing_cues_.size() &&
               practice_timing_cues_[ practice_next_timing_cue_ ] < 0 )
        {
            ++practice_next_timing_cue_;
        }

        bool pulse = false;
        while( practice_next_timing_cue_ < practice_timing_cues_.size() &&
               practice_timing_cues_[ practice_next_timing_cue_ ] <= marker_sample )
        {
            if( practice_timing_cues_[ practice_next_timing_cue_ ] > practice_last_timing_sample_ )
            {
                pulse = true;
            }

            ++practice_next_timing_cue_;
        }

        if( pulse )
        {
            app.input().rumble_motors( 0.38f, 0.62f, 55 );
        }

        practice_last_timing_sample_ = marker_sample;
    }

    void StageModule::return_to_stage_select( App &app )
    {
        if( tutorial_session() )
        {
            const auto category = load_data_.stage.tutorial_difficulty == gamedata::TutorialDifficulty::Easy
                                      ? StageSelectModule::StageCategory::SingleEasy
                                      : StageSelectModule::StageCategory::SingleNormal;

            app.change_module( std::make_unique<StageSelectModule>( category ) );
            return;
        }

        if( load_data_.session_mode == StageSessionMode::TrueGitaroo )
        {
            app.change_module( std::make_unique<MainMenuModule>() );
            return;
        }

        if( load_data_.session_mode == StageSessionMode::Practice )
        {
            app.change_module( std::make_unique<StageSelectModule>( StageSelectModule::StageCategory::SingleNormal,
                StageSelectModule::LaunchMode::Practice ) );

            return;
        }

        app.change_module( std::make_unique<StageSelectModule>() );
    }

    bool StageModule::tutorial_session() const
    {
        return load_data_.stage.mode == gamedata::PlayMode::Tutorial &&
               load_data_.stage.chart_style == gamedata::ChartStyle::GitarooMan && !load_data_.stage.portable_visuals;
    }

    bool StageModule::teaching_tutorial() const
    {
        return tutorial_session() && utils::uppercase_ascii( load_data_.stage.chc_path.stem().string() ) != "ST00B";
    }

    std::optional<StageLoadData> StageModule::load_tutorial_practice( App &app ) const
    {
        try
        {
            auto stage = load_data_.stage;
            stage.chc_path = app.game_data().resolve_runtime_asset( "STAGE00/CHARTS/PS2/ST00B.json" );

            if( stage.chc_path.empty() )
            {
                return std::nullopt;
            }

            stage.runtime_audio_path =
                app.game_data().resolve_runtime_asset( "STAGE00/AUDIO/PS2/ST00B/audio.json" ).parent_path();

            stage.chc_exists = true;

            stage.using_runtime_chart = true;
            StageLoadData next;
            next.stage = std::move( stage );

            next.song = filetypes::chc::load_json_file( next.stage.chc_path );

            next.chc_loaded = true;
            return next;
        }
        catch( const std::exception &exception )
        {
            std::cerr << "[Piko Tutorial] Practice load failed: " << exception.what() << '\n';
            return std::nullopt;
        }
    }

    std::optional<StageLoadData> StageModule::load_tutorial_next_stage( App &app ) const
    {
        try
        {
            const auto stages =
                gamedata::load_playable_stage_catalog( app.game_data(), gamedata::ChartStyle::GitarooMan );

            const char *difficulty =
                load_data_.stage.tutorial_difficulty == gamedata::TutorialDifficulty::Easy ? "Easy" : "Normal";

            const auto entry = std::find_if( stages.begin(),
                stages.end(),
                [ difficulty ]( const auto &stage )
                {
                    return stage.stage_number == 1 && stage.mode == gamedata::PlayMode::SinglePlayer &&
                           !stage.portable_visuals && stage.chc_exists &&
                           gamedata::difficulty_from_chc_path( stage.chc_path ) == difficulty;
                } );

            if( entry == stages.end() )
            {
                std::cerr << "[Piko Tutorial] Stage 1 " << difficulty << " chart is unavailable\n";
                return std::nullopt;
            }

            StageLoadData next;
            next.stage = *entry;

            next.song = filetypes::chc::load_json_file( next.stage.chc_path );

            next.chc_loaded = true;
            return next;
        }
        catch( const std::exception &exception )
        {
            std::cerr << "[Piko Tutorial] Stage 1 load failed: " << exception.what() << '\n';
            return std::nullopt;
        }
    }

    void StageModule::start_tutorial_next_stage( App &app )
    {
        auto next = load_tutorial_next_stage( app );

        if( !next )
        {
            return;
        }

        if( app.settings().play_movies )
        {
            try
            {
                if( const auto page = gamedata::album_intro_movie_page( app.game_data(), next->stage.page_id ) )
                {
                    const auto target =
                        gamedata::resolve_album_flow_target( app.game_data(), *page, gamedata::ChartStyle::GitarooMan );

                    if( target.type == gamedata::AlbumFlowTargetType::Movie && !target.movie_path.empty() )
                    {
                        app.change_module( std::make_unique<MovieModule>( target.page_id, target.movie_path ) );
                        return;
                    }
                }
            }
            catch( const std::exception &exception )
            {
                std::cerr << "[Piko Tutorial] Stage 1 movie: " << exception.what() << '\n';
            }
        }

        app.change_module( std::make_unique<StageModule>( std::move( *next ) ) );
    }

    double StageModule::play_tutorial_menu_sound( App &app, const char *name ) const
    {
        if( result_menu_sound_channel_ && app.audio().has_subsong( name ) &&
            app.audio().play_subsong( result_menu_sound_channel_, name, false ) )
        {
            return static_cast<double>( app.audio().subsong_frame_count( name ) ) / stage3d_audio_sample_rate;
        }

        return 0.0;
    }

    bool StageModule::update_tutorial_session( App &app )
    {
        if( !tutorial_session() )
        {
            return false;
        }

        if( !tutorial_completion_active_ )
        {
            if( teaching_tutorial() )
            {
                tutorial_.update( runtime_.state().global_sample );
            }

            if( !runtime_.state().song_finished )
            {
                return false;
            }

            if( teaching_tutorial() )
            {
                if( auto practice = load_tutorial_practice( app ) )
                {
                    app.change_module( std::make_unique<StageModule>( std::move( *practice ) ) );
                    return true;
                }
            }

            tutorial_completion_active_ = true;

            tutorial_retry_selected_ = true;
            tutorial_.begin_completion( app );
            runtime_.set_paused( true );
            stage_audio_.stop_song( app.audio() );
            app.input().set_battle_input( app.window(), false, false );
            return true;
        }

        if( tutorial_choice_ != TutorialChoice::None )
        {
            if( elapsed_seconds_ < tutorial_choice_ready_seconds_ )
            {
                return true;
            }

            const auto choice = tutorial_choice_;
            tutorial_choice_ = TutorialChoice::None;

            if( choice == TutorialChoice::Again )
            {
                app.change_module( std::make_unique<StageModule>( load_data_ ) );
            }
            else if( choice == TutorialChoice::Next )
            {
                start_tutorial_next_stage( app );
            }
            else
            {
                return_to_stage_select( app );
            }

            return true;
        }

        const bool previous_selection = tutorial_retry_selected_;

        const auto &input = app.input();

        if( input.pressed( managers::InputAction::MenuLeft ) || input.pressed( managers::InputAction::MenuRight ) )
        {
            tutorial_retry_selected_ = !tutorial_retry_selected_;
        }

        const auto &mouse = input.mouse();
        bool mouse_confirm = false;

        for( const bool retry : { true, false } )
        {
            const bool inside =
                stage::StageTutorialRenderer::completion_button_contains( app.renderer(), retry, mouse.x, mouse.y );

            if( inside && ( mouse.moved || mouse.left_clicked ) )
            {
                tutorial_retry_selected_ = retry;

                mouse_confirm = mouse.left_clicked;
            }
        }

        if( tutorial_retry_selected_ != previous_selection )
        {
            (void)play_tutorial_menu_sound( app, "SELECT" );
        }

        if( input.pressed( managers::InputAction::Back ) )
        {
            tutorial_choice_ = TutorialChoice::Back;

            tutorial_choice_ready_seconds_ = elapsed_seconds_ + play_tutorial_menu_sound( app, "CANCEL" );
        }
        else if( input.pressed( managers::InputAction::Confirm ) || mouse_confirm )
        {
            tutorial_choice_ = tutorial_retry_selected_ ? TutorialChoice::Again : TutorialChoice::Next;

            tutorial_choice_ready_seconds_ = elapsed_seconds_ + play_tutorial_menu_sound( app, "OK" );
        }

        return true;
    }
}
