#pragma once

#include "FileTypes/CHC/CHC.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace piko::runtime::stage
{
    struct StageConditionState
    {

        float left_energy = 1.0f;
        float right_energy = 1.0f;

        bool player3_available = false;
        bool player4_available = false;
    };

    struct StageConditionEvaluationContext
    {

        float random_value = 0.0f;
    };

    enum class StageConditionResult
    {
        Unknown,
        False,
        True
    };

    struct StageConditionDecision
    {
        StageConditionResult truth = StageConditionResult::Unknown;
        std::int32_t selected_effect = 0;
        bool known = false;
    };

    struct StageSectionTransition
    {
        bool song_finished = false;
        std::size_t next_section_index = 0;
    };

    class StageConditionResolver
    {
    public:
        [[nodiscard]] StageSectionTransition evaluate_transition( const filetypes::chc::Song &song,
            std::size_t current_section_index,
            const StageConditionState &state,
            const StageConditionEvaluationContext &context ) const;

        [[nodiscard]] std::vector<std::size_t> collect_possible_next_sections( const filetypes::chc::Song &song,
            std::size_t current_section_index ) const;

        [[nodiscard]] StageConditionDecision evaluate_condition( const filetypes::chc::Condition &condition,
            const StageConditionState &state,
            const StageConditionEvaluationContext &context = {} ) const;

    private:
        [[nodiscard]] StageSectionTransition apply_effect( const filetypes::chc::Song &song,
            std::int32_t effect ) const;

        void collect_possible_from_condition( const filetypes::chc::Song &song,
            const filetypes::chc::SongSection &section,
            std::size_t condition_index,
            std::vector<std::size_t> &out_sections,
            std::size_t depth ) const;
    };

    [[nodiscard]] StageConditionDecision evaluate_condition( const filetypes::chc::Condition &condition,
        const StageConditionState &state );
}
