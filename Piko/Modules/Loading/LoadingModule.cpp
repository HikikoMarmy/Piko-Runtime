#include "Localization/Language.hpp"
#include "Localization/LabelTexture.hpp"
#include "LoadingModule.hpp"
#include <iostream>

#include "App/piko.hpp"
#include "FileTypes/CHC/CHCJson.hpp"
#include "Managers/InputManager.hpp"
#include "Modules/Stage/StageModule.hpp"
#include "Rendering/Renderer.hpp"
#include "Utils/MathUtil.hpp"

#include <cstdint>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace piko::modules
{
    LoadingTask make_stage_loading_task( const gamedata::PlayableStageEntry &stage,
        std::function<std::unique_ptr<PikoModule>()> create_cancel_module )
    {
        LoadingTask task{};
        task.title = localization::text( "@ui.loading" ) + localization::stage_name( stage );

        task.initial_status = localization::text( "@ui.preparing_stage" );
        const std::int32_t stage_number = std::clamp( stage.stage_number, 0, 99 );
        task.stage_number = stage_number;
        char ps2_asset[ 96 ]{};

        if( stage.mode == gamedata::PlayMode::Multiplayer )
        {
            std::snprintf( ps2_asset, sizeof( ps2_asset ), "LOADING/TEXTURES/STANDALONE/V_LOAD.png" );
        }
        else
        {
            std::snprintf( ps2_asset, sizeof( ps2_asset ), "LOADING/TEXTURES/STANDALONE/LOADST%02d.png", stage_number );
        }

        task.background_asset = ps2_asset;

        if( stage.portable_visuals )
        {
            const std::string name = stage.mode == gamedata::PlayMode::Multiplayer || stage_number >= 11
                                         ? "LOAD_VS.png"
                                         : "LOAD_ST" + std::to_string( stage_number ) + ".png";

            const std::string group =
                std::string( "STAGE" ) + ( stage_number < 10 ? "0" : "" ) + std::to_string( stage_number );

            task.background_asset = group + "/TEXTURES/LOADING/" + name;

            task.background_candidates = { "UI/LOADING/" + name, "UI/LOAD/" + name, ps2_asset };
        }

        task.fallback_background_asset = "LOADING/TEXTURES/STANDALONE/LOADING.png";

        auto load_data = std::make_shared<StageLoadData>();
        load_data->stage = stage;

        task.work = [ load_data ]( LoadingProgress &progress )
        {
            if( progress.cancel_requested() )
            {
                return;
            }

            progress.set_progress( 0.12f );

            if( load_data->stage.chc_path.empty() || !load_data->stage.chc_exists )
            {
                throw std::runtime_error( "Selected stage has no converted chart JSON" );
            }

            progress.set_progress( 0.28f );
            load_data->song = filetypes::chc::load_json_file( load_data->stage.chc_path );

            load_data->chc_loaded = true;

            if( progress.cancel_requested() )
            {
                return;
            }

            progress.set_progress( 0.82f );
            progress.set_progress( 1.0f );
        };

        task.create_next_module = [ load_data ]()
        {
            return std::make_unique<StageModule>( std::move( *load_data ) );
        };

        task.create_cancel_module = std::move( create_cancel_module );
        return task;
    }

    void LoadingProgress::set_progress( float progress )
    {
        const std::int32_t per_thousand =
            static_cast<std::int32_t>( std::round( piko::utils::clamp01( progress ) * 1000.0f ) );

        progress_per_thousand_.store( per_thousand, std::memory_order_relaxed );
    }

    float LoadingProgress::progress() const
    {
        return static_cast<float>( progress_per_thousand_.load( std::memory_order_relaxed ) ) / 1000.0f;
    }

    void LoadingProgress::set_status( std::string status )
    {
        std::scoped_lock lock( status_mutex_ );
        status_ = std::move( status );
    }

    std::string LoadingProgress::status() const
    {
        std::scoped_lock lock( status_mutex_ );
        return status_;
    }

    void LoadingProgress::request_cancel()
    {
        cancel_requested_.store( true, std::memory_order_relaxed );
    }

    bool LoadingProgress::cancel_requested() const
    {
        return cancel_requested_.load( std::memory_order_relaxed );
    }

    LoadingModule::LoadingModule( LoadingTask task ) : task_( std::move( task ) )
    {
    }

    LoadingModule::~LoadingModule()
    {
        progress_.request_cancel();
        join_worker();
    }

    void LoadingModule::enter( App &app )
    {
        elapsed_seconds_ = 0.0;

        transition_requested_ = false;
        worker_done_.store( false, std::memory_order_relaxed );
        worker_failed_.store( false, std::memory_order_relaxed );
        worker_started_.store( false, std::memory_order_relaxed );

        progress_.set_progress( 0.0f );
        progress_.set_status( task_.initial_status );

        const auto label_path = app.game_data().resolve_runtime_asset( "LOADING/TEXTURES/STANDALONE/LOADING.png" );

        if( !label_path.empty() )
        {
            try
            {
                loading_label_texture_ = app.texture_manager().load_png( "loading/lettering", label_path );
            }
            catch( const std::exception &error )
            {
                std::cerr << "[Piko Runtime] Loading PNG failed: " << error.what() << '\n';
            }
        }

        load_background( app );

        if( !task_.work )
        {
            worker_done_.store( true, std::memory_order_release );
            progress_.set_progress( 1.0f );
            return;
        }

        start_worker();
    }

    void LoadingModule::leave( App &app )
    {
        progress_.request_cancel();
        join_worker();
        unload_background( app );
        app.texture_manager().unload( loading_label_texture_ );
        loading_label_texture_ = {};
    }

    void LoadingModule::start_worker()
    {
        worker_started_.store( true, std::memory_order_release );

        worker_ = std::thread(
            [ this ]()
            {
                try
                {
                    task_.work( progress_ );

                    if( !progress_.cancel_requested() )
                    {
                        progress_.set_progress( 1.0f );
                    }
                }
                catch( const std::exception &e )
                {
                    worker_failed_.store( true, std::memory_order_relaxed );
                    store_error( e.what() );
                }
                catch( ... )
                {
                    worker_failed_.store( true, std::memory_order_relaxed );
                    store_error( "Unknown loading error" );
                }

                worker_done_.store( true, std::memory_order_release );
            } );
    }

    void LoadingModule::join_worker()
    {
        if( worker_.joinable() )
        {
            worker_.join();
        }
    }

    void LoadingModule::store_error( std::string message )
    {
        std::scoped_lock lock( error_mutex_ );
        error_message_ = std::move( message );
    }

    std::string LoadingModule::error_message() const
    {
        std::scoped_lock lock( error_mutex_ );
        return error_message_;
    }

    void LoadingModule::load_background( App &app )
    {
        unload_background( app );

        const auto try_load = [ & ]( const std::string &asset )
        {
            if( asset.empty() )
            {
                return false;
            }

            const std::filesystem::path path = app.game_data().resolve_runtime_asset( asset );

            if( path.empty() )
            {
                return false;
            }

            try
            {
                background_texture_ = app.texture_manager().load_png( "loading/background", path );

                art_layout_ = ArtLayout::Other;

                if( background_texture_ )
                {
                    if( asset.find( "/LOAD_" ) != std::string::npos )
                    {
                        art_layout_ = ArtLayout::Psp;
                    }
                    else if( asset.find( "/LOADST" ) != std::string::npos )
                    {
                        art_layout_ = ArtLayout::Ps2Stage;
                    }
                    else if( asset.find( "/V_LOAD" ) != std::string::npos )
                    {
                        art_layout_ = ArtLayout::Ps2Versus;
                    }
                    else if( asset.find( "/LOADING.png" ) != std::string::npos )
                    {
                        art_layout_ = ArtLayout::Ps2Generic;
                    }
                }

                return static_cast<bool>( background_texture_ );
            }
            catch( const std::exception & )
            {
                background_texture_ = {};

                art_layout_ = ArtLayout::Other;
                return false;
            }
        };

        if( try_load( task_.background_asset ) )
        {
            return;
        }

        for( const std::string &candidate : task_.background_candidates )
        {
            if( try_load( candidate ) )
            {
                return;
            }
        }

        try_load( task_.fallback_background_asset );
    }

    void LoadingModule::unload_background( App &app )
    {
        app.texture_manager().unload( background_texture_ );
        background_texture_ = {};

        art_layout_ = ArtLayout::Other;
    }

    void LoadingModule::update( App &app, double delta_seconds )
    {
        elapsed_seconds_ += delta_seconds;

        if( transition_requested_ )
        {
            return;
        }

        const managers::InputManager &input = app.input();

        if( input.pressed( managers::InputAction::Back ) && !worker_done_.load( std::memory_order_acquire ) )
        {
            progress_.request_cancel();
            progress_.set_status( localization::text( "@ui.cancelling" ) );
        }

        if( !worker_done_.load( std::memory_order_acquire ) )
        {
            return;
        }

        join_worker();

        if( worker_failed_.load( std::memory_order_relaxed ) )
        {
            progress_.set_status( localization::text( "@ui.loading.failed_hint" ) );

            if( ( input.pressed( managers::InputAction::Confirm ) || input.pressed( managers::InputAction::Back ) ) &&
                task_.create_cancel_module )
            {
                transition_requested_ = true;
                app.change_module( task_.create_cancel_module() );
            }

            return;
        }

        if( progress_.cancel_requested() )
        {
            if( task_.create_cancel_module )
            {
                transition_requested_ = true;
                app.change_module( task_.create_cancel_module() );
            }

            return;
        }

        if( elapsed_seconds_ < 2.1 )
        {
            return;
        }

        if( task_.create_next_module )
        {
            transition_requested_ = true;
            app.change_module( task_.create_next_module() );
        }
    }

    void LoadingModule::draw_ps2_letters( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        float origin_x,
        float origin_y,
        float scale,
        float letter_x,
        float letter_y,
        float frame )
    {
        constexpr std::array<float, 13>
            offsets{
            0.0f,
            21.0f,
            39.0f,
            66.0f,
            81.0f,
            99.0f,
            118.0f,
            137.0f,
            145.0f,
            163.0f,
            182.0f,
            190.0f,
            197.0f
        };

        constexpr std::array<float, 13>
            delays{
            0.0f,
            5.0f,
            10.0f,
            15.0f,
            20.0f,
            25.0f,
            30.0f,
            35.0f,
            40.0f,
            45.0f,
            50.0f,
            55.0f,
            60.0f
        };

        const auto draw_letter = [ & ]( std::size_t index, float offset_y, float size, float alpha )
        {
            if( size <= 0.0f || alpha <= 0.0f )
            {
                return;
            }

            renderer.draw_texture_region( texture,
                2.0f + 22.0f * static_cast<float>( index ),
                2.0f,
                20.0f,
                16.0f,
                origin_x + ( letter_x + offsets[ index ] + 10.0f - 10.0f * size ) * scale,
                origin_y + ( letter_y + offset_y + 8.0f - 8.0f * size ) * scale,
                20.0f * size * scale,
                16.0f * size * scale,
                rendering::Color{ 1.0f, 1.0f, 1.0f, alpha } );
        };

        for( std::size_t index = 0; index < offsets.size(); ++index )
        {
            const float start = 16.0f + delays[ index ];

            if( frame < start )
            {
                continue;
            }

            const float elapsed = frame - start;
            draw_letter( index, 0.0f, 1.0f, std::max( 0.0f, 1.0f - 0.005f * elapsed ) );
            const float flash_alpha = std::max( 0.0f, 1.0f - 0.05f * elapsed );
            draw_letter( index, 0.0f, std::exp( 1.0f - flash_alpha ), flash_alpha );

            for( std::int32_t particle = 0; particle < 20; ++particle )
            {
                const float particle_elapsed = frame - start - 0.3f * static_cast<float>( particle );

                if( particle_elapsed < 0.0f )
                {
                    continue;
                }

                const float particle_alpha =
                    std::min( 1.0f / static_cast<float>( particle + 1 ), 0.01f * particle_elapsed );

                const float distance = std::max( 0.0f, 150.0f - 4.0f * particle_elapsed );
                draw_letter( index, distance, 1.0f, particle_alpha );
                draw_letter( index, -distance, 1.0f, particle_alpha );
            }
        }
    }

    void LoadingModule::render( App &app, rendering::Renderer &renderer )
    {
        const float width = static_cast<float>( renderer.output_width() );
        const float height = static_cast<float>( renderer.output_height() );
        const float time = static_cast<float>( elapsed_seconds_ );
        const float pulse = 0.5f + 0.5f * std::sin( time * 4.0f );
        renderer.draw_filled_rect( rendering::Rect{ 0.0f, 0.0f, width, height },
            rendering::Color{ 0.0f, 0.0f, 0.0f, 1.0f } );

        bool ps2_art_drawn = false;

        if( const rendering::Texture *background = app.texture_manager().get( background_texture_ ) )
        {
            const bool atlas_available =
                ( art_layout_ == ArtLayout::Ps2Stage && background->width >= 512 && background->height >= 512 ) ||
                ( art_layout_ == ArtLayout::Ps2Versus && background->width >= 322 && background->height >= 243 ) ||
                ( art_layout_ == ArtLayout::Ps2Generic && background->width >= 342 && background->height >= 241 );

            if( art_layout_ == ArtLayout::Psp && background->width >= 481 && background->height >= 289 )
            {
                const float scale = std::min( width / 480.0f, height / 272.0f );
                const float x = ( width - 480.0f * scale ) * 0.5f;
                const float y = ( height - 272.0f * scale ) * 0.5f;
                renderer.draw_texture_region( *background,
                    1.0f,
                    17.0f,
                    480.0f,
                    272.0f,
                    x,
                    y,
                    480.0f * scale,
                    272.0f * scale );

                if( const auto *letters = app.texture_manager().get( loading_label_texture_ ) )
                {
                    draw_ps2_letters( renderer, *letters, x, y, scale, 132.0f, 126.0f, time * 60.0f );
                }
            }
            else if( atlas_available )
            {
                ps2_art_drawn = true;
                constexpr std::array<std::array<float, 3>, 11> placements{
                    {
                        { 130.0f, 39.0f, 35.0f },
                        { 78.0f, 369.0f, 71.0f },
                        { 130.0f, 37.0f, 182.0f },
                        { 130.0f, 48.0f, 34.0f },
                        { 41.0f, 391.0f, 374.0f },
                        { 22.0f, 388.0f, 92.0f },
                        { 0.0f, 398.0f, 50.0f },
                        { 130.0f, 48.0f, 50.0f },
                        { 65.0f, 394.0f, 398.0f },
                        { 0.0f, 389.0f, 276.0f },
                        { 130.0f, 33.0f, 409.0f }
                    }
                };

                const std::array<float, 3> placement =
                    task_.stage_number >= 0 && task_.stage_number < static_cast<std::int32_t>( placements.size() )
                        ? placements[ static_cast<std::size_t>( task_.stage_number ) ]
                        : std::array<float, 3>{ 64.0f, 48.0f, 35.0f };

                const float scale = std::min( width / 640.0f, height / 448.0f );
                const float origin_x = ( width - 640.0f * scale ) * 0.5f;
                const float origin_y = ( height - 448.0f * scale ) * 0.5f;
                float letter_x = placement[ 1 ];
                float letter_y = placement[ 2 ];

                if( art_layout_ == ArtLayout::Ps2Stage )
                {
                    renderer.draw_texture_region( *background,
                        1.0f,
                        63.0f,
                        511.0f,
                        448.0f,
                        origin_x + placement[ 0 ] * scale,
                        origin_y,
                        511.0f * scale,
                        448.0f * scale );
                }
                else if( art_layout_ == ArtLayout::Ps2Versus )
                {
                    renderer.draw_texture_region( *background,
                        2.0f,
                        19.0f,
                        320.0f,
                        224.0f,
                        origin_x,
                        origin_y,
                        640.0f * scale,
                        448.0f * scale );

                    letter_x = 240.0f;

                    letter_y = 224.0f;
                }
                else
                {
                    renderer.draw_texture_region( *background,
                        268.0f,
                        39.0f,
                        74.0f,
                        202.0f,
                        origin_x,
                        origin_y + 241.0f * scale,
                        74.0f * scale,
                        202.0f * scale );

                    renderer.draw_texture_region( *background,
                        174.0f,
                        39.0f,
                        128.0f,
                        139.0f,
                        origin_x + 256.0f * scale,
                        origin_y + 278.0f * scale,
                        128.0f * scale,
                        139.0f * scale );

                    letter_x = 240.0f;

                    letter_y = 224.0f;
                }

                draw_ps2_letters( renderer, *background, origin_x, origin_y, scale, letter_x, letter_y, time * 60.0f );
            }
            else
            {
                const float source_width = std::min( 512.0f, static_cast<float>( background->width ) );
                const float source_height = std::min( 448.0f, static_cast<float>( background->height ) );
                const float scale = std::min( width / source_width, height / source_height );
                const float breathe = 1.0f + 0.025f * std::sin( time * 2.2f );
                const float target_width = source_width * scale * breathe;
                const float target_height = source_height * scale * breathe;
                renderer.draw_texture_region( *background,
                    0.0f,
                    0.0f,
                    source_width,
                    source_height,
                    ( width - target_width ) * 0.5f,
                    ( height - target_height ) * 0.5f + std::sin( time * 1.5f ) * 3.0f * scale,
                    target_width,
                    target_height,
                    rendering::Color{ 1.0f, 1.0f, 1.0f, 0.90f + 0.10f * pulse } );
            }
        }

        if( worker_failed_.load( std::memory_order_relaxed ) )
        {
            const std::string error = error_message();
            renderer.draw_text( width * 0.5f - 160.0f,
                height * 0.78f,
                error.empty() ? localization::text( "@ui.load_failed" ) : error.c_str(),
                rendering::Color{ 1.0f, 0.55f, 0.48f, 1.0f },
                1.0f );

            return;
        }

        if( ps2_art_drawn )
        {
            return;
        }

        const float indicator_y = height * 0.89f;
        const float indicator_scale = std::max( 0.65f, std::min( width / 1280.0f, height / 720.0f ) );

        for( std::int32_t index = 0; index < 4; ++index )
        {
            const float phase = time * 6.0f - static_cast<float>( index ) * 0.72f;
            const float alpha = 0.16f + 0.72f * std::pow( 0.5f + 0.5f * std::sin( phase ), 3.0f );
            const float size = ( 5.0f + 2.0f * alpha ) * indicator_scale;
            renderer.draw_filled_rect(
                rendering::Rect{
                    width * 0.5f + ( static_cast<float>( index ) - 1.5f ) * 20.0f * indicator_scale - size * 0.5f,
                    indicator_y - size * 0.5f,
                    size,
                    size
                },
                rendering::Color{ 1.0f, 0.94f, 0.76f, alpha } );
        }
    }
}
