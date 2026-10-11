#include "Runtime/RuntimeBuilder.hpp"

#include "Utility/JsonUtil.hpp"

#include "Audio/ImcConverter.hpp"
#include "Audio/PspAtracConverter.hpp"
#include "Chart/ChcJsonWriter.hpp"
#include "Image/ImageConverter.hpp"
#include "Image/PngWriter.hpp"
#include "Utils/BattleTexturePixels.hpp"
#include "Image/GimConverter.hpp"
#include "Input/SourceFactory.hpp"
#include "Model/GmoGltfConverter.hpp"
#include "Model/XgGltfConverter.hpp"
#include "FileTypes/AlbumDef/AlbumDef.hpp"
#include "FileTypes/GMO/GMO.hpp"
#include "FileTypes/SSQ/SSQ.hpp"
#include "FileTypes/SSQ/SSQJson.hpp"
#include "Utility/PathNormalize.hpp"
#include "Video/PssConverter.hpp"
#include "Utils/StrUtil.hpp"

#include <cstdint>
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace piko::convert::runtime_builder
{
    constexpr std::string_view ps2_stdata_dir = "PROJECTS/STDATA";

    void report_progress( const RuntimeBuildOptions &options,
        float fraction,
        std::string status,
        std::string current_file = {} )
    {
        if( !options.progress_callback )
        {
            return;
        }

        options.progress_callback( RuntimeBuildProgress{
            .fraction = std::clamp( fraction, 0.0f, 1.0f ),
            .status = std::move( status ),
            .current_file = std::move( current_file )
        } );
    }

    void export_note_textures( const std::filesystem::path &game_data )
    {
        const auto directory = game_data / "PIKO/ui/battle/notes/Piko";

        const auto images = ui::generated_note_images();
        const std::array<const char *, 3> filenames{
            "ribbon.png",
            "cap.png",
            "note.png"
        };

        for( std::size_t index = 0; index < images.size(); ++index )
        {
            const auto path = directory / filenames[ index ];

            if( !std::filesystem::exists( path ) )
            {
                const auto &image = images[ index ];
                write_rgba_png( path, image.width, image.height, image.pixels );
            }
        }

        const auto trace_path = directory / "trace.png";

        if( !std::filesystem::exists( trace_path ) )
        {
            write_rgba_png( trace_path,
                ui::generated_trace_texture_width,
                ui::generated_trace_texture_height,
                ui::make_generated_trace_texture_pixels() );
        }
    }

    void write_result_atlas( const std::filesystem::path &path,
        std::string_view platform,
        std::string_view common_texture,
        std::string_view stage_texture,
        std::string_view no_miss_texture )
    {
        std::filesystem::create_directories( path.parent_path() );
        std::ofstream output( path, std::ios::binary );

        if( !output )
        {
            throw std::runtime_error( "Could not write result atlas: " + path.string() );
        }

        output
            << "{\n"
            << "  \"format\": \"piko-result-atlas\",\n"
            << "  \"version\": 1,\n"
            << "  \"platform\": \"" << json_util::escape_string( platform, json_util::EscapeMode::Text ) << "\",\n"
            << "  \"textures\": {\n"
            << "    \"common\": \"" << json_util::escape_string( common_texture, json_util::EscapeMode::Text )
            << "\",\n"
            << "    \"stage\": \"" << json_util::escape_string( stage_texture, json_util::EscapeMode::Text ) << "\",\n"
            << "    \"no_miss\": \"" << json_util::escape_string( no_miss_texture, json_util::EscapeMode::Text )
            << "\"\n"
            << "  },\n"
            << "  \"rects\": {\n"
            << "    \"background\": [295, 232, 216, 159],\n"
            << "    \"common_base\": [1, 1, 462, 427],\n"
            << "    \"stage_emblem\": [275, 1, 236, 236],\n"
            << "    \"stage_title\": [1, 1, 230, 191],\n"
            << "    \"stat_panel\": [1, 198, 131, 131],\n"
            << "    \"stat_labels\": [[1, 430, 94, 22], [1, 454, 94, 22], [1, 478, 94, 22], [97, 430, 94, 22]],\n"
            << "    \"punctuation\": [477, 430, 5, 22],\n"
            << "    \"percent\": [443, 430, 32, 17],\n"
            << "    \"score_label\": [193, 464, 318, 47],\n"
            << "    \"digit\": [193, 430, 23, 17],\n"
            << "    \"digit_stride\": 25,\n"
            << "    \"ranks\": [[134, 300, 141, 100], [219, 402, 147, 109], [368, 411, 143, 100], [134, 198, 128, 100], [134, 300, 141, 100], [86, 408, 131, 103]],\n"
            << "    \"no_miss\": [0, 0, 256, 64]\n"
            << "  }\n"
            << "}\n";

        if( !output )
        {
            throw std::runtime_error( "Could not write result atlas: " + path.string() );
        }
    }

    void write_result_atlases( const std::filesystem::path &converted_root )
    {
        write_result_atlas( converted_root / "UI" / "RESULT" / "PS2" / "atlas.json",
            "PS2",
            "LOADING/TEXTURES/STANDALONE/RSLT_ALL.png",
            "LOADING/TEXTURES/STANDALONE/RSLT_S{stage2}.png",
            "LOADING/TEXTURES/STANDALONE/NOMISS.png" );

        const auto duet_path = converted_root / "UI/RESULT/PSP_DUET/atlas.json";
        std::filesystem::create_directories( duet_path.parent_path() );
        std::ofstream duet( duet_path, std::ios::binary );
        duet << R"({
  "format": "piko-result-atlas",
  "version": 1,
  "platform": "PSP_DUET",
  "textures": {
    "common": "UI/RESULT/MRSLT_ELEMENTS1.png",
    "stage": "UI/RESULT/MRSLT_ELEMENTS1.png",
    "no_miss": ""
  },
  "rects": {
    "background": [0, 0, 480, 272],
    "portraits": [[189, 274, 92, 71], [281, 274, 92, 71]],
    "win": [0, 346, 199, 61],
    "lose": [199, 346, 199, 61],
    "ranks": [[430, 405, 46, 38], [292, 405, 46, 38], [338, 405, 46, 38], [384, 405, 46, 38], [430, 405, 46, 38], [246, 405, 46, 38]]
  }
})";

        if( !duet )
        {
            throw std::runtime_error( "Could not write PSP duet result atlas" );
        }
    }

    void write_stage_select_atlas( const std::filesystem::path &converted_root )
    {
        const std::filesystem::path path = converted_root / "UI" / "STAGE_SELECT" / "atlas.json";
        std::filesystem::create_directories( path.parent_path() );
        std::ofstream output( path, std::ios::binary );

        if( !output )
        {
            throw std::runtime_error( "Could not write stage-select atlas: " + path.string() );
        }

        output
            << "{\n"
            << "  \"format\": \"piko-stage-select-atlas\",\n"
            << "  \"version\": 1,\n"
            << "  \"textures\": {\"portraits\": \"MENU/TEXTURES/STGSEL/STSEL02.png\"},\n"
            << "  \"rects\": {\n"
            << "    \"portraits\": [[0, 0, 115, 121], [115, 0, 115, 121], [230, 0, 115, 121], [345, 0, 115, 121], [0, 121, 115, 121], [115, 121, 115, 121], [230, 121, 115, 121], [345, 121, 115, 121], [0, 242, 115, 121], [115, 242, 115, 121]]\n"
            << "  }\n"
            << "}\n";
    }

    void normalize_album_movie_arguments( filetypes::albumdef::AlbumDef &album )
    {
        for( auto &[ page_id, page ] : album.pages )
        {
            (void)page_id;

            for( std::string &argument : page.args )
            {
                std::string normalized = piko::utils::normalize_slashes( argument );

                const std::size_t version = normalized.find( ';' );

                if( version != std::string::npos )
                {
                    normalized.erase( version );
                }

                const std::filesystem::path source_path( normalized );

                if( piko::utils::uppercase_ascii( source_path.extension().string() ) != ".PSS" )
                {
                    continue;
                }

                std::filesystem::path filename = source_path.filename();
                filename.replace_extension( ".mp4" );
                argument = ( std::filesystem::path( "MOVIES" ) / filename ).generic_string();
            }
        }
    }

    bool looks_like_stage_id( std::string_view value )
    {
        return value.size() == 7u && value.rfind( "STAGE", 0 ) == 0u &&
               std::isdigit( static_cast<unsigned char>( value[ 5 ] ) ) &&
               std::isdigit( static_cast<unsigned char>( value[ 6 ] ) );
    }

    bool is_menu_or_ui_group( std::string_view value )
    {
        const std::string text( value );
        return text.find( "MENU" ) != std::string::npos || text.find( "TITLE" ) != std::string::npos ||
               text.find( "SELECT" ) != std::string::npos || text.find( "OPTION" ) != std::string::npos ||
               text.find( "RESULT" ) != std::string::npos || text.find( "RANK" ) != std::string::npos ||
               text.find( "SOUND" ) != std::string::npos;
    }

    bool is_runtime_group( std::string_view value )
    {
        return looks_like_stage_id( value ) || is_menu_or_ui_group( value );
    }

    std::string extension( std::string_view path )
    {
        const std::string filename = virtual_filename( path );
        const std::size_t dot = filename.find_last_of( '.' );
        return dot == std::string::npos ? std::string{} : piko::utils::uppercase_ascii( filename.substr( dot ) );
    }

    std::string stem( std::string_view path )
    {
        std::string filename = virtual_filename( path );
        const std::size_t dot = filename.find_last_of( '.' );

        if( dot != std::string::npos )
        {
            filename.erase( dot );
        }

        return filename;
    }

    bool is_runtime_source_extension( std::string_view path )
    {
        const std::string ext = extension( path );
        return ext == ".IMC" || ext == ".IMX" || ext == ".XGM" || ext == ".XG" || ext == ".CHC" || ext == ".SSQ" ||
               ext == ".WEB" || ext == ".BIN";
    }

    std::filesystem::path movie_relative_path( std::string_view source_path, std::string_view language )
    {
        std::string normalized = normalize_virtual_path( source_path );

        const std::array<std::string_view, 3> markers{
            "PROJECTS/MOOVS/",
            "MOOVS/",
            "MOVIE/"
        };

        for( const std::string_view marker : markers )
        {
            const std::size_t marker_offset = normalized.find( marker );

            if( marker_offset == std::string::npos )
            {
                continue;
            }

            normalized = normalized.substr( marker_offset + marker.size() );
            break;
        }

        const std::string normalized_language = piko::utils::uppercase_ascii( std::string( language ) );

        if( !normalized_language.empty() )
        {
            const std::string language_prefix = normalized_language + "/";

            if( normalized.rfind( language_prefix, 0u ) == 0u )
            {
                normalized.erase( 0u, language_prefix.size() );
            }
        }

        std::filesystem::path relative = normalized;
        relative.replace_extension( ".mp4" );
        return relative;
    }

    bool ps2_movie_matches_language( std::string_view path, std::string_view language )
    {
        const std::string normalized = piko::utils::uppercase_ascii( normalize_virtual_path( path ) );
        const std::string marker = "PROJECTS/MOOVS/";
        const std::size_t offset = normalized.find( marker );

        if( offset == std::string::npos )
        {
            return true;
        }

        const std::string relative = normalized.substr( offset + marker.size() );
        const std::size_t slash = relative.find( '/' );

        if( slash == std::string::npos )
        {
            return true;
        }

        const std::string folder = relative.substr( 0u, slash );

        if( folder != "US" && folder != "EN" && folder != "FR" && folder != "GR" && folder != "JP" )
        {
            return true;
        }

        std::string selected = piko::utils::uppercase_ascii( std::string( language ) );

        if( selected == "JA" )
        {
            selected = "JP";
        }
        else if( selected == "DE" )
        {
            selected = "GR";
        }

        return folder == selected;
    }

    std::size_t convert_ps2_movies( const IGameSource &source,
        std::span<const SourceFileInfo> files,
        const std::filesystem::path &converted_root,
        const std::filesystem::path &ffmpeg,
        std::string_view language,
        const RuntimeBuildOptions &options,
        RuntimeBuildResult &result )
    {
        std::size_t converted = 0u;
        std::size_t movie_count = 0u;

        for( const SourceFileInfo &file : files )
        {
            if( !file.directory && extension( file.path ) == ".PSS" &&
                ps2_movie_matches_language( file.path, language ) )
            {
                ++movie_count;
            }
        }

        std::size_t movie_index = 0u;

        for( const SourceFileInfo &file : files )
        {
            if( file.directory || extension( file.path ) != ".PSS" ||
                !ps2_movie_matches_language( file.path, language ) )
            {
                continue;
            }

            const float movie_fraction = movie_count == 0u ? 0.96f
                                                           : 0.76f + 0.20f * static_cast<float>( movie_index ) /
                                                                         static_cast<float>( movie_count );

            report_progress( options, movie_fraction, "Converting movie", file.path );
            ++movie_index;

            try
            {
                const std::filesystem::path output =
                    converted_root / "MOVIES" / movie_relative_path( file.path, language );

                std::cout << "converting movie " << file.path << '\n';
                convert_pss_to_mp4( source.get_file( file.path ), output, ffmpeg );
                ++converted;
            }
            catch( const std::exception &error )
            {
                result.warnings.push_back( file.path + ": " + error.what() );
            }
        }

        return converted;
    }

    std::string normalize_language( std::string value )
    {
        value = piko::utils::uppercase_ascii( value );

        if( value.empty() || value == "AUTO" )
        {
            return {};
        }

        if( value == "JA" )
        {
            return "JP";
        }

        if( value == "DE" )
        {
            return "GR";
        }

        if( value == "EU" || value == "PAL" )
        {
            return "EN";
        }

        return value;
    }

    std::string default_language_for_region( std::string region )
    {
        region = piko::utils::lowercase_ascii( region );

        if( region == "jp" )
        {
            return "JP";
        }

        if( region == "eu" || region == "pal" )
        {
            return "EN";
        }

        return "US";
    }

    std::string selected_language( const SourceProfile &profile, const RuntimeBuildOptions &options )
    {
        if( const std::string language = normalize_language( options.language ); !language.empty() )
        {
            return language;
        }

        if( const std::string language = normalize_language( options.region ); !language.empty() )
        {
            return language;
        }

        return default_language_for_region( profile.region );
    }

    std::string prefix_for_language( std::string_view language )
    {
        if( normalize_language( std::string( language ) ) == "JP" )
        {
            return std::string( ps2_stdata_dir );
        }

        return normalize_virtual_path( std::string( ps2_stdata_dir ) + "/" + std::string( language ) );
    }

    bool prefix_has_runtime_groups( const IGameSource &source, std::string_view prefix )
    {
        if( !source.exists( prefix ) || !source.is_directory( prefix ) )
        {
            return false;
        }

        for( const std::string &entry : source.list_directory( prefix ) )
        {
            if( is_runtime_group( entry ) )
            {
                return true;
            }
        }

        return false;
    }

    std::string find_content_prefix( const IGameSource &source,
        const SourceProfile &profile,
        const RuntimeBuildOptions &options )
    {
        std::vector<std::string> languages{
            selected_language( profile, options ),
            default_language_for_region( profile.region ),
            "US",
            "EN",
            "FR",
            "GR",
            "JP"
        };

        std::set<std::string> tested;

        for( const std::string &language : languages )
        {
            const std::string prefix = prefix_for_language( language );

            if( tested.insert( prefix ).second && prefix_has_runtime_groups( source, prefix ) )
            {
                return prefix;
            }
        }

        if( prefix_has_runtime_groups( source, "" ) )
        {
            return {};
        }

        throw std::runtime_error( "Could not find PS2 stage data under PROJECTS/STDATA or at the source root" );
    }

    std::string remove_prefix( std::string_view path, std::string_view prefix )
    {
        const std::string normalized_path = normalize_virtual_path( path );
        const std::string normalized_prefix = normalize_virtual_path( prefix );

        if( normalized_prefix.empty() )
        {
            return normalized_path;
        }

        if( normalized_path == normalized_prefix )
        {
            return {};
        }

        if( virtual_path_starts_with( normalized_path, normalized_prefix ) )
        {
            return normalized_path.substr( normalized_prefix.size() + 1u );
        }

        return normalized_path;
    }

    std::filesystem::path game_data_root( const RuntimeBuildOptions &options )
    {
        if( piko::utils::uppercase_ascii( options.output_path.filename().string() ) == "GAME_DATA" )
        {
            return options.output_path;
        }

        return options.output_path / "GAME_DATA";
    }

    std::filesystem::path converted_runtime_root( const std::filesystem::path &game_data, std::string_view language )
    {
        return game_data / normalize_language( std::string( language ) );
    }

    std::string psp_content_prefix( const IGameSource &source )
    {
        for( const std::string &candidate : { "PSP_GAME/USRDIR/DATA", "USRDIR/DATA" } )
        {
            if( source.exists( candidate ) && source.is_directory( candidate ) )
            {
                return candidate;
            }
        }

        throw std::runtime_error( "Could not find PSP data under PSP_GAME/USRDIR/DATA" );
    }

    std::string existing_ps2_language( const std::filesystem::path &game_data )
    {
        for( const std::string &language : { "US", "EN", "FR", "GR", "JP" } )
        {
            if( std::filesystem::is_regular_file( game_data / language / "manifest.json" ) )
            {
                return language;
            }
        }

        throw std::runtime_error( "PSP import requires a PS2 Piko game-data import first" );
    }

    std::string selected_psp_language( const std::filesystem::path &game_data, const RuntimeBuildOptions &options )
    {
        if( const std::string language = normalize_language( options.language ); !language.empty() )
        {
            return language;
        }

        return existing_ps2_language( game_data );
    }

    bool looks_like_psp_stage( std::string_view value )
    {
        return value.size() == 4u && value.rfind( "ST", 0u ) == 0u &&
               std::isdigit( static_cast<unsigned char>( value[ 2 ] ) ) &&
               std::isdigit( static_cast<unsigned char>( value[ 3 ] ) );
    }

    std::string psp_stage_group( std::string_view stage )
    {
        if( !looks_like_psp_stage( stage ) )
        {
            throw std::runtime_error( "Invalid PSP stage directory: " + std::string( stage ) );
        }

        return "STAGE" + std::string( stage.substr( 2u ) );
    }

    std::string psp_model_stage_group( std::string_view path )
    {
        const std::string name = piko::utils::uppercase_ascii( stem( path ) );

        if( name.size() < 6u || name.rfind( "ST", 0u ) != 0u ||
            !std::isdigit( static_cast<unsigned char>( name[ 2u ] ) ) ||
            !std::isdigit( static_cast<unsigned char>( name[ 3u ] ) ) || name[ 4u ] != 'X' || name[ 5u ] != 'G' )
        {
            return {};
        }

        return "STAGE" + name.substr( 2u, 2u );
    }

    std::string psp_audio_folder( std::string_view language )
    {
        return normalize_language( std::string( language ) ) == "JP" ? "MSJ" : "MSE";
    }

    std::string psp_texture_language_folder( std::string_view language )
    {
        const std::string normalized = normalize_language( std::string( language ) );

        if( normalized == "JP" )
        {
            return "J";
        }

        if( normalized == "FR" )
        {
            return "F";
        }

        if( normalized == "GR" )
        {
            return "G";
        }

        if( normalized == "KR" || normalized == "KO" )
        {
            return "K";
        }

        return "E";
    }

    std::string stage_group_from_psp_texture( std::string_view filename )
    {
        const std::string name = piko::utils::uppercase_ascii( stem( filename ) );

        if( name.size() < 4u || name.rfind( "ST", 0u ) != 0u ||
            !std::isdigit( static_cast<unsigned char>( name[ 2u ] ) ) ||
            !std::isdigit( static_cast<unsigned char>( name[ 3u ] ) ) )
        {
            return {};
        }

        return "STAGE" + name.substr( 2u, 2u );
    }

    bool is_psp_only_visual_stage_group( std::string_view group )
    {
        return group == "STAGE11" || group == "STAGE12";
    }

    bool convert_psp_gim_asset( const IGameSource &source,
        std::string_view source_path,
        const std::filesystem::path &output,
        const std::filesystem::path &centre_directory,
        RuntimeBuildResult &result )
    {
        try
        {
            const ConvertedTexture converted = convert_gim_to_png( source.get_file( source_path ),
                output,
                virtual_filename( source_path ),
                centre_directory );

            std::cout << "converting PSP GIM " << source_path << " -> " << output.string() << " (" << converted.width
                      << 'x' << converted.height << ")\n";

            return true;
        }
        catch( const std::exception &error )
        {
            result.warnings.push_back( std::string( source_path ) + ": " + error.what() );
            return false;
        }
    }

    std::size_t convert_psp_ui_and_stage_textures( const IGameSource &source,
        std::span<const SourceFileInfo> files,
        std::string_view content_prefix,
        std::string_view language,
        const std::filesystem::path &converted_root,
        const std::filesystem::path &centre_directory,
        const RuntimeBuildOptions &options,
        RuntimeBuildResult &result )
    {
        const std::string tex_root = normalize_virtual_path( std::string( content_prefix ) + "/TEX" );
        const std::string localized_root =
            normalize_virtual_path( tex_root + "/" + psp_texture_language_folder( language ) );

        const std::string animation_root = normalize_virtual_path( tex_root + "/ANIM" );
        std::size_t converted_count = 0u;
        std::size_t gim_count = 0u;

        for( const SourceFileInfo &file : files )
        {
            if( !file.directory && extension( file.path ) == ".GIM" )
            {
                ++gim_count;
            }
        }

        std::size_t gim_index = 0u;

        for( const SourceFileInfo &file : files )
        {
            if( file.directory || extension( file.path ) != ".GIM" )
            {
                continue;
            }

            const float gim_fraction =
                gim_count == 0u ? 0.32f
                                : 0.06f + 0.26f * static_cast<float>( gim_index ) / static_cast<float>( gim_count );

            ++gim_index;
            std::filesystem::path output;

            if( virtual_path_starts_with( file.path, localized_root ) )
            {
                const std::string relative = remove_prefix( file.path, localized_root );
                const std::vector<std::string> parts = split_virtual_path( relative );

                if( parts.empty() )
                {
                    continue;
                }

                const std::string name = piko::utils::uppercase_ascii( stem( parts.back() ) );

                if( parts.size() == 1u )
                {
                    if( name == "RADER" || name == "CHARGE_BLUE" || name == "CHARGE_RED" || name == "SOMKE" )
                    {
                        output = converted_root / "UI" / "STAGE" / ( stem( parts.back() ) + ".png" );
                    }
                    else if( const std::string group = stage_group_from_psp_texture( parts.back() ); !group.empty() )
                    {
                        const std::string category = name.find( "_EFF" ) != std::string::npos ? "EFFECTS" : "HUD";
                        output = converted_root / group / "TEXTURES" / category / ( stem( parts.back() ) + ".png" );
                    }
                }
                else
                {
                    const std::string directory = piko::utils::uppercase_ascii( parts.front() );

                    if( directory == "GOVR" )
                    {
                        output = converted_root / "UI" / "GAMEOVER" / ( stem( parts.back() ) + ".png" );
                    }
                    else if( directory == "MENU" )
                    {
                        if( parts.size() >= 3u && piko::utils::uppercase_ascii( parts[ 1u ] ) == "COLLECT" )
                        {
                            output =
                                converted_root / "UI" / "COLLECTION" / "COLLECT" / ( stem( parts.back() ) + ".png" );
                        }
                        else if( name == "CLCT_ELEMENTS1" || name == "CLCT_ELEMENTS2" || name == "CLCT_MSG" )
                        {
                            output = converted_root / "UI" / "COLLECTION" / ( stem( parts.back() ) + ".png" );
                        }
                        else if( name == "RSLT_ELEMENTS" || name == "NOMISS" || name == "MRSLT_ELEMENTS1" ||
                                 name == "MRSLT_ELEMENTS2" )
                        {
                            output = converted_root / "UI" / "RESULT" / ( stem( parts.back() ) + ".png" );
                        }
                        else if( name.ends_with( "_RSLT" ) )
                        {
                            if( const std::string group = stage_group_from_psp_texture( parts.back() ); !group.empty() )
                            {
                                output =
                                    converted_root / group / "TEXTURES" / "RESULT" / ( stem( parts.back() ) + ".png" );
                            }
                        }
                        else if( parts.size() >= 3u && piko::utils::uppercase_ascii( parts[ 1u ] ) == "LOAD" )
                        {
                            const std::string load_name = piko::utils::uppercase_ascii( stem( parts.back() ) );
                            const std::size_t stage_offset = load_name.find( "ST" );

                            if( stage_offset != std::string::npos && stage_offset + 4u <= load_name.size() )
                            {
                                const std::string stage_name = load_name.substr( stage_offset, 4u );

                                if( looks_like_psp_stage( stage_name ) )
                                {
                                    output = converted_root / psp_stage_group( stage_name ) / "TEXTURES" / "LOADING" /
                                             ( stem( parts.back() ) + ".png" );
                                }
                            }

                            if( output.empty() )
                            {
                                output = converted_root / "UI" / "LOADING" / ( stem( parts.back() ) + ".png" );
                            }
                        }
                    }
                    else if( directory == "CAPTION" || directory == "STG" || directory == "INTRO" )
                    {
                        if( const std::string group = stage_group_from_psp_texture( parts.back() ); !group.empty() )
                        {
                            output = converted_root / group / "TEXTURES" / directory;

                            if( parts.size() > 2u )
                            {
                                for( std::size_t index = 1u; index + 1u < parts.size(); ++index )
                                {
                                    output /= piko::utils::uppercase_ascii( parts[ index ] );
                                }
                            }

                            output /= stem( parts.back() ) + ".png";
                        }
                        else
                        {
                            output = converted_root / "UI" / directory;

                            if( parts.size() > 2u )
                            {
                                for( std::size_t index = 1u; index + 1u < parts.size(); ++index )
                                {
                                    output /= piko::utils::uppercase_ascii( parts[ index ] );
                                }
                            }

                            output /= stem( parts.back() ) + ".png";
                        }
                    }
                }
            }
            else if( virtual_path_starts_with( file.path, normalize_virtual_path( tex_root + "/GMO" ) ) )
            {
                output =
                    converted_root / "COMMON" / "TEXTURES" / "GMO" / ( stem( virtual_filename( file.path ) ) + ".png" );
            }
            else if( virtual_path_starts_with( file.path, animation_root ) )
            {
                const std::string relative = remove_prefix( file.path, animation_root );
                const std::vector<std::string> parts = split_virtual_path( relative );

                if( parts.size() >= 2u && looks_like_psp_stage( parts.front() ) )
                {
                    const std::string group = psp_stage_group( parts.front() );

                    if( is_psp_only_visual_stage_group( group ) )
                    {
                        output = converted_root / group / "TEXTURES" / "ANIM";

                        if( parts.size() > 2u )
                        {
                            for( std::size_t index = 1u; index + 1u < parts.size(); ++index )
                            {
                                output /= parts[ index ];
                            }
                        }

                        output /= stem( parts.back() ) + ".png";
                    }
                }
            }

            if( output.empty() )
            {
                continue;
            }

            const std::string output_key = piko::utils::uppercase_ascii( output.generic_string() );

            if( output_key.find( "/STAGE11/" ) == std::string::npos &&
                output_key.find( "/STAGE12/" ) == std::string::npos &&
                output_key.find( "/COMMON/TEXTURES/GMO/" ) == std::string::npos &&
                output_key.find( "/UI/" ) == std::string::npos )
            {
                continue;
            }

            report_progress( options, gim_fraction, "Converting PSP interface texture", file.path );

            if( convert_psp_gim_asset( source, file.path, output, centre_directory, result ) )
            {
                ++converted_count;
            }
        }

        return converted_count;
    }

    std::vector<PspAudioSource> collect_psp_audio( const IGameSource &source,
        std::string_view stage_prefix,
        std::string_view language )
    {
        std::vector<PspAudioSource> output;
        std::set<std::string> names;
        std::array<std::string, 2> folders{
            psp_audio_folder( language ),
            "MS"
        };

        for( const std::string &folder : folders )
        {
            const std::string directory = normalize_virtual_path( std::string( stage_prefix ) + "/" + folder );

            if( !source.exists( directory ) || !source.is_directory( directory ) )
            {
                continue;
            }

            std::vector<std::string> entries = source.list_directory( directory );
            std::sort( entries.begin(), entries.end() );

            for( const std::string &entry : entries )
            {
                const std::string name = piko::utils::uppercase_ascii( stem( entry ) );
                const std::string path = normalize_virtual_path( directory + "/" + entry );

                if( source.is_directory( path ) || !names.insert( name ).second )
                {
                    continue;
                }

                output.push_back( PspAudioSource{ name, source.get_file( path ) } );
            }
        }

        return output;
    }

    std::vector<PspAudioSource> collect_psp_collection_audio( const IGameSource &source,
        std::string_view content_prefix,
        std::string_view language )
    {
        std::vector<PspAudioSource> output;
        std::set<std::string> names;
        std::array<std::string, 2> folders{
            psp_texture_language_folder( language ),
            "E"
        };

        for( const std::string &folder : folders )
        {
            const std::string directory =
                normalize_virtual_path( std::string( content_prefix ) + "/COLLECT/" + folder );

            if( !source.exists( directory ) || !source.is_directory( directory ) )
            {
                continue;
            }

            std::vector<std::string> entries = source.list_directory( directory );
            std::sort( entries.begin(), entries.end() );

            for( const std::string &entry : entries )
            {
                const std::string name = piko::utils::uppercase_ascii( stem( entry ) );
                const std::string path = normalize_virtual_path( directory + "/" + entry );

                if( source.is_directory( path ) || name.rfind( "CHRCL", 0u ) != 0u || !names.insert( name ).second )
                {
                    continue;
                }

                output.push_back( PspAudioSource{ name, source.get_file( path ) } );
            }
        }

        return output;
    }

    std::string psp_chart_stem( std::string value )
    {
        value = piko::utils::uppercase_ascii( std::move( value ) );

        if( value.size() == 5u && value.rfind( "ST", 0u ) == 0u )
        {
            const char suffix = value.back();

            if( suffix == 'E' || suffix == 'H' || suffix == 'M' || suffix == 'N' )
            {
                value.pop_back();
                value += '_';

                value += suffix;
            }
        }

        return value;
    }

    void prepare_directory( const std::filesystem::path &path, bool overwrite )
    {
        if( std::filesystem::exists( path ) )
        {
            if( !overwrite )
            {
                throw std::runtime_error( "Runtime output already exists: " + path.string() );
            }

            std::filesystem::remove_all( path );
        }

        std::filesystem::create_directories( path );
    }

    std::pair<std::size_t, std::uint64_t> directory_stats( const std::filesystem::path &root )
    {
        std::size_t files = 0;
        std::uint64_t bytes = 0;
        std::error_code error;

        if( !std::filesystem::exists( root, error ) )
        {
            return { files, bytes };
        }

        for( const std::filesystem::directory_entry &entry : std::filesystem::recursive_directory_iterator( root ) )
        {
            if( !entry.is_regular_file() )
            {
                continue;
            }

            ++files;
            bytes += static_cast<std::uint64_t>( entry.file_size() );
        }

        return { files, bytes };
    }

    std::filesystem::path path_from_parts( const std::filesystem::path &root,
        const std::vector<std::string> &parts,
        std::size_t begin,
        std::size_t end )
    {
        std::filesystem::path result = root;

        for( std::size_t index = begin; index < end; ++index )
        {
            result /= parts[ index ];
        }

        return result;
    }

    bool contains_gmo_magic( std::span<const std::uint8_t> bytes )
    {
        constexpr std::array<std::uint8_t, 12> magic{
            'O',
            'M',
            'G',
            '.',
            '0',
            '0',
            '.',
            '1',
            'P',
            'S',
            'P',
            0
        };

        return std::search( bytes.begin(), bytes.end(), magic.begin(), magic.end() ) != bytes.end();
    }

    std::vector<std::string> unique_ssq_model_names( std::span<const std::uint8_t> bytes )
    {
        const filetypes::ssq::Scene scene = filetypes::ssq::decode( bytes );
        std::set<std::string> seen;
        std::vector<std::string> names;
        names.reserve( scene.models.size() );

        for( const filetypes::ssq::ModelEntry &model : scene.models )
        {
            const std::string key = piko::utils::uppercase_ascii( model.xg_name );

            if( key.empty() || !seen.insert( key ).second )
            {
                continue;
            }

            names.push_back( model.xg_name );
        }

        return names;
    }

    std::vector<std::string> psp_pak_entry_aliases( const IGameSource &source,
        std::string_view source_path,
        std::string_view model_root,
        std::string_view stage_root,
        std::size_t entry_count,
        RuntimeBuildResult &result )
    {
        const std::string relative = remove_prefix( source_path, model_root );
        const std::vector<std::string> parts = split_virtual_path( relative );

        if( parts.size() < 2u || !looks_like_psp_stage( parts.front() ) )
        {
            return {};
        }

        const std::string source_stage = normalize_virtual_path( std::string( stage_root ) + "/" + parts.front() );

        if( !source.exists( source_stage ) || !source.is_directory( source_stage ) )
        {
            return {};
        }

        std::vector<std::string> sequences;

        for( const std::string &entry : source.list_directory( source_stage ) )
        {
            if( extension( entry ) == ".SSQ" )
            {
                sequences.push_back( entry );
            }
        }

        const std::string preferred = piko::utils::uppercase_ascii( parts.front() + ".SSQ" );
        std::sort( sequences.begin(),
            sequences.end(),
            [ & ]( const std::string &left, const std::string &right )
            {
                const std::string left_upper = piko::utils::uppercase_ascii( left );
                const std::string right_upper = piko::utils::uppercase_ascii( right );
                const std::int32_t left_rank = left_upper == preferred               ? 0
                                               : stem( left_upper ).ends_with( "M" ) ? 2
                                                                                     : 1;

                const std::int32_t right_rank = right_upper == preferred               ? 0
                                                : stem( right_upper ).ends_with( "M" ) ? 2
                                                                                       : 1;

                if( left_rank != right_rank )
                {
                    return left_rank < right_rank;
                }

                return left_upper < right_upper;
            } );

        std::vector<std::string> mismatches;

        for( const std::string &sequence : sequences )
        {
            const std::string path = normalize_virtual_path( source_stage + "/" + sequence );

            try
            {
                std::vector<std::string> aliases = unique_ssq_model_names( source.get_file( path ) );

                if( aliases.size() == entry_count )
                {
                    return aliases;
                }

                std::ostringstream mismatch;
                mismatch << sequence << " has " << aliases.size() << " unique models";
                mismatches.push_back( mismatch.str() );
            }
            catch( const std::exception &error )
            {
                mismatches.push_back( sequence + ": " + error.what() );
            }
        }

        if( !sequences.empty() )
        {
            std::ostringstream warning;
            warning << source_path << ": PAK has " << entry_count
                    << " entries but no PSP SSQ has the same unique model count";

            if( !mismatches.empty() )
            {
                warning << " (";

                for( std::size_t index = 0; index < mismatches.size(); ++index )
                {
                    if( index != 0u )
                    {
                        warning << "; ";
                    }

                    warning << mismatches[ index ];
                }

                warning << ')';
            }

            result.warnings.push_back( warning.str() );
        }

        return {};
    }

    std::filesystem::path psp_model_output_path( std::string_view source_path,
        std::string_view model_root,
        const std::filesystem::path &converted_root )
    {
        const std::string relative = remove_prefix( source_path, model_root );
        const std::vector<std::string> parts = split_virtual_path( relative );

        if( parts.empty() )
        {
            return converted_root / "MODELS" / stem( source_path );
        }

        std::filesystem::path output = converted_root / "MODELS";
        std::size_t directory_begin = 0u;

        if( looks_like_psp_stage( parts.front() ) )
        {
            output = converted_root / psp_stage_group( parts.front() ) / "MODELS";

            directory_begin = 1u;
        }
        else if( const std::string stage_group = psp_model_stage_group( parts.back() ); !stage_group.empty() )
        {
            output = converted_root / stage_group / "MODELS";
        }

        if( directory_begin < parts.size() - 1u )
        {
            output = path_from_parts( output, parts, directory_begin, parts.size() - 1u );
        }

        return output / stem( source_path );
    }

    std::size_t convert_psp_models( const IGameSource &source,
        std::span<const SourceFileInfo> files,
        std::string_view model_root,
        std::string_view stage_root,
        const std::filesystem::path &converted_root,
        const RuntimeBuildOptions &options,
        RuntimeBuildResult &result )
    {
        std::size_t converted_model_count = 0u;
        std::size_t model_candidate_count = 0u;

        for( const SourceFileInfo &file : files )
        {
            if( file.directory || !virtual_path_starts_with( file.path, model_root ) )
            {
                continue;
            }

            const std::string ext = extension( file.path );

            if( ext == ".XG" || ext == ".GMO" || ext == ".PAK" )
            {
                ++model_candidate_count;
            }
        }

        std::size_t model_candidate_index = 0u;

        for( const SourceFileInfo &file : files )
        {
            if( file.directory || !virtual_path_starts_with( file.path, model_root ) )
            {
                continue;
            }

            const std::string relative = remove_prefix( file.path, model_root );
            const std::vector<std::string> model_parts = split_virtual_path( relative );
            std::string stage_group;

            if( !model_parts.empty() && looks_like_psp_stage( model_parts.front() ) )
            {
                stage_group = psp_stage_group( model_parts.front() );
            }

            if( stage_group.empty() && !model_parts.empty() )
            {
                stage_group = psp_model_stage_group( model_parts.back() );
            }

            if( !is_psp_only_visual_stage_group( stage_group ) )
            {
                continue;
            }

            const std::string ext = extension( file.path );

            if( ext != ".XG" && ext != ".GMO" && ext != ".PAK" )
            {
                continue;
            }

            const float model_fraction = model_candidate_count == 0u
                                             ? 0.50f
                                             : 0.34f + 0.16f * static_cast<float>( model_candidate_index ) /
                                                           static_cast<float>( model_candidate_count );

            ++model_candidate_index;
            report_progress( options, model_fraction, "Converting PSP stage model", file.path );
            const std::vector<std::uint8_t> bytes = source.get_file( file.path );
            const bool direct_gmo = filetypes::gmo::is_gmo( bytes );
            const bool gmo_package = ext == ".PAK" && contains_gmo_magic( bytes );

            if( !direct_gmo && !gmo_package )
            {
                continue;
            }

            const std::filesystem::path output = psp_model_output_path( file.path, model_root, converted_root );

            try
            {
                if( direct_gmo )
                {
                    std::cout << "converting PSP GMO model " << file.path << '\n';
                    const GmoConversionResult converted = convert_gmo_to_gltf( bytes, output );
                    ++converted_model_count;

                    for( const std::string &warning : converted.warnings )
                    {
                        result.warnings.push_back( file.path + ": " + warning );
                    }
                }
                else
                {
                    std::cout << "converting PSP GMO package " << file.path << '\n';
                    const std::vector<filetypes::gmo::PakEntry> pak_entries =
                        filetypes::gmo::decode_pak_entries( bytes );

                    const std::vector<std::string> entry_aliases =
                        psp_pak_entry_aliases( source, file.path, model_root, stage_root, pak_entries.size(), result );

                    if( !entry_aliases.empty() )
                    {
                        std::cout << "mapped " << entry_aliases.size()
                                  << " PSP GMO entries from the stage SSQ model order\n";
                    }

                    const std::vector<GmoConversionResult> converted =
                        convert_gmo_pak_to_gltf( bytes, output, {}, entry_aliases );

                    converted_model_count += converted.size();

                    for( const GmoConversionResult &model : converted )
                    {
                        for( const std::string &warning : model.warnings )
                        {
                            result.warnings.push_back( file.path + "/" + model.model_name + ": " + warning );
                        }
                    }
                }
            }
            catch( const std::exception &error )
            {
                result.warnings.push_back( file.path + ": " + error.what() );
            }
        }

        return converted_model_count;
    }

    void convert_source_file( std::string_view source_path,
        std::span<const std::uint8_t> bytes,
        std::string_view content_prefix,
        const std::filesystem::path &converted_root,
        const std::filesystem::path &centre_directory,
        const std::filesystem::path &ffmpeg,
        AudioCompression audio_compression,
        RuntimeBuildResult &result )
    {
        const std::string relative = remove_prefix( source_path, content_prefix );
        const std::vector<std::string> parts = split_virtual_path( relative );

        if( parts.empty() )
        {
            return;
        }

        const std::string &group = parts.front();
        const std::string ext = extension( source_path );

        try
        {
            if( ext == ".IMC" )
            {
                std::filesystem::path directory = converted_root / group / "AUDIO";

                if( looks_like_stage_id( group ) )
                {
                    directory /= "PS2";
                }

                directory /= stem( source_path );
                std::cout << "converting audio " << source_path << '\n';
                const ImcAudioConversionResult conversion =
                    convert_imc_audio( bytes, directory, audio_compression, ffmpeg );

                for( const FailedAudioCue &failure : conversion.failures )
                {
                    const std::string warning = std::string( source_path ) + " cue #" +
                                                std::to_string( failure.index ) + " (" + failure.name +
                                                "): " + failure.error;

                    result.warnings.push_back( warning );
                    std::cerr << "[Piko Convert] " << warning << '\n';
                }
            }
            else if( ext == ".CHC" )
            {
                std::filesystem::path output = converted_root / group / "CHARTS";

                if( looks_like_stage_id( group ) )
                {
                    output /= "PS2";
                }

                output /= stem( source_path ) + ".json";
                std::cout << "converting chart " << source_path << '\n';
                convert_chc_to_json( bytes, output );
            }
            else if( ext == ".SSQ" )
            {
                const std::filesystem::path output =
                    converted_root / group / "SEQUENCES" / ( stem( source_path ) + ".json" );

                std::cout << "converting stage sequence " << source_path << '\n';
                filetypes::ssq::save_json( filetypes::ssq::decode( bytes ), output );
            }
            else if( ext == ".IMX" )
            {
                std::filesystem::path output = converted_root / group / "TEXTURES" / "STANDALONE";

                if( parts.size() > 2u )
                {
                    output = path_from_parts( output, parts, 1u, parts.size() - 1u );
                }

                output /= stem( source_path ) + ".png";
                std::cout << "converting texture " << source_path << '\n';
                convert_imx_to_png( bytes, output, virtual_filename( source_path ), centre_directory );
            }
            else if( ext == ".XGM" )
            {
                const std::filesystem::path directory = converted_root / group / "TEXTURES" / stem( source_path );
                std::cout << "extracting package textures " << source_path << '\n';
                convert_xgm_textures_to_png( bytes, directory, centre_directory );
                const XgConversionResult converted_models =
                    convert_xgm_models_to_gltf( bytes, converted_root / group / "MODELS" );

                for( const std::string &model_name : converted_models.converted_models )
                {
                    std::cout << "converting XG model " << model_name << " from " << source_path << '\n';
                }

                for( const std::string &warning : converted_models.warnings )
                {
                    result.warnings.push_back( std::string( source_path ) + "/" + warning );
                }
            }
        }
        catch( const std::exception &error )
        {
            result.warnings.push_back( std::string( source_path ) + ": " + error.what() );
        }
    }

    std::string find_source_file_by_name( std::span<const SourceFileInfo> files,
        std::span<const std::string_view> names )
    {
        for( const SourceFileInfo &file : files )
        {
            if( file.directory )
            {
                continue;
            }

            const std::string filename = piko::utils::uppercase_ascii( virtual_filename( file.path ) );

            for( const std::string_view name : names )
            {
                if( filename == piko::utils::uppercase_ascii( std::string( name ) ) )
                {
                    return file.path;
                }
            }
        }

        return {};
    }

    std::string album_definition_source( std::span<const SourceFileInfo> files, std::string_view language )
    {
        const std::string normalized = normalize_language( std::string( language ) );

        if( normalized == "EN" )
        {
            const std::array<std::string_view, 2> names{
                "ALBUMEN.TXT",
                "ALBUMDEF.TXT"
            };

            return find_source_file_by_name( files, names );
        }

        if( normalized == "FR" )
        {
            const std::array<std::string_view, 2> names{
                "ALBUMFR.TXT",
                "ALBUMDEF.TXT"
            };

            return find_source_file_by_name( files, names );
        }

        if( normalized == "GR" )
        {
            const std::array<std::string_view, 3> names{
                "ALBUMGR.TXT",
                "ALBUMDE.TXT",
                "ALBUMDEF.TXT"
            };

            return find_source_file_by_name( files, names );
        }

        const std::array<std::string_view, 1> names{
            "ALBUMDEF.TXT"
        };

        return find_source_file_by_name( files, names );
    }

    void write_runtime_manifest( const std::filesystem::path &path,
        const SourceProfile &profile,
        std::string_view language,
        std::string_view content_prefix,
        const RuntimeBuildResult &result )
    {
        if( path.has_parent_path() )
        {
            std::filesystem::create_directories( path.parent_path() );
        }

        std::ofstream file( path, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not create runtime manifest: " + path.string() );
        }

        file << "{\n";
        file << "  \"format\": \"piko-runtime\",\n";
        file << "  \"version\": 4,\n";
        file << "  \"source\": {\n";
        file << "    \"platform\": \"ps2\",\n";
        file << "    \"edition\": \"" << json_util::escape_string( profile.edition, json_util::EscapeMode::Text )
             << "\",\n";

        file << "    \"region\": \"" << json_util::escape_string( profile.region, json_util::EscapeMode::Text )
             << "\",\n";

        file << "    \"language\": \"" << json_util::escape_string( language, json_util::EscapeMode::Text ) << "\",\n";
        file << "    \"disc_id\": \"" << json_util::escape_string( profile.disc_id, json_util::EscapeMode::Text )
             << "\",\n";

        file << "    \"content_prefix\": \"" << json_util::escape_string( content_prefix, json_util::EscapeMode::Text )
             << "\"\n";

        file << "  },\n";
        file << "  \"features\": {\n";
        file << "    \"textures\": \"png\",\n";
        file << "    \"audio\": \"wav-flac-opus\",\n";
        file << "    \"charts\": \"json\",\n";
        file << "    \"models\": \"gltf2\",\n";
        file << "    \"stage_sequences\": \"piko-json\",\n";
        file << "    \"movies\": \"mp4-h264-aac\",\n";
        file << "    \"ui\": \"canonical-png\"\n";
        file << "  },\n";
        file << "  \"warnings\": " << result.warnings.size() << "\n";
        file << "}\n";

        if( !file )
        {
            throw std::runtime_error( "Could not write runtime manifest: " + path.string() );
        }
    }

    void write_psp_runtime_manifest( const std::filesystem::path &path,
        const SourceProfile &profile,
        std::string_view language,
        std::string_view content_prefix,
        const RuntimeBuildResult &result,
        std::size_t model_count )
    {
        if( path.has_parent_path() )
        {
            std::filesystem::create_directories( path.parent_path() );
        }

        std::ofstream file( path, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not create PSP runtime manifest: " + path.string() );
        }

        file << "{\n";
        file << "  \"format\": \"piko-runtime-source\",\n";
        file << "  \"version\": 1,\n";
        file << "  \"source\": {\n";
        file << "    \"platform\": \"psp\",\n";
        file << "    \"edition\": \"" << json_util::escape_string( profile.edition, json_util::EscapeMode::Text )
             << "\",\n";

        file << "    \"region\": \"" << json_util::escape_string( profile.region, json_util::EscapeMode::Text )
             << "\",\n";

        file << "    \"language\": \"" << json_util::escape_string( language, json_util::EscapeMode::Text ) << "\",\n";
        file << "    \"disc_id\": \"" << json_util::escape_string( profile.disc_id, json_util::EscapeMode::Text )
             << "\",\n";

        file << "    \"content_prefix\": \"" << json_util::escape_string( content_prefix, json_util::EscapeMode::Text )
             << "\"\n";

        file << "  },\n";
        file << "  \"features\": {\n";
        file << "    \"textures\": \"stage11-stage12-png-gim\",\n";
        file << "    \"audio\": \"psp-chart-audio\",\n";
        file << "    \"charts\": \"psp-json\",\n";
        file << "    \"models\": \"gltf2-stage11-stage12\",\n";
        file << "    \"stage_sequences\": \"piko-json-stage11-stage12\",\n";
        file << "    \"ui\": \"png-gim\"\n";
        file << "  },\n";
        file << "  \"model_count\": " << model_count << ",\n";
        file << "  \"warnings\": " << result.warnings.size() << "\n";
        file << "}\n";

        if( !file )
        {
            throw std::runtime_error( "Could not write PSP runtime manifest: " + path.string() );
        }
    }

    void write_content_index( const std::filesystem::path &game_data )
    {
        std::vector<std::string> locales;

        for( const std::string &locale : { "US", "EN", "FR", "GR", "JP" } )
        {
            if( std::filesystem::is_regular_file( game_data / locale / "manifest.json" ) )
            {
                locales.push_back( locale );
            }
        }

        std::vector<std::string> bonus;

        if( std::filesystem::is_regular_file( game_data / "BONUS" / "WAN" / "manifest.json" ) )
        {
            bonus.push_back( "WAN" );
        }

        std::ofstream file( game_data / "content.json", std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not create GAME_DATA/content.json" );
        }

        file << "{\n";
        file << "  \"format\": \"piko-content-index\",\n";
        file << "  \"version\": 1,\n";
        file << "  \"locales\": [";

        for( std::size_t index = 0; index < locales.size(); ++index )
        {
            if( index != 0u )
            {
                file << ", ";
            }

            file << "\"" << json_util::escape_string( locales[ index ], json_util::EscapeMode::Text ) << "\"";
        }

        file << "],\n";
        file << "  \"bonus\": [";

        for( std::size_t index = 0; index < bonus.size(); ++index )
        {
            if( index != 0u )
            {
                file << ", ";
            }

            file << "\"" << json_util::escape_string( bonus[ index ], json_util::EscapeMode::Text ) << "\"";
        }

        file << "]\n";
        file << "}\n";
    }

}

