#include "Input/SourceFactory.hpp"
#include "Model/GmoGltfConverter.hpp"
#include "FileTypes/GMO/GMO.hpp"
#include "Region/SourceScanner.hpp"
#include "Runtime/RuntimeBuilder.hpp"
#include "Utility/PathNormalize.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace piko::convert::cli
{
    void print_usage()
    {
        std::cout
            << "Convert - Piko runtime builder\n"
               "\n"
               "Primary usage:\n"
               "  Convert build-runtime --platform auto|ps2|psp --input <iso-or-folder> --output <piko-root> [--lang US|JP|EN|FR|GR|auto] [--audio flac|opus] [--overwrite]\n"
               "\n"
               "Quick GMO usage:\n"
               "  Drag a PSP .XG, .GMO, or .PAK file onto Convert.exe\n"
               "  Convert <gmo-or-pak> [--out <output-directory>] [--entry <index>]\n"
               "\n"
               "Debug/dev usage:\n"
               "  Convert scan <iso-or-folder>\n"
               "  Convert list <iso-or-folder> [virtual-prefix]\n"
               "  Convert get-file <iso-or-folder> <virtual-path> --out <output-file>\n"
               "  Convert gmo-list <gmo-pak>\n"
               "  Convert gmo-to-gltf <gmo-or-pak> --out <output-directory> [--entry <index>]\n"
               "\n"
               "Examples:\n"
               "  Convert build-runtime --platform auto --input D:\\Games\\Gitaroo.iso --output D:\\Piko --overwrite\n"
               "  Convert scan D:\\Games\\Gitaroo.iso\n"
               "  Convert list D:\\Games\\Gitaroo.iso PROJECTS/STDATA\n"
               "  Convert get-file D:\\Games\\Gitaroo.iso PROJECTS/STDATA/US/STAGE01/SONGDATA/ST01.IMC --out ST01.IMC\n"
               "  Convert gmo-list ST02XG.PAK\n"
               "  Convert gmo-to-gltf ST02XG.PAK --entry 36 --out converted\\stage02\n"
               "  Convert FLYING_O.XG\n";
    }

    std::string find_argument( std::int32_t argc, char **argv, std::string_view name )
    {
        for( std::int32_t i = 0; i < argc - 1; ++i )
        {
            if( std::string_view( argv[ i ] ) == name )
            {
                return argv[ i + 1 ];
            }
        }

        return {};
    }

    bool has_argument( std::int32_t argc, char **argv, std::string_view name )
    {
        for( std::int32_t i = 0; i < argc; ++i )
        {
            if( std::string_view( argv[ i ] ) == name )
            {
                return true;
            }
        }

        return false;
    }

    std::int32_t command_build_runtime( std::int32_t argc, char **argv )
    {
        RuntimeBuildOptions options;
        options.platform = find_argument( argc, argv, "--platform" );

        options.region = find_argument( argc, argv, "--region" );

        options.language = find_argument( argc, argv, "--lang" );

        if( options.language.empty() )
        {
            options.language = find_argument( argc, argv, "--language" );
        }

        options.input_path = find_argument( argc, argv, "--input" );

        options.output_path = find_argument( argc, argv, "--output" );
        const std::string audio = find_argument( argc, argv, "--audio" );

        if( audio == "opus" )
        {
            options.audio_compression = AudioCompression::Opus;
        }
        else if( audio.empty() || audio == "flac" )
        {
            options.audio_compression = AudioCompression::Flac;
        }
        else
        {
            throw std::runtime_error( "--audio must be flac or opus" );
        }

        options.overwrite_existing = has_argument( argc, argv, "--overwrite" );

        if( options.platform.empty() )
        {
            options.platform = "auto";
        }

        if( options.region.empty() )
        {
            options.region = "auto";
        }

        if( options.language.empty() )
        {
            options.language = "auto";
        }

        const RuntimeBuildResult result = RuntimeBuilder{}.build( options );

        if( !result.success )
        {
            for( const std::string &error : result.errors )
            {
                std::cerr << "error: " << error << '\n';
            }

            return 1;
        }

        for( const std::string &warning : result.warnings )
        {
            std::cout << "warning: " << warning << '\n';
        }

        std::cout << "\nBuild complete.\n";
        std::cout << "Files written: " << result.files_written << '\n';
        std::cout << "Bytes written: " << result.bytes_written << '\n';
        return 0;
    }

    std::int32_t command_scan( std::int32_t argc, char **argv )
    {
        if( argc < 3 )
        {
            print_usage();
            return 2;
        }

        const std::filesystem::path source_path = argv[ 2 ];
        auto source = open_game_source( source_path );

        SourceScanner scanner;
        const SourceProfile profile = scanner.scan( *source );

        const auto files = source->list_all_files();

        std::cout << "Source: " << source_path.string() << '\n';
        std::cout << "Files indexed: " << files.size() << '\n';
        std::cout << "Platform: " << profile.platform << '\n';
        std::cout << "Region: " << profile.region << '\n';

        if( !profile.disc_id.empty() )
        {
            std::cout << "Disc ID: " << profile.disc_id << '\n';
        }

        if( !profile.executable_path.empty() )
        {
            std::cout << "Executable: " << profile.executable_path << '\n';
        }

        if( !profile.content_roots.empty() )
        {
            std::cout << "Content roots:\n";

            for( const auto &root : profile.content_roots )
            {
                std::cout << "  " << root << '\n';
            }
        }

        if( !profile.language_folders.empty() )
        {
            std::cout << "Language/content folders:\n";

            for( const auto &folder : profile.language_folders )
            {
                std::cout << "  " << folder << '\n';
            }
        }

        if( !profile.stage_ids.empty() )
        {
            std::cout << "Stages:\n";

            for( const auto &stage : profile.stage_ids )
            {
                std::cout << "  " << stage << '\n';
            }
        }

        std::cout << "\nUseful file samples:\n";
        std::int32_t printed = 0;

        for( const auto &file : files )
        {
            if( file.path.find( "STDATA" ) != std::string::npos || file.path.find( "STAGE" ) != std::string::npos ||
                file.path.ends_with( ".IMC" ) || file.path.ends_with( ".CHC" ) || file.path.ends_with( ".SSQ" ) ||
                file.path.ends_with( ".IMX" ) || file.path.ends_with( ".XGM" ) )
            {
                std::cout << "  " << file.path << "  (" << file.size << " bytes)\n";

                if( ++printed >= 40 )
                {
                    break;
                }
            }
        }

        return 0;
    }

    std::int32_t command_list( std::int32_t argc, char **argv )
    {
        if( argc < 3 )
        {
            print_usage();
            return 2;
        }

        const std::filesystem::path source_path = argv[ 2 ];
        const std::string prefix = argc >= 4 ? normalize_virtual_path( argv[ 3 ] ) : std::string{};

        auto source = open_game_source( source_path );

        const auto files = source->list_all_files();

        for( const auto &file : files )
        {
            if( virtual_path_starts_with( file.path, prefix ) )
            {
                std::cout << file.path << "  (" << file.size << " bytes)\n";
            }
        }

        return 0;
    }

    std::vector<std::uint8_t> read_binary_file( const std::filesystem::path &path )
    {
        std::ifstream file( path, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not open input file: " + path.string() );
        }

        file.seekg( 0, std::ios::end );
        const std::streamoff size = file.tellg();

        if( size < 0 )
        {
            throw std::runtime_error( "Could not determine input size: " + path.string() );
        }

        file.seekg( 0, std::ios::beg );

        std::vector<std::uint8_t> bytes( static_cast<std::size_t>( size ) );

        if( !bytes.empty() )
        {
            file.read( reinterpret_cast<char *>( bytes.data() ), static_cast<std::streamsize>( size ) );
        }

        if( !file )
        {
            throw std::runtime_error( "Could not read input file: " + path.string() );
        }

        return bytes;
    }

    std::vector<std::uint32_t> find_entry_arguments( std::int32_t argc, char **argv )
    {
        std::vector<std::uint32_t> entries;

        for( std::int32_t index = 0; index + 1 < argc; ++index )
        {
            if( std::string_view( argv[ index ] ) != "--entry" )
            {
                continue;
            }

            const std::string value = argv[ index + 1 ];
            std::size_t parsed = 0;
            const unsigned long number = std::stoul( value, &parsed, 10 );

            if( parsed != value.size() || number > std::numeric_limits<std::uint32_t>::max() )
            {
                throw std::runtime_error( "Invalid GMO PAK entry index: " + value );
            }

            entries.push_back( static_cast<std::uint32_t>( number ) );
        }

        return entries;
    }

    std::filesystem::path default_gmo_output_path( const std::filesystem::path &input_path )
    {
        std::filesystem::path filename = input_path.stem();
        filename += "_gltf";
        return input_path.parent_path() / filename;
    }

    bool is_quick_gmo_input( const std::filesystem::path &path )
    {
        if( !std::filesystem::is_regular_file( path ) )
        {
            return false;
        }

        std::string extension = path.extension().string();

        for( char &character : extension )
        {
            if( character >= 'a' && character <= 'z' )
            {
                character = static_cast<char>( character - 'a' + 'A' );
            }
        }

        return extension == ".XG" || extension == ".GMO" || extension == ".PAK";
    }

    void write_gmo_conversion_log( const std::filesystem::path &output_path,
        const std::filesystem::path &input_path,
        const std::vector<GmoConversionResult> &results )
    {
        std::filesystem::create_directories( output_path );
        const std::filesystem::path log_path = output_path / "conversion.log";
        std::ofstream log( log_path, std::ios::binary );

        if( !log )
        {
            return;
        }

        log << "Input: " << input_path.string() << '\n';
        log << "Output: " << output_path.string() << '\n';
        log << "Models: " << results.size() << "\n\n";

        for( const GmoConversionResult &result : results )
        {
            log << result.model_name << '\n';
            log << "  glTF: " << result.gltf_path.string() << '\n';
            log << "  nodes: " << result.node_count << '\n';
            log << "  primitives: " << result.primitive_count << '\n';
            log << "  animations: " << result.animation_count << '\n';

            for( const std::string &warning : result.warnings )
            {
                log << "  warning: " << warning << '\n';
            }

            log << '\n';
        }
    }

    std::int32_t command_quick_gmo( std::int32_t argc, char **argv )
    {
        const std::filesystem::path input_path = argv[ 1 ];
        const std::string output_argument = find_argument( argc, argv, "--out" );
        const std::filesystem::path output_path =
            output_argument.empty() ? default_gmo_output_path( input_path ) : std::filesystem::path( output_argument );

        const std::vector<std::uint8_t> bytes = read_binary_file( input_path );
        std::vector<GmoConversionResult> results;

        if( piko::filetypes::gmo::is_gmo( bytes ) )
        {
            results.push_back( convert_gmo_to_gltf( bytes, output_path ) );
        }
        else
        {
            const std::vector<std::uint32_t> entries = find_entry_arguments( argc, argv );
            results = convert_gmo_pak_to_gltf( bytes, output_path, entries );

            if( results.empty() )
            {
                throw std::runtime_error( "No GMO PAK entries matched the requested selection" );
            }
        }

        write_gmo_conversion_log( output_path, input_path, results );

        std::size_t warning_count = 0;

        for( const GmoConversionResult &result : results )
        {
            std::cout << "Converted " << result.model_name << ": " << result.node_count << " nodes, "
                      << result.primitive_count << " primitives, " << result.animation_count << " animations\n";

            for( const std::string &warning : result.warnings )
            {
                ++warning_count;
                std::cout << "warning: " << warning << '\n';
            }
        }

        std::cout << "\nOutput: " << output_path.string() << '\n';
        std::cout << "Models converted: " << results.size() << '\n';
        std::cout << "Warnings: " << warning_count << '\n';
        std::cout << "Log: " << ( output_path / "conversion.log" ).string() << '\n';
        return 0;
    }

    std::int32_t command_gmo_list( std::int32_t argc, char **argv )
    {
        if( argc < 3 )
        {
            print_usage();
            return 2;
        }

        const std::vector<std::uint8_t> bytes = read_binary_file( argv[ 2 ] );
        const std::vector<piko::filetypes::gmo::PakEntry> entries = piko::filetypes::gmo::decode_pak_entries( bytes );

        for( const piko::filetypes::gmo::PakEntry &entry : entries )
        {
            std::cout << entry.index << "  " << entry.model_name << "  (" << entry.size << " bytes, offset 0x"
                      << std::hex << entry.offset << std::dec << ")\n";
        }

        return 0;
    }

    std::int32_t command_gmo_to_gltf( std::int32_t argc, char **argv )
    {
        if( argc < 5 )
        {
            print_usage();
            return 2;
        }

        const std::filesystem::path input_path = argv[ 2 ];
        const std::string output_argument = find_argument( argc, argv, "--out" );

        if( output_argument.empty() )
        {
            throw std::runtime_error( "gmo-to-gltf requires --out <output-directory>" );
        }

        const std::vector<std::uint8_t> bytes = read_binary_file( input_path );
        const std::filesystem::path output_path = output_argument;

        if( piko::filetypes::gmo::is_gmo( bytes ) )
        {
            const GmoConversionResult result = convert_gmo_to_gltf( bytes, output_path );
            std::cout << "Converted " << result.model_name << '\n';
            std::cout << "Nodes: " << result.node_count << '\n';
            std::cout << "Primitives: " << result.primitive_count << '\n';
            std::cout << "Animations: " << result.animation_count << '\n';
            std::cout << "Output: " << result.gltf_path.string() << '\n';

            for( const std::string &warning : result.warnings )
            {
                std::cout << "warning: " << warning << '\n';
            }

            return 0;
        }

        const std::vector<std::uint32_t> entries = find_entry_arguments( argc, argv );
        const std::vector<GmoConversionResult> results = convert_gmo_pak_to_gltf( bytes, output_path, entries );

        if( results.empty() )
        {
            throw std::runtime_error( "No GMO PAK entries matched the requested selection" );
        }

        for( const GmoConversionResult &result : results )
        {
            std::cout << "Converted " << result.model_name << ": " << result.node_count << " nodes, "
                      << result.primitive_count << " primitives, " << result.animation_count << " animations\n";

            for( const std::string &warning : result.warnings )
            {
                std::cout << "warning: " << warning << '\n';
            }
        }

        return 0;
    }

    std::int32_t command_get_file( std::int32_t argc, char **argv )
    {
        if( argc < 6 )
        {
            print_usage();
            return 2;
        }

        const std::filesystem::path source_path = argv[ 2 ];
        const std::string virtual_path = argv[ 3 ];
        const std::string output_path = find_argument( argc, argv, "--out" );

        if( output_path.empty() )
        {
            throw std::runtime_error( "get-file requires --out <output-file>" );
        }

        auto source = open_game_source( source_path );

        const auto data = source->get_file( virtual_path );

        const std::filesystem::path out = output_path;

        if( out.has_parent_path() )
        {
            std::filesystem::create_directories( out.parent_path() );
        }

        std::ofstream file( out, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not create output file: " + out.string() );
        }

        if( !data.empty() )
        {
            file.write( reinterpret_cast<const char *>( data.data() ), static_cast<std::streamsize>( data.size() ) );
        }

        if( !file )
        {
            throw std::runtime_error( "Could not write output file: " + out.string() );
        }

        std::cout << "Wrote " << data.size() << " bytes: " << out.string() << '\n';
        return 0;
    }
}

