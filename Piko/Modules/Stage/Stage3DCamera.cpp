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
    void Stage3DRenderer::build_camera_presetup_ranges( const gamedata::PlayableStageEntry &stage )
    {
        camera_ranges_.clear();

        if( stage.chc_exists && !stage.chc_path.empty() )
        {
            try
            {
                const filetypes::chc::Song song = filetypes::chc::load_json_file( stage.chc_path );
                camera_ranges_.reserve( song.cues.size() );

                for( std::size_t i = 0; i < song.cues.size(); ++i )
                {
                    const filetypes::chc::Cue &cue = song.cues[ i ];

                    if( !std::isfinite( cue.first_frame ) || !std::isfinite( cue.last_frame ) )
                    {
                        continue;
                    }

                    const float start = std::min( cue.first_frame, cue.last_frame );
                    const float end = std::max( cue.first_frame, cue.last_frame );

                    if( end < start )
                    {
                        continue;
                    }

                    CameraPresetupRange range{};
                    range.start_frame = start;

                    range.end_frame = end;

                    range.duration_frames = std::max( 1.0f, end - start + 1.0f );

                    range.loop = false;

                    range.name = !cue.audio_section_name.empty() ? cue.audio_section_name : cue.fallback_name;

                    if( range.name.empty() )
                    {
                        std::ostringstream fallback;
                        fallback << "cue" << i;
                        range.name = fallback.str();
                    }

                    camera_ranges_.push_back( std::move( range ) );
                }

                const auto make_range = []( const filetypes::chc::SceneFrameRange &source, const char *name )
                {
                    CameraPresetupRange range{};
                    range.start_frame = std::min( source.first_frame, source.last_frame );

                    range.end_frame = std::max( source.first_frame, source.last_frame );

                    range.duration_frames = std::max( 1.0f, range.end_frame - range.start_frame + 1.0f );

                    range.loop = false;

                    range.name = name;
                    return range;
                };

                win_a_range_ = make_range( song.win_a, "win_a" );

                win_b_range_ = make_range( song.win_b, "win_b" );

                lose_pre_range_ = make_range( song.lose_pre, "lose_pre" );

                lose_post_range_ = make_range( song.lose_post, "lose_post" );

                const auto extend_win_range_to_authored_key = [ & ]( CameraPresetupRange &range )
                {
                    if( range.start_frame <= 0.0f || range.end_frame < range.start_frame )
                    {
                        return;
                    }

                    const float window_end = range.start_frame + 119.0f;
                    float authored_end = range.end_frame;

                    const auto consider = [ & ]( float frame )
                    {
                        if( std::isfinite( frame ) && frame > authored_end && frame <= window_end )
                        {
                            authored_end = frame;
                        }
                    };

                    for( const auto &key : scene_.camera.position_keys )
                    {
                        consider( key.frame );
                    }

                    for( const auto &key : scene_.camera.rotation_keys )
                    {
                        consider( key.frame );
                    }

                    for( const auto &key : scene_.camera.projection_keys )
                    {
                        consider( key.frame );
                    }

                    for( const filetypes::ssq::ModelEntry &model : scene_.models )
                    {
                        for( const auto &key : model.position_keys )
                        {
                            consider( key.frame );
                        }

                        for( const auto &key : model.rotation_keys )
                        {
                            consider( key.frame );
                        }

                        for( const auto &key : model.scale_keys )
                        {
                            consider( key.frame );
                        }

                        for( const auto &key : model.anim_keys )
                        {
                            consider( key.frame );
                        }
                    }

                    if( authored_end > range.end_frame )
                    {
                        range.end_frame = authored_end;

                        range.duration_frames = std::max( 1.0f, range.end_frame - range.start_frame + 1.0f );
                    }
                };

                extend_win_range_to_authored_key( win_a_range_ );
                extend_win_range_to_authored_key( win_b_range_ );
            }
            catch( const std::exception & )
            {
                camera_ranges_.clear();
            }
        }

        if( !camera_ranges_.empty() )
        {
            return;
        }

        bool have_frame = false;
        float min_frame = 0.0f;
        float max_frame = 0.0f;

        const auto consider = [ & ]( float frame )
        {
            if( !std::isfinite( frame ) )
            {
                return;
            }

            if( !have_frame )
            {
                min_frame = frame;

                max_frame = frame;

                have_frame = true;
            }
            else
            {
                min_frame = std::min( min_frame, frame );

                max_frame = std::max( max_frame, frame );
            }
        };

        for( const auto &key : scene_.camera.position_keys )
        {
            consider( key.frame );
        }

        for( const auto &key : scene_.camera.rotation_keys )
        {
            consider( key.frame );
        }

        for( const auto &key : scene_.camera.projection_keys )
        {
            consider( key.frame );
        }

        CameraPresetupRange range{};
        range.start_frame = std::max( 0.0f, min_frame );

        range.end_frame = std::max( range.start_frame, max_frame );

        range.duration_frames = std::max( 1.0f, range.end_frame - range.start_frame + 1.0f );

        range.loop = false;

        range.name = "raw";
        camera_ranges_.push_back( std::move( range ) );
    }

    const Stage3DRenderer::CameraPresetupRange *Stage3DRenderer::active_camera_range( std::size_t *range_index ) const
    {
        if( camera_ranges_.empty() )
        {
            if( range_index != nullptr )
            {
                *range_index = static_cast<std::size_t>( -1 );
            }

            return nullptr;
        }

        const std::size_t index = std::min( stage_section_index_, camera_ranges_.size() - 1u );

        if( range_index != nullptr )
        {
            *range_index = index;
        }

        return &camera_ranges_[ index ];
    }

    float Stage3DRenderer::effective_stage_frame() const
    {
        if( stage_result_active_ )
        {
            // sub_19F130
            const CameraPresetupRange *range = &win_a_range_;
            float local_frame = std::min( stage_result_elapsed_frame_, 119.0f );

            if( !stage_result_won_ )
            {
                if( stage_result_elapsed_frame_ < lose_pre_range_.duration_frames )
                {
                    range = &lose_pre_range_;

                    local_frame = stage_result_elapsed_frame_;
                }
                else
                {
                    range = &lose_post_range_;

                    local_frame = stage_result_elapsed_frame_ - lose_pre_range_.duration_frames;

                    if( range->duration_frames > 0.0f )
                    {
                        local_frame = std::fmod( local_frame, range->duration_frames );

                        if( local_frame < 0.0f )
                        {
                            local_frame += range->duration_frames;
                        }
                    }
                }
            }

            const float last_local_frame = std::max( 0.0f, range->duration_frames - 1.0f );
            return std::max( 0.0f, range->start_frame + std::clamp( local_frame, 0.0f, last_local_frame ) );
        }

        const CameraPresetupRange *range = active_camera_range();

        if( range == nullptr )
        {
            float frame = stage_frame_ * camera_time_scale_ + camera_frame_offset_;

            if( !std::isfinite( frame ) )
            {
                frame = 0.0f;
            }

            return std::max( 0.0f, frame );
        }

        // sub_19F290
        float local_frame = stage_section_frame_ * camera_time_scale_ + camera_frame_offset_;

        if( !std::isfinite( local_frame ) )
        {
            local_frame = 0.0f;
        }

        if( range->loop )
        {
            local_frame = std::fmod( std::max( 0.0f, local_frame ), range->duration_frames );

            if( local_frame < 0.0f )
            {
                local_frame += range->duration_frames;
            }
        }
        else
        {
            local_frame = std::clamp( local_frame, 0.0f, range->duration_frames );
        }

        return std::max( 0.0f, range->start_frame + local_frame );
    }

    float Stage3DRenderer::animation_global_frame() const
    {
        return stage_result_active_ ? effective_stage_frame() : stage_frame_;
    }

    filetypes::ssq::Camera Stage3DRenderer::sample_camera() const
    {
        const float frame = effective_stage_frame();
        filetypes::ssq::Camera camera = scene_.camera;
        camera.position = stage3d::sample_vec3_keys( scene_.camera.position_keys, scene_.camera.position, frame );

        const filetypes::xg::Quat raw_rotation =
            stage3d::sample_quat_keys( scene_.camera.rotation_keys, scene_.camera.raw_rotation, frame, true );

        auto convert_camera_quat = [ & ]( filetypes::xg::Quat q )
        {
            switch( camera_rotation_mode_ )
            {
                case CameraRotationMode::ZReflectOpenGlForward:
                case CameraRotationMode::ZReflectInverseOpenGlForward:
                    q.x = -q.x;

                    q.y = -q.y;
                    break;

                case CameraRotationMode::XReflectOpenGlForward:
                case CameraRotationMode::XReflectInverseOpenGlForward:
                    q.y = -q.y;

                    q.z = -q.z;
                    break;

                case CameraRotationMode::YReflectOpenGlForward:
                case CameraRotationMode::YReflectInverseOpenGlForward:
                    q.x = -q.x;

                    q.z = -q.z;
                    break;

                case CameraRotationMode::RawOpenGlForward:
                case CameraRotationMode::RawInverseOpenGlForward:
                    break;
            }

            return filetypes::xg::normalize_quaternion( q );
        };

        camera.raw_rotation = raw_rotation;

        camera.rotation = convert_camera_quat( raw_rotation );

        stage3d::sample_projection_keys( scene_.camera.projection_keys,
            frame,
            camera.fov_degrees,
            camera.aspect,
            camera.z_near,
            camera.z_far );

        return camera;
    }

    float Stage3DRenderer::default_gmlt_light_strength() const
    {
        return stage_number_ == 0 && !portable_visuals_ ? tutorial_gmlt_light_strength_ : stage_gmlt_light_strength_;
    }

    Stage3DRenderer::SceneLighting Stage3DRenderer::sample_scene_lighting( float frame ) const
    {
        SceneLighting lighting{};
        lighting.fog = scene_.camera.fog_color;

        lighting.ambient = stage3d::sample_color_keys( scene_.camera.ambient_keys, scene_.camera.ambient_color, frame );

        lighting.diffuse = scene_.camera.base_diffuse_color;

        lighting.fog.a = 1.0f;

        lighting.ambient.a = 1.0f;

        lighting.diffuse.a = 1.0f;

        for( std::size_t setup_index = 0; setup_index < scene_.camera.light_setups.size(); ++setup_index )
        {
            if( lighting.gmlt_light_count >= lighting.gmlt_lights.size() )
            {
                break;
            }

            if( gmlt_debug_mode_ == GmltDebugMode::None )
            {
                break;
            }

            if( gmlt_debug_mode_ == GmltDebugMode::Only0 && setup_index != 0 )
            {
                continue;
            }

            if( gmlt_debug_mode_ == GmltDebugMode::Only1 && setup_index != 1 )
            {
                continue;
            }

            const filetypes::ssq::LightSetup &setup = scene_.camera.light_setups[ setup_index ];

            if( !setup.enabled )
            {
                continue;
            }

            const filetypes::ssq::LightColorKey color =
                stage3d::sample_light_color_keys( setup.color_keys, setup.primary_color, setup.secondary_color, frame );

            SceneLight &light = lighting.gmlt_lights[ lighting.gmlt_light_count++ ];
            light.source_index = static_cast<std::int32_t>( setup_index );

            light.primary = color.primary;

            light.secondary = color.secondary;

            light.primary.a = 1.0f;

            light.secondary.a = 1.0f;

            if( setup.rotation_keys.empty() )
            {
                light.direction = filetypes::xg::normalize( setup.direction );
            }
            else
            {
                filetypes::xg::Quat rotation = stage3d::sample_light_rotation_keys( setup.rotation_keys,
                    gmlt_direction_uses_raw_quat() ? setup.raw_rotation : setup.rotation,
                    frame,
                    gmlt_direction_uses_raw_quat() );

                if( gmlt_direction_uses_inverse_quat() )
                {
                    rotation = stage3d::conjugate_quat( rotation );
                }

                light.direction = stage3d::rotate_vector_by_quat( rotation, gmlt_direction_basis() );
            }
        }

        return lighting;
    }

    bool Stage3DRenderer::should_draw_stage_sky() const
    {
        return debug_stage_sky_enabled_ && stage_show_sky_flag_ && sky_texture_.valid();
    }

    void Stage3DRenderer::resolve_stage_sky_texture()
    {
        sky_texture_ = {};
        sky_texture_name_.clear();

        if( !stage_show_sky_flag_ )
        {
            return;
        }

        std::vector<std::string> candidates;

        if( stage_number_ >= 0 )
        {
            std::ostringstream name;
            name << "ST" << std::setw( 2 ) << std::setfill( '0' ) << stage_number_ << "SKY.png";
            candidates.push_back( name.str() );
        }

        for( const std::string &candidate : candidates )
        {
            const std::string key = normalize_texture_name( candidate );

            const auto it = texture_by_name_.find( key );

            if( it != texture_by_name_.end() && it->second.valid() )
            {
                sky_texture_ = it->second;

                sky_texture_name_ = key;
                return;
            }
        }
    }

    void Stage3DRenderer::draw_stage_sky( App &app,
        rendering::Renderer &renderer,
        std::int32_t width,
        std::int32_t height )
    {
        if( !should_draw_stage_sky() || width <= 0 || height <= 0 )
        {
            return;
        }

        const rendering::Texture *texture = app.texture_manager().get( sky_texture_ );

        if( texture == nullptr || !texture->valid() )
        {
            return;
        }

        glDisable( GL_DEPTH_TEST );
        glDepthMask( GL_FALSE );
        glDisable( GL_CULL_FACE );
        glEnable( GL_BLEND );
        glBlendEquation( GL_FUNC_ADD );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

        renderer.draw_texture( *texture,
            0.0f,
            0.0f,
            static_cast<float>( width ),
            static_cast<float>( height ),
            rendering::Color{ 1.0f, 1.0f, 1.0f, 1.0f } );

        glDepthMask( GL_TRUE );
    }

    filetypes::xg::Matrix Stage3DRenderer::make_camera_view_matrix( const filetypes::ssq::Camera &camera ) const
    {
        filetypes::xg::Quat rotation = camera.rotation;

        if( camera_rotation_mode_ == CameraRotationMode::ZReflectInverseOpenGlForward ||
            camera_rotation_mode_ == CameraRotationMode::XReflectInverseOpenGlForward ||
            camera_rotation_mode_ == CameraRotationMode::YReflectInverseOpenGlForward ||
            camera_rotation_mode_ == CameraRotationMode::RawInverseOpenGlForward )
        {
            rotation = stage3d::conjugate_quat( rotation );
        }

        filetypes::xg::Matrix camera_world =
            filetypes::xg::make_trs( camera.position, rotation, filetypes::xg::Vec3{ 1.0f, 1.0f, 1.0f } );

        if( std::abs( camera_debug_local_offset_.x ) > 0.0001f || std::abs( camera_debug_local_offset_.y ) > 0.0001f ||
            std::abs( camera_debug_local_offset_.z ) > 0.0001f )
        {
            const filetypes::xg::Matrix local_offset = filetypes::xg::make_trs( camera_debug_local_offset_,
                filetypes::xg::Quat{},
                filetypes::xg::Vec3{ 1.0f, 1.0f, 1.0f } );

            camera_world = filetypes::xg::multiply( camera_world, local_offset );
        }

        return stage3d::transpose_rotation_inverse_rigid( camera_world );
    }

    float Stage3DRenderer::effective_projection_near( const filetypes::ssq::Camera &camera ) const
    {
        return std::max( std::max( camera.z_near, minimum_projection_near_ ), 0.01f );
    }

    filetypes::xg::Matrix Stage3DRenderer::make_projection_matrix( std::int32_t width,
        std::int32_t height,
        const filetypes::ssq::Camera &camera ) const
    {
        return depth_mode_ == DepthMode::ReverseZ ? make_reverse_z_projection_matrix( width, height, camera )
                                                  : make_standard_projection_matrix( width, height, camera );
    }

    filetypes::xg::Matrix Stage3DRenderer::make_standard_projection_matrix( std::int32_t width,
        std::int32_t height,
        const filetypes::ssq::Camera &camera ) const
    {
        const float aspect = height > 0 ? static_cast<float>( width ) / static_cast<float>( height ) : camera.aspect;
        const float fov_radians = camera.fov_degrees * 3.14159265358979323846f / 180.0f;
        const float f = 1.0f / std::tan( fov_radians * 0.5f );
        const float z_near = effective_projection_near( camera );
        const float z_far = std::max( camera.z_far, z_near + 1.0f );

        filetypes::xg::Matrix matrix{};
        matrix.m[ 0 ] = f / aspect;

        matrix.m[ 5 ] = f;

        matrix.m[ 10 ] = -( z_far + z_near ) / ( z_far - z_near );

        matrix.m[ 11 ] = -1.0f;

        matrix.m[ 14 ] = -( 2.0f * z_far * z_near ) / ( z_far - z_near );
        return matrix;
    }

    filetypes::xg::Matrix Stage3DRenderer::make_reverse_z_projection_matrix( std::int32_t width,
        std::int32_t height,
        const filetypes::ssq::Camera &camera ) const
    {
        const float aspect = height > 0 ? static_cast<float>( width ) / static_cast<float>( height ) : camera.aspect;
        const float fov_radians = camera.fov_degrees * 3.14159265358979323846f / 180.0f;
        const float f = 1.0f / std::tan( fov_radians * 0.5f );

        const float z_near = effective_projection_near( camera );
        const float z_far = std::max( camera.z_far, z_near + 1.0f );

        filetypes::xg::Matrix matrix{};
        matrix.m[ 0 ] = f / aspect;

        matrix.m[ 5 ] = f;

        matrix.m[ 10 ] = ( z_far + z_near ) / ( z_far - z_near );

        matrix.m[ 11 ] = -1.0f;

        matrix.m[ 14 ] = ( 2.0f * z_far * z_near ) / ( z_far - z_near );
        return matrix;
    }

    filetypes::xg::Matrix Stage3DRenderer::make_mvp( const filetypes::xg::Matrix &projection,
        const filetypes::xg::Matrix &view,
        const filetypes::xg::Matrix &model ) const
    {
        return filetypes::xg::multiply( projection, filetypes::xg::multiply( view, model ) );
    }
}
