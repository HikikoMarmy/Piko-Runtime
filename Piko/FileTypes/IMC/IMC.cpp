#include "Utils/EnumUtil.hpp"
#include "IMC.hpp"
#include "Utils/StrUtil.hpp"
#include "Utils/BinaryUtil.hpp"

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace piko::filetypes::imc
{
    namespace parser
    {
        constexpr std::int32_t frame_bytes = 0x10;
        constexpr std::int32_t samples_per_frame = 28;

        struct Coef
        {
            std::int32_t c1 = 0;
            std::int32_t c2 = 0;
        };

        constexpr std::array<Coef, 5>
            coefs{
            Coef{ 0, 0 },
            Coef{ 60, 0 },
            Coef{ 115, -52 },
            Coef{ 98, -55 },
            Coef{ 122, -60 }
        };

        std::vector<std::uint8_t> read_file_bytes( const std::filesystem::path &path )
        {
            std::ifstream file( path, std::ios::binary | std::ios::ate );

            if( !file )
            {
                throw std::runtime_error( "Could not open IMC file: " + path.string() );
            }

            const std::streamsize size = file.tellg();

            if( size <= 0 )
            {
                throw std::runtime_error( "IMC file is empty: " + path.string() );
            }

            file.seekg( 0, std::ios::beg );

            std::vector<std::uint8_t> bytes( static_cast<std::size_t>( size ) );

            if( !file.read( reinterpret_cast<char *>( bytes.data() ), size ) )
            {
                throw std::runtime_error( "Could not read IMC file: " + path.string() );
            }

            return bytes;
        }

        std::uint32_t read_u32_le( std::span<const std::uint8_t> data, std::size_t offset )
        {
            if( offset + 4 > data.size() )
            {
                throw std::runtime_error( "IMC read_u32 out of range" );
            }

            return piko::utils::read_le_u32( data.data() + offset );
        }

        std::string read_fixed_name_16( std::span<const std::uint8_t> data, std::size_t offset )
        {
            if( offset + 16 > data.size() )
            {
                throw std::runtime_error( "IMC name out of range" );
            }

            std::size_t len = 0;

            while( len < 16 && data[ offset + len ] != 0 )
            {
                ++len;
            }

            return std::string( reinterpret_cast<const char *>( data.data() + offset ),
                reinterpret_cast<const char *>( data.data() + offset + len ) );
        }

        std::int16_t clamp_s16( std::int32_t value )
        {
            if( value > 32767 )
            {
                return 32767;
            }

            if( value < -32768 )
            {
                return -32768;
            }

            return static_cast<std::int16_t>( value );
        }

        void decode_nibble( std::int32_t nibble,
            std::int32_t shift,
            std::int32_t c1,
            std::int32_t c2,
            std::int32_t &history_1,
            std::int32_t &history_2,
            std::vector<std::int16_t> &output )
        {
            if( nibble >= 8 )
            {
                nibble -= 16;
            }

            std::int32_t sample = nibble << 12;
            sample >>= shift;

            const std::int32_t predictor = ( history_1 * c1 + history_2 * c2 + 32 ) >> 6;

            sample += predictor;

            const std::int16_t out = clamp_s16( sample );
            output.push_back( out );

            history_2 = history_1;

            history_1 = sample;
        }

        std::vector<std::int16_t> decode_channel( std::span<const std::uint8_t> channel_adpcm )
        {
            std::vector<std::int16_t> output;
            output.reserve( ( channel_adpcm.size() / frame_bytes ) * samples_per_frame );

            std::int32_t history_1 = 0;
            std::int32_t history_2 = 0;

            for( std::size_t offset = 0; offset + frame_bytes <= channel_adpcm.size(); offset += frame_bytes )
            {
                const std::uint8_t header = channel_adpcm[ offset + 0 ];
                const std::uint8_t flag = channel_adpcm[ offset + 1 ];

                std::int32_t coef_index = ( header >> 4 ) & 0x0F;
                std::int32_t shift = header & 0x0F;

                if( shift > 0x0C )
                {
                    shift = 0x0C;
                }

                if( coef_index < 0 || coef_index >= static_cast<std::int32_t>( coefs.size() ) )
                {
                    coef_index = 0;
                }

                const auto coef = coefs[ static_cast<std::size_t>( coef_index ) ];

                if( flag == 0x07 )
                {
                    break;
                }

                for( std::int32_t i = 0; i < 14; ++i )
                {
                    const std::uint8_t packed = channel_adpcm[ offset + 2 + static_cast<std::size_t>( i ) ];

                    const std::int32_t lo = packed & 0x0F;
                    const std::int32_t hi = ( packed >> 4 ) & 0x0F;

                    decode_nibble( lo, shift, coef.c1, coef.c2, history_1, history_2, output );
                    decode_nibble( hi, shift, coef.c1, coef.c2, history_1, history_2, output );
                }

                if( flag == 0x01 )
                {
                    break;
                }
            }

            return output;
        }
    }

    const Subsong *Container::find_subsong( std::string_view name ) const
    {
        const std::string target( name );

        for( const auto &subsong : subsongs )
        {
            if( subsong.table_entry.name == target )
            {
                return &subsong;
            }
        }

        const std::string target_upper = piko::utils::uppercase_ascii( target );

        for( const auto &subsong : subsongs )
        {
            if( piko::utils::uppercase_ascii( subsong.table_entry.name ) == target_upper )
            {
                return &subsong;
            }
        }

        return nullptr;
    }

    Container decode( std::span<const std::uint8_t> span )
    {
        if( span.size() < 4 )
        {
            throw std::runtime_error( "IMC too small" );
        }

        const std::uint32_t subsong_count = parser::read_u32_le( span, 0 );

        if( subsong_count == 0 || subsong_count > 4096 )
        {
            throw std::runtime_error( "Unreasonable IMC subsong count: " + std::to_string( subsong_count ) );
        }

        constexpr std::size_t table_entry_size = 32;
        const std::size_t table_start = 4;
        const std::size_t table_end = table_start + static_cast<std::size_t>( subsong_count ) * table_entry_size;

        if( table_end > span.size() )
        {
            throw std::runtime_error( "IMC table extends past end of file" );
        }

        std::vector<TableEntry> entries;
        entries.reserve( subsong_count );

        for( std::uint32_t i = 0; i < subsong_count; ++i )
        {
            const std::size_t offset = table_start + static_cast<std::size_t>( i ) * table_entry_size;

            TableEntry entry{};
            entry.index = static_cast<std::int32_t>( i );

            entry.name = parser::read_fixed_name_16( span, offset + 0x00 );

            entry.offset = parser::read_u32_le( span, offset + 0x10 );

            entry.unknown_1 = parser::read_u32_le( span, offset + 0x14 );

            entry.unknown_2 = parser::read_u32_le( span, offset + 0x18 );

            entry.load_mode = utils::to_enum<LoadMode>( parser::read_u32_le( span, offset + 0x1C ) );

            if( entry.offset > span.size() )
            {
                throw std::runtime_error( "IMC subsong offset out of range: " + entry.name );
            }

            if( i > 0 && entry.offset < entries.back().offset )
            {
                throw std::runtime_error( "IMC subsong offsets are not ascending" );
            }

            entries.push_back( std::move( entry ) );
        }

        Container container{};
        container.subsongs.reserve( subsong_count );

        for( std::size_t i = 0; i < entries.size(); ++i )
        {
            const std::size_t start = entries[ i ].offset;
            const std::size_t end =
                ( i + 1 < entries.size() ) ? static_cast<std::size_t>( entries[ i + 1 ].offset ) : span.size();

            if( end < start )
            {
                throw std::runtime_error( "Invalid IMC subsong range" );
            }

            Subsong subsong{};
            subsong.table_entry = entries[ i ];
            subsong.raw_subsong_data.assign( span.begin() + static_cast<std::ptrdiff_t>( start ),
                span.begin() + static_cast<std::ptrdiff_t>( end ) );

            container.subsongs.push_back( std::move( subsong ) );
        }

        return container;
    }

    Container load_file( const std::filesystem::path &path )
    {
        const std::vector<std::uint8_t> bytes = parser::read_file_bytes( path );
        return decode( bytes );
    }

    SubsongHeader parse_subsong_header( std::span<const std::uint8_t> raw_subsong )
    {
        if( raw_subsong.size() < 16 )
        {
            throw std::runtime_error( "IMC subsong too small" );
        }

        SubsongHeader header{};
        header.channels = parser::read_u32_le( raw_subsong, 0x00 );

        header.sample_rate = parser::read_u32_le( raw_subsong, 0x04 );

        header.frames_per_block = parser::read_u32_le( raw_subsong, 0x08 );

        header.total_blocks = parser::read_u32_le( raw_subsong, 0x0C );

        return header;
    }

    LoopPoints find_loop_points( std::span<const std::uint8_t> raw_subsong )
    {
        const SubsongHeader header = parse_subsong_header( raw_subsong );

        if( header.channels < 1 || header.frames_per_block == 0 || header.total_blocks == 0 )
        {
            return {};
        }

        if( header.total_blocks % header.channels != 0 )
        {
            return {};
        }

        const std::uint32_t blocks_per_channel = header.total_blocks / header.channels;
        const std::size_t bytes_per_block = static_cast<std::size_t>( header.frames_per_block ) * parser::frame_bytes;
        const std::size_t channel_stride = bytes_per_block * static_cast<std::size_t>( header.channels );

        if( raw_subsong.size() < 16 + channel_stride * static_cast<std::size_t>( blocks_per_channel ) )
        {
            return {};
        }

        LoopPoints points{};
        std::int64_t decoded_frame = 0;
        bool done = false;

        for( std::uint32_t block = 0; block < blocks_per_channel && !done; ++block )
        {
            const std::size_t block_offset = 16 + static_cast<std::size_t>( block ) * channel_stride;

            for( std::uint32_t frame = 0; frame < header.frames_per_block; ++frame )
            {
                const std::size_t offset = block_offset + static_cast<std::size_t>( frame ) * parser::frame_bytes;
                const std::uint8_t flag = raw_subsong[ offset + 1 ];

                if( ( flag & 0x04u ) != 0u && points.start_frame < 0 )
                {
                    points.start_frame = decoded_frame;
                }

                if( flag == 0x07u )
                {
                    if( points.start_frame >= 0 && points.end_frame < 0 )
                    {
                        points.end_frame = decoded_frame;
                    }

                    done = true;
                    break;
                }

                decoded_frame += parser::samples_per_frame;

                if( ( flag & 0x03u ) == 0x03u && points.start_frame >= 0 && points.end_frame < 0 )
                {
                    points.end_frame = decoded_frame;
                }

                if( flag == 0x01u )
                {
                    done = true;
                    break;
                }
            }
        }

        if( points.start_frame < 0 || points.end_frame <= points.start_frame )
        {
            return {};
        }

        return points;
    }

    Pcm16 decode_subsong_to_pcm16( std::span<const std::uint8_t> raw_subsong )
    {
        const SubsongHeader header = parse_subsong_header( raw_subsong );

        if( header.channels < 1 || header.channels > 8 )
        {
            throw std::runtime_error( "Invalid IMC channel count" );
        }

        if( header.sample_rate != 11025 && header.sample_rate != 16000 && header.sample_rate != 22050 &&
            header.sample_rate != 32000 && header.sample_rate != 44100 && header.sample_rate != 48000 )
        {
            throw std::runtime_error( "Invalid IMC sample rate" );
        }

        if( header.frames_per_block == 0 || header.total_blocks == 0 )
        {
            throw std::runtime_error( "Invalid IMC block counts" );
        }

        if( header.total_blocks % header.channels != 0 )
        {
            throw std::runtime_error( "IMC total block count is not divisible by channel count" );
        }

        const std::uint32_t blocks_per_channel = header.total_blocks / header.channels;
        const std::size_t bytes_per_block = static_cast<std::size_t>( header.frames_per_block ) * parser::frame_bytes;
        const std::size_t total_data_bytes = bytes_per_block * static_cast<std::size_t>( header.total_blocks );

        if( raw_subsong.size() < 16 + total_data_bytes )
        {
            throw std::runtime_error( "IMC subsong shorter than header predicts" );
        }

        const std::uint8_t *interleaved = raw_subsong.data() + 16;

        std::vector<std::vector<std::uint8_t>> channel_data( header.channels );

        for( auto &channel : channel_data )
        {
            channel.resize( static_cast<std::size_t>( blocks_per_channel ) * bytes_per_block );
        }

        std::size_t src = 0;

        for( std::uint32_t block = 0; block < blocks_per_channel; ++block )
        {
            for( std::uint32_t channel = 0; channel < header.channels; ++channel )
            {
                const std::size_t dst = static_cast<std::size_t>( block ) * bytes_per_block;

                std::memcpy( channel_data[ channel ].data() + dst, interleaved + src, bytes_per_block );

                src += bytes_per_block;
            }
        }

        std::vector<std::vector<std::int16_t>> channel_samples( header.channels );
        std::size_t max_samples = 0;

        for( std::uint32_t channel = 0; channel < header.channels; ++channel )
        {
            channel_samples[ channel ] = parser::decode_channel( channel_data[ channel ] );

            max_samples = std::max( max_samples, channel_samples[ channel ].size() );
        }

        Pcm16 pcm{};
        pcm.sample_rate = static_cast<std::int32_t>( header.sample_rate );

        pcm.channels = static_cast<std::int32_t>( header.channels );
        pcm.interleaved.resize( max_samples * static_cast<std::size_t>( pcm.channels ) );

        for( std::size_t frame = 0; frame < max_samples; ++frame )
        {
            for( std::int32_t channel = 0; channel < pcm.channels; ++channel )
            {
                std::int16_t sample = 0;

                if( frame < channel_samples[ channel ].size() )
                {
                    sample = channel_samples[ channel ][ frame ];
                }

                pcm.interleaved[ frame * static_cast<std::size_t>( pcm.channels ) +
                                 static_cast<std::size_t>( channel ) ] = sample;
            }
        }

        return pcm;
    }

    Pcm16 ensure_stereo( const Pcm16 &input )
    {
        if( input.channels <= 0 )
        {
            throw std::runtime_error( "Cannot convert invalid PCM channel count" );
        }

        if( input.channels == 2 )
        {
            return input;
        }

        const std::int64_t frames = input.frame_count();

        Pcm16 stereo{};
        stereo.sample_rate = input.sample_rate;

        stereo.channels = 2;
        stereo.interleaved.resize( static_cast<std::size_t>( frames ) * 2 );

        if( input.channels == 1 )
        {
            for( std::int64_t frame = 0; frame < frames; ++frame )
            {
                const std::int16_t sample = input.interleaved[ static_cast<std::size_t>( frame ) ];

                stereo.interleaved[ static_cast<std::size_t>( frame ) * 2 + 0 ] = sample;

                stereo.interleaved[ static_cast<std::size_t>( frame ) * 2 + 1 ] = sample;
            }

            return stereo;
        }

        for( std::int64_t frame = 0; frame < frames; ++frame )
        {
            double left = 0.0;
            double right = 0.0;

            for( std::int32_t channel = 0; channel < input.channels; ++channel )
            {
                const std::size_t src = static_cast<std::size_t>( frame ) * static_cast<std::size_t>( input.channels ) +
                                        static_cast<std::size_t>( channel );

                const double sample = static_cast<double>( input.interleaved[ src ] );

                switch( channel )
                {
                    case 0:
                    case 2:
                    case 3:
                    case 6:
                        left += sample;
                        break;

                    case 1:
                    case 4:
                    case 5:
                    case 7:
                        right += sample;
                        break;

                    default:
                        if( ( channel & 1 ) == 0 )
                        {
                            left += sample;
                        }
                        else
                        {
                            right += sample;
                        }
                        break;
                }
            }

            constexpr double gain = 0.5;
            left *= gain;

            right *= gain;

            left = std::clamp( left, -32768.0, 32767.0 );

            right = std::clamp( right, -32768.0, 32767.0 );

            const std::size_t dst = static_cast<std::size_t>( frame ) * 2;
            stereo.interleaved[ dst + 0 ] = static_cast<std::int16_t>( std::lrint( left ) );

            stereo.interleaved[ dst + 1 ] = static_cast<std::int16_t>( std::lrint( right ) );
        }

        return stereo;
    }
}