namespace piko::convert
{
    RuntimeBuildResult RuntimeBuilder::build( const RuntimeBuildOptions &options ) const
    {
        RuntimeBuildResult result;

        try
        {
            if( options.input_path.empty() )
            {
                throw std::runtime_error( "build-runtime requires --input <iso-or-folder>" );
            }

            if( options.output_path.empty() )
            {
                throw std::runtime_error( "build-runtime requires --output <folder>" );
            }

            runtime_builder::report_progress( options,
                0.0f,
                "Opening disc image",
                options.input_path.filename().string() );

            auto source = open_game_source( options.input_path );
            runtime_builder::report_progress( options,
                0.015f,
                "Scanning game data",
                options.input_path.filename().string() );

            const SourceProfile profile = SourceScanner{}.scan( *source );
            std::string platform = piko::utils::lowercase_ascii( options.platform );

            if( platform.empty() || platform == "auto" )
            {
                platform = piko::utils::lowercase_ascii( profile.platform );
            }

            if( platform.empty() || platform == "unknown" )
            {
                platform = "ps2";
            }

            RuntimeBuildOptions resolved = options;
            resolved.platform = platform;

            if( platform == "ps2" && profile.edition == "gitaroo_man_wan" )
            {
                return build_wan_runtime( *source, profile, resolved );
            }

            if( platform == "ps2" )
            {
                return build_ps2_runtime( *source, profile, resolved );
            }

            if( platform == "psp" )
            {
                return build_psp_runtime( *source, profile, resolved );
            }

            throw std::runtime_error( "Unsupported game-data platform: " + platform );
        }
        catch( const std::exception &error )
        {
            result.errors.push_back( error.what() );
            return result;
        }
    }

