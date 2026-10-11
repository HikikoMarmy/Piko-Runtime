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
    std::uint32_t StageModule::retail_note_flash_random( std::uint32_t &state )
    {
        // sub_1425E8
        state = state * 1103515245u + 12345u;
        return state & 0x7fffffffu;
    }

    StageModule::RetailNoteFlashSample StageModule::retail_note_flash_sample( std::int64_t retail_tick, bool portable )
    {
        // sub_1425E8
        const std::uint64_t unsigned_tick = static_cast<std::uint64_t>( std::max<std::int64_t>( 0, retail_tick ) );

        if( portable )
        {
            // PSP sub_892C3F8 scales one 64-pixel sprite by [1,4], rotates it
            // through a full turn, and leaves its alpha at 1. sub_8939588 uses
            // this 64-bit LCG. Seed by tick so pause/redraw do not change it.
            std::uint64_t random_state = unsigned_tick ^ 0x49544643u;

            const auto next_unit = [ &random_state ]()
            {
                random_state = random_state * 0x5851f42d4c957f2dULL + 1u;
                return static_cast<float>( ( random_state >> 32u ) & 0x7fffffffu ) / 2147483648.0f;
            };

            const float scale = 1.0f + next_unit() * 3.0f;
            const float angle = next_unit() * 360.0f;
            return RetailNoteFlashSample{ .size = 64.0f * scale, .angle_degrees = angle, .alpha = 1.0f };
        }

        std::uint32_t random_state = static_cast<std::uint32_t>( unsigned_tick ) ^
                                     static_cast<std::uint32_t>( unsigned_tick >> 32u ) ^ 0x49544643u;

        const std::uint32_t size_random = retail_note_flash_random( random_state );
        const std::uint32_t angle_random = retail_note_flash_random( random_state );
        const std::uint32_t alpha_random = retail_note_flash_random( random_state );

        constexpr float radians_to_degrees_local = 57.29577951308232f;

        return RetailNoteFlashSample{
            .size = static_cast<float>( size_random % 280u + 20u ),
            .angle_degrees = static_cast<float>( angle_random % 300000u ) * 0.00001f * radians_to_degrees_local,
            // sub_115820
            .alpha = static_cast<float>( alpha_random % 900u + 100u ) * 0.1f
        };
    }

    float StageModule::ssq_action_angle_from_aim( float aim_x, float aim_y )
    {
        return -std::atan2( aim_y, -aim_x );
    }

    Stage3DRenderer::ActionAnimationInput StageModule::make_actor_animation_input(
        const runtime::stage::StageActorActionState &actor,
        std::uint32_t model_type,
        bool duet_stage )
    {
        Stage3DRenderer::ActionAnimationInput input{};

        if( actor.has_aim )
        {
            input.has_aim_angle = true;

            input.aim_angle_radians = ssq_action_angle_from_aim( actor.aim_x, actor.aim_y );
        }

        if( actor.phrase_active )
        {
            // Keep Kirah aiming while the note is held.
            const bool directional_hold =
                duet_stage && model_type == duet_partner_model_type && actor.has_aim && !actor.phrase_started;

            if( actor.phrase_started )
            {
                input.event_flags = ssq_event_press_start;

                input.descriptor = actor.action_descriptor;
            }
            else if( directional_hold )
            {
                input.event_flags = ssq_event_aim_or_pending;
            }
            else
            {
                input.event_flags = ssq_event_hold;

                input.descriptor = actor.action_descriptor;
            }

            input.action_down = true;

            input.successful_strum_result_serial = actor.action_serial;

            input.effect_hold_owner_model_type = model_type;

            input.effect_hold_owner_model_mask = 1u << ( model_type - 1u );
        }
        else if( actor.chart_active && actor.has_aim )
        {
            input.event_flags = ssq_event_aim_or_pending;
        }

        input.fixed_sprite_active_model_mask = input.effect_hold_owner_model_mask;
        return input;
    }

    void StageModule::render_trace_runtime( rendering::Renderer &renderer,
        const rendering::Texture *itfc_texture,
        const rendering::Texture *tube_texture,
        const rendering::Texture *trace_texture,
        const rendering::Texture *arrow_texture,
        const rendering::Texture *wing_texture,
        const rendering::Texture *center_nub_texture,
        const rendering::Texture *pressed_nub_texture,
        const std::array<const rendering::Texture *, 4> &custom_guard_icons,
        const rendering::Rect &area,
        const managers::GameSettings &settings,
        float pulse )
    {
        (void)pulse;

        const managers::GameplayVisualStyle &visual_style = settings.gameplay_visual;

        const auto difficulty =
            managers::aim_difficulty( gamedata::difficulty_from_chc_path( load_data_.stage.chc_path ) );

        const float snap_half_angle = settings.snap_half_angle_for( difficulty, load_data_.stage.chart_style );

        if( !runtime_initialized_ || !runtime_.has_song() )
        {
            renderer.draw_text( area.x + 18.0f,
                area.y + 62.0f,
                "No runtime song loaded.",
                rendering::Color{ 1.0f, 0.48f, 0.42f, 0.95f },
                1.0f );

            return;
        }

        const float logical_view_scale = std::max( 0.001f,
            std::min( area.w / stage::gameplay_logical_width, area.h / stage::gameplay_logical_height ) );

        const float aim_visual_scale = logical_view_scale * stage::aim_marker_visual_scale;

        const auto tutorial_offset = teaching_tutorial() ? tutorial_.timeline().centre_offset() : rendering::Vec2{};

        const auto center = stage::gameplay_center( area, tutorial_offset );
        const float logical_center_x = center.x;
        const float logical_center_y = center.y;

        const runtime::stage::StageGameplayView gameplay_view{
            .center_x = logical_center_x,
            .center_y = logical_center_y,
            .zoom = logical_view_scale * debug_zoom_multiplier_,
            .note_speed = visual_style.note_speed,
            .scale_x = stage::gameplay_trace_scale_x,
            .scale_y = stage::gameplay_trace_scale_y,
            .presentation_center_x = logical_center_x,
            .presentation_center_y = logical_center_y,
            .presentation_scale_x = debug_presentation_scale_x_,
            .presentation_scale_y = debug_presentation_scale_y_
        };

        struct RadarCandidate
        {
            bool valid = false;
            bool has_visible_trace_line = false;
            bool holding_phrase = false;
            runtime::stage::TracePosition current_position{};
            runtime::stage::TracePosition tangent_position{};
            std::int32_t priority = 0;
        };

        RadarCandidate radar_candidate{};

        const auto consider_radar_candidate = [ & ]( const runtime::stage::ActiveChart &active_chart,
                                                  const std::vector<runtime::stage::TraceSample> &samples,
                                                  std::int64_t chart_sample,
                                                  const runtime::stage::TracePosition &current_position,
                                                  bool has_visible_trace_line )
        {
            if( samples.size() < 2 )
            {
                return;
            }

            const std::int64_t first_sample = static_cast<std::int64_t>( samples.front().chart_sample );
            const std::int64_t last_sample = static_cast<std::int64_t>( samples.back().chart_sample );

            const bool primary = active_chart.source_player_index == 0;

            std::int32_t priority = 0;
            runtime::stage::TracePosition tangent_position = current_position;

            if( chart_sample >= first_sample && chart_sample <= last_sample )
            {
                priority = primary ? 400 : 300;

                if( has_visible_trace_line )
                {
                    priority += 4000;
                }
            }
            else if( has_visible_trace_line && chart_sample < first_sample &&
                     first_sample - chart_sample <= stage::trace_lookahead_samples )
            {
                priority = primary ? 2000 : 1000;

                tangent_position = runtime::stage::trace_position_at( samples, first_sample );
            }
            else
            {
                return;
            }

            if( radar_candidate.valid && radar_candidate.priority >= priority )
            {
                return;
            }

            radar_candidate.valid = true;

            radar_candidate.has_visible_trace_line = has_visible_trace_line;

            radar_candidate.holding_phrase = runtime_.holding_phrase_for_chart( active_chart );

            radar_candidate.current_position = current_position;

            radar_candidate.tangent_position = tangent_position;

            radar_candidate.priority = priority;
        };

        const auto chart_has_guard_hit_window = []( const runtime::stage::ActiveChart &active_chart ) -> bool
        {
            if( active_chart.chart == nullptr || active_chart.chart->guards.empty() )
            {
                return false;
            }

            constexpr std::int64_t guard_hit_window_samples = stage::sample_rate * 115 / 1000;

            for( const filetypes::chc::GuardFragment &guard : active_chart.chart->guards )
            {
                const std::int64_t time_to_hit =
                    static_cast<std::int64_t>( guard.start_sample ) - active_chart.chart_sample;

                if( std::llabs( time_to_hit ) <= guard_hit_window_samples )
                {
                    return true;
                }
            }

            return false;
        };

        const runtime::stage::StageRuntimeState &state = runtime_.state();
        const std::vector<runtime::stage::StagePhraseVisualState> phrase_visual_states =
            runtime_.phrase_visual_states();

        const std::vector<runtime::stage::StagePhraseHitVisualState> phrase_hit_visual_states =
            runtime_.phrase_hit_visual_states();

        const std::vector<runtime::stage::StageGuardHitVisualState> guard_hit_visual_states =
            runtime_.guard_hit_visual_states();

        struct ChartDrawOptions
        {
            float alpha_scale = 1.0f;
            bool allow_radar = false;
            bool preview = false;
            bool draw_trace = true;
            bool draw_phrases = true;
            bool draw_guards = true;
        };

        struct GameplayModeVisualState
        {
            bool guard_mode = false;
            bool attack_content_before_guard = false;
            bool guard_visible_or_bridged = false;
        };

        GameplayModeVisualState gameplay_mode_visual{};
        bool guard_section_rails_drawn = false;

        const auto build_gameplay_mode_visual =
            []( const std::vector<runtime::stage::ActiveChart> &current_charts,
                const std::vector<runtime::stage::ActiveChart> &preview_charts ) -> GameplayModeVisualState
        {
            GameplayModeVisualState mode{};
            constexpr std::int64_t guard_rail_bridge_tail_samples = stage::sample_rate * 9 / 2;
            constexpr std::int64_t attack_reenable_before_trace_samples = stage::sample_rate / 4;
            constexpr std::int64_t infinity_samples = 0x3fffffffffffffffLL;
            std::int64_t earliest_guard_target = infinity_samples;
            std::int64_t next_guard_target = infinity_samples;
            bool visible_guard = false;
            bool recent_guard = false;
            bool next_guard_for_bridge = false;

            const auto scan_charts = [ & ]( const std::vector<runtime::stage::ActiveChart> &charts )
            {
                for( const runtime::stage::ActiveChart &active_chart : charts )
                {
                    if( active_chart.chart == nullptr )
                    {
                        continue;
                    }

                    for( const filetypes::chc::GuardFragment &guard : active_chart.chart->guards )
                    {
                        const std::int64_t time_to_hit =
                            static_cast<std::int64_t>( guard.start_sample ) - active_chart.chart_sample;

                        if( time_to_hit >= -stage::guard_visual_post_hit_samples &&
                            time_to_hit <= stage::trace_lookahead_samples )
                        {
                            visible_guard = true;

                            earliest_guard_target = std::min( earliest_guard_target, time_to_hit );
                        }

                        if( time_to_hit < 0 && time_to_hit >= -guard_rail_bridge_tail_samples )
                        {
                            recent_guard = true;
                        }

                        if( time_to_hit > 0 && time_to_hit <= stage::trace_lookahead_samples )
                        {
                            next_guard_for_bridge = true;

                            next_guard_target = std::min( next_guard_target, time_to_hit );
                        }
                    }
                }
            };

            scan_charts( current_charts );
            scan_charts( preview_charts );
            const bool bridged_guard = recent_guard && next_guard_for_bridge;
            mode.guard_visible_or_bridged = visible_guard || bridged_guard;

            if( !mode.guard_visible_or_bridged )
            {
                return mode;
            }

            const std::int64_t guard_compare_target =
                earliest_guard_target != infinity_samples ? earliest_guard_target : next_guard_target;

            const auto scan_attack_content = [ & ]( const std::vector<runtime::stage::ActiveChart> &charts )
            {
                for( const runtime::stage::ActiveChart &active_chart : charts )
                {
                    if( active_chart.chart == nullptr )
                    {
                        continue;
                    }

                    if( active_chart.trace_samples != nullptr && active_chart.trace_samples->size() >= 2 )
                    {
                        const std::int64_t trace_start =
                            active_chart.trace_samples->front().chart_sample - active_chart.chart_sample;

                        const std::int64_t trace_end =
                            active_chart.trace_samples->back().chart_sample - active_chart.chart_sample;

                        if( trace_end > 0 && trace_start <= stage::trace_lookahead_samples &&
                            trace_start <= guard_compare_target )
                        {
                            mode.attack_content_before_guard = true;
                            return;
                        }
                    }

                    for( const filetypes::chc::PhraseFragment &phrase : active_chart.chart->phrases )
                    {
                        const std::int64_t phrase_start =
                            static_cast<std::int64_t>( phrase.start_sample ) - active_chart.chart_sample;

                        const std::int64_t phrase_end =
                            phrase_start + static_cast<std::int64_t>( phrase.duration_samples );

                        if( phrase_end > 0 && phrase_start <= stage::trace_lookahead_samples &&
                            phrase_start <= guard_compare_target )
                        {
                            mode.attack_content_before_guard = true;
                            return;
                        }
                    }
                }
            };

            scan_attack_content( current_charts );

            if( !mode.attack_content_before_guard )
            {
                scan_attack_content( preview_charts );
            }

            if( bridged_guard && mode.attack_content_before_guard )
            {
                mode.guard_mode = false;
            }
            else if( mode.attack_content_before_guard && guard_compare_target <= attack_reenable_before_trace_samples )
            {
                mode.guard_mode = false;
            }
            else
            {
                mode.guard_mode = !mode.attack_content_before_guard;
            }

            return mode;
        };

        const auto draw_chart_list = [ & ]( const std::vector<runtime::stage::ActiveChart> &draw_charts,
                                         const ChartDrawOptions &options ) -> bool
        {
            bool drew_anything = false;

            for( const runtime::stage::ActiveChart &active_chart : draw_charts )
            {
                const std::vector<runtime::stage::TraceSample> empty_samples;
                const std::vector<runtime::stage::TraceSample> &samples =
                    active_chart.trace_samples != nullptr ? *active_chart.trace_samples : empty_samples;

                const runtime::stage::TracePosition current_position =
                    runtime::stage::visual_trace_position_at( samples, active_chart.chart_sample );

                std::vector<runtime::stage::TraceRenderPoint> points;

                if( options.draw_trace && samples.size() >= 2 )
                {
                    points = runtime::stage::make_gameplay_trace_render_points( samples,
                        current_position,
                        active_chart.chart_sample,
                        gameplay_view,
                        stage::trace_lookahead_samples,
                        stage::trace_fade_start_samples,
                        stage::trace_min_alpha,
                        stage::trace_max_alpha );
                }

                const bool drew_trace_line = options.draw_trace && points.size() >= 2;
                const bool can_draw_chart_events = active_chart.chart != nullptr;
                const bool has_guards = can_draw_chart_events && !active_chart.chart->guards.empty();
                const bool has_phrases = can_draw_chart_events && !active_chart.chart->phrases.empty();

                if( !drew_trace_line && !( options.draw_guards && has_guards ) &&
                    !( options.draw_phrases && has_phrases ) )
                {
                    continue;
                }

                drew_anything = true;

                if( options.allow_radar )
                {
                    consider_radar_candidate( active_chart,
                        samples,
                        active_chart.chart_sample,
                        current_position,
                        drew_trace_line );
                }

                const bool partner_chart =
                    active_chart.source_player_index == 2 || active_chart.source_player_index == 3;

                bool trace_locked_by_cone = false;

                if( options.allow_radar && latest_aim_strength_ > 0.15f && samples.size() >= 2 )
                {
                    const std::int64_t first_sample = static_cast<std::int64_t>( samples.front().chart_sample );
                    const std::int64_t last_sample = static_cast<std::int64_t>( samples.back().chart_sample );
                    bool lock_candidate = false;
                    runtime::stage::TracePosition tangent_position = current_position;

                    if( active_chart.chart_sample >= first_sample && active_chart.chart_sample <= last_sample )
                    {
                        lock_candidate = true;
                    }
                    else if( drew_trace_line && active_chart.chart_sample < first_sample &&
                             first_sample - active_chart.chart_sample <= stage::trace_lookahead_samples )
                    {
                        lock_candidate = true;

                        tangent_position = runtime::stage::trace_position_at( samples, first_sample );
                    }

                    if( lock_candidate )
                    {
                        const float inv_strength = 1.0f / std::max( latest_aim_strength_, 0.0001f );
                        const float aim_x = latest_aim_x_ * inv_strength;
                        const float aim_y = latest_aim_y_ * inv_strength;
                        const float trace_tangent_degrees =
                            stage::trace_tangent_degrees_at( tangent_position, gameplay_view );

                        const float trace_radians = trace_tangent_degrees / stage::radians_to_degrees;
                        const float trace_x = std::cos( trace_radians );
                        const float trace_y = std::sin( trace_radians );
                        const float alignment = std::clamp( aim_x * trace_x + aim_y * trace_y, -1.0f, 1.0f );
                        const bool holding_phrase = runtime_.holding_phrase_for_chart( active_chart );

                        const auto window = runtime::stage::aim_window( snap_half_angle, holding_phrase );
                        trace_locked_by_cone = alignment >= window.minimum_alignment;
                    }
                }

                const bool player_is_aiming = latest_aim_strength_ > 0.15f;

                const auto style_color = []( const std::array<float, 4> &color )
                {
                    return rendering::Color{ color[ 0 ], color[ 1 ], color[ 2 ], color[ 3 ] };
                };

                rendering::Color trace_color = !player_is_aiming      ? style_color( visual_style.trace_idle_color )
                                               : trace_locked_by_cone ? style_color( visual_style.trace_locked_color )
                                                                      : style_color( visual_style.trace_lost_color );

                if( partner_chart )
                {
                    trace_color.a *= 0.96f;
                }

                const float trace_dimension_scale =
                    std::max( 0.25f, gameplay_view.zoom ) * stage::trace_line_visual_scale;

                if( drew_trace_line )
                {
                    if( trace_texture != nullptr && trace_texture->valid() )
                    {
                        if( visual_style.trace_scale > 0.001f )
                        {
                            stage::draw_trace_render_points( renderer,
                                points,
                                stage::trace_line_glow_width * visual_style.trace_scale * trace_dimension_scale,
                                rendering::Color{
                                    trace_color.r,
                                    trace_color.g,
                                    trace_color.b,
                                    stage::trace_line_glow_alpha * trace_color.a
                                },
                                options.alpha_scale,
                                &gameplay_view );
                        }

                        stage::draw_generated_trace_render_points( renderer,
                            *trace_texture,
                            points,
                            stage::trace_line_glow_width * visual_style.trace_scale * trace_dimension_scale,
                            trace_color,
                            options.alpha_scale,
                            rendering::Vec2{ gameplay_view.presentation_center_x, gameplay_view.presentation_center_y },
                            gameplay_view.presentation_scale_x,
                            gameplay_view.presentation_scale_y );
                    }
                    else
                    {
                        stage::draw_trace_render_points( renderer,
                            points,
                            stage::trace_line_glow_width * visual_style.trace_scale * trace_dimension_scale,
                            rendering::Color{ 0.32f, 0.72f, 1.0f, stage::trace_line_glow_alpha },
                            options.alpha_scale,
                            &gameplay_view );

                        stage::draw_trace_render_points( renderer,
                            points,
                            ( stage::trace_line_width + 2.0f ) * visual_style.trace_scale * trace_dimension_scale,
                            rendering::Color{ 0.10f, 0.32f, 0.70f, 0.58f },
                            options.alpha_scale,
                            &gameplay_view );

                        stage::draw_trace_render_points( renderer,
                            points,
                            stage::trace_line_width * visual_style.trace_scale * trace_dimension_scale,
                            trace_color,
                            options.alpha_scale,
                            &gameplay_view );
                    }
                }

                if( can_draw_chart_events )
                {
                    const float phrase_time_seconds =
                        static_cast<float>( active_chart.chart_sample ) / static_cast<float>( stage::sample_rate );

                    if( options.draw_phrases )
                    {
                        std::vector<stage::PhraseVisualOverride> phrase_overrides;
                        std::vector<stage::PhraseHitVisualOverride> phrase_hit_overrides;
                        const std::int64_t chart_section_start =
                            static_cast<std::int64_t>( active_chart.chart->start_sample );

                        for( const runtime::stage::StagePhraseVisualState &visual_state : phrase_visual_states )
                        {
                            if( ( !visual_state.darkened && !visual_state.held ) ||
                                visual_state.source_player_index != active_chart.source_player_index ||
                                visual_state.chart_index != active_chart.chart_index )
                            {
                                continue;
                            }

                            phrase_overrides.push_back( stage::PhraseVisualOverride{
                                .start_sample = visual_state.start_section_sample - chart_section_start,
                                .end_sample = visual_state.end_section_sample - chart_section_start,
                                .darkened = visual_state.darkened,
                                .held = visual_state.held
                            } );
                        }

                        for( const runtime::stage::StagePhraseHitVisualState &visual_state : phrase_hit_visual_states )
                        {
                            if( visual_state.source_player_index != active_chart.source_player_index ||
                                visual_state.chart_index != active_chart.chart_index )
                            {
                                continue;
                            }

                            phrase_hit_overrides.push_back( stage::PhraseHitVisualOverride{
                                .start_sample = visual_state.start_section_sample - chart_section_start,
                                .hit_sample = visual_state.hit_section_sample - chart_section_start,
                                .direction_x = visual_state.direction_x,
                                .direction_y = visual_state.direction_y
                            } );
                        }

                        const float phrase_visual_bpm = stage::estimate_chart_visual_bpm( *active_chart.chart );
                        draw_phrase_fragments( renderer,
                            *active_chart.chart,
                            samples,
                            current_position,
                            active_chart.chart_sample,
                            gameplay_view,
                            area,
                            phrase_time_seconds,
                            tube_texture,
                            itfc_texture,
                            debug_phrase_tube_visual_trim_pixels_,
                            debug_phrase_cap_consumes_body_,
                            phrase_overrides.empty() ? nullptr : &phrase_overrides,
                            settings.phrase_tube_wobble,
                            settings.phrase_note_color_index,
                            settings.phrase_note_held_color_index,
                            settings.phrase_tube_gradient_pulse,
                            phrase_visual_bpm,
                            settings.show_judgement_detail || settings.show_stage_debug_overlay,
                            phrase_hit_overrides.empty() ? nullptr : &phrase_hit_overrides,
                            visual_style.note_scale,
                            rendering::Color{
                                visual_style.note_color[ 0 ],
                                visual_style.note_color[ 1 ],
                                visual_style.note_color[ 2 ],
                                visual_style.note_color[ 3 ]
                            },
                            rendering::Color{
                                visual_style.held_note_color[ 0 ],
                                visual_style.held_note_color[ 1 ],
                                visual_style.held_note_color[ 2 ],
                                visual_style.held_note_color[ 3 ]
                            },
                            rendering::Color{
                                visual_style.note_body_color[ 0 ],
                                visual_style.note_body_color[ 1 ],
                                visual_style.note_body_color[ 2 ],
                                visual_style.note_body_color[ 3 ]
                            },
                            note_texture_regions_ );
                    }

                    if( options.draw_guards )
                    {
                        if( guard_rail_visual_alpha_ > 0.001f && !guard_section_rails_drawn && has_guards )
                        {
                            stage::draw_guard_section_rails( renderer,
                                gameplay_view,
                                area,
                                guard_rail_visual_alpha_,
                                settings.gameplay_visual.guard_rail_scale,
                                rendering::Color{
                                    visual_style.guard_rail_color[ 0 ],
                                    visual_style.guard_rail_color[ 1 ],
                                    visual_style.guard_rail_color[ 2 ],
                                    visual_style.guard_rail_color[ 3 ]
                                } );

                            guard_section_rails_drawn = true;
                        }

                        std::vector<stage::GuardHitVisualOverride> guard_hit_overrides;
                        const std::int64_t chart_section_start =
                            static_cast<std::int64_t>( active_chart.chart->start_sample );

                        for( const runtime::stage::StageGuardHitVisualState &visual_state : guard_hit_visual_states )
                        {
                            if( visual_state.source_player_index != active_chart.source_player_index ||
                                visual_state.chart_index != active_chart.chart_index )
                            {
                                continue;
                            }

                            guard_hit_overrides.push_back( stage::GuardHitVisualOverride{
                                .start_sample = visual_state.start_section_sample - chart_section_start,
                                .button = visual_state.button,
                                .hit_sample = visual_state.hit_section_sample - chart_section_start
                            } );
                        }

                        draw_guard_fragments( renderer,
                            *active_chart.chart,
                            active_chart.chart_sample,
                            gameplay_view,
                            load_data_.song.gameplay_speed_factor,
                            itfc_texture,
                            guard_hit_overrides.empty() ? nullptr : &guard_hit_overrides,
                            false,
                            settings.gameplay_visual.guard_note_scale,
                            settings.gameplay_visual.guard_hit_pop_scale,
                            settings.gameplay_visual.guard_hit_fade_scale,
                            settings.gameplay_visual.guard_icon_set.empty() ? nullptr : &custom_guard_icons,
                            settings.controller_orientation );
                    }
                }
            }

            return drew_anything;
        };

        std::vector<runtime::stage::ActiveChart> preview_charts;

        if( !state.song_finished )
        {
            const runtime::stage::StageTransitionPlan &plan = runtime_.transition_plan();

            const runtime::stage::StageSectionTransition route =
                plan.committed ? plan.committed_transition : plan.predicted_transition;

            if( !route.song_finished )
            {
                const std::int64_t current_duration = runtime_.current_section_duration_samples();

                const std::int64_t remaining = runtime_.current_section_remaining_samples();

                if( current_duration > 0 && remaining >= 0 && remaining <= stage::next_section_visual_preview_samples )
                {

                    const std::int64_t preview_section_sample = state.section_sample - current_duration;

                    preview_charts =
                        runtime_.collect_preview_charts_for_section( route.next_section_index, preview_section_sample );
                }
            }
        }

        gameplay_mode_visual = build_gameplay_mode_visual( state.active_charts, preview_charts );

        const float guard_rail_target_alpha = gameplay_mode_visual.guard_mode ? 1.0f : 0.0f;
        const float fade_duration_seconds = 0.075f;
        const float fade_step =
            std::clamp( static_cast<float>( last_frame_delta_seconds_ / static_cast<double>( fade_duration_seconds ) ),
                0.0f,
                1.0f );

        if( guard_rail_visual_alpha_ < guard_rail_target_alpha )
        {
            guard_rail_visual_alpha_ = std::min( guard_rail_target_alpha, guard_rail_visual_alpha_ + fade_step );
        }
        else
        {
            guard_rail_visual_alpha_ = std::max( guard_rail_target_alpha, guard_rail_visual_alpha_ - fade_step );
        }

        const bool vision_cone_enabled = ( runtime_.visual_state().vision_enabled ||
                                             ( teaching_tutorial() && tutorial_.timeline().pointer_active() ) ) &&
                                         ( !teaching_tutorial() || tutorial_.timeline().aim_visible() );

        if( teaching_tutorial() && !tutorial_.timeline().aim_visible() )
        {
            cone_arrow_visual_alpha_ = 0.0f;

            cone_wing_slide_ = 0.0f;
        }

        const float cone_arrow_target_alpha = vision_cone_enabled && latest_aim_strength_ > 0.15f ? 1.0f : 0.0f;
        const float cone_arrow_step = std::clamp( static_cast<float>( last_frame_delta_seconds_ / 0.045 ), 0.0f, 1.0f );

        if( cone_arrow_visual_alpha_ < cone_arrow_target_alpha )
        {
            cone_arrow_visual_alpha_ = std::min( cone_arrow_target_alpha, cone_arrow_visual_alpha_ + cone_arrow_step );
        }
        else
        {
            cone_arrow_visual_alpha_ = std::max( cone_arrow_target_alpha, cone_arrow_visual_alpha_ - cone_arrow_step );
        }

        const float cone_wing_slide_step =
            std::clamp( static_cast<float>( last_frame_delta_seconds_ / 0.13 ), 0.0f, 1.0f );

        if( cone_wing_slide_ < cone_arrow_target_alpha )
        {
            cone_wing_slide_ = std::min( cone_arrow_target_alpha, cone_wing_slide_ + cone_wing_slide_step );
        }
        else
        {
            cone_wing_slide_ = std::max( cone_arrow_target_alpha, cone_wing_slide_ - cone_wing_slide_step );
        }

        bool drew_trace = draw_chart_list( state.active_charts,
            ChartDrawOptions{
                .alpha_scale = 1.0f,
                .allow_radar = vision_cone_enabled,
                .preview = false,
                .draw_trace = true,
                .draw_phrases = false,
                .draw_guards = false
            } );

        if( !preview_charts.empty() )
        {
            const runtime::stage::StageTransitionPlan &plan = runtime_.transition_plan();

            drew_trace = draw_chart_list( preview_charts,
                             ChartDrawOptions{
                                 .alpha_scale = plan.committed ? 0.78f : 0.55f,
                                 .allow_radar = vision_cone_enabled,
                                 .preview = true,
                                 .draw_trace = true,
                                 .draw_phrases = false,
                                 .draw_guards = false
                             } ) ||
                         drew_trace;
        }

        (void)drew_trace;

        const rendering::Vec2 marker{ gameplay_view.center_x, gameplay_view.center_y };

        bool note_flash_active = std::any_of( phrase_visual_states.begin(),
            phrase_visual_states.end(),
            []( const runtime::stage::StagePhraseVisualState &visual_state )
            {
                return visual_state.held && !visual_state.darkened;
            } );

        if( !note_flash_active )
        {
            note_flash_active = std::any_of( guard_hit_visual_states.begin(),
                guard_hit_visual_states.end(),
                [ &state ]( const runtime::stage::StageGuardHitVisualState &visual_state )
                {
                    const std::int64_t elapsed_samples = state.section_sample - visual_state.hit_section_sample;
                    return elapsed_samples >= 0 && elapsed_samples < stage::retail_update_samples;
                } );
        }

        if( note_flash_active && itfc_texture != nullptr && itfc_texture->valid() )
        {
            const std::int64_t retail_tick =
                std::max<std::int64_t>( 0, state.global_sample ) / stage::retail_update_samples;
            {
                const RetailNoteFlashSample flash =
                    retail_note_flash_sample( retail_tick, load_data_.stage.portable_visuals );

                const float flash_size = flash.size * aim_visual_scale;
                const float flash_alpha = std::clamp( flash.alpha, 0.0f, 1.0f );
                const rendering::Rect &source = load_data_.stage.portable_visuals
                                                    ? runtime::stage::gameplay_interface_atlas::portable_note_flash
                                                    : runtime::stage::gameplay_interface_atlas::note_flash;

                // Keep the glow's soft edges.
                renderer.draw_texture_region_rotated( *itfc_texture,
                    source.x,
                    source.y,
                    source.w,
                    source.h,
                    marker.x,
                    marker.y,
                    flash_size,
                    flash_size,
                    0.5f,
                    0.5f,
                    flash.angle_degrees,
                    rendering::Color{ 1.0f, 1.0f, 1.0f, flash_alpha } );
            }
        }

        {
            const bool player_is_aiming = latest_aim_strength_ > 0.15f;
            const bool cone_visually_alive =
                vision_cone_enabled && ( player_is_aiming || cone_arrow_visual_alpha_ > 0.001f );

            if( cone_visually_alive )
            {
                const float inv_strength = 1.0f / std::max( latest_aim_strength_, 0.0001f );
                const float aim_x = latest_aim_x_ * inv_strength;
                const float aim_y = latest_aim_y_ * inv_strength;
                const float aim_degrees =
                    player_is_aiming ? std::atan2( aim_y, aim_x ) * stage::radians_to_degrees : latest_cone_degrees_;

                float cone_degrees = aim_degrees;
                bool cone_locked_to_trace = false;

                if( player_is_aiming && radar_candidate.valid )
                {
                    const float trace_tangent_degrees =
                        stage::trace_tangent_degrees_at( radar_candidate.tangent_position, gameplay_view );

                    const float trace_radians = trace_tangent_degrees / stage::radians_to_degrees;
                    const float trace_x = std::cos( trace_radians );
                    const float trace_y = std::sin( trace_radians );
                    const float alignment = std::clamp( aim_x * trace_x + aim_y * trace_y, -1.0f, 1.0f );

                    const auto window = runtime::stage::aim_window( snap_half_angle, radar_candidate.holding_phrase );

                    if( alignment >= window.minimum_alignment )
                    {
                        cone_degrees = trace_tangent_degrees;

                        cone_locked_to_trace = true;
                    }
                }

                latest_cone_degrees_ = cone_degrees;

                const float cone_radians = cone_degrees / stage::radians_to_degrees;

                const auto window = runtime::stage::aim_window( snap_half_angle, radar_candidate.holding_phrase );
                const float half_angle_radians = visual_style.cone_angle * 0.5f / stage::radians_to_degrees;
                const float half_angle_cos = std::cos( half_angle_radians );
                const float half_angle_sin = std::sin( half_angle_radians );
                const float cone_length = visual_style.cone_length * logical_view_scale;
                const rendering::Vec2 axis{ std::cos( cone_radians ), std::sin( cone_radians ) };
                const rendering::Vec2 perpendicular{ -axis.y, axis.x };
                const rendering::Vec2 upper_direction{
                    axis.x * half_angle_cos - perpendicular.x * half_angle_sin,
                    axis.y * half_angle_cos - perpendicular.y * half_angle_sin
                };

                const rendering::Vec2 lower_direction{
                    axis.x * half_angle_cos + perpendicular.x * half_angle_sin,
                    axis.y * half_angle_cos + perpendicular.y * half_angle_sin
                };

                const auto starts = stage::view_cone_start_points( marker,
                    perpendicular,
                    visual_style.cone_inner_diameter * aim_visual_scale );

                const rendering::Vec2 upper_start = starts.upper;
                const rendering::Vec2 lower_start = starts.lower;

                const rendering::Vec2 upper_end{
                    marker.x + upper_direction.x * cone_length,
                    marker.y + upper_direction.y * cone_length
                };

                const rendering::Vec2 lower_end{
                    marker.x + lower_direction.x * cone_length,
                    marker.y + lower_direction.y * cone_length
                };

                float off_center_mix = 1.0f;
                const bool trace_line_pending = radar_candidate.valid && radar_candidate.has_visible_trace_line;

                if( player_is_aiming && trace_line_pending && cone_locked_to_trace )
                {
                    float angle_delta = std::fmod( aim_degrees - cone_degrees + 540.0f, 360.0f ) - 180.0f;
                    angle_delta = std::fabs( angle_delta );

                    constexpr float perfect_lock_degrees = 5.0f;
                    const float fade_end_degrees = window.half_angle_degrees;
                    const float off_center_t =
                        std::clamp( ( angle_delta - perfect_lock_degrees ) /
                                        std::max( fade_end_degrees - perfect_lock_degrees, 0.0001f ),
                            0.0f,
                            1.0f );

                    off_center_mix = off_center_t * off_center_t * ( 3.0f - 2.0f * off_center_t );
                }

                const auto blend_cone_color =
                    [ off_center_mix ]( const std::array<float, 4> &on_target, const std::array<float, 4> &off_target )
                {
                    return rendering::Color{
                        on_target[ 0 ] + ( off_target[ 0 ] - on_target[ 0 ] ) * off_center_mix,
                        on_target[ 1 ] + ( off_target[ 1 ] - on_target[ 1 ] ) * off_center_mix,
                        on_target[ 2 ] + ( off_target[ 2 ] - on_target[ 2 ] ) * off_center_mix,
                        on_target[ 3 ] + ( off_target[ 3 ] - on_target[ 3 ] ) * off_center_mix
                    };
                };

                rendering::Color fill_color = blend_cone_color( settings.view_cone_on_target_fill_color,
                    settings.view_cone_off_target_fill_color );

                fill_color.a *= cone_arrow_visual_alpha_;
                stage::draw_view_cone_gradient( renderer,
                    marker,
                    upper_start,
                    lower_start,
                    cone_radians,
                    half_angle_radians,
                    cone_length,
                    fill_color );

                rendering::Color line_color = blend_cone_color( settings.view_cone_on_target_line_color,
                    settings.view_cone_off_target_line_color );

                line_color.a *= cone_arrow_visual_alpha_;
                const float line_width = visual_style.cone_line_width * aim_visual_scale;
                stage::draw_view_cone_border( renderer, upper_start, upper_end, line_width, line_color );
                stage::draw_view_cone_border( renderer, lower_start, lower_end, line_width, line_color );

                const float wing_half_height = cone_length * half_angle_sin;

                stage::draw_view_cone_arrows( renderer,
                    marker,
                    axis,
                    perpendicular,
                    cone_arrow_visual_alpha_,
                    cone_wing_slide_,
                    wing_half_height,
                    aim_visual_scale,
                    visual_style,
                    arrow_texture,
                    wing_texture );
            }
        }

        draw_chart_list( state.active_charts,
            ChartDrawOptions{
                .alpha_scale = 1.0f,
                .allow_radar = false,
                .preview = false,
                .draw_trace = false,
                .draw_phrases = true,
                .draw_guards = true
            } );

        if( !preview_charts.empty() )
        {
            const runtime::stage::StageTransitionPlan &plan = runtime_.transition_plan();

            draw_chart_list( preview_charts,
                ChartDrawOptions{
                    .alpha_scale = plan.committed ? 0.78f : 0.55f,
                    .allow_radar = false,
                    .preview = true,
                    .draw_trace = false,
                    .draw_phrases = true,
                    .draw_guards = true
                } );
        }

        stage::draw_view_cone_center_nub( renderer,
            marker,
            latest_center_pressed_,
            aim_visual_scale,
            visual_style.center_nub_scale,
            visual_style.pressed_nub_scale,
            center_nub_texture,
            pressed_nub_texture,
            itfc_texture );
    }
}
