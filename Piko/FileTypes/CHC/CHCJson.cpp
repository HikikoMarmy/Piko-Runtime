#include "CHCJson.hpp"

#include "Utils/Json.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace piko::filetypes::chc::json
{
    using utils::json::Value;

    std::uint32_t u32( const Value &value, std::string_view field )
    {
        const double number = value.require( field ).number();

        if( number < 0.0 || number > static_cast<double>( std::numeric_limits<std::uint32_t>::max() ) )
        {
            throw std::runtime_error( "CHC JSON field is outside uint32 range: " + std::string( field ) );
        }

        return static_cast<std::uint32_t>( number );
    }

    std::int32_t i32( const Value &value, std::string_view field )
    {
        const double number = value.require( field ).number();

        if( number < static_cast<double>( std::numeric_limits<std::int32_t>::min() ) ||
            number > static_cast<double>( std::numeric_limits<std::int32_t>::max() ) )
        {
            throw std::runtime_error( "CHC JSON field is outside int32 range: " + std::string( field ) );
        }

        return static_cast<std::int32_t>( number );
    }

    float f32( const Value &value, std::string_view field )
    {
        return static_cast<float>( value.require( field ).number() );
    }

    SceneFrameRange scene_range( const Value &value )
    {
        return SceneFrameRange{ f32( value, "first_frame" ), f32( value, "last_frame" ) };
    }

    EnergyDamageFactors energy_factors( const Value &value )
    {
        EnergyDamageFactors result{};
        result.starting_energy = f32( value, "starting_energy" );

        result.charge_gain_press = f32( value, "charge_gain_press" );

        result.attack_damage_press = f32( value, "attack_damage_press" );

        result.guard_gain = f32( value, "guard_gain" );

        result.attack_miss_damage = f32( value, "attack_miss_damage" );

        result.guard_miss_damage = f32( value, "guard_miss_damage" );

        result.charge_gain_release = f32( value, "charge_gain_release" );

        result.attack_damage_release = f32( value, "attack_damage_release" );
        return result;
    }

    PlayerEnergyDamageFactors player_factors( const Value &value )
    {
        PlayerEnergyDamageFactors result{};
        result.intro = energy_factors( value.require( "intro" ) );

        result.charge = energy_factors( value.require( "charge" ) );

        result.battle = energy_factors( value.require( "battle" ) );

        result.harmony = energy_factors( value.require( "harmony" ) );

        result.end = energy_factors( value.require( "end" ) );
        return result;
    }

    TraceFragment trace( const Value &value )
    {
        TraceFragment result{};
        result.start_sample = i32( value, "start_sample" );

        result.duration_samples = u32( value, "duration_samples" );

        result.stored_angle = f32( value, "stored_angle" );

        result.curve = u32( value, "curve" );
        return result;
    }

    PhraseFragment phrase( const Value &value )
    {
        PhraseFragment result{};
        result.start_sample = i32( value, "start_sample" );

        result.duration_samples = u32( value, "duration_samples" );

        result.phrase_start = u32( value, "phrase_start" );

        result.phrase_end = u32( value, "phrase_end" );

        result.animation = u32( value, "animation" );

        result.reserved_14 = u32( value, "reserved_14" );

        result.reserved_18 = u32( value, "reserved_18" );

        result.reserved_1c = u32( value, "reserved_1c" );
        return result;
    }

    GuardFragment guard( const Value &value )
    {
        GuardFragment result{};
        result.start_sample = i32( value, "start_sample" );

        result.button = u32( value, "button" );

        result.reserved_08 = u32( value, "reserved_08" );

        result.reserved_0c = u32( value, "reserved_0c" );
        return result;
    }

    AuxiliaryFragment auxiliary_fragment( const Value &value )
    {
        AuxiliaryFragment result{};
        result.start_sample = i32( value, "start_sample" );

        result.duration_samples = u32( value, "duration_samples" );

        result.reserved_08 = u32( value, "reserved_08" );

        result.reserved_0c = u32( value, "reserved_0c" );
        return result;
    }

    Chart chart( const Value &value )
    {
        Chart result{};
        result.file_offset = u32( value, "file_offset" );

        result.start_sample = u32( value, "start_sample" );

        result.end_sample = u32( value, "end_sample" );

        for( const Value &entry : value.require( "traces" ).array() )
        {
            result.traces.push_back( trace( entry ) );
        }

        for( const Value &entry : value.require( "phrases" ).array() )
        {
            result.phrases.push_back( phrase( entry ) );
        }

        for( const Value &entry : value.require( "guards" ).array() )
        {
            result.guards.push_back( guard( entry ) );
        }

        if( const Value *entries = value.find( "auxiliary_fragments" ) )
        {
            for( const Value &entry : entries->array() )
            {
                result.auxiliary_fragments.push_back( auxiliary_fragment( entry ) );
            }
        }

        return result;
    }

    Condition condition( const Value &value )
    {
        Condition result{};
        result.type = u32( value, "type" );

        result.argument = f32( value, "argument" );

        result.true_effect = i32( value, "true_effect" );

        result.false_effect = i32( value, "false_effect" );
        return result;
    }

    TempoChange tempo_change( const Value &value )
    {
        TempoChange result{};
        result.start_sample = u32( value, "start_sample" );

        result.tempo = f32( value, "tempo" );

        result.reserved_08 = f32( value, "reserved_08" );

        result.reserved_0c = f32( value, "reserved_0c" );
        return result;
    }

    SongSection section( const Value &value )
    {
        SongSection result{};
        result.file_offset = u32( value, "file_offset" );

        result.battle_phase = u32( value, "battle_phase" );

        result.tempo = f32( value, "tempo" );

        result.duration_samples = u32( value, "duration_samples" );

        result.player_count = u32( value, "player_count" );

        result.charts_per_player = u32( value, "charts_per_player" );

        for( const Value &entry : value.require( "tempo_changes" ).array() )
        {
            result.tempo_changes.push_back( tempo_change( entry ) );
        }

        for( const Value &entry : value.require( "conditions" ).array() )
        {
            result.conditions.push_back( condition( entry ) );
        }

        for( const Value &entry : value.require( "charts" ).array() )
        {
            result.charts.push_back( chart( entry ) );
        }

        return result;
    }

    Cue cue( const Value &value )
    {
        Cue result{};
        result.index = u32( value, "index" );

        result.fallback_name = value.require( "fallback_name" ).string();

        result.audio_section_name = value.require( "audio_section_name" ).string();

        result.first_frame = f32( value, "first_frame" );

        result.last_frame = f32( value, "last_frame" );

        result.timeline_advance_samples = u32( value, "timeline_advance_samples" );
        return result;
    }
}

