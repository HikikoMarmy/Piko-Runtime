#include "Chart/ChcJsonWriter.hpp"

#include "Utility/JsonUtil.hpp"

#include "FileTypes/CHC/CHC.hpp"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <string_view>

namespace piko::convert::chc_json
{
    void write_scene_range( std::ofstream &file,
        std::string_view name,
        const filetypes::chc::SceneFrameRange &range,
        bool trailing_comma )
    {
        file << "    \"" << name << "\": { \"first_frame\": " << range.first_frame
             << ", \"last_frame\": " << range.last_frame << " }" << ( trailing_comma ? ",\n" : "\n" );
    }

    void write_energy_factors( std::ofstream &file,
        const filetypes::chc::EnergyDamageFactors &factors,
        std::int32_t indent )
    {
        const std::string spaces( static_cast<std::size_t>( indent ), ' ' );
        file << spaces << "{\n";
        file << spaces << "  \"starting_energy\": " << factors.starting_energy << ",\n";
        file << spaces << "  \"charge_gain_press\": " << factors.charge_gain_press << ",\n";
        file << spaces << "  \"attack_damage_press\": " << factors.attack_damage_press << ",\n";
        file << spaces << "  \"guard_gain\": " << factors.guard_gain << ",\n";
        file << spaces << "  \"attack_miss_damage\": " << factors.attack_miss_damage << ",\n";
        file << spaces << "  \"guard_miss_damage\": " << factors.guard_miss_damage << ",\n";
        file << spaces << "  \"charge_gain_release\": " << factors.charge_gain_release << ",\n";
        file << spaces << "  \"attack_damage_release\": " << factors.attack_damage_release << "\n";
        file << spaces << "}";
    }

    void write_player_factors( std::ofstream &file,
        const filetypes::chc::PlayerEnergyDamageFactors &factors,
        std::int32_t indent )
    {
        const std::string spaces( static_cast<std::size_t>( indent ), ' ' );
        file << spaces << "{\n";
        file << spaces << "  \"intro\": ";
        write_energy_factors( file, factors.intro, indent + 2 );
        file << ",\n";
        file << spaces << "  \"charge\": ";
        write_energy_factors( file, factors.charge, indent + 2 );
        file << ",\n";
        file << spaces << "  \"battle\": ";
        write_energy_factors( file, factors.battle, indent + 2 );
        file << ",\n";
        file << spaces << "  \"harmony\": ";
        write_energy_factors( file, factors.harmony, indent + 2 );
        file << ",\n";
        file << spaces << "  \"end\": ";
        write_energy_factors( file, factors.end, indent + 2 );
        file << "\n" << spaces << "}";
    }
}

