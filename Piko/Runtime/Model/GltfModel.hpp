#pragma once

#include "FileTypes/XG/XG.hpp"
#include "Rendering/RenderTypes.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace piko::runtime::model
{
    struct AnimationTiming
    {
        float playback_length = 0.0f;
        float keyframe_interval = 0.0f;
        float start_keyframe = 0.0f;
        std::uint32_t speed_mode = 0;
    };

    struct GltfVertex
    {
        filetypes::xg::Vec3 position{};
        filetypes::xg::Vec3 normal{ 0.0f, 1.0f, 0.0f };
        rendering::Vec2 uv{};
        rendering::Color color{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::array<std::uint16_t, 4> joints{};
        std::array<float, 4> weights{};
    };

    struct GltfMaterial
    {
        std::string name;
        std::filesystem::path texture_path;
        rendering::Color diffuse{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::uint32_t blend_type = 0;
        std::uint32_t shading_type = 0;
        std::uint32_t flags = 0;
        std::uint32_t texture_env = 0;
        std::uint32_t u_tile = 0;
        std::uint32_t v_tile = 0;
        bool uses_texture_alpha = false;
        bool wrap_texture = true;
    };

    struct GltfPrimitive
    {
        struct Vec3Track
        {
            std::uint32_t interpolation_type = 0;
            std::vector<float> times;
            std::vector<std::vector<filetypes::xg::Vec3>> frames;
        };

        struct Vec2Track
        {
            std::uint32_t interpolation_type = 0;
            std::vector<float> times;
            std::vector<std::vector<rendering::Vec2>> frames;
        };

        std::string name;
        std::vector<GltfVertex> vertices;
        std::vector<std::uint32_t> indices;
        Vec3Track position_track;
        Vec3Track normal_track;
        Vec2Track texcoord_track;
        std::int32_t node_index = -1;
        std::int32_t skin_index = -1;
        std::int32_t material_index = -1;
        std::uint32_t cull_func = 0;
        bool skinned = false;
    };

    struct GltfNode
    {
        std::string name;
        std::int32_t parent = -1;
        std::vector<std::int32_t> children;
        filetypes::xg::Vec3 translation{};
        filetypes::xg::Quat rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
        filetypes::xg::Vec3 scale{ 1.0f, 1.0f, 1.0f };
        filetypes::xg::Matrix matrix{};
        bool has_matrix = false;
    };

    enum class GltfChannelPath
    {
        Translation,
        Rotation,
        Scale
    };

    struct GltfAnimationChannel
    {
        std::int32_t node_index = -1;
        GltfChannelPath path = GltfChannelPath::Translation;
        std::string interpolation;
        std::vector<float> times;
        std::vector<std::array<float, 4>> values;
    };

    struct GltfAnimationClip
    {
        std::string name;
        float start_frame = 0.0f;
        float end_frame = 0.0f;
        float playback_rate = 0.0f;
        bool uses_gmo_timing = false;
        bool uses_model_playback_speed = false;
        std::vector<GltfAnimationChannel> channels;
    };

    struct GltfSkin
    {
        std::string name;
        std::vector<std::int32_t> joints;
        std::vector<filetypes::xg::Matrix> inverse_bind_matrices;
    };

    struct GltfModelAsset
    {
        std::filesystem::path source_path;
        std::string source_model_name;
        float frame_rate = 30.0f;
        std::int32_t global_animation = 0;
        bool indexed_animations = false;
        bool gmo_model = false;
        std::vector<AnimationTiming> animation_timings;
        std::vector<GltfNode> nodes;
        std::vector<GltfMaterial> materials;
        std::vector<GltfPrimitive> primitives;
        std::vector<GltfSkin> skins;
        std::vector<GltfAnimationClip> animation_clips;
    };

    [[nodiscard]] GltfModelAsset load_gltf_model( const std::filesystem::path &path );

    [[nodiscard]] filetypes::xg::Matrix evaluate_gltf_node_world( const GltfModelAsset &asset,
        std::int32_t node_index,
        std::uint32_t animation_index,
        float animation_frame );

    [[nodiscard]] filetypes::xg::Matrix evaluate_gltf_node_world_blended( const GltfModelAsset &asset,
        std::int32_t node_index,
        std::uint32_t source_animation_index,
        float source_frame,
        std::uint32_t target_animation_index,
        float target_frame,
        float blend_weight );

    [[nodiscard]] std::vector<filetypes::xg::Matrix> build_gltf_skin_matrices( const GltfModelAsset &asset,
        std::int32_t skin_index,
        std::int32_t mesh_node_index,
        std::uint32_t animation_index,
        float animation_frame,
        bool blend_active,
        std::uint32_t source_animation_index,
        float source_frame,
        std::uint32_t target_animation_index,
        float target_frame,
        float blend_weight );

    [[nodiscard]] std::vector<GltfVertex> sample_gltf_primitive_vertices( const GltfPrimitive &primitive,
        float animation_frame );

    [[nodiscard]] std::vector<GltfVertex> sample_gltf_primitive_vertices_blended( const GltfPrimitive &primitive,
        float source_frame,
        float target_frame,
        float blend_weight );
}
