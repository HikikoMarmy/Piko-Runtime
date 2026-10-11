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
    void StageModule::handle_event( App &app, const SDL_Event &event )
    {
#if defined( _DEBUG )
        if( event.type != SDL_EVENT_KEY_DOWN || event.key.repeat )
        {
            return;
        }

        if( ( event.key.mod & SDL_KMOD_CTRL ) != 0 )
        {
            if( event.key.key == SDLK_F7 )
            {
                show_preview_timing_ = !show_preview_timing_;
                return;
            }

            if( event.key.key == SDLK_F8 )
            {
                copy_preview_timing();
                return;
            }
        }

        const auto set_visual_debug_message = [ this ]()
        {
            char message[ 160 ]{};
            std::snprintf( message,
                sizeof( message ),
                "Debug: trace spacing X %.2f Y %.2f | cap-end trim %.1f px | cap %s",
                debug_presentation_scale_x_,
                debug_presentation_scale_y_,
                debug_phrase_tube_visual_trim_pixels_,
                debug_phrase_cap_consumes_body_ ? "consume" : "append" );

            debug_message_ = message;
        };

        const auto adjust_presentation_scale_x = [ this, &set_visual_debug_message ]( float delta )
        {
            debug_presentation_scale_x_ = std::clamp( debug_presentation_scale_x_ + delta,
                stage::gameplay_presentation_scale_debug_min,
                stage::gameplay_presentation_scale_debug_max );

            set_visual_debug_message();
        };

        const auto adjust_presentation_scale_y = [ this, &set_visual_debug_message ]( float delta )
        {
            debug_presentation_scale_y_ = std::clamp( debug_presentation_scale_y_ + delta,
                stage::gameplay_presentation_scale_debug_min,
                stage::gameplay_presentation_scale_debug_max );

            set_visual_debug_message();
        };

        const auto adjust_phrase_tube_visual_trim = [ this, &set_visual_debug_message ]( float delta )
        {
            debug_phrase_tube_visual_trim_pixels_ = std::clamp( debug_phrase_tube_visual_trim_pixels_ + delta,
                stage::phrase_tube_visual_trim_debug_min,
                stage::phrase_tube_visual_trim_debug_max );

            set_visual_debug_message();
        };

        switch( event.key.key )
        {
            case SDLK_F1:
                adjust_presentation_scale_x( -stage::gameplay_presentation_scale_debug_step );
                break;

            case SDLK_F2:
                adjust_presentation_scale_x( stage::gameplay_presentation_scale_debug_step );
                break;

            case SDLK_F3:
                adjust_presentation_scale_y( -stage::gameplay_presentation_scale_debug_step );
                break;

            case SDLK_F4:
                adjust_presentation_scale_y( stage::gameplay_presentation_scale_debug_step );
                break;

            case SDLK_F7:
                debug_presentation_scale_x_ = stage::gameplay_presentation_scale_x;

                debug_presentation_scale_y_ = stage::gameplay_presentation_scale_y;
                set_visual_debug_message();
                break;

            case SDLK_F8:
                if( runtime_initialized_ && runtime_.has_song() && !runtime_.state().song_finished )
                {
                    const std::int64_t remaining =
                        std::max<std::int64_t>( 0, runtime_.current_section_remaining_samples() );

                    runtime_.seek_relative_samples( remaining + 1 );
                    stage_audio_.restart_from_runtime( app.audio(), runtime_ );
                    debug_message_ = "Debug: skipped to next section";
                }
                break;

            case SDLK_F9:
                adjust_phrase_tube_visual_trim( -stage::phrase_tube_visual_trim_debug_step );
                break;

            case SDLK_F10:
                adjust_phrase_tube_visual_trim( stage::phrase_tube_visual_trim_debug_step );
                break;

            case SDLK_F11:
                debug_phrase_tube_visual_trim_pixels_ = stage::phrase_tube_visual_trim_pixels;
                set_visual_debug_message();
                break;

            case SDLK_F12:
                debug_phrase_cap_consumes_body_ = !debug_phrase_cap_consumes_body_;
                set_visual_debug_message();
                break;

            case SDLK_F6:
                debug_zoom_multiplier_ = std::min( 1.50f, debug_zoom_multiplier_ + 0.1f );

                debug_message_ = "Debug: zoom " + std::to_string( debug_zoom_multiplier_ ).substr( 0, 3 ) + "x";
                break;

            case SDLK_F5:
                debug_zoom_multiplier_ = std::max( 0.50f, debug_zoom_multiplier_ - 0.1f );

                debug_message_ = "Debug: zoom " + std::to_string( debug_zoom_multiplier_ ).substr( 0, 3 ) + "x";
                break;

            case SDLK_HOME:
                stage3d_.toggle_texture_debug_overlay();
                debug_message_ =
                    stage3d_.texture_debug_overlay_visible() ? "Texture inspector opened" : "Texture inspector closed";
                break;

            case SDLK_PAGEUP:
                stage3d_.cycle_texture_debug_sample( -1 );
                debug_message_ = "Texture: " + stage3d_.texture_debug_name();
                break;

            case SDLK_PAGEDOWN:
                stage3d_.cycle_texture_debug_sample( 1 );
                debug_message_ = "Texture: " + stage3d_.texture_debug_name();
                break;

            case SDLK_END:
                stage3d_.cycle_texture_debug_view();
                debug_message_ = "Texture view: " + std::string( stage3d_.texture_debug_view_name() );
                break;

            case 'b':
                stage3d_.begin_debug_camera_pullback( 3.0f );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case 'r':
                stage3d_.reset_debug_camera();
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case 'i':
                stage3d_.nudge_debug_camera_local( 0.0f, 0.0f, -stage3d_debug_camera_nudge_units );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case 'k':
                stage3d_.nudge_debug_camera_local( 0.0f, 0.0f, stage3d_debug_camera_nudge_units );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case 'j':
                stage3d_.nudge_debug_camera_local( -stage3d_debug_camera_nudge_units, 0.0f, 0.0f );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case 'l':
                stage3d_.nudge_debug_camera_local( stage3d_debug_camera_nudge_units, 0.0f, 0.0f );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case 'u':
                stage3d_.nudge_debug_camera_local( 0.0f, -stage3d_debug_camera_nudge_units, 0.0f );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case 'o':
                stage3d_.nudge_debug_camera_local( 0.0f, stage3d_debug_camera_nudge_units, 0.0f );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case 'm':
                stage3d_.cycle_material_debug_mode();
                debug_message_ = stage3d_.material_debug_status_line();
                break;

            case 'n':
                stage3d_.cycle_material_debug_sample();
                debug_message_ = stage3d_.material_debug_status_line();
                break;

            case 'v':
                stage3d_.toggle_material_debug_focus();
                debug_message_ = stage3d_.material_debug_status_line();
                break;

            case 'c':
                stage3d_.cycle_camera_rotation_mode();
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case 'g':
                stage3d_.cycle_animation_mode();
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case 'y':
                stage3d_.cycle_model_rotation_mode();
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case 'h':
                stage3d_.cycle_xg_rotation_mode();
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case 't':
                stage3d_.cycle_skinning_formula_mode();
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case 'x':
                stage3d_.cycle_model_basis_mode();
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case 'e':
                stage3d_.toggle_special_model_types();
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case 'q':
                stage3d_.toggle_lighting_backend();
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case 'z':
                stage3d_.toggle_depth_mode();
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case '5':
                stage3d_.nudge_depth_near_plane( -1.0f );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case '6':
                stage3d_.nudge_depth_near_plane( 1.0f );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case ';':
                stage3d_.nudge_scene_lighting_strength( -0.05f );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case '\'':
                stage3d_.nudge_scene_lighting_strength( 0.05f );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case '-':
                stage3d_.nudge_scene_lighting_floor( -0.05f );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case '=':
                stage3d_.nudge_scene_lighting_floor( 0.05f );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case '[':
                stage3d_.nudge_gmlt_light_strength( -0.10f );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case ']':
                stage3d_.nudge_gmlt_light_strength( 0.10f );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case '7':
                stage3d_.cycle_gmlt_debug_mode();
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case '8':
                stage3d_.cycle_gmlt_direction_mode();
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case '3':
                stage3d_.nudge_ps2_light_wrap( -0.05f );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case '4':
                stage3d_.nudge_ps2_light_wrap( 0.05f );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case '0':
                stage3d_.reset_scene_lighting();
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case 'p':
                stage3d_.toggle_stage_sky();
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case ',':
                stage3d_.nudge_camera_frame_offset( -15.0f );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case '.':
                stage3d_.nudge_camera_frame_offset( 15.0f );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case '/':
                stage3d_.reset_camera_frame_offset();
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case '1':
                stage3d_.nudge_camera_time_scale( -1 );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            case '2':
                stage3d_.nudge_camera_time_scale( 1 );
                debug_message_ = stage3d_.debug_camera_line();
                break;

            default:
                break;
        }
#else
        (void)app;
        (void)event;
#endif
    }

    void StageModule::update_mouse_capture( App &app )
    {
        bool allow_mouse_aim = app.settings().mouse_controls_enabled && !runtime_.paused() && !stage_result_active_ &&
                               !tutorial_completion_active_ &&
                               ( !teaching_tutorial() || tutorial_.timeline().aim_visible() );

#if PIKO_STAGE_HAS_IMGUI
        if( ImGui::GetCurrentContext() != nullptr )
        {
            const auto &io = ImGui::GetIO();
            allow_mouse_aim = allow_mouse_aim && !io.WantCaptureMouse && !io.WantCaptureKeyboard;
        }
#endif

        app.input().set_battle_input( app.window(),
            !stage_result_active_ && !tutorial_completion_active_,
            allow_mouse_aim );
    }

    void StageModule::render_mouse_tether( const App &app,
        rendering::Renderer &renderer,
        float width,
        float height ) const
    {
        const auto aim = app.input().mouse_aim();

        if( !aim.active || !app.settings().mouse_controls_enabled ||
            ( teaching_tutorial() && !tutorial_.timeline().aim_visible() ) )
        {
            return;
        }

        std::int32_t window_width = 0;
        std::int32_t window_height = 0;

        if( !SDL_GetWindowSize( app.window(), &window_width, &window_height ) || window_width <= 0 ||
            window_height <= 0 )
        {
            return;
        }

        const float scale_x = width / static_cast<float>( window_width );
        const float scale_y = height / static_cast<float>( window_height );

        const auto offset = teaching_tutorial() ? tutorial_.timeline().centre_offset() : rendering::Vec2{};

        const auto center = stage::gameplay_center( { 0.0f, 0.0f, width, height }, offset );
        const float center_x = center.x;
        const float center_y = center.y;
        const float length = std::hypot( aim.x, aim.y );

        const auto circle = [ & ]( float x, float y, float radius, const rendering::Color &color )
        {
            const rendering::Rect rect{
                center_x + ( x - radius ) * scale_x,
                center_y + ( y - radius ) * scale_y,
                radius * 2.0f * scale_x,
                radius * 2.0f * scale_y
            };

            renderer.draw_filled_ellipse( rect, color );
        };

        if( length > tether_start_distance )
        {
            const float direction_x = aim.x / length;
            const float direction_y = aim.y / length;
            const float line_end = length - tether_ball_radius;

            for( float distance = tether_start_distance; distance < line_end; distance += tether_dot_spacing )
            {
                const float x = direction_x * distance;
                const float y = direction_y * distance;
                circle( x, y, tether_dot_radius + 1.0f, { 0.05f, 0.12f, 0.20f, 0.85f } );
                circle( x, y, tether_dot_radius, { 0.82f, 0.96f, 1.0f, 0.95f } );
            }
        }

        circle( aim.x, aim.y, tether_ball_radius + 2.0f, { 0.05f, 0.12f, 0.20f, 0.95f } );
        circle( aim.x, aim.y, tether_ball_radius, { 0.30f, 0.82f, 1.0f, 1.0f } );
        circle( aim.x - 2.0f, aim.y - 2.0f, 2.0f, { 0.90f, 0.98f, 1.0f, 1.0f } );
    }
}
