#include "XGM.hpp"
#include "Utils/BinaryUtil.hpp"
#include "Utils/StrUtil.hpp"

#include <cstddef>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>

namespace piko::filetypes::xgm
{
    namespace parser
    {
        class Reader
        {
        public:
            explicit Reader( std::span<const std::uint8_t> bytes ) : bytes_( bytes )
            {
            }

            [[nodiscard]] std::size_t offset() const
            {
                return offset_;
            }

            [[nodiscard]] std::size_t remaining() const
            {
                return bytes_.size() - offset_;
            }

            void seek( std::size_t offset, const char *context )
            {
                if( offset > bytes_.size() )
                {
                    throw DecodeError( std::string( "XGM seek outside file while reading " ) + context );
                }

                offset_ = offset;
            }

            std::uint32_t read_u32( const char *field_name )
            {
                if( remaining() < 4 )
                {
                    throw DecodeError( std::string( "Unexpected end of XGM while reading " ) + field_name );
                }

                const std::uint8_t *p = bytes_.data() + offset_;
                offset_ += 4;

                return piko::utils::read_le_u32( p );
            }

            float read_f32( const char *field_name )
            {
                const std::uint32_t bits = read_u32( field_name );

                float value = 0.0f;
                static_assert( sizeof( value ) == sizeof( bits ) );
                std::memcpy( &value, &bits, sizeof( value ) );

                return value;
            }

            std::string read_fixed_string( std::size_t size, const char *field_name )
            {
                if( remaining() < size )
                {
                    throw DecodeError( std::string( "Unexpected end of XGM while reading " ) + field_name );
                }

                const char *begin = reinterpret_cast<const char *>( bytes_.data() + offset_ );

                std::size_t length = 0;
                while( length < size && begin[ length ] != '\0' )
                {
                    ++length;
                }

                offset_ += size;
                return std::string( begin, begin + length );
            }

            std::span<const std::uint8_t> span_at( std::size_t offset, std::size_t size, const char *field_name ) const
            {
                if( offset > bytes_.size() || size > bytes_.size() - offset )
                {
                    throw DecodeError( std::string( "XGM span outside file while reading " ) + field_name );
                }

                return std::span<const std::uint8_t>( bytes_.data() + offset, size );
            }

        private:
            std::span<const std::uint8_t> bytes_;
            std::size_t offset_ = 0;
        };

        void validate_imx_magic( std::span<const std::uint8_t> bytes,
            std::size_t texture_index,
            std::string_view filename )
        {
            if( bytes.size() < 4 ||
                !( bytes[ 0 ] == 'I' && bytes[ 1 ] == 'M' && bytes[ 2 ] == 'X' && bytes[ 3 ] == 0 ) )
            {
                std::ostringstream message;
                message << "XGM texture " << texture_index << " (" << filename << ") does not start with IMX magic";
                throw DecodeError( message.str() );
            }
        }
    }

    DecodeError::DecodeError( const std::string &message ) : std::runtime_error( message )
    {
    }

    std::vector<std::uint8_t> read_file_bytes( const std::filesystem::path &path )
    {
        std::ifstream file( path, std::ios::binary );

        if( !file )
        {
            throw DecodeError( "Could not open XGM file: " + path.string() );
        }

        file.seekg( 0, std::ios::end );

        const std::streamoff size = file.tellg();

        if( size <= 0 )
        {
            throw DecodeError( "XGM file is empty: " + path.string() );
        }

        file.seekg( 0, std::ios::beg );

        std::vector<std::uint8_t> bytes( static_cast<std::size_t>( size ) );

        file.read( reinterpret_cast<char *>( bytes.data() ), static_cast<std::streamsize>( bytes.size() ) );

        if( !file )
        {
            throw DecodeError( "Could not read full XGM file: " + path.string() );
        }

        return bytes;
    }

