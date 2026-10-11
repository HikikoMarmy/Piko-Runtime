#pragma once

#include "Rendering/RenderTypes.hpp"

#include <cstdint>
#include <stdexcept>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace piko::filetypes::xg
{
    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Quat
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
    };

    struct Matrix
    {
        float m[ 16 ]{};
    };

    struct Vec4
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;
    };

    struct Vertex
    {
        Vec3 position{};
        Vec3 normal{ 0.0f, 1.0f, 0.0f };
        rendering::Color color{ 1.0f, 1.0f, 1.0f, 1.0f };
        rendering::Vec2 uv{};
        bool has_uv = false;
    };

    struct Geometry
    {
        std::string name;
        std::vector<Vertex> vertices;
        std::vector<std::string> input_geometry_nodes;
    };

    struct Material
    {
        std::string name;
        std::string texture_name;
        rendering::Color diffuse{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::uint32_t blend_type = 0;
        std::uint32_t shading_type = 0;
        std::uint32_t flags = 0;
        std::uint32_t texture_env = 0;
        std::uint32_t u_tile = 0;
        std::uint32_t v_tile = 0;
        bool uses_texture_alpha = false;
    };

    struct MultiPassMaterial
    {
        std::string name;
        std::vector<std::string> material_names;
    };

    struct Vec3Interpolator
    {
        std::string name;
        std::uint32_t type = 0;
        std::vector<float> times;
        std::vector<Vec3> keys;
        std::vector<std::uint32_t> targets;
    };

    struct QuatInterpolator
    {
        std::string name;
        std::uint32_t type = 0;
        std::vector<float> times;
        std::vector<Quat> keys;
    };

    struct VertexInterpolator
    {
        std::string name;
        std::uint32_t type = 0;
        std::vector<float> times;
        std::vector<std::vector<Vec3>> keys;
        std::vector<std::uint32_t> targets;
    };

    struct NormalInterpolator
    {
        std::string name;
        std::uint32_t type = 0;
        std::vector<float> times;
        std::vector<std::vector<Vec3>> keys;
        std::vector<std::uint32_t> targets;
    };

    struct TexCoordInterpolator
    {
        std::string name;
        std::uint32_t type = 0;
        std::vector<float> times;
        std::vector<std::vector<rendering::Vec2>> keys;
        std::vector<std::uint32_t> targets;
    };

    struct ShapeInterpolator
    {
        std::string name;
        std::uint32_t type = 0;
        std::vector<float> times;
        std::vector<std::vector<Vertex>> keys;
    };

    struct MatrixNode
    {
        std::string name;
        Vec3 position{};
        Quat rotation{};
        Vec3 scale{ 1.0f, 1.0f, 1.0f };
        std::string input_position;
        std::string input_rotation;
        std::string input_scale;
        std::string parent_matrix;
    };

    struct Bone
    {
        std::string name;
        Matrix rest_matrix{};
        std::string input_matrix;
    };

    struct Envelope
    {
        std::string name;
        std::uint32_t start_vertex = 0;
        std::vector<Vec4> weights;
        std::vector<std::vector<std::uint32_t>> vertex_targets;
        std::string input_geometry;
        std::string input_matrices[ 4 ];
    };

    struct DagMesh
    {
        std::string name;
        std::string geometry_name;
        std::string material_name;
        std::uint32_t prim_type = 0;
        std::uint32_t cull_func = 0;
        std::vector<std::uint32_t> triangle_indices;
    };

    struct DagTransform
    {
        std::string name;
        std::string matrix_name;
    };

    struct DagNode
    {
        std::string name;
        std::vector<std::string> children;
    };

    struct Model
    {
        std::vector<Geometry> geometries;
        std::vector<Material> materials;
        std::vector<MultiPassMaterial> multipass_materials;
        std::vector<Vec3Interpolator> vec3_interpolators;
        std::vector<QuatInterpolator> quat_interpolators;
        std::vector<VertexInterpolator> vertex_interpolators;
        std::vector<NormalInterpolator> normal_interpolators;
        std::vector<TexCoordInterpolator> texcoord_interpolators;
        std::vector<ShapeInterpolator> shape_interpolators;
        std::vector<MatrixNode> matrix_nodes;
        std::vector<Bone> bones;
        std::vector<Envelope> envelopes;
        std::vector<DagMesh> meshes;
        std::vector<DagTransform> transforms;
        std::vector<DagNode> dag_nodes;
        std::unordered_map<std::string, Matrix> matrices;

        [[nodiscard]] const Geometry *find_geometry( const std::string &name ) const;
        [[nodiscard]] const Material *find_material( const std::string &name ) const;
        [[nodiscard]] const MultiPassMaterial *find_multipass_material( const std::string &name ) const;
        [[nodiscard]] const Vec3Interpolator *find_vec3_interpolator( const std::string &name ) const;
        [[nodiscard]] const QuatInterpolator *find_quat_interpolator( const std::string &name ) const;
        [[nodiscard]] const VertexInterpolator *find_vertex_interpolator( const std::string &name ) const;
        [[nodiscard]] const NormalInterpolator *find_normal_interpolator( const std::string &name ) const;
        [[nodiscard]] const TexCoordInterpolator *find_texcoord_interpolator( const std::string &name ) const;
        [[nodiscard]] const ShapeInterpolator *find_shape_interpolator( const std::string &name ) const;
        [[nodiscard]] const MatrixNode *find_matrix_node( const std::string &name ) const;
        [[nodiscard]] const Bone *find_bone( const std::string &name ) const;
        [[nodiscard]] const Envelope *find_envelope( const std::string &name ) const;
        [[nodiscard]] const DagMesh *find_mesh( const std::string &name ) const;
        [[nodiscard]] const DagTransform *find_transform( const std::string &name ) const;
        [[nodiscard]] const DagNode *find_dag_node( const std::string &name ) const;
    };

    class DecodeError final : public std::runtime_error
    {
    public:
        explicit DecodeError( const std::string &message );
    };

    [[nodiscard]] Model decode( std::span<const std::uint8_t> bytes );

    [[nodiscard]] Matrix identity_matrix();
    [[nodiscard]] Vec3 normalize( Vec3 value );
    [[nodiscard]] Quat normalize_quaternion( Quat value );
    [[nodiscard]] Vec3 lerp( const Vec3 &a, const Vec3 &b, float t );
    [[nodiscard]] Matrix inverse_affine_matrix( const Matrix &matrix );
    [[nodiscard]] Matrix multiply( const Matrix &a, const Matrix &b );
    [[nodiscard]] Matrix make_trs( const Vec3 &position, const Quat &rotation, const Vec3 &scale );
    [[nodiscard]] Matrix convert_lh_matrix_to_rh( const Matrix &matrix );
}
