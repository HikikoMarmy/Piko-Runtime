#include "Region/SourceScanner.hpp"

#include "Utility/PathNormalize.hpp"
#include "Utils/StrUtil.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace piko::convert
{
    std::string SourceScanner::bytes_to_text( const std::vector<std::uint8_t> &data )
    {
        return std::string( reinterpret_cast<const char *>( data.data() ),
            reinterpret_cast<const char *>( data.data() ) + data.size() );
    }

    bool SourceScanner::looks_like_stage_id( std::string_view value )
    {
        const std::string text( value );

        if( text.size() != 7 )
        {
            return false;
        }

        if( text.rfind( "STAGE", 0 ) != 0 )
        {
            return false;
        }

        return std::isdigit( static_cast<unsigned char>( text[ 5 ] ) ) &&
               std::isdigit( static_cast<unsigned char>( text[ 6 ] ) );
    }

    bool SourceScanner::contains_string( const std::vector<std::string> &list, const std::string &value )
    {
        return std::find( list.begin(), list.end(), value ) != list.end();
    }

    SourceProfile SourceScanner::scan( const IGameSource &source ) const
    {
        SourceProfile profile;

        if( source.exists( "PSP_GAME/PARAM.SFO" ) || source.exists( "UMD_DATA.BIN" ) )
        {
            profile.platform = "psp";

            profile.edition = "gitaroo_man_lives";
        }
        else if( source.exists( "SYSTEM.CNF" ) )
        {
            profile.platform = "ps2";
        }

        if( source.exists( "SYSTEM.CNF" ) )
        {
            const auto system_cnf = source.get_file( "SYSTEM.CNF" );
            profile.executable_path = extract_boot_executable( system_cnf );

            profile.disc_id = extract_disc_id( profile.executable_path );

            profile.region = detect_region_from_executable( profile.executable_path );

            if( piko::utils::uppercase_ascii( profile.disc_id ) == "SLPM_620.62" )
            {
                profile.edition = "gitaroo_man_wan";
            }
        }
        else if( source.exists( "UMD_DATA.BIN" ) )
        {
            const auto umd_data = bytes_to_text( source.get_file( "UMD_DATA.BIN" ) );
            profile.disc_id = piko::utils::uppercase_ascii( umd_data.substr( 0, umd_data.find_first_of( "\r\n\0" ) ) );

            profile.region = detect_region_from_executable( profile.disc_id );
        }

        const std::string stdata_root = "PROJECTS/STDATA";

        if( source.exists( stdata_root ) && source.is_directory( stdata_root ) )
        {
            if( !contains_string( profile.content_roots, stdata_root ) )
            {
                profile.content_roots.push_back( stdata_root );
            }

            for( const std::string &entry : source.list_directory( stdata_root ) )
            {
                const std::string child = normalize_virtual_path( stdata_root + "/" + entry );

                if( !source.is_directory( child ) )
                {
                    continue;
                }

                if( entry == "US" || entry == "EN" || entry == "FR" || entry == "GR" )
                {
                    if( !contains_string( profile.language_folders, entry ) )
                    {
                        profile.language_folders.push_back( entry );
                    }
                }
            }

            // Japanese PS2 data is directly in PROJECTS/STDATA with no JP folder.
            if( source.exists( normalize_virtual_path( stdata_root + "/STAGE01" ) ) &&
                source.is_directory( normalize_virtual_path( stdata_root + "/STAGE01" ) ) &&
                !contains_string( profile.language_folders, "JP" ) )
            {
                profile.language_folders.push_back( "JP" );
            }
        }

        for( const char *candidate : { "USRDIR/DATA", "PSP_GAME/USRDIR/DATA" } )
        {
            if( source.exists( candidate ) && source.is_directory( candidate ) )
            {
                if( !contains_string( profile.content_roots, candidate ) )
                {
                    profile.content_roots.push_back( candidate );
                }
            }
        }

        std::set<std::string> stages;

        for( const SourceFileInfo &file : source.list_all_files() )
        {
            for( const std::string &part : split_virtual_path( file.path ) )
            {
                if( looks_like_stage_id( part ) )
                {
                    stages.insert( part );
                }
            }
        }

        profile.stage_ids.assign( stages.begin(), stages.end() );

        if( profile.region == "unknown" )
        {
            if( contains_string( profile.language_folders, "US" ) )
            {
                profile.region = "us";
            }
            else if( contains_string( profile.language_folders, "EN" ) ||
                     contains_string( profile.language_folders, "FR" ) ||
                     contains_string( profile.language_folders, "GR" ) )
            {
                profile.region = "eu";
            }
            else if( source.exists( "PROJECTS/STDATA" ) && source.is_directory( "PROJECTS/STDATA" ) )
            {
                profile.region = "jp";
            }
        }

        return profile;
    }

    std::string SourceScanner::detect_region_from_executable( std::string_view executable_path )
    {
        const std::string text = piko::utils::uppercase_ascii( std::string( executable_path ) );

        // Check disc IDs before region prefixes.
        if( text.find( "SLUS_202.94" ) != std::string::npos )
        {
            return "us";
        }

        if( text.find( "SLPM_650.12" ) != std::string::npos )
        {
            return "jp";
        }

        if( text.find( "SLES_506.53" ) != std::string::npos )
        {
            return "eu";
        }

        if( text.find( "SLUS" ) != std::string::npos || text.find( "SCUS" ) != std::string::npos ||
            text.find( "ULUS" ) != std::string::npos )
        {
            return "us";
        }

        if( text.find( "SLPM" ) != std::string::npos || text.find( "SLPS" ) != std::string::npos ||
            text.find( "SCPS" ) != std::string::npos || text.find( "ULJM" ) != std::string::npos )
        {
            return "jp";
        }

        if( text.find( "SLES" ) != std::string::npos || text.find( "SCES" ) != std::string::npos ||
            text.find( "ULES" ) != std::string::npos )
        {
            return "eu";
        }

        return "unknown";
    }

    std::string SourceScanner::extract_boot_executable( const std::vector<std::uint8_t> &system_cnf )
    {
        const std::string text = piko::utils::uppercase_ascii( bytes_to_text( system_cnf ) );

        for( const std::string &line : piko::utils::split_lines( text ) )
        {
            const std::size_t boot = line.find( "BOOT2" );

            if( boot == std::string::npos )
            {
                continue;
            }

            const std::size_t equals = line.find( '=' );

            if( equals == std::string::npos )
            {
                continue;
            }

            std::string value = line.substr( equals + 1 );
            value.erase( std::remove_if( value.begin(),
                             value.end(),
                             []( unsigned char c )
                             {
                                 return std::isspace( c );
                             } ),
                value.end() );

            const std::size_t colon = value.find( ':' );

            if( colon != std::string::npos )
            {
                value.erase( 0, colon + 1 );
            }

            return normalize_virtual_path( value );
        }

        return {};
    }

    std::string SourceScanner::extract_disc_id( std::string_view executable_path )
    {
        std::string text = piko::utils::uppercase_ascii( std::string( executable_path ) );
        const std::size_t slash = text.find_last_of( '/' );

        if( slash != std::string::npos )
        {
            text.erase( 0, slash + 1 );
        }

        const std::size_t semicolon = text.find( ';' );

        if( semicolon != std::string::npos )
        {
            text.erase( semicolon );
        }

        return text;
    }
}
