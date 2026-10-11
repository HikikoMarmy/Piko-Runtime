#include "StageConditionResolver.hpp"

#include <algorithm>

namespace piko::runtime::stage
{
    StageSectionTransition StageConditionResolver::evaluate_transition( const filetypes::chc::Song &song,
        std::size_t current_section_index,
        const StageConditionState &state,
        const StageConditionEvaluationContext &context ) const
    {
        if( current_section_index >= song.sections.size() )
        {
            return StageSectionTransition{ true, song.sections.size() };
        }

        const filetypes::chc::SongSection &section = song.sections[ current_section_index ];

        if( section.conditions.empty() )
        {
            const std::size_t next = current_section_index + 1;
            return StageSectionTransition{ next >= song.sections.size(), next };
        }

        std::size_t condition_index = 0;
        const std::size_t max_steps = std::max<std::size_t>( section.conditions.size() * 8, 32 );

        for( std::size_t step = 0; step < max_steps; ++step )
        {
            if( condition_index >= section.conditions.size() )
            {
                return StageSectionTransition{ true, song.sections.size() };
            }

            const filetypes::chc::Condition &condition = section.conditions[ condition_index ];

            const StageConditionDecision decision = evaluate_condition( condition, state, context );

            if( !decision.known )
            {
                ++condition_index;
                continue;
            }

            const bool truth = decision.truth == StageConditionResult::True;
            const std::int32_t effect = truth ? condition.true_effect : condition.false_effect;

            if( effect >= 0 )
            {
                return apply_effect( song, effect );
            }

            condition_index += static_cast<std::size_t>( -effect );
        }

        return StageSectionTransition{ true, song.sections.size() };
    }

    std::vector<std::size_t> StageConditionResolver::collect_possible_next_sections( const filetypes::chc::Song &song,
        std::size_t current_section_index ) const
    {
        std::vector<std::size_t> result;

        if( current_section_index >= song.sections.size() )
        {
            return result;
        }

        const filetypes::chc::SongSection &section = song.sections[ current_section_index ];

        if( section.conditions.empty() )
        {
            const std::size_t next = current_section_index + 1;

            if( next < song.sections.size() )
            {
                result.push_back( next );
            }

            return result;
        }

        collect_possible_from_condition( song, section, 0, result, 0 );

        std::sort( result.begin(), result.end() );
        result.erase( std::unique( result.begin(), result.end() ), result.end() );

        return result;
    }

    StageConditionDecision StageConditionResolver::evaluate_condition( const filetypes::chc::Condition &condition,
        const StageConditionState &state,
        const StageConditionEvaluationContext &context ) const
    {
        StageConditionDecision decision{};
        decision.known = true;

        switch( condition.type )
        {
            case 0:
                decision.truth = StageConditionResult::True;
                break;

            case 1:
                decision.truth =
                    state.left_energy < condition.argument ? StageConditionResult::True : StageConditionResult::False;
                break;

            case 2:
                decision.truth =
                    state.right_energy < condition.argument ? StageConditionResult::True : StageConditionResult::False;
                break;

            case 3:
                decision.truth = context.random_value > condition.argument ? StageConditionResult::True
                                                                           : StageConditionResult::False;
                break;

            case 4:
                decision.truth = !state.player3_available && condition.argument != 0.0f ? StageConditionResult::True
                                                                                        : StageConditionResult::False;
                break;

            case 5:
                decision.truth = !state.player4_available && condition.argument != 0.0f ? StageConditionResult::True
                                                                                        : StageConditionResult::False;
                break;

            case 6:
                decision.truth =
                    state.left_energy < state.right_energy ? StageConditionResult::True : StageConditionResult::False;
                break;

            default:
                decision.truth = StageConditionResult::Unknown;

                decision.known = false;
                break;
        }

        if( decision.truth == StageConditionResult::True )
        {
            decision.selected_effect = condition.true_effect;
        }
        else
        {
            decision.selected_effect = condition.false_effect;
        }

        return decision;
    }

    StageSectionTransition StageConditionResolver::apply_effect( const filetypes::chc::Song &song,
        std::int32_t effect ) const
    {
        const std::size_t next = static_cast<std::size_t>( effect );
        return StageSectionTransition{ next >= song.sections.size(), next };
    }

    void StageConditionResolver::collect_possible_from_condition( const filetypes::chc::Song &song,
        const filetypes::chc::SongSection &section,
        std::size_t condition_index,
        std::vector<std::size_t> &out_sections,
        std::size_t depth ) const
    {
        if( condition_index >= section.conditions.size() )
        {
            return;
        }

        if( depth > section.conditions.size() * 8 + 32 )
        {
            return;
        }

        const filetypes::chc::Condition &condition = section.conditions[ condition_index ];

        if( condition.type > 6u )
        {
            collect_possible_from_condition( song, section, condition_index + 1u, out_sections, depth + 1u );
            return;
        }

        const auto handle_effect = [ & ]( std::int32_t effect )
        {
            if( effect >= 0 )
            {
                const std::size_t next = static_cast<std::size_t>( effect );

                if( next < song.sections.size() )
                {
                    out_sections.push_back( next );
                }

                return;
            }

            collect_possible_from_condition( song,
                section,
                condition_index + static_cast<std::size_t>( -effect ),
                out_sections,
                depth + 1 );
        };

        handle_effect( condition.true_effect );

        if( condition.type != 0u )
        {
            handle_effect( condition.false_effect );
        }
    }

    StageConditionDecision evaluate_condition( const filetypes::chc::Condition &condition,
        const StageConditionState &state )
    {
        return StageConditionResolver{}.evaluate_condition( condition, state );
    }
}
