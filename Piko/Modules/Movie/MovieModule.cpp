#include "MovieModule.hpp"

#include "App/piko.hpp"
#include "FileTypes/CHC/CHC.hpp"
#include "FileTypes/CHC/CHCJson.hpp"
#include "GameData/AlbumFlow.hpp"
#include "Managers/InputManager.hpp"
#include "Modules/Loading/LoadingModule.hpp"
#include "Modules/MainMenu/MainMenuModule.hpp"
#include "Modules/Stage/StageModule.hpp"
#include "Modules/StageSelect/StageSelectModule.hpp"
#include "Rendering/Renderer.hpp"
#include "Runtime/Audio/Pcm16.hpp"

#include <cstdint>
#include <algorithm>
#include <iostream>
#include <memory>
#include <utility>

namespace piko::modules
{
    MovieModule::MovieModule( std::int32_t album_page_id, std::filesystem::path movie_path )
        : album_page_id_( album_page_id ), movie_path_( std::move( movie_path ) )
    {
    }

    void MovieModule::enter( App &app )
    {
        elapsed_seconds_ = 0.0;

        frame_accumulator_ = 0.0;

        frame_dirty_ = false;

        advancing_ = false;
        status_.clear();
        audio_name_ = "movie/page-" + std::to_string( album_page_id_ );

        std::cout << "Playing movie: " << movie_path_.string() << '\n';
        open_ok_ = player_.open( movie_path_ );

        if( !open_ok_ )
        {
            status_ = player_.last_error();
            std::cerr << "Movie playback failed: " << status_ << '\n';
            return;
        }

        runtime::audio::Pcm16 pcm = player_.take_audio();

        if( pcm.frame_count() > 0 )
        {
            audio_channel_ = app.audio().claim_channel( "FMV" );

            if( !app.audio().play_pcm( audio_channel_, audio_name_, std::move( pcm ), false ) )
            {
                status_ = app.audio().last_error();
            }
        }

        present_next_frame();
    }

    void MovieModule::leave( App &app )
    {
        if( audio_channel_ )
        {
            app.audio().stop_channel( audio_channel_ );
            app.audio().release_channel( audio_channel_ );
            audio_channel_ = {};
        }

        player_.close();
        app.renderer().destroy_texture( texture_ );
        pending_frame_ = {};

        frame_dirty_ = false;
    }

    void MovieModule::update( App &app, double delta_seconds )
    {
        if( advancing_ )
        {
            return;
        }

        elapsed_seconds_ += std::max( 0.0, delta_seconds );

        frame_accumulator_ += std::max( 0.0, delta_seconds );
        const double frame_seconds = 1.0 / player_.frames_per_second();

        while( frame_accumulator_ >= frame_seconds )
        {
            if( !present_next_frame() )
            {
                break;
            }

            frame_accumulator_ -= frame_seconds;
        }

        const bool skip_requested =
            elapsed_seconds_ >= 0.20 && ( app.input().pressed( managers::InputAction::Confirm ) ||
                                            app.input().pressed( managers::InputAction::Back ) );

        const bool audio_finished = !audio_channel_ || !app.audio().is_channel_playing( audio_channel_ );
        const bool natural_finish = open_ok_ && player_.video_finished() && audio_finished;
        const bool failed_finish = ( !open_ok_ || player_.failed() ) && elapsed_seconds_ >= 0.25;

        if( skip_requested || natural_finish || failed_finish )
        {
            advance( app );
        }
    }

    void MovieModule::render( App &app, rendering::Renderer &renderer )
    {
        if( frame_dirty_ && !pending_frame_.rgba.empty() )
        {
            if( renderer.update_texture_rgba( texture_,
                    pending_frame_.rgba.data(),
                    player_.width(),
                    player_.height() ) )
            {
                frame_dirty_ = false;
            }
        }

        const float width = static_cast<float>( renderer.output_width() );
        const float height = static_cast<float>( renderer.output_height() );
        renderer.draw_filled_rect( rendering::Rect{ 0.0f, 0.0f, width, height },
            rendering::Color{ 0.0f, 0.0f, 0.0f, 1.0f } );

        if( texture_.valid() )
        {
            const float scale = std::min( width / static_cast<float>( texture_.width ),
                height / static_cast<float>( texture_.height ) );

            const float draw_width = static_cast<float>( texture_.width ) * scale;
            const float draw_height = static_cast<float>( texture_.height ) * scale;
            renderer.draw_texture_opaque( texture_,
                ( width - draw_width ) * 0.5f,
                ( height - draw_height ) * 0.5f,
                draw_width,
                draw_height );
        }
        else if( !status_.empty() )
        {
            renderer.draw_text( 28.0f,
                height - 52.0f,
                status_.c_str(),
                rendering::Color{ 1.0f, 0.72f, 0.72f, 1.0f },
                1.0f );
        }

        (void)app;
    }

    void MovieModule::advance( App &app )
    {
        if( advancing_ )
        {
            return;
        }

        advancing_ = true;

        try
        {
            const std::optional<std::int32_t> next_page =
                gamedata::album_return_page( app.game_data(), album_page_id_, 0 );

            if( !next_page.has_value() )
            {
                app.change_module( std::make_unique<StageSelectModule>() );
                return;
            }

            const gamedata::AlbumFlowTarget target = gamedata::resolve_album_flow_target( app.game_data(),
                *next_page,
                app.settings().chart_style,
                app.settings().play_movies );

            if( target.type == gamedata::AlbumFlowTargetType::Movie )
            {
                app.change_module( std::make_unique<MovieModule>( target.page_id, target.movie_path ) );
                return;
            }

            if( target.type == gamedata::AlbumFlowTargetType::MainMenu )
            {
                app.change_module( std::make_unique<MainMenuModule>() );
                return;
            }

            if( target.type == gamedata::AlbumFlowTargetType::Stage )
            {
                LoadingTask task = make_stage_loading_task( target.stage,
                    []()
                    {
                        return std::make_unique<StageSelectModule>();
                    } );
                app.change_module( std::make_unique<LoadingModule>( std::move( task ) ) );
                return;
            }
        }
        catch( ... )
        {
        }

        app.change_module( std::make_unique<StageSelectModule>() );
    }

    bool MovieModule::present_next_frame()
    {
        runtime::movie::MovieFrame frame{};

        if( !player_.pop_frame( frame ) )
        {
            return false;
        }

        pending_frame_ = std::move( frame );

        frame_dirty_ = true;
        return true;
    }
}
