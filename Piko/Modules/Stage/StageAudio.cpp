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
    const char *StageModule::stage_audio_source_channel_role( std::int32_t source_channel,
        std::int32_t source_channel_count,
        managers::AudioPhraseStemMode phrase_stem_mode )
    {
        if( source_channel_count <= 1 )
        {
            return "MONO";
        }

        if( source_channel_count == 2 )
        {
            return source_channel == 0 ? "L" : "R";
        }

        switch( source_channel )
        {
            case 0:
                return "BG L";
            case 1:
                return "BG R";
            case 2:
                return phrase_stem_mode == managers::AudioPhraseStemMode::MonoCh2Center ? "PL M" : "PL L";
            case 3:
                return "EN L";
            case 4:
                return phrase_stem_mode == managers::AudioPhraseStemMode::MonoCh2Center ? "NP R" : "PL R";
            case 5:
                return "EN R";
            case 6:
                return "FX L";
            case 7:
                return "FX R";
            default:
                return ( source_channel & 1 ) == 0 ? "EX L" : "EX R";
        }
    }

    std::int32_t StageModule::peak_percent( float peak )
    {
        return static_cast<std::int32_t>( std::lround( std::clamp( peak, 0.0f, 1.0f ) * 100.0f ) );
    }

    std::filesystem::path StageModule::resolve_stage_audio_path( const App &app ) const
    {
        if( !load_data_.chc_loaded )
        {
            return {};
        }

        if( const std::filesystem::path &runtime_audio = load_data_.stage.runtime_audio_path;
            piko::utils::regular_file( runtime_audio / "audio.json" ) )
        {
            return runtime_audio;
        }

        const gamedata::ChartStyle chart_style =
            load_data_.stage.portable_visuals ? gamedata::ChartStyle::GitarooManLives : app.settings().chart_style;

        if( const std::filesystem::path runtime_audio =
                app.game_data().runtime_stage_audio( load_data_.stage.stage_number,
                    chart_style,
                    load_data_.stage.chc_path.filename().string() );
            piko::utils::regular_file( runtime_audio / "audio.json" ) )
        {
            return runtime_audio;
        }

        if( chart_style == gamedata::ChartStyle::GitarooManLives )
        {
            if( const std::filesystem::path runtime_audio =
                    app.game_data().runtime_stage_audio( load_data_.stage.stage_number,
                        gamedata::ChartStyle::GitarooMan,
                        load_data_.stage.chc_path.filename().string() );
                piko::utils::regular_file( runtime_audio / "audio.json" ) )
            {
                return runtime_audio;
            }
        }

        return {};
    }

    void StageModule::initialize_stage_audio( App &app )
    {
        if( !runtime_initialized_ || !runtime_.has_song() || !load_data_.chc_loaded )
        {
            return;
        }

        const std::filesystem::path audio_path = resolve_stage_audio_path( app );
        const std::filesystem::path fallback_audio_path =
            app.game_data().runtime_stage_audio( load_data_.stage.stage_number,
                gamedata::ChartStyle::GitarooMan,
                load_data_.stage.chc_path.filename().string() );

        if( audio_path.empty() )
        {
            debug_message_ = "Stage audio: converted audio bank or audio.json is missing for Stage " +
                             std::to_string( load_data_.stage.stage_number );

            std::cerr << "[Piko Runtime] " << debug_message_ << '\n';
            return;
        }

        if( stage_audio_.initialize( app.audio(),
                load_data_.song,
                audio_path,
                piko::utils::regular_file( fallback_audio_path / "audio.json" ) ? fallback_audio_path
                                                                                : std::filesystem::path{},
                runtime_,
                load_data_.stage.stereo_phrases ) )
        {
            const std::filesystem::path menu_audio_path = app.game_data().main_menu_audio_path();

            if( !menu_audio_path.empty() )
            {
                app.audio().merge_audio_bank( menu_audio_path );
                app.audio().preload_subsongs( { "SELECT", "OK", "CANCEL" } );
            }

            result_menu_sound_channel_ = app.audio().claim_channel( "Stage Result Menu" );

            debug_message_ = "[Piko Runtime] Audio: Converted bank " + audio_path.string();
            std::cout << debug_message_ << '\n';
        }
        else
        {
            debug_message_ = stage_audio_.status_line().empty()
                                 ? std::string( "Stage audio failed: " ) + app.audio().last_error()
                                 : stage_audio_.status_line();

            std::cerr << "[Piko Runtime] " << debug_message_ << '\n';
        }
    }

    void StageModule::shutdown_stage_audio( App &app )
    {
        stage_audio_.shutdown( app.audio() );
    }

    float StageModule::song_tempo_at( std::size_t section_index, std::int64_t section_sample ) const
    {
        if( !load_data_.chc_loaded || section_index >= load_data_.song.sections.size() )
        {
            return load_data_.stage.tempo;
        }

        const filetypes::chc::SongSection &section = load_data_.song.sections[ section_index ];
        float tempo = std::isfinite( section.tempo ) && section.tempo > 0.0f ? section.tempo : load_data_.stage.tempo;
        const std::int64_t sample = std::max<std::int64_t>( 0, section_sample );

        for( const filetypes::chc::TempoChange &change : section.tempo_changes )
        {
            if( static_cast<std::int64_t>( change.start_sample ) > sample )
            {
                break;
            }

            if( std::isfinite( change.tempo ) && change.tempo > 0.0f )
            {
                tempo = change.tempo;
            }
        }

        return std::isfinite( tempo ) && tempo > 0.0f ? tempo : 120.0f;
    }

    float StageModule::portable_song_animation_frame_at( std::size_t section_index,
        std::int64_t section_sample,
        std::int64_t global_sample ) const
    {
        if( !load_data_.stage.portable_visuals )
        {
            return 0.0f;
        }

        const float tempo = song_tempo_at( section_index, section_sample );
        const double sample = static_cast<double>( std::max<std::int64_t>( 0, global_sample ) );
        const double song_frame = sample * static_cast<double>( tempo ) * 8.0 / ( stage3d_audio_sample_rate * 60.0 );
        return static_cast<float>( song_frame );
    }
}
