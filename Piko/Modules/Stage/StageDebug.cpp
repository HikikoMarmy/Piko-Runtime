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
    void StageModule::render_stage3d_material_debug( rendering::Renderer &renderer, float width, float height ) const
    {
        if( !stage3d_.material_debug_overlay_visible() )
        {
            return;
        }

        const std::vector<std::string> lines = stage3d_.material_debug_lines();

        if( lines.empty() )
        {
            return;
        }

        const float panel_w = 720.0f;
        const float line_h = 17.0f;
        const float panel_h = 24.0f + static_cast<float>( lines.size() ) * line_h;
        const float x = 24.0f;
        const float y = std::max( 120.0f, height - panel_h - 230.0f );

        renderer.draw_filled_rect( rendering::Rect{ x - 8.0f, y - 8.0f, panel_w, panel_h },
            rendering::Color{ 0.0f, 0.0f, 0.0f, 0.58f } );

        renderer.draw_filled_rect( rendering::Rect{ x - 10.0f, y - 10.0f, panel_w + 4.0f, 2.0f },
            rendering::Color{ 0.20f, 0.48f, 0.95f, 0.70f } );

        for( std::size_t i = 0; i < lines.size(); ++i )
        {
            const rendering::Color color =
                i == 0 ? rendering::Color{ 0.82f, 0.94f, 1.0f, 0.96f } : rendering::Color{ 0.72f, 0.82f, 0.94f, 0.88f };

            renderer.draw_text( x, y + static_cast<float>( i ) * line_h, lines[ i ].c_str(), color, 0.86f );
        }
    }

    void StageModule::render_stage3d_texture_debug( const App &app,
        rendering::Renderer &renderer,
        float width,
        float height ) const
    {
        if( !stage3d_.texture_debug_overlay_visible() )
        {
            return;
        }

        const managers::TextureHandle handle = stage3d_.texture_debug_handle();
        const rendering::Texture *texture = app.texture_manager().get( handle );
        const managers::TextureInfo *info = app.texture_manager().info( handle );

        if( texture == nullptr || info == nullptr )
        {
            return;
        }

        const float panel_x = std::max( 24.0f, width * 0.16f );
        const float panel_y = std::max( 72.0f, height * 0.10f );
        const float panel_w = std::min( width - panel_x * 2.0f, 920.0f );
        const float panel_h = std::min( height - panel_y * 2.0f, 760.0f );
        const float text_h = 92.0f;
        const float available_w = panel_w - 32.0f;
        const float available_h = panel_h - text_h - 32.0f;
        const float texture_aspect = static_cast<float>( texture->width ) / static_cast<float>( texture->height );

        float preview_w = available_w;
        float preview_h = preview_w / texture_aspect;

        if( preview_h > available_h )
        {
            preview_h = available_h;

            preview_w = preview_h * texture_aspect;
        }

        const float preview_x = panel_x + ( panel_w - preview_w ) * 0.5f;
        const float preview_y = panel_y + text_h;

        renderer.draw_filled_rect( rendering::Rect{ panel_x - 3.0f, panel_y - 3.0f, panel_w + 6.0f, panel_h + 6.0f },
            rendering::Color{ 0.12f, 0.42f, 0.76f, 0.94f } );

        renderer.draw_filled_rect( rendering::Rect{ panel_x, panel_y, panel_w, panel_h },
            rendering::Color{ 0.008f, 0.012f, 0.020f, 0.97f } );

        constexpr float checker_size = 20.0f;
        const std::int32_t checker_columns = static_cast<std::int32_t>( std::ceil( preview_w / checker_size ) );
        const std::int32_t checker_rows = static_cast<std::int32_t>( std::ceil( preview_h / checker_size ) );

        for( std::int32_t row = 0; row < checker_rows; ++row )
        {
            for( std::int32_t column = 0; column < checker_columns; ++column )
            {
                const float cell_x = preview_x + static_cast<float>( column ) * checker_size;
                const float cell_y = preview_y + static_cast<float>( row ) * checker_size;
                const float cell_w = std::min( checker_size, preview_x + preview_w - cell_x );
                const float cell_h = std::min( checker_size, preview_y + preview_h - cell_y );
                const float shade = ( ( row + column ) & 1 ) == 0 ? 0.20f : 0.36f;
                renderer.draw_filled_rect( rendering::Rect{ cell_x, cell_y, cell_w, cell_h },
                    rendering::Color{ shade, shade, shade, 1.0f } );
            }
        }

        switch( stage3d_.texture_debug_view_index() )
        {
            case 1:
                renderer.draw_texture_opaque( *texture, preview_x, preview_y, preview_w, preview_h );
                break;
            case 2:
                renderer.draw_texture_alpha_mask( *texture, preview_x, preview_y, preview_w, preview_h );
                break;
            default:
                renderer.draw_texture( *texture, preview_x, preview_y, preview_w, preview_h );
                break;
        }

        const std::size_t texture_number = stage3d_.texture_debug_index() + 1u;
        const std::size_t texture_count = stage3d_.texture_debug_count();
        const double pixel_count = static_cast<double>( std::max<std::uint64_t>( info->pixel_count, 1u ) );
        const double zero_percent = static_cast<double>( info->alpha_zero_pixels ) * 100.0 / pixel_count;
        const double partial_percent = static_cast<double>( info->alpha_partial_pixels ) * 100.0 / pixel_count;
        const double full_percent = static_cast<double>( info->alpha_full_pixels ) * 100.0 / pixel_count;

        char title[ 512 ]{};
        std::snprintf( title,
            sizeof( title ),
            "Texture %zu/%zu: %s | %s",
            texture_number,
            texture_count,
            stage3d_.texture_debug_name().c_str(),
            stage3d_.texture_debug_view_name() );

        char details[ 512 ]{};
        std::snprintf( details,
            sizeof( details ),
            "%dx%d | IMX modes %u/%u | alpha %u-%u | zero %.1f%% partial %.1f%% full %.1f%%",
            info->width,
            info->height,
            info->mode_value_1,
            info->mode_value_2,
            static_cast<unsigned>( info->alpha_min ),
            static_cast<unsigned>( info->alpha_max ),
            zero_percent,
            partial_percent,
            full_percent );

        renderer.draw_text( panel_x + 16.0f,
            panel_y + 14.0f,
            title,
            rendering::Color{ 0.82f, 0.94f, 1.0f, 1.0f },
            0.92f );

        renderer.draw_text( panel_x + 16.0f,
            panel_y + 36.0f,
            details,
            rendering::Color{ 0.72f, 0.84f, 0.94f, 1.0f },
            0.76f );

        renderer.draw_text( panel_x + 16.0f,
            panel_y + 58.0f,
            "Home close | PageUp/PageDown texture | End view",
            rendering::Color{ 0.98f, 0.82f, 0.42f, 1.0f },
            0.82f );
    }

    void StageModule::render_stage3d_camera_debug( rendering::Renderer &renderer, float width, float height ) const
    {
        if( !stage3d_.loaded() )
        {
            return;
        }

        const std::vector<std::string> lines = stage3d_.camera_debug_lines();

        if( lines.empty() )
        {
            return;
        }

        const float panel_w = std::min( 940.0f, std::max( 620.0f, width - 48.0f ) );
        const float line_h = 29.0f;
        const float panel_h = 28.0f + static_cast<float>( lines.size() ) * line_h;
        const float x = std::max( 24.0f, width - panel_w - 24.0f );
        const float y = 116.0f;

        renderer.draw_filled_rect( rendering::Rect{ x - 10.0f, y - 10.0f, panel_w + 20.0f, panel_h },
            rendering::Color{ 0.0f, 0.0f, 0.0f, 0.62f } );

        renderer.draw_filled_rect( rendering::Rect{ x - 10.0f, y - 10.0f, panel_w + 20.0f, 2.0f },
            rendering::Color{ 1.0f, 0.76f, 0.22f, 0.78f } );

        for( std::size_t i = 0; i < lines.size(); ++i )
        {
            const bool selected = lines[ i ].starts_with( "> " );
            const rendering::Color color = selected ? rendering::Color{ 1.0f, 0.90f, 0.38f, 1.0f }
                                                    : ( i == 0 ? rendering::Color{ 0.92f, 0.96f, 1.0f, 0.98f }
                                                               : rendering::Color{ 0.76f, 0.86f, 0.96f, 0.92f } );

            renderer.draw_text( x,
                y + static_cast<float>( i ) * line_h,
                lines[ i ].c_str(),
                color,
                selected ? 1.34f : ( i == 0 ? 1.30f : 1.20f ) );
        }
    }

    void StageModule::render_audio_channel_debug( App &app,
        rendering::Renderer &renderer,
        float width,
        float height ) const
    {
        const managers::AudioChannelMixDebug audio_debug = stage_audio_.audio_mix_debug( app.audio() );

        if( !audio_debug.valid )
        {
            return;
        }

        const std::int32_t visible_channel_count = std::clamp( audio_debug.source_channel_count, 0, 12 );
        const float panel_w = 268.0f;
        const float row_h = 18.0f;
        const float panel_h = 94.0f + static_cast<float>( std::max( visible_channel_count, 1 ) ) * row_h;
        const float panel_x = width - panel_w - 18.0f;
        const float panel_y = std::max( 112.0f, height * 0.5f - panel_h * 0.5f );

        renderer.draw_filled_rect( rendering::Rect{ panel_x - 2.0f, panel_y - 2.0f, panel_w + 4.0f, panel_h + 4.0f },
            rendering::Color{ 0.12f, 0.24f, 0.52f, 0.34f } );

        renderer.draw_filled_rect( rendering::Rect{ panel_x, panel_y, panel_w, panel_h },
            rendering::Color{ 0.0f, 0.0f, 0.0f, 0.58f } );

        renderer.draw_text( panel_x + 10.0f,
            panel_y + 9.0f,
            "AUDIO CHANNELS",
            rendering::Color{ 0.88f, 0.94f, 1.0f, 0.96f },
            1.15f );

        char stem_line[ 192 ]{};
        std::snprintf( stem_line,
            sizeof( stem_line ),
            "stem B%.2f P%.2f E%.2f F%.2f -> P%.2f fade %lld",
            audio_debug.current_gains.backing,
            audio_debug.current_gains.player,
            audio_debug.current_gains.enemy,
            audio_debug.current_gains.fx,
            audio_debug.target_gains.player,
            static_cast<long long>( audio_debug.fade_samples_remaining ) );

        renderer.draw_text( panel_x + 10.0f,
            panel_y + 28.0f,
            stem_line,
            rendering::Color{ 0.62f, 0.76f, 1.0f, 0.88f },
            0.82f );

        if( !audio_debug.subsong_name.empty() )
        {
            renderer.draw_text( panel_x + 10.0f,
                panel_y + 43.0f,
                audio_debug.subsong_name.c_str(),
                rendering::Color{ 0.68f, 0.76f, 0.88f, 0.78f },
                0.82f );
        }

        const bool mono_phrase_mask = audio_debug.phrase_stem_mode == managers::AudioPhraseStemMode::MonoCh2Center;
        const bool stereo_source_pair = audio_debug.phrase_layout_stereo_pair;

        char phrase_line[ 128 ]{};
        std::snprintf( phrase_line,
            sizeof( phrase_line ),
            "phrase mask %s | %s",
            mono_phrase_mask ? "01" : "05",
            stereo_source_pair ? "source 2/4 stereo" : ( mono_phrase_mask ? "mono ch2 center" : "stereo ch2/ch4" ) );

        renderer.draw_text( panel_x + 10.0f,
            panel_y + 58.0f,
            phrase_line,
            mono_phrase_mask && !stereo_source_pair ? rendering::Color{ 0.78f, 1.0f, 0.72f, 0.86f }
                                                    : rendering::Color{ 0.72f, 0.92f, 1.0f, 0.86f },
            0.82f );

        if( visible_channel_count <= 0 )
        {
            renderer.draw_text( panel_x + 10.0f,
                panel_y + 78.0f,
                "waiting for mixed audio chunk...",
                rendering::Color{ 0.76f, 0.78f, 0.84f, 0.78f },
                0.86f );

            return;
        }

        for( std::int32_t i = 0; i < visible_channel_count; ++i )
        {
            const managers::AudioSourceChannelDebug &source =
                audio_debug.source_channels[ static_cast<std::size_t>( i ) ];

            const bool actively_muted = source.gain <= 0.01f && source.raw_peak > 0.004f;
            const bool gain_muted = source.gain <= 0.01f;
            const bool silent = source.raw_peak <= 0.004f;
            const bool mirrored = source.mirrored_from_channel >= 0;

            char state[ 24 ]{};

            if( actively_muted )
            {
                std::snprintf( state, sizeof( state ), "MUTED" );
            }
            else if( mirrored )
            {
                std::snprintf( state, sizeof( state ), "mirr%02d", source.mirrored_from_channel );
            }
            else if( silent )
            {
                std::snprintf( state, sizeof( state ), "silent" );
            }
            else
            {
                std::snprintf( state, sizeof( state ), "live" );
            }

            char line[ 160 ]{};
            std::snprintf( line,
                sizeof( line ),
                "%02d %-4s g %.2f raw %3d out %3d %s",
                source.index,
                source.mirrored_from_channel == 2   ? "PL R"
                : source.mirrored_from_channel == 3 ? "EN R"
                                                    : stage_audio_source_channel_role( source.index,
                                                          audio_debug.source_channel_count,
                                                          audio_debug.phrase_stem_mode ),
                source.gain,
                peak_percent( source.raw_peak ),
                peak_percent( source.post_gain_peak ),
                state );

            const rendering::Color color =
                actively_muted
                    ? rendering::Color{ 1.0f, 0.34f, 0.26f, 0.96f }
                    : ( gain_muted ? rendering::Color{ 0.74f, 0.55f, 0.54f, 0.86f }
                                   : ( mirrored ? rendering::Color{ 0.70f, 1.0f, 0.78f, 0.92f }
                                                : ( silent ? rendering::Color{ 0.52f, 0.58f, 0.68f, 0.68f }
                                                           : rendering::Color{ 0.72f, 0.92f, 1.0f, 0.90f } ) ) );

            renderer.draw_text( panel_x + 10.0f,
                panel_y + 80.0f + static_cast<float>( i ) * row_h,
                line,
                color,
                0.82f );
        }
    }

    void StageModule::render_debug_footer( App &app, rendering::Renderer &renderer, float width, float height ) const
    {
        const managers::GameSettings &settings = app.settings();

        if( !settings.show_performance_overlay && !settings.show_stage_debug_overlay &&
            !settings.show_stage_audio_status )
        {
            return;
        }

        const managers::FrameStats &stats = app.frame_stats();
        const managers::FrameProfile &profile = stats.profile();

        if( settings.show_performance_overlay )
        {
            char fps_text[ 256 ]{};
            std::snprintf( fps_text,
                sizeof( fps_text ),
                "FPS %.1f | %.2f ms avg | %.2f ms now | evt %.2f upd %.2f aud %.2f rend %.2f pres %.2f",
                stats.average_fps(),
                stats.average_frame_ms(),
                stats.frame_ms(),
                profile.events_ms,
                profile.update_ms,
                profile.audio_ms,
                profile.render_ms,
                profile.present_ms );

            renderer.draw_filled_rect( rendering::Rect{ width - 760.0f, height - 54.0f, 740.0f, 22.0f },
                rendering::Color{ 0.0f, 0.0f, 0.0f, 0.42f } );

            renderer.draw_text( width - 750.0f,
                height - 49.0f,
                fps_text,
                rendering::Color{ 0.86f, 0.92f, 1.0f, 0.92f },
                1.0f );

            char draw_text[ 224 ]{};
            std::snprintf( draw_text,
                sizeof( draw_text ),
                "Draw calls C%llu T%llu | text %llu strings / %llu glyph pixels | verts %llu",
                static_cast<unsigned long long>( profile.colored_draw_calls ),
                static_cast<unsigned long long>( profile.textured_draw_calls ),
                static_cast<unsigned long long>( profile.text_draw_calls ),
                static_cast<unsigned long long>( profile.glyph_quads ),
                static_cast<unsigned long long>( profile.vertices ) );

            renderer.draw_filled_rect( rendering::Rect{ width - 760.0f, height - 30.0f, 740.0f, 22.0f },
                rendering::Color{ 0.0f, 0.0f, 0.0f, 0.42f } );

            renderer.draw_text( width - 750.0f,
                height - 25.0f,
                draw_text,
                rendering::Color{ 0.76f, 0.88f, 1.0f, 0.88f },
                1.0f );
        }

        if( !runtime_initialized_ || !runtime_.has_song() )
        {
            return;
        }

        if( settings.show_stage_debug_overlay )
        {
            const runtime::stage::StageRuntimeState &state = runtime_.state();
            const runtime::stage::StageTransitionPlan &plan = runtime_.transition_plan();

            char debug_text[ 512 ]{};
            std::snprintf( debug_text,
                sizeof( debug_text ),
                "Sec %zu | sample %lld/%lld | charts %zu | zoom %.1fx | present %.2f/%.2f | trim %.1f | cap %s | F1-F4 present | F7 reset | F8 next | F9/F10 trim | F11 reset trim | F12 cap | next %s%s%s",
                state.section_index,
                static_cast<long long>( state.section_sample ),
                static_cast<long long>( runtime_.current_section_duration_samples() ),
                state.active_charts.size(),
                debug_zoom_multiplier_,
                debug_presentation_scale_x_,
                debug_presentation_scale_y_,
                debug_phrase_tube_visual_trim_pixels_,
                debug_phrase_cap_consumes_body_ ? "consume" : "append",
                plan.committed ? stage::transition_text( plan.committed_transition ).c_str()
                               : stage::transition_text( plan.predicted_transition ).c_str(),
                runtime_.paused() ? " | PAUSED" : "",
                state.song_finished ? " | FINISHED" : "" );

            renderer.draw_filled_rect( rendering::Rect{ 18.0f, height - 30.0f, 1180.0f, 22.0f },
                rendering::Color{ 0.0f, 0.0f, 0.0f, 0.36f } );

            renderer.draw_text( 28.0f,
                height - 25.0f,
                debug_text,
                rendering::Color{ 0.72f, 0.78f, 0.88f, 0.82f },
                1.0f );

            const runtime::stage::StageJudgementDebugState &judgement = runtime_.judgement_debug_state();

            if( judgement.enabled )
            {
                const char guard_char = judgement.next_guard_button == 0   ? 'S'
                                        : judgement.next_guard_button == 1 ? 'X'
                                        : judgement.next_guard_button == 2 ? 'O'
                                        : judgement.next_guard_button == 3 ? 'T'
                                                                           : '-';

                const long long delta_ms = static_cast<long long>(
                    std::llround( static_cast<double>( judgement.last_timing_delta_samples ) * 1000.0 / 48000.0 ) );

                char delta_text[ 32 ]{};
                std::snprintf( delta_text, sizeof( delta_text ), "%s%lldms", delta_ms > 0 ? "+" : "", delta_ms );

                char judgement_text[ 640 ]{};
                std::snprintf( judgement_text,
                    sizeof( judgement_text ),
                    "Judge: %s %s %s @%lld | sec %zu samp %lld | notes P%zu/%zu G%zu/%zu | phrase #%d %lld-%lld %s%s%s | guard #%d %c @%lld | aim %.2f %.2f str %.2f align %.2f",
                    judgement.last_event.empty() ? "none" : judgement.last_event.c_str(),
                    judgement.last_event_kind.empty() ? "" : judgement.last_event_kind.c_str(),
                    judgement.last_event_kind.empty() ? "" : delta_text,
                    static_cast<long long>( judgement.last_event_sample ),
                    judgement.section_index,
                    static_cast<long long>( judgement.section_sample ),
                    judgement.pending_phrase_count,
                    judgement.phrase_note_count,
                    judgement.pending_guard_count,
                    judgement.guard_note_count,
                    judgement.active_phrase_index,
                    static_cast<long long>( judgement.active_phrase_start_sample ),
                    static_cast<long long>( judgement.active_phrase_end_sample ),
                    judgement.active_phrase_started ? "active" : "",
                    judgement.active_phrase_completed ? " done" : "",
                    judgement.active_phrase_missed ? " miss" : "",
                    judgement.next_guard_index,
                    guard_char,
                    static_cast<long long>( judgement.next_guard_sample ),
                    judgement.aim_x,
                    judgement.aim_y,
                    judgement.aim_strength,
                    judgement.aim_alignment );

                renderer.draw_filled_rect( rendering::Rect{ 18.0f, height - 78.0f, 1180.0f, 20.0f },
                    rendering::Color{ 0.0f, 0.0f, 0.0f, 0.30f } );

                renderer.draw_text( 28.0f,
                    height - 74.0f,
                    judgement_text,
                    rendering::Color{ 0.88f, 0.86f, 0.64f, 0.88f },
                    1.0f );

                if( !judgement.phrase_tail_status.empty() )
                {
                    renderer.draw_filled_rect( rendering::Rect{ 18.0f, height - 132.0f, width - 36.0f, 34.0f },
                        rendering::Color{ 0.0f, 0.0f, 0.0f, 0.42f } );

                    renderer.draw_text( 28.0f,
                        height - 124.0f,
                        judgement.phrase_tail_status.c_str(),
                        rendering::Color{ 0.70f, 0.94f, 0.70f, 0.96f },
                        1.25f );
                }
            }
        }

        if( ( settings.show_stage_audio_status || settings.show_stage_debug_overlay ) &&
            !stage_audio_.status_line().empty() )
        {
            renderer.draw_filled_rect( rendering::Rect{ 18.0f, height - 54.0f, 760.0f, 20.0f },
                rendering::Color{ 0.0f, 0.0f, 0.0f, 0.30f } );

            renderer.draw_text( 28.0f,
                height - 50.0f,
                stage_audio_.status_line().c_str(),
                rendering::Color{ 0.76f, 0.88f, 1.0f, 0.82f },
                1.0f );
        }

        if( settings.show_stage_audio_status || settings.show_stage_debug_overlay )
        {
            render_audio_channel_debug( app, renderer, width, height );
        }
    }

    std::string StageModule::preview_timing_text() const
    {
        if( !preview_timing_valid_ || stage_result_active_ )
        {
            return {};
        }

        char text[ 512 ]{};
        std::snprintf( text,
            sizeof( text ),
            "Stage %02d | %s | %s | SSQ %.0f fps | global_frame=%.9g | section_index=%zu | section_frame=%.9g | tempo=%.9g | portable_animation_frame=%.9g",
            load_data_.stage.stage_number,
            load_data_.stage.portable_visuals ? "PSP" : "PS2",
            gamedata::difficulty_from_chc_path( load_data_.stage.chc_path ).c_str(),
            stage3d_ssq_frames_per_second,
            preview_timing_.global_frame,
            preview_timing_.section_index,
            preview_timing_.section_frame,
            preview_timing_.tempo,
            preview_timing_.portable_animation_frame );

        return text;
    }

    void StageModule::copy_preview_timing()
    {
        const std::string text = preview_timing_text();

        if( text.empty() )
        {
            return;
        }

        if( SDL_SetClipboardText( text.c_str() ) )
        {
            debug_message_ = "Preview timing copied - paste it into chat.";
        }
        else
        {
            debug_message_ = "Could not copy preview timing: " + std::string( SDL_GetError() );
        }
    }

    void StageModule::render_preview_timing( rendering::Renderer &renderer, float width, float height ) const
    {
        if( !show_preview_timing_ || !preview_timing_valid_ || stage_result_active_ )
        {
            return;
        }

        const float x = 18.0f;
        const float y = height - 132.0f;
        const float scale = std::min( 1.5f, ( width - 56.0f ) / 450.0f );
        renderer.draw_filled_rect( rendering::Rect{ x, y, std::min( 680.0f, width - 36.0f ), 70.0f },
            rendering::Color{ 0.0f, 0.0f, 0.0f, 0.78f } );

        const rendering::Color color{ 1.0f, 0.94f, 0.66f, 1.0f };
        char text[ 256 ]{};
        std::snprintf( text,
            sizeof( text ),
            "PREVIEW / Stage %02d %s / %s / SSQ %.0f fps%s",
            load_data_.stage.stage_number,
            load_data_.stage.portable_visuals ? "PSP" : "PS2",
            gamedata::difficulty_from_chc_path( load_data_.stage.chc_path ).c_str(),
            stage3d_ssq_frames_per_second,
            runtime_.paused() ? " / PAUSED" : "" );

        renderer.draw_text( x + 10.0f, y + 9.0f, text, color, scale );
        std::snprintf( text,
            sizeof( text ),
            "Global %.3f / Section %zu (index %zu) / Local %.3f",
            preview_timing_.global_frame,
            preview_timing_.section_index + 1u,
            preview_timing_.section_index,
            preview_timing_.section_frame );

        renderer.draw_text( x + 10.0f, y + 29.0f, text, color, scale );
        std::snprintf( text,
            sizeof( text ),
            "Tempo %.3f / Animation %.3f / Ctrl F8 copy",
            preview_timing_.tempo,
            preview_timing_.portable_animation_frame );

        renderer.draw_text( x + 10.0f, y + 49.0f, text, color, scale );
    }

    void StageModule::render_imgui_debug( App &app, rendering::Renderer &renderer )
    {
#if PIKO_STAGE_HAS_IMGUI
        managers::GameSettings &settings = app.settings();

        auto apply_sampling = [ & ]()
        {
            rendering::TextureSamplingState sampling{};
            sampling.mode = renderer_filter_mode_from_settings( settings.texture_filtering_mode );

            sampling.anisotropy = settings.texture_anisotropy;
            renderer.set_texture_sampling( sampling );
        };

        const auto lighting_combo_getter = []( void *data, std::int32_t index ) -> const char *
        {
            auto *stage = static_cast<Stage3DRenderer *>( data );
            return stage != nullptr ? stage->lighting_backend_display_name( index ) : "";
        };

        if( !ImGui::BeginMainMenuBar() )
        {
            return;
        }

        if( ImGui::BeginMenu( "Piko" ) )
        {
            ImGui::TextDisabled( "Debug UI is mouse/keyboard only." );
            ImGui::TextDisabled( "Gamepad navigation is disabled so stage input stays clean." );
            ImGui::Separator();

            if( ImGui::MenuItem( "Reset runtime options" ) )
            {
                settings.reset_defaults();
                renderer.set_msaa_enabled( settings.msaa_enabled );
                apply_sampling();

                if( stage3d_.loaded() )
                {
                    stage3d_.reset_scene_lighting();
                }
            }

            ImGui::EndMenu();
        }

        if( ImGui::BeginMenu( "Render" ) )
        {
            if( ImGui::MenuItem( "MSAA enabled", nullptr, settings.msaa_enabled ) )
            {
                settings.msaa_enabled = !settings.msaa_enabled;
                renderer.set_msaa_enabled( settings.msaa_enabled );
            }

            ImGui::SetNextItemWidth( 150.0f );
            ImGui::SliderInt( "MSAA samples", &settings.msaa_samples, 0, 8 );
            ImGui::TextDisabled( "Sample count applies after restarting the GL window." );

            static constexpr const char *filter_names[] = {
                "@ui.nearest",
                "@ui.linear",
                "Linear + mipmaps",
                "@ui.anisotropic"
            };

            std::int32_t filter_index = utils::to_int( settings.texture_filtering_mode );
            ImGui::SetNextItemWidth( 180.0f );

            if( ImGui::Combo( "Texture filtering", &filter_index, filter_names, 4 ) )
            {
                settings.texture_filtering_mode = utils::to_enum<managers::TextureFilteringMode>( filter_index );
                apply_sampling();
            }

            ImGui::SetNextItemWidth( 150.0f );

            if( ImGui::SliderFloat( localization::text( "@ui.anisotropy" ),
                    &settings.texture_anisotropy,
                    1.0f,
                    16.0f,
                    "%.1fx" ) )
            {
                apply_sampling();
            }

            ImGui::Separator();
            std::int32_t bloom = utils::to_int( settings.bloom_preset );
            ImGui::SetNextItemWidth( 150.0f );

            if( ImGui::Combo( "Bloom", &bloom, "Off\0Low\0Medium\0High\0" ) )
            {
                settings.bloom_preset = utils::to_enum<managers::BloomPreset>( bloom );
            }

            ImGui::Separator();
            ImGui::TextUnformatted( "CRT overlay" );
            ImGui::Checkbox( "CRT overlay enabled", &settings.crt_filter_enabled );
            ImGui::SetNextItemWidth( 150.0f );
            ImGui::SliderFloat( "Scanlines", &settings.crt_scanline_strength, 0.0f, 0.65f, "%.2f" );
            ImGui::SetNextItemWidth( 150.0f );
            ImGui::SliderFloat( "Vignette", &settings.crt_vignette_strength, 0.0f, 0.75f, "%.2f" );
            ImGui::SetNextItemWidth( 150.0f );
            ImGui::SliderFloat( "Noise", &settings.crt_noise_strength, 0.0f, 0.25f, "%.3f" );
            ImGui::EndMenu();
        }

        if( ImGui::BeginMenu( "Stage 3D" ) )
        {
            if( !stage3d_.loaded() )
            {
                ImGui::TextDisabled( "No 3D stage is loaded." );
            }
            else
            {
                std::int32_t backend = stage3d_.lighting_backend_index();
                ImGui::SetNextItemWidth( 190.0f );

                if( ImGui::Combo( "Lighting mode",
                        &backend,
                        lighting_combo_getter,
                        &stage3d_,
                        stage3d_.lighting_backend_display_count() ) )
                {
                    stage3d_.set_lighting_backend_index( backend );
                }

                float light = stage3d_.scene_lighting_strength();
                ImGui::SetNextItemWidth( 150.0f );

                if( ImGui::SliderFloat( "Scene light", &light, 0.0f, 1.0f, "%.2f" ) )
                {
                    stage3d_.set_scene_lighting_strength( light );
                }

                float floor = stage3d_.scene_lighting_floor();
                ImGui::SetNextItemWidth( 150.0f );

                if( ImGui::SliderFloat( "Light floor", &floor, 0.0f, 1.0f, "%.2f" ) )
                {
                    stage3d_.set_scene_lighting_floor( floor );
                }

                float gmlt = stage3d_.gmlt_light_strength();
                ImGui::SetNextItemWidth( 150.0f );

                if( ImGui::SliderFloat( "GMLT strength", &gmlt, 0.0f, 2.5f, "%.2f" ) )
                {
                    stage3d_.set_gmlt_light_strength( gmlt );
                }

                float wrap = stage3d_.ps2_light_wrap();
                ImGui::SetNextItemWidth( 150.0f );

                if( ImGui::SliderFloat( "PS2 light wrap", &wrap, 0.0f, 0.85f, "%.2f" ) )
                {
                    stage3d_.set_ps2_light_wrap( wrap );
                }

                float near_plane = stage3d_.minimum_projection_near();
                ImGui::SetNextItemWidth( 150.0f );

                if( ImGui::SliderFloat( "Near clamp", &near_plane, 0.5f, 120.0f, "%.1f" ) )
                {
                    stage3d_.set_minimum_projection_near( near_plane );
                }

                bool sky = stage3d_.stage_sky_enabled();

                if( ImGui::Checkbox( "Stage sky", &sky ) )
                {
                    stage3d_.set_stage_sky_enabled( sky );
                }

                if( stage3d_.lighting_backend_index() == 2 )
                {
                    ImGui::Separator();
                    ImGui::TextUnformatted( "Cel shade" );
                    float threshold = stage3d_.cel_shadow_threshold();
                    ImGui::SetNextItemWidth( 150.0f );

                    if( ImGui::SliderFloat( "Shadow threshold", &threshold, 0.05f, 0.95f, "%.2f" ) )
                    {
                        stage3d_.set_cel_shadow_threshold( threshold );
                    }

                    float shadow = stage3d_.cel_shadow_strength();
                    ImGui::SetNextItemWidth( 150.0f );

                    if( ImGui::SliderFloat( "Shadow tone", &shadow, 0.15f, 0.95f, "%.2f" ) )
                    {
                        stage3d_.set_cel_shadow_strength( shadow );
                    }

                    float edge = stage3d_.cel_edge_strength();
                    ImGui::SetNextItemWidth( 150.0f );

                    if( ImGui::SliderFloat( "Soft edge darken", &edge, 0.0f, 0.75f, "%.2f" ) )
                    {
                        stage3d_.set_cel_edge_strength( edge );
                    }

                    ImGui::TextDisabled( "Two-tone lighting; outline pass can come later." );
                }

                ImGui::Separator();
                ImGui::TextUnformatted( "Debug actions" );

                if( ImGui::Button( "Reset lighting defaults" ) )
                {
                    stage3d_.reset_scene_lighting();
                }

                ImGui::SameLine();

                if( ImGui::Button( "Camera pullback" ) )
                {
                    stage3d_.begin_debug_camera_pullback( 3.0f );
                }

                if( ImGui::Button( "Cycle material mode" ) )
                {
                    stage3d_.cycle_material_debug_mode();
                }

                ImGui::SameLine();

                if( ImGui::Button( "Next material" ) )
                {
                    stage3d_.cycle_material_debug_sample();
                }

                if( ImGui::Button( "Cycle GMLT source" ) )
                {
                    stage3d_.cycle_gmlt_debug_mode();
                }

                ImGui::SameLine();

                if( ImGui::Button( "Cycle GMLT direction" ) )
                {
                    stage3d_.cycle_gmlt_direction_mode();
                }
            }

            ImGui::EndMenu();
        }

        if( ImGui::BeginMenu( "Actions" ) )
        {
            if( !stage3d_.loaded() )
            {
                ImGui::TextDisabled( "No 3D stage is loaded." );
            }
            else
            {
                bool override_enabled = stage3d_.action_event_flag_override_enabled();
                std::uint32_t override_flags = stage3d_.action_event_flag_override();

                if( ImGui::Checkbox( "Override SSQ event flags", &override_enabled ) )
                {
                    stage3d_.set_action_event_flag_override( override_enabled, override_flags );
                }

                ImGui::TextDisabled(
                    "Auto flags are Piko's current best guess. Override is for discovering the official bit meanings." );

                for( std::int32_t bit = 0; bit < 12; ++bit )
                {
                    bool enabled = ( override_flags & ( 1u << bit ) ) != 0;

                    if( ImGui::Checkbox( stage3d_.action_event_flag_name( bit ), &enabled ) )
                    {
                        if( enabled )
                        {
                            override_flags |= 1u << bit;
                        }
                        else
                        {
                            override_flags &= ~( 1u << bit );
                        }

                        stage3d_.set_action_event_flag_override( override_enabled, override_flags );
                    }
                }

                if( ImGui::Button( "Clear override flags" ) )
                {
                    override_flags = 0;
                    stage3d_.set_action_event_flag_override( override_enabled, override_flags );
                }

                ImGui::SameLine();

                if( ImGui::Button( "Reset action states" ) )
                {
                    stage3d_.reset_action_animation_state();
                }

                ImGui::Separator();
                const std::vector<std::string> action_lines = stage3d_.action_debug_lines();

                for( const std::string &line : action_lines )
                {
                    ImGui::TextUnformatted( line.c_str() );
                }
            }

            ImGui::EndMenu();
        }

        if( ImGui::BeginMenu( "Gameplay" ) )
        {
            if( runtime_.duet_stage() )
            {
                static constexpr const char *duet_side_names[] = {
                    "Auto (Primary)",
                    "Primary",
                    "Partner",
                    "Both",
                    "Combine gaps"
                };

                std::int32_t duet_side = static_cast<std::int32_t>( runtime_.duet_side() );
                ImGui::SetNextItemWidth( 170.0f );

                if( ImGui::Combo( "Duet side", &duet_side, duet_side_names, 5 ) )
                {
                    runtime_.set_duet_side( utils::to_enum<runtime::stage::StageDuetSide>( duet_side ) );
                }

                ImGui::TextDisabled( "Filters trace, phrase, guard, and judgement data." );
                ImGui::Separator();
            }

            ImGui::Checkbox( "Accept D-pad guard notes", &settings.guard_notes_accept_dpad );
            ImGui::Checkbox( "Phrase tube wobble", &settings.phrase_tube_wobble );
            ImGui::Checkbox( "Held note pulse", &settings.phrase_tube_gradient_pulse );
            ImGui::Checkbox( "BPM edge glow", &settings.stage_bpm_edge_glow );

            ImGui::Separator();
            ImGui::TextUnformatted( "View cone" );
            constexpr ImGuiColorEditFlags view_cone_color_flags =
                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;

            ImGui::ColorEdit4( "On-centre line colour",
                settings.view_cone_on_target_line_color.data(),
                view_cone_color_flags );

            ImGui::ColorEdit4( "Off-centre line colour",
                settings.view_cone_off_target_line_color.data(),
                view_cone_color_flags );

            ImGui::ColorEdit4( "On-centre fill colour",
                settings.view_cone_on_target_fill_color.data(),
                view_cone_color_flags );

            ImGui::ColorEdit4( "Off-centre fill colour",
                settings.view_cone_off_target_fill_color.data(),
                view_cone_color_flags );

            ImGui::Separator();
            ImGui::SetNextItemWidth( 150.0f );
            ImGui::SliderFloat( "Trace spacing X",
                &debug_presentation_scale_x_,
                stage::gameplay_presentation_scale_debug_min,
                stage::gameplay_presentation_scale_debug_max,
                "%.2f" );

            ImGui::SetNextItemWidth( 150.0f );
            ImGui::SliderFloat( "Trace spacing Y",
                &debug_presentation_scale_y_,
                stage::gameplay_presentation_scale_debug_min,
                stage::gameplay_presentation_scale_debug_max,
                "%.2f" );

            ImGui::SetNextItemWidth( 150.0f );
            ImGui::SliderFloat( "Tube cap trim",
                &debug_phrase_tube_visual_trim_pixels_,
                stage::phrase_tube_visual_trim_debug_min,
                stage::phrase_tube_visual_trim_debug_max,
                "%.1f" );

            ImGui::EndMenu();
        }

        if( ImGui::BeginMenu( "Overlays" ) )
        {
            ImGui::Checkbox( "Performance overlay", &settings.show_performance_overlay );
            ImGui::Checkbox( "Stage debug overlay", &settings.show_stage_debug_overlay );
            ImGui::Checkbox( "Judgement detail", &settings.show_judgement_detail );
            ImGui::Checkbox( "Raw energy values", &settings.show_raw_energy_values );
            ImGui::Checkbox( "Stage audio status", &settings.show_stage_audio_status );
            ImGui::EndMenu();
        }

        if( ImGui::BeginMenu( "Previews" ) )
        {
            ImGui::MenuItem( "Show preview timing", "Ctrl+F7", &show_preview_timing_ );
            const std::string timing = preview_timing_text();

            if( ImGui::MenuItem( "Copy preview timing", "Ctrl+F8", false, !timing.empty() ) )
            {
                copy_preview_timing();
            }

            if( ImGui::MenuItem( runtime_.paused() ? "Resume stage" : "Pause stage",
                    nullptr,
                    false,
                    !stage_result_active_ ) )
            {
                runtime_.toggle_paused();
                stage_audio_.set_paused( app.audio(), runtime_.paused() );
            }

            ImGui::TextDisabled( "Section numbers start at 1; copied indices start at 0." );
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
#else
        (void)app;
        (void)renderer;
#endif
    }
}
