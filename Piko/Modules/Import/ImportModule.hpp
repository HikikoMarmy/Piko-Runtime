#pragma once

#include "GameData/RuntimeImporter.hpp"
#include "GameData/StageCatalog.hpp"
#include "Modules/MainMenu/TitleBackgroundRenderer.hpp"
#include "Modules/PikoModule.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace piko::rendering
{
    struct Color;
    struct LogicalViewport;
    struct Rect;
    class TrueTypeFont;
}

namespace piko::modules
{
    class ImportModule final : public PikoModule
    {
    public:
        enum class ReturnTarget
        {
            Opening,
            MainMenu,
            Options,
            StageSelect
        };

        explicit ImportModule( ReturnTarget return_target = ReturnTarget::MainMenu, bool runtime_required = false );
        ~ImportModule() override;

        void enter( App &app ) override;
        void leave( App &app ) override;
        void handle_event( App &app, const SDL_Event &event ) override;
        void update( App &app, double delta_seconds ) override;
        void render( App &app, rendering::Renderer &renderer ) override;

    private:
        static constexpr float reference_width = 1280.0f;
        static constexpr float reference_height = 720.0f;
        static constexpr double dot_spacing = 140.0;
        static constexpr double dot_scroll_speed = 22.0;
        static constexpr double dot_vertical_scroll_ratio = 0.72;
        static constexpr double dot_pulse_speed = 0.55;
        static constexpr double dot_pulse_phase = 0.43;
        static constexpr const char *title_texture_name = "import/title_text";
        static constexpr const char *dot_texture_name = "import/polka_dot";
        static constexpr const char *title_font_filename = "Anak Paud.ttf";
        static constexpr const char *body_font_regular_filename = "noto-sans.regular.ttf";
        static constexpr const char *body_font_bold_filename = "noto-sans.bold.ttf";

        static float text_width( const char *text, float scale );
        static float responsive_scale( const rendering::LogicalViewport &layout );
        static float responsive_text_scale( const rendering::LogicalViewport &layout, float text_scale );
        static std::filesystem::path resource_font_path( const App &app, const char *filename );
        static std::filesystem::path title_font_path( const App &app );
        static std::filesystem::path body_font_regular_path( const App &app );
        static std::filesystem::path body_font_bold_path( const App &app );
        static void load_title_texture( App &app, const rendering::TrueTypeFont &font );
        static std::vector<std::uint8_t> make_dot_rgba( std::int32_t size );
        static void ensure_dot_texture( App &app );
        static void draw_polka_dots( App &app,
            rendering::Renderer &renderer,
            const rendering::LogicalViewport &layout,
            double time_seconds );

        static void draw_centered_text( rendering::Renderer &renderer,
            const rendering::LogicalViewport &layout,
            float center_x,
            float y,
            const char *text,
            const rendering::Color &color,
            float text_scale );

        static void draw_button_frame( rendering::Renderer &renderer,
            const rendering::Rect &rect,
            bool enabled,
            bool emphasized,
            bool hovered );

        static std::string display_path( const std::filesystem::path &path, std::size_t max_length );
        static rendering::Rect list_rect( const rendering::LogicalViewport &layout );
        static rendering::Rect add_rect( const rendering::LogicalViewport &layout );
        static rendering::Rect remove_rect( const rendering::LogicalViewport &layout );
        static rendering::Rect flac_rect( const rendering::LogicalViewport &layout );
        static rendering::Rect opus_rect( const rendering::LogicalViewport &layout );
        static rendering::Rect cancel_rect( const rendering::LogicalViewport &layout );
        static rendering::Rect ok_rect( const rendering::LogicalViewport &layout );
        static rendering::Rect continue_rect( const rendering::LogicalViewport &layout );

        struct FileDialogState;
        struct ImportWorkerState;
        struct TextRendererState;

        enum class ScreenState
        {
            Selection,
            Importing,
            Previewing,
            Complete,
            Error
        };

        static void SDLCALL file_dialog_callback( void *userdata, const char *const *file_list, std::int32_t filter );

        void open_file_dialog( App &app );
        void consume_file_dialog();
        void add_source_path( const std::filesystem::path &path );
        void remove_selected_source();
        void start_import( App &app );
        void cancel( App &app );
        void return_to_target( App &app );

        void render_selection( App &app, rendering::Renderer &renderer );
        void render_importing( App &app, rendering::Renderer &renderer );
        void render_previewing( App &app, rendering::Renderer &renderer );
        void render_complete( App &app, rendering::Renderer &renderer );
        void render_error( App &app, rendering::Renderer &renderer );

    private:
        ReturnTarget return_target_ = ReturnTarget::MainMenu;
        bool runtime_required_ = false;
        ScreenState state_ = ScreenState::Selection;

        TitleBackgroundRenderer background_;
        std::vector<std::filesystem::path> source_paths_;
        std::int32_t selected_source_ = -1;
        convert::AudioCompression audio_compression_ = convert::AudioCompression::Flac;

        std::shared_ptr<FileDialogState> file_dialog_state_;
        std::shared_ptr<ImportWorkerState> worker_state_;
        std::unique_ptr<TextRendererState> text_renderer_;
        std::future<gamedata::RuntimeImportResult> import_future_;

        std::string error_message_;
        std::size_t warning_count_ = 0u;
        std::vector<gamedata::PlayableStageEntry> preview_stages_{};
        std::size_t preview_index_ = 0u;
        std::size_t preview_failed_ = 0u;
        double elapsed_seconds_ = 0.0;
    };
}
