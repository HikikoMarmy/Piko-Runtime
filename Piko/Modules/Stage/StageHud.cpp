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
    std::uint32_t StageModule::clamp_score_for_display( std::uint32_t score )
    {
        return std::min<std::uint32_t>( score, 9'999'999u );
    }

    void StageModule::update_energy_warning( double delta_seconds )
    {
        const auto &sides = runtime_.sides();
        const double elapsed_ticks = !runtime_.paused() && !stage_result_active_ && std::isfinite( delta_seconds )
                                         ? std::max( 0.0, delta_seconds ) * 60.0
                                         : 0.0;

        for( std::size_t index = 0; index < sides.size(); ++index )
        {
            // US EnergyGauge_UpdatePendingDelta tests the displayed gauge against 0.1.
            const bool warning = sides[ index ].active && sides[ index ].energy_ratio() < 0.1f;

            if( !warning || !energy_warning_active_[ index ] )
            {
                energy_warning_tick_phase_[ index ] = 0.0;
            }

            energy_warning_active_[ index ] = warning;

            if( warning )
            {
                energy_warning_tick_phase_[ index ] =
                    std::fmod( energy_warning_tick_phase_[ index ] + elapsed_ticks, 20.0 );
            }
        }
    }

    float StageModule::energy_warning_alpha( std::size_t side_index ) const
    {
        if( side_index >= energy_warning_active_.size() || !energy_warning_active_[ side_index ] )
        {
            return 0.0f;
        }

        return runtime::stage::retail_energy_warning_alpha(
            static_cast<std::int64_t>( std::floor( energy_warning_tick_phase_[ side_index ] + 0.000001 ) ) );
    }

    void StageModule::render_side_hud( const App &app,
        rendering::Renderer &renderer,
        const runtime::stage::StageSideState &side,
        const rendering::Rect &rect,
        bool align_right,
        float ui_scale ) const
    {
        if( !side.active )
        {
            return;
        }

        const bool enemy = side.role == runtime::stage::StageSideRole::Enemy;
        const rendering::Color frame_color =
            enemy ? rendering::Color{ 0.56f, 0.18f, 0.20f, 0.74f } : rendering::Color{ 0.18f, 0.30f, 0.56f, 0.74f };

        renderer.draw_filled_rect(
            rendering::Rect{
                rect.x - 2.0f * ui_scale,
                rect.y - 2.0f * ui_scale,
                rect.w + 4.0f * ui_scale,
                rect.h + 4.0f * ui_scale
            },
            frame_color );

        renderer.draw_filled_rect( rect, rendering::Color{ 0.02f, 0.025f, 0.045f, 0.82f } );

        const float pad = 10.0f * ui_scale;
        const float bar_x = rect.x + pad;
        const float label_x = align_right ? rect.x + rect.w - 102.0f * ui_scale : bar_x;
        const float bar_w = rect.w - pad * 2.0f;

        renderer.draw_text( label_x,
            rect.y + 8.0f * ui_scale,
            side.label.c_str(),
            rendering::Color{ 0.94f, 0.96f, 1.0f, 0.94f },
            ui_scale );

        renderer.draw_filled_rect( rendering::Rect{ bar_x, rect.y + 30.0f * ui_scale, bar_w, 10.0f * ui_scale },
            rendering::Color{ 0.04f, 0.05f, 0.08f, 0.96f } );

        renderer.draw_filled_rect(
            rendering::Rect{
                bar_x + ui_scale,
                rect.y + 31.0f * ui_scale,
                ( bar_w - 2.0f * ui_scale ) * side.health_ratio(),
                8.0f * ui_scale
            },
            enemy ? rendering::Color{ 0.95f, 0.30f, 0.30f, 0.96f } : rendering::Color{ 0.35f, 0.86f, 0.50f, 0.96f } );

        renderer.draw_filled_rect( rendering::Rect{ bar_x, rect.y + 46.0f * ui_scale, bar_w, 8.0f * ui_scale },
            rendering::Color{ 0.04f, 0.05f, 0.08f, 0.96f } );

        renderer.draw_filled_rect(
            rendering::Rect{
                bar_x + ui_scale,
                rect.y + 47.0f * ui_scale,
                ( bar_w - 2.0f * ui_scale ) * side.energy_ratio(),
                6.0f * ui_scale
            },
            enemy ? rendering::Color{ 0.98f, 0.58f, 0.25f, 0.96f } : rendering::Color{ 0.34f, 0.62f, 0.98f, 0.96f } );

        const float warning_alpha =
            side.slot >= 0 ? energy_warning_alpha( static_cast<std::size_t>( side.slot ) ) : 0.0f;

        if( warning_alpha > 0.0f )
        {
            const rendering::Color warning_color{ 1.0f, 0.40f, 0.70f, warning_alpha * 0.70f };
            renderer.draw_filled_rect( rendering::Rect{ bar_x, rect.y + 30.0f * ui_scale, bar_w, 10.0f * ui_scale },
                warning_color );

            renderer.draw_filled_rect( rendering::Rect{ bar_x, rect.y + 46.0f * ui_scale, bar_w, 8.0f * ui_scale },
                warning_color );
        }

        const managers::GameSettings &settings = app.settings();

        if( settings.show_raw_energy_values || settings.show_stage_debug_overlay )
        {
            char raw_energy_text[ 128 ]{};
            std::snprintf( raw_energy_text,
                sizeof( raw_energy_text ),
                "E %.2f | L %.2f | M %.2f | P %+.2f",
                side.energy,
                side.logical_energy,
                side.max_energy,
                side.pending_energy_delta );

            renderer.draw_text( bar_x,
                rect.y + 58.0f * ui_scale,
                raw_energy_text,
                rendering::Color{ 0.76f, 0.86f, 1.0f, 0.90f },
                1.20f * ui_scale );
        }
    }

    std::string StageModule::modifier_text( const managers::GameSettings &settings )
    {
        std::string text;

        const auto append = [ &text ]( const std::string &marker )
        {
            if( !text.empty() )
            {
                text += "  ";
            }

            text += marker;
        };

        if( settings.mirror_mode )
        {
            append( "MM" );
        }

        if( settings.no_death_mode )
        {
            append( "ND" );
        }

        if( settings.only_aim_mode )
        {
            append( "OA" );
        }

        if( settings.all_sections_mode )
        {
            append( "AS" );
        }

        if( settings.player_damage_multiplier != 1.0f )
        {
            const auto percent = std::lround( settings.player_damage_multiplier * 100.0f );
            append( "PD " + std::to_string( percent ) + "%" );
        }

        if( settings.enemy_damage_multiplier != 1.0f )
        {
            const auto percent = std::lround( settings.enemy_damage_multiplier * 100.0f );
            append( "ED " + std::to_string( percent ) + "%" );
        }

        return text;
    }

    void StageModule::render_modifier_hud( const App &app,
        rendering::Renderer &renderer,
        float width,
        float height ) const
    {
        const auto *texture = app.texture_manager().get( modifier_text_texture_ );

        if( texture == nullptr || !texture->valid() )
        {
            return;
        }

        const auto layout = rendering::make_logical_viewport( static_cast<std::int32_t>( width ),
            static_cast<std::int32_t>( height ),
            stage::gameplay_logical_width,
            stage::gameplay_logical_height );

        const float scale = layout.scale / modifier_raster_scale;
        renderer.draw_texture( *texture,
            layout.to_x( modifier_text_x ),
            layout.to_y( modifier_text_y ),
            static_cast<float>( texture->width ) * scale,
            static_cast<float>( texture->height ) * scale );
    }

    void StageModule::render_score_hud( const App &app, rendering::Renderer &renderer, float width, float height ) const
    {
        char score_text[ 8 ]{};
        std::snprintf( score_text, sizeof( score_text ), "%07u", clamp_score_for_display( runtime_.score() ) );

        const rendering::LogicalViewport layout = rendering::make_logical_viewport( static_cast<std::int32_t>( width ),
            static_cast<std::int32_t>( height ),
            stage::gameplay_logical_width,
            stage::gameplay_logical_height );

        const float x = layout.to_x( 34.0f );
        const float y = layout.to_y( stage::gameplay_logical_height - 61.0f );
        const float ui_scale = layout.scale;
        constexpr float digit_w = 30.0f;
        constexpr float digit_h = 38.0f;
        constexpr float digit_advance = 34.0f;
        constexpr float shadow_offset = 4.0f;

        const rendering::Texture *digit_texture = app.texture_manager().get( generated_score_digit_texture_ );

        if( digit_texture == nullptr || !digit_texture->valid() )
        {

            constexpr float fallback_scale = 5.0f;
            renderer.draw_text( x + shadow_offset * ui_scale,
                y + shadow_offset * ui_scale,
                score_text,
                rendering::Color{ 0.0f, 0.035f, 0.12f, 0.82f },
                fallback_scale * ui_scale );

            renderer.draw_text( x,
                y,
                score_text,
                rendering::Color{ 0.05f, 0.43f, 1.0f, 0.98f },
                fallback_scale * ui_scale );

            return;
        }

        const auto draw_digits = [ & ]( float draw_x, float draw_y, const rendering::Color &color )
        {
            for( std::int32_t i = 0; i < 7; ++i )
            {
                const std::int32_t digit = score_text[ i ] - '0';

                if( digit < 0 || digit > 9 )
                {
                    continue;
                }

                renderer.draw_texture_region( *digit_texture,
                    static_cast<float>( digit * stage::score_digit_texture_digit_width ),
                    0.0f,
                    static_cast<float>( stage::score_digit_texture_digit_width ),
                    static_cast<float>( stage::score_digit_texture_digit_height ),
                    draw_x + static_cast<float>( i ) * digit_advance * ui_scale,
                    draw_y,
                    digit_w * ui_scale,
                    digit_h * ui_scale,
                    color );
            }
        };

        draw_digits( x + shadow_offset * ui_scale,
            y + shadow_offset * ui_scale,
            rendering::Color{ 0.0f, 0.035f, 0.12f, 0.82f } );

        draw_digits( x, y, rendering::Color{ 0.05f, 0.43f, 1.0f, 0.98f } );
    }

    void StageModule::render_retail_hud( App &app, rendering::Renderer &renderer, float width, float height ) const
    {
        const runtime::stage::StageHudModel model = runtime::stage::build_stage_hud_model( runtime_ );
        const rendering::Texture *atlas = app.texture_manager().get( itfc_texture_ );
        const float scale = std::min( width / 640.0f, height / 448.0f );
        const float origin_x = ( width - 640.0f * scale ) * 0.5f;
        const float origin_y = ( height - 448.0f * scale ) * 0.5f;

        const auto logical_rect = [ origin_x, origin_y, scale ]( float x, float y, float w, float h )
        {
            return rendering::Rect{ origin_x + x * scale, origin_y + y * scale, w * scale, h * scale };
        };

        if( !tutorial_session() )
        {
            if( atlas != nullptr && atlas->valid() )
            {
                const auto draw_atlas = [ & ]( const rendering::Rect &source,
                                            const rendering::Rect &target,
                                            const rendering::Color &color = rendering::Color{ 1.0f, 1.0f, 1.0f, 1.0f } )
                {
                    renderer.draw_texture_region( *atlas,
                        source.x,
                        source.y,
                        source.w,
                        source.h,
                        target.x,
                        target.y,
                        target.w,
                        target.h,
                        color );
                };

                const auto draw_logical_atlas =
                    [ & ]( const rendering::Rect &source,
                        float x,
                        float y,
                        float w,
                        float h,
                        const rendering::Color &color = rendering::Color{ 1.0f, 1.0f, 1.0f, 1.0f } )
                {
                    draw_atlas( source, logical_rect( x, y, w, h ), color );
                };

                char score_text[ 8 ]{};
                std::snprintf( score_text, sizeof( score_text ), "%07u", clamp_score_for_display( model.score ) );

                for( std::int32_t index = 0; index < 7; ++index )
                {
                    const std::int32_t digit = score_text[ index ] - '0';
                    draw_logical_atlas( runtime::stage::retail_score_digit_source( digit ),
                        14.0f + static_cast<float>( index ) * 21.0f,
                        411.0f,
                        21.0f,
                        22.0f );
                }

                if( model.sides[ 0 ].active )
                {
                    draw_logical_atlas( runtime::stage::gameplay_interface_atlas::player_icon,
                        23.0f,
                        44.0f,
                        72.0f,
                        50.0f );

                    const auto &name = load_data_.stage.portable_visuals
                                           ? runtime::stage::gameplay_interface_atlas::portable_player_name
                                           : runtime::stage::gameplay_interface_atlas::player_name;

                    draw_logical_atlas( name, 97.0f, 47.0f, name.w, name.h );
                }

                if( model.sides[ 1 ].active )
                {
                    auto name = runtime::stage::gameplay_interface_atlas::opponent_name;

                    if( load_data_.stage.portable_visuals )
                    {
                        name = load_data_.stage.stage_number == 11
                                   ? runtime::stage::gameplay_interface_atlas::portable_kai_kakoon_name
                                   : runtime::stage::gameplay_interface_atlas::portable_ming_ming_name;
                    }

                    draw_logical_atlas( name, 314.0f, 47.0f, name.w, name.h );

                    draw_logical_atlas( runtime::stage::gameplay_interface_atlas::opponent_icon,
                        543.0f,
                        44.0f,
                        72.0f,
                        50.0f );
                }

                draw_logical_atlas( runtime::stage::gameplay_interface_atlas::energy_housing_left_cap,
                    11.0f,
                    8.0f,
                    33.0f,
                    40.0f );

                draw_logical_atlas( runtime::stage::gameplay_interface_atlas::energy_housing_middle,
                    44.0f,
                    8.0f,
                    200.0f,
                    40.0f );

                draw_logical_atlas( runtime::stage::gameplay_interface_atlas::energy_housing_right_cap,
                    244.0f,
                    8.0f,
                    33.0f,
                    40.0f );

                draw_logical_atlas( runtime::stage::gameplay_interface_atlas::energy_housing_left_cap,
                    359.0f,
                    8.0f,
                    33.0f,
                    40.0f );

                draw_logical_atlas( runtime::stage::gameplay_interface_atlas::energy_housing_middle,
                    392.0f,
                    8.0f,
                    200.0f,
                    40.0f );

                draw_logical_atlas( runtime::stage::gameplay_interface_atlas::energy_housing_right_cap,
                    592.0f,
                    8.0f,
                    33.0f,
                    40.0f );

                const float left_ratio = model.sides[ 0 ].active ? model.sides[ 0 ].energy_ratio : 0.0f;
                const float right_ratio = model.sides[ 1 ].active ? model.sides[ 1 ].energy_ratio : 0.0f;

                const auto draw_energy_fill = [ & ]( float x, float ratio )
                {
                    const float fill_width = 205.0f * std::clamp( ratio, 0.0f, 1.0f );

                    if( fill_width <= 0.0f )
                    {
                        return;
                    }

                    if( fill_width <= 4.0f )
                    {
                        draw_logical_atlas( runtime::stage::gameplay_interface_atlas::energy_fill_middle,
                            x,
                            16.0f,
                            fill_width,
                            17.0f );

                        return;
                    }

                    draw_logical_atlas( runtime::stage::gameplay_interface_atlas::energy_fill_left_cap,
                        x,
                        16.0f,
                        2.0f,
                        17.0f );

                    draw_logical_atlas( runtime::stage::gameplay_interface_atlas::energy_fill_middle,
                        x + 2.0f,
                        16.0f,
                        fill_width - 4.0f,
                        17.0f );

                    draw_logical_atlas( runtime::stage::gameplay_interface_atlas::energy_fill_right_cap,
                        x + fill_width - 2.0f,
                        16.0f,
                        2.0f,
                        17.0f );
                };

                draw_energy_fill( 247.0f - 205.0f * left_ratio, left_ratio );
                draw_energy_fill( 390.0f, right_ratio );

                draw_logical_atlas( runtime::stage::gameplay_interface_atlas::energy_marker,
                    230.0f - 205.0f * left_ratio,
                    -25.0f,
                    32.0f,
                    56.0f );

                draw_logical_atlas( runtime::stage::gameplay_interface_atlas::energy_marker,
                    374.0f + 205.0f * right_ratio,
                    -25.0f,
                    32.0f,
                    56.0f );

                const auto flash_alpha = []( std::int32_t ticks )
                {
                    if( ticks <= 0 )
                    {
                        return 0.0f;
                    }

                    const std::int32_t phase = ( 10 - std::clamp( ticks, 1, 10 ) ) % 5;
                    return 0.2f * static_cast<float>( phase + 1 );
                };

                if( model.sides[ 0 ].pending_gain_ticks > 0 )
                {
                    const float alpha = flash_alpha( model.sides[ 0 ].pending_gain_ticks );
                    draw_logical_atlas( runtime::stage::gameplay_interface_atlas::energy_flash,
                        -10.0f,
                        -3.0f,
                        59.0f,
                        61.0f,
                        rendering::Color{ 1.0f, 1.0f, 1.0f, alpha } );
                }

                if( model.sides[ 0 ].pending_loss_ticks > 0 )
                {
                    const float alpha = flash_alpha( model.sides[ 0 ].pending_loss_ticks );
                    draw_logical_atlas( runtime::stage::gameplay_interface_atlas::energy_flash,
                        245.0f,
                        -3.0f,
                        59.0f,
                        61.0f,
                        rendering::Color{ 1.0f, 1.0f, 1.0f, alpha } );
                }

                if( model.sides[ 1 ].pending_gain_ticks > 0 )
                {
                    const float alpha = flash_alpha( model.sides[ 1 ].pending_gain_ticks );
                    draw_logical_atlas( runtime::stage::gameplay_interface_atlas::energy_flash,
                        595.0f,
                        -3.0f,
                        59.0f,
                        61.0f,
                        rendering::Color{ 1.0f, 1.0f, 1.0f, alpha } );
                }

                if( model.sides[ 1 ].pending_loss_ticks > 0 )
                {
                    const float alpha = flash_alpha( model.sides[ 1 ].pending_loss_ticks );
                    draw_logical_atlas( runtime::stage::gameplay_interface_atlas::energy_flash,
                        340.0f,
                        -3.0f,
                        59.0f,
                        61.0f,
                        rendering::Color{ 1.0f, 1.0f, 1.0f, alpha } );
                }

                const auto draw_energy_warning = [ & ]( std::size_t side_index, float center_x )
                {
                    const float alpha = energy_warning_alpha( side_index );

                    if( alpha <= 0.0f || !model.sides[ side_index ].active )
                    {
                        return;
                    }

                    // CreateWarningPulse uses a 1.7x sprite centred at (150/500, 24).
                    // sub_1AAC50 scales its height and alpha together; GS blend is additive.
                    const auto &source = runtime::stage::gameplay_interface_atlas::energy_warning;
                    renderer.draw_texture_region_rotated_additive( *atlas,
                        source.x,
                        source.y,
                        source.w,
                        source.h,
                        origin_x + center_x * scale,
                        origin_y + 24.0f * scale,
                        source.w * 1.7f * scale,
                        source.h * 1.7f * alpha * scale,
                        0.5f,
                        0.5f,
                        0.0f,
                        rendering::Color{ 1.0f, 1.0f, 1.0f, alpha } );
                };

                draw_energy_warning( 0u, 150.0f );
                draw_energy_warning( 1u, 500.0f );

                if( load_data_.stage.portable_visuals )
                {
                    draw_logical_atlas( runtime::stage::gameplay_interface_atlas::portable_phase_background,
                        510.0f,
                        356.0f,
                        116.0f,
                        84.0f );

                    draw_logical_atlas( runtime::stage::gameplay_interface_atlas::portable_phase_puma,
                        550.0f,
                        351.0f,
                        68.0f,
                        66.0f );
                }
                else
                {
                    draw_logical_atlas( runtime::stage::gameplay_interface_atlas::phase_background,
                        510.0f,
                        356.0f,
                        116.0f,
                        84.0f );

                    draw_logical_atlas( runtime::stage::gameplay_interface_atlas::phase_puma,
                        550.0f,
                        351.0f,
                        68.0f,
                        62.0f );
                }

                float exit_progress = std::clamp( hud_phase_change_seconds_ / phase_exit_seconds_, 0.0f, 1.0f );

                if( hud_outgoing_phase_ != runtime::stage::StageActionMode::Unknown && exit_progress < 1.0f )
                {
                    const auto source = runtime::stage::retail_phase_source( hud_outgoing_phase_ );
                    const float y = 408.5f + phase_slide_distance_ * exit_progress;
                    draw_atlas( source,
                        logical_rect( 509.5f, y, 121.0f, 27.0f ),
                        rendering::Color{ 1.0f, 1.0f, 1.0f, 1.0f - exit_progress } );
                }

                const float enter_delay =
                    hud_outgoing_phase_ == runtime::stage::StageActionMode::Unknown ? 0.0f : phase_exit_seconds_;

                const float enter_seconds = hud_phase_change_seconds_ - enter_delay;

                if( enter_seconds >= 0.0f )
                {
                    // Bring the phase in.
                    const auto animation = runtime::stage::retail_phase_animation( enter_seconds );

                    auto source = runtime::stage::retail_phase_source( model.phase );
                    const float phase_w = 121.0f * animation.width_scale;
                    const float phase_h = 27.0f * std::abs( animation.height_scale );

                    if( animation.height_scale < 0.0f )
                    {
                        source.y += source.h;

                        source.h = -source.h;
                    }

                    draw_atlas( source,
                        logical_rect( 570.0f - phase_w * 0.5f,
                            422.0f + animation.offset_y - phase_h * 0.5f,
                            phase_w,
                            phase_h ),
                        rendering::Color{ 1.0f, 1.0f, 1.0f, animation.alpha } );
                }
            }
            else
            {
                render_score_hud( app, renderer, width, height );

                if( model.sides[ 0 ].active )
                {
                    render_side_hud( app,
                        renderer,
                        runtime_.sides()[ 0 ],
                        logical_rect( 24.0f, 24.0f, 300.0f, 82.0f ),
                        false,
                        scale );
                }

                if( model.sides[ 1 ].active )
                {
                    render_side_hud( app,
                        renderer,
                        runtime_.sides()[ 1 ],
                        logical_rect( 316.0f, 24.0f, 300.0f, 82.0f ),
                        true,
                        scale );
                }
            }
        }

        if( model.judgement == runtime::stage::StageHudJudgement::None || model.judgement_age_samples < 0 )
        {
            return;
        }

        const runtime::stage::StageHudJudgementAnimation judgement_animation =
            runtime::stage::retail_judgement_animation( model.judgement_age_samples, stage::retail_update_samples );

        if( !judgement_animation.visible )
        {
            return;
        }

        rendering::Rect judgement_source{};

        switch( model.judgement )
        {
            case runtime::stage::StageHudJudgement::Miss:
                judgement_source = runtime::stage::gameplay_interface_atlas::judgement_miss;
                break;
            case runtime::stage::StageHudJudgement::Ok:
                judgement_source = runtime::stage::gameplay_interface_atlas::judgement_ok;
                break;
            case runtime::stage::StageHudJudgement::Good:
                judgement_source = runtime::stage::gameplay_interface_atlas::judgement_good;
                break;
            case runtime::stage::StageHudJudgement::Great:
                judgement_source = runtime::stage::gameplay_interface_atlas::judgement_great;
                break;
            case runtime::stage::StageHudJudgement::None:
                return;
        }

        const float center_x = origin_x + 320.0f * scale;
        const float center_y = origin_y + 140.0f * scale;
        const rendering::Color judgement_color{ 1.0f, 1.0f, 1.0f, judgement_animation.alpha };

        if( atlas != nullptr && atlas->valid() )
        {
            const float draw_width = judgement_source.w * judgement_animation.width_scale * scale;
            const float draw_height = judgement_source.h * judgement_animation.height_scale * scale;
            renderer.draw_texture_region( *atlas,
                judgement_source.x,
                judgement_source.y,
                judgement_source.w,
                judgement_source.h,
                center_x - draw_width * 0.5f,
                center_y - draw_height * 0.5f,
                draw_width,
                draw_height,
                judgement_color );
        }
    }

    void StageModule::render_phase_announcement( App &app, rendering::Renderer &renderer ) const
    {
        if( stage_result_active_ )
        {
            return;
        }

        const auto announcement = runtime::stage::phase_announcement_animation( phase_announcement_.seconds );
        const std::int32_t texture_index =
            runtime::stage::phase_announcement_texture_index( phase_announcement_.phase );

        if( !announcement.visible || texture_index < 0 )
        {
            return;
        }

        const auto *texture = app.texture_manager().get( phase_announcement_textures_[ texture_index ] );

        if( texture == nullptr || !texture->valid() )
        {
            return;
        }

        const auto layout =
            rendering::make_logical_viewport( renderer, stage::gameplay_logical_width, stage::gameplay_logical_height );

        const float center_x = phase_announcement_center_x_ + announcement.offset_x;

        const auto target = layout.rect( center_x - phase_announcement_width_ * 0.5f,
            phase_announcement_center_y_ - phase_announcement_height_ * 0.5f,
            phase_announcement_width_,
            phase_announcement_height_ );

        renderer.draw_texture( *texture,
            target.x,
            target.y,
            target.w,
            target.h,
            rendering::Color{ 1.0f, 1.0f, 1.0f, std::clamp( announcement.alpha, 0.0f, 1.0f ) } );
    }

    void StageModule::render_fullscreen_hud( App &app,
        rendering::Renderer &renderer,
        float width,
        float height,
        float pulse ) const
    {
        (void)pulse;

        if( tutorial_session() )
        {
            render_retail_hud( app, renderer, width, height );
            render_stage3d_texture_debug( app, renderer, width, height );
#if defined( _DEBUG )
            render_debug_footer( app, renderer, width, height );
#endif
            return;
        }

        render_retail_hud( app, renderer, width, height );
        render_modifier_hud( app, renderer, width, height );

        const runtime::stage::StageSideArray &sides = runtime_.sides();
        const rendering::LogicalViewport layout =
            rendering::make_logical_viewport( renderer, stage::gameplay_logical_width, stage::gameplay_logical_height );

        if( sides[ 2 ].active )
        {
            render_side_hud( app,
                renderer,
                sides[ 2 ],
                layout.rect( 24.0f, 78.0f, 260.0f, 72.0f ),
                false,
                layout.scale );
        }

        if( sides[ 3 ].active )
        {
            render_side_hud( app,
                renderer,
                sides[ 3 ],
                layout.rect( 356.0f, 78.0f, 260.0f, 72.0f ),
                true,
                layout.scale );
        }

        render_phase_announcement( app, renderer );

        render_stage3d_texture_debug( app, renderer, width, height );
#if defined( _DEBUG )
        render_debug_footer( app, renderer, width, height );
#endif
    }

    void StageModule::render_bpm_edge_glow( const App &app,
        rendering::Renderer &renderer,
        float width,
        float height ) const
    {
        const managers::GameSettings &settings = app.settings();

        if( !settings.stage_bpm_edge_glow || !runtime_initialized_ || !runtime_.has_song() )
        {
            return;
        }

        float bpm = 120.0f;
        const runtime::stage::StageRuntimeState &state = runtime_.state();

        for( const runtime::stage::ActiveChart &active_chart : state.active_charts )
        {
            if( active_chart.chart == nullptr )
            {
                continue;
            }

            const float estimated = stage::estimate_chart_visual_bpm( *active_chart.chart );

            if( std::isfinite( estimated ) && estimated >= 60.0f && estimated <= 240.0f )
            {
                bpm = estimated;
                break;
            }
        }

        const float beats = static_cast<float>( elapsed_seconds_ ) * bpm / 60.0f;
        const float beat_phase = beats - std::floor( beats );
        const float beat_flash = std::pow( 1.0f - beat_phase, 3.0f );
        const float alpha = 0.035f + beat_flash * 0.135f;
        const float edge = std::clamp( std::min( width, height ) * 0.105f, 54.0f, 120.0f );

        const rendering::Color glow{ 0.34f, 0.72f, 1.0f, alpha };
        const rendering::Color soft{ 0.18f, 0.32f, 0.72f, alpha * 0.42f };

        renderer.draw_filled_rect( rendering::Rect{ 0.0f, 0.0f, width, edge * 0.34f }, glow );
        renderer.draw_filled_rect( rendering::Rect{ 0.0f, height - edge * 0.34f, width, edge * 0.34f }, glow );
        renderer.draw_filled_rect( rendering::Rect{ 0.0f, 0.0f, edge * 0.34f, height }, glow );
        renderer.draw_filled_rect( rendering::Rect{ width - edge * 0.34f, 0.0f, edge * 0.34f, height }, glow );

        renderer.draw_filled_rect( rendering::Rect{ 0.0f, edge * 0.34f, width, edge * 0.66f }, soft );
        renderer.draw_filled_rect( rendering::Rect{ 0.0f, height - edge, width, edge * 0.66f }, soft );
        renderer.draw_filled_rect( rendering::Rect{ edge * 0.34f, 0.0f, edge * 0.66f, height }, soft );
        renderer.draw_filled_rect( rendering::Rect{ width - edge, 0.0f, edge * 0.66f, height }, soft );
    }

    void StageModule::render_stage_result( App &app, rendering::Renderer &renderer, float width, float height )
    {
        if( !stage_result_active_ )
        {
            return;
        }

        if( !stage_result_won_ && stage_result_elapsed_frame_ < lose_pre_duration_frames_ )
        {
            return;
        }

        const managers::TextureHandle result_handle = stage_result_won_ ? you_win_texture_ : you_lose_texture_;

        const rendering::Texture *texture = app.texture_manager().get( result_handle );
        const bool have_texture = texture != nullptr && texture->valid();

        const float scale = std::min( width / 640.0f, height / 448.0f );
        const float origin_x = ( width - 640.0f * scale ) * 0.5f;
        const float origin_y = ( height - 448.0f * scale ) * 0.5f;
        const float timer =
            std::max( 0.0f, stage_result_elapsed_frame_ - ( stage_result_won_ ? 0.0f : lose_pre_duration_frames_ ) );

        const float art_alpha = std::clamp( timer / 10.0f, 0.0f, 1.0f );
        const float fade_alpha = std::clamp( timer / 120.0f, 0.0f, 1.0f );
        const float logical_y = stage_result_won_ ? 448.0f - 4.4f * timer : 448.0f - 2.2f * timer;

        if( stage_result_won_ )
        {
            renderer.draw_filled_rect( rendering::Rect{ 0.0f, 0.0f, width, height },
                rendering::Color{ 0.0f, 0.0f, 0.0f, fade_alpha } );
        }
        else
        {
            TitleScreenBackdrop::draw( renderer,
                result_background_,
                renderer.output_width(),
                renderer.output_height(),
                timer / 30.0f,
                app.texture_manager().get( "options/clean-circle" ) );
        }

        if( !stage_result_won_ )
        {
            const auto *again = app.texture_manager().get( again_texture_ );
            const bool have_again = again != nullptr && again->valid();
            const rendering::Color tint{ 1.0f, 1.0f, 1.0f, art_alpha };

            const auto draw_region = [ & ]( const rendering::Rect &source, const rendering::Rect &bounds )
            {
                renderer.draw_texture_region( *again,
                    source.x + 0.5f,
                    source.y + 0.5f,
                    source.w - 1.0f,
                    source.h - 1.0f,
                    origin_x + bounds.x * scale,
                    origin_y + bounds.y * scale,
                    bounds.w * scale,
                    bounds.h * scale,
                    tint );
            };

            if( have_again )
            {
                draw_region( { 0.0f, 1.0f, 398.0f, 47.0f }, { 127.0f, 92.0f, 398.0f, 47.0f } );
            }

            for( std::size_t index = 0; index < retry_button_bounds_.size(); ++index )
            {
                const bool selected = ( index == 0 ) == lose_retry_selected_;
                const auto &bounds = retry_button_bounds_[ index ];

                if( have_again )
                {
                    draw_region( retry_button_sources_[ selected ? 0u : 1u ], bounds );
                }
                else
                {
                    renderer.draw_filled_ellipse(
                        { origin_x + bounds.x * scale, origin_y + bounds.y * scale, bounds.w * scale, bounds.h * scale },
                        selected ? rendering::Color{ 0.28f, 0.60f, 0.95f, art_alpha }
                                 : rendering::Color{ 0.08f, 0.18f, 0.32f, art_alpha } );
                }

                localization::draw_label( renderer,
                    app.texture_manager().get( retry_button_labels_[ index ] ),
                    { origin_x + ( bounds.x + 10.0f ) * scale,
                        origin_y + ( bounds.y + 42.0f ) * scale,
                        ( bounds.w - 20.0f ) * scale,
                        32.0f * scale },
                    tint );
            }

            if( have_again )
            {
                return;
            }
        }

        if( have_texture )
        {
            const auto source = stage_result_won_ ? rendering::Rect{ 42.0f, 0.0f, 435.0f, 63.0f }
                                                  : rendering::Rect{ 7.0f, 83.0f, 504.0f, 64.0f };
            renderer.draw_texture_region( *texture,
                source.x,
                source.y,
                source.w,
                source.h,
                origin_x + ( 640.0f - source.w ) * 0.5f * scale,
                origin_y + logical_y * scale,
                source.w * scale,
                source.h * scale,
                rendering::Color{ 1.0f, 1.0f, 1.0f, art_alpha } );
        }
    }
}
