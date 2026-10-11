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
    float Stage3DRenderer::xg_animation_time( const DrawModel &model,
        const ResolvedAnimation &resolved,
        float frame ) const
    {
        (void)model;
        (void)frame;
        return resolved.evaluated_frame;
    }

    std::uint32_t Stage3DRenderer::gpu_skin_bone_index( DrawModel &model, const std::string &bone_name )
    {
        if( bone_name.empty() )
        {
            return stage3d::max_gpu_skin_bones;
        }

        const auto it = std::find( model.gpu_skin_bones.begin(), model.gpu_skin_bones.end(), bone_name );

        if( it != model.gpu_skin_bones.end() )
        {
            return static_cast<std::uint32_t>( std::distance( model.gpu_skin_bones.begin(), it ) );
        }

        if( model.gpu_skin_bones.size() >= stage3d::max_gpu_skin_bones )
        {
            return stage3d::max_gpu_skin_bones;
        }

        model.gpu_skin_bones.push_back( bone_name );
        return static_cast<std::uint32_t>( model.gpu_skin_bones.size() - 1u );
    }

    filetypes::xg::Matrix Stage3DRenderer::evaluate_mesh_local_matrix( const DrawModel &model,
        const DrawMesh &mesh,
        const ResolvedAnimation &animation ) const
    {
        if( model.gltf_asset != nullptr && mesh.gltf_node_index >= 0 )
        {
            if( !xg_rigid_animation_enabled() )
            {
                return runtime::model::evaluate_gltf_node_world( *model.gltf_asset, mesh.gltf_node_index, 0u, 0.0f );
            }

            if( animation.pose_blend_active )
            {
                return runtime::model::evaluate_gltf_node_world_blended( *model.gltf_asset,
                    mesh.gltf_node_index,
                    animation.pose_blend_source_animation_index,
                    animation.pose_blend_source_frame,
                    animation.pose_blend_target_animation_index,
                    animation.pose_blend_target_frame,
                    animation.pose_blend_weight );
            }

            return runtime::model::evaluate_gltf_node_world( *model.gltf_asset,
                mesh.gltf_node_index,
                animation.animation_index,
                animation.evaluated_frame );
        }

        if( !xg_rigid_animation_enabled() || mesh.matrix_chain.empty() )
        {
            return mesh.static_local_matrix;
        }

        filetypes::xg::Matrix local_matrix = filetypes::xg::identity_matrix();

        for( const std::string &matrix_name : mesh.matrix_chain )
        {
            local_matrix = filetypes::xg::multiply( local_matrix,
                evaluate_xg_matrix_blended( model.source_model, matrix_name, animation, true ) );
        }

        return local_matrix;
    }

    std::vector<filetypes::xg::Matrix> Stage3DRenderer::build_gpu_skin_matrices( const DrawModel &model,
        const DrawMesh &mesh,
        const ResolvedAnimation &animation ) const
    {
        if( model.gltf_asset != nullptr )
        {
            const float frame = xg_rigid_animation_enabled() ? animation.evaluated_frame : 0.0f;
            return runtime::model::build_gltf_skin_matrices( *model.gltf_asset,
                mesh.gltf_skin_index,
                mesh.gltf_node_index,
                animation.animation_index,
                frame,
                xg_rigid_animation_enabled() && animation.pose_blend_active,
                animation.pose_blend_source_animation_index,
                animation.pose_blend_source_frame,
                animation.pose_blend_target_animation_index,
                animation.pose_blend_target_frame,
                animation.pose_blend_weight );
        }

        std::vector<filetypes::xg::Matrix> matrices;
        matrices.reserve( model.gpu_skin_bones.size() );

        for( const std::string &bone_name : model.gpu_skin_bones )
        {
            const filetypes::xg::Bone *bone = model.source_model.find_bone( bone_name );

            if( bone == nullptr || bone->input_matrix.empty() )
            {
                matrices.push_back( filetypes::xg::identity_matrix() );
                continue;
            }

            const filetypes::xg::Matrix bone_pose =
                evaluate_xg_matrix_blended( model.source_model, bone->input_matrix, animation, true );

            matrices.push_back( make_skin_matrix( bone_pose, bone->rest_matrix ) );
        }

        return matrices;
    }

    bool Stage3DRenderer::xg_rigid_animation_enabled() const
    {
        return animation_mode_ == AnimationMode::RigidXgMatrices || animation_mode_ == AnimationMode::CpuSkinning ||
               animation_mode_ == AnimationMode::GpuSkinning;
    }

    bool Stage3DRenderer::cpu_skinning_enabled() const
    {
        return animation_mode_ == AnimationMode::CpuSkinning;
    }

    bool Stage3DRenderer::gpu_skinning_enabled() const
    {
        return animation_mode_ == AnimationMode::GpuSkinning;
    }

    std::string Stage3DRenderer::animation_mode_name() const
    {
        switch( animation_mode_ )
        {
            case AnimationMode::SsqOnly:
                return "SSQ only";
            case AnimationMode::RigidXgMatrices:
                return "SSQ + rigid XG";
            case AnimationMode::CpuSkinning:
                return "CPU skin probe";
            case AnimationMode::GpuSkinning:
                return "GPU skinning";
        }

        return "SSQ + rigid XG";
    }

    std::string Stage3DRenderer::model_rotation_mode_name() const
    {
        return model_rotation_mode_name( model_rotation_mode_ );
    }

    std::string Stage3DRenderer::model_rotation_mode_name( ModelRotationMode mode ) const
    {
        switch( mode )
        {
            case ModelRotationMode::ZReflect:
                return "Z refl";
            case ModelRotationMode::ZReflectInverse:
                return "Z refl inv";
            case ModelRotationMode::XReflect:
                return "X refl";
            case ModelRotationMode::XReflectInverse:
                return "X refl inv";
            case ModelRotationMode::YReflect:
                return "Y refl";
            case ModelRotationMode::YReflectInverse:
                return "Y refl inv";
            case ModelRotationMode::Raw:
                return "raw";
            case ModelRotationMode::RawInverse:
                return "raw inv";
            case ModelRotationMode::Ignore:
                return "ignore SSQ rot";
            case ModelRotationMode::RawXzy:
                return "raw XZY";
            case ModelRotationMode::RawXzyInverse:
                return "raw XZY inv";
            case ModelRotationMode::RawXzyNegY:
                return "raw XZY -Y";
            case ModelRotationMode::RawXzyNegYInverse:
                return "raw XZY -Y inv";
            case ModelRotationMode::RawXzyNegZ:
                return "raw XZY -Z";
            case ModelRotationMode::RawXzyNegZInverse:
                return "raw XZY -Z inv";
            case ModelRotationMode::RawXzyNegYZ:
                return "raw XZY -Y -Z";
            case ModelRotationMode::RawXzyNegYZInverse:
                return "raw XZY -Y -Z inv";
        }

        return "Z refl";
    }

    std::size_t Stage3DRenderer::model_rotation_mode_index() const
    {
        return utils::enum_index( model_rotation_mode_ );
    }

    std::string Stage3DRenderer::xg_rotation_mode_name() const
    {
        return xg_rotation_mode_name( xg_rotation_mode_ );
    }

    std::string Stage3DRenderer::xg_rotation_mode_name( XgRotationMode mode ) const
    {
        switch( mode )
        {
            case XgRotationMode::ZReflect:
                return "Z refl";
            case XgRotationMode::ZReflectInverse:
                return "Z refl inv";
            case XgRotationMode::XReflect:
                return "X refl";
            case XgRotationMode::XReflectInverse:
                return "X refl inv";
            case XgRotationMode::YReflect:
                return "Y refl";
            case XgRotationMode::YReflectInverse:
                return "Y refl inv";
            case XgRotationMode::Raw:
                return "raw";
            case XgRotationMode::RawInverse:
                return "raw inv";
        }

        return "Z refl";
    }

    std::size_t Stage3DRenderer::xg_rotation_mode_index() const
    {
        return utils::enum_index( xg_rotation_mode_ );
    }

    std::string Stage3DRenderer::skinning_formula_mode_name() const
    {
        return skinning_formula_mode_name( skinning_formula_mode_ );
    }

    std::string Stage3DRenderer::skinning_formula_mode_name( SkinningFormulaMode mode ) const
    {
        switch( mode )
        {
            case SkinningFormulaMode::PoseTimesRest:
                return "pose*rest";
            case SkinningFormulaMode::RestTimesPose:
                return "rest*pose";
            case SkinningFormulaMode::PoseTimesInverseRest:
                return "pose*inv(rest)";
            case SkinningFormulaMode::InverseRestTimesPose:
                return "inv(rest)*pose";
            case SkinningFormulaMode::PoseTimesRestTranspose:
                return "pose*transpose(rest)";
            case SkinningFormulaMode::RestTransposeTimesPose:
                return "transpose(rest)*pose";
        }

        return "pose*rest";
    }

    filetypes::xg::Quat Stage3DRenderer::convert_rotation_for_mode( filetypes::xg::Quat q, XgRotationMode mode ) const
    {
        switch( mode )
        {
            case XgRotationMode::ZReflect:
            case XgRotationMode::ZReflectInverse:
                q.x = -q.x;

                q.y = -q.y;
                break;

            case XgRotationMode::XReflect:
            case XgRotationMode::XReflectInverse:
                q.y = -q.y;

                q.z = -q.z;
                break;

            case XgRotationMode::YReflect:
            case XgRotationMode::YReflectInverse:
                q.x = -q.x;

                q.z = -q.z;
                break;

            case XgRotationMode::Raw:
            case XgRotationMode::RawInverse:
                break;
        }

        q = filetypes::xg::normalize_quaternion( q );

        if( mode == XgRotationMode::ZReflectInverse || mode == XgRotationMode::XReflectInverse ||
            mode == XgRotationMode::YReflectInverse || mode == XgRotationMode::RawInverse )
        {
            q = stage3d::conjugate_quat( q );
        }

        return q;
    }

    filetypes::xg::Quat Stage3DRenderer::convert_model_rotation( filetypes::xg::Quat q ) const
    {
        switch( model_rotation_mode_ )
        {
            case ModelRotationMode::ZReflect:
                return convert_rotation_for_mode( q, XgRotationMode::ZReflect );
            case ModelRotationMode::ZReflectInverse:
                return convert_rotation_for_mode( q, XgRotationMode::ZReflectInverse );
            case ModelRotationMode::XReflect:
                return convert_rotation_for_mode( q, XgRotationMode::XReflect );
            case ModelRotationMode::XReflectInverse:
                return convert_rotation_for_mode( q, XgRotationMode::XReflectInverse );
            case ModelRotationMode::YReflect:
                return convert_rotation_for_mode( q, XgRotationMode::YReflect );
            case ModelRotationMode::YReflectInverse:
                return convert_rotation_for_mode( q, XgRotationMode::YReflectInverse );
            case ModelRotationMode::Raw:
                return convert_rotation_for_mode( q, XgRotationMode::Raw );
            case ModelRotationMode::RawInverse:
                return convert_rotation_for_mode( q, XgRotationMode::RawInverse );
            case ModelRotationMode::Ignore:
                return stage3d::identity_quat();
            case ModelRotationMode::RawXzy:
            case ModelRotationMode::RawXzyInverse:
            case ModelRotationMode::RawXzyNegY:
            case ModelRotationMode::RawXzyNegYInverse:
            case ModelRotationMode::RawXzyNegZ:
            case ModelRotationMode::RawXzyNegZInverse:
            case ModelRotationMode::RawXzyNegYZ:
            case ModelRotationMode::RawXzyNegYZInverse:
            {

                filetypes::xg::Quat swizzled{ q.x, q.z, q.y, q.w };

                if( model_rotation_mode_ == ModelRotationMode::RawXzyNegY ||
                    model_rotation_mode_ == ModelRotationMode::RawXzyNegYInverse ||
                    model_rotation_mode_ == ModelRotationMode::RawXzyNegYZ ||
                    model_rotation_mode_ == ModelRotationMode::RawXzyNegYZInverse )
                {
                    swizzled.y = -swizzled.y;
                }

                if( model_rotation_mode_ == ModelRotationMode::RawXzyNegZ ||
                    model_rotation_mode_ == ModelRotationMode::RawXzyNegZInverse ||
                    model_rotation_mode_ == ModelRotationMode::RawXzyNegYZ ||
                    model_rotation_mode_ == ModelRotationMode::RawXzyNegYZInverse )
                {
                    swizzled.z = -swizzled.z;
                }

                swizzled = filetypes::xg::normalize_quaternion( swizzled );

                if( model_rotation_mode_ == ModelRotationMode::RawXzyInverse ||
                    model_rotation_mode_ == ModelRotationMode::RawXzyNegYInverse ||
                    model_rotation_mode_ == ModelRotationMode::RawXzyNegZInverse ||
                    model_rotation_mode_ == ModelRotationMode::RawXzyNegYZInverse )
                {
                    swizzled = stage3d::conjugate_quat( swizzled );
                }

                return swizzled;
            }
        }

        return convert_rotation_for_mode( q, XgRotationMode::ZReflect );
    }

    filetypes::xg::Quat Stage3DRenderer::convert_xg_animation_rotation( filetypes::xg::Quat q ) const
    {
        return convert_rotation_for_mode( q, xg_rotation_mode_ );
    }

    filetypes::xg::Matrix Stage3DRenderer::make_xg_animation_matrix( const filetypes::xg::Vec3 &position,
        const filetypes::xg::Quat &rotation,
        const filetypes::xg::Vec3 &scale ) const
    {

        filetypes::xg::Vec3 converted_position = position;
        converted_position.z = -converted_position.z;
        return filetypes::xg::make_trs( converted_position, convert_xg_animation_rotation( rotation ), scale );
    }

    filetypes::xg::Matrix Stage3DRenderer::make_skin_matrix( const filetypes::xg::Matrix &pose_matrix,
        const filetypes::xg::Matrix &rest_matrix ) const
    {
        switch( skinning_formula_mode_ )
        {
            case SkinningFormulaMode::PoseTimesRest:
                return filetypes::xg::multiply( pose_matrix, rest_matrix );
            case SkinningFormulaMode::RestTimesPose:
                return filetypes::xg::multiply( rest_matrix, pose_matrix );
            case SkinningFormulaMode::PoseTimesInverseRest:
                return filetypes::xg::multiply( pose_matrix, filetypes::xg::inverse_affine_matrix( rest_matrix ) );
            case SkinningFormulaMode::InverseRestTimesPose:
                return filetypes::xg::multiply( filetypes::xg::inverse_affine_matrix( rest_matrix ), pose_matrix );
            case SkinningFormulaMode::PoseTimesRestTranspose:
                return filetypes::xg::multiply( pose_matrix, stage3d::transpose_matrix( rest_matrix ) );
            case SkinningFormulaMode::RestTransposeTimesPose:
                return filetypes::xg::multiply( stage3d::transpose_matrix( rest_matrix ), pose_matrix );
        }

        return filetypes::xg::multiply( pose_matrix, rest_matrix );
    }

    std::string Stage3DRenderer::model_basis_mode_name() const
    {
        return model_basis_mode_name( model_basis_mode_ );
    }

    std::string Stage3DRenderer::model_basis_mode_name( ModelBasisMode mode ) const
    {
        switch( mode )
        {
            case ModelBasisMode::Identity:
                return "identity";
            case ModelBasisMode::RotateX90:
                return "rot X +90";
            case ModelBasisMode::RotateXMinus90:
                return "rot X -90";
            case ModelBasisMode::RotateX180:
                return "rot X 180";
            case ModelBasisMode::RotateY90:
                return "rot Y +90";
            case ModelBasisMode::RotateYMinus90:
                return "rot Y -90";
            case ModelBasisMode::RotateY180:
                return "rot Y 180";
            case ModelBasisMode::RotateZ90:
                return "rot Z +90";
            case ModelBasisMode::RotateZMinus90:
                return "rot Z -90";
            case ModelBasisMode::RotateZ180:
                return "rot Z 180";
            case ModelBasisMode::MirrorX:
                return "mirror X";
            case ModelBasisMode::MirrorY:
                return "mirror Y";
            case ModelBasisMode::MirrorZ:
                return "mirror Z";
            case ModelBasisMode::MirrorXY:
                return "mirror X/Y";
            case ModelBasisMode::MirrorXZ:
                return "mirror X/Z";
            case ModelBasisMode::MirrorYZ:
                return "mirror Y/Z";
        }

        return "identity";
    }

    std::size_t Stage3DRenderer::model_basis_mode_index() const
    {
        return utils::enum_index( model_basis_mode_ );
    }

    filetypes::xg::Matrix Stage3DRenderer::model_basis_matrix() const
    {
        constexpr float pi = 3.14159265358979323846f;
        const filetypes::xg::Vec3 origin{};
        const filetypes::xg::Vec3 one{ 1.0f, 1.0f, 1.0f };

        switch( model_basis_mode_ )
        {
            case ModelBasisMode::Identity:
                return filetypes::xg::identity_matrix();
            case ModelBasisMode::RotateX90:
                return filetypes::xg::make_trs( origin, stage3d::axis_angle_quat( 1.0f, 0.0f, 0.0f, pi * 0.5f ), one );
            case ModelBasisMode::RotateXMinus90:
                return filetypes::xg::make_trs( origin, stage3d::axis_angle_quat( 1.0f, 0.0f, 0.0f, -pi * 0.5f ), one );
            case ModelBasisMode::RotateX180:
                return filetypes::xg::make_trs( origin, stage3d::axis_angle_quat( 1.0f, 0.0f, 0.0f, pi ), one );
            case ModelBasisMode::RotateY90:
                return filetypes::xg::make_trs( origin, stage3d::axis_angle_quat( 0.0f, 1.0f, 0.0f, pi * 0.5f ), one );
            case ModelBasisMode::RotateYMinus90:
                return filetypes::xg::make_trs( origin, stage3d::axis_angle_quat( 0.0f, 1.0f, 0.0f, -pi * 0.5f ), one );
            case ModelBasisMode::RotateY180:
                return filetypes::xg::make_trs( origin, stage3d::axis_angle_quat( 0.0f, 1.0f, 0.0f, pi ), one );
            case ModelBasisMode::RotateZ90:
                return filetypes::xg::make_trs( origin, stage3d::axis_angle_quat( 0.0f, 0.0f, 1.0f, pi * 0.5f ), one );
            case ModelBasisMode::RotateZMinus90:
                return filetypes::xg::make_trs( origin, stage3d::axis_angle_quat( 0.0f, 0.0f, 1.0f, -pi * 0.5f ), one );
            case ModelBasisMode::RotateZ180:
                return filetypes::xg::make_trs( origin, stage3d::axis_angle_quat( 0.0f, 0.0f, 1.0f, pi ), one );
            case ModelBasisMode::MirrorX:
                return filetypes::xg::make_trs( origin,
                    stage3d::identity_quat(),
                    filetypes::xg::Vec3{ -1.0f, 1.0f, 1.0f } );
            case ModelBasisMode::MirrorY:
                return filetypes::xg::make_trs( origin,
                    stage3d::identity_quat(),
                    filetypes::xg::Vec3{ 1.0f, -1.0f, 1.0f } );
            case ModelBasisMode::MirrorZ:
                return filetypes::xg::make_trs( origin,
                    stage3d::identity_quat(),
                    filetypes::xg::Vec3{ 1.0f, 1.0f, -1.0f } );
            case ModelBasisMode::MirrorXY:
                return filetypes::xg::make_trs( origin,
                    stage3d::identity_quat(),
                    filetypes::xg::Vec3{ -1.0f, -1.0f, 1.0f } );
            case ModelBasisMode::MirrorXZ:
                return filetypes::xg::make_trs( origin,
                    stage3d::identity_quat(),
                    filetypes::xg::Vec3{ -1.0f, 1.0f, -1.0f } );
            case ModelBasisMode::MirrorYZ:
                return filetypes::xg::make_trs( origin,
                    stage3d::identity_quat(),
                    filetypes::xg::Vec3{ 1.0f, -1.0f, -1.0f } );
        }

        return filetypes::xg::identity_matrix();
    }

    filetypes::xg::Matrix Stage3DRenderer::evaluate_xg_matrix( const filetypes::xg::Model &model,
        const std::string &matrix_name,
        float animation_frame,
        bool include_parent ) const
    {
        const filetypes::xg::Matrix *static_matrix = nullptr;

        const auto static_it = model.matrices.find( matrix_name );

        if( static_it != model.matrices.end() )
        {
            static_matrix = &static_it->second;
        }

        const filetypes::xg::MatrixNode *node = model.find_matrix_node( matrix_name );

        if( node == nullptr )
        {
            return static_matrix != nullptr ? *static_matrix : filetypes::xg::identity_matrix();
        }

        filetypes::xg::Vec3 position = node->position;
        filetypes::xg::Quat rotation = node->rotation;
        filetypes::xg::Vec3 scale = node->scale;

        if( const filetypes::xg::Vec3Interpolator *interp = model.find_vec3_interpolator( node->input_position ) )
        {
            position = stage3d::sample_xg_vec3_interpolator( *interp, animation_frame );
        }

        if( const filetypes::xg::QuatInterpolator *interp = model.find_quat_interpolator( node->input_rotation ) )
        {
            rotation = stage3d::sample_xg_quat_interpolator( *interp, animation_frame );
        }

        if( const filetypes::xg::Vec3Interpolator *interp = model.find_vec3_interpolator( node->input_scale ) )
        {
            scale = stage3d::sample_xg_vec3_interpolator( *interp, animation_frame );
        }

        filetypes::xg::Matrix local = make_xg_animation_matrix( position, rotation, scale );

        if( include_parent && !node->parent_matrix.empty() )
        {
            local = filetypes::xg::multiply( evaluate_xg_matrix( model, node->parent_matrix, animation_frame, true ),
                local );
        }

        return local;
    }

    filetypes::xg::Matrix Stage3DRenderer::evaluate_xg_matrix_blended( const filetypes::xg::Model &model,
        const std::string &matrix_name,
        const ResolvedAnimation &animation,
        bool include_parent ) const
    {
        if( !animation.pose_blend_active )
        {
            return evaluate_xg_matrix( model, matrix_name, animation.evaluated_frame, include_parent );
        }

        const filetypes::xg::Matrix *static_matrix = nullptr;

        const auto static_it = model.matrices.find( matrix_name );

        if( static_it != model.matrices.end() )
        {
            static_matrix = &static_it->second;
        }

        const filetypes::xg::MatrixNode *node = model.find_matrix_node( matrix_name );

        if( node == nullptr )
        {
            return static_matrix != nullptr ? *static_matrix : filetypes::xg::identity_matrix();
        }

        filetypes::xg::Vec3 source_position = node->position;
        filetypes::xg::Vec3 target_position = node->position;
        filetypes::xg::Quat source_rotation = node->rotation;
        filetypes::xg::Quat target_rotation = node->rotation;
        filetypes::xg::Vec3 source_scale = node->scale;
        filetypes::xg::Vec3 target_scale = node->scale;

        if( const filetypes::xg::Vec3Interpolator *interp = model.find_vec3_interpolator( node->input_position ) )
        {
            source_position = stage3d::sample_xg_vec3_interpolator( *interp, animation.pose_blend_source_frame );

            target_position = stage3d::sample_xg_vec3_interpolator( *interp, animation.pose_blend_target_frame );
        }

        if( const filetypes::xg::QuatInterpolator *interp = model.find_quat_interpolator( node->input_rotation ) )
        {
            source_rotation = stage3d::sample_xg_quat_interpolator( *interp, animation.pose_blend_source_frame );

            target_rotation = stage3d::sample_xg_quat_interpolator( *interp, animation.pose_blend_target_frame );
        }

        if( const filetypes::xg::Vec3Interpolator *interp = model.find_vec3_interpolator( node->input_scale ) )
        {
            source_scale = stage3d::sample_xg_vec3_interpolator( *interp, animation.pose_blend_source_frame );

            target_scale = stage3d::sample_xg_vec3_interpolator( *interp, animation.pose_blend_target_frame );
        }

        const float weight = std::clamp( animation.pose_blend_weight, 0.0f, 1.0f );
        filetypes::xg::Matrix local =
            make_xg_animation_matrix( filetypes::xg::lerp( source_position, target_position, weight ),
                stage3d::lerp_quat_shortest( source_rotation, target_rotation, weight ),
                filetypes::xg::lerp( source_scale, target_scale, weight ) );

        if( include_parent && !node->parent_matrix.empty() )
        {
            local = filetypes::xg::multiply( evaluate_xg_matrix_blended( model, node->parent_matrix, animation, true ),
                local );
        }

        return local;
    }

    std::vector<filetypes::xg::Vertex> Stage3DRenderer::resolve_geometry_vertices( const filetypes::xg::Model &model,
        const filetypes::xg::Geometry &geometry,
        float animation_frame ) const
    {
        std::vector<filetypes::xg::Vertex> vertices = geometry.vertices;

        if( vertices.empty() || geometry.input_geometry_nodes.empty() )
        {
            return vertices;
        }

        const auto apply_vertex_targets =
            [ & ]( const std::vector<filetypes::xg::Vec3> &positions, const std::vector<std::uint32_t> &targets )
        {
            const std::size_t count = targets.empty() ? std::min( positions.size(), vertices.size() )
                                                      : std::min( positions.size(), targets.size() );

            for( std::size_t i = 0; i < count; ++i )
            {
                const std::size_t target = targets.empty() ? i : static_cast<std::size_t>( targets[ i ] );

                if( target < vertices.size() )
                {
                    vertices[ target ].position = positions[ i ];
                }
            }
        };

        const auto apply_normal_targets =
            [ & ]( const std::vector<filetypes::xg::Vec3> &normals, const std::vector<std::uint32_t> &targets )
        {
            const std::size_t count = targets.empty() ? std::min( normals.size(), vertices.size() )
                                                      : std::min( normals.size(), targets.size() );

            for( std::size_t i = 0; i < count; ++i )
            {
                const std::size_t target = targets.empty() ? i : static_cast<std::size_t>( targets[ i ] );

                if( target < vertices.size() )
                {
                    vertices[ target ].normal = filetypes::xg::normalize( normals[ i ] );
                }
            }
        };

        const auto apply_uv_targets =
            [ & ]( const std::vector<rendering::Vec2> &uvs, const std::vector<std::uint32_t> &targets )
        {
            const std::size_t count =
                targets.empty() ? std::min( uvs.size(), vertices.size() ) : std::min( uvs.size(), targets.size() );

            for( std::size_t i = 0; i < count; ++i )
            {
                const std::size_t target = targets.empty() ? i : static_cast<std::size_t>( targets[ i ] );

                if( target < vertices.size() )
                {
                    vertices[ target ].uv = uvs[ i ];

                    vertices[ target ].has_uv = true;
                }
            }
        };

        for( const std::string &input_node : geometry.input_geometry_nodes )
        {
            const filetypes::xg::ShapeInterpolator *shape = model.find_shape_interpolator( input_node );

            if( shape == nullptr )
            {
                continue;
            }

            std::vector<filetypes::xg::Vertex> sampled = stage3d::sample_shape_interpolator( *shape, animation_frame );

            if( !sampled.empty() )
            {
                vertices = std::move( sampled );
            }
        }

        for( const std::string &input_node : geometry.input_geometry_nodes )
        {
            if( const filetypes::xg::VertexInterpolator *vertex_interpolator =
                    model.find_vertex_interpolator( input_node ) )
            {
                const std::vector<filetypes::xg::Vec3> sampled =
                    stage3d::sample_vertex_interpolator( *vertex_interpolator, animation_frame );

                apply_vertex_targets( sampled, vertex_interpolator->targets );
            }
            else if( const filetypes::xg::NormalInterpolator *normal_interpolator =
                         model.find_normal_interpolator( input_node ) )
            {
                const std::vector<filetypes::xg::Vec3> sampled =
                    stage3d::sample_normal_interpolator( *normal_interpolator, animation_frame );

                apply_normal_targets( sampled, normal_interpolator->targets );
            }
            else if( const filetypes::xg::TexCoordInterpolator *uv_interpolator =
                         model.find_texcoord_interpolator( input_node ) )
            {
                const std::vector<rendering::Vec2> sampled =
                    stage3d::sample_texcoord_interpolator( *uv_interpolator, animation_frame );

                apply_uv_targets( sampled, uv_interpolator->targets );
            }
        }

        return vertices;
    }

    std::vector<Stage3DRenderer::GpuVertex> Stage3DRenderer::build_mesh_vertices( const DrawModel &model,
        const DrawMesh &mesh,
        const ResolvedAnimation &animation ) const
    {
        const float animation_frame = animation.evaluated_frame;
        const bool animate_matrices = xg_rigid_animation_enabled() && model.has_dynamic_matrices;
        const bool animate_geometry = mesh.has_geometry_interpolators;
        const bool skin_vertices =
            ( cpu_skinning_enabled() || ( gpu_skinning_enabled() && animate_geometry ) ) && model.has_envelopes;

        if( !animate_matrices && !skin_vertices && !animate_geometry )
        {
            return mesh.vertices;
        }

        if( model.gltf_asset != nullptr && mesh.gltf_primitive_index >= 0 && animate_geometry )
        {
            const std::size_t primitive_index = static_cast<std::size_t>( mesh.gltf_primitive_index );

            if( primitive_index >= model.gltf_asset->primitives.size() )
            {
                return mesh.vertices;
            }

            const runtime::model::GltfPrimitive &primitive = model.gltf_asset->primitives[ primitive_index ];
            const std::vector<runtime::model::GltfVertex> sampled =
                animation.pose_blend_active
                    ? runtime::model::sample_gltf_primitive_vertices_blended( primitive,
                          animation.pose_blend_source_frame,
                          animation.pose_blend_target_frame,
                          animation.pose_blend_weight )
                    : runtime::model::sample_gltf_primitive_vertices( primitive, animation_frame );

            std::vector<GpuVertex> output;
            output.reserve( sampled.size() );

            for( const runtime::model::GltfVertex &source : sampled )
            {
                output.push_back( GpuVertex{
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
                } );
            }

            return output;
        }

        const filetypes::xg::Geometry *geometry = model.source_model.find_geometry( mesh.geometry_name );

        if( geometry == nullptr || geometry->vertices.empty() || mesh.triangle_indices.empty() )
        {
            return mesh.vertices;
        }

        std::vector<filetypes::xg::Vertex> resolved_vertices;

        if( animate_geometry )
        {
            if( animation.pose_blend_active )
            {
                resolved_vertices =
                    resolve_geometry_vertices( model.source_model, *geometry, animation.pose_blend_source_frame );

                const std::vector<filetypes::xg::Vertex> target_vertices =
                    resolve_geometry_vertices( model.source_model, *geometry, animation.pose_blend_target_frame );

                const std::size_t count = std::min( resolved_vertices.size(), target_vertices.size() );
                const float weight = std::clamp( animation.pose_blend_weight, 0.0f, 1.0f );

                for( std::size_t i = 0; i < count; ++i )
                {
                    resolved_vertices[ i ] =
                        stage3d::lerp_vertex( resolved_vertices[ i ], target_vertices[ i ], weight );
                }
            }
            else
            {
                resolved_vertices = resolve_geometry_vertices( model.source_model, *geometry, animation_frame );
            }
        }
        else
        {
            resolved_vertices = geometry->vertices;
        }

        const std::vector<filetypes::xg::Vertex> *source_vertices = &resolved_vertices;
        std::vector<filetypes::xg::Vertex> skinned_vertices;

        if( skin_vertices )
        {
            skinned_vertices = resolved_vertices;

            source_vertices = &skinned_vertices;

            for( const std::string &input_node : geometry->input_geometry_nodes )
            {
                const filetypes::xg::Envelope *envelope = model.source_model.find_envelope( input_node );

                if( envelope == nullptr || envelope->input_geometry.empty() )
                {
                    continue;
                }

                const filetypes::xg::Geometry *envelope_geometry =
                    model.source_model.find_geometry( envelope->input_geometry );

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

                    const filetypes::xg::Vertex &position_source = envelope_geometry->vertices[ source_index ];
                    const filetypes::xg::Vec4 weight = envelope->weights[ envelope_index ];
                    const float weights[ 4 ] = {
                        weight.x,
                        weight.y,
                        weight.z,
                        weight.w
                    };

                    filetypes::xg::Matrix skin_matrices[ 4 ]{};
                    bool has_skin_matrix[ 4 ]{};

                    filetypes::xg::Vec3 skinned_position{};
                    float total_weight = 0.0f;

                    for( std::size_t slot = 0; slot < 4; ++slot )
                    {
                        if( std::abs( weights[ slot ] ) <= 0.000001f || envelope->input_matrices[ slot ].empty() )
                        {
                            continue;
                        }

                        const filetypes::xg::Bone *bone =
                            model.source_model.find_bone( envelope->input_matrices[ slot ] );

                        if( bone == nullptr || bone->input_matrix.empty() )
                        {
                            continue;
                        }

                        const filetypes::xg::Matrix bone_pose =
                            evaluate_xg_matrix_blended( model.source_model, bone->input_matrix, animation, true );

                        const filetypes::xg::Matrix skin_matrix = make_skin_matrix( bone_pose, bone->rest_matrix );
                        skin_matrices[ slot ] = skin_matrix;

                        has_skin_matrix[ slot ] = true;

                        const filetypes::xg::Vec3 p = stage3d::transform_point( skin_matrix, position_source.position );
                        skinned_position.x += p.x * weights[ slot ];

                        skinned_position.y += p.y * weights[ slot ];

                        skinned_position.z += p.z * weights[ slot ];

                        total_weight += weights[ slot ];
                    }

                    if( total_weight <= 0.000001f )
                    {
                        continue;
                    }

                    if( std::abs( total_weight - 1.0f ) > 0.0001f )
                    {
                        skinned_position.x /= total_weight;

                        skinned_position.y /= total_weight;

                        skinned_position.z /= total_weight;
                    }

                    for( std::uint32_t target_index : envelope->vertex_targets[ envelope_index ] )
                    {
                        if( target_index < skinned_vertices.size() )
                        {

                            const filetypes::xg::Vec3 target_normal = resolved_vertices[ target_index ].normal;
                            filetypes::xg::Vec3 skinned_normal{};

                            for( std::size_t slot = 0; slot < 4; ++slot )
                            {
                                if( !has_skin_matrix[ slot ] )
                                {
                                    continue;
                                }

                                const filetypes::xg::Vec3 n =
                                    stage3d::transform_vector( skin_matrices[ slot ], target_normal );

                                skinned_normal.x += n.x * weights[ slot ];

                                skinned_normal.y += n.y * weights[ slot ];

                                skinned_normal.z += n.z * weights[ slot ];
                            }

                            skinned_vertices[ target_index ].position = skinned_position;

                            skinned_vertices[ target_index ].normal = filetypes::xg::normalize( skinned_normal );
                        }
                    }
                }
            }
        }

        filetypes::xg::Matrix local_matrix = mesh.static_local_matrix;

        if( animate_matrices && !mesh.matrix_chain.empty() )
        {
            local_matrix = filetypes::xg::identity_matrix();

            for( const std::string &matrix_name : mesh.matrix_chain )
            {
                local_matrix = filetypes::xg::multiply( local_matrix,
                    evaluate_xg_matrix_blended( model.source_model, matrix_name, animation, true ) );
            }
        }

        std::vector<GpuVertex> vertices;
        vertices.reserve( mesh.triangle_indices.size() );

        for( std::uint32_t index : mesh.triangle_indices )
        {
            if( index >= source_vertices->size() )
            {
                continue;
            }

            const filetypes::xg::Vertex &source = ( *source_vertices )[ index ];
            const filetypes::xg::Vec3 p = stage3d::transform_point( local_matrix, source.position );
            const filetypes::xg::Vec3 n = stage3d::transform_vector( local_matrix, source.normal );
            rendering::Color color = source.color;
            vertices.push_back( GpuVertex{
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

        return vertices.empty() ? mesh.vertices : vertices;
    }
}
