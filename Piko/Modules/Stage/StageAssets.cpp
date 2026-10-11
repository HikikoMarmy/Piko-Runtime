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
    rendering::TextureFilterMode StageModule::renderer_filter_mode_from_settings( managers::TextureFilteringMode mode )
    {
        switch( mode )
        {
            case managers::TextureFilteringMode::Nearest:
                return rendering::TextureFilterMode::Nearest;
            case managers::TextureFilteringMode::Linear:
                return rendering::TextureFilterMode::Linear;
            case managers::TextureFilteringMode::LinearMipmap:
                return rendering::TextureFilterMode::LinearMipmap;
            case managers::TextureFilteringMode::Anisotropic:
                return rendering::TextureFilterMode::Anisotropic;
        }

        return rendering::TextureFilterMode::LinearMipmap;
    }

    void StageModule::load_stage_textures( App &app )
    {
        itfc_status_.clear();

        for( auto &texture : phase_announcement_textures_ )
        {
            app.texture_manager().unload( texture );

            texture = {};
        }

        if( itfc_texture_ )
        {
            app.texture_manager().unload( itfc_texture_ );
        }

        if( you_win_texture_ )
        {
            app.texture_manager().unload( you_win_texture_ );
        }

        if( you_lose_texture_ )
        {
            app.texture_manager().unload( you_lose_texture_ );
        }

        if( again_texture_ )
        {
            app.texture_manager().unload( again_texture_ );
        }

        if( generated_tube_texture_ )
        {
            app.texture_manager().unload( generated_tube_texture_ );
        }

        if( generated_trace_texture_ )
        {
            app.texture_manager().unload( generated_trace_texture_ );
        }

        if( generated_score_digit_texture_ )
        {
            app.texture_manager().unload( generated_score_digit_texture_ );
        }

        app.texture_manager().unload( aim_arrow_texture_ );
        app.texture_manager().unload( wing_arrow_texture_ );
        app.texture_manager().unload( center_nub_texture_ );
        app.texture_manager().unload( pressed_nub_texture_ );
        aim_arrow_texture_ = {};

        wing_arrow_texture_ = {};

        center_nub_texture_ = {};

        pressed_nub_texture_ = {};

        itfc_texture_ = {};

        you_win_texture_ = {};

        you_lose_texture_ = {};

        again_texture_ = {};

        for( auto &label : retry_button_labels_ )
        {
            app.texture_manager().unload( label );
            label = {};
        }

        generated_tube_texture_ = {};

        generated_trace_texture_ = {};

        generated_score_digit_texture_ = {};

        try
        {
            generated_tube_texture_ = stage::load_note_textures( app.texture_manager(),
                app.game_data().game_data_root(),
                note_texture_regions_,
                "stage/note-tube",
                app.settings().gameplay_visual.note_style );
        }
        catch( const std::exception &e )
        {
            debug_message_ = std::string( "Generated note tube texture failed: " ) + e.what();

            generated_tube_texture_ = {};
        }

        try
        {
            generated_trace_texture_ = stage::load_trace_texture( app.texture_manager(),
                app.game_data().game_data_root(),
                "stage/trace-line",
                app.settings().gameplay_visual.note_style );
        }
        catch( const std::exception &e )
        {
            debug_message_ = std::string( "Generated trace texture failed: " ) + e.what();

            generated_trace_texture_ = {};
        }

        try
        {
            const std::vector<std::uint8_t> pixels = stage::make_generated_score_digit_texture_pixels();

            generated_score_digit_texture_ =
                app.texture_manager().create_rgba( stage::generated_score_digit_texture_name,
                    pixels.data(),
                    stage::score_digit_texture_width,
                    stage::score_digit_texture_height );
        }
        catch( const std::exception &e )
        {
            debug_message_ = std::string( "Generated score digit texture failed: " ) + e.what();

            generated_score_digit_texture_ = {};
        }

        aim_arrow_texture_ = stage::load_aim_sprite( app.texture_manager(),
            app.game_data().resource_root(),
            "stage/hud/aim-arrow",
            "AimArrow.png",
            app.settings().gameplay_visual.aim_style );

        wing_arrow_texture_ = stage::load_aim_sprite( app.texture_manager(),
            app.game_data().resource_root(),
            "stage/hud/wing-arrow",
            "WingArrow.png",
            app.settings().gameplay_visual.aim_style );

        center_nub_texture_ = stage::load_centre_sprite( app.texture_manager(),
            app.game_data().game_data_root(),
            "stage/hud/centre",
            app.settings().gameplay_visual,
            false );

        pressed_nub_texture_ = stage::load_centre_sprite( app.texture_manager(),
            app.game_data().game_data_root(),
            "stage/hud/centre-pressed",
            app.settings().gameplay_visual,
            true );

        char stage_group[ 16 ]{};
        std::snprintf( stage_group, sizeof( stage_group ), "STAGE%02d", std::max( 0, load_data_.stage.stage_number ) );

        for( std::size_t index = 0; index < phase_announcement_textures_.size(); ++index )
        {
            char filename[ 16 ]{};
            std::snprintf( filename, sizeof( filename ), "TITLE%02zu.png", index + 1 );

            const auto path = app.game_data().resolve_runtime_asset(
                ( std::filesystem::path( "LOADING/TEXTURES/STSHARE" ) / filename ).generic_string() );

            if( path.empty() )
            {
                continue;
            }

            try
            {
                phase_announcement_textures_[ index ] =
                    app.texture_manager().load_png( "stage/hud/phase/" + std::to_string( index ), path );
            }
            catch( const std::exception &error )
            {
                std::cerr << "[Piko Runtime] Phase PNG failed: " << error.what() << '\n';
            }
        }

        const auto outcome_path = app.game_data().resolve_runtime_asset( "LOADING/TEXTURES/STSHARE/YOU_WIN.png" );

        if( !outcome_path.empty() )
        {
            try
            {
                you_win_texture_ = app.texture_manager().load_png( "stage/hud/outcome", outcome_path );

                you_lose_texture_ = you_win_texture_;
            }
            catch( const std::exception &error )
            {
                std::cerr << "[Piko Runtime] Outcome PNG failed: " << error.what() << '\n';
            }
        }

        retry_button_labels_[ 0 ] = localization::make_label( app.texture_manager(), "@ui.result.retry", "menu" );

        retry_button_labels_[ 1 ] = localization::make_label( app.texture_manager(), "@ui.result.end", "menu" );

        const auto again_path = app.game_data().resolve_runtime_asset( "LOADING/TEXTURES/STANDALONE/S_AGAIN.png" );

        if( !again_path.empty() )
        {
            try
            {
                again_texture_ = app.texture_manager().load_png( "stage/hud/again", again_path );
            }
            catch( const std::exception &error )
            {
                std::cerr << "[Piko Runtime] Retry PNG failed: " << error.what() << '\n';
            }
        }

        if( itfc_texture_ )
        {
            itfc_status_ = "ITFC.png override loaded";
            return;
        }

        if( load_data_.stage.portable_visuals )
        {
            char filename[ 16 ]{};
            std::snprintf( filename, sizeof( filename ), "ST%02d.png", load_data_.stage.stage_number );
            const std::filesystem::path portable_hud =
                app.game_data().runtime_stage_texture( load_data_.stage.stage_number, "HUD", filename );

            if( !portable_hud.empty() )
            {
                try
                {
                    itfc_texture_ = app.texture_manager().load_png( "stage/hud/portable", portable_hud );

                    itfc_status_ = "PSP HUD loaded";
                    return;
                }
                catch( const std::exception &e )
                {
                    itfc_texture_ = {};

                    itfc_status_ = std::string( "PSP HUD load failed: " ) + e.what();
                }
            }
        }

        const std::filesystem::path converted_itfc =
            app.game_data().runtime_stage_texture( load_data_.stage.stage_number, stage_group, "ITFC.png" );

        if( converted_itfc.empty() )
        {
            itfc_status_ = "Converted ITFC.png is missing";
            std::cerr << "[Piko Runtime] Stage " << load_data_.stage.stage_number << ": " << itfc_status_ << '\n';
            return;
        }

        try
        {
            itfc_texture_ = app.texture_manager().load_png( stage::itfc_texture_name, converted_itfc );

            itfc_status_ = "Converted ITFC.png loaded";
        }
        catch( const std::exception &e )
        {
            itfc_texture_ = {};

            itfc_status_ = std::string( "Converted ITFC.png load failed: " ) + e.what();
            std::cerr << "[Piko Runtime] Stage " << load_data_.stage.stage_number << ": " << itfc_status_ << '\n';
        }
    }

    void StageModule::unload_stage_textures( App &app )
    {
        for( auto &texture : phase_announcement_textures_ )
        {
            app.texture_manager().unload( texture );

            texture = {};
        }

        app.texture_manager().unload( itfc_texture_ );
        app.texture_manager().unload( aim_arrow_texture_ );
        app.texture_manager().unload( wing_arrow_texture_ );
        app.texture_manager().unload( center_nub_texture_ );
        app.texture_manager().unload( pressed_nub_texture_ );
        aim_arrow_texture_ = {};

        wing_arrow_texture_ = {};

        center_nub_texture_ = {};

        pressed_nub_texture_ = {};

        app.texture_manager().unload( you_win_texture_ );
        app.texture_manager().unload( you_lose_texture_ );
        app.texture_manager().unload( again_texture_ );
        app.texture_manager().unload( generated_tube_texture_ );
        app.texture_manager().unload( generated_trace_texture_ );
        app.texture_manager().unload( generated_score_digit_texture_ );
        app.texture_manager().unload( modifier_text_texture_ );
        modifier_text_texture_ = {};

        itfc_texture_ = {};

        you_win_texture_ = {};

        you_lose_texture_ = {};

        again_texture_ = {};

        for( auto &label : retry_button_labels_ )
        {
            app.texture_manager().unload( label );
            label = {};
        }

        generated_tube_texture_ = {};

        generated_trace_texture_ = {};

        generated_score_digit_texture_ = {};
    }

    void StageModule::load_modifier_texture( App &app )
    {
        if( tutorial_session() )
        {
            return;
        }

        app.texture_manager().unload( modifier_text_texture_ );
        modifier_text_texture_ = {};

        if( active_modifier_text_.empty() )
        {
            return;
        }

        try
        {
            rendering::TrueTypeFont font;
            std::string error;

            for( const char *filename : modifier_font_filenames )
            {
                const auto path = utils::find_resource_file( app.game_data().application_root(),
                    std::filesystem::path( "fonts" ) / filename );

                if( !path.empty() && font.load( path, &error ) )
                {
                    break;
                }
            }

            if( !font.loaded() )
            {
                std::cerr << "[Piko UI] Modifier font unavailable: " << error << '\n';
                return;
            }

            rendering::StrokedTextStyle style;
            style.fill = modifier_fill_color;

            style.inner_border_pixels = 0;

            style.outer_border_pixels = modifier_border_pixels;

            style.outer_border = modifier_border_color;

            const auto text = font.rasterize_stroked( active_modifier_text_,
                modifier_font_height * modifier_raster_scale,
                style,
                &error );

            if( !text.valid() )
            {
                std::cerr << "[Piko UI] Modifier text failed: " << error << '\n';
                return;
            }

            modifier_text_texture_ =
                app.texture_manager().create_rgba( "stage/modifiers", text.rgba.data(), text.width, text.height );
        }
        catch( const std::exception &error )
        {
            std::cerr << "[Piko UI] Modifier label failed: " << error.what() << '\n';
        }
    }
}