    Metadata decode_metadata( std::span<const std::uint8_t> bytes )
    {
        parser::Reader reader( bytes );

        Metadata metadata{};
        metadata.texture_count = reader.read_u32( "texture count" );

        metadata.model_count = reader.read_u32( "model count" );

        metadata.textures.reserve( metadata.texture_count );

        for( std::uint32_t i = 0; i < metadata.texture_count; ++i )
        {
            TextureMetadata texture{};
            texture.entry_offset = reader.offset();

            texture.original_path = reader.read_fixed_string( 0x100, "texture original path" );

            texture.filename = reader.read_fixed_string( 0x10, "texture filename" );

            texture.index = reader.read_u32( "texture index" );

            texture.size = reader.read_u32( "texture size" );

            texture.cumulative_size_before = reader.read_u32( "texture cumulative size before" );

            texture.usage_hint = reader.read_u32( "texture usage hint" );

            texture.unknown_120 = reader.read_u32( "texture unknown 0x120" );

            reader.seek( reader.offset() + 0x0C, "texture reserved bytes" );

            texture.data_offset = reader.offset();

            const auto imx_magic = reader.span_at( texture.data_offset, 4, "embedded texture magic" );

            parser::validate_imx_magic( imx_magic, i, texture.filename );

            reader.seek( texture.data_offset + texture.size, "next texture entry" );

            metadata.textures.push_back( std::move( texture ) );
        }

        metadata.models.reserve( metadata.model_count );

        for( std::uint32_t i = 0; i < metadata.model_count; ++i )
        {
            ModelMetadata model{};
            model.entry_offset = reader.offset();

            model.original_path = reader.read_fixed_string( 0x100, "model original path" );

            model.filename = reader.read_fixed_string( 0x10, "model filename" );

            model.index = reader.read_u32( "model index" );

            model.size = reader.read_u32( "model size" );

            model.animation_count = reader.read_u32( "model animation count" );

            model.unknown_11C = reader.read_u32( "model unknown 0x11C" );

            model.animations.reserve( model.animation_count );

            for( std::uint32_t anim = 0; anim < model.animation_count; ++anim )
            {
                AnimationEntry entry{};
                entry.playback_length = reader.read_f32( "animation playback length" );

                entry.keyframe_interval = reader.read_f32( "animation keyframe interval" );
                (void)reader.read_f32( "animation reserved 60" );
                entry.start_keyframe = reader.read_f32( "animation start keyframe" );

                entry.speed_mode = reader.read_u32( "animation speed mode" );
                (void)reader.read_u32( "animation reserved 0x14" );
                (void)reader.read_u32( "animation reserved 0x18" );
                (void)reader.read_u32( "animation reserved 0x1C" );

                model.animations.push_back( entry );
            }

            model.data_offset = reader.offset();
            reader.seek( model.data_offset + model.size, "next model entry" );

            metadata.models.push_back( std::move( model ) );
        }

        return metadata;
    }

    Texture decode_texture( std::span<const std::uint8_t> bytes, std::size_t texture_index )
    {
        const Metadata metadata = decode_metadata( bytes );

        if( texture_index >= metadata.textures.size() )
        {
            std::ostringstream message;
            message << "XGM texture index " << texture_index << " is outside texture count "
                    << metadata.textures.size();

            throw DecodeError( message.str() );
        }

        const TextureMetadata &texture_metadata = metadata.textures[ texture_index ];

        parser::Reader reader( bytes );

        const auto imx_bytes =
            reader.span_at( texture_metadata.data_offset, texture_metadata.size, "embedded IMX texture" );

        Texture texture{};
        texture.metadata = texture_metadata;

        texture.image = imx::decode( imx_bytes );

        return texture;
    }

    std::vector<std::uint8_t> decode_texture_bytes( std::span<const std::uint8_t> bytes, std::size_t texture_index )
    {
        const Metadata metadata = decode_metadata( bytes );

        if( texture_index >= metadata.textures.size() )
        {
            std::ostringstream message;
            message << "XGM texture index " << texture_index << " is outside texture count "
                    << metadata.textures.size();

            throw DecodeError( message.str() );
        }

        const TextureMetadata &texture_metadata = metadata.textures[ texture_index ];

        parser::Reader reader( bytes );

        const auto imx_bytes =
            reader.span_at( texture_metadata.data_offset, texture_metadata.size, "embedded IMX texture" );

        return std::vector<std::uint8_t>( imx_bytes.begin(), imx_bytes.end() );
    }

    Metadata load_metadata( const std::filesystem::path &path )
    {
        return decode_metadata( read_file_bytes( path ) );
    }

    Texture load_texture( const std::filesystem::path &path, std::size_t texture_index )
    {
        return decode_texture( read_file_bytes( path ), texture_index );
    }

    std::vector<std::uint8_t> load_texture_bytes( const std::filesystem::path &path, std::size_t texture_index )
    {
        return decode_texture_bytes( read_file_bytes( path ), texture_index );
    }

    Package::Package( std::filesystem::path path )
    {
        load( path );
    }

