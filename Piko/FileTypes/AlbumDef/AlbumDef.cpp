#include "AlbumDef.hpp"

#include "json.hpp"
#include "Utils/StrUtil.hpp"

#include <cstdint>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace piko::filetypes::albumdef::parser
{
    std::string trim( std::string value )
    {
        while( !value.empty() && std::isspace( static_cast<unsigned char>( value.front() ) ) )
        {
            value.erase( value.begin() );
        }

        while( !value.empty() && std::isspace( static_cast<unsigned char>( value.back() ) ) )
        {
            value.pop_back();
        }

        return value;
    }

    std::vector<std::string> tokenize( const std::string &line )
    {
        std::istringstream stream( line );
        std::vector<std::string> tokens;
        std::string token;

        while( stream >> token )
        {
            tokens.push_back( token );
        }

        return tokens;
    }

    std::int32_t parse_int( const std::string &text, const char *context )
    {
        try
        {
            std::size_t parsed = 0;
            const std::int32_t value = std::stoi( text, &parsed, 10 );

            if( parsed != text.size() )
            {
                throw std::runtime_error( "trailing characters" );
            }

            return value;
        }
        catch( ... )
        {
            throw std::runtime_error( std::string( "Invalid ALBUMDEF integer for " ) + context + ": " + text );
        }
    }

    AlbumPage &page_for( AlbumDef &album, std::int32_t id )
    {
        AlbumPage &page = album.pages[ id ];
        page.id = id;
        return page;
    }

    std::optional<std::string> argument_value( const std::vector<std::string> &args, std::string_view option )
    {
        for( std::size_t index = 0; index + 1u < args.size(); ++index )
        {
            if( args[ index ] == option )
            {
                return args[ index + 1u ];
            }
        }

        return std::nullopt;
    }

    bool has_argument( const std::vector<std::string> &args, std::string_view option )
    {
        return std::find( args.begin(), args.end(), option ) != args.end();
    }

    nlohmann::json argument_list( const std::vector<std::string> &args, const std::set<std::string> &excluded = {} )
    {
        nlohmann::json output = nlohmann::json::array();

        for( std::size_t index = 0; index < args.size(); ++index )
        {
            const std::string &option = args[ index ];

            if( excluded.contains( option ) )
            {
                if( index + 1u < args.size() && !args[ index + 1u ].starts_with( '-' ) )
                {
                    ++index;
                }
                continue;
            }

            nlohmann::json item{ { "option", option } };

            if( index + 1u < args.size() && !args[ index + 1u ].starts_with( '-' ) )
            {
                item[ "value" ] = args[ ++index ];
            }

            output.push_back( std::move( item ) );
        }

        return output;
    }

    std::vector<std::string> arguments_from_json( const nlohmann::json &items )
    {
        std::vector<std::string> output;

        for( const nlohmann::json &item : items )
        {
            const std::string option = item.value( "option", std::string{} );

            if( option.empty() )
            {
                continue;
            }

            output.push_back( option );

            if( const auto value = item.find( "value" ); value != item.end() )
            {
                if( value->is_string() )
                {
                    output.push_back( value->get<std::string>() );
                }
                else if( value->is_number_integer() )
                {
                    output.push_back( std::to_string( value->get<long long>() ) );
                }
                else if( value->is_number_float() )
                {
                    std::ostringstream text;
                    text << value->get<double>();
                    output.push_back( text.str() );
                }
            }
        }

        return output;
    }

    void append_value_argument( std::vector<std::string> &args,
        const nlohmann::json &object,
        std::string_view key,
        std::string_view option )
    {
        const auto value = object.find( key );

        if( value == object.end() || value->is_null() )
        {
            return;
        }

        args.emplace_back( option );

        if( value->is_string() )
        {
            args.push_back( value->get<std::string>() );
        }
        else if( value->is_number_integer() )
        {
            args.push_back( std::to_string( value->get<long long>() ) );
        }
        else if( value->is_number_float() )
        {
            std::ostringstream text;
            text << value->get<double>();
            args.push_back( text.str() );
        }
    }

    void append_flag_argument( std::vector<std::string> &args,
        const nlohmann::json &object,
        std::string_view key,
        std::string_view option )
    {
        if( object.value( std::string( key ), false ) )
        {
            args.emplace_back( option );
        }
    }

    nlohmann::json stage_json( const AlbumPage &page )
    {
        const std::set<std::string> known{
            "-n",
            "-y",
            "-c",
            "-s",
            "-m",
            "-q",
            "-t",
            "-l",
            "-p",
            "-g",
            "-k",
            "-w"
        };

        nlohmann::json stage{
            { "play_immediately", has_argument( page.args, "-p" ) },
            { "show_chart", has_argument( page.args, "-g" ) },
            { "billboard_sky", has_argument( page.args, "-k" ) },
            { "stereo_phrases", has_argument( page.args, "-w" ) },
            { "extra_arguments", argument_list( page.args, known ) }
        };

        const auto add = [ & ]( std::string_view key, std::string_view option )
        {
            if( const std::optional<std::string> value = argument_value( page.args, option ); value.has_value() )
            {
                stage[ std::string( key ) ] = *value;
            }
        };

        add( "number", "-n" );
        add( "players", "-y" );
        add( "chart", "-c" );
        add( "sequence", "-s" );
        add( "model", "-m" );
        add( "loading_scene", "-q" );
        add( "tempo", "-t" );
        add( "animation_marker_frames", "-l" );
        return stage;
    }

    std::vector<std::string> stage_args( const nlohmann::json &stage )
    {
        std::vector<std::string> args;
        append_value_argument( args, stage, "number", "-n" );
        append_value_argument( args, stage, "players", "-y" );
        append_value_argument( args, stage, "sequence", "-s" );
        append_value_argument( args, stage, "model", "-m" );
        append_value_argument( args, stage, "chart", "-c" );
        append_value_argument( args, stage, "loading_scene", "-q" );
        append_value_argument( args, stage, "tempo", "-t" );
        append_value_argument( args, stage, "animation_marker_frames", "-l" );
        append_flag_argument( args, stage, "play_immediately", "-p" );
        append_flag_argument( args, stage, "show_chart", "-g" );
        append_flag_argument( args, stage, "billboard_sky", "-k" );
        append_flag_argument( args, stage, "stereo_phrases", "-w" );

        if( const auto extra = stage.find( "extra_arguments" ); extra != stage.end() )
        {
            std::vector<std::string> values = arguments_from_json( *extra );
            args.insert( args.end(), values.begin(), values.end() );
        }

        return args;
    }
}