    RuntimeBuildResult RuntimeBuilder::build_ps2_runtime( const IGameSource &source,
        const SourceProfile &profile,
        const RuntimeBuildOptions &options ) const
    {
        RuntimeBuildResult result;
        const std::string language = runtime_builder::selected_language( profile, options );
        const std::string content_prefix = runtime_builder::find_content_prefix( source, profile, options );
        const std::filesystem::path game_data = runtime_builder::game_data_root( options );
        const std::filesystem::path converted_root = runtime_builder::converted_runtime_root( game_data, language );
        const std::filesystem::path ffmpeg = find_ffmpeg_executable( options.output_path );
        const std::vector<SourceFileInfo> source_files = source.list_all_files();

        std::filesystem::create_directories( game_data );
        runtime_builder::prepare_directory( converted_root, options.overwrite_existing );
        runtime_builder::write_result_atlases( converted_root );
        runtime_builder::write_stage_select_atlas( converted_root );

        std::cout << "PS2 language: " << language << '\n';
        std::cout << "Content prefix: " << ( content_prefix.empty() ? "<source root>" : content_prefix ) << '\n';
        std::cout << "Piko game data: " << converted_root.string() << '\n';

        std::size_t content_file_count = 0u;

        for( const SourceFileInfo &file : source_files )
        {
            if( file.directory )
            {
                continue;
            }

            if( !content_prefix.empty() && !virtual_path_starts_with( file.path, content_prefix ) )
            {
                continue;
            }

            ++content_file_count;
        }

        std::size_t content_file_index = 0u;

        for( const SourceFileInfo &file : source_files )
        {
            if( file.directory )
            {
                continue;
            }

            if( !content_prefix.empty() && !virtual_path_starts_with( file.path, content_prefix ) )
            {
                continue;
            }

            const float content_fraction = content_file_count == 0u
                                               ? 0.74f
                                               : 0.04f + 0.70f * static_cast<float>( content_file_index ) /
                                                             static_cast<float>( content_file_count );

            runtime_builder::report_progress( options, content_fraction, "Importing game data", file.path );
            ++content_file_index;

            const std::string relative = runtime_builder::remove_prefix( file.path, content_prefix );
            const std::vector<std::string> parts = split_virtual_path( relative );

            if( parts.empty() )
            {
                continue;
            }

            if( runtime_builder::is_runtime_source_extension( file.path ) )
            {
                const std::vector<std::uint8_t> bytes = source.get_file( file.path );
                runtime_builder::convert_source_file( file.path,
                    bytes,
                    content_prefix,
                    converted_root,
                    game_data / "PIKO/ui/battle/centre/default",
                    ffmpeg,
                    options.audio_compression,
                    result );
            }
        }

        const std::size_t converted_movie_count = runtime_builder::convert_ps2_movies( source,
            source_files,
            converted_root,
            ffmpeg,
            language,
            options,
            result );

        std::cout << "Converted movies: " << converted_movie_count << '\n';

        const std::string album_source = runtime_builder::album_definition_source( source_files, language );

        if( !album_source.empty() )
        {
            const std::vector<std::uint8_t> album_bytes = source.get_file( album_source );
            const std::string album_text( reinterpret_cast<const char *>( album_bytes.data() ), album_bytes.size() );
            filetypes::albumdef::AlbumDef album = filetypes::albumdef::parse_text( album_text );
            runtime_builder::normalize_album_movie_arguments( album );
            filetypes::albumdef::save_json( album, converted_root / "album.json" );
        }
        else
        {
            result.warnings.push_back( "No album definition was present for language " + language );
        }

        runtime_builder::write_runtime_manifest( converted_root / "manifest.json",
            profile,
            language,
            content_prefix,
            result );

        runtime_builder::export_note_textures( game_data );

        runtime_builder::write_content_index( game_data );

        const auto converted_stats = runtime_builder::directory_stats( converted_root );
        result.files_written = converted_stats.first;

        result.bytes_written = converted_stats.second;
        runtime_builder::report_progress( options, 0.985f, "Finalizing runtime", "manifest.json" );
        result.success = true;

        runtime_builder::report_progress( options, 1.0f, "PlayStation 2 import complete" );
        return result;
    }