    bool Package::load( const std::filesystem::path &path )
    {
        clear();

        metadata_ = load_metadata( path );

        path_ = path;

        loaded_ = true;

        rebuild_texture_lookup();
        return true;
    }

    void Package::clear()
    {
        loaded_ = false;
        path_.clear();
        metadata_ = Metadata{};
        texture_by_name_.clear();
    }

    bool Package::loaded() const
    {
        return loaded_;
    }

    const std::filesystem::path &Package::path() const
    {
        return path_;
    }

    const Metadata &Package::metadata() const
    {
        return metadata_;
    }

    std::size_t Package::texture_count() const
    {
        return metadata_.textures.size();
    }

    std::size_t Package::model_count() const
    {
        return metadata_.models.size();
    }

    bool Package::has_texture( std::string_view texture_name ) const
    {
        return find_texture_index( texture_name ).has_value();
    }

    std::optional<std::size_t> Package::find_texture_index( std::string_view texture_name ) const
    {
        const std::string key = normalize_texture_key( texture_name );

        const auto it = texture_by_name_.find( key );

        if( it == texture_by_name_.end() )
        {
            return std::nullopt;
        }

        return it->second;
    }

    const TextureMetadata *Package::texture_metadata_by_index( std::size_t texture_index ) const
    {
        if( texture_index >= metadata_.textures.size() )
        {
            return nullptr;
        }

        return &metadata_.textures[ texture_index ];
    }

    const TextureMetadata *Package::texture_metadata_by_name( std::string_view texture_name ) const
    {
        const std::optional<std::size_t> index = find_texture_index( texture_name );

        if( !index )
        {
            return nullptr;
        }

        return texture_metadata_by_index( *index );
    }

    Texture Package::load_texture_by_index( std::size_t texture_index ) const
    {
        if( !loaded_ )
        {
            throw DecodeError( "XGM package texture load called before load()" );
        }

        return piko::filetypes::xgm::load_texture( path_, texture_index );
    }

    Texture Package::load_texture( std::string_view texture_name ) const
    {
        if( !loaded_ )
        {
            throw DecodeError( "XGM package texture load called before load()" );
        }

        const std::optional<std::size_t> index = find_texture_index( texture_name );

        if( !index )
        {
            std::ostringstream message;
            message << "Texture '" << texture_name << "' was not found in XGM package " << path_.string();
            throw DecodeError( message.str() );
        }

        return load_texture_by_index( *index );
    }

    std::vector<std::uint8_t> Package::load_texture_bytes_by_index( std::size_t texture_index ) const
    {
        if( !loaded_ )
        {
            throw DecodeError( "XGM package texture byte load called before load()" );
        }

        return piko::filetypes::xgm::load_texture_bytes( path_, texture_index );
    }

    std::vector<std::uint8_t> Package::load_texture_bytes( std::string_view texture_name ) const
    {
        if( !loaded_ )
        {
            throw DecodeError( "XGM package texture byte load called before load()" );
        }

        const std::optional<std::size_t> index = find_texture_index( texture_name );

        if( !index )
        {
            std::ostringstream message;
            message << "Texture '" << texture_name << "' was not found in XGM package " << path_.string();
            throw DecodeError( message.str() );
        }

        return load_texture_bytes_by_index( *index );
    }

    std::vector<std::string> Package::texture_names() const
    {
        std::vector<std::string> names;
        names.reserve( metadata_.textures.size() );

        for( const TextureMetadata &texture : metadata_.textures )
        {
            names.push_back( texture.filename );
        }

        return names;
    }

    std::string Package::normalize_texture_key( std::string_view texture_name )
    {
        std::string key = piko::utils::normalize_slashes( std::string( texture_name ) );

        const std::size_t slash = key.find_last_of( '/' );

        if( slash != std::string::npos )
        {
            key.erase( 0, slash + 1 );
        }

        return piko::utils::uppercase_ascii( std::move( key ) );
    }

    void Package::rebuild_texture_lookup()
    {
        texture_by_name_.clear();
        texture_by_name_.reserve( metadata_.textures.size() );

        for( std::size_t i = 0; i < metadata_.textures.size(); ++i )
        {
            const TextureMetadata &texture = metadata_.textures[ i ];

            if( !texture.filename.empty() )
            {
                texture_by_name_.emplace( normalize_texture_key( texture.filename ), i );
            }

            if( !texture.original_path.empty() )
            {
                texture_by_name_.emplace( normalize_texture_key( texture.original_path ), i );
            }
        }
    }
}
