#include "piko.hpp"

#include "Runtime/Model/GltfDocument.hpp"
#include "Utils/FileUtil.hpp"

#if defined( _WIN32 ) && !defined( _DEBUG )
#include <SDL3/SDL_main.h>
#endif

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>

int main( int argc, char **argv )
{
    bool force_runtime_import = false;
    std::optional<std::filesystem::path> inspect_gltf_path;

    for( std::int32_t index = 1; index < argc; ++index )
    {
        const std::string_view argument( argv[ index ] );

        if( argument == "--import-runtime" )
        {
            force_runtime_import = true;
        }
        else if( argument == "--inspect-gltf" )
        {
            if( index + 1 >= argc )
            {
                std::cerr << "--inspect-gltf requires a .gltf or .glb path\n";
                return 1;
            }

#if defined( _WIN32 ) && !defined( _DEBUG )
            inspect_gltf_path = piko::utils::path_from_utf8( argv[ ++index ] );
#else
            inspect_gltf_path = std::filesystem::path( argv[ ++index ] );
#endif
        }
    }

    try
    {
        if( inspect_gltf_path.has_value() )
        {
            const piko::runtime::model::GltfDocumentSummary summary =
                piko::runtime::model::inspect_gltf( *inspect_gltf_path );

            std::cout << "TinyGLTF: ready\n"
                      << "File: " << piko::utils::path_to_utf8( summary.path ) << '\n'
                      << "Container: " << ( summary.binary ? "GLB" : "glTF" ) << '\n'
                      << "Version: " << summary.version << '\n'
                      << "Generator: " << summary.generator << '\n'
                      << "Default scene: " << summary.default_scene << '\n'
                      << "Scenes: " << summary.scene_count << '\n'
                      << "Nodes: " << summary.node_count << '\n'
                      << "Meshes: " << summary.mesh_count << '\n'
                      << "Primitives: " << summary.primitive_count << '\n'
                      << "Materials: " << summary.material_count << '\n'
                      << "Textures: " << summary.texture_count << '\n'
                      << "Images: " << summary.image_count << '\n'
                      << "Skins: " << summary.skin_count << '\n'
                      << "Animations: " << summary.animation_count << '\n'
                      << "Buffers: " << summary.buffer_count << '\n'
                      << "Buffer views: " << summary.buffer_view_count << '\n'
                      << "Accessors: " << summary.accessor_count << '\n';

            for( const std::string &warning : summary.warnings )
            {
                std::cout << "Warning: " << warning << '\n';
            }

            return 0;
        }

        piko::App app;

        if( !app.initialize( force_runtime_import ) )
        {
            return 1;
        }

        app.run();
        app.shutdown();

        return 0;
    }
    catch( const std::exception &e )
    {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return 1;
    }
}
