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
    std::string Stage3DRenderer::normalize_texture_name( std::string text )
    {
        text = piko::utils::lowercase_filename( std::move( text ) );
        const std::size_t dot = text.find_last_of( '.' );

        if( dot != std::string::npos )
        {
            text.erase( dot );
        }

        return text;
    }

    std::string Stage3DRenderer::normalize_model_name( std::string text )
    {
        text = piko::utils::lowercase_filename( std::move( text ) );
        const std::size_t dot = text.find_last_of( '.' );

        if( dot != std::string::npos )
        {
            text.erase( dot );
        }

        return text;
    }

    void Stage3DRenderer::unload( App &app )
    {
        for( managers::TextureHandle handle : texture_handles_ )
        {
            app.texture_manager().unload( handle );
        }

        texture_handles_.clear();
        texture_by_name_.clear();
        texture_sampler_states_.clear();
        runtime_texture_animations_.clear();
        runtime_fixed_sprites_.clear();
        release_model_buffers();
        models_.clear();
        instances_.clear();
        action_states_.clear();
        effect_states_.clear();
        next_effect_activation_serial_ = 1;

        action_input_ = {};
        actor_action_inputs_.fill( ActionAnimationInput{} );
        scene_ = {};

        camera_debug_local_offset_ = {};

        camera_debug_pullback_seconds_ = 0.0;

        stage_frame_ = 0.0f;

        stage_section_index_ = 0;

        stage_section_frame_ = 0.0f;

        stage_tempo_ = 120.0f;

        portable_song_animation_frame_ = 0.0f;

        portable_visuals_ = false;

        stage_result_active_ = false;

        stage_result_won_ = true;

        stage_result_elapsed_frame_ = 0.0f;

        win_a_range_ = {};

        win_b_range_ = {};

        lose_pre_range_ = {};

        lose_post_range_ = {};

        camera_frame_offset_ = 0.0f;
        camera_ranges_.clear();
        debug_camera_line_.clear();
        material_debug_status_line_.clear();
        selected_material_debug_id_ = 0;

        next_material_debug_id_ = 0;

        texture_debug_overlay_visible_ = false;

        selected_texture_debug_index_ = 0;

        texture_debug_view_ = TextureDebugView::Normal;

        stage_number_ = -1;

        stage_show_sky_flag_ = false;

        debug_stage_sky_enabled_ = true;

        sky_texture_ = {};
        sky_texture_name_.clear();
        loaded_ = false;
        status_line_.clear();
    }

    bool Stage3DRenderer::load( App &app, const gamedata::PlayableStageEntry &stage, std::string *status )
    {
        unload( app );

        const bool portable_visuals = stage.portable_visuals;
        portable_visuals_ = portable_visuals;

        std::error_code model_root_error{};
        const bool runtime_models_available =
            !stage.runtime_model_root.empty() &&
            std::filesystem::is_directory( stage.runtime_model_root, model_root_error );

        if( !stage.ssq_exists || !runtime_models_available )
        {
            status_line_ = portable_visuals ? "3D stage unavailable: missing portable-stage JSON/glTF assets"
                                            : "3D stage unavailable: missing converted SSQ JSON or glTF models";

            if( status != nullptr )
            {
                *status = status_line_;
            }

            return false;
        }

        if( !initialize_gl( app, status ) )
        {
            return false;
        }

        try
        {
            stage_number_ = stage.stage_number;

            gmlt_light_strength_ = default_gmlt_light_strength();

            stage_tempo_ = std::isfinite( stage.tempo ) && stage.tempo > 0.0f ? stage.tempo : 120.0f;

            stage_show_sky_flag_ = stage.show_sky;

            debug_stage_sky_enabled_ = true;

            sky_texture_ = {};
            sky_texture_name_.clear();
            next_material_debug_id_ = 0;
            refresh_material_debug_status_line();
            scene_ = filetypes::ssq::load_json( stage.ssq_path );
            build_camera_presetup_ranges( stage );

            std::unordered_map<std::string, std::filesystem::path> normalized_texture_paths;
            char stage_group[ 16 ]{};
            std::snprintf( stage_group, sizeof( stage_group ), "STAGE%02d", stage_number_ );
            const std::filesystem::path normalized_texture_root =
                !stage.runtime_model_root.empty()
                    ? stage.runtime_model_root.parent_path() / "TEXTURES"
                    : app.game_data().resolve_runtime_asset(
                          ( std::filesystem::path( stage_group ) / "TEXTURES" ).generic_string() );

            std::error_code texture_error{};

            if( std::filesystem::is_directory( normalized_texture_root, texture_error ) )
            {
                for( const std::filesystem::directory_entry &entry :
                    std::filesystem::recursive_directory_iterator( normalized_texture_root, texture_error ) )
                {
                    if( !entry.is_regular_file() )
                    {
                        continue;
                    }

                    const std::string extension = piko::utils::uppercase_ascii( entry.path().extension().string() );

                    if( extension != ".PNG" )
                    {
                        continue;
                    }

                    const std::string relative =
                        entry.path().lexically_relative( normalized_texture_root ).generic_string();

                    const std::string manager_key = "stage3d/png/" + std::to_string( stage_number_ ) + "/" + relative;

                    try
                    {
                        const managers::TextureHandle handle =
                            app.texture_manager().load_png( manager_key, entry.path() );

                        texture_handles_.push_back( handle );

                        const std::string texture_key = normalize_texture_name( entry.path().filename().string() );
                        const std::string png_key = piko::utils::lowercase_filename( entry.path().filename().string() );
                        texture_by_name_[ texture_key ] = handle;

                        texture_by_name_[ png_key ] = handle;

                        normalized_texture_paths[ texture_key ] = entry.path();

                        normalized_texture_paths[ png_key ] = entry.path();
                    }
                    catch( const std::exception & )
                    {
                    }
                }
            }

            for( std::size_t animation_index = 0; animation_index < scene_.texture_animations.size();
                ++animation_index )
            {
                const filetypes::ssq::TextureAnimation &animation = scene_.texture_animations[ animation_index ];
                const std::string key = normalize_texture_name( animation.texture_name );

                const auto handle_it = texture_by_name_.find( key );

                if( handle_it == texture_by_name_.end() )
                {
                    continue;
                }

                RuntimeTextureAnimation runtime_animation{};
                runtime_animation.scene_animation_index = animation_index;

                runtime_animation.texture = handle_it->second;

                if( const rendering::Texture *loaded_texture = app.texture_manager().get( handle_it->second ) )
                {
                    runtime_animation.texture_id = loaded_texture->id;
                }

                bool source_loaded = false;

                const auto path_it = normalized_texture_paths.find( key );

                if( path_it != normalized_texture_paths.end() )
                {
                    SDL_Surface *loaded = SDL_LoadPNG( path_it->second.string().c_str() );

                    if( loaded != nullptr )
                    {
                        SDL_Surface *rgba = SDL_ConvertSurface( loaded, SDL_PIXELFORMAT_RGBA32 );

                        if( rgba != nullptr )
                        {
                            runtime_animation.width = rgba->w;

                            runtime_animation.height = rgba->h;
                            runtime_animation.source_rgba.resize(
                                static_cast<std::size_t>( rgba->w ) * static_cast<std::size_t>( rgba->h ) * 4u );

                            for( std::int32_t y = 0; y < rgba->h; ++y )
                            {
                                std::memcpy( runtime_animation.source_rgba.data() +
                                                 static_cast<std::size_t>( y ) * static_cast<std::size_t>( rgba->w ) *
                                                     4u,
                                    static_cast<const std::uint8_t *>( rgba->pixels ) +
                                        static_cast<std::size_t>( y ) * static_cast<std::size_t>( rgba->pitch ),
                                    static_cast<std::size_t>( rgba->w ) * 4u );
                            }

                            source_loaded = true;
                            SDL_DestroySurface( rgba );
                        }

                        SDL_DestroySurface( loaded );
                    }
                }

                if( source_loaded )
                {
                    runtime_texture_animations_.push_back( std::move( runtime_animation ) );
                }
            }

            for( std::size_t sprite_index = 0; sprite_index < scene_.fixed_sprites.size(); ++sprite_index )
            {
                const filetypes::ssq::FixedSprite &sprite = scene_.fixed_sprites[ sprite_index ];

                if( sprite.descriptor.texture_index >= scene_.imx_entries.size() )
                {
                    continue;
                }

                const std::string key = normalize_texture_name( scene_.imx_entries[ sprite.descriptor.texture_index ] );

                const auto handle_it = texture_by_name_.find( key );

                if( handle_it == texture_by_name_.end() )
                {
                    continue;
                }

                const rendering::Texture *texture = app.texture_manager().get( handle_it->second );

                if( texture == nullptr || !texture->valid() )
                {
                    continue;
                }

                RuntimeFixedSprite runtime_sprite{};
                runtime_sprite.scene_sprite_index = sprite_index;

                runtime_sprite.texture = handle_it->second;

                runtime_sprite.texture_width = texture->width;

                runtime_sprite.texture_height = texture->height;
                runtime_fixed_sprites_.push_back( runtime_sprite );
            }

            resolve_stage_sky_texture();

            std::vector<std::string> referenced_models;
            std::unordered_map<std::string, std::string> referenced_model_names;

            for( const filetypes::ssq::ModelEntry &entry : scene_.models )
            {
                const std::string key = normalize_model_name( entry.xg_name );

                if( key.empty() )
                {
                    continue;
                }

                referenced_model_names.try_emplace( key, entry.xg_name );

                if( std::find( referenced_models.begin(), referenced_models.end(), key ) == referenced_models.end() )
                {
                    referenced_models.push_back( key );
                }
            }

            std::filesystem::path primary_model_root = stage.runtime_model_root;

            if( primary_model_root.empty() )
            {
                primary_model_root = app.game_data().runtime_stage_models( stage.stage_number );
            }

            std::cout << "[Piko Runtime] Stage model branch: " << ( portable_visuals ? "portable" : "PS2" ) << '\n';
            std::cout << "[Piko Runtime] Stage model root: " << primary_model_root.string() << '\n';

            struct RuntimeGltfCandidate
            {
                std::filesystem::path path;
            };

            std::unordered_map<std::string, std::vector<RuntimeGltfCandidate>> runtime_gltf_models;

            if( !primary_model_root.empty() )
            {
                std::error_code scan_error{};
                std::filesystem::recursive_directory_iterator iterator( primary_model_root,
                    std::filesystem::directory_options::skip_permission_denied,
                    scan_error );

                const std::filesystem::recursive_directory_iterator end_iterator;
                while( iterator != end_iterator )
                {
                    if( scan_error )
                    {
                        scan_error.clear();
                        iterator.increment( scan_error );
                        continue;
                    }

                    const std::filesystem::directory_entry &entry = *iterator;

                    if( entry.is_regular_file( scan_error ) &&
                        piko::utils::lowercase_filename( entry.path().filename().string() ) == "model.gltf" )
                    {
                        auto add_runtime_model = [ & ]( const std::string &model_key )
                        {
                            if( model_key.empty() )
                            {
                                return;
                            }

                            std::vector<RuntimeGltfCandidate> &candidates = runtime_gltf_models[ model_key ];

                            const auto existing = std::find_if( candidates.begin(),
                                candidates.end(),
                                [ & ]( const RuntimeGltfCandidate &candidate )
                                {
                                    return candidate.path == entry.path();
                                } );
                            if( existing == candidates.end() )
                            {
                                candidates.push_back( RuntimeGltfCandidate{ entry.path() } );
                            }
                        };

                        const std::string directory_name = entry.path().parent_path().filename().string();
                        add_runtime_model( normalize_model_name( directory_name ) );

                        const std::size_t underscore = directory_name.find( '_' );

                        if( underscore != std::string::npos && underscore != 0u &&
                            std::all_of( directory_name.begin(),
                                directory_name.begin() + static_cast<std::ptrdiff_t>( underscore ),
                                []( unsigned char character )
                                {
                                    return std::isdigit( character ) != 0;
                                } ) )
                        {
                            add_runtime_model( normalize_model_name( directory_name.substr( underscore + 1u ) ) );
                        }

                        add_runtime_model( normalize_model_name( stage3d::gltf_source_model_name( entry.path() ) ) );
                    }

                    iterator.increment( scan_error );
                }
            }

            std::size_t native_gltf_model_count = 0;
            std::size_t psp_gltf_model_count = 0;
            std::size_t missing_model_count = 0;
            const std::vector<runtime::model::AnimationTiming> no_animations;

            for( const std::string &key : referenced_models )
            {
                const auto name_it = referenced_model_names.find( key );
                const std::string source_name = name_it != referenced_model_names.end() ? name_it->second : key;
                const std::vector<runtime::model::AnimationTiming> &animations = no_animations;

                bool loaded_native_gltf = false;

                const auto gltf_model = runtime_gltf_models.find( key );

                if( gltf_model != runtime_gltf_models.end() )
                {
                    for( const RuntimeGltfCandidate &candidate : gltf_model->second )
                    {
                        const std::filesystem::path &gltf_path = candidate.path;
                        std::error_code gltf_error{};

                        if( !std::filesystem::is_regular_file( gltf_path, gltf_error ) )
                        {
                            continue;
                        }

                        try
                        {
                            auto asset = std::make_shared<runtime::model::GltfModelAsset>(
                                runtime::model::load_gltf_model( gltf_path ) );

                            const std::string asset_key = normalize_model_name( asset->source_model_name );

                            if( !portable_visuals && !asset_key.empty() && asset_key != key )
                            {
                                continue;
                            }

                            if( asset->indexed_animations && !asset->animation_clips.empty() && !animations.empty() &&
                                asset->animation_clips.size() != animations.size() )
                            {
                                if( !portable_visuals )
                                {
                                    throw std::runtime_error(
                                        "animation clip count does not match the stage model table" );
                                }

                                std::cerr << "[Piko Runtime] portable glTF animation count differs for " << source_name
                                          << ": " << asset->animation_clips.size() << " clips, stage table has "
                                          << animations.size() << "; loading for visual inspection\n";
                            }

                            DrawModel draw_model =
                                compile_gltf_model( app, std::move( asset ), source_name, animations );

                            if( draw_model.vertex_count == 0u )
                            {
                                continue;
                            }

                            models_.emplace( key, std::move( draw_model ) );
                            loaded_native_gltf = true;
                            ++native_gltf_model_count;

                            if( portable_visuals )
                            {
                                ++psp_gltf_model_count;
                            }

                            std::cout << "[Piko Runtime] Model source: "
                                      << ( portable_visuals ? "portable glTF " : "Native glTF " ) << gltf_path.string()
                                      << '\n';
                            break;
                        }
                        catch( const std::exception &error )
                        {
                            std::cerr << "[Piko Runtime] " << ( portable_visuals ? "portable" : "Native" )
                                      << " glTF failed for " << source_name << ": " << error.what() << '\n';
                        }
                    }
                }

                if( loaded_native_gltf )
                {
                    continue;
                }

                ++missing_model_count;
                std::cerr << "[Piko Runtime] Stage model unavailable in "
                          << ( portable_visuals ? "portable glTF branch: " : "all model sources: " ) << source_name
                          << '\n';
            }

            for( std::size_t model_setup_index = 0; model_setup_index < scene_.models.size(); ++model_setup_index )
            {
                const filetypes::ssq::ModelEntry &entry = scene_.models[ model_setup_index ];
                const std::string key = normalize_model_name( entry.xg_name );

                const auto it = models_.find( key );

                if( it == models_.end() )
                {
                    continue;
                }

                SceneInstance instance{};
                instance.model = &it->second;

                instance.transform = make_model_transform( entry );

                instance.model_setup_index = model_setup_index;

                instance.name = entry.xg_name;

                instance.model_type = entry.model_type;
                instances_.push_back( std::move( instance ) );
            }

            action_states_.clear();
            action_states_.resize( scene_.models.size() );
            effect_states_.clear();
            effect_states_.resize( scene_.models.size() );
            next_effect_activation_serial_ = 1;

            std::size_t total_vertices = 0;

            for( const SceneInstance &instance : instances_ )
            {
                total_vertices += instance.model != nullptr ? instance.model->vertex_count : 0u;
            }

            std::ostringstream ss;
            ss << "3D stage: " << instances_.size() << " instances, " << models_.size() << " models, " << total_vertices
               << " verts, " << camera_ranges_.size() << " camera ranges, " << runtime_fixed_sprites_.size()
               << " fixed sprites, ";

            if( portable_visuals )
            {
                ss << psp_gltf_model_count << " portable glTF models, " << missing_model_count
                   << " missing, strict PSP branch";
            }
            else
            {
                ss << native_gltf_model_count << " glTF models, " << missing_model_count << " missing";
            }

            if( stage_show_sky_flag_ )
            {
                ss << ", sky " << ( sky_texture_name_.empty() ? "missing" : sky_texture_name_ );
            }

            status_line_ = ss.str();

            if( status != nullptr )
            {
                *status = status_line_;
            }

            loaded_ = !instances_.empty() || !runtime_fixed_sprites_.empty();
            refresh_material_debug_status_line();

            if( loaded_ )
            {
                refresh_debug_camera_line();
            }

            return loaded_;
        }
        catch( const std::exception &ex )
        {
            status_line_ = std::string( "3D stage load failed: " ) + ex.what();

            if( status != nullptr )
            {
                *status = status_line_;
            }

            unload( app );
            return false;
        }
    }

    Stage3DRenderer::DrawModel Stage3DRenderer::compile_model( const filetypes::xg::Model &model,
        const std::string &name,
        const std::vector<runtime::model::AnimationTiming> &animations )
    {
        DrawModel out{};
        out.name = name;

        out.source_model = model;

        out.animations = animations;

        out.has_dynamic_matrices = !model.vec3_interpolators.empty() || !model.quat_interpolators.empty();

        out.has_envelopes = !model.envelopes.empty();

        out.has_geometry_interpolators = !model.vertex_interpolators.empty() || !model.normal_interpolators.empty() ||
                                         !model.texcoord_interpolators.empty() || !model.shape_interpolators.empty();

        const filetypes::xg::Matrix identity = filetypes::xg::identity_matrix();
        const std::vector<std::string> chain;

        for( const filetypes::xg::DagNode &root : model.dag_nodes )
        {
            append_dag_node( model, root.name, identity, chain, out );
        }

        return out;
    }

    Stage3DRenderer::DrawModel Stage3DRenderer::compile_gltf_model( App &app,
        std::shared_ptr<runtime::model::GltfModelAsset> asset,
        const std::string &name,
        const std::vector<runtime::model::AnimationTiming> &animations )
    {
        if( asset == nullptr )
        {
            throw std::runtime_error( "glTF model asset is null" );
        }

        DrawModel out{};
        out.name = name;

        out.gltf_asset = std::move( asset );

        out.animations = animations.empty() ? out.gltf_asset->animation_timings : animations;

        out.gltf_asset->animation_timings = out.animations;

        out.has_dynamic_matrices = std::any_of( out.gltf_asset->animation_clips.begin(),
            out.gltf_asset->animation_clips.end(),
            []( const runtime::model::GltfAnimationClip &clip )
            {
                return !clip.channels.empty();
            } );
        out.has_envelopes = !out.gltf_asset->skins.empty();

        out.has_geometry_interpolators = std::any_of( out.gltf_asset->primitives.begin(),
            out.gltf_asset->primitives.end(),
            []( const runtime::model::GltfPrimitive &primitive )
            {
                return !primitive.position_track.frames.empty() || !primitive.normal_track.frames.empty() ||
                       !primitive.texcoord_track.frames.empty();
            } );

        for( const runtime::model::GltfSkin &skin : out.gltf_asset->skins )
        {
            if( skin.joints.size() > stage3d::max_gpu_skin_bones )
            {
                throw std::runtime_error( "glTF skin exceeds the renderer bone limit" );
            }
        }

        for( std::size_t primitive_index = 0; primitive_index < out.gltf_asset->primitives.size(); ++primitive_index )
        {
            runtime::model::GltfPrimitive &primitive = out.gltf_asset->primitives[ primitive_index ];
            DrawMesh mesh{};
            mesh.debug_id = next_material_debug_id_++;

            mesh.model_name = name;

            mesh.mesh_name = primitive.name;

            mesh.geometry_name = primitive.name;

            mesh.static_local_matrix = filetypes::xg::identity_matrix();

            mesh.gltf_primitive_index = static_cast<std::int32_t>( primitive_index );

            mesh.gltf_node_index = primitive.node_index;

            mesh.gltf_skin_index = primitive.skin_index;

            mesh.gpu_skin_ready = primitive.skinned && primitive.skin_index >= 0;

            mesh.cull_func = primitive.cull_func;

            mesh.has_vertex_interpolator = !primitive.position_track.frames.empty();

            mesh.has_normal_interpolator = !primitive.normal_track.frames.empty();

            mesh.has_texcoord_interpolator = !primitive.texcoord_track.frames.empty();

            mesh.has_geometry_interpolators =
                mesh.has_vertex_interpolator || mesh.has_normal_interpolator || mesh.has_texcoord_interpolator;

            if( primitive.material_index >= 0 &&
                static_cast<std::size_t>( primitive.material_index ) < out.gltf_asset->materials.size() )
            {
                const runtime::model::GltfMaterial &material =
                    out.gltf_asset->materials[ static_cast<std::size_t>( primitive.material_index ) ];

                mesh.has_material = true;

                mesh.material_name = material.name;

                mesh.diffuse = material.diffuse;

                mesh.blend_type = material.blend_type;

                mesh.shading_type = material.shading_type;

                mesh.flags = material.flags;

                mesh.texture_env = material.texture_env;

                mesh.u_tile = material.u_tile;

                mesh.v_tile = material.v_tile;

                mesh.uses_alpha =
                    material.uses_texture_alpha || material.blend_type != 0u || material.diffuse.a < 0.999f;

                mesh.wrap_texture = material.wrap_texture;

                if( !material.texture_path.empty() )
                {
                    mesh.texture_name = material.texture_path.filename().string();
                    const std::string manager_key =
                        "stage3d/gltf/" + piko::utils::lowercase_filename( name ) + "/" +
                        piko::utils::lowercase_filename( material.texture_path.filename().string() );

                    const auto existing = texture_by_name_.find( manager_key );

                    if( existing != texture_by_name_.end() )
                    {
                        mesh.texture = existing->second;

                        mesh.texture_found = mesh.texture.valid();
                    }
                    else
                    {
                        try
                        {
                            mesh.texture = app.texture_manager().load_png( manager_key, material.texture_path );

                            mesh.texture_found = mesh.texture.valid();

                            if( mesh.texture_found )
                            {
                                texture_handles_.push_back( mesh.texture );
                                texture_by_name_[ manager_key ] = mesh.texture;
                            }
                        }
                        catch( const std::exception & )
                        {
                        }
                    }
                }

                if( mesh.texture_found )
                {
                    register_gltf_texture_animation( app, material.texture_path, mesh.texture );
                }
            }

            mesh.indexed_vertices = !primitive.indices.empty();

            mesh.triangle_indices = std::move( primitive.indices );

            mesh.vertex_count = primitive.vertices.size();

            mesh.index_count = mesh.triangle_indices.size();
            mesh.vertices.reserve( mesh.vertex_count );

            for( const runtime::model::GltfVertex &source : primitive.vertices )
            {
                const GpuVertex vertex{
                    source.position.x,
                    source.position.y,
                    source.position.z,
                    source.uv.x,
                    source.uv.y,
                    source.color.r,
                    source.color.g,
                    source.color.b,
                    source.color.a,
                    source.normal.x,
                    source.normal.y,
                    source.normal.z,
                    static_cast<float>( source.joints[ 0 ] ),
                    static_cast<float>( source.joints[ 1 ] ),
                    static_cast<float>( source.joints[ 2 ] ),
                    static_cast<float>( source.joints[ 3 ] ),
                    source.weights[ 0 ],
                    source.weights[ 1 ],
                    source.weights[ 2 ],
                    source.weights[ 3 ]
                };

                mesh.vertices.push_back( vertex );
            }

            if( mesh.gpu_skin_ready && out.gltf_skin_index < 0 )
            {
                out.gltf_skin_index = mesh.gltf_skin_index;
            }

            out.vertex_count += mesh.vertex_count;
            upload_static_mesh_buffers( mesh );

            if( mesh.static_gpu_ready )
            {
                std::vector<GpuVertex>().swap( mesh.vertices );
                std::vector<std::uint32_t>().swap( mesh.triangle_indices );

                if( !mesh.has_geometry_interpolators )
                {
                    std::vector<runtime::model::GltfVertex>().swap( primitive.vertices );
                }
            }

            out.meshes.push_back( std::move( mesh ) );
        }

        if( out.gltf_skin_index >= 0 && static_cast<std::size_t>( out.gltf_skin_index ) < out.gltf_asset->skins.size() )
        {
            const runtime::model::GltfSkin &skin =
                out.gltf_asset->skins[ static_cast<std::size_t>( out.gltf_skin_index ) ];

            out.gpu_skin_bones.reserve( skin.joints.size() );

            for( const std::int32_t joint : skin.joints )
            {
                if( joint >= 0 && static_cast<std::size_t>( joint ) < out.gltf_asset->nodes.size() )
                {
                    out.gpu_skin_bones.push_back( out.gltf_asset->nodes[ static_cast<std::size_t>( joint ) ].name );
                }
                else
                {
                    out.gpu_skin_bones.emplace_back();
                }
            }
        }

        return out;
    }

    void Stage3DRenderer::append_dag_node( const filetypes::xg::Model &model,
        const std::string &node_name,
        const filetypes::xg::Matrix &parent_matrix,
        const std::vector<std::string> &matrix_chain,
        DrawModel &out_model )
    {
        filetypes::xg::Matrix local_matrix = parent_matrix;
        std::vector<std::string> local_chain = matrix_chain;

        if( const filetypes::xg::DagTransform *transform = model.find_transform( node_name ) )
        {
            if( !transform->matrix_name.empty() )
            {

                if( model.find_matrix_node( transform->matrix_name ) != nullptr )
                {

                    local_matrix = filetypes::xg::multiply( parent_matrix,
                        evaluate_xg_matrix( model, transform->matrix_name, 0.0f, true ) );
                }
                else
                {
                    const auto matrix_it = model.matrices.find( transform->matrix_name );

                    if( matrix_it != model.matrices.end() )
                    {
                        local_matrix = filetypes::xg::multiply( parent_matrix, matrix_it->second );
                    }
                }

                local_chain.push_back( transform->matrix_name );
            }
        }

        if( const filetypes::xg::DagMesh *mesh = model.find_mesh( node_name ) )
        {
            append_mesh( model, *mesh, local_matrix, local_chain, out_model );
        }

        if( const filetypes::xg::DagNode *node = model.find_dag_node( node_name ) )
        {
            for( const std::string &child : node->children )
            {
                append_dag_node( model, child, local_matrix, local_chain, out_model );
            }
        }
    }

    void Stage3DRenderer::append_mesh( const filetypes::xg::Model &model,
        const filetypes::xg::DagMesh &mesh,
        const filetypes::xg::Matrix &matrix,
        const std::vector<std::string> &matrix_chain,
        DrawModel &out_model )
    {
        const filetypes::xg::Geometry *geometry = model.find_geometry( mesh.geometry_name );

        if( geometry == nullptr || geometry->vertices.empty() || mesh.triangle_indices.empty() )
        {
            return;
        }

        if( const filetypes::xg::MultiPassMaterial *multipass = model.find_multipass_material( mesh.material_name ) )
        {
            const std::uint32_t layer_count = static_cast<std::uint32_t>( multipass->material_names.size() );

            if( layer_count > 0 )
            {
                for( std::uint32_t layer = 0; layer < layer_count; ++layer )
                {
                    const filetypes::xg::Material *material = model.find_material( multipass->material_names[ layer ] );
                    append_mesh_layer( model,
                        *geometry,
                        mesh,
                        material,
                        matrix,
                        matrix_chain,
                        out_model,
                        true,
                        layer,
                        layer_count );
                }

                return;
            }
        }

        append_mesh_layer( model,
            *geometry,
            mesh,
            model.find_material( mesh.material_name ),
            matrix,
            matrix_chain,
            out_model,
            false,
            0,
            1 );
    }

    void Stage3DRenderer::append_mesh_layer( const filetypes::xg::Model &model,
        const filetypes::xg::Geometry &geometry,
        const filetypes::xg::DagMesh &mesh,
        const filetypes::xg::Material *material,
        const filetypes::xg::Matrix &matrix,
        const std::vector<std::string> &matrix_chain,
        DrawModel &out_model,
        bool is_multipass,
        std::uint32_t multipass_layer_index,
        std::uint32_t multipass_layer_count )
    {
        DrawMesh draw_mesh{};
        draw_mesh.debug_id = next_material_debug_id_++;

        draw_mesh.model_name = out_model.name;

        draw_mesh.mesh_name = mesh.name;

        draw_mesh.geometry_name = mesh.geometry_name;

        draw_mesh.static_local_matrix = matrix;

        draw_mesh.matrix_chain = matrix_chain;

        draw_mesh.input_geometry_nodes = geometry.input_geometry_nodes;

        draw_mesh.triangle_indices = mesh.triangle_indices;
        bool geometry_uvs_need_repeat = false;

        for( const std::string &input_node : geometry.input_geometry_nodes )
        {
            draw_mesh.has_vertex_interpolator =
                draw_mesh.has_vertex_interpolator || model.find_vertex_interpolator( input_node ) != nullptr;

            draw_mesh.has_normal_interpolator =
                draw_mesh.has_normal_interpolator || model.find_normal_interpolator( input_node ) != nullptr;

            if( const filetypes::xg::TexCoordInterpolator *texcoord_interpolator =
                    model.find_texcoord_interpolator( input_node ) )
            {
                draw_mesh.has_texcoord_interpolator = true;

                geometry_uvs_need_repeat =
                    geometry_uvs_need_repeat || stage3d::texcoord_interpolator_needs_repeat( *texcoord_interpolator );
            }

            draw_mesh.has_shape_interpolator =
                draw_mesh.has_shape_interpolator || model.find_shape_interpolator( input_node ) != nullptr;
        }

        for( const filetypes::xg::Vertex &vertex : geometry.vertices )
        {
            if( vertex.has_uv && rendering::uv_requires_texture_repeat( vertex.uv.x, vertex.uv.y ) )
            {
                geometry_uvs_need_repeat = true;
                break;
            }
        }

        draw_mesh.has_geometry_interpolators = draw_mesh.has_vertex_interpolator || draw_mesh.has_normal_interpolator ||
                                               draw_mesh.has_texcoord_interpolator || draw_mesh.has_shape_interpolator;

        draw_mesh.material_name = is_multipass && material == nullptr
                                      ? mesh.material_name
                                      : ( material != nullptr ? material->name : mesh.material_name );

        draw_mesh.has_material = material != nullptr;

        draw_mesh.is_multipass = is_multipass;

        draw_mesh.multipass_layer_index = multipass_layer_index;

        draw_mesh.multipass_layer_count = std::max<std::uint32_t>( 1, multipass_layer_count );

        draw_mesh.multipass_default_visible =
            !is_multipass || multipass_layer_index + 1u == draw_mesh.multipass_layer_count;

        draw_mesh.cull_func = mesh.cull_func;

        if( material != nullptr )
        {
            draw_mesh.texture_name = piko::utils::lowercase_filename( material->texture_name );

            const auto texture_it = texture_by_name_.find( draw_mesh.texture_name );

            if( texture_it != texture_by_name_.end() )
            {
                draw_mesh.texture = texture_it->second;

                draw_mesh.texture_found = true;
            }

            draw_mesh.uses_alpha = stage3d::material_should_use_blend_pass( *material );

            draw_mesh.wrap_texture = stage3d::material_should_wrap_texture( *material ) || geometry_uvs_need_repeat;

            draw_mesh.blend_type = material->blend_type;

            draw_mesh.shading_type = material->shading_type;

            draw_mesh.flags = material->flags;

            draw_mesh.texture_env = material->texture_env;

            draw_mesh.u_tile = material->u_tile;

            draw_mesh.v_tile = material->v_tile;

            draw_mesh.diffuse = material->diffuse;
        }

        draw_mesh.vertices.reserve( mesh.triangle_indices.size() );

        for( std::uint32_t index : mesh.triangle_indices )
        {
            if( index >= geometry.vertices.size() )
            {
                continue;
            }

            const filetypes::xg::Vertex &source = geometry.vertices[ index ];
            const filetypes::xg::Vec3 p = stage3d::transform_point( matrix, source.position );
            const filetypes::xg::Vec3 n = stage3d::transform_normal( matrix, source.normal );
            rendering::Color color = source.color;

            draw_mesh.vertices.push_back( GpuVertex{
                p.x,
                p.y,
                p.z,
                source.has_uv ? source.uv.x : 0.0f,
                source.has_uv ? source.uv.y : 0.0f,
                color.r,
                color.g,
                color.b,
                color.a,
                n.x,
                n.y,
                n.z
            } );
        }

        if( out_model.has_envelopes && !geometry.input_geometry_nodes.empty() && !mesh.triangle_indices.empty() )
        {
            struct SkinBinding
            {
                filetypes::xg::Vertex source{};
                filetypes::xg::Vec4 weights{};
                std::uint32_t bone_indices[ 4 ]{};
                bool valid = false;
            };

            std::vector<SkinBinding> bindings( geometry.vertices.size() );
            bool any_binding = false;

            for( const std::string &input_node : geometry.input_geometry_nodes )
            {
                const filetypes::xg::Envelope *envelope = model.find_envelope( input_node );

                if( envelope == nullptr || envelope->input_geometry.empty() )
                {
                    continue;
                }

                const filetypes::xg::Geometry *envelope_geometry = model.find_geometry( envelope->input_geometry );

                if( envelope_geometry == nullptr || envelope_geometry->vertices.empty() )
                {
                    continue;
                }

                const std::size_t count = std::min( envelope->weights.size(), envelope->vertex_targets.size() );

                for( std::size_t envelope_index = 0; envelope_index < count; ++envelope_index )
                {
                    const std::size_t source_index =
                        static_cast<std::size_t>( envelope->start_vertex ) + envelope_index;

                    if( source_index >= envelope_geometry->vertices.size() )
                    {
                        continue;
                    }

                    const filetypes::xg::Vec4 weight = envelope->weights[ envelope_index ];
                    const float weights[ 4 ] = {
                        weight.x,
                        weight.y,
                        weight.z,
                        weight.w
                    };

                    std::uint32_t bone_indices[ 4 ]{};
                    float gpu_weights[ 4 ]{};
                    bool has_weighted_bone = false;

                    for( std::size_t slot = 0; slot < 4; ++slot )
                    {
                        if( std::abs( weights[ slot ] ) <= 0.000001f || envelope->input_matrices[ slot ].empty() )
                        {
                            continue;
                        }

                        const std::uint32_t index = gpu_skin_bone_index( out_model, envelope->input_matrices[ slot ] );

                        if( index >= stage3d::max_gpu_skin_bones )
                        {
                            continue;
                        }

                        bone_indices[ slot ] = index;

                        gpu_weights[ slot ] = weights[ slot ];

                        has_weighted_bone = true;
                    }

                    if( !has_weighted_bone )
                    {
                        continue;
                    }

                    SkinBinding binding{};
                    binding.source = envelope_geometry->vertices[ source_index ];

                    binding.weights =
                        filetypes::xg::Vec4{ gpu_weights[ 0 ], gpu_weights[ 1 ], gpu_weights[ 2 ], gpu_weights[ 3 ] };

                    for( std::size_t slot = 0; slot < 4; ++slot )
                    {
                        binding.bone_indices[ slot ] = bone_indices[ slot ];
                    }

                    binding.valid = true;

                    for( std::uint32_t target_index : envelope->vertex_targets[ envelope_index ] )
                    {
                        if( target_index < bindings.size() )
                        {
                            bindings[ target_index ] = binding;

                            any_binding = true;
                        }
                    }
                }
            }

            if( any_binding )
            {
                draw_mesh.gpu_skin_vertices.reserve( mesh.triangle_indices.size() );

                for( std::uint32_t index : mesh.triangle_indices )
                {
                    if( index >= geometry.vertices.size() )
                    {
                        continue;
                    }

                    const SkinBinding &binding = bindings[ index ];
                    const filetypes::xg::Vertex &target = geometry.vertices[ index ];
                    const filetypes::xg::Vertex &position_source = binding.valid ? binding.source : target;
                    rendering::Color color = target.color;

                    GpuVertex vertex{
                        position_source.position.x,
                        position_source.position.y,
                        position_source.position.z,
                        target.has_uv ? target.uv.x : 0.0f,
                        target.has_uv ? target.uv.y : 0.0f,
                        color.r,
                        color.g,
                        color.b,
                        color.a,
                        target.normal.x,
                        target.normal.y,
                        target.normal.z
                    };

                    if( binding.valid )
                    {
                        vertex.bi0 = static_cast<float>( binding.bone_indices[ 0 ] );

                        vertex.bi1 = static_cast<float>( binding.bone_indices[ 1 ] );

                        vertex.bi2 = static_cast<float>( binding.bone_indices[ 2 ] );

                        vertex.bi3 = static_cast<float>( binding.bone_indices[ 3 ] );

                        vertex.bw0 = binding.weights.x;

                        vertex.bw1 = binding.weights.y;

                        vertex.bw2 = binding.weights.z;

                        vertex.bw3 = binding.weights.w;
                    }

                    draw_mesh.gpu_skin_vertices.push_back( vertex );
                }

                draw_mesh.gpu_skin_ready = !draw_mesh.gpu_skin_vertices.empty();
            }
        }

        if( !draw_mesh.vertices.empty() )
        {
            draw_mesh.vertex_count = draw_mesh.vertices.size();

            draw_mesh.index_count = draw_mesh.triangle_indices.size();

            out_model.vertex_count += draw_mesh.vertex_count;
            out_model.meshes.push_back( std::move( draw_mesh ) );
        }
    }
}