    RuntimeBuildResult RuntimeBuilder::build_psp_runtime( const IGameSource &source,
        const SourceProfile &profile,
        const RuntimeBuildOptions &options ) const
    {
        RuntimeBuildResult result;
        const std::filesystem::path game_data = runtime_builder::game_data_root( options );
        const std::string language = runtime_builder::selected_psp_language( game_data, options );
        const std::string content_prefix = runtime_builder::psp_content_prefix( source );
        const std::string stage_root = normalize_virtual_path( content_prefix + "/STAGE" );
        const std::string model_root = normalize_virtual_path( content_prefix + "/MDL" );
        const std::filesystem::path converted_root = runtime_builder::converted_runtime_root( game_data, language );
        const std::filesystem::path ffmpeg = find_ffmpeg_executable( options.output_path );

        if( !std::filesystem::is_regular_file( converted_root / "manifest.json" ) )
        {
            throw std::runtime_error( "PSP import requires the PS2 runtime to be converted first" );
        }

        if( !source.exists( stage_root ) || !source.is_directory( stage_root ) )
        {
            throw std::runtime_error( "PSP source does not contain a DATA/STAGE directory" );
        }

        std::cout << "PSP chart/audio language: " << language << '\n';
        std::cout << "PSP content prefix: " << content_prefix << '\n';
        std::cout << "Canonical runtime: " << converted_root.string() << '\n';
        runtime_builder::write_result_atlases( converted_root );
        std::cout << "ATRAC decoder: " << ffmpeg.string() << '\n';

        const std::vector<SourceFileInfo> source_files = source.list_all_files();

        if( options.overwrite_existing )
        {
            std::filesystem::remove_all( converted_root / "STAGE11" );
            std::filesystem::remove_all( converted_root / "STAGE12" );
        }

        std::size_t converted_chart_count = 0u;
        std::size_t converted_sequence_count = 0u;
        std::size_t converted_audio_cue_count = 0u;
        std::size_t converted_model_count = 0u;
        runtime_builder::report_progress( options, 0.06f, "Converting PSP interface textures" );
        std::size_t converted_ui_texture_count = runtime_builder::convert_psp_ui_and_stage_textures( source,
            source_files,
            content_prefix,
            language,
            converted_root,
            game_data / "PIKO/ui/battle/centre/default",
            options,
            result );

        try
        {
            runtime_builder::report_progress( options, 0.32f, "Converting PSP collection voices" );
            const std::vector<PspAudioSource> collection_audio =
                runtime_builder::collect_psp_collection_audio( source, content_prefix, language );

            if( !collection_audio.empty() )
            {
                const std::filesystem::path collection_audio_output = converted_root / "UI" / "COLLECTION" / "AUDIO";

                if( options.overwrite_existing )
                {
                    std::filesystem::remove_all( collection_audio_output );
                }

                convert_psp_atrac_audio( collection_audio, collection_audio_output, options.audio_compression, ffmpeg );
            }
        }
        catch( const std::exception &error )
        {
            result.warnings.push_back( std::string( "PSP collection voices: " ) + error.what() );
        }

        if( source.exists( model_root ) && source.is_directory( model_root ) )
        {
            runtime_builder::report_progress( options, 0.34f, "Converting PSP stage models" );
            converted_model_count = runtime_builder::convert_psp_models( source,
                source_files,
                model_root,
                stage_root,
                converted_root,
                options,
                result );
        }
        else
        {
            result.warnings.push_back( "PSP source does not contain a DATA/MDL directory" );
        }

        std::vector<std::string> stages = source.list_directory( stage_root );
        std::sort( stages.begin(), stages.end() );
        std::size_t stage_index = 0u;
        const std::size_t stage_count = std::max<std::size_t>( stages.size(), 1u );

        for( const std::string &stage : stages )
        {
            const float stage_fraction =
                0.52f + 0.40f * static_cast<float>( stage_index ) / static_cast<float>( stage_count );

            runtime_builder::report_progress( options, stage_fraction, "Converting PSP stage data", stage );
            ++stage_index;

            if( !runtime_builder::looks_like_psp_stage( stage ) )
            {
                continue;
            }

            const std::string source_stage = normalize_virtual_path( stage_root + "/" + stage );

            if( !source.is_directory( source_stage ) )
            {
                continue;
            }

            const std::string group = runtime_builder::psp_stage_group( stage );
            const bool psp_only_visuals = runtime_builder::is_psp_only_visual_stage_group( group );
            const std::filesystem::path stage_output = converted_root / group;

            if( options.overwrite_existing )
            {
                std::filesystem::remove_all( stage_output / "CHARTS" / "PSP" );
                std::filesystem::remove_all( stage_output / "AUDIO" / "PSP" );
            }

            std::filesystem::create_directories( stage_output / "CHARTS" / "PSP" );
            std::filesystem::create_directories( stage_output / "AUDIO" / "PSP" );

            if( psp_only_visuals )
            {
                std::filesystem::create_directories( stage_output / "SEQUENCES" );
            }

            std::vector<std::string> entries = source.list_directory( source_stage );
            std::sort( entries.begin(), entries.end() );

            for( const std::string &entry : entries )
            {
                const std::string path = normalize_virtual_path( source_stage + "/" + entry );

                if( source.is_directory( path ) )
                {
                    continue;
                }

                const std::string ext = runtime_builder::extension( entry );
                runtime_builder::report_progress( options, stage_fraction, "Converting PSP stage data", path );

                try
                {
                    if( ext == ".CHC" )
                    {
                        const std::string output_stem =
                            runtime_builder::psp_chart_stem( runtime_builder::stem( entry ) );

                        convert_chc_to_json( source.get_file( path ),
                            stage_output / "CHARTS" / "PSP" / ( output_stem + ".json" ) );

                        ++converted_chart_count;
                    }
                    else if( ext == ".SSQ" && psp_only_visuals )
                    {
                        const std::vector<std::uint8_t> sequence = source.get_file( path );
                        filetypes::ssq::save_json( filetypes::ssq::decode( sequence ),
                            stage_output / "SEQUENCES" / ( runtime_builder::stem( entry ) + ".json" ) );

                        ++converted_sequence_count;
                    }
                }
                catch( const std::exception &error )
                {
                    result.warnings.push_back( path + ": " + error.what() );
                }
            }

            try
            {
                runtime_builder::report_progress( options, stage_fraction, "Converting PSP stage audio", source_stage );
                const std::vector<PspAudioSource> audio =
                    runtime_builder::collect_psp_audio( source, source_stage, language );

                if( audio.empty() )
                {
                    result.warnings.push_back( source_stage + ": no PSP stage audio was found" );
                }
                else
                {
                    const std::vector<ConvertedAudioCue> cues = convert_psp_atrac_audio( audio,
                        stage_output / "AUDIO" / "PSP" / stage,
                        options.audio_compression,
                        ffmpeg );

                    converted_audio_cue_count += cues.size();
                }
            }
            catch( const std::exception &error )
            {
                result.warnings.push_back( source_stage + ": " + error.what() );
            }
        }

        if( converted_chart_count == 0u )
        {
            throw std::runtime_error( "PSP import did not convert any CHC charts" );
        }

        if( converted_audio_cue_count == 0u )
        {
            throw std::runtime_error( "PSP import did not convert any stage audio cues" );
        }

        if( converted_ui_texture_count == 0u )
        {
            throw std::runtime_error( "PSP import did not convert any UI GIM textures" );
        }

        if( converted_sequence_count == 0u )
        {
            result.warnings.push_back( "PSP import did not find stage 11/12 sequences" );
        }

        if( converted_model_count == 0u )
        {
            result.warnings.push_back( "PSP import did not find stage 11/12 GMO models" );
        }

        runtime_builder::report_progress( options, 0.96f, "Finalizing PSP chart/audio data", "SOURCES/PSP.json" );
        runtime_builder::write_psp_runtime_manifest( converted_root / "SOURCES" / "PSP.json",
            profile,
            language,
            content_prefix,
            result,
            converted_model_count );

        runtime_builder::export_note_textures( game_data );

        runtime_builder::write_content_index( game_data );

        const auto stats = runtime_builder::directory_stats( converted_root );
        result.files_written = stats.first;

        result.bytes_written = stats.second;

        result.success = true;

        runtime_builder::report_progress( options, 1.0f, "PSP import complete" );
        return result;
    }

