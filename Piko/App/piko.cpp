#include "Utils/EnumUtil.hpp"
#include "piko.hpp"
#include "Localization/Language.hpp"
#include "Utils/FileUtil.hpp"

#include <GameData/AlbumFlow.hpp>
#include <Modules/Import/ImportModule.hpp>
#include <Modules/MainMenu/MainMenuModule.hpp>
#include <Modules/Movie/MovieModule.hpp>
#include <Modules/PikoModule.hpp>
#include <Runtime/Model/GltfDocument.hpp>

#include <SDL3/SDL.h>

#if defined( _DEBUG ) &&                                                                                               \
    __has_include(                                                                                                     \
        <imgui.h>) && __has_include(<backends/imgui_impl_sdl3.h>) && __has_include(<backends/imgui_impl_opengl3.h>)
#define PIKO_HAS_IMGUI 1
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_opengl3.h>
#else
#define PIKO_HAS_IMGUI 0
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace piko
{
    std::filesystem::path App::application_root_path()
    {
        if( const char *base_path = SDL_GetBasePath(); base_path != nullptr && *base_path != '\0' )
        {
            return std::filesystem::path( base_path );
        }

        return std::filesystem::current_path();
    }

    bool App::imgui_wants_keyboard_event( const SDL_Event &event )
    {
#if PIKO_HAS_IMGUI
        switch( event.type )
        {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
            case SDL_EVENT_TEXT_INPUT:
                return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
            default:
                return false;
        }
#else
        (void)event;
        return false;
#endif
    }

    bool App::imgui_wants_mouse_event( const SDL_Event &event )
    {
#if PIKO_HAS_IMGUI
        switch( event.type )
        {
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_WHEEL:
                return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
            default:
                return false;
        }
#else
        (void)event;
        return false;
#endif
    }

    App::~App()
    {
        shutdown();
    }

    bool App::initialize( bool force_runtime_import )
    {
        if( !initialize_sdl() )
        {
            return false;
        }

        std::string settings_error;

        if( !managers::load_game_settings( settings_, &settings_error ) )
        {
            std::cerr << "Could not load settings: " << settings_error << '\n';
        }

        if( !input_manager_.initialize() )
        {
            shutdown();
            return false;
        }

        input_manager_.set_controller_orientation( settings_.controller_orientation );

        if( !settings_.input_device_name.empty() )
        {
            for( const managers::InputManager::Device &device : input_manager_.available_devices() )
            {
                if( device.name == settings_.input_device_name )
                {
                    input_manager_.select_device( device.id );
                    break;
                }
            }
        }

        if( !audio_manager_.initialize() )
        {
            shutdown();
            return false;
        }

        audio_manager_.set_stereo_output( settings_.stereo_audio );
        audio_manager_.set_background_fx_enabled( settings_.background_fx_enabled );
        audio_manager_.set_instrument_routing( settings_.instrument_routing );

        frame_stats_.reset();

        const std::filesystem::path application_root = application_root_path();

        std::string language_error;

        if( !localization::load( application_root, settings_.ui_language, language_error ) )
        {
            std::cerr << "[Piko UI] " << language_error << '\n';

            if( !localization::load( application_root, "en", language_error ) )
            {
                std::cerr << "[Piko UI] GAME_DATA/EN/language.json is required: " << language_error << '\n';

                return false;
            }

            settings_.ui_language = "en";
        }

        settings_.ui_language = localization::language_id();

        localization::set_interface_style( settings_.interface_styles[ settings_.ui_language ] );

        game_data_root_.initialize( application_root, settings_.language );
        game_data_root_.set_texture_roots( localization::texture_roots() );
        settings_.chart_style = game_data_root_.resolve_chart_style( settings_.chart_style );

        if( !save_data_manager_.initialize( application_root ) )
        {
            std::cerr << save_data_manager_.last_error() << '\n';
        }

        if( game_data_root_.valid() )
        {
            settings_.language = game_data_root_.language();
        }

        if( !initialize_window_and_opengl() )
        {
            shutdown();
            return false;
        }

        const bool runtime_required = !game_data_root_.valid() || !game_data_root_.native_runtime_available();

        if( !runtime_required )
        {
            log_runtime_summary();
        }

        if( force_runtime_import || runtime_required )
        {
            const modules::ImportModule::ReturnTarget return_target =
                runtime_required ? modules::ImportModule::ReturnTarget::Opening
                                 : modules::ImportModule::ReturnTarget::MainMenu;

            current_module_ = std::make_unique<modules::ImportModule>( return_target, runtime_required );
        }
        else
        {
            try
            {
                const std::optional<std::int32_t> opening_page =
                    settings_.play_movies ? gamedata::album_opening_movie_page( game_data_root_ ) : std::nullopt;

                if( opening_page.has_value() )
                {
                    const gamedata::AlbumFlowTarget target =
                        gamedata::resolve_album_flow_target( game_data_root_, *opening_page, settings_.chart_style );

                    if( target.type == gamedata::AlbumFlowTargetType::Movie )
                    {
                        current_module_ = std::make_unique<modules::MovieModule>( target.page_id, target.movie_path );
                    }
                }
            }
            catch( const std::exception & )
            {
            }

            if( !current_module_ )
            {
                current_module_ = std::make_unique<modules::MainMenuModule>();
            }
        }

        current_module_->enter( *this );

        running_ = true;
        return true;
    }

    bool App::initialize_sdl()
    {
        if( initialized_sdl_ )
        {
            return true;
        }

        if( !SDL_Init( SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO ) )
        {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
            return false;
        }

        initialized_sdl_ = true;
        return true;
    }

    bool App::initialize_window_and_opengl()
    {
        SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 3 );
        SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 3 );
        SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );

        SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

        if( settings_.msaa_samples > 0 )
        {
            SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 1 );
            SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, settings_.msaa_samples );
        }

        SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
        SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 8 );

        window_ = SDL_CreateWindow( window_title_.c_str(),
            settings_.window_width,
            settings_.window_height,
            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE );

        if( window_ == nullptr )
        {
            std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
            return false;
        }

        gl_context_ = SDL_GL_CreateContext( window_ );

        if( gl_context_ == nullptr )
        {
            std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << '\n';
            return false;
        }

        if( !SDL_GL_MakeCurrent( window_, gl_context_ ) )
        {
            std::cerr << "SDL_GL_MakeCurrent failed: " << SDL_GetError() << '\n';
            return false;
        }

        if( !SDL_GL_SetSwapInterval( 1 ) )
        {
            std::cerr << "SDL_GL_SetSwapInterval failed: " << SDL_GetError() << '\n';
        }

        if( !renderer_.initialize( window_, game_data_root_.resolve_resource_path( "shaders" ) ) )
        {
            std::cerr << "Renderer initialization failed\n";
            return false;
        }

        if( !texture_manager_.initialize( renderer_ ) )
        {
            std::cerr << "TextureManager initialization failed\n";
            return false;
        }

        renderer_.set_msaa_enabled( settings_.msaa_enabled );
        rendering::TextureSamplingState sampling{};
        sampling.mode =
            utils::to_enum<rendering::TextureFilterMode>( utils::to_int( settings_.texture_filtering_mode ) );

        sampling.anisotropy = settings_.texture_anisotropy;
        renderer_.set_texture_sampling( sampling );

