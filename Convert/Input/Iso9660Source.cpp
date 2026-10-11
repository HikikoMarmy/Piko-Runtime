#include "Input/Iso9660Source.hpp"

#include "Utility/PathNormalize.hpp"
#include "Utils/BinaryUtil.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace piko::convert
{
    const std::filesystem::path &Iso9660Source::iso_path() const noexcept
    {
        return iso_path_;
    }

    std::size_t Iso9660Source::file_count() const noexcept
    {
        return files_.size();
    }

    std::string Iso9660Source::decode_iso_identifier( const std::uint8_t *data, std::size_t length )
    {
        std::string name;
        name.reserve( length );

        for( std::size_t i = 0; i < length; ++i )
        {
            name.push_back( static_cast<char>( data[ i ] ) );
        }

        return normalize_virtual_path( name );
    }

    Iso9660Source::Iso9660Source( std::filesystem::path iso_path )
        : iso_path_( std::filesystem::absolute( std::move( iso_path ) ) )
    {
        iso_stream_.open( iso_path_, std::ios::binary );

        if( !iso_stream_ )
        {
            throw std::runtime_error( "Could not open ISO: " + iso_path_.string() );
        }

        parse_primary_volume_descriptor();
    }

    bool Iso9660Source::exists( std::string_view virtual_path ) const
    {
        const std::string normalized = normalize_virtual_path( virtual_path );
        return files_.contains( normalized ) || directories_.contains( normalized );
    }

    bool Iso9660Source::is_directory( std::string_view virtual_path ) const
    {
        return directories_.contains( normalize_virtual_path( virtual_path ) );
    }

    std::vector<std::uint8_t> Iso9660Source::get_file( std::string_view virtual_path ) const
    {
        const std::string normalized = normalize_virtual_path( virtual_path );

        const auto it = files_.find( normalized );

        if( it == files_.end() )
        {
            throw std::runtime_error( "ISO file not found: " + std::string( virtual_path ) );
        }

        return read_extent( it->second.extent, it->second.size );
    }

    std::vector<std::string> Iso9660Source::list_directory( std::string_view virtual_path ) const
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

    std::vector<SourceFileInfo> Iso9660Source::list_all_files() const
    {
        std::vector<SourceFileInfo> result;
        result.reserve( files_.size() );

        for( const auto &[ path, entry ] : files_ )
        {
            result.push_back( SourceFileInfo{ .path = path, .size = entry.size, .directory = false } );
        }

        std::sort( result.begin(),
            result.end(),
            []( const SourceFileInfo &a, const SourceFileInfo &b )
            {
                return a.path < b.path;
            } );
        return result;
    }

    void Iso9660Source::parse_primary_volume_descriptor()
    {
        std::array<std::uint8_t, sector_size> sector{};

        iso_stream_.seekg( static_cast<std::streamoff>( 16 ) * sector_size, std::ios::beg );
        iso_stream_.read( reinterpret_cast<char *>( sector.data() ), sector.size() );

        if( !iso_stream_ )
        {
            throw std::runtime_error( "Could not read ISO primary volume descriptor" );
        }

        if( sector[ 0 ] != 1 || std::memcmp( sector.data() + 1, "CD001", 5 ) != 0 )
        {
            throw std::runtime_error( "Unsupported ISO: primary volume descriptor CD001 not found at sector 16" );
        }

        // Root directory record starts at byte 156 in the primary volume descriptor.
        const std::size_t root_offset = 156;
        const std::uint8_t record_length = sector[ root_offset ];

        if( record_length < 34 )
        {
            throw std::runtime_error( "Invalid ISO root directory record" );
        }

        Entry root{};
        root.path = "";

        root.extent = piko::utils::read_le_u32( sector.data() + root_offset + 2 );

        root.size = piko::utils::read_le_u32( sector.data() + root_offset + 10 );

        root.directory = true;

        directories_.emplace( root.path, root );

        std::unordered_set<std::uint32_t> visited_extents;
        parse_directory( root, visited_extents );
    }

    void Iso9660Source::parse_directory( const Entry &directory_entry,
        std::unordered_set<std::uint32_t> &visited_extents )
    {
        if( !visited_extents.insert( directory_entry.extent ).second )
        {
            return;
        }

        const std::vector<std::uint8_t> data = read_extent( directory_entry.extent, directory_entry.size );
        std::size_t offset = 0;

        while( offset < data.size() )
        {
            const std::uint8_t length = data[ offset ];

            if( length == 0 )
            {
                // Directory records do not cross sector boundaries. A zero length record pads to the next sector.
                offset = ( ( offset / sector_size ) + 1 ) * sector_size;
                continue;
            }

            if( offset + length > data.size() )
            {
                break;
            }

            Entry entry = parse_directory_record( data, offset, directory_entry.path );
            offset += length;

            if( entry.path.empty() )
            {
                continue;
            }

            const std::string child_name = virtual_filename( entry.path );
            add_directory_child( directory_entry.path, child_name );

            if( entry.directory )
            {
                directories_[ entry.path ] = entry;
                parse_directory( entry, visited_extents );
            }
            else
            {
                files_[ entry.path ] = entry;
            }
        }
    }

    std::vector<std::uint8_t> Iso9660Source::read_extent( std::uint32_t extent, std::uint32_t size ) const
    {
        std::vector<std::uint8_t> data( size );

        if( size == 0 )
        {
            return data;
        }

        iso_stream_.clear();
        iso_stream_.seekg( static_cast<std::streamoff>( extent ) * sector_size, std::ios::beg );
        iso_stream_.read( reinterpret_cast<char *>( data.data() ), static_cast<std::streamsize>( data.size() ) );

        if( !iso_stream_ )
        {
            throw std::runtime_error( "Could not read ISO extent" );
        }

        return data;
    }

    Iso9660Source::Entry Iso9660Source::parse_directory_record( const std::vector<std::uint8_t> &data,
        std::size_t offset,
        const std::string &parent_path ) const
    {
        Entry entry{};

        const std::uint8_t length = data[ offset ];

        if( length < 34 )
        {
            return entry;
        }

        const std::uint8_t name_length = data[ offset + 32 ];

        if( offset + 33 + name_length > data.size() )
        {
            return entry;
        }

        const std::uint8_t *name_data = data.data() + offset + 33;

        // 0 = current directory, 1 = parent directory.
        if( name_length == 1 && ( name_data[ 0 ] == 0 || name_data[ 0 ] == 1 ) )
        {
            return entry;
        }

        const std::string name = decode_iso_identifier( name_data, name_length );

        if( name.empty() )
        {
            return entry;
        }

        entry.extent = piko::utils::read_le_u32( data.data() + offset + 2 );

        entry.size = piko::utils::read_le_u32( data.data() + offset + 10 );

        entry.directory = ( data[ offset + 25 ] & 0x02 ) != 0;

        entry.path = parent_path.empty() ? name : parent_path + "/" + name;

        entry.path = normalize_virtual_path( entry.path );
        return entry;
    }

    void Iso9660Source::add_directory_child( const std::string &parent, const std::string &child_name )
    {
        auto &entries = directory_entries_[ parent ];

        if( std::find( entries.begin(), entries.end(), child_name ) == entries.end() )
        {
            entries.push_back( child_name );
        }
    }
}