    RuntimeBuildResult RuntimeBuilder::build_wan_runtime( const IGameSource &source,
        const SourceProfile &profile,
        const RuntimeBuildOptions &options ) const
    {
        RuntimeBuildResult result;
        const std::filesystem::path game_data = runtime_builder::game_data_root( options );
        bool has_ps2_base = false;

        for( const std::string &locale : { "US", "EN", "FR", "GR", "JP" } )
        {
            if( std::filesystem::is_regular_file( game_data / locale / "manifest.json" ) )
            {
                has_ps2_base = true;
                break;
            }
        }

        if( !has_ps2_base )
        {
            throw std::runtime_error( "Gitaroo Man Wan import requires a retail PS2 runtime first" );
        }

        const std::string content_prefix = "PROJECTS/STDATA";
        const std::string stage_prefix = "PROJECTS/STDATA/STAGE02";

        if( !source.exists( stage_prefix ) || !source.is_directory( stage_prefix ) )
        {
            throw std::runtime_error( "Gitaroo Man Wan does not contain PROJECTS/STDATA/STAGE02" );
        }

        const std::filesystem::path converted_root = game_data / "BONUS" / "WAN";
        const std::filesystem::path ffmpeg = find_ffmpeg_executable( options.output_path );
        runtime_builder::prepare_directory( converted_root, options.overwrite_existing );

        const std::vector<SourceFileInfo> files = source.list_all_files();
        std::size_t file_count = 0u;

        for( const SourceFileInfo &file : files )
        {
            if( !file.directory && virtual_path_starts_with( file.path, stage_prefix ) )
            {
                ++file_count;
            }
        }

        std::size_t file_index = 0u;

        for( const SourceFileInfo &file : files )
        {
            if( file.directory || !virtual_path_starts_with( file.path, stage_prefix ) )
            {
                continue;
            }

            const float fraction =
                file_count == 0u ? 0.9f
                                 : 0.08f + 0.80f * static_cast<float>( file_index ) / static_cast<float>( file_count );

            ++file_index;
            runtime_builder::report_progress( options, fraction, "Importing Gitaroo Man Wan Stage 2", file.path );

            const std::string relative = runtime_builder::remove_prefix( file.path, content_prefix );
            const std::vector<std::string> parts = split_virtual_path( relative );

            if( parts.empty() )
            {
                continue;
            }

            if( runtime_builder::is_runtime_source_extension( file.path ) )
            {
                const std::vector<std::uint8_t> bytes = source.get_file( file.path );
                runtime_builder::convert_source_file( file.path,
                    bytes,
                    content_prefix,
                    converted_root,
                    game_data / "PIKO/ui/battle/centre/default",
                    ffmpeg,
                    options.audio_compression,
                    result );
            }
        }

        const std::filesystem::path chart_root = converted_root / "STAGE02" / "CHARTS" / "PS2";
        const std::filesystem::path audio_root = converted_root / "STAGE02" / "AUDIO" / "PS2" / "ST02";

        if( !std::filesystem::is_directory( chart_root ) )
        {
            throw std::runtime_error( "Gitaroo Man Wan import did not produce a Stage 2 chart" );
        }

        if( !std::filesystem::is_regular_file( audio_root / "audio.json" ) )
        {
            throw std::runtime_error( "Gitaroo Man Wan import did not produce Stage 2 audio" );
        }

        std::filesystem::create_directories( converted_root );
        std::ofstream manifest( converted_root / "manifest.json", std::ios::binary );

        if( !manifest )
        {
            throw std::runtime_error( "Could not create Gitaroo Man Wan runtime manifest" );
        }

        manifest << "{\n";
        manifest << "  \"format\": \"piko-bonus-runtime\",\n";
        manifest << "  \"version\": 1,\n";
        manifest << "  \"edition\": \"gitaroo_man_wan\",\n";
        manifest << "  \"disc_id\": \"" << json_util::escape_string( profile.disc_id, json_util::EscapeMode::Text )
                 << "\",\n";

        manifest << "  \"stage\": 2\n";
        manifest << "}\n";
        manifest.close();
        runtime_builder::export_note_textures( game_data );

        runtime_builder::write_content_index( game_data );

        const auto runtime_stats = runtime_builder::directory_stats( converted_root );
        result.files_written = runtime_stats.first;

        result.bytes_written = runtime_stats.second;

        result.success = true;
        runtime_builder::report_progress( options, 1.0f, "Gitaroo Man Wan import complete" );
        return result;
    }

}
