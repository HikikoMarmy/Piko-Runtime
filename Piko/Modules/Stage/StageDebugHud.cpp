#include "Modules/Stage/StageDebugHud.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <sstream>

namespace piko::modules::stage
{
    std::string format_count_line( const filetypes::chc::Song &song )
    {
        std::size_t chart_count = 0;
        std::size_t trace_count = 0;
        std::size_t phrase_count = 0;
        std::size_t guard_count = 0;

        for( const filetypes::chc::SongSection &section : song.sections )
        {
            chart_count += section.charts.size();

            for( const filetypes::chc::Chart &chart : section.charts )
            {
                trace_count += chart.traces.size();

                phrase_count += chart.phrases.size();

                guard_count += chart.guards.size();
            }
        }

        return "Sections " + std::to_string( song.sections.size() ) + " | Cues " + std::to_string( song.cues.size() ) +
               " | Charts " + std::to_string( chart_count ) + " | Trace " + std::to_string( trace_count ) +
               " | Phrases " + std::to_string( phrase_count ) + " | Guards " + std::to_string( guard_count );
    }

    std::string section_line( std::size_t index, const filetypes::chc::SongSection &section )
    {
        return "[" + std::to_string( index ) + "] " + filetypes::chc::battle_phase_name( section.battle_phase ) +
               " | dur " + std::to_string( section.duration_samples ) + " | players " +
               std::to_string( section.player_count ) + " | charts/p " + std::to_string( section.charts_per_player ) +
               " | cond " + std::to_string( section.conditions.size() );
    }

    std::string result_name( runtime::stage::StageConditionResult result )
    {
        switch( result )
        {
            case runtime::stage::StageConditionResult::True:
                return "true";
            case runtime::stage::StageConditionResult::False:
                return "false";
            case runtime::stage::StageConditionResult::Unknown:
                return "unknown";
        }

        return "unknown";
    }

    std::string condition_line( const filetypes::chc::Condition &condition,
        const runtime::stage::StageConditionState &state )
    {
        const runtime::stage::StageConditionDecision decision = runtime::stage::evaluate_condition( condition, state );

        char text[ 224 ]{};
        std::snprintf( text,
            sizeof( text ),
            "cond type=%u arg=%.2f true=%d false=%d -> %s effect=%d",
            condition.type,
            condition.argument,
            condition.true_effect,
            condition.false_effect,
            result_name( decision.truth ).c_str(),
            decision.selected_effect );

        return text;
    }

    void draw_labeled_bar( rendering::Renderer &renderer,
        const char *label,
        float x,
        float y,
        float width,
        float value,
        const rendering::Color &fill )
    {
        value = std::clamp( value, 0.0f, 1.0f );

        renderer.draw_text( x, y - 18.0f, label, rendering::Color{ 0.84f, 0.90f, 1.0f, 0.92f }, 1.0f );

        renderer.draw_filled_rect( rendering::Rect{ x, y, width, 14.0f },
            rendering::Color{ 0.06f, 0.07f, 0.11f, 0.94f } );

        renderer.draw_filled_rect( rendering::Rect{ x + 2.0f, y + 2.0f, ( width - 4.0f ) * value, 10.0f }, fill );
    }

    std::string transition_text( const runtime::stage::StageSectionTransition &transition )
    {
        if( transition.song_finished )
        {
            return "END";
        }

        return std::to_string( transition.next_section_index );
    }

    std::string join_sections( const std::vector<std::size_t> &sections )
    {
        if( sections.empty() )
        {
            return "-";
        }

        std::ostringstream out;

        for( std::size_t i = 0; i < sections.size(); ++i )
        {
            if( i != 0 )
            {
                out << ", ";
            }

            out << sections[ i ];
        }

        return out.str();
    }
}