namespace piko::filetypes::albumdef
{
    bool is_playable_module( std::string_view module )
    {
        return module == "MODULE_TYPE_SINGLE_PLYR_GAME" || module == "MODULE_TYPE_TUTORIAL" ||
               module == "MODULE_TYPE_MULTI_PLYR_GAME";
    }

    AlbumDef parse_text( std::string_view text )
    {
        AlbumDef album{};
        std::istringstream file{ std::string( text ) };
        std::string line;
        std::size_t line_number = 0;

        while( std::getline( file, line ) )
        {
            ++line_number;

            const std::size_t comment = line.find( ';' );

            if( comment != std::string::npos )
            {
                line.erase( comment );
            }

            line = parser::trim( line );

            if( line.empty() )
            {
                continue;
            }

            const std::vector<std::string> tokens = parser::tokenize( line );

            if( tokens.empty() )
            {
                continue;
            }

            if( tokens[ 0 ] == "PAGE_MOD" )
            {
                if( tokens.size() < 3 )
                {
                    throw std::runtime_error( "Invalid PAGE_MOD at line " + std::to_string( line_number ) );
                }

                AlbumPage &page = parser::page_for( album, parser::parse_int( tokens[ 1 ], "PAGE_MOD id" ) );
                page.module_type = tokens[ 2 ];
            }
            else if( tokens[ 0 ] == "PAGE_ARG" )
            {
                if( tokens.size() < 3 )
                {
                    throw std::runtime_error( "Invalid PAGE_ARG at line " + std::to_string( line_number ) );
                }

                AlbumPage &page = parser::page_for( album, parser::parse_int( tokens[ 1 ], "PAGE_ARG id" ) );
                page.args.clear();

                for( std::size_t i = 3; i < tokens.size(); ++i )
                {
                    page.args.push_back( tokens[ i ] );
                }
            }
            else if( tokens[ 0 ] == "PAGE_RET" )
            {
                if( tokens.size() < 4 )
                {
                    throw std::runtime_error( "Invalid PAGE_RET at line " + std::to_string( line_number ) );
                }

                AlbumPage &page = parser::page_for( album, parser::parse_int( tokens[ 1 ], "PAGE_RET id" ) );
                page.returns[ parser::parse_int( tokens[ 2 ], "PAGE_RET return" ) ] =
                    parser::parse_int( tokens[ 3 ], "PAGE_RET next" );
            }
        }

        return album;
    }

