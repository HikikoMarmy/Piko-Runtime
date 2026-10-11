#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace piko::runtime::model
{
    struct GltfDocumentSummary
    {
        std::filesystem::path path;
        std::string version;
        std::string generator;
        bool binary = false;
        std::int32_t default_scene = -1;
        std::size_t scene_count = 0;
        std::size_t node_count = 0;
        std::size_t mesh_count = 0;
        std::size_t primitive_count = 0;
        std::size_t material_count = 0;
        std::size_t texture_count = 0;
        std::size_t image_count = 0;
        std::size_t sampler_count = 0;
        std::size_t skin_count = 0;
        std::size_t animation_count = 0;
        std::size_t camera_count = 0;
        std::size_t buffer_count = 0;
        std::size_t buffer_view_count = 0;
        std::size_t accessor_count = 0;
        std::vector<std::string> warnings;
    };

    [[nodiscard]] bool tinygltf_available();
    [[nodiscard]] GltfDocumentSummary inspect_gltf( const std::filesystem::path &path );
}
