#include "Utils/EnumUtil.hpp"
#include "Stage3DRenderer.hpp"
#include "Stage3DUtil.hpp"

#include "App/piko.hpp"
#include "FileTypes/CHC/CHC.hpp"
#include "FileTypes/CHC/CHCJson.hpp"
#include "FileTypes/SSQ/SSQJson.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/RenderUtil.hpp"
#include "Utils/MathUtil.hpp"
#include "Utils/StrUtil.hpp"

#include <cstdint>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace piko::modules
{
    float Stage3DRenderer::effect_duration_stage_frames( const DrawModel &model,
        const filetypes::ssq::ModelEntry &entry,
        std::uint32_t animation_index ) const
    {
        if( model.animations.empty() )
        {
            return 12.0f;
        }

        const runtime::model::AnimationTiming &animation =
            model.animations[ std::min<std::size_t>( animation_index, model.animations.size() - 1u ) ];

        const float playback_length = std::max( 1.0f, animation.playback_length );
        const bool portable_stage_driven =
            portable_visuals_ && ( !entry.anim_keys.empty() || !entry.controllables.empty() );

        if( portable_stage_driven && animation.speed_mode == 2u )
        {
            const float tempo = std::isfinite( stage_tempo_ ) && stage_tempo_ > 0.0f ? stage_tempo_ : 120.0f;
            const float source_frames_per_stage_frame = tempo * 8.0f / 1800.0f;
            return std::clamp( std::max( 1.0f, playback_length - 1.0f ) /
                                   std::max( source_frames_per_stage_frame, 0.0001f ),
                1.0f,
                120.0f );
        }

        if( animation.speed_mode == 2u || animation.speed_mode == 3u )
        {
            const float interval =
                std::abs( animation.keyframe_interval ) > 0.0001f ? animation.keyframe_interval : 1.0f;

            const float source_frames_per_stage_frame = 2.0f / interval;
            return std::clamp( playback_length / std::max( source_frames_per_stage_frame, 0.0001f ), 1.0f, 120.0f );
        }

        if( animation.speed_mode != 0 )
        {
            return std::clamp( playback_length, 1.0f, 120.0f );
        }

        const float tempo = std::isfinite( stage_tempo_ ) && stage_tempo_ > 0.0f ? stage_tempo_ : 120.0f;
        return std::clamp( playback_length * 900.0f / ( tempo * 4.0f ), 1.0f, 120.0f );
    }

    void Stage3DRenderer::update_effect_playback( float frame )
    {
        if( effect_states_.size() < scene_.models.size() )
        {
            effect_states_.resize( scene_.models.size() );
        }

        for( std::size_t i = 0; i < scene_.models.size(); ++i )
        {
            const filetypes::ssq::ModelEntry &entry = scene_.models[ i ];

            if( entry.model_type < 5u || entry.model_type > 8u )
            {
                continue;
            }

            EffectPlaybackState &state = effect_states_[ i ];
            const filetypes::ssq::ModelAnimKey *key = active_model_anim_key( entry, frame );

            if( key == nullptr || !key->state_driven )
            {
                state.active = false;

                state.last_stage_frame = frame;
                continue;
            }

            if( frame + 0.0001f < state.last_stage_frame ||
                std::abs( state.state_driver_start_stage_frame - key->frame ) > 0.0001f )
            {
                state = {};

                state.state_driver_start_stage_frame = key->frame;
            }

            const std::uint32_t owner_model_type = entry.model_type - 4u;
            const ActionAnimationInput *trigger_input = nullptr;
            bool held_by_owner = false;

            for( const ActionAnimationInput &actor_input : actor_action_inputs_ )
            {
                if( actor_input.effect_result_serial != 0u && actor_input.effect_owner_model_type == owner_model_type &&
                    actor_input.effect_result_serial != state.last_result_serial )
                {
                    trigger_input = &actor_input;
                }

                held_by_owner =
                    held_by_owner || actor_input.effect_hold_owner_model_type == owner_model_type ||
                    ( actor_input.effect_hold_owner_model_mask & ( 1u << ( owner_model_type - 1u ) ) ) != 0u;
            }

            const bool trigger_edge = trigger_input != nullptr;

            if( trigger_edge )
            {
                const auto model_it = models_.find( piko::utils::lowercase_filename( entry.xg_name ) );
                const float duration =
                    model_it != models_.end()
                        ? effect_duration_stage_frames( model_it->second, entry, key->animation_index )
                        : 12.0f;

                state.active = true;

                state.start_stage_frame = frame;

                state.end_stage_frame = frame + duration;

                state.last_result_serial = trigger_input->effect_result_serial;

                state.activation_serial = next_effect_activation_serial_++;
            }

            // sub_1A7390
            if( held_by_owner )
            {
                if( !state.active )
                {
                    state.start_stage_frame = frame;

                    state.activation_serial = next_effect_activation_serial_++;
                }

                state.active = true;
            }
            else if( state.active && frame >= state.end_stage_frame )
            {
                state.active = false;
            }

            state.last_stage_frame = frame;
        }
    }

    bool Stage3DRenderer::load_png_rgba( const std::filesystem::path &path,
        std::int32_t &width,
        std::int32_t &height,
        std::vector<std::uint8_t> &pixels )
    {
        width = 0;

        height = 0;
        pixels.clear();

        SDL_Surface *loaded = SDL_LoadPNG( path.string().c_str() );

        if( loaded == nullptr )
        {
            return false;
        }

        struct SurfaceGuard
        {
            SDL_Surface *surface = nullptr;

            ~SurfaceGuard()
            {
                SDL_DestroySurface( surface );
            }
        } loaded_guard{ loaded };

        SDL_Surface *rgba = SDL_ConvertSurface( loaded, SDL_PIXELFORMAT_RGBA32 );

        if( rgba == nullptr )
        {
            return false;
        }

        SurfaceGuard rgba_guard{ rgba };

        if( rgba->w <= 0 || rgba->h <= 0 )
        {
            return false;
        }

        width = rgba->w;

        height = rgba->h;
        pixels.resize( static_cast<std::size_t>( width ) * static_cast<std::size_t>( height ) * 4u );

        for( std::int32_t y = 0; y < height; ++y )
        {
            std::memcpy( pixels.data() + static_cast<std::size_t>( y ) * static_cast<std::size_t>( width ) * 4u,
                static_cast<const std::uint8_t *>( rgba->pixels ) +
                    static_cast<std::size_t>( y ) * static_cast<std::size_t>( rgba->pitch ),
                static_cast<std::size_t>( width ) * 4u );
        }

        return true;
    }

    float Stage3DRenderer::texture_animation_coordinate_scale( const filetypes::ssq::TextureAnimation &animation,
        std::int32_t texture_dimension,
        bool horizontal )
    {
        if( texture_dimension <= 0 )
        {
            return 1.0f;
        }

        float maximum = horizontal ? static_cast<float>( animation.paste_x ) : static_cast<float>( animation.paste_y );

        for( const filetypes::ssq::TextureCutout &cutout : animation.cutouts )
        {
            maximum = std::max( maximum, horizontal ? cutout.x0 : cutout.y0 );

            maximum = std::max( maximum, horizontal ? cutout.x1 : cutout.y1 );
        }

        if( !std::isfinite( maximum ) || maximum <= static_cast<float>( texture_dimension ) + 0.5f )
        {
            return 1.0f;
        }

        std::uint32_t reference_dimension = 1u;
        const std::uint32_t required_dimension = static_cast<std::uint32_t>( std::ceil( maximum ) );
        while( reference_dimension < required_dimension && reference_dimension < 0x40000000u )
        {
            reference_dimension <<= 1u;
        }

        if( reference_dimension == 0u )
        {
            return 1.0f;
        }

        return static_cast<float>( texture_dimension ) / static_cast<float>( reference_dimension );
    }

    void Stage3DRenderer::register_gltf_texture_animation( App &app,
        const std::filesystem::path &texture_path,
        managers::TextureHandle texture )
    {
        if( !texture.valid() || texture_path.empty() )
        {
            return;
        }

        const rendering::Texture *loaded_texture = app.texture_manager().get( texture );

        if( loaded_texture == nullptr || !loaded_texture->valid() )
        {
            return;
        }

        const std::string texture_key = normalize_model_name( texture_path.filename().string() );

        if( texture_key.empty() )
        {
            return;
        }

        for( std::size_t animation_index = 0; animation_index < scene_.texture_animations.size(); ++animation_index )
        {
            const filetypes::ssq::TextureAnimation &animation = scene_.texture_animations[ animation_index ];

            if( normalize_model_name( animation.texture_name ) != texture_key )
            {
                continue;
            }

            const auto existing = std::find_if( runtime_texture_animations_.begin(),
                runtime_texture_animations_.end(),
                [ & ]( const RuntimeTextureAnimation &runtime_animation )
                {
                    return runtime_animation.scene_animation_index == animation_index &&
                           runtime_animation.texture.id == texture.id;
                } );
            if( existing != runtime_texture_animations_.end() )
            {
                continue;
            }

            RuntimeTextureAnimation runtime_animation{};
            runtime_animation.scene_animation_index = animation_index;

            runtime_animation.texture = texture;

            runtime_animation.texture_id = loaded_texture->id;

            if( !load_png_rgba( texture_path,
                    runtime_animation.width,
                    runtime_animation.height,
                    runtime_animation.source_rgba ) )
            {
                continue;
            }

            runtime_animation.coordinate_scale_x =
                texture_animation_coordinate_scale( animation, runtime_animation.width, true );

            runtime_animation.coordinate_scale_y =
                texture_animation_coordinate_scale( animation, runtime_animation.height, false );

            runtime_texture_animations_.push_back( std::move( runtime_animation ) );
        }
    }

    void Stage3DRenderer::update_texture_animations( float frame )
    {
        for( RuntimeTextureAnimation &runtime_animation : runtime_texture_animations_ )
        {
            if( runtime_animation.scene_animation_index >= scene_.texture_animations.size() ||
                runtime_animation.width <= 0 || runtime_animation.height <= 0 || runtime_animation.source_rgba.empty() )
            {
                continue;
            }

            const filetypes::ssq::TextureAnimation &animation =
                scene_.texture_animations[ runtime_animation.scene_animation_index ];

            if( animation.frames.empty() || animation.cutouts.empty() )
            {
                continue;
            }

            float schedule_frame = frame;
            const float period = animation.frames.back().frame;

            if( period != 0.0f && schedule_frame != 0.0f )
            {
                const std::int32_t quotient = static_cast<std::int32_t>( schedule_frame / period );

                if( quotient != 0 )
                {
                    const float multiple = static_cast<float>( quotient ) * period;
                    schedule_frame = multiple == schedule_frame ? 0.0f : std::abs( schedule_frame - multiple );
                }
            }

            const filetypes::ssq::TextureAnimFrame *selected = &animation.frames.front();

            for( const filetypes::ssq::TextureAnimFrame &candidate : animation.frames )
            {
                if( candidate.frame > schedule_frame )
                {
                    break;
                }

                selected = &candidate;
            }

            if( selected->cutout_index >= animation.cutouts.size() ||
                runtime_animation.last_cutout_index == selected->cutout_index )
            {
                continue;
            }

            const filetypes::ssq::TextureCutout &cutout = animation.cutouts[ selected->cutout_index ];
            std::int32_t source_x =
                static_cast<std::int32_t>( std::lround( cutout.x0 * runtime_animation.coordinate_scale_x ) );

            std::int32_t source_y =
                static_cast<std::int32_t>( std::lround( cutout.y0 * runtime_animation.coordinate_scale_y ) );

            std::int32_t copy_width = static_cast<std::int32_t>(
                std::lround( ( cutout.x1 - cutout.x0 ) * runtime_animation.coordinate_scale_x ) );

            std::int32_t copy_height = static_cast<std::int32_t>(
                std::lround( ( cutout.y1 - cutout.y0 ) * runtime_animation.coordinate_scale_y ) );

            if( copy_width <= 0 || copy_height <= 0 )
            {
                const auto fallback = std::find_if( animation.cutouts.begin(),
                    animation.cutouts.end(),
                    []( const filetypes::ssq::TextureCutout &candidate )
                    {
                        return candidate.x1 > candidate.x0 && candidate.y1 > candidate.y0;
                    } );
                if( fallback == animation.cutouts.end() )
                {
                    continue;
                }

                copy_width = static_cast<std::int32_t>(
                    std::lround( ( fallback->x1 - fallback->x0 ) * runtime_animation.coordinate_scale_x ) );

                copy_height = static_cast<std::int32_t>(
                    std::lround( ( fallback->y1 - fallback->y0 ) * runtime_animation.coordinate_scale_y ) );

                source_x = static_cast<std::int32_t>(
                    std::lround( static_cast<float>( animation.paste_x ) * runtime_animation.coordinate_scale_x ) );

                source_y = static_cast<std::int32_t>(
                    std::lround( static_cast<float>( animation.paste_y ) * runtime_animation.coordinate_scale_y ) );
            }

            const std::int32_t destination_x = static_cast<std::int32_t>(
                std::lround( static_cast<float>( animation.paste_x ) * runtime_animation.coordinate_scale_x ) );

            const std::int32_t destination_y = static_cast<std::int32_t>(
                std::lround( static_cast<float>( animation.paste_y ) * runtime_animation.coordinate_scale_y ) );

            copy_width = std::min( copy_width, runtime_animation.width - std::max( source_x, destination_x ) );

            copy_height = std::min( copy_height, runtime_animation.height - std::max( source_y, destination_y ) );

            if( source_x < 0 || source_y < 0 || destination_x < 0 || destination_y < 0 || copy_width <= 0 ||
                copy_height <= 0 )
            {
                continue;
            }

            std::vector<std::uint8_t> pixels(
                static_cast<std::size_t>( copy_width ) * static_cast<std::size_t>( copy_height ) * 4u );

            for( std::int32_t y = 0; y < copy_height; ++y )
            {
                const std::size_t source_offset =
                    ( static_cast<std::size_t>( source_y + y ) * static_cast<std::size_t>( runtime_animation.width ) +
                        static_cast<std::size_t>( source_x ) ) *
                    4u;

                const std::size_t destination_offset =
                    static_cast<std::size_t>( y ) * static_cast<std::size_t>( copy_width ) * 4u;

                std::memcpy( pixels.data() + destination_offset,
                    runtime_animation.source_rgba.data() + source_offset,
                    static_cast<std::size_t>( copy_width ) * 4u );
            }

            if( runtime_animation.texture_id == 0u )
            {
                continue;
            }

            glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( runtime_animation.texture_id ) );
            glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
            glTexSubImage2D( GL_TEXTURE_2D,
                0,
                destination_x,
                destination_y,
                copy_width,
                copy_height,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                pixels.data() );

            glGenerateMipmap( GL_TEXTURE_2D );
            glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
            glBindTexture( GL_TEXTURE_2D, 0 );
            runtime_animation.last_cutout_index = selected->cutout_index;
        }
    }

    Stage3DRenderer::SampledFixedSprite Stage3DRenderer::sample_fixed_sprite( const filetypes::ssq::FixedSprite &sprite,
        float frame,
        bool external_visibility ) const
    {
        SampledFixedSprite sampled{};
        sampled.position = sprite.descriptor.position;

        sampled.width = sprite.descriptor.width;

        sampled.height = sprite.descriptor.height;

        sampled.source_x = sprite.descriptor.source_x;

        sampled.source_y = sprite.descriptor.source_y;

        sampled.source_width = sprite.descriptor.source_width;

        sampled.source_height = sprite.descriptor.source_height;

        const auto wrap_frame = []( float value, float period )
        {
            if( !std::isfinite( value ) || !std::isfinite( period ) || period <= 0.0001f )
            {
                return std::max( 0.0f, value );
            }

            float wrapped = std::fmod( value, period );

            if( wrapped < 0.0f )
            {
                wrapped += period;
            }

            if( std::abs( wrapped - period ) <= 0.0001f )
            {
                wrapped = 0.0f;
            }

            return wrapped;
        };

        if( !sprite.transform_keys.empty() )
        {
            const float transform_frame = wrap_frame( frame, sprite.transform_keys.back().frame );
            std::size_t key_index = 0;

            for( std::size_t i = 1; i < sprite.transform_keys.size(); ++i )
            {
                if( sprite.transform_keys[ i ].frame > transform_frame )
                {
                    break;
                }

                key_index = i;
            }

            const filetypes::ssq::FixedSpriteTransformKey &current = sprite.transform_keys[ key_index ];

            if( current.external_visibility_control && sprite.descriptor.owner_selector != 0u )
            {
                sampled.visible = external_visibility;
            }
            else
            {
                bool visibility_found = false;

                for( std::size_t i = 0; i <= key_index; ++i )
                {
                    if( !sprite.transform_keys[ i ].external_visibility_control )
                    {
                        sampled.visible = !sprite.transform_keys[ i ].hidden;

                        visibility_found = true;
                    }
                }

                if( !visibility_found )
                {
                    for( std::size_t i = sprite.transform_keys.size(); i > key_index + 1u; --i )
                    {
                        const filetypes::ssq::FixedSpriteTransformKey &candidate = sprite.transform_keys[ i - 1u ];

                        if( !candidate.external_visibility_control )
                        {
                            sampled.visible = !candidate.hidden;
                            break;
                        }
                    }
                }
            }

            sampled.position = current.position;

            sampled.width = current.width;

            sampled.height = current.height;

            if( current.interpolate && key_index + 1u < sprite.transform_keys.size() )
            {
                const filetypes::ssq::FixedSpriteTransformKey &next = sprite.transform_keys[ key_index + 1u ];
                const float t = stage3d::key_lerp_t( transform_frame,
                    current.frame,
                    next.frame,
                    current.reciprocal_frames_to_next );

                sampled.position = filetypes::xg::lerp( current.position, next.position, t );

                sampled.width = current.width + ( next.width - current.width ) * t;

                sampled.height = current.height + ( next.height - current.height ) * t;
            }
        }

        if( !sprite.color_keys.empty() )
        {
            const float color_frame = wrap_frame( frame, sprite.color_keys.back().frame );
            std::size_t key_index = 0;

            for( std::size_t i = 1; i < sprite.color_keys.size(); ++i )
            {
                if( sprite.color_keys[ i ].frame > color_frame )
                {
                    break;
                }

                key_index = i;
            }

            const filetypes::ssq::ColorKey &current = sprite.color_keys[ key_index ];
            sampled.color = current.value;

            if( current.interpolate && key_index + 1u < sprite.color_keys.size() )
            {
                const filetypes::ssq::ColorKey &next = sprite.color_keys[ key_index + 1u ];
                const float t =
                    stage3d::key_lerp_t( color_frame, current.frame, next.frame, current.reciprocal_frames_to_next );

                sampled.color = rendering::lerp_color( current.value, next.value, t );
            }
        }

        if( !sprite.frame_keys.empty() )
        {
            const float sprite_frame = wrap_frame( frame, sprite.frame_keys.back().frame );
            std::size_t key_index = 0;

            for( std::size_t i = 1; i < sprite.frame_keys.size(); ++i )
            {
                if( sprite.frame_keys[ i ].frame > sprite_frame )
                {
                    break;
                }

                key_index = i;
            }

            const filetypes::ssq::FixedSpriteFrameKey &current = sprite.frame_keys[ key_index ];
            sampled.source_x = current.source_x;

            sampled.source_y = current.source_y;

            sampled.source_width = current.source_width;

            sampled.source_height = current.source_height;

            if( current.interpolate && key_index + 1u < sprite.frame_keys.size() )
            {
                const filetypes::ssq::FixedSpriteFrameKey &next = sprite.frame_keys[ key_index + 1u ];
                const float t =
                    stage3d::key_lerp_t( sprite_frame, current.frame, next.frame, current.reciprocal_frames_to_next );

                sampled.source_x = current.source_x + ( next.source_x - current.source_x ) * t;

                sampled.source_y = current.source_y + ( next.source_y - current.source_y ) * t;

                sampled.source_width = current.source_width + ( next.source_width - current.source_width ) * t;

                sampled.source_height = current.source_height + ( next.source_height - current.source_height ) * t;
            }
        }

        sampled.color.r = piko::utils::clamp01( sampled.color.r );

        sampled.color.g = piko::utils::clamp01( sampled.color.g );

        sampled.color.b = piko::utils::clamp01( sampled.color.b );

        sampled.color.a = piko::utils::clamp01( sampled.color.a );

        sampled.visible = sampled.visible && std::isfinite( sampled.width ) && std::isfinite( sampled.height ) &&
                          std::abs( sampled.width ) > 0.0001f && std::abs( sampled.height ) > 0.0001f &&
                          sampled.color.a > 0.0001f;

        return sampled;
    }

    bool Stage3DRenderer::fixed_sprite_owner_active( std::uint32_t owner_selector ) const
    {
        std::uint32_t model_type = 0;

        if( owner_selector >= 1u && owner_selector <= 4u )
        {
            model_type = owner_selector;
        }
        else if( owner_selector >= 5u && owner_selector <= 8u )
        {
            model_type = owner_selector - 4u;
        }

        if( model_type == 0u )
        {
            return false;
        }

        const std::uint32_t bit = 1u << ( model_type - 1u );
        return std::any_of( actor_action_inputs_.begin(),
            actor_action_inputs_.end(),
            [ bit ]( const ActionAnimationInput &input )
            {
                return ( input.fixed_sprite_active_model_mask & bit ) != 0u;
            } );
    }

    void Stage3DRenderer::draw_fixed_sprites( App &app,
        rendering::Renderer &renderer,
        const filetypes::xg::Matrix &projection,
        const filetypes::xg::Matrix &view,
        float frame )
    {
        if( runtime_fixed_sprites_.empty() )
        {
            return;
        }

        const GLint mvp_location = glGetUniformLocation( shader_.program(), "u_mvp" );
        const GLint mesh_matrix_location = glGetUniformLocation( shader_.program(), "u_mesh_matrix" );
        const GLint normal_matrix_location = glGetUniformLocation( shader_.program(), "u_normal_matrix" );
        const GLint gpu_skinning_location = glGetUniformLocation( shader_.program(), "u_gpu_skinning" );
        const GLint use_texture_location = glGetUniformLocation( shader_.program(), "u_use_texture" );
        const GLint texture_env_location = glGetUniformLocation( shader_.program(), "u_texture_env" );
        const GLint debug_mode_location = glGetUniformLocation( shader_.program(), "u_debug_mode" );
        const GLint shading_type_location = glGetUniformLocation( shader_.program(), "u_shading_type" );
        const GLint blend_type_location = glGetUniformLocation( shader_.program(), "u_blend_type" );
        const GLint alpha_active_location = glGetUniformLocation( shader_.program(), "u_alpha_active" );
        const GLint alpha_cutoff_location = glGetUniformLocation( shader_.program(), "u_alpha_cutoff" );
        const GLint alpha_key_mode_location = glGetUniformLocation( shader_.program(), "u_alpha_key_mode" );
        const GLint effect_alpha_as_luma_location = glGetUniformLocation( shader_.program(), "u_effect_alpha_as_luma" );
        const GLint material_diffuse_location = glGetUniformLocation( shader_.program(), "u_material_diffuse" );

        const filetypes::xg::Matrix identity = filetypes::xg::identity_matrix();
        const filetypes::xg::Matrix mvp = make_mvp( projection, view, identity );
        const std::array<float, 9> normal_matrix = stage3d::normal_matrix3_from_model( identity );

        if( mvp_location >= 0 )
        {
            glUniformMatrix4fv( mvp_location, 1, GL_FALSE, mvp.m );
        }

        if( mesh_matrix_location >= 0 )
        {
            glUniformMatrix4fv( mesh_matrix_location, 1, GL_FALSE, identity.m );
        }

        if( normal_matrix_location >= 0 )
        {
            glUniformMatrix3fv( normal_matrix_location, 1, GL_FALSE, normal_matrix.data() );
        }

        if( gpu_skinning_location >= 0 )
        {
            glUniform1i( gpu_skinning_location, 0 );
        }

        if( use_texture_location >= 0 )
        {
            glUniform1i( use_texture_location, 1 );
        }

        if( texture_env_location >= 0 )
        {
            glUniform1i( texture_env_location, 0 );
        }

        if( debug_mode_location >= 0 )
        {
            glUniform1i( debug_mode_location, 0 );
        }

        if( shading_type_location >= 0 )
        {
            glUniform1i( shading_type_location, 3 );
        }

        if( alpha_active_location >= 0 )
        {
            glUniform1i( alpha_active_location, 1 );
        }

        if( alpha_cutoff_location >= 0 )
        {
            glUniform1f( alpha_cutoff_location, 1.0f / 255.0f );
        }

        if( material_diffuse_location >= 0 )
        {
            glUniform4f( material_diffuse_location, 1.0f, 1.0f, 1.0f, 1.0f );
        }

        const filetypes::xg::Matrix camera_world = filetypes::xg::inverse_affine_matrix( view );
        const filetypes::xg::Vec3 right = filetypes::xg::normalize(
            filetypes::xg::Vec3{ camera_world.m[ 0 ], camera_world.m[ 1 ], camera_world.m[ 2 ] } );

        const filetypes::xg::Vec3 up = filetypes::xg::normalize(
            filetypes::xg::Vec3{ camera_world.m[ 4 ], camera_world.m[ 5 ], camera_world.m[ 6 ] } );

        const filetypes::xg::Vec3 normal = filetypes::xg::normalize( stage3d::cross_vec3( right, up ) );

        glBindVertexArray( vao_ );
        glBindBuffer( GL_ARRAY_BUFFER, vbo_ );
        glDisable( GL_CULL_FACE );
        glEnable( GL_DEPTH_TEST );
        glDepthMask( GL_FALSE );
        glEnable( GL_BLEND );

        for( const RuntimeFixedSprite &runtime_sprite : runtime_fixed_sprites_ )
        {
            if( runtime_sprite.scene_sprite_index >= scene_.fixed_sprites.size() || runtime_sprite.texture_width <= 0 ||
                runtime_sprite.texture_height <= 0 )
            {
                continue;
            }

            const filetypes::ssq::FixedSprite &sprite = scene_.fixed_sprites[ runtime_sprite.scene_sprite_index ];
            const SampledFixedSprite sampled =
                sample_fixed_sprite( sprite, frame, fixed_sprite_owner_active( sprite.descriptor.owner_selector ) );

            if( !sampled.visible )
            {
                continue;
            }

            const rendering::Texture *texture = app.texture_manager().get( runtime_sprite.texture );

            if( texture == nullptr || !texture->valid() )
            {
                continue;
            }

            const float inv_width = 1.0f / static_cast<float>( runtime_sprite.texture_width );
            const float inv_height = 1.0f / static_cast<float>( runtime_sprite.texture_height );
            const float u0 = sampled.source_x * inv_width;
            const float v0 = sampled.source_y * inv_height;
            const float u1 = ( sampled.source_x + sampled.source_width ) * inv_width;
            const float v1 = ( sampled.source_y + sampled.source_height ) * inv_height;
            const float half_width = sampled.width * 0.5f;
            const float half_height = sampled.height * 0.5f;

            const auto vertex_position = [ & ]( float horizontal, float vertical )
            {
                return filetypes::xg::Vec3{
                    sampled.position.x + right.x * horizontal + up.x * vertical,
                    sampled.position.y + right.y * horizontal + up.y * vertical,
                    sampled.position.z + right.z * horizontal + up.z * vertical
                };
            };

            const filetypes::xg::Vec3 bottom_left = vertex_position( -half_width, -half_height );
            const filetypes::xg::Vec3 bottom_right = vertex_position( half_width, -half_height );
            const filetypes::xg::Vec3 top_right = vertex_position( half_width, half_height );
            const filetypes::xg::Vec3 top_left = vertex_position( -half_width, half_height );

            const auto make_vertex = [ & ]( const filetypes::xg::Vec3 &position, float u, float v )
            {
                GpuVertex vertex{};
                vertex.px = position.x;

                vertex.py = position.y;

                vertex.pz = position.z;

                vertex.u = u;

                vertex.v = v;

                vertex.r = sampled.color.r;

                vertex.g = sampled.color.g;

                vertex.b = sampled.color.b;

                vertex.a = sampled.color.a;

                vertex.nx = normal.x;

                vertex.ny = normal.y;

                vertex.nz = normal.z;
                return vertex;
            };

            const std::array<GpuVertex, 6> vertices{
                make_vertex( bottom_left, u0, v0 ),
                make_vertex( bottom_right, u1, v0 ),
                make_vertex( top_right, u1, v1 ),
                make_vertex( bottom_left, u0, v0 ),
                make_vertex( top_right, u1, v1 ),
                make_vertex( top_left, u0, v1 )
            };

            const std::uint32_t blend_mode = sprite.base_words[ 6 ];

            if( blend_mode == 2u )
            {
                glBlendEquation( GL_FUNC_ADD );
                glBlendFunc( GL_SRC_ALPHA, GL_ONE );

                if( blend_type_location >= 0 )
                {
                    glUniform1i( blend_type_location, 1 );
                }

                if( effect_alpha_as_luma_location >= 0 )
                {
                    glUniform1i( effect_alpha_as_luma_location, 1 );
                }
            }
            else
            {
                glBlendEquation( GL_FUNC_ADD );
                glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

                if( blend_type_location >= 0 )
                {
                    glUniform1i( blend_type_location, 5 );
                }

                if( effect_alpha_as_luma_location >= 0 )
                {
                    glUniform1i( effect_alpha_as_luma_location, 0 );
                }
            }

            if( alpha_key_mode_location >= 0 )
            {
                glUniform1i( alpha_key_mode_location, 1 );
            }

            glActiveTexture( GL_TEXTURE0 );
            glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( texture->id ) );
            configure_texture_sampling( static_cast<GLuint>( texture->id ),
                false,
                static_cast<std::int32_t>( renderer.texture_sampling().mode ) );

            glBufferData( GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>( sizeof( vertices ) ),
                vertices.data(),
                GL_DYNAMIC_DRAW );

            glDrawArrays( GL_TRIANGLES, 0, static_cast<GLsizei>( vertices.size() ) );
        }
    }
}
