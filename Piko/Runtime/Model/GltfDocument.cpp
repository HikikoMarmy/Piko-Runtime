#include "GltfDocument.hpp"
#include "Utils/StrUtil.hpp"
#include "Utils/FileUtil.hpp"

#include "ThirdParty/TinyGLTFConfig.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace piko::runtime::model
{
    bool tinygltf_available()
    {
#if PIKO_HAS_TINYGLTF
        return true;
#else
        return false;
#endif
    }

    GltfDocumentSummary inspect_gltf( const std::filesystem::path &path )
    {
#if PIKO_HAS_TINYGLTF
        if( path.empty() )
        {
            throw std::runtime_error( "No glTF path was provided" );
        }

        if( !std::filesystem::is_regular_file( path ) )
        {
            throw std::runtime_error( "glTF file was not found: " + utils::path_to_utf8( path ) );
        }

        const std::string extension = piko::utils::lowercase_ascii( utils::path_to_utf8( path.extension() ) );

        if( extension != ".gltf" && extension != ".glb" )
        {
            throw std::runtime_error( "Expected a .gltf or .glb file: " + utils::path_to_utf8( path ) );
        }

        tinygltf::TinyGLTF loader;
        tinygltf::Model model;
        std::string warning_text;
        std::string error_text;

        const bool binary = extension == ".glb";
        const bool loaded =
            binary ? loader.LoadBinaryFromFile( &model, &error_text, &warning_text, utils::path_to_utf8( path ) )
                   : loader.LoadASCIIFromFile( &model, &error_text, &warning_text, utils::path_to_utf8( path ) );

        if( !loaded )
        {
            std::ostringstream message;
            message << "TinyGLTF could not load " << utils::path_to_utf8( path );

            if( !error_text.empty() )
            {
                message << ": " << error_text;
            }

            throw std::runtime_error( message.str() );
        }

        GltfDocumentSummary summary{};
        summary.path = path.lexically_normal();

        summary.version = model.asset.version;

        summary.generator = model.asset.generator;

        summary.binary = binary;

        summary.default_scene = model.defaultScene;

        summary.scene_count = model.scenes.size();

        summary.node_count = model.nodes.size();

        summary.mesh_count = model.meshes.size();

        summary.material_count = model.materials.size();

        summary.texture_count = model.textures.size();

        summary.image_count = model.images.size();

        summary.sampler_count = model.samplers.size();

        summary.skin_count = model.skins.size();

        summary.animation_count = model.animations.size();

        summary.camera_count = model.cameras.size();

        summary.buffer_count = model.buffers.size();

        summary.buffer_view_count = model.bufferViews.size();

        summary.accessor_count = model.accessors.size();

        summary.warnings = piko::utils::split_lines( warning_text );

        for( const tinygltf::Mesh &mesh : model.meshes )
        {
            summary.primitive_count += mesh.primitives.size();
        }

        return summary;
#else
        (void)path;
        throw std::runtime_error( "TinyGLTF is not available." );
#endif
    }
}