#if PIKO_HAS_IMGUI
        if( !initialize_imgui() )
        {
            std::cerr << "ImGui initialization failed.\n";
        }
#endif

        return true;
    }

    void App::run()
    {
        const std::uint64_t performance_frequency = SDL_GetPerformanceFrequency();
        std::uint64_t previous_counter = SDL_GetPerformanceCounter();

        const auto elapsed_ms = [ performance_frequency ]( std::uint64_t begin, std::uint64_t end ) -> double
        {
            return performance_frequency != 0
                       ? static_cast<double>( end - begin ) * 1000.0 / static_cast<double>( performance_frequency )
                       : 0.0;
        };

        while( running_ )
        {
            const std::uint64_t now_counter = SDL_GetPerformanceCounter();
            double delta_seconds = performance_frequency != 0 ? static_cast<double>( now_counter - previous_counter ) /
                                                                    static_cast<double>( performance_frequency )
                                                              : 0.0;

            previous_counter = now_counter;

            delta_seconds = std::clamp( delta_seconds, 0.0, 0.25 );
            frame_stats_.record_frame( delta_seconds );

            const std::uint64_t events_begin = SDL_GetPerformanceCounter();
            input_manager_.begin_frame();
            handle_events();
            input_manager_.update( delta_seconds );
            apply_pending_module_change();
            const std::uint64_t events_end = SDL_GetPerformanceCounter();

            update( delta_seconds );
            const std::uint64_t update_end = SDL_GetPerformanceCounter();

            audio_manager_.update();
            apply_pending_module_change();
            const std::uint64_t audio_end = SDL_GetPerformanceCounter();

            renderer_.reset_frame_stats();
            const std::uint64_t render_begin = SDL_GetPerformanceCounter();

            const float t = static_cast<float>( elapsed_seconds_ );
            const float r = 0.04f + 0.03f * ( 0.5f + 0.5f * std::sin( t * 0.7f ) );
            const float g = 0.05f + 0.03f * ( 0.5f + 0.5f * std::sin( t * 0.9f + 1.4f ) );
            const float b = 0.09f + 0.04f * ( 0.5f + 0.5f * std::sin( t * 0.5f + 2.1f ) );

            renderer_.begin_frame();
            renderer_.clear( r, g, b, 1.0f );
            begin_imgui_frame();

            if( current_module_ )
            {
                current_module_->render( *this, renderer_ );
            }

            render_imgui();
            const std::uint64_t render_end = SDL_GetPerformanceCounter();
            renderer_.end_frame();
            const std::uint64_t present_end = SDL_GetPerformanceCounter();

            const rendering::RendererFrameStats &render_stats = renderer_.frame_stats();
            managers::FrameProfile profile{};
            profile.events_ms = elapsed_ms( events_begin, events_end );

            profile.update_ms = elapsed_ms( events_end, update_end );

            profile.audio_ms = elapsed_ms( update_end, audio_end );

            profile.render_ms = elapsed_ms( render_begin, render_end );

            profile.present_ms = elapsed_ms( render_end, present_end );

            profile.colored_draw_calls = render_stats.colored_draw_calls;

            profile.textured_draw_calls = render_stats.textured_draw_calls;

            profile.text_draw_calls = render_stats.text_draw_calls;

            profile.glyph_quads = render_stats.glyph_quads;

            profile.vertices = render_stats.vertices;
            frame_stats_.record_profile( profile );

            apply_pending_module_change();
        }
    }

    void App::shutdown()
    {
        if( current_module_ )
        {
            current_module_->leave( *this );
            current_module_.reset();
        }

        pending_module_.reset();

        audio_manager_.shutdown();
        texture_manager_.shutdown();
        shutdown_window_and_opengl();

        input_manager_.shutdown();

        game_data_root_.clear();

        if( initialized_sdl_ )
        {
            std::string settings_error;

            if( !managers::save_game_settings( settings_, &settings_error ) )
            {
                std::cerr << "Could not save settings: " << settings_error << '\n';
            }

            SDL_Quit();
            initialized_sdl_ = false;
        }

        running_ = false;
    }

    void App::request_quit()
    {
        running_ = false;
    }

    void App::change_module( std::unique_ptr<modules::PikoModule> module )
    {
        pending_module_ = std::move( module );
    }

    SDL_Window *App::window() const
    {
        return window_;
    }

    rendering::Renderer &App::renderer()
    {
        return renderer_;
    }

    const rendering::Renderer &App::renderer() const
    {
        return renderer_;
    }

    managers::TextureManager &App::texture_manager()
    {
        return texture_manager_;
    }

    const managers::TextureManager &App::texture_manager() const
    {
        return texture_manager_;
    }

    managers::InputManager &App::input()
    {
        return input_manager_;
    }

    const managers::InputManager &App::input() const
    {
        return input_manager_;
    }

    managers::AudioManager &App::audio()
    {
        return audio_manager_;
    }

    const managers::AudioManager &App::audio() const
    {
        return audio_manager_;
    }

    const gamedata::GameDataRoot &App::game_data() const
    {
        return game_data_root_;
    }

    gamedata::SaveDataManager &App::save_data()
    {
        return save_data_manager_;
    }

    const gamedata::SaveDataManager &App::save_data() const
    {
        return save_data_manager_;
    }

    bool App::reload_runtime_data( std::string &message )
    {
        const std::filesystem::path application_root = application_root_path();

        if( !game_data_root_.initialize( application_root, settings_.language ) )
        {
            message = "The import completed, but Piko could not reload GAME_DATA.";
            return false;
        }

        if( !game_data_root_.native_runtime_available() )
        {
            message = "The import completed, but Piko could not find the canonical runtime.";
            return false;
        }

        game_data_root_.set_texture_roots( localization::texture_roots() );
        settings_.chart_style = game_data_root_.resolve_chart_style( settings_.chart_style );

        settings_.language = game_data_root_.language();
        log_runtime_summary();
        message = "Runtime import complete. Canonical runtime updated.";
        return true;
    }

    bool App::set_ui_language( const std::string &language, std::string &message )
    {
        if( !localization::load( application_root_path(), language, message ) )
        {
            return false;
        }

        settings_.ui_language = localization::language_id();
        game_data_root_.set_texture_roots( localization::texture_roots() );
        localization::set_interface_style( settings_.interface_styles[ settings_.ui_language ] );
        managers::save_game_settings( settings_ );
        return true;
    }

    bool App::set_game_language( gamedata::GameLanguage language, std::string &message )
    {
        const std::filesystem::path application_root = application_root_path();

        if( !game_data_root_.initialize( application_root, language ) )
        {
            message = "Piko could not reload GAME_DATA for the selected language.";
            return false;
        }

        game_data_root_.set_texture_roots( localization::texture_roots() );
        settings_.chart_style = game_data_root_.resolve_chart_style( settings_.chart_style );

        settings_.language = game_data_root_.language();
        log_runtime_summary();
        message = "Language: " + game_data_root_.language_name();
        return true;
    }

    void App::log_runtime_summary() const
    {
        const std::filesystem::path native_runtime_root = game_data_root_.native_runtime_root();
        const bool psp_overlay = std::filesystem::is_regular_file( native_runtime_root / "SOURCES" / "PSP.json" );

        std::cout << "GAME_DATA: " << game_data_root_.game_data_root().string() << '\n'
                  << "Region: " << game_data_root_.region_name() << ", Language: " << game_data_root_.language_name()
                  << '\n'
                  << "[Piko Runtime] Root: " << native_runtime_root.string() << '\n'
                  << "[Piko Runtime] Visual base: PlayStation 2\n"
                  << "[Piko Runtime] PSP chart/audio: " << ( psp_overlay ? "installed" : "not installed" ) << '\n'
                  << "[Piko Runtime] Chart style: "
                  << ( settings_.chart_style == gamedata::ChartStyle::GitarooManLives ? "Gitaroo Man Lives! (PSP)"
                                                                                      : "Gitaroo Man (PS2)" )
                  << '\n'
                  << "[Piko Runtime] Charts: Normalized JSON\n"
                  << "[Piko Runtime] Audio: Normalized runtime audio (WAV/FLAC/Opus)\n"
                  << "[Piko Runtime] Textures: Canonical PNG\n"
                  << "[Piko Runtime] Models: Canonical glTF\n"
                  << "[Piko Runtime] glTF: "
                  << ( runtime::model::tinygltf_available() ? "TinyGLTF ready" : "TinyGLTF not installed" ) << '\n'
                  << "[Piko Runtime] Stage sequences: Normalized JSON\n";
    }

    const managers::FrameStats &App::frame_stats() const
    {
        return frame_stats_;
    }

    managers::GameSettings &App::settings()
    {
        return settings_;
    }

    const managers::GameSettings &App::settings() const
    {
        return settings_;
    }

    void App::handle_events()
    {
        SDL_Event event{};

        while( SDL_PollEvent( &event ) )
        {
#if PIKO_HAS_IMGUI
            if( imgui_initialized_ )
            {
                ImGui_ImplSDL3_ProcessEvent( &event );
            }
#endif

            if( event.type == SDL_EVENT_QUIT )
            {
                request_quit();
            }

            const bool captured_by_imgui = imgui_wants_keyboard_event( event ) || imgui_wants_mouse_event( event );

            if( !captured_by_imgui )
            {
                input_manager_.handle_event( event );

                if( current_module_ )
                {
                    current_module_->handle_event( *this, event );
                }
            }
        }
    }

    void App::update( double delta_seconds )
    {
        elapsed_seconds_ += delta_seconds;

        if( current_module_ )
        {
            current_module_->update( *this, delta_seconds );
        }
    }

    void App::render()
    {
        const float t = static_cast<float>( elapsed_seconds_ );
        const float r = 0.04f + 0.03f * ( 0.5f + 0.5f * std::sin( t * 0.7f ) );
        const float g = 0.05f + 0.03f * ( 0.5f + 0.5f * std::sin( t * 0.9f + 1.4f ) );
        const float b = 0.09f + 0.04f * ( 0.5f + 0.5f * std::sin( t * 0.5f + 2.1f ) );

        renderer_.begin_frame();
        renderer_.clear( r, g, b, 1.0f );
        begin_imgui_frame();

        if( current_module_ )
        {
            current_module_->render( *this, renderer_ );
        }

        render_imgui();
        renderer_.end_frame();
    }

    bool App::initialize_imgui()
    {
#if PIKO_HAS_IMGUI
        if( imgui_initialized_ )
        {
            return true;
        }

        if( window_ == nullptr || gl_context_ == nullptr )
        {
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;

        io.LogFilename = nullptr;

        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        ImGui::StyleColorsDark();

        if( !ImGui_ImplSDL3_InitForOpenGL( window_, gl_context_ ) )
        {
            return false;
        }

        if( !ImGui_ImplOpenGL3_Init( "#version 330" ) )
        {
            ImGui_ImplSDL3_Shutdown();
            return false;
        }

        imgui_initialized_ = true;
        return true;
#else
        return false;
#endif
    }

    void App::shutdown_imgui()
    {
#if PIKO_HAS_IMGUI
        if( !imgui_initialized_ )
        {
            return;
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        imgui_initialized_ = false;
#endif
    }

    void App::begin_imgui_frame()
    {
#if PIKO_HAS_IMGUI
        if( !imgui_initialized_ )
        {
            return;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
#endif
    }

    void App::render_imgui()
    {
#if PIKO_HAS_IMGUI
        if( !imgui_initialized_ )
        {
            return;
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData( ImGui::GetDrawData() );
#endif
    }

    void App::shutdown_window_and_opengl()
    {
        shutdown_imgui();
        texture_manager_.shutdown();
        renderer_.shutdown();

        if( gl_context_ != nullptr )
        {
            SDL_GL_DestroyContext( gl_context_ );
            gl_context_ = nullptr;
        }

        if( window_ != nullptr )
        {
            SDL_DestroyWindow( window_ );
            window_ = nullptr;
        }
    }

    void App::apply_pending_module_change()
    {
        if( !pending_module_ )
        {
            return;
        }

        if( current_module_ )
        {
            current_module_->leave( *this );
        }

        current_module_ = std::move( pending_module_ );

        if( current_module_ )
        {
            current_module_->enter( *this );
        }
    }
}
