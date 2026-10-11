#pragma once

#include <cstdint>
#include <SDL3/SDL.h>

#include <GameData/GameDataRoot.hpp>
#include <GameData/SaveData.hpp>
#include <Managers/AudioManager.hpp>
#include <Managers/FrameStats.hpp>
#include <Managers/GameSettings.hpp>
#include <Managers/InputManager.hpp>
#include <Managers/MenuNavigation.hpp>
#include <Managers/TextureManager.hpp>
#include <Modules/PikoModule.hpp>
#include <Rendering/Renderer.hpp>

#include <memory>
#include <string>

namespace piko
{
    class App
    {
    public:
        App() = default;
        ~App();

        App( const App & ) = delete;
        App &operator=( const App & ) = delete;

        bool initialize( bool force_runtime_import = false );
        void run();
        void shutdown();

        void request_quit();
        void change_module( std::unique_ptr<modules::PikoModule> module );

        SDL_Window *window() const;

        rendering::Renderer &renderer();
        const rendering::Renderer &renderer() const;

        managers::TextureManager &texture_manager();
        const managers::TextureManager &texture_manager() const;

        managers::InputManager &input();
        const managers::InputManager &input() const;

        managers::AudioManager &audio();
        const managers::AudioManager &audio() const;

        const gamedata::GameDataRoot &game_data() const;
        gamedata::SaveDataManager &save_data();
        const gamedata::SaveDataManager &save_data() const;
        bool reload_runtime_data( std::string &message );
        bool set_game_language( gamedata::GameLanguage language, std::string &message );
        bool set_ui_language( const std::string &language, std::string &message );

        const managers::FrameStats &frame_stats() const;

        managers::GameSettings &settings();
        const managers::GameSettings &settings() const;

        managers::MenuNavigation &menu_navigation()
        {
            return menu_navigation_;
        }

    private:
        void handle_events();
        void update( double delta_seconds );
        void render();

        bool initialize_sdl();
        bool initialize_window_and_opengl();
        void shutdown_window_and_opengl();
        void apply_pending_module_change();
        bool initialize_imgui();
        void shutdown_imgui();
        void begin_imgui_frame();
        void render_imgui();
        void log_runtime_summary() const;
        static std::filesystem::path application_root_path();
        static bool imgui_wants_keyboard_event( const SDL_Event &event );
        static bool imgui_wants_mouse_event( const SDL_Event &event );

    private:
        static constexpr std::int32_t default_window_width = 1280;
        static constexpr std::int32_t default_window_height = 720;

        bool running_ = false;
        bool initialized_sdl_ = false;

        SDL_Window *window_ = nullptr;
        SDL_GLContext gl_context_ = nullptr;
        bool imgui_initialized_ = false;

        rendering::Renderer renderer_;
        managers::TextureManager texture_manager_;
        managers::InputManager input_manager_;
        managers::AudioManager audio_manager_;
        managers::FrameStats frame_stats_;
        managers::GameSettings settings_;
        managers::MenuNavigation menu_navigation_;
        gamedata::GameDataRoot game_data_root_;
        gamedata::SaveDataManager save_data_manager_;

        std::unique_ptr<modules::PikoModule> current_module_;
        std::unique_ptr<modules::PikoModule> pending_module_;

        std::string window_title_ = "Piko";
        double elapsed_seconds_ = 0.0;
    };
}