namespace piko::convert
{
    void convert_chc_to_json( std::span<const std::uint8_t> bytes, const std::filesystem::path &output_path )
    {
        const filetypes::chc::Song song = filetypes::chc::decode( bytes );

        if( output_path.has_parent_path() )
        {
            std::filesystem::create_directories( output_path.parent_path() );
        }

        std::ofstream file( output_path, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not create CHC JSON file: " + output_path.string() );
        }

        file << std::setprecision( 9 );
        file << "{\n";
        file << "  \"format\": \"piko-chc\",\n";
        file << "  \"version\": 3,\n";
        file << "  \"imc_path\": \"" << json_util::escape_string( song.imc_path ) << "\",\n";
        file << "  \"gameplay_speed_factor\": " << song.gameplay_speed_factor << ",\n";
        file << "  \"audio_mix\": {\n";
        file << "    \"authored\": " << ( song.audio_mix.authored ? "true" : "false" ) << ",\n";
        file << "    \"channels\": [\n";

        for( std::size_t index = 0; index < song.audio_mix.channels.size(); ++index )
        {
            const auto &channel = song.audio_mix.channels[ index ];
            file << "      { \"volume\": " << channel.volume << ", \"pan\": " << channel.pan << " }";
            file << ( index + 1 < song.audio_mix.channels.size() ? ",\n" : "\n" );
        }

        file << "    ]\n  },\n";
        file << "  \"result_frames\": {\n";
        chc_json::write_scene_range( file, "win_a", song.win_a, true );
        chc_json::write_scene_range( file, "win_b", song.win_b, true );
        chc_json::write_scene_range( file, "lose_pre", song.lose_pre, true );
        chc_json::write_scene_range( file, "lose_post", song.lose_post, false );
        file << "  },\n";
        file << "  \"cues\": [\n";

        for( std::size_t cue_index = 0; cue_index < song.cues.size(); ++cue_index )
        {
            const filetypes::chc::Cue &cue = song.cues[ cue_index ];
            file << "    {\n";
            file << "      \"index\": " << cue.index << ",\n";
            file << "      \"fallback_name\": \"" << json_util::escape_string( cue.fallback_name ) << "\",\n";
            file << "      \"audio_section_name\": \"" << json_util::escape_string( cue.audio_section_name ) << "\",\n";
            file << "      \"first_frame\": " << cue.first_frame << ",\n";
            file << "      \"last_frame\": " << cue.last_frame << ",\n";
            file << "      \"timeline_advance_samples\": " << cue.timeline_advance_samples << "\n";
            file << "    }" << ( cue_index + 1u == song.cues.size() ? "\n" : ",\n" );
        }

        file << "  ],\n";
        file << "  \"sections\": [\n";

        for( std::size_t section_index = 0; section_index < song.sections.size(); ++section_index )
        {
            const filetypes::chc::SongSection &section = song.sections[ section_index ];
            file << "    {\n";
            file << "      \"index\": " << section_index << ",\n";
            file << "      \"file_offset\": " << section.file_offset << ",\n";
            file << "      \"battle_phase\": " << section.battle_phase << ",\n";
            file << "      \"battle_phase_name\": \"" << filetypes::chc::battle_phase_name( section.battle_phase )
                 << "\",\n";

            file << "      \"tempo\": " << section.tempo << ",\n";
            file << "      \"duration_samples\": " << section.duration_samples << ",\n";
            file << "      \"tempo_changes\": [\n";

            for( std::size_t change_index = 0; change_index < section.tempo_changes.size(); ++change_index )
            {
                const filetypes::chc::TempoChange &change = section.tempo_changes[ change_index ];
                file << "        { \"start_sample\": " << change.start_sample << ", \"tempo\": " << change.tempo
                     << ", \"reserved_08\": " << change.reserved_08 << ", \"reserved_0c\": " << change.reserved_0c
                     << " }" << ( change_index + 1u == section.tempo_changes.size() ? "\n" : ",\n" );
            }

            file << "      ],\n";
            file << "      \"player_count\": " << section.player_count << ",\n";
            file << "      \"charts_per_player\": " << section.charts_per_player << ",\n";
            file << "      \"conditions\": [\n";

            for( std::size_t condition_index = 0; condition_index < section.conditions.size(); ++condition_index )
            {
                const filetypes::chc::Condition &condition = section.conditions[ condition_index ];
                file << "        { \"type\": " << condition.type << ", \"argument\": " << condition.argument
                     << ", \"true_effect\": " << condition.true_effect
                     << ", \"false_effect\": " << condition.false_effect << " }"
                     << ( condition_index + 1u == section.conditions.size() ? "\n" : ",\n" );
            }

            file << "      ],\n";
            file << "      \"charts\": [\n";

            for( std::size_t chart_index = 0; chart_index < section.charts.size(); ++chart_index )
            {
                const filetypes::chc::Chart &chart = section.charts[ chart_index ];
                file << "        {\n";
                file << "          \"index\": " << chart_index << ",\n";
                file << "          \"file_offset\": " << chart.file_offset << ",\n";
                file << "          \"start_sample\": " << chart.start_sample << ",\n";
                file << "          \"end_sample\": " << chart.end_sample << ",\n";
                file << "          \"traces\": [\n";

                for( std::size_t trace_index = 0; trace_index < chart.traces.size(); ++trace_index )
                {
                    const filetypes::chc::TraceFragment &trace = chart.traces[ trace_index ];
                    file << "            { \"start_sample\": " << trace.start_sample
                         << ", \"duration_samples\": " << trace.duration_samples
                         << ", \"stored_angle\": " << trace.stored_angle << ", \"curve\": " << trace.curve << " }"
                         << ( trace_index + 1u == chart.traces.size() ? "\n" : ",\n" );
                }

                file << "          ],\n";
                file << "          \"phrases\": [\n";

                for( std::size_t phrase_index = 0; phrase_index < chart.phrases.size(); ++phrase_index )
                {
                    const filetypes::chc::PhraseFragment &phrase = chart.phrases[ phrase_index ];
                    file << "            { \"start_sample\": " << phrase.start_sample
                         << ", \"duration_samples\": " << phrase.duration_samples
                         << ", \"phrase_start\": " << phrase.phrase_start << ", \"phrase_end\": " << phrase.phrase_end
                         << ", \"animation\": " << phrase.animation << ", \"reserved_14\": " << phrase.reserved_14
                         << ", \"reserved_18\": " << phrase.reserved_18 << ", \"reserved_1c\": " << phrase.reserved_1c
                         << " }" << ( phrase_index + 1u == chart.phrases.size() ? "\n" : ",\n" );
                }

                file << "          ],\n";
                file << "          \"guards\": [\n";

                for( std::size_t guard_index = 0; guard_index < chart.guards.size(); ++guard_index )
                {
                    const filetypes::chc::GuardFragment &guard = chart.guards[ guard_index ];
                    file << "            { \"start_sample\": " << guard.start_sample << ", \"button\": " << guard.button
                         << ", \"reserved_08\": " << guard.reserved_08 << ", \"reserved_0c\": " << guard.reserved_0c
                         << " }" << ( guard_index + 1u == chart.guards.size() ? "\n" : ",\n" );
                }

                file << "          ],\n";
                file << "          \"auxiliary_fragments\": [\n";

                for( std::size_t fragment_index = 0; fragment_index < chart.auxiliary_fragments.size();
                    ++fragment_index )
                {
                    const filetypes::chc::AuxiliaryFragment &fragment = chart.auxiliary_fragments[ fragment_index ];
                    file << "            { \"start_sample\": " << fragment.start_sample
                         << ", \"duration_samples\": " << fragment.duration_samples
                         << ", \"reserved_08\": " << fragment.reserved_08
                         << ", \"reserved_0c\": " << fragment.reserved_0c << " }"
                         << ( fragment_index + 1u == chart.auxiliary_fragments.size() ? "\n" : ",\n" );
                }

                file << "          ]\n";
                file << "        }" << ( chart_index + 1u == section.charts.size() ? "\n" : ",\n" );
            }

            file << "      ]\n";
            file << "    }" << ( section_index + 1u == song.sections.size() ? "\n" : ",\n" );
        }

        file << "  ],\n";
        file << "  \"energy_damage\": {\n";
        file << "    \"players\": [\n";

        for( std::size_t player_index = 0; player_index < 4u; ++player_index )
        {
            chc_json::write_player_factors( file, song.energy_damage.players[ player_index ], 6 );
            file << ( player_index == 3u ? "\n" : ",\n" );
        }

        file << "    ]\n";
        file << "  }\n";
        file << "}\n";

        if( !file )
        {
            throw std::runtime_error( "Could not write CHC JSON file: " + output_path.string() );
        }
    }
}
