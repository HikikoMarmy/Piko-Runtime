#include "Utils/EnumUtil.hpp"
#include "CHC.hpp"
#include "Utils/BinaryUtil.hpp"
#include "Utils/StrUtil.hpp"
#include "CHCJson.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <numbers>
#include <span>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace piko::filetypes::chc
{
    namespace parser
    {
        constexpr float pi = std::numbers::pi_v<float>;

        constexpr std::uint32_t make_magic( char a, char b, char c, char d )
        {
            return static_cast<std::uint32_t>( static_cast<unsigned char>( a ) ) |
                   ( static_cast<std::uint32_t>( static_cast<unsigned char>( b ) ) << 8 ) |
                   ( static_cast<std::uint32_t>( static_cast<unsigned char>( c ) ) << 16 ) |
                   ( static_cast<std::uint32_t>( static_cast<unsigned char>( d ) ) << 24 );
        }

        constexpr std::uint32_t magic_sngs = make_magic( 'S', 'N', 'G', 'S' );
        constexpr std::uint32_t magic_chls = make_magic( 'C', 'H', 'L', 'S' );
        constexpr std::uint32_t magic_chch = make_magic( 'C', 'H', 'C', 'H' );

        class Reader
        {
        public:
            explicit Reader( std::span<const std::byte> data ) : data_( data )
            {
            }

            [[nodiscard]] std::size_t position() const
            {
                return pos_;
            }

            void seek( std::size_t pos )
            {
                if( pos > data_.size() )
                {
                    throw std::runtime_error( "CHC reader seek out of range" );
                }

                pos_ = pos;
            }

            void skip( std::size_t count )
            {
                seek( pos_ + count );
            }

            std::uint32_t read_u32()
            {
                std::uint32_t value{};
                read_raw( value );
                return value;
            }

            std::int32_t read_s32()
            {
                std::int32_t value{};
                read_raw( value );
                return value;
            }

            float read_f32()
            {
                float value{};
                read_raw( value );
                return value;
            }

            std::string read_fixed_string( std::size_t size )
            {
                if( pos_ + size > data_.size() )
                {
                    throw std::runtime_error( "CHC reader string out of range" );
                }

                const char *ptr = reinterpret_cast<const char *>( data_.data() + pos_ );

                std::size_t len = 0;
                while( len < size && ptr[ len ] != '\0' )
                {
                    ++len;
                }

                pos_ += size;
                return std::string( ptr, ptr + len );
            }

            std::uint32_t read_magic()
            {
                return read_u32();
            }

        private:
            template <typename T> void read_raw( T &out )
            {
                if( pos_ + sizeof( T ) > data_.size() )
                {
                    throw std::runtime_error( "CHC reader read out of range" );
                }

                std::memcpy( &out, data_.data() + pos_, sizeof( T ) );
                pos_ += sizeof( T );
            }

        private:
            std::span<const std::byte> data_;
            std::size_t pos_ = 0;
        };

        [[noreturn]]
        void parse_error( const std::string &message, std::size_t offset )
        {
            std::ostringstream oss;
            oss << message << " at file offset 0x" << std::hex << std::uppercase << offset;

            throw std::runtime_error( oss.str() );
        }

        void expect_magic( Reader &reader, std::uint32_t expected, const char *name )
        {
            const std::size_t offset = reader.position();
            const std::uint32_t actual = reader.read_magic();

            if( actual != expected )
            {
                std::ostringstream oss;
                oss << "Expected magic " << name << ", got 0x" << std::hex << std::uppercase << actual;

                parse_error( oss.str(), offset );
            }
        }

        std::vector<std::byte> read_file_bytes( const std::filesystem::path &path )
        {
            std::ifstream file( path, std::ios::binary | std::ios::ate );

            if( !file )
            {
                throw std::runtime_error( "Could not open CHC file: " + path.string() );
            }

            const std::streamsize size = file.tellg();

            if( size <= 0 )
            {
                throw std::runtime_error( "CHC file is empty: " + path.string() );
            }

            file.seekg( 0, std::ios::beg );

            std::vector<std::byte> bytes( static_cast<std::size_t>( size ) );

            if( !file.read( reinterpret_cast<char *>( bytes.data() ), size ) )
            {
                throw std::runtime_error( "Could not read CHC file: " + path.string() );
            }

            return bytes;
        }

        std::uint32_t read_u32_at( std::span<const std::uint8_t> bytes, std::size_t offset )
        {
            if( offset + sizeof( std::uint32_t ) > bytes.size() )
            {
                throw std::runtime_error( "CHC read out of range" );
            }

            return piko::utils::read_le_u32( bytes.data() + offset );
        }

        bool is_wan_layout( std::span<const std::uint8_t> bytes )
        {
            if( bytes.size() < 0x144u )
            {
                return false;
            }

            if( read_u32_at( bytes, 0 ) != magic_sngs )
            {
                return false;
            }

            const std::uint32_t cue_count = read_u32_at( bytes, 0x13Cu );

            if( cue_count == 0 || cue_count > 256 )
            {
                return false;
            }

            const std::size_t section_count_offset = 0x140u + static_cast<std::size_t>( cue_count ) * 0x60u;

            if( section_count_offset + 8u > bytes.size() )
            {
                return false;
            }

            const std::uint32_t section_count = read_u32_at( bytes, section_count_offset );

            if( section_count == 0 || section_count > 256 )
            {
                return false;
            }

            return read_u32_at( bytes, section_count_offset + 4u ) == magic_chls;
        }

        Cue read_cue( Reader &reader )
        {
            Cue cue{};

            cue.index = reader.read_u32();

            cue.fallback_name = reader.read_fixed_string( 16 );

            cue.audio_section_name = reader.read_fixed_string( 16 );

            cue.first_frame = reader.read_f32();

            cue.last_frame = reader.read_f32();

            cue.timeline_advance_samples = reader.read_u32();

            return cue;
        }

        Cue read_wan_cue( Reader &reader )
        {
            Cue cue{};

            cue.index = reader.read_u32();

            cue.fallback_name = reader.read_fixed_string( 16 );

            cue.audio_section_name = reader.read_fixed_string( 16 );

            cue.first_frame = static_cast<float>( reader.read_u32() );

            cue.last_frame = static_cast<float>( reader.read_u32() );

            cue.timeline_advance_samples = reader.read_u32();
            reader.skip( 0x30 );

            return cue;
        }

        Condition read_condition( Reader &reader )
        {
            Condition condition{};

            condition.type = reader.read_u32();

            condition.argument = reader.read_f32();

            condition.true_effect = reader.read_s32();

            condition.false_effect = reader.read_s32();

            return condition;
        }

        TraceFragment read_trace_fragment( Reader &reader )
        {
            TraceFragment fragment{};

            fragment.start_sample = reader.read_s32();

            fragment.duration_samples = reader.read_u32();

            fragment.stored_angle = reader.read_f32();

            fragment.curve = reader.read_u32();

            return fragment;
        }

        PhraseFragment read_phrase_fragment( Reader &reader )
        {
            PhraseFragment fragment{};

            fragment.start_sample = reader.read_s32();

            fragment.duration_samples = reader.read_u32();

            fragment.phrase_start = reader.read_u32();

            fragment.phrase_end = reader.read_u32();

            fragment.animation = reader.read_u32();

            fragment.reserved_14 = reader.read_u32();

            fragment.reserved_18 = reader.read_u32();

            fragment.reserved_1c = reader.read_u32();

            return fragment;
        }

        GuardFragment read_guard_fragment( Reader &reader )
        {
            GuardFragment fragment{};

            fragment.start_sample = reader.read_s32();

            fragment.button = reader.read_u32();

            fragment.reserved_08 = reader.read_u32();

            fragment.reserved_0c = reader.read_u32();

            return fragment;
        }

        AuxiliaryFragment read_auxiliary_fragment( Reader &reader )
        {
            AuxiliaryFragment fragment{};

            fragment.start_sample = reader.read_s32();

            fragment.duration_samples = reader.read_u32();

            fragment.reserved_08 = reader.read_u32();

            fragment.reserved_0c = reader.read_u32();

            return fragment;
        }

        TempoChange read_tempo_change( Reader &reader )
        {
            TempoChange change{};

            change.start_sample = reader.read_u32();

            change.tempo = reader.read_f32();

            change.reserved_08 = reader.read_f32();

            change.reserved_0c = reader.read_f32();

            return change;
        }

        EnergyDamageFactors read_energy_damage_factors( Reader &reader )
        {
            EnergyDamageFactors factors{};

            factors.starting_energy = reader.read_f32();

            factors.charge_gain_press = reader.read_f32();

            factors.attack_damage_press = reader.read_f32();

            factors.guard_gain = reader.read_f32();

            factors.attack_miss_damage = reader.read_f32();

            factors.guard_miss_damage = reader.read_f32();

            factors.charge_gain_release = reader.read_f32();

            factors.attack_damage_release = reader.read_f32();

            return factors;
        }

        PlayerEnergyDamageFactors read_player_energy_damage_factors( Reader &reader )
        {
            PlayerEnergyDamageFactors factors{};

            factors.intro = read_energy_damage_factors( reader );

            factors.charge = read_energy_damage_factors( reader );

            factors.battle = read_energy_damage_factors( reader );

            factors.harmony = read_energy_damage_factors( reader );

            factors.end = read_energy_damage_factors( reader );

            return factors;
        }

        SongEnergyDamageFactors read_song_energy_damage_factors( Reader &reader )
        {
            SongEnergyDamageFactors factors{};

            const std::size_t factor_offset = reader.position();
            const std::uint32_t factor_count = reader.read_u32();

            if( factor_count != 20 )
            {
                parse_error( "Expected energy/damage factor count 20", factor_offset );
            }

            for( std::size_t i = 0; i < 4; ++i )
            {
                factors.players[ i ] = read_player_energy_damage_factors( reader );
            }

            return factors;
        }

        Chart read_chart( Reader &reader )
        {
            Chart chart{};

            const std::size_t chart_offset = reader.position();
            chart.file_offset = static_cast<std::uint32_t>( chart_offset );

            expect_magic( reader, magic_chch, "CHCH" );
            reader.skip( 4 );

            const std::uint32_t chart_size_minus_44 = reader.read_u32();
            const std::size_t chart_end = chart_offset + static_cast<std::size_t>( chart_size_minus_44 ) + 0x2C;

            reader.skip( 4 );
            reader.skip( 4 );
            reader.skip( 16 );

            chart.start_sample = reader.read_u32();

            chart.end_sample = reader.read_u32();

            const std::uint32_t trace_count = reader.read_u32();
            chart.traces.reserve( trace_count );

            for( std::uint32_t i = 0; i < trace_count; ++i )
            {
                chart.traces.push_back( read_trace_fragment( reader ) );
            }

            const std::uint32_t phrase_count = reader.read_u32();
            chart.phrases.reserve( phrase_count );

            for( std::uint32_t i = 0; i < phrase_count; ++i )
            {
                chart.phrases.push_back( read_phrase_fragment( reader ) );
            }

            const std::uint32_t guard_count = reader.read_u32();
            chart.guards.reserve( guard_count );

            for( std::uint32_t i = 0; i < guard_count; ++i )
            {
                chart.guards.push_back( read_guard_fragment( reader ) );
            }

            const std::uint32_t auxiliary_count = reader.read_u32();
            chart.auxiliary_fragments.reserve( auxiliary_count );

            for( std::uint32_t i = 0; i < auxiliary_count; ++i )
            {
                chart.auxiliary_fragments.push_back( read_auxiliary_fragment( reader ) );
            }

            if( reader.position() > chart_end )
            {
                parse_error( "CHCH parser overran chart size", chart_offset );
            }

            if( reader.position() < chart_end )
            {
                reader.seek( chart_end );
            }

            return chart;
        }

        SongSection read_song_section( Reader &reader )
        {
            SongSection section{};

            section.file_offset = static_cast<std::uint32_t>( reader.position() );

            expect_magic( reader, magic_chls, "CHLS" );

            reader.skip( 4 );
            reader.skip( 4 );
            reader.skip( 8 );
            reader.skip( 16 );

            section.battle_phase = reader.read_u32();

            section.tempo = reader.read_f32();

            section.duration_samples = reader.read_u32();

            const std::uint32_t tempo_change_count = reader.read_u32();
            section.tempo_changes.reserve( tempo_change_count );

            for( std::uint32_t i = 0; i < tempo_change_count; ++i )
            {
                section.tempo_changes.push_back( read_tempo_change( reader ) );
            }

            const std::uint32_t condition_count = reader.read_u32();
            section.conditions.reserve( condition_count );

            for( std::uint32_t i = 0; i < condition_count; ++i )
            {
                section.conditions.push_back( read_condition( reader ) );
            }

            section.player_count = reader.read_u32();

            section.charts_per_player = reader.read_u32();

            const std::uint64_t chart_count_u64 = static_cast<std::uint64_t>( section.player_count ) *
                                                  static_cast<std::uint64_t>( section.charts_per_player );

            if( chart_count_u64 > 4096 )
            {
                parse_error( "Unreasonable chart count", section.file_offset );
            }

            const auto chart_count = static_cast<std::uint32_t>( chart_count_u64 );

            section.charts.reserve( chart_count );

            for( std::uint32_t i = 0; i < chart_count; ++i )
            {
                section.charts.push_back( read_chart( reader ) );
            }

            return section;
        }

        SongSection read_wan_song_section( Reader &reader )
        {
            SongSection section{};

            section.file_offset = static_cast<std::uint32_t>( reader.position() );

            expect_magic( reader, magic_chls, "CHLS" );

            reader.skip( 4 );
            reader.skip( 4 );
            reader.skip( 8 );
            reader.skip( 16 );

            section.battle_phase = reader.read_u32();

            section.tempo = reader.read_f32();

            section.duration_samples = reader.read_u32();

            const std::uint32_t reserved = reader.read_u32();

            if( reserved != 0 )
            {
                parse_error( "Unexpected Gitaroo Man Wan CHLS field", section.file_offset + 0x30u );
            }

            const std::uint32_t chart_count = reader.read_u32();

            if( chart_count == 0 || chart_count > 4 )
            {
                parse_error( "Unreasonable Gitaroo Man Wan chart count", section.file_offset + 0x34u );
            }

            section.player_count = chart_count;

            section.charts_per_player = 1;
            section.charts.reserve( chart_count );

            for( std::uint32_t i = 0; i < chart_count; ++i )
            {
                section.charts.push_back( read_chart( reader ) );
            }

            return section;
        }

        EnergyDamageFactors make_wan_energy_damage_factors()
        {
            EnergyDamageFactors factors{};
            factors.starting_energy = 0.5f;

            factors.charge_gain_press = 0.03f;

            factors.attack_damage_press = 0.04f;

            factors.guard_gain = 0.04f;

            factors.attack_miss_damage = 0.05f;

            factors.guard_miss_damage = 0.05f;

            factors.charge_gain_release = 1.0f;

            factors.attack_damage_release = 1.0f;
            return factors;
        }

        SongEnergyDamageFactors make_wan_energy_damage()
        {
            SongEnergyDamageFactors song_factors{};
            const EnergyDamageFactors factors = make_wan_energy_damage_factors();

            for( PlayerEnergyDamageFactors &player : song_factors.players )
            {
                player.intro = factors;

                player.charge = factors;

                player.battle = factors;

                player.harmony = factors;

                player.end = factors;
            }

            return song_factors;
        }

        float effective_angle( float stored_angle )
        {
            return stored_angle + pi;
        }

        float retail_angle_delta( float current_stored_angle, float next_stored_angle )
        {
            const float current = -current_stored_angle;
            const float next = -next_stored_angle;
            const float raw_delta = next - current;
            const float wrapped_current = current < 0.0f ? current + pi * 2.0f : current;
            const float wrapped_next = next < 0.0f ? next + pi * 2.0f : next;
            const float wrapped_delta = wrapped_next - wrapped_current;
            const float retail_delta = std::abs( raw_delta ) < std::abs( wrapped_delta ) ? raw_delta : wrapped_delta;
            return -retail_delta;
        }

        float interpolate_angle_delta( float start_angle, float delta, float t )
        {
            return start_angle + delta * std::clamp( t, 0.0f, 1.0f );
        }

        float unwrap_angle_near( float angle, float reference )
        {
            return angle + std::round( ( reference - angle ) / ( pi * 2.0f ) ) * pi * 2.0f;
        }

        constexpr std::uint32_t official_short_curve_sample_limit = 0x2581u;
        constexpr float official_straight_transition_pixels = 40.0f;
        constexpr float official_min_segment_pixels = 0.001f;

        struct TraceDelta
        {
            float x = 0.0f;
            float y = 0.0f;

            TraceDelta &operator+=( const TraceDelta &other )
            {
                x += other.x;

                y += other.y;
                return *this;
            }
        };

        struct TraceSegmentModel
        {
            std::int32_t start_sample = 0;
            std::int32_t end_sample = 0;
            float length = 0.0f;
            float start_angle = 0.0f;
            float angle_delta = 0.0f;
            bool curve = false;
            bool tail_transition = false;
        };

        TraceDelta integrate_linear_angle( float start_angle,
            float angle_delta,
            float angle_distance,
            float from_distance,
            float to_distance )
        {
            from_distance = std::max( from_distance, 0.0f );

            to_distance = std::max( to_distance, from_distance );

            const float distance = to_distance - from_distance;

            if( distance <= 0.0f )
            {
                return {};
            }

            if( std::abs( angle_delta ) <= 0.000001f || angle_distance <= official_min_segment_pixels )
            {
                return TraceDelta{ std::cos( start_angle ) * distance, std::sin( start_angle ) * distance };
            }

            const float k = angle_delta / angle_distance;
            const float angle0 = start_angle + k * from_distance;
            const float angle1 = start_angle + k * to_distance;

            return TraceDelta{
                ( std::sin( angle1 ) - std::sin( angle0 ) ) / k,
                ( -std::cos( angle1 ) + std::cos( angle0 ) ) / k
            };
        }

        float angle_at_distance( const TraceSegmentModel &segment, float distance )
        {
            if( segment.length <= official_min_segment_pixels )
            {
                return segment.start_angle;
            }

            distance = std::clamp( distance, 0.0f, segment.length );

            if( segment.curve )
            {
                return interpolate_angle_delta( segment.start_angle, segment.angle_delta, distance / segment.length );
            }

            if( segment.tail_transition )
            {
                const float transition_length = std::min( official_straight_transition_pixels, segment.length );
                const float transition_start = segment.length - transition_length;

                if( distance <= transition_start )
                {
                    return segment.start_angle;
                }

                return interpolate_angle_delta( segment.start_angle,
                    segment.angle_delta,
                    ( distance - transition_start ) / transition_length );
            }

            return segment.start_angle;
        }

        TraceDelta integrate_segment_span( const TraceSegmentModel &segment, float from_distance, float to_distance )
        {
            from_distance = std::clamp( from_distance, 0.0f, segment.length );

            to_distance = std::clamp( to_distance, from_distance, segment.length );

            if( to_distance <= from_distance )
            {
                return {};
            }

            if( segment.curve )
            {
                return integrate_linear_angle( segment.start_angle,
                    segment.angle_delta,
                    segment.length,
                    from_distance,
                    to_distance );
            }

            if( segment.tail_transition )
            {
                const float transition_length = std::min( official_straight_transition_pixels, segment.length );
                const float transition_start = segment.length - transition_length;
                TraceDelta out{};

                if( from_distance < transition_start )
                {
                    const float straight_to = std::min( to_distance, transition_start );
                    const float straight_distance = straight_to - from_distance;

                    if( straight_distance > 0.0f )
                    {
                        out.x += std::cos( segment.start_angle ) * straight_distance;

                        out.y += std::sin( segment.start_angle ) * straight_distance;
                    }

                    from_distance = straight_to;
                }

                if( to_distance > transition_start )
                {
                    out += integrate_linear_angle( segment.start_angle,
                        segment.angle_delta,
                        transition_length,
                        from_distance - transition_start,
                        to_distance - transition_start );
                }

                return out;
            }

            const float distance = to_distance - from_distance;
            return TraceDelta{ std::cos( segment.start_angle ) * distance, std::sin( segment.start_angle ) * distance };
        }
    }

    const char *battle_phase_name( std::uint32_t phase )
    {
        switch( phase )
        {
            case utils::to_int( BattlePhase::Intro ):
                return "Intro";

            case utils::to_int( BattlePhase::Charge ):
                return "Charge";

            case utils::to_int( BattlePhase::Battle ):
                return "Battle";

            case utils::to_int( BattlePhase::Guard ):
                return "Guard";

            case utils::to_int( BattlePhase::Harmony ):
                return "Harmony";

            case utils::to_int( BattlePhase::End ):
                return "End";
        }

        return "Unknown";
    }

    Song decode( std::span<const std::uint8_t> bytes )
    {
        parser::Reader reader(
            std::span<const std::byte>( reinterpret_cast<const std::byte *>( bytes.data() ), bytes.size() ) );

        Song song{};

        expect_magic( reader, parser::magic_sngs, "SNGS" );

        reader.seek( 0x24 );
        song.imc_path = reader.read_fixed_string( 256 );

        if( parser::is_wan_layout( bytes ) )
        {
            reader.seek( 0x124 );
            song.win_a.first_frame = static_cast<float>( reader.read_u32() );

            song.win_a.last_frame = static_cast<float>( reader.read_u32() );

            song.win_b.first_frame = static_cast<float>( reader.read_u32() );

            song.win_b.last_frame = static_cast<float>( reader.read_u32() );

            song.lose_pre.first_frame = static_cast<float>( reader.read_u32() );

            song.lose_pre.last_frame = static_cast<float>( reader.read_u32() );

            song.lose_post = song.lose_pre;

            const std::uint32_t cue_count = reader.read_u32();
            song.cues.reserve( cue_count );

            for( std::uint32_t i = 0; i < cue_count; ++i )
            {
                song.cues.push_back( read_wan_cue( reader ) );
            }

            const std::size_t section_count_offset = reader.position();
            const std::uint32_t section_count = reader.read_u32();

            if( section_count == 0 || section_count > 256 )
            {
                parser::parse_error( "Unreasonable Gitaroo Man Wan SongSection count", section_count_offset );
            }

            song.sections.reserve( section_count );

            for( std::uint32_t i = 0; i < section_count; ++i )
            {
                song.sections.push_back( read_wan_song_section( reader ) );
            }

            if( reader.position() != bytes.size() )
            {
                parser::parse_error( "Unexpected trailing Gitaroo Man Wan CHC data", reader.position() );
            }

            song.energy_damage = parser::make_wan_energy_damage();
            return song;
        }

        if( bytes.size() >= 0x184 && parser::read_u32_at( bytes, 4 ) >= 0x1500 )
        {
            reader.seek( 0x144 );
            song.audio_mix.authored = true;

            for( AudioChannelMix &channel : song.audio_mix.channels )
            {
                channel.volume = std::clamp( reader.read_s32(), 0, 32767 );

                channel.pan = std::clamp( reader.read_s32(), 0, 32767 );
            }
        }

        reader.seek( 0x184 );
        song.gameplay_speed_factor = reader.read_f32();

        reader.seek( 0x124 );
        song.win_a.first_frame = reader.read_f32();

        song.win_a.last_frame = reader.read_f32();

        song.win_b.first_frame = reader.read_f32();

        song.win_b.last_frame = reader.read_f32();

        song.lose_pre.first_frame = reader.read_f32();

        song.lose_pre.last_frame = reader.read_f32();

        song.lose_post.first_frame = reader.read_f32();

        song.lose_post.last_frame = reader.read_f32();

        reader.seek( 0x188 );

        const std::uint32_t cue_count = reader.read_u32();

        song.cues.reserve( cue_count );

        for( std::uint32_t i = 0; i < cue_count; ++i )
        {
            song.cues.push_back( read_cue( reader ) );
        }

        const std::size_t after_cues = reader.position();
        const std::uint32_t section_count_or_magic = reader.read_u32();

        std::uint32_t section_count = 0;

        if( section_count_or_magic == parser::magic_chls )
        {
            section_count = cue_count;
            reader.seek( after_cues );
        }
        else
        {
            section_count = section_count_or_magic;
        }

        if( section_count == 0 || section_count > 256 )
        {
            parser::parse_error( "Unreasonable SongSection count", after_cues );
        }

        song.sections.reserve( section_count );

        for( std::uint32_t i = 0; i < section_count; ++i )
        {
            song.sections.push_back( read_song_section( reader ) );
        }

        song.energy_damage = read_song_energy_damage_factors( reader );

        return song;
    }

    Song load_file( const std::filesystem::path &path )
    {
        const std::string extension = piko::utils::lowercase_ascii( path.extension().string() );

        if( extension == ".json" )
        {
            return load_json_file( path );
        }

        const auto bytes = parser::read_file_bytes( path );
        return decode(
            std::span<const std::uint8_t>( reinterpret_cast<const std::uint8_t *>( bytes.data() ), bytes.size() ) );
    }

    std::vector<TraceSample> build_trace_samples( const Chart &chart, float speed_factor, std::int32_t sample_step )
    {
        if( chart.traces.empty() )
        {
            throw std::runtime_error( "Cannot build trace samples: chart has no trace fragments" );
        }

        if( speed_factor <= 0.0f )
        {
            throw std::runtime_error( "Cannot build trace samples: invalid speed factor" );
        }

        if( sample_step <= 0 )
        {
            sample_step = 720;
        }

        std::vector<TraceSample> samples;
        float x = 0.0f;
        float y = 0.0f;

        const std::size_t drawable_trace_count = chart.traces.size() - 1;

        for( std::size_t i = 0; i < drawable_trace_count; ++i )
        {
            const TraceFragment &fragment = chart.traces[ i ];

            parser::TraceSegmentModel segment{};
            segment.start_sample = fragment.start_sample;

            segment.end_sample = fragment.start_sample + static_cast<std::int32_t>( fragment.duration_samples );

            segment.length = static_cast<float>( fragment.duration_samples ) / speed_factor;

            if( fragment.duration_samples == 0 || segment.length <= parser::official_min_segment_pixels )
            {
                continue;
            }

            segment.start_angle = parser::effective_angle( fragment.stored_angle );

            if( !samples.empty() )
            {
                segment.start_angle = parser::unwrap_angle_near( segment.start_angle, samples.back().angle );
            }

            const bool has_later_drawable_trace = i + 1 < drawable_trace_count;

            if( has_later_drawable_trace )
            {
                const TraceFragment &next_fragment = chart.traces[ i + 1 ];
                segment.angle_delta = parser::retail_angle_delta( fragment.stored_angle, next_fragment.stored_angle );
            }

            const bool has_angle_delta = std::abs( segment.angle_delta ) >= 0.000001f;
            const bool retail_curve =
                has_later_drawable_trace &&
                ( fragment.curve != 0 || fragment.duration_samples < parser::official_short_curve_sample_limit );

            segment.curve = retail_curve && has_angle_delta;

            segment.tail_transition = !segment.curve && has_angle_delta && has_later_drawable_trace;

            if( samples.empty() )
            {
                samples.push_back(
                    TraceSample{ .chart_sample = segment.start_sample, .x = x, .y = y, .angle = segment.start_angle } );
            }

            std::int32_t current_sample = segment.start_sample;

            while( current_sample < segment.end_sample )
            {
                const std::int32_t next_sample = std::min( current_sample + sample_step, segment.end_sample );

                const float from_distance = static_cast<float>( current_sample - segment.start_sample ) / speed_factor;
                const float to_distance = static_cast<float>( next_sample - segment.start_sample ) / speed_factor;

                const parser::TraceDelta delta = integrate_segment_span( segment, from_distance, to_distance );

                x += delta.x;

                y += delta.y;

                samples.push_back( TraceSample{
                    .chart_sample = next_sample,
                    .x = x,
                    .y = y,
                    .angle = angle_at_distance( segment, to_distance )
                } );

                current_sample = next_sample;
            }
        }

        return samples;
    }

    TracePosition trace_position_at( const std::vector<TraceSample> &samples, std::int64_t chart_sample )
    {
        if( samples.empty() )
        {
            return {};
        }

        if( chart_sample <= samples.front().chart_sample )
        {
            return TracePosition{ .x = samples.front().x, .y = samples.front().y, .angle = samples.front().angle };
        }

        if( chart_sample >= samples.back().chart_sample )
        {
            return TracePosition{ .x = samples.back().x, .y = samples.back().y, .angle = samples.back().angle };
        }

        const auto upper = std::lower_bound( samples.begin(),
            samples.end(),
            chart_sample,
            []( const TraceSample &sample, std::int64_t value )
            {
                return sample.chart_sample < value;
            } );

        const auto &b = *upper;

        const auto &a = *( upper - 1 );

        const std::int64_t span = b.chart_sample - a.chart_sample;

        if( span <= 0 )
        {
            return TracePosition{ .x = a.x, .y = a.y, .angle = a.angle };
        }

        const float t = static_cast<float>( chart_sample - a.chart_sample ) / static_cast<float>( span );

        return TracePosition{
            .x = std::lerp( a.x, b.x, t ),
            .y = std::lerp( a.y, b.y, t ),
            .angle = std::lerp( a.angle, b.angle, t )
        };
    }
}