namespace piko::filetypes::chc
{
    Song load_json_file( const std::filesystem::path &path )
    {
        const utils::json::Value root = utils::json::load_file( path );

        if( root.require( "format" ).string() != "piko-chc" )
        {
            throw std::runtime_error( "Unsupported CHC JSON format: " + path.string() );
        }

        const std::int64_t version = root.require( "version" ).integer_or( 0 );

        if( version != 3 )
        {
            throw std::runtime_error( "Unsupported CHC JSON version: " + path.string() );
        }

        Song song{};
        song.imc_path = root.require( "imc_path" ).string();

        song.gameplay_speed_factor = static_cast<float>( root.require( "gameplay_speed_factor" ).number() );

        const auto &mix = root.require( "audio_mix" );
        song.audio_mix.authored = mix.require( "authored" ).boolean_or( false );

        const auto &channels = mix.require( "channels" ).array();

        if( channels.size() != song.audio_mix.channels.size() )
        {
            throw std::runtime_error( "Invalid CHC audio channel count" );
        }

        for( std::size_t index = 0; index < channels.size(); ++index )
        {
            song.audio_mix.channels[ index ].volume = std::clamp( json::i32( channels[ index ], "volume" ), 0, 32767 );

            song.audio_mix.channels[ index ].pan = std::clamp( json::i32( channels[ index ], "pan" ), 0, 32767 );
        }

        const utils::json::Value &ranges = root.require( "result_frames" );
        song.win_a = json::scene_range( ranges.require( "win_a" ) );

        song.win_b = json::scene_range( ranges.require( "win_b" ) );

        song.lose_pre = json::scene_range( ranges.require( "lose_pre" ) );

        song.lose_post = json::scene_range( ranges.require( "lose_post" ) );

        for( const utils::json::Value &entry : root.require( "cues" ).array() )
        {
            song.cues.push_back( json::cue( entry ) );
        }

        for( const utils::json::Value &entry : root.require( "sections" ).array() )
        {
            song.sections.push_back( json::section( entry ) );
        }

        const auto &players = root.require( "energy_damage" ).require( "players" ).array();

        if( players.size() != 4u )
        {
            throw std::runtime_error( "CHC JSON must contain four player factor records" );
        }

        for( std::size_t index = 0; index < players.size(); ++index )
        {
            song.energy_damage.players[ index ] = json::player_factors( players[ index ] );
        }

        if( song.sections.empty() )
        {
            throw std::runtime_error( "CHC JSON contains no sections: " + path.string() );
        }

        return song;
    }
}
