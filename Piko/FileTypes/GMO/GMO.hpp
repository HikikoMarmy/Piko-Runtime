#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace piko::filetypes::gmo
{
    inline constexpr std::uint32_t invalid_index = 0xFFFFFFFFu;

    class DecodeError : public std::runtime_error
    {
    public:
        explicit DecodeError( const std::string &message );
    };

    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Vec4
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
    };

    struct Matrix
    {
        std::array<float, 16> values{};
    };

    struct Vertex
    {
        Vec3 position{};
        Vec3 normal{ 0.0f, 1.0f, 0.0f };
        Vec2 uv{};
        Vec4 color{};
        std::array<float, 8> weights{};
        std::uint32_t weight_count = 0;
        bool has_normal = false;
        bool has_uv = false;
        bool has_color = false;
    };

    struct VertexArray
    {
        std::string name;
        std::uint32_t vertex_type = 0;
        std::uint32_t vertex_count = 0;
        std::uint32_t morph_count = 0;
        std::uint32_t stride = 0;
        std::uint32_t index_format = 0;
        std::vector<Vertex> vertices;
    };

    struct DrawPrimitive
    {
        std::uint32_t array_index = invalid_index;
        std::uint32_t primitive_type = 0;
        std::uint32_t flags = 0;
        std::vector<std::uint32_t> indices;
    };

    struct Mesh
    {
        std::string name;
        std::uint32_t material_index = invalid_index;
        std::vector<std::uint32_t> bone_palette;
        std::vector<DrawPrimitive> draws;
    };

    struct Part
    {
        std::string name;
        std::vector<Mesh> meshes;
        std::vector<VertexArray> arrays;
    };

    struct Node
    {
        std::string name;
        std::uint32_t parent_index = invalid_index;
        std::uint32_t part_index = invalid_index;
        std::vector<std::uint32_t> part_indices;
        Vec3 translation{};
        Vec4 rotation{};
        Vec3 scale{ 1.0f, 1.0f, 1.0f };
        std::vector<std::uint32_t> bone_nodes;
        std::vector<Matrix> inverse_bind_matrices;
    };

    struct Material
    {
        std::string name;
        Vec4 diffuse{};
        std::uint32_t texture_index = invalid_index;
        bool has_blend_function = false;
        std::uint32_t blend_operation = 0;
        std::uint32_t blend_source = 0;
        std::uint32_t blend_destination = 0;
        std::vector<std::array<std::uint32_t, 2>> states;
    };

    struct Texture
    {
        std::string name;
        std::string source_name;
        std::vector<std::uint8_t> embedded_data;
    };

    struct Curve
    {
        std::string name;
        std::uint32_t format = 0;
        std::uint32_t dimensions = 0;
        std::vector<float> times;
        std::vector<float> values;
    };

    struct MotionChannel
    {
        std::uint32_t node_index = invalid_index;
        std::uint32_t target_attribute = 0;
        std::uint32_t curve_index = invalid_index;
    };

    struct Motion
    {
        std::string name;
        float start_frame = 0.0f;
        float end_frame = 0.0f;
        float playback_rate = 0.0f;
        bool has_playback_rate = false;
        std::vector<Curve> curves;
        std::vector<MotionChannel> channels;
    };

    struct Model
    {
        std::string name;
        std::vector<Node> nodes;
        std::vector<Part> parts;
        std::vector<Material> materials;
        std::vector<Texture> textures;
        std::vector<Motion> motions;
    };

    struct PakEntry
    {
        std::uint32_t index = 0;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
        std::string model_name;
    };

    bool is_gmo( std::span<const std::uint8_t> bytes );
    Model decode( std::span<const std::uint8_t> bytes );
    std::vector<PakEntry> decode_pak_entries( std::span<const std::uint8_t> bytes, bool decode_model_names = true );
}
