#pragma once

#include <cstdint>
#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace piko::filetypes::chc
{
    enum class BattlePhase : std::uint32_t
    {
        Intro = 0,
        Charge = 1,
        Battle = 2,
        Guard = 3,
        Harmony = 4,
        End = 5
    };

    struct EnergyDamageFactors
    {
        float starting_energy = 0.0f;

        float charge_gain_press = 0.0f;
        float attack_damage_press = 0.0f;
        float guard_gain = 0.0f;

        float attack_miss_damage = 0.0f;
        float guard_miss_damage = 0.0f;

        float charge_gain_release = 0.0f;
        float attack_damage_release = 0.0f;
    };

    struct PlayerEnergyDamageFactors
    {
        EnergyDamageFactors intro;
        EnergyDamageFactors charge;
        EnergyDamageFactors battle;
        EnergyDamageFactors harmony;
        EnergyDamageFactors end;
    };

    struct SongEnergyDamageFactors
    {
        PlayerEnergyDamageFactors players[ 4 ]{};
    };

    struct TraceFragment
    {
        std::int32_t start_sample = 0;
        std::uint32_t duration_samples = 0;
        float stored_angle = 0.0f;
        std::uint32_t curve = 0;
    };

    struct PhraseFragment
    {
        std::int32_t start_sample = 0;
        std::uint32_t duration_samples = 0;
        std::uint32_t phrase_start = 0;
        std::uint32_t phrase_end = 0;
        std::uint32_t animation = 0;
        std::uint32_t reserved_14 = 0;
        std::uint32_t reserved_18 = 0;
        std::uint32_t reserved_1c = 0;
    };

    struct GuardFragment
    {
        std::int32_t start_sample = 0;
        std::uint32_t button = 0;
        std::uint32_t reserved_08 = 0;
        std::uint32_t reserved_0c = 0;
    };

    struct AuxiliaryFragment
    {
        std::int32_t start_sample = 0;
        std::uint32_t duration_samples = 0;
        std::uint32_t reserved_08 = 0;
        std::uint32_t reserved_0c = 0;
    };

    struct Chart
    {
        std::uint32_t file_offset = 0;

        std::uint32_t start_sample = 0;
        std::uint32_t end_sample = 0;

        std::vector<TraceFragment> traces;
        std::vector<PhraseFragment> phrases;
        std::vector<GuardFragment> guards;
        std::vector<AuxiliaryFragment> auxiliary_fragments;
    };

    struct Condition
    {
        std::uint32_t type = 0;
        float argument = 0.0f;
        std::int32_t true_effect = 0;
        std::int32_t false_effect = 0;
    };

    struct TempoChange
    {
        std::uint32_t start_sample = 0;
        float tempo = 0.0f;
        float reserved_08 = 0.0f;
        float reserved_0c = 0.0f;
    };

    struct SongSection
    {
        std::uint32_t file_offset = 0;

        std::uint32_t battle_phase = 0;
        float tempo = 0.0f;
        std::uint32_t duration_samples = 0;
        std::vector<TempoChange> tempo_changes;

        std::vector<Condition> conditions;

        std::uint32_t player_count = 0;
        std::uint32_t charts_per_player = 0;

        std::vector<Chart> charts;
    };

    struct Cue
    {
        std::uint32_t index = 0;
        std::string fallback_name;
        std::string audio_section_name;
        float first_frame = 0.0f;
        float last_frame = 0.0f;
        std::uint32_t timeline_advance_samples = 0;
    };

    struct SceneFrameRange
    {
        float first_frame = 0.0f;
        float last_frame = 0.0f;
    };

    struct AudioChannelMix
    {
        std::int32_t volume = 32767;
        std::int32_t pan = 16383;
    };

    struct AudioMix
    {
        bool authored = false;
        std::array<AudioChannelMix, 8> channels{};
    };

    struct Song
    {
        std::string imc_path;
        float gameplay_speed_factor = 240.0f;
        AudioMix audio_mix{};

        std::vector<Cue> cues;
        std::vector<SongSection> sections;

        SceneFrameRange win_a;
        SceneFrameRange win_b;
        SceneFrameRange lose_pre;
        SceneFrameRange lose_post;

        SongEnergyDamageFactors energy_damage{};
    };

    struct TraceSample
    {
        std::int64_t chart_sample = 0;
        float x = 0.0f;
        float y = 0.0f;
        float angle = 0.0f;
    };

    struct TracePosition
    {
        float x = 0.0f;
        float y = 0.0f;
        float angle = 0.0f;
    };

    [[nodiscard]] const char *battle_phase_name( std::uint32_t phase );

    [[nodiscard]] Song decode( std::span<const std::uint8_t> bytes );
    [[nodiscard]] Song load_file( const std::filesystem::path &path );

    [[nodiscard]] std::vector<TraceSample> build_trace_samples( const Chart &chart,
        float speed_factor,
        std::int32_t sample_step = 720 );

    [[nodiscard]] TracePosition trace_position_at( const std::vector<TraceSample> &samples, std::int64_t chart_sample );
}
