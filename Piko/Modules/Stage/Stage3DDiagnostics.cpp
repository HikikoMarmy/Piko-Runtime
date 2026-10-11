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
    const std::string &Stage3DRenderer::debug_camera_line() const
    {
        return debug_camera_line_;
    }

    bool Stage3DRenderer::debug_camera_animating() const
    {
        return camera_debug_pullback_seconds_ > 0.0;
    }

    std::vector<std::string> Stage3DRenderer::camera_debug_lines() const
    {
        std::vector<std::string> lines;
        std::size_t range_index = static_cast<std::size_t>( -1 );
        const CameraPresetupRange *range = active_camera_range( &range_index );
        const filetypes::ssq::Camera camera = sample_camera();
        const SceneLighting lighting = sample_scene_lighting( effective_stage_frame() );

        std::ostringstream title;
        title << "3D lighting/debug | backend " << lighting_backend_name() << " | GMLT " << gmlt_debug_mode_name()
              << " | material " << material_debug_mode_name();

        lines.push_back( title.str() );

        std::ostringstream timing;
        timing << "frame " << static_cast<std::int32_t>( std::lround( effective_stage_frame() ) ) << " | section "
               << stage_section_index_ << " local " << static_cast<std::int32_t>( std::lround( stage_section_frame_ ) );

        if( range != nullptr )
        {
            timing << " | cam cue " << range_index << "/" << camera_ranges_.size() << " " << range->name << " ["
                   << static_cast<std::int32_t>( std::lround( range->start_frame ) ) << "-"
                   << static_cast<std::int32_t>( std::lround( range->end_frame ) ) << "]";
        }
        else
        {
            timing << " | cam cue none/" << camera_ranges_.size();
        }

        lines.push_back( timing.str() );

        std::ostringstream depth_line;
        depth_line << std::fixed << std::setprecision( 2 ) << "depth " << depth_mode_name() << " | near raw "
                   << camera.z_near << " min " << minimum_projection_near_ << " effective "
                   << effective_projection_near( camera ) << " | far " << camera.z_far;

        lines.push_back( depth_line.str() );

        std::ostringstream controls;
        controls << std::fixed << std::setprecision( 2 ) << "scales | light " << scene_lighting_strength_ << " | floor "
                 << scene_lighting_floor_ << " | GMLT " << gmlt_light_strength_ << " (" << gmlt_debug_mode_name() << ")"
                 << " | wrap " << ps2_light_wrap_;

        lines.push_back( controls.str() );

        std::ostringstream ambient_line;
        ambient_line << std::fixed << std::setprecision( 3 ) << "ambient RGB " << lighting.ambient.r << ", "
                     << lighting.ambient.g << ", " << lighting.ambient.b << " | diffuse RGB " << lighting.diffuse.r
                     << ", " << lighting.diffuse.g << ", " << lighting.diffuse.b;

        lines.push_back( ambient_line.str() );

        std::ostringstream sky_line;
        sky_line << "sky "
                 << ( should_draw_stage_sky() ? ( sky_texture_name_.empty() ? "on/missing" : sky_texture_name_ )
                                              : "off" )
                 << " | GMLT lights " << lighting.gmlt_light_count << "/" << scene_.camera.light_setups.size()
                 << " | dir mode " << gmlt_direction_mode_name();

        lines.push_back( sky_line.str() );

        for( std::size_t i = 0; i < lighting.gmlt_light_count && i < 2; ++i )
        {
            const SceneLight &gmlt = lighting.gmlt_lights[ i ];
            std::ostringstream gmlt_line;
            gmlt_line << std::fixed << std::setprecision( 3 ) << "GMLT" << gmlt.source_index << " -> slot " << i
                      << " rgb " << gmlt.primary.r << ", " << gmlt.primary.g << ", " << gmlt.primary.b << " | fill "
                      << gmlt.secondary.r << ", " << gmlt.secondary.g << ", " << gmlt.secondary.b << " | dir "
                      << gmlt.direction.x << ", " << gmlt.direction.y << ", " << gmlt.direction.z;

            lines.push_back( gmlt_line.str() );
        }

        lines.push_back(
            "keys: ;/' light  -/= floor  [/ ] GMLT str  7 GMLT mode  8 dir/axis  Q backend  Z depth  5/6 near  P sky  0 reset" );

        lines.push_back( "camera: C rot  ,/. frame  1/2 speed  debug cam B/R/I/K/J/L/U/O" );

        return lines;
    }

    std::vector<std::string> Stage3DRenderer::action_debug_lines() const
    {
        std::vector<std::string> lines;

        std::ostringstream header;
        header << std::hex << std::uppercase << std::setfill( '0' ) << "SSQ actions | flags 0x" << std::setw( 8 )
               << effective_action_event_flags() << std::dec << " desc " << action_input_.descriptor;

        if( action_event_flag_override_enabled_ )
        {
            header << " (override)";
        }

        if( action_input_.has_aim_angle )
        {
            header << std::fixed << std::setprecision( 2 ) << " angle " << action_input_.aim_angle_radians;
        }
        else
        {
            header << " angle none";
        }

        lines.push_back( header.str() );

        if( frozen_action_debug_seconds_ > 0.0 )
        {
            std::ostringstream frozen;
            frozen << std::hex << std::uppercase << std::setfill( '0' ) << "last action " << std::fixed
                   << std::setprecision( 2 ) << frozen_action_debug_seconds_ << "s"
                   << " | flags 0x" << std::setw( 8 ) << frozen_action_debug_input_.event_flags << std::dec << " desc "
                   << frozen_action_debug_input_.descriptor;

            if( frozen_action_debug_input_.has_aim_angle )
            {
                frozen << std::fixed << std::setprecision( 2 ) << " angle "
                       << frozen_action_debug_input_.aim_angle_radians;
            }
            else
            {
                frozen << " angle none";
            }

            lines.push_back( frozen.str() );
        }

        std::ostringstream names;
        names << "bits:";
        const std::uint32_t flags = effective_action_event_flags();
        bool any = false;

        for( std::int32_t bit = 0; bit < 16; ++bit )
        {
            if( ( flags & ( 1u << bit ) ) == 0 )
            {
                continue;
            }

            names << ' ' << bit << '=' << action_event_flag_name( bit );
            any = true;
        }

        if( !any )
        {
            names << " none";
        }

        lines.push_back( names.str() );

        std::size_t shown = 0;

        for( const SceneInstance &instance : instances_ )
        {
            if( instance.model_setup_index >= scene_.models.size() ||
                instance.model_setup_index >= action_states_.size() )
            {
                continue;
            }

            const filetypes::ssq::ModelEntry &entry = scene_.models[ instance.model_setup_index ];

            if( entry.controllables.empty() )
            {
                continue;
            }

            const ActionAnimationState &state = action_states_[ instance.model_setup_index ];
            std::ostringstream line;
            line << instance.name << " type " << instance.model_type << " ctrl " << state.current_controllable << '/'
                 << entry.controllables.size();

            if( state.current_controllable < entry.controllables.size() )
            {
                const filetypes::ssq::Controllable &ctrl = entry.controllables[ state.current_controllable ];
                line << " anim " << state.selected_animation_index << " desc " << ctrl.descriptor << std::hex
                     << std::uppercase << std::setfill( '0' ) << " flag 0x" << std::setw( 8 ) << ctrl.event_flag
                     << std::dec << std::setfill( ' ' ) << " conns " << ctrl.connections.size() << " def "
                     << ctrl.default_event;
            }

            line << " | ";

            if( state.used_connection )
            {
                line << "conn " << state.selected_connection_index;
            }
            else if( state.used_default )
            {
                line << "default";
            }
            else
            {
                line << "hold";
            }

            line << " e/d/a " << ( state.event_matched ? 'Y' : 'n' ) << '/' << ( state.descriptor_matched ? 'Y' : 'n' )
                 << '/' << ( state.angle_matched ? 'Y' : 'n' );

            lines.push_back( line.str() );

            ++shown;

            if( shown >= 6 )
            {
                break;
            }
        }

        if( shown == 0 )
        {
            lines.push_back( "No SSQ PlayerModelSetup controllables are active/decoded for visible models." );
        }
        else if( shown < instances_.size() )
        {
            lines.push_back(
                "Showing first controllable models only; use material/model debug if a specific actor is hidden." );
        }

        return lines;
    }

    std::string Stage3DRenderer::material_debug_mode_name() const
    {
        switch( material_debug_mode_ )
        {
            case MaterialDebugMode::Normal:
                return "normal";
            case MaterialDebugMode::TextureOnly:
                return "texture only";
            case MaterialDebugMode::ColorOnly:
                return "vertex colour";
            case MaterialDebugMode::NoEnvMap:
                return "env maps off";
            case MaterialDebugMode::OpaqueOnly:
                return "opaque only";
            case MaterialDebugMode::AlphaOnly:
                return "alpha/effects only";
            case MaterialDebugMode::AlphaMask:
                return "alpha mask as texture";
            case MaterialDebugMode::MultipassBase:
                return "multipass base layer";
            case MaterialDebugMode::MultipassAll:
                return "multipass all layers";
        }

        return "normal";
    }

    std::string Stage3DRenderer::lighting_backend_name() const
    {
        switch( lighting_backend_ )
        {
            case LightingBackend::Fragment:
                return "fragment";
            case LightingBackend::VertexPs2:
                return "ps2 vertex";
            case LightingBackend::Cel:
                return "cel";
        }

        return "ps2 vertex";
    }

    std::string Stage3DRenderer::gmlt_debug_mode_name() const
    {
        switch( gmlt_debug_mode_ )
        {
            case GmltDebugMode::All:
                return "all";
            case GmltDebugMode::Only0:
                return "GMLT0 only";
            case GmltDebugMode::Only1:
                return "GMLT1 only";
            case GmltDebugMode::None:
                return "off";
        }

        return "unknown";
    }

    std::string Stage3DRenderer::gmlt_direction_mode_name() const
    {
        switch( gmlt_direction_mode_ )
        {
            case GmltDirectionMode::LocalNegativeZ:
                return "q*-Z";
            case GmltDirectionMode::LocalPositiveZ:
                return "q*+Z";
            case GmltDirectionMode::LocalNegativeY:
                return "q*-Y";
            case GmltDirectionMode::LocalPositiveY:
                return "q*+Y";
            case GmltDirectionMode::LocalNegativeX:
                return "q*-X";
            case GmltDirectionMode::LocalPositiveX:
                return "q*+X";
            case GmltDirectionMode::InverseNegativeZ:
                return "invq*-Z";
            case GmltDirectionMode::InversePositiveZ:
                return "invq*+Z";
            case GmltDirectionMode::InverseNegativeY:
                return "invq*-Y";
            case GmltDirectionMode::InversePositiveY:
                return "invq*+Y";
            case GmltDirectionMode::InverseNegativeX:
                return "invq*-X";
            case GmltDirectionMode::InversePositiveX:
                return "invq*+X";
            case GmltDirectionMode::RawNegativeZ:
                return "rawq*-Z";
            case GmltDirectionMode::RawPositiveZ:
                return "rawq*+Z";
            case GmltDirectionMode::RawNegativeY:
                return "rawq*-Y";
            case GmltDirectionMode::RawPositiveY:
                return "rawq*+Y";
            case GmltDirectionMode::RawNegativeX:
                return "rawq*-X";
            case GmltDirectionMode::RawPositiveX:
                return "rawq*+X";
            case GmltDirectionMode::RawInverseNegativeZ:
                return "invrawq*-Z";
            case GmltDirectionMode::RawInversePositiveZ:
                return "invrawq*+Z";
            case GmltDirectionMode::RawInverseNegativeY:
                return "invrawq*-Y";
            case GmltDirectionMode::RawInversePositiveY:
                return "invrawq*+Y";
            case GmltDirectionMode::RawInverseNegativeX:
                return "invrawq*-X";
            case GmltDirectionMode::RawInversePositiveX:
                return "invrawq*+X";
        }

        return "unknown";
    }

    filetypes::xg::Vec3 Stage3DRenderer::gmlt_direction_basis() const
    {
        switch( gmlt_direction_mode_ )
        {
            case GmltDirectionMode::LocalNegativeZ:
            case GmltDirectionMode::InverseNegativeZ:
            case GmltDirectionMode::RawNegativeZ:
            case GmltDirectionMode::RawInverseNegativeZ:
                return filetypes::xg::Vec3{ 0.0f, 0.0f, -1.0f };
            case GmltDirectionMode::LocalPositiveZ:
            case GmltDirectionMode::InversePositiveZ:
            case GmltDirectionMode::RawPositiveZ:
            case GmltDirectionMode::RawInversePositiveZ:
                return filetypes::xg::Vec3{ 0.0f, 0.0f, 1.0f };
            case GmltDirectionMode::LocalNegativeY:
            case GmltDirectionMode::InverseNegativeY:
            case GmltDirectionMode::RawNegativeY:
            case GmltDirectionMode::RawInverseNegativeY:
                return filetypes::xg::Vec3{ 0.0f, -1.0f, 0.0f };
            case GmltDirectionMode::LocalPositiveY:
            case GmltDirectionMode::InversePositiveY:
            case GmltDirectionMode::RawPositiveY:
            case GmltDirectionMode::RawInversePositiveY:
                return filetypes::xg::Vec3{ 0.0f, 1.0f, 0.0f };
            case GmltDirectionMode::LocalNegativeX:
            case GmltDirectionMode::InverseNegativeX:
            case GmltDirectionMode::RawNegativeX:
            case GmltDirectionMode::RawInverseNegativeX:
                return filetypes::xg::Vec3{ -1.0f, 0.0f, 0.0f };
            case GmltDirectionMode::LocalPositiveX:
            case GmltDirectionMode::InversePositiveX:
            case GmltDirectionMode::RawPositiveX:
            case GmltDirectionMode::RawInversePositiveX:
                return filetypes::xg::Vec3{ 1.0f, 0.0f, 0.0f };
        }

        return filetypes::xg::Vec3{ 0.0f, 0.0f, -1.0f };
    }

    bool Stage3DRenderer::gmlt_direction_uses_inverse_quat() const
    {
        switch( gmlt_direction_mode_ )
        {
            case GmltDirectionMode::InverseNegativeZ:
            case GmltDirectionMode::InversePositiveZ:
            case GmltDirectionMode::InverseNegativeY:
            case GmltDirectionMode::InversePositiveY:
            case GmltDirectionMode::InverseNegativeX:
            case GmltDirectionMode::InversePositiveX:
            case GmltDirectionMode::RawInverseNegativeZ:
            case GmltDirectionMode::RawInversePositiveZ:
            case GmltDirectionMode::RawInverseNegativeY:
            case GmltDirectionMode::RawInversePositiveY:
            case GmltDirectionMode::RawInverseNegativeX:
            case GmltDirectionMode::RawInversePositiveX:
                return true;
            default:
                return false;
        }
    }

    bool Stage3DRenderer::gmlt_direction_uses_raw_quat() const
    {
        switch( gmlt_direction_mode_ )
        {
            case GmltDirectionMode::RawNegativeZ:
            case GmltDirectionMode::RawPositiveZ:
            case GmltDirectionMode::RawNegativeY:
            case GmltDirectionMode::RawPositiveY:
            case GmltDirectionMode::RawNegativeX:
            case GmltDirectionMode::RawPositiveX:
            case GmltDirectionMode::RawInverseNegativeZ:
            case GmltDirectionMode::RawInversePositiveZ:
            case GmltDirectionMode::RawInverseNegativeY:
            case GmltDirectionMode::RawInversePositiveY:
            case GmltDirectionMode::RawInverseNegativeX:
            case GmltDirectionMode::RawInversePositiveX:
                return true;
            default:
                return false;
        }
    }

    std::string Stage3DRenderer::depth_mode_name() const
    {
        switch( depth_mode_ )
        {
            case DepthMode::Standard:
                return "standard";
            case DepthMode::ReverseZ:
                return "reverse-z";
        }

        return "reverse-z";
    }

    std::size_t Stage3DRenderer::material_debug_mesh_count() const
    {
        return next_material_debug_id_;
    }

    const Stage3DRenderer::DrawMesh *Stage3DRenderer::find_debug_mesh( std::size_t debug_id ) const
    {
        for( const auto &[ _, model ] : models_ )
        {
            (void)_;

            for( const DrawMesh &mesh : model.meshes )
            {
                if( mesh.debug_id == debug_id )
                {
                    return &mesh;
                }
            }
        }

        return nullptr;
    }

    void Stage3DRenderer::refresh_material_debug_status_line()
    {
        const std::size_t count = material_debug_mesh_count();

        if( count > 0 )
        {
            selected_material_debug_id_ %= count;
        }
        else
        {
            selected_material_debug_id_ = 0;
        }

        std::ostringstream ss;
        ss << "3D mat debug: " << material_debug_mode_name();

        if( material_debug_focus_selected_ && count > 0 )
        {
            ss << " | focus " << selected_material_debug_id_ << "/" << ( count - 1 );
        }

        ss << " | M mode, N next, V focus";
        material_debug_status_line_ = ss.str();
    }

    void Stage3DRenderer::cycle_material_debug_mode()
    {
        const std::int32_t next = ( utils::to_int( material_debug_mode_ ) + 1 ) % 9;
        material_debug_mode_ = utils::to_enum<MaterialDebugMode>( next );
        refresh_material_debug_status_line();
    }

    void Stage3DRenderer::cycle_material_debug_sample()
    {
        const std::size_t count = material_debug_mesh_count();

        if( count > 0 )
        {
            selected_material_debug_id_ = ( selected_material_debug_id_ + 1 ) % count;
        }

        refresh_material_debug_status_line();
    }

    void Stage3DRenderer::toggle_material_debug_focus()
    {
        material_debug_focus_selected_ = !material_debug_focus_selected_;
        refresh_material_debug_status_line();
    }

    std::vector<std::pair<std::string, managers::TextureHandle>> Stage3DRenderer::texture_debug_entries() const
    {
        std::vector<std::pair<std::string, managers::TextureHandle>> entries;
        std::unordered_set<std::uint32_t> seen;

        for( const auto &[ _, model ] : models_ )
        {
            (void)_;

            for( const DrawMesh &mesh : model.meshes )
            {
                if( !mesh.texture || !seen.insert( mesh.texture.id ).second )
                {
                    continue;
                }

                entries.emplace_back( mesh.texture_name.empty() ? std::string( "<unnamed>" ) : mesh.texture_name,
                    mesh.texture );
            }
        }

        std::sort( entries.begin(),
            entries.end(),
            []( const auto &a, const auto &b )
            {
                if( a.first != b.first )
                {
                    return a.first < b.first;
                }

                return a.second.id < b.second.id;
            } );
        return entries;
    }

    void Stage3DRenderer::toggle_texture_debug_overlay()
    {
        texture_debug_overlay_visible_ = !texture_debug_overlay_visible_;
    }

    void Stage3DRenderer::cycle_texture_debug_sample( std::int32_t direction )
    {
        const std::size_t count = texture_debug_count();

        if( count == 0 )
        {
            selected_texture_debug_index_ = 0;
            return;
        }

        const std::ptrdiff_t step = direction < 0 ? -1 : 1;
        const std::ptrdiff_t current = static_cast<std::ptrdiff_t>( selected_texture_debug_index_ % count );
        const std::ptrdiff_t wrapped =
            ( current + step + static_cast<std::ptrdiff_t>( count ) ) % static_cast<std::ptrdiff_t>( count );

        selected_texture_debug_index_ = static_cast<std::size_t>( wrapped );
    }

    void Stage3DRenderer::cycle_texture_debug_view()
    {
        const std::int32_t next = ( utils::to_int( texture_debug_view_ ) + 1 ) % 3;
        texture_debug_view_ = utils::to_enum<TextureDebugView>( next );
    }

    bool Stage3DRenderer::texture_debug_overlay_visible() const
    {
        return texture_debug_overlay_visible_;
    }

    managers::TextureHandle Stage3DRenderer::texture_debug_handle() const
    {
        const auto entries = texture_debug_entries();

        if( entries.empty() )
        {
            return {};
        }

        return entries[ selected_texture_debug_index_ % entries.size() ].second;
    }

    std::string Stage3DRenderer::texture_debug_name() const
    {
        const auto entries = texture_debug_entries();

        if( entries.empty() )
        {
            return "<none>";
        }

        return entries[ selected_texture_debug_index_ % entries.size() ].first;
    }

    std::size_t Stage3DRenderer::texture_debug_index() const
    {
        const std::size_t count = texture_debug_count();
        return count == 0 ? 0 : selected_texture_debug_index_ % count;
    }

    std::size_t Stage3DRenderer::texture_debug_count() const
    {
        return texture_debug_entries().size();
    }

    const char *Stage3DRenderer::texture_debug_view_name() const
    {
        switch( texture_debug_view_ )
        {
            case TextureDebugView::Normal:
                return "normal alpha";
            case TextureDebugView::OpaqueRgb:
                return "RGB ignoring alpha";
            case TextureDebugView::AlphaMask:
                return "decoded alpha";
        }

        return "normal alpha";
    }

    std::int32_t Stage3DRenderer::texture_debug_view_index() const
    {
        return utils::to_int( texture_debug_view_ );
    }

    bool Stage3DRenderer::material_debug_overlay_visible() const
    {
        return material_debug_mode_ != MaterialDebugMode::Normal || material_debug_focus_selected_;
    }

    const std::string &Stage3DRenderer::material_debug_status_line() const
    {
        return material_debug_status_line_;
    }

    std::vector<std::string> Stage3DRenderer::material_debug_lines() const
    {
        std::size_t mesh_count = 0;
        std::size_t missing_textures = 0;
        std::size_t alpha_meshes = 0;
        std::size_t env_meshes = 0;
        std::size_t multipass_meshes = 0;
        std::size_t animated_geometry_meshes = 0;

        for( const auto &[ _, model ] : models_ )
        {
            (void)_;

            for( const DrawMesh &mesh : model.meshes )
            {
                ++mesh_count;

                if( !mesh.texture_name.empty() && !mesh.texture_found )
                {
                    ++missing_textures;
                }

                if( mesh.uses_alpha )
                {
                    ++alpha_meshes;
                }

                if( mesh.texture_env != 0 )
                {
                    ++env_meshes;
                }

                if( mesh.is_multipass )
                {
                    ++multipass_meshes;
                }

                if( mesh.has_geometry_interpolators )
                {
                    ++animated_geometry_meshes;
                }
            }
        }

        std::vector<std::string> lines;
        std::ostringstream first;
        first << "3D mat debug: " << material_debug_mode_name() << " | meshes " << mesh_count << " missTex "
              << missing_textures << " alpha " << alpha_meshes << " env " << env_meshes << " mp " << multipass_meshes
              << " geoAnim " << animated_geometry_meshes;

        lines.push_back( first.str() );
        lines.push_back( "M mode | N next material | V focus selected | mode is diagnostic only" );

        if( mesh_count == 0 )
        {
            return lines;
        }

        const DrawMesh *mesh = find_debug_mesh( selected_material_debug_id_ % mesh_count );

        if( mesh == nullptr )
        {
            return lines;
        }

        std::ostringstream second;
        second << "#" << mesh->debug_id << " " << mesh->model_name << " :: " << mesh->mesh_name;
        lines.push_back( second.str() );

        std::ostringstream third;
        third << "mat " << ( mesh->material_name.empty() ? "<none>" : mesh->material_name ) << " tex "
              << ( mesh->texture_name.empty() ? "<none>" : mesh->texture_name )
              << ( mesh->texture_found ? " found" : " MISSING" );

        lines.push_back( third.str() );

        std::ostringstream fourth;
        fourth << "blend " << mesh->blend_type << " shade " << mesh->shading_type << " flags 0x" << std::hex
               << mesh->flags << std::dec << " env " << mesh->texture_env << " tile " << mesh->u_tile << "/"
               << mesh->v_tile << " cull " << mesh->cull_func << " geo " << ( mesh->has_shape_interpolator ? "S" : "-" )
               << ( mesh->has_vertex_interpolator ? "V" : "-" ) << ( mesh->has_normal_interpolator ? "N" : "-" )
               << ( mesh->has_texcoord_interpolator ? "T" : "-" ) << " wrap "
               << ( mesh->wrap_texture ? "repeat" : "clamp" ) << " blendPass " << ( mesh->uses_alpha ? "yes" : "no" )
               << " alpha "
               << ( stage3d::material_alpha_is_active( mesh->blend_type, mesh->flags ) ? "yes" : "ignored" )
               << " alphaSource texture";

        lines.push_back( fourth.str() );

        if( mesh->is_multipass )
        {
            std::ostringstream fifth;
            fifth << "multipass layer " << ( mesh->multipass_layer_index + 1 ) << "/" << mesh->multipass_layer_count
                  << ( mesh->multipass_default_visible ? " default-visible" : " hidden-in-normal" );

            lines.push_back( fifth.str() );
        }

        return lines;
    }

    std::string Stage3DRenderer::camera_rotation_mode_name() const
    {
        return camera_rotation_mode_name( camera_rotation_mode_ );
    }

    std::string Stage3DRenderer::camera_rotation_mode_name( CameraRotationMode mode ) const
    {
        switch( mode )
        {
            case CameraRotationMode::ZReflectOpenGlForward:
                return "Z refl -Z";
            case CameraRotationMode::ZReflectInverseOpenGlForward:
                return "Z refl inv -Z";
            case CameraRotationMode::XReflectOpenGlForward:
                return "X refl -Z";
            case CameraRotationMode::XReflectInverseOpenGlForward:
                return "X refl inv -Z";
            case CameraRotationMode::YReflectOpenGlForward:
                return "Y refl -Z";
            case CameraRotationMode::YReflectInverseOpenGlForward:
                return "Y refl inv -Z";
            case CameraRotationMode::RawOpenGlForward:
                return "raw -Z";
            case CameraRotationMode::RawInverseOpenGlForward:
                return "raw inv -Z";
        }

        return "Z refl inv -Z";
    }

    std::size_t Stage3DRenderer::camera_rotation_mode_index() const
    {
        return utils::enum_index( camera_rotation_mode_ );
    }

    std::string Stage3DRenderer::camera_time_scale_name() const
    {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision( 2 ) << camera_time_scale_ << "x";
        return ss.str();
    }

    void Stage3DRenderer::refresh_debug_camera_line()
    {
        std::size_t range_index = static_cast<std::size_t>( -1 );
        const CameraPresetupRange *range = active_camera_range( &range_index );

        std::ostringstream ss;
        ss << "3D cam raw " << static_cast<std::int32_t>( std::lround( stage_frame_ ) ) << " sec "
           << stage_section_index_ << " loc " << static_cast<std::int32_t>( std::lround( stage_section_frame_ ) )
           << " eff " << static_cast<std::int32_t>( std::lround( effective_stage_frame() ) );

        if( range != nullptr )
        {
            ss << " cue " << range_index << "/" << camera_ranges_.size() << " " << range->name << " ["
               << static_cast<std::int32_t>( std::lround( range->start_frame ) ) << "-"
               << static_cast<std::int32_t>( std::lround( range->end_frame ) ) << "]";
        }
        else
        {
            ss << " cue none/" << camera_ranges_.size();
        }

        ss << " off " << static_cast<std::int32_t>( std::lround( camera_frame_offset_ ) ) << " speed "
           << camera_time_scale_name() << " rot " << ( camera_rotation_mode_index() + 1 ) << "/8 "
           << camera_rotation_mode_name() << " anim " << animation_mode_name() << " modelrot "
           << ( model_rotation_mode_index() + 1 ) << "/17 " << model_rotation_mode_name() << " xg "
           << ( xg_rotation_mode_index() + 1 ) << "/8 " << xg_rotation_mode_name() << std::fixed
           << std::setprecision( 2 ) << " | light " << scene_lighting_strength_ << "/" << scene_lighting_floor_
           << " gmlt " << gmlt_light_strength_ << " wrap " << ps2_light_wrap_ << " backend " << lighting_backend_name()
           << " depth " << depth_mode_name() << " near " << minimum_projection_near_ << " sky "
           << ( should_draw_stage_sky() ? "on" : "off" ) << " | local X "
           << static_cast<std::int32_t>( std::lround( camera_debug_local_offset_.x ) ) << " Y "
           << static_cast<std::int32_t>( std::lround( camera_debug_local_offset_.y ) ) << " Z "
           << static_cast<std::int32_t>( std::lround( camera_debug_local_offset_.z ) );

        if( camera_debug_pullback_seconds_ > 0.0 )
        {
            ss << " | auto pullback";
        }

        ss << " | B auto, R reset, I/K/J/L/U/O nudge, C cam rot, G anim, Y model rot, H xg rot, Q light, Z depth, 5/6 near";
        debug_camera_line_ = ss.str();
    }

    void Stage3DRenderer::nudge_debug_camera_local( float x, float y, float z )
    {
        camera_debug_local_offset_.x += x;

        camera_debug_local_offset_.y += y;

        camera_debug_local_offset_.z += z;

        camera_debug_pullback_seconds_ = 0.0;
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::reset_debug_camera()
    {
        camera_debug_local_offset_ = {};

        camera_debug_pullback_seconds_ = 0.0;
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::begin_debug_camera_pullback( float seconds )
    {
        camera_debug_pullback_seconds_ = std::max( 0.0f, seconds );
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::nudge_camera_frame_offset( float frames )
    {
        camera_frame_offset_ = std::clamp( camera_frame_offset_ + frames, -900.0f, 900.0f );
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::reset_camera_frame_offset()
    {
        camera_frame_offset_ = 0.0f;
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::cycle_camera_rotation_mode()
    {
        const std::int32_t next = ( utils::to_int( camera_rotation_mode_ ) + 1 ) % 8;
        camera_rotation_mode_ = utils::to_enum<CameraRotationMode>( next );
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::nudge_camera_time_scale( std::int32_t direction )
    {
        constexpr float speeds[] = {
            0.25f,
            0.5f,
            1.0f,
            2.0f
        };

        std::int32_t current = 0;
        float best_delta = std::abs( camera_time_scale_ - speeds[ 0 ] );

        for( std::int32_t i = 1; i < static_cast<std::int32_t>( std::size( speeds ) ); ++i )
        {
            const float delta = std::abs( camera_time_scale_ - speeds[ i ] );

            if( delta < best_delta )
            {
                best_delta = delta;

                current = i;
            }
        }

        const std::int32_t count = static_cast<std::int32_t>( std::size( speeds ) );
        std::int32_t next = ( current + ( direction >= 0 ? 1 : -1 ) ) % count;

        if( next < 0 )
        {
            next += count;
        }

        camera_time_scale_ = speeds[ next ];
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::cycle_animation_mode()
    {
        const std::int32_t next = ( utils::to_int( animation_mode_ ) + 1 ) % 4;
        animation_mode_ = utils::to_enum<AnimationMode>( next );
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::cycle_model_rotation_mode()
    {
        constexpr std::int32_t mode_count = 17;
        const std::int32_t next = ( utils::to_int( model_rotation_mode_ ) + 1 ) % mode_count;
        model_rotation_mode_ = utils::to_enum<ModelRotationMode>( next );
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::cycle_xg_rotation_mode()
    {
        const std::int32_t next = ( utils::to_int( xg_rotation_mode_ ) + 1 ) % 8;
        xg_rotation_mode_ = utils::to_enum<XgRotationMode>( next );
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::cycle_skinning_formula_mode()
    {
        const std::int32_t next = ( utils::to_int( skinning_formula_mode_ ) + 1 ) % 6;
        skinning_formula_mode_ = utils::to_enum<SkinningFormulaMode>( next );
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::cycle_model_basis_mode()
    {
        const std::int32_t next = ( utils::to_int( model_basis_mode_ ) + 1 ) % 16;
        model_basis_mode_ = utils::to_enum<ModelBasisMode>( next );
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::toggle_special_model_types()
    {
        render_special_model_types_ = !render_special_model_types_;
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::nudge_scene_lighting_strength( float delta )
    {
        scene_lighting_strength_ = std::clamp( scene_lighting_strength_ + delta, 0.0f, 1.0f );
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::nudge_scene_lighting_floor( float delta )
    {
        scene_lighting_floor_ = std::clamp( scene_lighting_floor_ + delta, 0.0f, 1.0f );
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::nudge_gmlt_light_strength( float delta )
    {
        gmlt_light_strength_ = std::clamp( gmlt_light_strength_ + delta, 0.0f, 2.5f );
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::cycle_gmlt_debug_mode()
    {
        const std::int32_t next = ( utils::to_int( gmlt_debug_mode_ ) + 1 ) % 4;
        gmlt_debug_mode_ = utils::to_enum<GmltDebugMode>( next );
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::cycle_gmlt_direction_mode()
    {
        constexpr std::int32_t count = 24;
        const std::int32_t next = ( utils::to_int( gmlt_direction_mode_ ) + 1 ) % count;
        gmlt_direction_mode_ = utils::to_enum<GmltDirectionMode>( next );
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::nudge_ps2_light_wrap( float delta )
    {
        ps2_light_wrap_ = std::clamp( ps2_light_wrap_ + delta, 0.0f, 0.85f );
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::toggle_lighting_backend()
    {
        const std::int32_t next = ( utils::to_int( lighting_backend_ ) + 1 ) % lighting_backend_display_count();

        lighting_backend_ = utils::to_enum<LightingBackend>( next );
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::toggle_depth_mode()
    {
        depth_mode_ = depth_mode_ == DepthMode::ReverseZ ? DepthMode::Standard : DepthMode::ReverseZ;
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::nudge_depth_near_plane( float delta )
    {

        minimum_projection_near_ = std::clamp( minimum_projection_near_ + delta, 0.5f, 500.0f );
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::reset_scene_lighting()
    {
        scene_lighting_strength_ = 1.00f;

        scene_lighting_floor_ = 0.00f;

        gmlt_light_strength_ = default_gmlt_light_strength();

        gmlt_debug_mode_ = GmltDebugMode::All;

        gmlt_direction_mode_ = GmltDirectionMode::InversePositiveZ;

        ps2_light_wrap_ = 0.00f;

        cel_shadow_threshold_ = 0.55f;

        cel_shadow_strength_ = 0.58f;

        cel_edge_strength_ = 0.0f;

        lighting_backend_ = LightingBackend::VertexPs2;

        depth_mode_ = DepthMode::ReverseZ;

        minimum_projection_near_ = 5.0f;

        debug_stage_sky_enabled_ = true;
        refresh_debug_camera_line();
    }

    void Stage3DRenderer::toggle_stage_sky()
    {
        debug_stage_sky_enabled_ = !debug_stage_sky_enabled_;
        refresh_debug_camera_line();
    }

    std::int32_t Stage3DRenderer::lighting_backend_index() const
    {
        return utils::to_int( lighting_backend_ );
    }

    void Stage3DRenderer::set_lighting_backend_index( std::int32_t index )
    {
        index = std::clamp( index, 0, lighting_backend_display_count() - 1 );

        lighting_backend_ = utils::to_enum<LightingBackend>( index );
        refresh_debug_camera_line();
    }

    const char *Stage3DRenderer::lighting_backend_display_name( std::int32_t index ) const
    {
        switch( utils::to_enum<LightingBackend>( std::clamp( index, 0, lighting_backend_display_count() - 1 ) ) )
        {
            case LightingBackend::Fragment:
                return "Fragment / modern";
            case LightingBackend::VertexPs2:
                return "PS2 vertex";
            case LightingBackend::Cel:
                return "Cel shaded";
        }

        return "PS2 vertex";
    }

    std::int32_t Stage3DRenderer::lighting_backend_display_count() const
    {
        return 3;
    }

    float Stage3DRenderer::scene_lighting_strength() const
    {
        return scene_lighting_strength_;
    }

    void Stage3DRenderer::set_scene_lighting_strength( float value )
    {
        scene_lighting_strength_ = std::clamp( value, 0.0f, 1.0f );
        refresh_debug_camera_line();
    }

    float Stage3DRenderer::scene_lighting_floor() const
    {
        return scene_lighting_floor_;
    }

    void Stage3DRenderer::set_scene_lighting_floor( float value )
    {
        scene_lighting_floor_ = std::clamp( value, 0.0f, 1.0f );
        refresh_debug_camera_line();
    }

    float Stage3DRenderer::gmlt_light_strength() const
    {
        return gmlt_light_strength_;
    }

    void Stage3DRenderer::set_gmlt_light_strength( float value )
    {
        gmlt_light_strength_ = std::clamp( value, 0.0f, 2.5f );
        refresh_debug_camera_line();
    }

    float Stage3DRenderer::ps2_light_wrap() const
    {
        return ps2_light_wrap_;
    }

    void Stage3DRenderer::set_ps2_light_wrap( float value )
    {
        ps2_light_wrap_ = std::clamp( value, 0.0f, 0.85f );
        refresh_debug_camera_line();
    }

    float Stage3DRenderer::minimum_projection_near() const
    {
        return minimum_projection_near_;
    }

    void Stage3DRenderer::set_minimum_projection_near( float value )
    {
        minimum_projection_near_ = std::clamp( value, 0.5f, 500.0f );
        refresh_debug_camera_line();
    }

    bool Stage3DRenderer::stage_sky_enabled() const
    {
        return debug_stage_sky_enabled_;
    }

    void Stage3DRenderer::set_stage_sky_enabled( bool enabled )
    {
        debug_stage_sky_enabled_ = enabled;
        refresh_debug_camera_line();
    }

    float Stage3DRenderer::cel_shadow_threshold() const
    {
        return cel_shadow_threshold_;
    }

    void Stage3DRenderer::set_cel_shadow_threshold( float value )
    {
        cel_shadow_threshold_ = std::clamp( value, 0.05f, 0.95f );
        refresh_debug_camera_line();
    }

    float Stage3DRenderer::cel_shadow_strength() const
    {
        return cel_shadow_strength_;
    }

    void Stage3DRenderer::set_cel_shadow_strength( float value )
    {
        cel_shadow_strength_ = std::clamp( value, 0.05f, 1.0f );
        refresh_debug_camera_line();
    }

    float Stage3DRenderer::cel_edge_strength() const
    {
        return cel_edge_strength_;
    }

    void Stage3DRenderer::set_cel_edge_strength( float value )
    {
        cel_edge_strength_ = std::clamp( value, 0.0f, 0.95f );
        refresh_debug_camera_line();
    }

    bool Stage3DRenderer::action_event_flag_override_enabled() const
    {
        return action_event_flag_override_enabled_;
    }

    std::uint32_t Stage3DRenderer::action_event_flag_override() const
    {
        return action_event_flag_override_;
    }

    const char *Stage3DRenderer::action_event_flag_name( std::int32_t bit_index ) const
    {
        switch( bit_index )
        {
            case 0:
                return "press/start";
            case 1:
                return "hold";
            case 2:
                return "bad/no-hit";
            case 3:
                return "end success";
            case 4:
                return "forced damage";
            case 5:
                return "drop/miss";
            case 6:
                return "success/action";
            case 7:
                return "alt damage A";
            case 8:
                return "alt damage B";
            case 9:
                return "aim/pending";
            default:
                return "unknown";
        }
    }
}