int main( int argc, char **argv )
{
    try
    {
        if( argc < 2 )
        {
            piko::convert::cli::print_usage();
            return 2;
        }

        const std::filesystem::path direct_input = argv[ 1 ];

        if( piko::convert::cli::is_quick_gmo_input( direct_input ) )
        {
            return piko::convert::cli::command_quick_gmo( argc, argv );
        }

        const std::string command = argv[ 1 ];

        if( command == "build-runtime" )
        {
            return piko::convert::cli::command_build_runtime( argc, argv );
        }

        if( command == "scan" )
        {
            return piko::convert::cli::command_scan( argc, argv );
        }

        if( command == "list" )
        {
            return piko::convert::cli::command_list( argc, argv );
        }

        if( command == "get-file" )
        {
            return piko::convert::cli::command_get_file( argc, argv );
        }

        if( command == "gmo-list" )
        {
            return piko::convert::cli::command_gmo_list( argc, argv );
        }

        if( command == "gmo-to-gltf" )
        {
            return piko::convert::cli::command_gmo_to_gltf( argc, argv );
        }

        piko::convert::cli::print_usage();
        return 2;
    }
    catch( const std::exception &e )
    {
        std::cerr << "error: " << e.what() << '\n';

        if( argc >= 2 )
        {
            const std::filesystem::path input_path = argv[ 1 ];

            if( piko::convert::cli::is_quick_gmo_input( input_path ) )
            {
                const std::string output_argument = piko::convert::cli::find_argument( argc, argv, "--out" );
                const std::filesystem::path output_path =
                    output_argument.empty() ? piko::convert::cli::default_gmo_output_path( input_path )
                                            : std::filesystem::path( output_argument );

                std::filesystem::create_directories( output_path );
                std::ofstream log( output_path / "conversion_error.log", std::ios::binary );

                if( log )
                {
                    log << "Input: " << input_path.string() << '\n' << "Error: " << e.what() << '\n';
                }
            }
        }

        return 1;
    }
}
