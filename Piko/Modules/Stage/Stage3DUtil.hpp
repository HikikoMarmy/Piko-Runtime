#pragma once

#include "FileTypes/SSQ/SSQ.hpp"
#include "FileTypes/XG/XG.hpp"
#include "Rendering/RenderTypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace piko::modules::stage3d
{
    inline constexpr float debug_camera_pullback_units_per_second = 160.0f;
    inline constexpr std::uint32_t max_gpu_skin_bones = 96;

    [[nodiscard]] std::string gltf_source_model_name( const std::filesystem::path &path );
    [[nodiscard]] float normalize_angle_radians( float value );

    [[nodiscard]] filetypes::xg::Vec3 transform_point( const filetypes::xg::Matrix &matrix,
        const filetypes::xg::Vec3 &point );

    [[nodiscard]] filetypes::xg::Vec3 transform_vector( const filetypes::xg::Matrix &matrix,
        const filetypes::xg::Vec3 &vector );

    [[nodiscard]] filetypes::xg::Vec3 cross_vec3( const filetypes::xg::Vec3 &a, const filetypes::xg::Vec3 &b );
    [[nodiscard]] filetypes::xg::Vec3 rotate_vector_by_quat( filetypes::xg::Quat rotation,
        const filetypes::xg::Vec3 &vector );

    [[nodiscard]] filetypes::xg::Matrix transpose_matrix( const filetypes::xg::Matrix &matrix );
    [[nodiscard]] float determinant_3x3( const filetypes::xg::Matrix &matrix );
    [[nodiscard]] std::uint32_t cull_func_after_winding_transform( std::uint32_t cull_func,
        const filetypes::xg::Matrix &winding_matrix );

    void set_xg_cull_state( std::uint32_t cull_func );
    [[nodiscard]] std::array<float, 9> matrix3_from_matrix4( const filetypes::xg::Matrix &matrix );
    [[nodiscard]] std::array<float, 9> normal_matrix3_from_model( const filetypes::xg::Matrix &matrix );
    [[nodiscard]] filetypes::xg::Vec3 transform_normal( const filetypes::xg::Matrix &matrix,
        const filetypes::xg::Vec3 &normal );

    [[nodiscard]] float key_lerp_t( float frame, float current_frame, float next_frame, float reciprocal );
    [[nodiscard]] filetypes::xg::Vertex lerp_vertex( const filetypes::xg::Vertex &a,
        const filetypes::xg::Vertex &b,
        float t );

    [[nodiscard]] filetypes::xg::Quat identity_quat();
    [[nodiscard]] filetypes::xg::Quat axis_angle_quat( float x, float y, float z, float radians );
    [[nodiscard]] filetypes::xg::Quat lerp_quat_shortest( filetypes::xg::Quat a, filetypes::xg::Quat b, float t );
    [[nodiscard]] filetypes::xg::Quat slerp_quat_shortest( filetypes::xg::Quat a, filetypes::xg::Quat b, float t );
    [[nodiscard]] filetypes::xg::Quat conjugate_quat( filetypes::xg::Quat rotation );

    [[nodiscard]] filetypes::xg::Vec3 sample_vec3_keys( const std::vector<filetypes::ssq::Vec3Key> &keys,
        const filetypes::xg::Vec3 &fallback,
        float frame );

    [[nodiscard]] rendering::Color sample_color_keys( const std::vector<filetypes::ssq::ColorKey> &keys,
        const rendering::Color &fallback,
        float frame );

    [[nodiscard]] filetypes::ssq::LightColorKey sample_light_color_keys(
        const std::vector<filetypes::ssq::LightColorKey> &keys,
        const rendering::Color &primary_fallback,
        const rendering::Color &secondary_fallback,
        float frame );

    [[nodiscard]] filetypes::xg::Quat light_rotation_key_value( const filetypes::ssq::LightRotationKey &key, bool raw );
    [[nodiscard]] filetypes::xg::Quat sample_light_rotation_keys(
        const std::vector<filetypes::ssq::LightRotationKey> &keys,
        const filetypes::xg::Quat &fallback,
        float frame,
        bool raw );

    [[nodiscard]] filetypes::xg::Quat quat_key_value( const filetypes::ssq::QuatKey &key, bool raw );
    [[nodiscard]] filetypes::xg::Quat sample_quat_keys( const std::vector<filetypes::ssq::QuatKey> &keys,
        const filetypes::xg::Quat &fallback,
        float frame,
        bool raw );

    [[nodiscard]] filetypes::xg::Vec3 sample_scale_keys( const std::vector<filetypes::ssq::ScaleKey> &keys,
        const filetypes::xg::Vec3 &fallback,
        float frame );

    [[nodiscard]] std::size_t interpolator_key_index( std::size_t key_count,
        const std::vector<float> &times,
        float frame,
        float *t );

    [[nodiscard]] filetypes::xg::Vec3 sample_xg_vec3_interpolator( const filetypes::xg::Vec3Interpolator &interpolator,
        float frame );

    [[nodiscard]] filetypes::xg::Quat sample_xg_quat_interpolator( const filetypes::xg::QuatInterpolator &interpolator,
        float frame );

    [[nodiscard]] std::vector<filetypes::xg::Vertex> sample_shape_interpolator(
        const filetypes::xg::ShapeInterpolator &interpolator,
        float frame );

    [[nodiscard]] std::vector<filetypes::xg::Vec3> sample_vertex_interpolator(
        const filetypes::xg::VertexInterpolator &interpolator,
        float frame );

    [[nodiscard]] std::vector<filetypes::xg::Vec3> sample_normal_interpolator(
        const filetypes::xg::NormalInterpolator &interpolator,
        float frame );

    [[nodiscard]] std::vector<rendering::Vec2> sample_texcoord_interpolator(
        const filetypes::xg::TexCoordInterpolator &interpolator,
        float frame );

    void sample_projection_keys( const std::vector<filetypes::ssq::ProjectionKey> &keys,
        float frame,
        float &fov_degrees,
        float &aspect,
        float &z_near,
        float &z_far );

    [[nodiscard]] bool material_alpha_is_active( std::uint32_t blend_type, std::uint32_t flags );
    [[nodiscard]] float material_alpha_cutoff( std::uint32_t blend_type, std::uint32_t flags );
    [[nodiscard]] bool material_should_use_blend_pass( const filetypes::xg::Material &material );
    [[nodiscard]] bool texcoord_interpolator_needs_repeat( const filetypes::xg::TexCoordInterpolator &interpolator );
    [[nodiscard]] bool material_should_wrap_texture( const filetypes::xg::Material &material );
    [[nodiscard]] filetypes::xg::Matrix transpose_rotation_inverse_rigid( const filetypes::xg::Matrix &world );
}
