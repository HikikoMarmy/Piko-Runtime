#pragma once

#include "FileTypes/CHC/CHC.hpp"
#include "GameData/GameDataRoot.hpp"

#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>

namespace piko::runtime::stage
{
    enum class InstrumentResultKind : std::int32_t
    {
        Idle = 0,
        ChargePress = 1,
        ChargeRelease = 2,
        AttackPress = 3,
        AttackRelease = 4,
        GuardPress = 5,
        GuardRelease = 6
    };

    enum class InstrumentCommand : std::int32_t
    {
        Keep,
        Recover,
        Fade
    };

    struct InstrumentResult
    {
        InstrumentResultKind kind = InstrumentResultKind::Idle;
        float quality = 0.0f;
        bool retained = false;
        bool fade_locked = false;
        std::uint64_t note = 0;
    };

    class InstrumentResultController
    {
    public:
        void reset();
        [[nodiscard]] InstrumentCommand apply( const InstrumentResult &result,
            gamedata::ChartStyle style,
            bool secondary,
            bool cooperative );

    private:
        InstrumentResultKind previous_kind_ = InstrumentResultKind::Idle;
        std::uint64_t previous_note_ = 0;
        std::int32_t provisional_result_ = 0;
    };

    class StageInstrumentState
    {
    public:
        static constexpr std::int64_t hit_window_samples = 3800;
        static constexpr std::int64_t approach_samples = 100000;
        static constexpr std::int64_t psp_hit_window_samples = 4000;
        static constexpr std::int64_t release_trim_samples = 2500;
        static constexpr std::int64_t hold_padding_samples = 4500;

        void initialize( const filetypes::chc::Chart &chart,
            gamedata::ChartStyle style = gamedata::ChartStyle::GitarooMan );

        void reset();
        [[nodiscard]] InstrumentResult update( std::int64_t section_sample,
            bool aligned,
            bool pressed,
            bool held,
            bool automatic,
            bool attack,
            const std::array<bool, 4> &guard_pressed = {} );

        [[nodiscard]] std::int64_t end_section_sample() const;
        [[nodiscard]] bool holding() const;

    private:
        enum class NoteState : std::int32_t
        {
            Hidden = 0,
            Dormant = 1,
            Approaching = 2,
            Early = 3,
            Late = 4,
            Missed = 5,
            Held = 6,
            Released = 7,
            Automatic = 8
        };

        struct Note
        {
            std::int64_t start = 0;
            std::int64_t duration = 0;
            std::int64_t group_start = 0;
            std::int64_t group_duration = 0;
            bool begins = false;
            bool ends = false;
            NoteState state = NoteState::Dormant;
        };

        std::vector<Note> notes_;

        struct Guard
        {
            std::int64_t sample = 0;
            std::uint32_t button = 0;
            bool responded = false;
        };

        std::vector<Guard> guards_;
        std::int64_t chart_start_ = 0;
        std::int64_t chart_end_ = 0;
        std::size_t cursor_ = 0;
        std::int64_t hit_window_ = hit_window_samples;
        std::int64_t hold_padding_ = hold_padding_samples;
    };
}
