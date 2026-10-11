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
    Stage3DRenderer::~Stage3DRenderer()
    {
        shutdown_gl();
    }

    bool Stage3DRenderer::loaded() const
    {
        return loaded_;
    }

    const std::string &Stage3DRenderer::status_line() const
    {
        return status_line_;
    }

    void Stage3DRenderer::update( double delta_seconds )
    {
        const double step = std::clamp( delta_seconds, 0.0, 0.25 );
        frame_delta_seconds_ = step;

        if( frozen_action_debug_seconds_ > 0.0 )
        {
            frozen_action_debug_seconds_ = std::max( 0.0, frozen_action_debug_seconds_ - step );
        }

        if( camera_debug_pullback_seconds_ <= 0.0 )
        {
            return;
        }

        const double used = std::min( step, camera_debug_pullback_seconds_ );
        camera_debug_pullback_seconds_ -= used;

        camera_debug_local_offset_.z += static_cast<float>( used * stage3d::debug_camera_pullback_units_per_second );
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::set_stage_frame( float frame )
    {
        set_stage_timing( frame, 0, frame, 120.0f );
    }

    void Stage3DRenderer::set_stage_timing( float global_frame,
        std::size_t section_index,
        float section_frame,
        float stage_tempo )
    {
        if( !std::isfinite( global_frame ) )
        {
            global_frame = 0.0f;
        }

        if( !std::isfinite( section_frame ) )
        {
            section_frame = 0.0f;
        }

        if( !std::isfinite( stage_tempo ) || stage_tempo <= 0.0f )
        {
            stage_tempo = 120.0f;
        }

        stage_frame_ = std::max( 0.0f, global_frame );

        stage_section_index_ = section_index;

        stage_section_frame_ = std::max( 0.0f, section_frame );

        stage_tempo_ = stage_tempo;

        if( loaded_ )
        {
            refresh_debug_camera_line();
        }
    }

    void Stage3DRenderer::set_portable_song_animation_frame( float frame )
    {
        portable_song_animation_frame_ = std::isfinite( frame ) ? frame : 0.0f;
    }

    void Stage3DRenderer::set_stage_result_timing( bool active, bool won, float elapsed_frame )
    {
        stage_result_active_ = active;

        stage_result_won_ = won;

        stage_result_elapsed_frame_ = std::isfinite( elapsed_frame ) ? std::max( 0.0f, elapsed_frame ) : 0.0f;
    }

    void Stage3DRenderer::set_action_animation_input( const ActionAnimationInput &input )
    {
        action_input_ = input;

        action_input_.aim_angle_radians = stage3d::normalize_angle_radians( action_input_.aim_angle_radians );

        actor_action_inputs_[ 0 ] = action_input_;
        update_player_pose_aim( action_input_ );

        constexpr std::uint32_t aim_only_mask = 0x200u;

        if( ( action_input_.event_flags & ~aim_only_mask ) != 0 )
        {
            frozen_action_debug_input_ = action_input_;

            frozen_action_debug_seconds_ = 1.50;
        }
    }

    void Stage3DRenderer::set_actor_action_animation_input( std::uint32_t model_type,
        const ActionAnimationInput &input )
    {
        if( model_type == 0u || model_type > actor_action_inputs_.size() )
        {
            return;
        }

        ActionAnimationInput normalized = input;
        normalized.aim_angle_radians = stage3d::normalize_angle_radians( normalized.aim_angle_radians );

        actor_action_inputs_[ model_type - 1u ] = normalized;
    }

    void Stage3DRenderer::reset_action_animation_state()
    {
        for( ActionAnimationState &state : action_states_ )
        {
            state = {};
        }
    }

    void Stage3DRenderer::set_action_event_flag_override( bool enabled, std::uint32_t flags )
    {
        action_event_flag_override_enabled_ = enabled;

        action_event_flag_override_ = flags;
    }

    std::uint32_t Stage3DRenderer::effective_action_event_flags() const
    {
        return action_event_flag_override_enabled_ ? action_event_flag_override_ : action_input_.event_flags;
    }

    bool Stage3DRenderer::initialize_gl( App &app, std::string *status )
    {
        if( gl_initialized_ )
        {
            return true;
        }

        const std::filesystem::path vertex_path = app.game_data().resolve_resource_path( "shaders/stage3d.vert" );
        const std::filesystem::path fragment_path = app.game_data().resolve_resource_path( "shaders/stage3d.frag" );

        std::string shader_error;

        if( !shader_.create_from_files( vertex_path, fragment_path, &shader_error ) )
        {
            if( status != nullptr )
            {
                *status = "3D shader failed: " + shader_error + " | vertex=" + vertex_path.string() +
                          " | fragment=" + fragment_path.string();
            }

            return false;
        }

        glGenVertexArrays( 1, &vao_ );
        glGenBuffers( 1, &vbo_ );
        glGenBuffers( 1, &ebo_ );

        if( vao_ == 0 || vbo_ == 0 || ebo_ == 0 )
        {
            if( status != nullptr )
            {
                *status = "3D renderer failed to create GL buffers";
            }

            shutdown_gl();
            return false;
        }

        glBindVertexArray( vao_ );
        glBindBuffer( GL_ARRAY_BUFFER, vbo_ );
        glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, ebo_ );
        glEnableVertexAttribArray( 0 );
        glVertexAttribPointer( 0,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof( GpuVertex ),
            reinterpret_cast<void *>( offsetof( GpuVertex, px ) ) );

        glEnableVertexAttribArray( 1 );
        glVertexAttribPointer( 1,
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof( GpuVertex ),
            reinterpret_cast<void *>( offsetof( GpuVertex, u ) ) );

        glEnableVertexAttribArray( 2 );
        glVertexAttribPointer( 2,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof( GpuVertex ),
            reinterpret_cast<void *>( offsetof( GpuVertex, r ) ) );

        glEnableVertexAttribArray( 3 );
        glVertexAttribPointer( 3,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof( GpuVertex ),
            reinterpret_cast<void *>( offsetof( GpuVertex, nx ) ) );

        glEnableVertexAttribArray( 4 );
        glVertexAttribPointer( 4,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof( GpuVertex ),
            reinterpret_cast<void *>( offsetof( GpuVertex, bi0 ) ) );

        glEnableVertexAttribArray( 5 );
        glVertexAttribPointer( 5,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof( GpuVertex ),
            reinterpret_cast<void *>( offsetof( GpuVertex, bw0 ) ) );

        glBindVertexArray( 0 );
        glBindBuffer( GL_ARRAY_BUFFER, 0 );
        glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );

        gl_initialized_ = true;
        return true;
    }

    void Stage3DRenderer::configure_texture_sampling( GLuint texture_id, bool wrap_texture, std::int32_t filter_mode )
    {
        auto [ state_it, inserted ] =
            texture_sampler_states_.try_emplace( texture_id, TextureSamplerState{ wrap_texture, filter_mode } );

        TextureSamplerState &state = state_it->second;

        if( inserted || state.wrap_texture != wrap_texture )
        {
            const GLint wrap = wrap_texture ? GL_REPEAT : GL_CLAMP_TO_EDGE;
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap );
            state.wrap_texture = wrap_texture;
        }

        if( inserted || state.filter_mode != filter_mode )
        {
            switch( utils::to_enum<rendering::TextureFilterMode>( filter_mode ) )
            {
                case rendering::TextureFilterMode::Nearest:
                    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
                    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
                    break;
                case rendering::TextureFilterMode::Linear:
                    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
                    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
                    break;
                case rendering::TextureFilterMode::LinearMipmap:
                case rendering::TextureFilterMode::Anisotropic:
                    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
                    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
                    break;
            }

            state.filter_mode = filter_mode;
        }
    }

    void Stage3DRenderer::upload_static_mesh_buffers( DrawMesh &mesh )
    {
        if( mesh.vertices.empty() || mesh.has_geometry_interpolators || mesh.static_gpu_ready )
        {
            return;
        }

        glGenVertexArrays( 1, &mesh.static_vao );
        glGenBuffers( 1, &mesh.static_vbo );

        if( mesh.indexed_vertices && !mesh.triangle_indices.empty() )
        {
            glGenBuffers( 1, &mesh.static_ebo );
        }

        if( mesh.static_vao == 0 || mesh.static_vbo == 0 ||
            ( mesh.indexed_vertices && !mesh.triangle_indices.empty() && mesh.static_ebo == 0 ) )
        {
            if( mesh.static_ebo != 0 )
            {
                glDeleteBuffers( 1, &mesh.static_ebo );
            }

            if( mesh.static_vbo != 0 )
            {
                glDeleteBuffers( 1, &mesh.static_vbo );
            }

            if( mesh.static_vao != 0 )
            {
                glDeleteVertexArrays( 1, &mesh.static_vao );
            }

            mesh.static_vao = 0;

            mesh.static_vbo = 0;

            mesh.static_ebo = 0;
            return;
        }

        glBindVertexArray( mesh.static_vao );
        glBindBuffer( GL_ARRAY_BUFFER, mesh.static_vbo );
        glBufferData( GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>( sizeof( GpuVertex ) * mesh.vertices.size() ),
            mesh.vertices.data(),
            GL_STATIC_DRAW );

        glEnableVertexAttribArray( 0 );
        glVertexAttribPointer( 0,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof( GpuVertex ),
            reinterpret_cast<void *>( offsetof( GpuVertex, px ) ) );

        glEnableVertexAttribArray( 1 );
        glVertexAttribPointer( 1,
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof( GpuVertex ),
            reinterpret_cast<void *>( offsetof( GpuVertex, u ) ) );

        glEnableVertexAttribArray( 2 );
        glVertexAttribPointer( 2,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof( GpuVertex ),
            reinterpret_cast<void *>( offsetof( GpuVertex, r ) ) );

        glEnableVertexAttribArray( 3 );
        glVertexAttribPointer( 3,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof( GpuVertex ),
            reinterpret_cast<void *>( offsetof( GpuVertex, nx ) ) );

        glEnableVertexAttribArray( 4 );
        glVertexAttribPointer( 4,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof( GpuVertex ),
            reinterpret_cast<void *>( offsetof( GpuVertex, bi0 ) ) );

        glEnableVertexAttribArray( 5 );
        glVertexAttribPointer( 5,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof( GpuVertex ),
            reinterpret_cast<void *>( offsetof( GpuVertex, bw0 ) ) );

        if( mesh.static_ebo != 0 )
        {
            glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, mesh.static_ebo );
            glBufferData( GL_ELEMENT_ARRAY_BUFFER,
                static_cast<GLsizeiptr>( sizeof( std::uint32_t ) * mesh.triangle_indices.size() ),
                mesh.triangle_indices.data(),
                GL_STATIC_DRAW );
        }

        glBindVertexArray( 0 );
        glBindBuffer( GL_ARRAY_BUFFER, 0 );
        glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
        mesh.static_gpu_ready = true;
    }

    void Stage3DRenderer::release_model_buffers()
    {
        for( auto &[ name, model ] : models_ )
        {
            (void)name;

            for( DrawMesh &mesh : model.meshes )
            {
                if( mesh.static_ebo != 0 )
                {
                    glDeleteBuffers( 1, &mesh.static_ebo );
                }

                if( mesh.static_vbo != 0 )
                {
                    glDeleteBuffers( 1, &mesh.static_vbo );
                }

                if( mesh.static_vao != 0 )
                {
                    glDeleteVertexArrays( 1, &mesh.static_vao );
                }

                mesh.static_vao = 0;

                mesh.static_vbo = 0;

                mesh.static_ebo = 0;

                mesh.static_gpu_ready = false;
            }
        }
    }

    void Stage3DRenderer::shutdown_gl()
    {
        release_model_buffers();

        if( ebo_ != 0 )
        {
            glDeleteBuffers( 1, &ebo_ );
            ebo_ = 0;
        }

        if( vbo_ != 0 )
        {
            glDeleteBuffers( 1, &vbo_ );
            vbo_ = 0;
        }

        if( vao_ != 0 )
        {
            glDeleteVertexArrays( 1, &vao_ );
            vao_ = 0;
        }

        shader_.destroy();
        gl_initialized_ = false;
    }

    bool Stage3DRenderer::render( App &app,
        rendering::Renderer &renderer,
        std::int32_t target_width,
        std::int32_t target_height )
    {
        if( !loaded_ || !gl_initialized_ )
        {
            return false;
        }

        const std::int32_t width = target_width > 0 ? target_width : renderer.output_width();
        const std::int32_t height = target_height > 0 ? target_height : renderer.output_height();

        if( width <= 0 || height <= 0 )
        {
            return false;
        }

        const float current_stage_frame = effective_stage_frame();
        update_effect_playback( current_stage_frame );
        update_texture_animations( current_stage_frame );
        const filetypes::ssq::Camera camera = sample_camera();
        const SceneLighting lighting = sample_scene_lighting( current_stage_frame );
        const filetypes::xg::Matrix projection = make_projection_matrix( width, height, camera );
        const filetypes::xg::Matrix view = make_camera_view_matrix( camera );

        glViewport( 0, 0, width, height );
        draw_stage_sky( app, renderer, width, height );

        glEnable( GL_DEPTH_TEST );

        if( depth_mode_ == DepthMode::ReverseZ )
        {
            glClearDepth( 0.0 );
            glDepthFunc( GL_GEQUAL );
        }
        else
        {
            glClearDepth( 1.0 );
            glDepthFunc( GL_LEQUAL );
        }

        glDepthMask( GL_TRUE );
        glClear( GL_DEPTH_BUFFER_BIT );
        glEnable( GL_CULL_FACE );
        glCullFace( GL_BACK );
        glFrontFace( GL_CCW );

        shader_.use();
        shader_.set_int( "u_texture", 0 );

        const GLint mvp_location = glGetUniformLocation( shader_.program(), "u_mvp" );
        const GLint mesh_matrix_location = glGetUniformLocation( shader_.program(), "u_mesh_matrix" );
        const GLint normal_matrix_location = glGetUniformLocation( shader_.program(), "u_normal_matrix" );
        const GLint gpu_skinning_location = glGetUniformLocation( shader_.program(), "u_gpu_skinning" );
        const GLint skin_matrices_location = glGetUniformLocation( shader_.program(), "u_skin_matrices[0]" );
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
        const GLint scene_ambient_location = glGetUniformLocation( shader_.program(), "u_scene_ambient" );
        const GLint scene_diffuse_location = glGetUniformLocation( shader_.program(), "u_scene_diffuse" );
        const GLint light_direction_location = glGetUniformLocation( shader_.program(), "u_light_direction" );
        const GLint env_view_rotation_location = glGetUniformLocation( shader_.program(), "u_env_view_rotation" );
        const GLint model_view_location = glGetUniformLocation( shader_.program(), "u_model_view" );
        const GLint gmlt_light_count_location = glGetUniformLocation( shader_.program(), "u_gmlt_light_count" );
        const GLint gmlt_light_direction_location =
            glGetUniformLocation( shader_.program(), "u_gmlt_light_direction[0]" );

        const GLint gmlt_light_color_location = glGetUniformLocation( shader_.program(), "u_gmlt_light_color[0]" );
        const GLint gmlt_light_secondary_location =
            glGetUniformLocation( shader_.program(), "u_gmlt_light_secondary[0]" );

        const GLint scene_lighting_strength_location =
            glGetUniformLocation( shader_.program(), "u_scene_lighting_strength" );

        const GLint scene_lighting_floor_location = glGetUniformLocation( shader_.program(), "u_scene_lighting_floor" );
        const GLint gmlt_light_strength_location = glGetUniformLocation( shader_.program(), "u_gmlt_light_strength" );
        const GLint ps2_light_wrap_location = glGetUniformLocation( shader_.program(), "u_ps2_light_wrap" );
        const GLint lighting_backend_location = glGetUniformLocation( shader_.program(), "u_lighting_backend" );
        const GLint cel_shadow_threshold_location = glGetUniformLocation( shader_.program(), "u_cel_shadow_threshold" );
        const GLint cel_shadow_strength_location = glGetUniformLocation( shader_.program(), "u_cel_shadow_strength" );
        const GLint cel_edge_strength_location = glGetUniformLocation( shader_.program(), "u_cel_edge_strength" );
        const filetypes::xg::Matrix identity_mesh_matrix = filetypes::xg::identity_matrix();
        const filetypes::xg::Vec3 light_direction =
            filetypes::xg::normalize( filetypes::xg::Vec3{ -0.35f, 0.75f, 0.55f } );

        if( scene_ambient_location >= 0 )
        {
            glUniform3f( scene_ambient_location,
                piko::utils::clamp01( lighting.ambient.r ),
                piko::utils::clamp01( lighting.ambient.g ),
                piko::utils::clamp01( lighting.ambient.b ) );
        }

        if( scene_diffuse_location >= 0 )
        {
            glUniform3f( scene_diffuse_location,
                piko::utils::clamp01( lighting.diffuse.r ),
                piko::utils::clamp01( lighting.diffuse.g ),
                piko::utils::clamp01( lighting.diffuse.b ) );
        }

        if( light_direction_location >= 0 )
        {
            glUniform3f( light_direction_location, light_direction.x, light_direction.y, light_direction.z );
        }

        if( env_view_rotation_location >= 0 )
        {
            const std::array<float, 9> view_rotation{
                view.m[ 0 ],
                view.m[ 1 ],
                view.m[ 2 ],
                view.m[ 4 ],
                view.m[ 5 ],
                view.m[ 6 ],
                view.m[ 8 ],
                view.m[ 9 ],
                view.m[ 10 ]
            };

            glUniformMatrix3fv( env_view_rotation_location, 1, GL_FALSE, view_rotation.data() );
        }

        if( gmlt_light_count_location >= 0 )
        {
            glUniform1i( gmlt_light_count_location, static_cast<std::int32_t>( lighting.gmlt_light_count ) );
        }

        if( gmlt_light_direction_location >= 0 )
        {
            std::array<float, 24> values{};

            for( std::size_t i = 0; i < lighting.gmlt_light_count && i < 8; ++i )
            {
                values[ i * 3 + 0 ] = lighting.gmlt_lights[ i ].direction.x;

                values[ i * 3 + 1 ] = lighting.gmlt_lights[ i ].direction.y;

                values[ i * 3 + 2 ] = lighting.gmlt_lights[ i ].direction.z;
            }

            glUniform3fv( gmlt_light_direction_location, 8, values.data() );
        }

        if( gmlt_light_color_location >= 0 )
        {
            std::array<float, 24> values{};

            for( std::size_t i = 0; i < lighting.gmlt_light_count && i < 8; ++i )
            {
                values[ i * 3 + 0 ] = lighting.gmlt_lights[ i ].primary.r;

                values[ i * 3 + 1 ] = lighting.gmlt_lights[ i ].primary.g;

                values[ i * 3 + 2 ] = lighting.gmlt_lights[ i ].primary.b;
            }

            glUniform3fv( gmlt_light_color_location, 8, values.data() );
        }

        if( gmlt_light_secondary_location >= 0 )
        {
            std::array<float, 24> values{};

            for( std::size_t i = 0; i < lighting.gmlt_light_count && i < 8; ++i )
            {
                values[ i * 3 + 0 ] = lighting.gmlt_lights[ i ].secondary.r;

                values[ i * 3 + 1 ] = lighting.gmlt_lights[ i ].secondary.g;

                values[ i * 3 + 2 ] = lighting.gmlt_lights[ i ].secondary.b;
            }

            glUniform3fv( gmlt_light_secondary_location, 8, values.data() );
        }

        if( scene_lighting_strength_location >= 0 )
        {
            glUniform1f( scene_lighting_strength_location, scene_lighting_strength_ );
        }

        if( scene_lighting_floor_location >= 0 )
        {
            glUniform1f( scene_lighting_floor_location, scene_lighting_floor_ );
        }

        if( gmlt_light_strength_location >= 0 )
        {
            glUniform1f( gmlt_light_strength_location, gmlt_light_strength_ );
        }

        if( ps2_light_wrap_location >= 0 )
        {
            glUniform1f( ps2_light_wrap_location, ps2_light_wrap_ );
        }

        if( lighting_backend_location >= 0 )
        {
            glUniform1i( lighting_backend_location, utils::to_int( lighting_backend_ ) );
        }

        if( cel_shadow_threshold_location >= 0 )
        {
            glUniform1f( cel_shadow_threshold_location, cel_shadow_threshold_ );
        }

        if( cel_shadow_strength_location >= 0 )
        {
            glUniform1f( cel_shadow_strength_location, cel_shadow_strength_ );
        }

        if( cel_edge_strength_location >= 0 )
        {
            glUniform1f( cel_edge_strength_location, cel_edge_strength_ );
        }

        glBindVertexArray( vao_ );
        glBindBuffer( GL_ARRAY_BUFFER, vbo_ );

        struct PreparedRenderInstance
        {
            const SceneInstance *instance = nullptr;
            filetypes::xg::Matrix model_transform{};
            filetypes::xg::Matrix mvp{};
            ResolvedAnimation animation{};
            bool special_effect_model = false;
            bool gpu_skin_instance = false;
            std::unordered_map<std::uint64_t, std::vector<filetypes::xg::Matrix>> skin_matrices;
            std::unordered_map<std::int32_t, filetypes::xg::Matrix> mesh_matrices;
        };

        std::vector<PreparedRenderInstance> prepared_instances;
        prepared_instances.reserve( instances_.size() + 5u );
        const filetypes::xg::Matrix basis_matrix = model_basis_matrix();

        std::vector<filetypes::xg::Matrix> snake_chain_transforms;
        filetypes::xg::Matrix snake_end_transform{};
        bool snake_linkage_valid = false;

        for( const filetypes::ssq::ModelEntry &entry : scene_.models )
        {
            if( entry.model_type != 9u )
            {
                continue;
            }

            snake_linkage_valid =
                make_snake_linked_transforms( entry, current_stage_frame, snake_chain_transforms, snake_end_transform );

            if( snake_linkage_valid )
            {
                break;
            }
        }

        const auto append_prepared = [ & ]( const SceneInstance &instance,
                                         const filetypes::ssq::ModelEntry &source_entry,
                                         const filetypes::xg::Matrix &instance_transform )
        {
            PreparedRenderInstance prepared{};
            prepared.instance = &instance;

            prepared.model_transform = filetypes::xg::multiply( instance_transform, basis_matrix );

            prepared.mvp = make_mvp( projection, view, prepared.model_transform );

            prepared.animation = resolve_animation_for_instance( *instance.model,
                source_entry,
                instance.model_setup_index,
                current_stage_frame );

            prepared.special_effect_model = source_entry.model_type >= 5u && source_entry.model_type <= 8u;

            prepared.gpu_skin_instance =
                gpu_skinning_enabled() && instance.model->has_envelopes && !instance.model->gpu_skin_bones.empty();

            prepared_instances.push_back( std::move( prepared ) );
        };

        for( const SceneInstance &instance : instances_ )
        {
            if( instance.model == nullptr || instance.model_setup_index >= scene_.models.size() )
            {
                continue;
            }

            const filetypes::ssq::ModelEntry &source_entry = scene_.models[ instance.model_setup_index ];
            filetypes::ssq::ModelEntry sampled_entry = sample_model_entry( source_entry, current_stage_frame );

            if( source_entry.model_type >= 5u && source_entry.model_type <= 8u )
            {
                const filetypes::ssq::ModelAnimKey *effect_key =
                    active_model_anim_key( source_entry, current_stage_frame );

                if( effect_key != nullptr && effect_key->state_driven )
                {
                    sampled_entry.visible = instance.model_setup_index < effect_states_.size() &&
                                            effect_states_[ instance.model_setup_index ].active;
                }
            }

            if( !sampled_entry.visible )
            {
                continue;
            }

            if( !render_special_model_types_ && instance.model_type != 0 )
            {
                continue;
            }

            if( source_entry.model_type == 9u && snake_linkage_valid )
            {
                for( const filetypes::xg::Matrix &segment_transform : snake_chain_transforms )
                {
                    append_prepared( instance, source_entry, segment_transform );
                }
                continue;
            }

            filetypes::xg::Matrix instance_transform = make_model_transform( sampled_entry );

            if( source_entry.model_type >= 5u && source_entry.model_type <= 8u )
            {
                (void)make_linked_effect_transform( source_entry, current_stage_frame, instance_transform );
            }
            else if( snake_linkage_valid && piko::utils::lowercase_filename( source_entry.xg_name ) == "woofa_b.xg" )
            {
                instance_transform = snake_end_transform;
            }

            append_prepared( instance, source_entry, instance_transform );
        }

        const auto draw_pass = [ & ]( bool alpha_pass )
        {
            for( PreparedRenderInstance &prepared : prepared_instances )
            {
                const SceneInstance &instance = *prepared.instance;
                const filetypes::xg::Matrix &model_transform = prepared.model_transform;
                const ResolvedAnimation &resolved_animation = prepared.animation;

                if( scene_lighting_floor_location >= 0 )
                {
                    const bool loss_actor = stage_result_active_ && !stage_result_won_ &&
                                            ( instance.model_type >= 1u && instance.model_type <= 4u );

                    const float loss_floor =
                        std::clamp( ( stage_result_elapsed_frame_ - 60.0f ) / 45.0f, 0.0f, 1.0f ) * 0.68f;

                    glUniform1f( scene_lighting_floor_location,
                        loss_actor ? std::max( scene_lighting_floor_, loss_floor ) : scene_lighting_floor_ );
                }

                if( prepared.special_effect_model )
                {
                    glDisable( GL_CULL_FACE );
                }
                else
                {
                    glEnable( GL_CULL_FACE );
                }

                if( mvp_location >= 0 )
                {
                    glUniformMatrix4fv( mvp_location, 1, GL_FALSE, prepared.mvp.m );
                }

                if( model_view_location >= 0 )
                {
                    const filetypes::xg::Matrix model_view = filetypes::xg::multiply( view, model_transform );
                    glUniformMatrix4fv( model_view_location, 1, GL_FALSE, model_view.m );
                }

                const bool gpu_skin_instance = prepared.gpu_skin_instance;

                for( const DrawMesh &mesh : instance.model->meshes )
                {
                    if( mesh.vertex_count == 0u )
                    {
                        continue;
                    }

                    if( material_debug_focus_selected_ && mesh.debug_id != selected_material_debug_id_ )
                    {
                        continue;
                    }

                    if( mesh.is_multipass )
                    {
                        if( material_debug_mode_ == MaterialDebugMode::MultipassBase )
                        {
                            if( mesh.multipass_layer_index != 0 )
                            {
                                continue;
                            }
                        }
                        else if( material_debug_mode_ != MaterialDebugMode::MultipassAll )
                        {
                            if( !mesh.multipass_default_visible )
                            {
                                continue;
                            }
                        }
                    }

                    if( material_debug_mode_ == MaterialDebugMode::Normal )
                    {
                        if( mesh.uses_alpha != alpha_pass )
                        {
                            continue;
                        }
                    }
                    else if( material_debug_mode_ == MaterialDebugMode::OpaqueOnly )
                    {
                        if( alpha_pass || mesh.uses_alpha )
                        {
                            continue;
                        }
                    }
                    else if( material_debug_mode_ == MaterialDebugMode::AlphaOnly )
                    {
                        if( !alpha_pass || !mesh.uses_alpha )
                        {
                            continue;
                        }
                    }
                    else if( mesh.uses_alpha != alpha_pass )
                    {
                        continue;
                    }

                    const bool alpha_active = stage3d::material_alpha_is_active( mesh.blend_type, mesh.flags );

                    if( alpha_pass )
                    {
                        glEnable( GL_BLEND );
                        glDepthMask( GL_FALSE );
                        glEnable( GL_DEPTH_TEST );

                        switch( mesh.blend_type )
                        {
                            case 1u:
                                glBlendEquation( GL_FUNC_ADD );
                                glBlendFunc( GL_SRC_ALPHA, GL_ONE );
                                break;
                            case 2u:
                                glBlendEquation( GL_FUNC_SUBTRACT );
                                glBlendFunc( GL_ONE, GL_ONE );
                                break;
                            case 3u:
                                glBlendEquation( GL_FUNC_REVERSE_SUBTRACT );
                                glBlendFunc( GL_ONE, GL_ONE );
                                break;
                            case 4u:
                                glBlendEquation( GL_FUNC_ADD );

                                if( alpha_active )
                                {
                                    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
                                }
                                else
                                {
                                    glBlendFunc( GL_ZERO, GL_SRC_COLOR );
                                }
                                break;
                            case 5u:
                            default:
                                glBlendEquation( GL_FUNC_ADD );
                                glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
                                break;
                        }
                    }
                    else
                    {
                        glDisable( GL_BLEND );
                        glBlendEquation( GL_FUNC_ADD );
                        glDepthMask( GL_TRUE );
                    }

                    const rendering::Texture *texture =
                        mesh.texture.valid() ? app.texture_manager().get( mesh.texture ) : nullptr;

                    const bool use_texture = texture != nullptr && texture->valid();

                    if( use_texture_location >= 0 )
                    {
                        glUniform1i( use_texture_location, use_texture ? 1 : 0 );
                    }

                    if( shading_type_location >= 0 )
                    {
                        glUniform1i( shading_type_location, static_cast<std::int32_t>( mesh.shading_type ) );
                    }

                    if( blend_type_location >= 0 )
                    {
                        glUniform1i( blend_type_location, static_cast<std::int32_t>( mesh.blend_type ) );
                    }

                    if( alpha_active_location >= 0 )
                    {
                        glUniform1i( alpha_active_location, alpha_active ? 1 : 0 );
                    }

                    if( alpha_cutoff_location >= 0 )
                    {
                        glUniform1f( alpha_cutoff_location,
                            alpha_active ? stage3d::material_alpha_cutoff( mesh.blend_type, mesh.flags ) : 0.01f );
                    }

                    if( alpha_key_mode_location >= 0 )
                    {
                        glUniform1i( alpha_key_mode_location, 0 );
                    }

                    if( effect_alpha_as_luma_location >= 0 )
                    {
                        glUniform1i( effect_alpha_as_luma_location, 0 );
                    }

                    if( material_diffuse_location >= 0 )
                    {
                        glUniform4f( material_diffuse_location,
                            mesh.diffuse.r,
                            mesh.diffuse.g,
                            mesh.diffuse.b,
                            mesh.diffuse.a );
                    }

                    if( texture_env_location >= 0 )
                    {
                        const std::int32_t texture_env = material_debug_mode_ == MaterialDebugMode::NoEnvMap
                                                             ? 0
                                                             : static_cast<std::int32_t>( mesh.texture_env );

                        glUniform1i( texture_env_location, texture_env );
                    }

                    if( debug_mode_location >= 0 )
                    {
                        std::int32_t debug_mode = 0;

                        if( material_debug_mode_ == MaterialDebugMode::TextureOnly )
                        {
                            debug_mode = 1;
                        }
                        else if( material_debug_mode_ == MaterialDebugMode::ColorOnly )
                        {
                            debug_mode = 2;
                        }
                        else if( material_debug_mode_ == MaterialDebugMode::AlphaOnly ||
                                 material_debug_mode_ == MaterialDebugMode::AlphaMask )
                        {
                            debug_mode = 3;
                        }

                        glUniform1i( debug_mode_location, debug_mode );
                    }

                    glActiveTexture( GL_TEXTURE0 );
                    glBindTexture( GL_TEXTURE_2D, use_texture ? static_cast<GLuint>( texture->id ) : 0u );

                    if( use_texture )
                    {
                        configure_texture_sampling( static_cast<GLuint>( texture->id ),
                            mesh.wrap_texture,
                            static_cast<std::int32_t>( renderer.texture_sampling().mode ) );
                    }

                    const bool native_gltf_mesh = instance.model->gltf_asset != nullptr;
                    const bool use_gpu_skin_for_mesh =
                        gpu_skin_instance && mesh.gpu_skin_ready &&
                        ( native_gltf_mesh ? mesh.vertex_count != 0u : !mesh.gpu_skin_vertices.empty() ) &&
                        !mesh.has_geometry_interpolators;

                    if( use_gpu_skin_for_mesh && skin_matrices_location >= 0 )
                    {
                        const std::uint64_t skin_key =
                            native_gltf_mesh
                                ? ( static_cast<std::uint64_t>( static_cast<std::uint32_t>( mesh.gltf_skin_index ) )
                                      << 32u ) |
                                      static_cast<std::uint32_t>( mesh.gltf_node_index )
                                : std::numeric_limits<std::uint64_t>::max();

                        auto [ skin_it, inserted ] = prepared.skin_matrices.try_emplace( skin_key );

                        if( inserted )
                        {
                            skin_it->second = build_gpu_skin_matrices( *instance.model, mesh, resolved_animation );
                        }

                        const std::vector<filetypes::xg::Matrix> &gpu_skin_matrices = skin_it->second;

                        if( !gpu_skin_matrices.empty() )
                        {
                            const GLsizei count = static_cast<GLsizei>(
                                std::min<std::size_t>( gpu_skin_matrices.size(), stage3d::max_gpu_skin_bones ) );

                            glUniformMatrix4fv( skin_matrices_location, count, GL_FALSE, gpu_skin_matrices.front().m );
                        }
                    }

                    filetypes::xg::Matrix runtime_mesh_matrix = identity_mesh_matrix;

                    if( native_gltf_mesh )
                    {
                        auto [ matrix_it, inserted ] = prepared.mesh_matrices.try_emplace( mesh.gltf_node_index );

                        if( inserted )
                        {
                            matrix_it->second = evaluate_mesh_local_matrix( *instance.model, mesh, resolved_animation );
                        }

                        runtime_mesh_matrix = matrix_it->second;
                    }
                    else if( use_gpu_skin_for_mesh )
                    {
                        runtime_mesh_matrix = evaluate_mesh_local_matrix( *instance.model, mesh, resolved_animation );
                    }

                    const bool needs_dynamic_vertices =
                        native_gltf_mesh
                            ? mesh.has_geometry_interpolators
                            : ( !use_gpu_skin_for_mesh &&
                                  ( mesh.has_geometry_interpolators ||
                                      ( xg_rigid_animation_enabled() &&
                                          ( instance.model->has_dynamic_matrices ||
                                              ( ( cpu_skinning_enabled() ||
                                                    ( gpu_skinning_enabled() && mesh.has_geometry_interpolators ) ) &&
                                                  instance.model->has_envelopes ) ) ) ) );

                    filetypes::xg::Matrix baked_local_matrix = mesh.static_local_matrix;

                    if( needs_dynamic_vertices && !mesh.matrix_chain.empty() )
                    {
                        baked_local_matrix = evaluate_mesh_local_matrix( *instance.model, mesh, resolved_animation );
                    }

                    const filetypes::xg::Matrix winding_matrix = filetypes::xg::multiply( model_transform,
                        use_gpu_skin_for_mesh || native_gltf_mesh ? runtime_mesh_matrix : baked_local_matrix );

                    stage3d::set_xg_cull_state(
                        stage3d::cull_func_after_winding_transform( mesh.cull_func, winding_matrix ) );

                    if( gpu_skinning_location >= 0 )
                    {
                        glUniform1i( gpu_skinning_location, use_gpu_skin_for_mesh ? 1 : 0 );
                    }

                    if( mesh_matrix_location >= 0 )
                    {
                        glUniformMatrix4fv( mesh_matrix_location, 1, GL_FALSE, runtime_mesh_matrix.m );
                    }

                    if( normal_matrix_location >= 0 )
                    {
                        const filetypes::xg::Matrix normal_source_matrix =
                            use_gpu_skin_for_mesh || native_gltf_mesh
                                ? filetypes::xg::multiply( model_transform, runtime_mesh_matrix )
                                : model_transform;

                        const std::array<float, 9> normal_matrix =
                            stage3d::normal_matrix3_from_model( normal_source_matrix );

                        glUniformMatrix3fv( normal_matrix_location, 1, GL_FALSE, normal_matrix.data() );
                    }

                    const std::vector<GpuVertex> *draw_vertices =
                        use_gpu_skin_for_mesh && !native_gltf_mesh ? &mesh.gpu_skin_vertices : &mesh.vertices;

                    std::vector<GpuVertex> dynamic_vertices;

                    if( needs_dynamic_vertices )
                    {
                        dynamic_vertices = build_mesh_vertices( *instance.model, mesh, resolved_animation );

                        if( !dynamic_vertices.empty() )
                        {
                            draw_vertices = &dynamic_vertices;
                        }
                    }

                    const bool static_draw = mesh.static_gpu_ready && !needs_dynamic_vertices;

                    if( !static_draw && draw_vertices->empty() )
                    {
                        continue;
                    }

                    const bool indexed_draw = mesh.indexed_vertices &&
                                              ( static_draw ? mesh.index_count != 0u : !mesh.triangle_indices.empty() );

                    if( static_draw )
                    {
                        glBindVertexArray( mesh.static_vao );

                        if( indexed_draw )
                        {
                            glDrawElements( GL_TRIANGLES,
                                static_cast<GLsizei>( mesh.index_count ),
                                GL_UNSIGNED_INT,
                                nullptr );
                        }
                        else
                        {
                            glDrawArrays( GL_TRIANGLES, 0, static_cast<GLsizei>( mesh.vertex_count ) );
                        }
                    }
                    else
                    {
                        glBindVertexArray( vao_ );
                        glBindBuffer( GL_ARRAY_BUFFER, vbo_ );
                        glBufferData( GL_ARRAY_BUFFER,
                            static_cast<GLsizeiptr>( sizeof( GpuVertex ) * draw_vertices->size() ),
                            draw_vertices->data(),
                            needs_dynamic_vertices ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW );

                        if( indexed_draw )
                        {
                            glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, ebo_ );
                            glBufferData( GL_ELEMENT_ARRAY_BUFFER,
                                static_cast<GLsizeiptr>( sizeof( std::uint32_t ) * mesh.triangle_indices.size() ),
                                mesh.triangle_indices.data(),
                                GL_STATIC_DRAW );

                            glDrawElements( GL_TRIANGLES,
                                static_cast<GLsizei>( mesh.triangle_indices.size() ),
                                GL_UNSIGNED_INT,
                                nullptr );
                        }
                        else
                        {
                            glDrawArrays( GL_TRIANGLES, 0, static_cast<GLsizei>( draw_vertices->size() ) );
                        }
                    }
                }
            }
        };

        draw_pass( false );
        draw_pass( true );
        draw_fixed_sprites( app, renderer, projection, view, current_stage_frame );

        glBindBuffer( GL_ARRAY_BUFFER, 0 );
        glBindVertexArray( 0 );
        glBindTexture( GL_TEXTURE_2D, 0 );
        glDepthMask( GL_TRUE );
        glClearDepth( 1.0 );
        glDepthFunc( GL_LEQUAL );
        glDisable( GL_DEPTH_TEST );
        glDisable( GL_CULL_FACE );
        glEnable( GL_BLEND );
        glBlendEquation( GL_FUNC_ADD );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
        return true;
    }
}
