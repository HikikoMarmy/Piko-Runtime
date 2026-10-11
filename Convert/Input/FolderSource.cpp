#include "Input/FolderSource.hpp"

#include "Utility/PathNormalize.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace piko::convert
{
    const std::filesystem::path &FolderSource::root_path() const noexcept
    {
        return root_path_;
    }

    FolderSource::FolderSource( std::filesystem::path root_path )
        : root_path_( std::filesystem::absolute( std::move( root_path ) ) )
    {
        if( !std::filesystem::exists( root_path_ ) || !std::filesystem::is_directory( root_path_ ) )
        {
            throw std::runtime_error(
                "Folder source root does not exist or is not a directory: " + root_path_.string() );
        }

        directories_.emplace( "", root_path_ );
        build_index();
    }

    bool FolderSource::exists( std::string_view virtual_path ) const
    {
        const std::string normalized = normalize_virtual_path( virtual_path );
        return files_.contains( normalized ) || directories_.contains( normalized );
    }

    bool FolderSource::is_directory( std::string_view virtual_path ) const
    {
        return directories_.contains( normalize_virtual_path( virtual_path ) );
    }

    std::vector<std::uint8_t> FolderSource::get_file( std::string_view virtual_path ) const
    {
        const std::string normalized = normalize_virtual_path( virtual_path );

        const auto it = files_.find( normalized );

        if( it == files_.end() )
        {
            throw std::runtime_error( "Folder source file not found: " + std::string( virtual_path ) );
        }

        std::ifstream file( it->second, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not open file: " + it->second.string() );
        }

        file.seekg( 0, std::ios::end );
        const std::streamoff size = file.tellg();
        file.seekg( 0, std::ios::beg );

        if( size < 0 )
        {
            throw std::runtime_error( "Could not determine file size: " + it->second.string() );
        }

        std::vector<std::uint8_t> data( static_cast<std::size_t>( size ) );

        if( !data.empty() )
        {
            file.read( reinterpret_cast<char *>( data.data() ), static_cast<std::streamsize>( data.size() ) );
        }

        if( !file && !data.empty() )
        {
            throw std::runtime_error( "Could not read whole file: " + it->second.string() );
        }

        return data;
    }

    std::vector<std::string> FolderSource::list_directory( std::string_view virtual_path ) const
    {
        const std::string normalized = normalize_virtual_path( virtual_path );

        const auto it = directory_entries_.find( normalized );

        if( it == directory_entries_.end() )
        {
            return {};
        }

        std::vector<std::string> result = it->second;
        std::sort( result.begin(), result.end() );
        return result;
    }

    std::vector<SourceFileInfo> FolderSource::list_all_files() const
    {
        std::vector<SourceFileInfo> result;
        result.reserve( files_.size() );

        for( const auto &[ path, real_path ] : files_ )
        {
            std::uint64_t size = 0;
            std::error_code ec;
            size = static_cast<std::uint64_t>( std::filesystem::file_size( real_path, ec ) );

            if( ec )
            {
                size = 0;
            }

            result.push_back( SourceFileInfo{ .path = path, .size = size, .directory = false } );
        }

        std::sort( result.begin(),
            result.end(),
            []( const SourceFileInfo &a, const SourceFileInfo &b )
            {
                return a.path < b.path;
            } );
        return result;
    }

    void FolderSource::build_index()
    {
        for( const auto &entry : std::filesystem::recursive_directory_iterator( root_path_ ) )
        {
            const auto relative = std::filesystem::relative( entry.path(), root_path_ ).generic_string();
            const std::string normalized = normalize_virtual_path( relative );

            if( normalized.empty() )
            {
                continue;
            }

            if( entry.is_directory() )
            {
                directories_.emplace( normalized, entry.path() );
                add_directory_entry( normalized );
            }
            else if( entry.is_regular_file() )
            {
                files_.emplace( normalized, entry.path() );
                add_directory_entry( normalized );
            }
        }
    }

    void FolderSource::add_directory_entry( const std::string &child_path )
    {
        const std::string parent = virtual_parent_path( child_path );
        const std::string name = virtual_filename( child_path );
        auto &entries = directory_entries_[ parent ];

        if( std::find( entries.begin(), entries.end(), name ) == entries.end() )
        {
            entries.push_back( name );
        }
    }
}
