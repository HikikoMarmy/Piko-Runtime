#include "Localization/Language.hpp"
#include "ImportModule.hpp"

#include "App/piko.hpp"
#include "Managers/InputManager.hpp"
#include "GameData/AlbumFlow.hpp"
#include "Modules/MainMenu/MainMenuModule.hpp"
#include "Modules/Options/OptionsModule.hpp"
#include "Modules/Movie/MovieModule.hpp"
#include "Modules/StageSelect/StageSelectModule.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/RenderUtil.hpp"
#include "Rendering/TrueTypeText.hpp"
#include "Utils/FileUtil.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace piko::modules
{
    float ImportModule::text_width( const char *text, float scale )
    {
        if( text == nullptr )
        {
            return 0.0f;
        }

        return static_cast<float>( std::strlen( text ) ) * 6.0f * scale;
    }

    float ImportModule::responsive_scale( const rendering::LogicalViewport &layout )
    {
        const float scale = std::max( layout.scale, 0.16f );

        if( scale >= 1.0f )
        {
            return scale;
        }

        return std::sqrt( scale );
    }

    float ImportModule::responsive_text_scale( const rendering::LogicalViewport &layout, float text_scale )
    {
        return text_scale * responsive_scale( layout );
    }

    std::filesystem::path ImportModule::resource_font_path( const App &app, const char *filename )
    {
        return piko::utils::find_resource_file( app.game_data().application_root(),
            std::filesystem::path( "fonts" ) / filename );
    }

    std::filesystem::path ImportModule::title_font_path( const App &app )
    {
        return resource_font_path( app, title_font_filename );
    }

    std::filesystem::path ImportModule::body_font_regular_path( const App &app )
    {
        return resource_font_path( app, body_font_regular_filename );
    }

    std::filesystem::path ImportModule::body_font_bold_path( const App &app )
    {
        return resource_font_path( app, body_font_bold_filename );
    }

    void ImportModule::load_title_texture( App &app, const rendering::TrueTypeFont &font )
    {
        app.texture_manager().unload( localization::texture_key( title_texture_name ) );

        if( !font.loaded() )
        {
            return;
        }

        const rendering::StrokedTextStyle style{
            rendering::color_from_rgb( 0xEFBB4Bu ),
            rendering::color_from_rgb( 0x0D005Au ),
            rendering::color_from_rgb( 0x4287DDu ),
            13,
            24,
            1.5f
        };

        std::string error;

        try
        {
            const rendering::RasterizedText text =
                font.rasterize_stroked( localization::text( "@ui.import_your_game_data_upper" ),
                    170.0f,
                    style,
                    &error );

            if( !text.valid() )
            {
                return;
            }

            app.texture_manager().create_rgba( localization::texture_key( title_texture_name ),
                text.rgba.data(),
                text.width,
                text.height );
        }
        catch( const std::exception &exception )
        {
            std::cerr << "[Piko Import] Could not create import title text: " << exception.what() << '\n';
        }
    }

    std::vector<std::uint8_t> ImportModule::make_dot_rgba( std::int32_t size )
    {
        const std::int32_t safe_size = std::max( size, 1 );
        std::vector<std::uint8_t> pixels( static_cast<std::size_t>( safe_size ) *
                                              static_cast<std::size_t>( safe_size ) * 4u,
            0u );

        const float center = static_cast<float>( safe_size - 1 ) * 0.5f;
        const float radius = static_cast<float>( safe_size ) * 0.36f;
        const float edge_width = 1.75f;

        for( std::int32_t y = 0; y < safe_size; ++y )
        {
            for( std::int32_t x = 0; x < safe_size; ++x )
            {
                const float dx = static_cast<float>( x ) - center;
                const float dy = static_cast<float>( y ) - center;
                const float distance = std::sqrt( dx * dx + dy * dy );
                const float alpha =
                    std::clamp( ( radius + edge_width - distance ) / ( edge_width * 2.0f ), 0.0f, 1.0f );

                const std::size_t offset = ( static_cast<std::size_t>( y ) * static_cast<std::size_t>( safe_size ) +
                                               static_cast<std::size_t>( x ) ) *
                                           4u;

                pixels[ offset + 0u ] = 255u;

                pixels[ offset + 1u ] = 255u;

                pixels[ offset + 2u ] = 255u;

                pixels[ offset + 3u ] =
                    static_cast<std::uint8_t>( std::clamp( std::lround( alpha * 255.0f ), 0l, 255l ) );
            }
        }

        return pixels;
    }

    void ImportModule::ensure_dot_texture( App &app )
    {
        if( app.texture_manager().get( dot_texture_name ) != nullptr )
        {
            return;
        }

        try
        {
            const std::vector<std::uint8_t> pixels = make_dot_rgba( 64 );
            app.texture_manager().create_rgba( dot_texture_name, pixels.data(), 64, 64 );
        }
        catch( const std::exception &exception )
        {
            std::cerr << "[Piko Import] Could not create polka dot texture: " << exception.what();
        }
    }

    void ImportModule::draw_polka_dots( App &app,
        rendering::Renderer &renderer,
        const rendering::LogicalViewport &layout,
        double time_seconds )
    {
        ensure_dot_texture( app );
        const rendering::Texture *dot = app.texture_manager().get( dot_texture_name );

        if( dot == nullptr )
        {
            return;
        }

        const double scroll_x = time_seconds * dot_scroll_speed;
        const double scroll_y = scroll_x * dot_vertical_scroll_ratio;
        const double column_shift = std::floor( scroll_x / dot_spacing );
        const double row_shift = std::floor( scroll_y / dot_spacing );
        const double offset_x = scroll_x - column_shift * dot_spacing;
        const double offset_y = scroll_y - row_shift * dot_spacing;

        const double pulse_time = time_seconds * dot_pulse_speed;
        const double grid_phase = ( row_shift + column_shift ) * dot_pulse_phase;

        for( std::int32_t row = -2; row <= 8; ++row )
        {
            for( std::int32_t column = -2; column <= 11; ++column )
            {
                const float x = static_cast<float>( column * dot_spacing - offset_x );
                const float y = static_cast<float>( row * dot_spacing - offset_y );
                const double pulse_phase = pulse_time + grid_phase + ( row + column ) * dot_pulse_phase;
                const float wave = 0.5f + 0.5f * static_cast<float>( std::sin( pulse_phase ) );

                const float size = 88.0f + wave * 24.0f;
                renderer.draw_texture( *dot,
                    layout.to_x( x ),
                    layout.to_y( y ),
                    size * layout.scale,
                    size * layout.scale,
                    rendering::Color{ 1.0f, 1.0f, 1.0f, 0.16f + wave * 0.07f } );
            }
        }
    }

    void ImportModule::draw_centered_text( rendering::Renderer &renderer,
        const rendering::LogicalViewport &layout,
        float center_x,
        float y,
        const char *text,
        const rendering::Color &color,
        float text_scale )
    {
        const float scaled_text = responsive_text_scale( layout, text_scale );
        renderer.draw_text( layout.to_x( center_x ) - text_width( text, scaled_text ) * 0.5f,
            layout.to_y( y ),
            text,
            color,
            scaled_text );
    }

    void ImportModule::draw_button_frame( rendering::Renderer &renderer,
        const rendering::Rect &rect,
        bool enabled,
        bool emphasized,
        bool hovered )
    {
        rendering::Color fill{};

        if( !enabled )
        {
            fill = rendering::Color{ 0.88f, 0.86f, 0.88f, 0.45f };
        }
        else if( emphasized && hovered )
        {
            fill = rendering::Color{ 0.90f, 0.54f, 0.68f, 0.98f };
        }
        else if( emphasized )
        {
            fill = rendering::Color{ 0.77f, 0.42f, 0.57f, 0.94f };
        }
        else if( hovered )
        {
            fill = rendering::Color{ 1.0f, 0.90f, 0.94f, 0.94f };
        }
        else
        {
            fill = rendering::Color{ 1.0f, 1.0f, 1.0f, 0.78f };
        }

        const rendering::Color border = !enabled  ? rendering::Color{ 0.45f, 0.43f, 0.45f, 0.30f }
                                        : hovered ? rendering::Color{ 0.48f, 0.20f, 0.38f, 0.98f }
                                                  : rendering::Color{ 0.33f, 0.20f, 0.31f, 0.72f };
        const float border_width = enabled && hovered ? 3.0f : 1.5f;

        renderer.draw_filled_rect( rect, fill );
        renderer.draw_line( rect.x, rect.y, rect.x + rect.w, rect.y, border_width, border );
        renderer.draw_line( rect.x, rect.y + rect.h, rect.x + rect.w, rect.y + rect.h, border_width, border );
        renderer.draw_line( rect.x, rect.y, rect.x, rect.y + rect.h, border_width, border );
        renderer.draw_line( rect.x + rect.w, rect.y, rect.x + rect.w, rect.y + rect.h, border_width, border );
    }

    std::string ImportModule::display_path( const std::filesystem::path &path, std::size_t max_length )
    {
        std::string text = path.string();

        if( text.size() <= max_length )
        {
            return text;
        }

        if( max_length < 8u )
        {
            return text.substr( text.size() - max_length );
        }

        return "..." + text.substr( text.size() - ( max_length - 3u ) );
    }

    rendering::Rect ImportModule::list_rect( const rendering::LogicalViewport &layout )
    {
        return layout.rect( 220.0f, 246.0f, 840.0f, 224.0f );
    }

    rendering::Rect ImportModule::add_rect( const rendering::LogicalViewport &layout )
    {
        return layout.rect( 220.0f, 516.0f, 156.0f, 40.0f );
    }

    rendering::Rect ImportModule::remove_rect( const rendering::LogicalViewport &layout )
    {
        return layout.rect( 390.0f, 516.0f, 156.0f, 40.0f );
    }

    rendering::Rect ImportModule::flac_rect( const rendering::LogicalViewport &layout )
    {
        return layout.rect( 660.0f, 516.0f, 160.0f, 40.0f );
    }

    rendering::Rect ImportModule::opus_rect( const rendering::LogicalViewport &layout )
    {
        return layout.rect( 832.0f, 516.0f, 160.0f, 40.0f );
    }

    rendering::Rect ImportModule::cancel_rect( const rendering::LogicalViewport &layout )
    {
        return layout.rect( 832.0f, 564.0f, 160.0f, 44.0f );
    }

    rendering::Rect ImportModule::ok_rect( const rendering::LogicalViewport &layout )
    {
        return layout.rect( 660.0f, 564.0f, 160.0f, 44.0f );
    }

    rendering::Rect ImportModule::continue_rect( const rendering::LogicalViewport &layout )
    {
        return layout.rect( 541.0f, 520.0f, 198.0f, 50.0f );
    }

    struct ImportModule::FileDialogState
    {
        std::mutex mutex;
        bool complete = false;
        bool failed = false;
        std::vector<std::string> paths;
        std::string error;
    };

    struct ImportModule::ImportWorkerState
    {
        std::mutex mutex;
        gamedata::RuntimeImportProgress progress;
    };

    struct ImportModule::TextRendererState
    {
        struct Slot
        {
            std::string texture_name;
            std::string text;
            std::int32_t width = 0;
            std::int32_t height = 0;
        };

        rendering::TrueTypeFont regular_font;
        rendering::TrueTypeFont bold_font;
        std::unordered_map<std::string, Slot> regular_slots;
        std::unordered_map<std::string, Slot> bold_slots;
        std::uint64_t next_texture_id = 0u;
        bool ready = false;

        bool initialize( App &app )
        {
            const std::filesystem::path regular_path = body_font_regular_path( app );

            if( regular_path.empty() )
            {
                return false;
            }

            std::string error;

            if( !localization::load_menu_font( regular_font, regular_path, &error ) )
            {
                std::cerr << "[Piko Import] " << error << '\n';
                return false;
            }

            const std::filesystem::path bold_path = body_font_bold_path( app );

            if( !bold_path.empty() )
            {
                std::string bold_error;

                if( !localization::load_menu_font( bold_font, bold_path, &bold_error ) )
                {
                    std::cerr << "[Piko Import] " << bold_error << '\n';
                }
            }

            ready = true;
            std::cout << "[Piko Import] UI font: " << regular_path.string() << '\n';
            return true;
        }

        void shutdown( App &app )
        {
            auto unload_slots = [ & ]( std::unordered_map<std::string, Slot> &slots )
            {
                for( const auto &[ key, slot ] : slots )
                {
                    (void)key;

                    if( !slot.texture_name.empty() )
                    {
                        app.texture_manager().unload( slot.texture_name );
                    }
                }

                slots.clear();
            };

            unload_slots( regular_slots );
            unload_slots( bold_slots );
            regular_font.clear();
            bold_font.clear();
            ready = false;
        }

        const rendering::Texture *ensure_texture( App &app, std::string_view slot_id, std::string_view text, bool bold )
        {
            if( !ready || text.empty() )
            {
                return nullptr;
            }

            rendering::TrueTypeFont &font = bold && bold_font.loaded() ? bold_font : regular_font;
            std::unordered_map<std::string, Slot> &slots = bold && bold_font.loaded() ? bold_slots : regular_slots;
            Slot &slot = slots[ std::string( slot_id ) ];

            if( slot.text == text && !slot.texture_name.empty() )
            {
                return app.texture_manager().get( slot.texture_name );
            }

            if( slot.texture_name.empty() )
            {
                slot.texture_name =
                    localization::texture_key( "import/plain_text_" + std::to_string( next_texture_id++ ) );
            }
            else
            {
                app.texture_manager().unload( slot.texture_name );
            }

            const rendering::StrokedTextStyle style{
                rendering::Color{ 1.0f, 1.0f, 1.0f, 1.0f },
                rendering::Color{ 0.0f, 0.0f, 0.0f, 0.0f },
                rendering::Color{ 0.0f, 0.0f, 0.0f, 0.0f },
                0,
                0,
                0.0f
            };

            std::string error;
            const rendering::RasterizedText rasterized = font.rasterize_stroked( text, 72.0f, style, &error );

            if( !rasterized.valid() )
            {
                std::cerr << "[Piko Import] Could not rasterize UI text: "
                          << ( error.empty() ? std::string( text ) : error ) << '\n';

                slot.text.clear();
                slot.width = 0;

                slot.height = 0;
                return nullptr;
            }

            try
            {
                app.texture_manager().create_rgba( slot.texture_name,
                    rasterized.rgba.data(),
                    rasterized.width,
                    rasterized.height );
            }
            catch( const std::exception &exception )
            {
                std::cerr << "[Piko Import] Could not create UI text texture: " << exception.what() << '\n';
                slot.text.clear();
                slot.width = 0;

                slot.height = 0;
                return nullptr;
            }

            slot.text.assign( text );
            slot.width = rasterized.width;

            slot.height = rasterized.height;
            return app.texture_manager().get( slot.texture_name );
        }

        void draw( App &app,
            rendering::Renderer &renderer,
            const rendering::LogicalViewport &layout,
            std::string_view slot_id,
            std::string_view text,
            const rendering::Color &color,
            float x,
            float y,
            float logical_height,
            bool centered,
            bool bold = false )
        {
            const rendering::Texture *texture = ensure_texture( app, slot_id, text, bold );

            if( texture == nullptr || texture->width <= 0 || texture->height <= 0 )
            {
                return;
            }

            const float aspect = static_cast<float>( texture->width ) / static_cast<float>( texture->height );
            const float available_width = centered ? 2.0f * std::min( x - 190.0f, 1090.0f - x ) : 1090.0f - x;
            const float height =
                std::min( logical_height * responsive_scale( layout ), available_width * layout.scale / aspect );

            const float width = height * aspect;
            float draw_x = layout.to_x( x );

            if( centered )
            {
                draw_x -= width * 0.5f;
            }

            renderer.draw_texture( *texture, draw_x, layout.to_y( y ), width, height, color );
        }

        void draw_in_rect( App &app,
            rendering::Renderer &renderer,
            const rendering::LogicalViewport &layout,
            std::string_view slot_id,
            std::string_view text,
            const rendering::Color &color,
            const rendering::Rect &rect,
            float logical_height,
            bool bold = false )
        {
            const rendering::Texture *texture = ensure_texture( app, slot_id, text, bold );

            if( texture == nullptr || texture->width <= 0 || texture->height <= 0 )
            {
                return;
            }

            const float aspect = static_cast<float>( texture->width ) / static_cast<float>( texture->height );
            const float height = std::min(
                { logical_height * responsive_scale( layout ),
                    rect.h - 4.0f * layout.scale,
                    ( rect.w - 12.0f * layout.scale ) / aspect } );

            const float width = height * aspect;
            renderer.draw_texture( *texture,
                rect.x + ( rect.w - width ) * 0.5f,
                rect.y + ( rect.h - height ) * 0.5f,
                width,
                height,
                color );
        }
    };

    ImportModule::ImportModule( ReturnTarget return_target, bool runtime_required )
        : return_target_( return_target ), runtime_required_( runtime_required )
    {
    }

    ImportModule::~ImportModule() = default;

    void ImportModule::enter( App &app )
    {
        elapsed_seconds_ = 0.0;

        state_ = ScreenState::Selection;
        error_message_.clear();
        warning_count_ = 0u;
        preview_stages_.clear();
        preview_index_ = 0u;

        preview_failed_ = 0u;

        audio_compression_ = app.settings().audio_import_compression == 1 ? convert::AudioCompression::Opus
                                                                          : convert::AudioCompression::Flac;

        background_.initialize( app );
        background_.set_palette( rendering::Color{ 0.98f, 0.73f, 0.82f, 1.0f },
            rendering::Color{ 1.00f, 0.91f, 0.64f, 1.0f },
            rendering::Color{ 1.00f, 0.98f, 0.86f, 1.0f } );

        text_renderer_ = std::make_unique<TextRendererState>();

        if( text_renderer_->initialize( app ) )
        {
            rendering::TrueTypeFont title_font;
            std::string title_error;
            const std::filesystem::path title_path = localization::font_path( title_font_path( app ) );

            if( !title_path.empty() && title_font.load( title_path, &title_error ) )
            {
                load_title_texture( app, title_font );
            }
            else if( !title_error.empty() )
            {
                std::cerr << "[Piko Import] " << title_error << '\n';
            }
        }
    }

    void ImportModule::leave( App &app )
    {
        app.settings().audio_import_compression = audio_compression_ == convert::AudioCompression::Opus ? 1 : 0;
        managers::save_game_settings( app.settings() );
        app.texture_manager().unload( localization::texture_key( title_texture_name ) );
        app.texture_manager().unload( dot_texture_name );

        if( text_renderer_ )
        {
            text_renderer_->shutdown( app );
            text_renderer_.reset();
        }

        background_.shutdown();
    }

    void ImportModule::file_dialog_callback( void *userdata, const char *const *file_list, std::int32_t )
    {
        std::unique_ptr<std::shared_ptr<FileDialogState>> holder(
            static_cast<std::shared_ptr<FileDialogState> *>( userdata ) );

        const std::shared_ptr<FileDialogState> state = *holder;
        std::scoped_lock lock( state->mutex );

        if( file_list == nullptr )
        {
            state->failed = true;

            state->error = SDL_GetError();
        }
        else
        {
            for( std::size_t index = 0u; file_list[ index ] != nullptr; ++index )
            {
                state->paths.emplace_back( file_list[ index ] );
            }
        }

        state->complete = true;
    }

    void ImportModule::open_file_dialog( App &app )
    {
        if( file_dialog_state_ || state_ != ScreenState::Selection )
        {
            return;
        }

        file_dialog_state_ = std::make_shared<FileDialogState>();
        auto *holder = new std::shared_ptr<FileDialogState>( file_dialog_state_ );
        const SDL_DialogFileFilter filters[]{
            { localization::text( "@ui.import.disc_filter" ), "iso" },
            { localization::text( "@ui.all_files" ), "*" }
        };

        SDL_ShowOpenFileDialog( file_dialog_callback, holder, app.window(), filters, 2, nullptr, true );
    }

    void ImportModule::consume_file_dialog()
    {
        if( !file_dialog_state_ )
        {
            return;
        }

        bool complete = false;
        bool failed = false;
        std::vector<std::string> paths;
        std::string error;

        {
            std::scoped_lock lock( file_dialog_state_->mutex );
            complete = file_dialog_state_->complete;

            if( complete )
            {
                failed = file_dialog_state_->failed;

                paths = file_dialog_state_->paths;

                error = file_dialog_state_->error;
            }
        }

        if( !complete )
        {
            return;
        }

        file_dialog_state_.reset();

        if( failed )
        {
            error_message_ = error.empty() ? localization::text( "@ui.the_iso_file_dialog_failed" ) : error;

            state_ = ScreenState::Error;
            return;
        }

        for( const std::string &path : paths )
        {
            add_source_path( path );
        }
    }

    void ImportModule::add_source_path( const std::filesystem::path &path )
    {
        if( path.empty() )
        {
            return;
        }

        const std::filesystem::path normalized = path.lexically_normal();

        const auto duplicate = std::find( source_paths_.begin(), source_paths_.end(), normalized );

        if( duplicate != source_paths_.end() )
        {
            selected_source_ = static_cast<std::int32_t>( std::distance( source_paths_.begin(), duplicate ) );
            return;
        }

        source_paths_.push_back( normalized );
        selected_source_ = static_cast<std::int32_t>( source_paths_.size() ) - 1;
    }

    void ImportModule::remove_selected_source()
    {
        if( selected_source_ < 0 || selected_source_ >= static_cast<std::int32_t>( source_paths_.size() ) )
        {
            return;
        }

        source_paths_.erase( source_paths_.begin() + selected_source_ );

        if( source_paths_.empty() )
        {
            selected_source_ = -1;
        }
        else
        {
            selected_source_ = std::min( selected_source_, static_cast<std::int32_t>( source_paths_.size() ) - 1 );
        }
    }

    void ImportModule::start_import( App &app )
    {
        if( state_ != ScreenState::Selection || source_paths_.empty() || import_future_.valid() )
        {
            return;
        }

        const std::vector<std::filesystem::path> source_paths = source_paths_;
        const std::filesystem::path application_root = app.game_data().application_root();
        const convert::AudioCompression audio_compression = audio_compression_;
        app.settings().audio_import_compression = audio_compression_ == convert::AudioCompression::Opus ? 1 : 0;
        managers::save_game_settings( app.settings() );
        worker_state_ = std::make_shared<ImportWorkerState>();

        state_ = ScreenState::Importing;
        error_message_.clear();
        warning_count_ = 0u;

        import_future_ = std::async( std::launch::async,
            [ source_paths, application_root, audio_compression, worker_state = worker_state_ ]()
            {
                return gamedata::RuntimeImporter::import_sources( source_paths,
                    application_root,
                    audio_compression,
                    [ worker_state ]( const gamedata::RuntimeImportProgress &progress )
                    {
                        std::scoped_lock lock( worker_state->mutex );
                        worker_state->progress = progress;
                    } );
            } );
    }

    void ImportModule::cancel( App &app )
    {
        if( state_ == ScreenState::Importing )
        {
            return;
        }

        if( state_ == ScreenState::Error )
        {
            state_ = ScreenState::Selection;
            error_message_.clear();
            return;
        }

        if( runtime_required_ )
        {
            app.request_quit();
            return;
        }

        return_to_target( app );
    }

    void ImportModule::return_to_target( App &app )
    {
        if( return_target_ == ReturnTarget::Options )
        {
            app.change_module( std::make_unique<OptionsModule>( nullptr, OptionsModule::EntryPage::GameData ) );
            return;
        }

        if( return_target_ == ReturnTarget::StageSelect )
        {
            app.change_module( std::make_unique<StageSelectModule>() );
            return;
        }

        if( return_target_ == ReturnTarget::Opening && app.settings().play_movies )
        {
            try
            {
                const std::optional<std::int32_t> opening_page = gamedata::album_opening_movie_page( app.game_data() );

                if( opening_page.has_value() )
                {
                    const gamedata::AlbumFlowTarget target = gamedata::resolve_album_flow_target( app.game_data(),
                        *opening_page,
                        app.settings().chart_style );

                    if( target.type == gamedata::AlbumFlowTargetType::Movie )
                    {
                        app.change_module( std::make_unique<MovieModule>( target.page_id, target.movie_path ) );
                        return;
                    }
                }
            }
            catch( const std::exception & )
            {
            }
        }

        app.change_module( std::make_unique<MainMenuModule>() );
    }

    void ImportModule::handle_event( App &app, const SDL_Event &event )
    {
        if( state_ != ScreenState::Selection )
        {
            return;
        }

        if( event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat )
        {
            if( event.key.key == SDLK_INSERT )
            {
                open_file_dialog( app );
            }
            else if( event.key.key == SDLK_DELETE )
            {
                remove_selected_source();
            }
        }
    }

    void ImportModule::update( App &app, double delta_seconds )
    {
        elapsed_seconds_ += delta_seconds;
        consume_file_dialog();

        if( state_ == ScreenState::Importing && import_future_.valid() &&
            import_future_.wait_for( std::chrono::milliseconds( 0 ) ) == std::future_status::ready )
        {
            const gamedata::RuntimeImportResult result = import_future_.get();
            warning_count_ = result.warnings.size();

            if( !result.success )
            {
                error_message_ =
                    result.error.empty() ? localization::text( "@ui.the_game_data_import_failed" ) : result.error;

                state_ = ScreenState::Error;
            }
            else
            {
                std::string reload_message;

                if( !app.reload_runtime_data( reload_message ) )
                {
                    error_message_ = reload_message;

                    state_ = ScreenState::Error;
                }
                else
                {
                    preview_stages_.clear();
                    std::unordered_set<std::string> seen;

                    for( const gamedata::ChartStyle style :
                        { gamedata::ChartStyle::GitarooMan, gamedata::ChartStyle::GitarooManLives } )
                    {
                        try
                        {
                            for( gamedata::PlayableStageEntry &stage :
                                gamedata::load_playable_stage_catalog( app.game_data(), style ) )
                            {
                                if( stage.ssq_exists && !stage.runtime_model_root.empty() &&
                                    seen.insert( StageSelectModule::stage_preview_key( stage ) ).second )
                                {
                                    preview_stages_.push_back( std::move( stage ) );
                                }
                            }
                        }
                        catch( const std::exception &error )
                        {
                            std::cerr << "Stage preview catalog unavailable: " << error.what() << '\n';
                        }
                    }

                    preview_index_ = 0u;

                    preview_failed_ = 0u;

                    state_ = preview_stages_.empty() ? ScreenState::Complete : ScreenState::Previewing;
                }
            }
        }

        const managers::InputManager &input = app.input();

        if( state_ == ScreenState::Previewing && preview_index_ >= preview_stages_.size() )
        {
            state_ = ScreenState::Complete;
        }

        const managers::MouseState &mouse = input.mouse();
        const rendering::LogicalViewport layout = rendering::make_logical_viewport( app.renderer(), 1280.0f, 720.0f );

        if( state_ == ScreenState::Selection )
        {
            if( input.pressed( managers::InputAction::MenuUp ) && !source_paths_.empty() )
            {
                selected_source_ = std::max( 0, selected_source_ - 1 );
            }

            if( input.pressed( managers::InputAction::MenuDown ) && !source_paths_.empty() )
            {
                selected_source_ =
                    std::min( static_cast<std::int32_t>( source_paths_.size() ) - 1, selected_source_ + 1 );
            }

            if( input.pressed( managers::InputAction::MenuLeft ) )
            {
                audio_compression_ = convert::AudioCompression::Flac;
            }

            if( input.pressed( managers::InputAction::MenuRight ) )
            {
                audio_compression_ = convert::AudioCompression::Opus;
            }

            if( input.pressed( managers::InputAction::Confirm ) && !mouse.left_clicked && !source_paths_.empty() )
            {
                start_import( app );
            }

            if( input.pressed( managers::InputAction::Back ) )
            {
                cancel( app );
            }

            if( mouse.left_clicked )
            {
                const rendering::Rect list = list_rect( layout );

                if( rendering::point_inside( list, mouse.x, mouse.y ) )
                {
                    const float row_height = 38.0f * layout.scale;
                    const std::int32_t row =
                        static_cast<std::int32_t>( ( mouse.y - list.y - 8.0f * layout.scale ) / row_height );

                    const std::int32_t first_visible = selected_source_ < 5 ? 0 : selected_source_ - 4;
                    const std::int32_t source_index = first_visible + row;

                    if( row >= 0 && row < 5 && source_index < static_cast<std::int32_t>( source_paths_.size() ) )
                    {
                        selected_source_ = source_index;
                    }
                }
                else if( rendering::point_inside( add_rect( layout ), mouse.x, mouse.y ) )
                {
                    open_file_dialog( app );
                }
                else if( rendering::point_inside( remove_rect( layout ), mouse.x, mouse.y ) )
                {
                    remove_selected_source();
                }
                else if( rendering::point_inside( flac_rect( layout ), mouse.x, mouse.y ) )
                {
                    audio_compression_ = convert::AudioCompression::Flac;
                }
                else if( rendering::point_inside( opus_rect( layout ), mouse.x, mouse.y ) )
                {
                    audio_compression_ = convert::AudioCompression::Opus;
                }
                else if( rendering::point_inside( ok_rect( layout ), mouse.x, mouse.y ) )
                {
                    start_import( app );
                }
                else if( rendering::point_inside( cancel_rect( layout ), mouse.x, mouse.y ) )
                {
                    cancel( app );
                }
            }
        }
        else if( state_ == ScreenState::Complete )
        {
            if( ( input.pressed( managers::InputAction::Confirm ) && !mouse.left_clicked ) ||
                input.pressed( managers::InputAction::Back ) )
            {
                return_to_target( app );
            }

            if( mouse.left_clicked && rendering::point_inside( continue_rect( layout ), mouse.x, mouse.y ) )
            {
                return_to_target( app );
            }
        }
        else if( state_ == ScreenState::Error )
        {
            if( ( input.pressed( managers::InputAction::Confirm ) && !mouse.left_clicked ) ||
                input.pressed( managers::InputAction::Back ) )
            {
                cancel( app );
            }

            if( mouse.left_clicked && rendering::point_inside( continue_rect( layout ), mouse.x, mouse.y ) )
            {
                cancel( app );
            }
        }
    }

    void ImportModule::render( App &app, rendering::Renderer &renderer )
    {
        if( !background_.render( renderer, static_cast<float>( elapsed_seconds_ ) ) )
        {
            renderer.clear( 0.98f, 0.82f, 0.75f, 1.0f );
        }

        const rendering::LogicalViewport layout = rendering::make_logical_viewport( renderer, 1280.0f, 720.0f );
        draw_polka_dots( app, renderer, layout, elapsed_seconds_ );
        renderer.draw_filled_rect( layout.rect( 150.0f, 44.0f, 980.0f, 604.0f ),
            rendering::Color{ 1.0f, 0.985f, 0.965f, 0.80f } );

        if( const rendering::Texture *title =
                app.texture_manager().get( localization::texture_key( title_texture_name ) ) )
        {
            const float aspect =
                static_cast<float>( title->width ) / static_cast<float>( std::max( title->height, 1 ) );

            const float title_height = std::min( 54.0f * responsive_scale( layout ), 920.0f * layout.scale / aspect );
            const float title_width = title_height * aspect;

            renderer.draw_texture( *title,
                layout.to_x( 640.0f ) - title_width * 0.5f,
                layout.to_y( 74.0f ),
                title_width,
                title_height );
        }
        else
        {
            draw_centered_text( renderer,
                layout,
                640.0f,
                92.0f,
                localization::text( "@ui.import_your_game_data_upper" ),
                rendering::Color{ 0.28f, 0.15f, 0.27f, 1.0f },
                2.8f );
        }

        const rendering::Color body_text{ 0.10f, 0.08f, 0.09f, 0.96f };

        if( text_renderer_ && text_renderer_->ready )
        {
            text_renderer_->draw( app,
                renderer,
                layout,
                "intro_0",
                localization::text( "@ui.import.disc_prompt" ),
                body_text,
                640.0f,
                152.0f,
                17.0f,
                true );

            text_renderer_->draw( app,
                renderer,
                layout,
                "intro_1",
                localization::text( "@ui.import.optional_content" ),
                body_text,
                640.0f,
                184.0f,
                17.0f,
                true );
        }
        else
        {
            draw_centered_text( renderer,
                layout,
                640.0f,
                152.0f,
                localization::text( "@ui.import.disc_prompt" ),
                body_text,
                1.16f );

            draw_centered_text( renderer,
                layout,
                640.0f,
                184.0f,
                localization::text( "@ui.import.optional_content" ),
                body_text,
                1.08f );
        }

        switch( state_ )
        {
            case ScreenState::Selection:
                render_selection( app, renderer );
                break;
            case ScreenState::Importing:
                render_importing( app, renderer );
                break;
            case ScreenState::Previewing:
                render_previewing( app, renderer );
                break;
            case ScreenState::Complete:
                render_complete( app, renderer );
                break;
            case ScreenState::Error:
                render_error( app, renderer );
                break;
        }
    }

    void ImportModule::render_selection( App &app, rendering::Renderer &renderer )
    {
        const rendering::LogicalViewport layout = rendering::make_logical_viewport( renderer, 1280.0f, 720.0f );
        const managers::MouseState &mouse = app.input().mouse();
        const rendering::Rect list = list_rect( layout );
        renderer.draw_filled_rect( list, rendering::Color{ 0.24f, 0.14f, 0.24f, 0.12f } );

        if( source_paths_.empty() )
        {
            if( text_renderer_ && text_renderer_->ready )
            {
                text_renderer_->draw( app,
                    renderer,
                    layout,
                    "empty_list",
                    localization::text( "@ui.no_disc_images_added_yet" ),
                    rendering::Color{ 0.42f, 0.33f, 0.40f, 0.70f },
                    640.0f,
                    342.0f,
                    17.0f,
                    true,
                    false );
            }
            else
            {
                draw_centered_text( renderer,
                    layout,
                    640.0f,
                    342.0f,
                    localization::text( "@ui.no_disc_images_added_yet_upper" ),
                    rendering::Color{ 0.42f, 0.33f, 0.40f, 0.70f },
                    1.2f );
            }
        }
        else
        {
            const std::int32_t first_visible = selected_source_ < 5 ? 0 : selected_source_ - 4;
            const std::int32_t visible_count =
                std::min<std::int32_t>( static_cast<std::int32_t>( source_paths_.size() ) - first_visible, 5 );

            for( std::int32_t visible_index = 0; visible_index < visible_count; ++visible_index )
            {
                const std::int32_t index = first_visible + visible_index;
                const float row_y = 254.0f + static_cast<float>( visible_index ) * 38.0f;
                const rendering::Rect row = layout.rect( 230.0f, row_y, 820.0f, 32.0f );

                if( index == selected_source_ )
                {
                    renderer.draw_filled_rect( row, rendering::Color{ 0.86f, 0.52f, 0.65f, 0.30f } );
                }

                const std::string number = std::to_string( index + 1 ) + ".";
                const std::string path = display_path( source_paths_[ index ], 84u );

                if( text_renderer_ && text_renderer_->ready )
                {
                    text_renderer_->draw( app,
                        renderer,
                        layout,
                        "row_number_" + std::to_string( visible_index ),
                        number,
                        rendering::Color{ 0.39f, 0.23f, 0.36f, 0.92f },
                        244.0f,
                        row_y + 7.0f,
                        16.0f,
                        false,
                        true );

                    text_renderer_->draw( app,
                        renderer,
                        layout,
                        "row_path_" + std::to_string( visible_index ),
                        path,
                        rendering::Color{ 0.20f, 0.14f, 0.20f, 0.96f },
                        278.0f,
                        row_y + 7.0f,
                        15.0f,
                        false );
                }
                else
                {
                    renderer.draw_text( layout.to_x( 244.0f ),
                        layout.to_y( row_y + 10.0f ),
                        number.c_str(),
                        rendering::Color{ 0.39f, 0.23f, 0.36f, 0.92f },
                        responsive_text_scale( layout, 1.25f ) );

                    renderer.draw_text( layout.to_x( 278.0f ),
                        layout.to_y( row_y + 10.0f ),
                        path.c_str(),
                        rendering::Color{ 0.28f, 0.19f, 0.28f, 0.94f },
                        responsive_text_scale( layout, 1.15f ) );
                }
            }

            if( source_paths_.size() > 5u )
            {
                const std::string range = std::to_string( first_visible + 1 ) + "-" +
                                          std::to_string( first_visible + visible_count ) + " / " +
                                          std::to_string( source_paths_.size() );

                if( text_renderer_ && text_renderer_->ready )
                {
                    text_renderer_->draw( app,
                        renderer,
                        layout,
                        "list_range",
                        range,
                        rendering::Color{ 0.45f, 0.34f, 0.42f, 0.75f },
                        930.0f,
                        448.0f,
                        12.0f,
                        false );
                }
                else
                {
                    renderer.draw_text( layout.to_x( 930.0f ),
                        layout.to_y( 448.0f ),
                        range.c_str(),
                        rendering::Color{ 0.45f, 0.34f, 0.42f, 0.75f },
                        responsive_text_scale( layout, 0.95f ) );
                }
            }
        }

        const auto draw_button = [ & ]( std::string_view slot_id,
                                     const rendering::Rect &rect,
                                     const char *label,
                                     bool enabled,
                                     bool emphasized )
        {
            const bool hovered = enabled && rendering::point_inside( rect, mouse.x, mouse.y );
            draw_button_frame( renderer, rect, enabled, emphasized, hovered );

            const rendering::Color text_color = !enabled     ? rendering::Color{ 0.46f, 0.43f, 0.46f, 0.65f }
                                                : emphasized ? rendering::Color{ 1.0f, 0.98f, 0.97f, 1.0f }
                                                             : rendering::Color{ 0.20f, 0.13f, 0.20f, 1.0f };
            if( text_renderer_ && text_renderer_->ready )
            {
                text_renderer_->draw_in_rect( app, renderer, layout, slot_id, label, text_color, rect, 16.0f, true );
            }
            else
            {
                const float text_scale = std::max( 1.0f, rect.h / 22.0f );
                renderer.draw_text( rect.x + ( rect.w - text_width( label, text_scale ) ) * 0.5f,
                    rect.y + ( rect.h - 7.0f * text_scale ) * 0.5f,
                    label,
                    text_color,
                    text_scale );
            }
        };

        draw_button( "button_add",
            add_rect( layout ),
            localization::text( "@ui.add_iso_upper" ),
            file_dialog_state_ == nullptr,
            false );

        draw_button( "button_remove",
            remove_rect( layout ),
            localization::text( "@ui.remove_upper" ),
            selected_source_ >= 0,
            false );

        if( text_renderer_ && text_renderer_->ready )
        {
            text_renderer_->draw( app,
                renderer,
                layout,
                "audio_compression_label",
                localization::text( "@ui.audio_compression" ),
                rendering::Color{ 0.30f, 0.19f, 0.29f, 0.92f },
                826.0f,
                493.0f,
                14.0f,
                true,
                true );
        }
        else
        {
            renderer.draw_text( layout.to_x( 620.0f ),
                layout.to_y( 488.0f ),
                localization::text( "@ui.audio_compression" ),
                rendering::Color{ 0.30f, 0.19f, 0.29f, 0.92f },
                responsive_text_scale( layout, 1.1f ) );
        }

        draw_button( "button_flac",
            flac_rect( layout ),
            localization::text( "@ui.flac_lossless_upper" ),
            true,
            audio_compression_ == convert::AudioCompression::Flac );

        draw_button( "button_opus",
            opus_rect( layout ),
            localization::text( "@ui.opus_lossy_upper" ),
            true,
            audio_compression_ == convert::AudioCompression::Opus );

        draw_button( "button_ok",
            ok_rect( layout ),
            localization::text( "@ui.confirm_upper" ),
            !source_paths_.empty(),
            true );

        draw_button( "button_cancel", cancel_rect( layout ), localization::text( "@ui.cancel_upper" ), true, false );
    }

    void ImportModule::render_importing( App &app, rendering::Renderer &renderer )
    {
        const rendering::LogicalViewport layout = rendering::make_logical_viewport( renderer, 1280.0f, 720.0f );
        gamedata::RuntimeImportProgress progress;

        if( worker_state_ )
        {
            std::scoped_lock lock( worker_state_->mutex );
            progress = worker_state_->progress;
        }

        const float fraction = std::clamp( progress.fraction, 0.0f, 1.0f );
        const rendering::Rect track = layout.rect( 250.0f, 356.0f, 780.0f, 26.0f );
        renderer.draw_filled_rect( track, rendering::Color{ 0.29f, 0.18f, 0.28f, 0.18f } );

        if( fraction > 0.0f )
        {
            renderer.draw_filled_rect( rendering::Rect{ track.x, track.y, track.w * fraction, track.h },
                rendering::Color{ 0.78f, 0.43f, 0.57f, 0.90f } );
        }

        const std::int32_t percent = static_cast<std::int32_t>( std::lround( fraction * 100.0f ) );
        const std::string percent_text = std::to_string( percent ) + "%";
        const char *status =
            progress.status.empty() ? localization::text( "@ui.preparing_import" ) : progress.status.c_str();

        if( text_renderer_ && text_renderer_->ready )
        {
            text_renderer_->draw( app,
                renderer,
                layout,
                "progress_status",
                status,
                rendering::Color{ 0.22f, 0.14f, 0.22f, 1.0f },
                640.0f,
                310.0f,
                18.0f,
                true );

            text_renderer_->draw( app,
                renderer,
                layout,
                "progress_percent",
                percent_text,
                rendering::Color{ 0.30f, 0.19f, 0.29f, 0.92f },
                640.0f,
                394.0f,
                15.0f,
                true );
        }
        else
        {
            draw_centered_text( renderer,
                layout,
                640.0f,
                312.0f,
                status,
                rendering::Color{ 0.30f, 0.18f, 0.29f, 1.0f },
                1.35f );

            draw_centered_text( renderer,
                layout,
                640.0f,
                391.0f,
                percent_text.c_str(),
                rendering::Color{ 0.38f, 0.24f, 0.35f, 0.88f },
                1.0f );
        }

        if( !progress.source_label.empty() )
        {
            std::string source = progress.source_label;

            if( progress.source_count > 0u && progress.source_index < progress.source_count )
            {
                source += "  ";

                source += std::to_string( progress.source_index + 1u );

                source += "/";

                source += std::to_string( progress.source_count );
            }

            if( text_renderer_ && text_renderer_->ready )
            {
                text_renderer_->draw( app,
                    renderer,
                    layout,
                    "progress_source",
                    source,
                    rendering::Color{ 0.39f, 0.28f, 0.37f, 0.86f },
                    640.0f,
                    425.0f,
                    14.0f,
                    true );
            }
            else
            {
                draw_centered_text( renderer,
                    layout,
                    640.0f,
                    424.0f,
                    source.c_str(),
                    rendering::Color{ 0.47f, 0.34f, 0.43f, 0.82f },
                    1.0f );
            }
        }

        const std::string current = progress.current_file.empty() ? std::string( localization::text( "@ui.scanning" ) )
                                                                  : display_path( progress.current_file, 110u );

        if( text_renderer_ && text_renderer_->ready )
        {
            text_renderer_->draw( app,
                renderer,
                layout,
                "progress_file",
                current,
                rendering::Color{ 0.30f, 0.22f, 0.29f, 0.82f },
                640.0f,
                466.0f,
                13.0f,
                true );
        }
        else
        {
            draw_centered_text( renderer,
                layout,
                640.0f,
                466.0f,
                current.c_str(),
                rendering::Color{ 0.36f, 0.27f, 0.34f, 0.78f },
                0.9f );
        }
    }

    void ImportModule::render_previewing( App &app, rendering::Renderer &renderer )
    {
        if( preview_index_ < preview_stages_.size() )
        {
            if( !StageSelectModule::ensure_stage_preview( app, renderer, preview_stages_[ preview_index_ ] ) )
            {
                ++preview_failed_;
            }

            ++preview_index_;
        }

        const rendering::LogicalViewport layout = rendering::make_logical_viewport( renderer, 1280.0f, 720.0f );
        const std::string status = localization::text( "@ui.preparing_stage_previews" ) +
                                   std::to_string( preview_index_ ) + " / " + std::to_string( preview_stages_.size() );

        const std::string detail = localization::text( "@ui.import.preview_description" );

        if( text_renderer_ && text_renderer_->ready )
        {
            text_renderer_->draw( app,
                renderer,
                layout,
                "preview_status",
                status,
                rendering::Color{ 0.22f, 0.13f, 0.21f, 1.0f },
                640.0f,
                310.0f,
                26.0f,
                true );

            text_renderer_->draw( app,
                renderer,
                layout,
                "preview_detail",
                detail,
                rendering::Color{ 0.35f, 0.26f, 0.33f, 0.90f },
                640.0f,
                369.0f,
                16.0f,
                true );
        }
        else
        {
            draw_centered_text( renderer,
                layout,
                640.0f,
                315.0f,
                status.c_str(),
                rendering::Color{ 0.28f, 0.16f, 0.27f, 1.0f },
                2.0f );
        }

        const float progress = preview_stages_.empty() ? 1.0f
                                                       : static_cast<float>( preview_index_ ) /
                                                             static_cast<float>( preview_stages_.size() );

        renderer.draw_filled_rect( layout.rect( 284.0f, 412.0f, 712.0f, 15.0f ),
            rendering::Color{ 0.28f, 0.16f, 0.27f, 0.30f } );

        renderer.draw_filled_rect( layout.rect( 284.0f, 412.0f, 712.0f * progress, 15.0f ),
            rendering::Color{ 0.87f, 0.42f, 0.58f, 1.0f } );
    }

    void ImportModule::render_complete( App &app, rendering::Renderer &renderer )
    {
        const rendering::LogicalViewport layout = rendering::make_logical_viewport( renderer, 1280.0f, 720.0f );
        const managers::MouseState &mouse = app.input().mouse();

        if( text_renderer_ && text_renderer_->ready )
        {
            text_renderer_->draw( app,
                renderer,
                layout,
                "complete_title",
                localization::text( "@ui.runtime_import_complete" ),
                rendering::Color{ 0.22f, 0.13f, 0.21f, 1.0f },
                640.0f,
                316.0f,
                26.0f,
                true );
        }
        else
        {
            draw_centered_text( renderer,
                layout,
                640.0f,
                320.0f,
                localization::text( "@ui.runtime_import_complete_upper" ),
                rendering::Color{ 0.28f, 0.16f, 0.27f, 1.0f },
                2.0f );
        }

        std::string detail = localization::text( "@ui.import.ready" );

        if( warning_count_ > 0u )
        {
            detail += localization::text( "@ui.warnings" ) + std::to_string( warning_count_ );
        }

        if( preview_failed_ > 0u )
        {
            detail += localization::text( "@ui.previews_unavailable" ) + std::to_string( preview_failed_ );
        }

        if( text_renderer_ && text_renderer_->ready )
        {
            text_renderer_->draw( app,
                renderer,
                layout,
                "complete_detail",
                detail,
                rendering::Color{ 0.35f, 0.26f, 0.33f, 0.90f },
                640.0f,
                372.0f,
                15.0f,
                true );
        }
        else
        {
            draw_centered_text( renderer,
                layout,
                640.0f,
                374.0f,
                detail.c_str(),
                rendering::Color{ 0.42f, 0.31f, 0.39f, 0.86f },
                1.05f );
        }

        const rendering::Rect button = continue_rect( layout );
        const bool hovered = rendering::point_inside( button, mouse.x, mouse.y );
        draw_button_frame( renderer, button, true, true, hovered );

        if( text_renderer_ && text_renderer_->ready )
        {
            text_renderer_->draw_in_rect( app,
                renderer,
                layout,
                "button_continue",
                localization::text( "@ui.continue_upper" ),
                rendering::Color{ 1.0f, 0.98f, 0.97f, 1.0f },
                button,
                22.0f );
        }
        else
        {
            const float text_scale = std::max( 1.0f, button.h / 22.0f );
            renderer.draw_text(
                button.x + ( button.w - text_width( localization::text( "@ui.continue_upper" ), text_scale ) ) * 0.5f,
                button.y + ( button.h - 7.0f * text_scale ) * 0.5f,
                localization::text( "@ui.continue_upper" ),
                rendering::Color{ 1.0f, 0.98f, 0.97f, 1.0f },
                text_scale );
        }
    }

    void ImportModule::render_error( App &app, rendering::Renderer &renderer )
    {
        const rendering::LogicalViewport layout = rendering::make_logical_viewport( renderer, 1280.0f, 720.0f );
        const managers::MouseState &mouse = app.input().mouse();

        if( text_renderer_ && text_renderer_->ready )
        {
            text_renderer_->draw( app,
                renderer,
                layout,
                "error_title",
                localization::text( "@ui.import_could_not_be_completed" ),
                rendering::Color{ 0.55f, 0.16f, 0.25f, 1.0f },
                640.0f,
                282.0f,
                24.0f,
                true );
        }
        else
        {
            draw_centered_text( renderer,
                layout,
                640.0f,
                284.0f,
                localization::text( "@ui.import_could_not_be_completed_upper" ),
                rendering::Color{ 0.55f, 0.16f, 0.25f, 1.0f },
                1.75f );
        }

        const std::string error = display_path( error_message_, 112u );

        if( text_renderer_ && text_renderer_->ready )
        {
            text_renderer_->draw( app,
                renderer,
                layout,
                "error_detail",
                error,
                rendering::Color{ 0.38f, 0.22f, 0.29f, 0.94f },
                640.0f,
                350.0f,
                14.0f,
                true );
        }
        else
        {
            draw_centered_text( renderer,
                layout,
                640.0f,
                350.0f,
                error.c_str(),
                rendering::Color{ 0.43f, 0.25f, 0.31f, 0.92f },
                0.95f );
        }

        const rendering::Rect button = continue_rect( layout );
        const bool hovered = rendering::point_inside( button, mouse.x, mouse.y );
        draw_button_frame( renderer, button, true, false, hovered );

        if( text_renderer_ && text_renderer_->ready )
        {
            text_renderer_->draw_in_rect( app,
                renderer,
                layout,
                "button_back",
                localization::text( "@ui.back_upper" ),
                rendering::Color{ 0.20f, 0.13f, 0.20f, 1.0f },
                button,
                22.0f );
        }
        else
        {
            const float text_scale = std::max( 1.0f, button.h / 22.0f );
            renderer.draw_text(
                button.x + ( button.w - text_width( localization::text( "@ui.back_upper" ), text_scale ) ) * 0.5f,
                button.y + ( button.h - 7.0f * text_scale ) * 0.5f,
                localization::text( "@ui.back_upper" ),
                rendering::Color{ 0.20f, 0.13f, 0.20f, 1.0f },
                text_scale );
        }
    }
}