    AlbumDef load_json( const std::filesystem::path &path )
    {
        std::ifstream file( path, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not open album JSON: " + path.string() );
        }

        nlohmann::json root;
        file >> root;

        if( root.value( "format", std::string{} ) != "piko-album" )
        {
            throw std::runtime_error( "Unsupported album JSON format: " + path.string() );
        }

        AlbumDef album{};

        for( const nlohmann::json &item : root.at( "pages" ) )
        {
            AlbumPage page{};
            page.id = item.at( "id" ).get<std::int32_t>();

            page.module_type = item.value( "module", std::string{} );

            if( const auto stage = item.find( "stage" ); stage != item.end() )
            {
                page.args = parser::stage_args( *stage );
            }
            else if( const auto arguments = item.find( "arguments" ); arguments != item.end() )
            {
                page.args = parser::arguments_from_json( *arguments );
            }
            else
            {
                page.args = item.value( "args", std::vector<std::string>{} );
            }

            if( const auto returns = item.find( "returns" ); returns != item.end() )
            {
                for( auto entry = returns->begin(); entry != returns->end(); ++entry )
                {
                    page.returns[ parser::parse_int( entry.key(), "JSON return" ) ] = entry.value().get<std::int32_t>();
                }
            }

            album.pages[ page.id ] = std::move( page );
        }

        return album;
    }

    AlbumDef load_file( const std::filesystem::path &path )
    {
        if( piko::utils::uppercase_ascii( path.extension().string() ) == ".JSON" )
        {
            return load_json( path );
        }

        std::ifstream file( path, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not open ALBUMDEF.TXT: " + path.string() );
        }

        const std::string text{ std::istreambuf_iterator<char>( file ), std::istreambuf_iterator<char>() };
        return parse_text( text );
    }

    void save_json( const AlbumDef &album, const std::filesystem::path &path )
    {
        if( path.has_parent_path() )
        {
            std::filesystem::create_directories( path.parent_path() );
        }

        std::vector<std::int32_t> ids;
        ids.reserve( album.pages.size() );

        for( const auto &[ id, page ] : album.pages )
        {
            ids.push_back( id );
        }

        std::sort( ids.begin(), ids.end() );

        nlohmann::json pages = nlohmann::json::array();

        for( const std::int32_t id : ids )
        {
            const AlbumPage &page = album.pages.at( id );
            nlohmann::json returns = nlohmann::json::object();
            std::vector<std::int32_t> return_ids;
            return_ids.reserve( page.returns.size() );

            for( const auto &[ result, next ] : page.returns )
            {
                return_ids.push_back( result );
            }

            std::sort( return_ids.begin(), return_ids.end() );

            for( const std::int32_t result : return_ids )
            {
                returns[ std::to_string( result ) ] = page.returns.at( result );
            }

            nlohmann::json item{
                { "id", page.id },
                { "module", page.module_type },
                { "returns", std::move( returns ) }
            };

            if( is_playable_module( page.module_type ) )
            {
                item[ "stage" ] = parser::stage_json( page );
            }
            else
            {
                item[ "arguments" ] = parser::argument_list( page.args );
            }

            pages.push_back( std::move( item ) );
        }

        const nlohmann::json root{ { "format", "piko-album" }, { "version", 2 }, { "pages", std::move( pages ) } };

        std::ofstream file( path, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not create album JSON: " + path.string() );
        }

        file << root.dump( 2 ) << '\n';

        if( !file )
        {
            throw std::runtime_error( "Could not write album JSON: " + path.string() );
        }
    }
}
