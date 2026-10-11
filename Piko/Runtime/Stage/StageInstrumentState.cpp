#include "StageInstrumentState.hpp"

#include <algorithm>
#include <cmath>

namespace piko::runtime::stage
{
    void InstrumentResultController::reset()
    {
        previous_kind_ = InstrumentResultKind::Idle;

        previous_note_ = 0;

        provisional_result_ = 0;
    }

    InstrumentCommand InstrumentResultController::apply( const InstrumentResult &result,
        gamedata::ChartStyle style,
        bool secondary,
        bool cooperative )
    {
        InstrumentResultKind kind = result.kind;
        InstrumentCommand command = InstrumentCommand::Keep;
        const bool portable = style == gamedata::ChartStyle::GitarooManLives;
        const bool press = kind == InstrumentResultKind::ChargePress || kind == InstrumentResultKind::AttackPress ||
                           kind == InstrumentResultKind::GuardPress;

        if( press && previous_kind_ == kind && previous_note_ != 0 && previous_note_ != result.note &&
            provisional_result_ != 0 )
        {
            if( kind == InstrumentResultKind::ChargePress )
            {
                kind = InstrumentResultKind::ChargeRelease;
            }
            else if( kind == InstrumentResultKind::AttackPress )
            {
                kind = InstrumentResultKind::AttackRelease;
            }
            else
            {
                kind = InstrumentResultKind::GuardRelease;
            }
        }

        switch( kind )
        {
            case InstrumentResultKind::Idle:
                command = InstrumentCommand::Fade;
                break;

            case InstrumentResultKind::ChargePress:
            case InstrumentResultKind::AttackPress:
                if( result.quality > 0.0f )
                {
                    if( !portable || !secondary || kind == InstrumentResultKind::AttackPress )
                    {
                        command = InstrumentCommand::Recover;

                        provisional_result_ = -1;
                    }
                }
                else if( provisional_result_ == 0 )
                {
                    provisional_result_ = 1;
                }

                previous_note_ = result.note;
                break;

            case InstrumentResultKind::ChargeRelease:
            case InstrumentResultKind::AttackRelease:
            {
                const bool partner_tail =
                    portable && secondary && cooperative && kind == InstrumentResultKind::AttackRelease;

                if( !result.retained && !result.fade_locked && !partner_tail )
                {
                    command = InstrumentCommand::Fade;
                }

                if( ( !portable || !secondary ) && provisional_result_ == 1 && !result.fade_locked )
                {
                    command = InstrumentCommand::Fade;
                }

                if( !portable || !secondary )
                {
                    provisional_result_ = 0;
                }
                break;
            }

            case InstrumentResultKind::GuardPress:
                if( result.quality > 0.0f )
                {
                    if( !portable || !secondary || !cooperative )
                    {
                        provisional_result_ = -1;
                    }
                }
                else if( provisional_result_ == 0 )
                {
                    provisional_result_ = 1;
                }

                previous_note_ = result.note;

                command = InstrumentCommand::Fade;
                break;

            case InstrumentResultKind::GuardRelease:
                if( !portable || !secondary )
                {
                    provisional_result_ = 0;
                }

                command = InstrumentCommand::Fade;
                break;
        }

        previous_kind_ = kind;
        return command;
    }

    void StageInstrumentState::initialize( const filetypes::chc::Chart &chart, gamedata::ChartStyle style )
    {
        notes_.clear();
        guards_.clear();
        const bool portable = style == gamedata::ChartStyle::GitarooManLives;
        hit_window_ = portable ? psp_hit_window_samples : hit_window_samples;

        hold_padding_ = portable ? 0 : hold_padding_samples;

        chart_start_ = chart.start_sample;

        chart_end_ = chart_start_;

        for( const auto &fragment : chart.phrases )
        {
            notes_.push_back( Note{
                .start = chart_start_ + fragment.start_sample,
                .duration = fragment.duration_samples,
                .begins = fragment.phrase_start != 0,
                .ends = fragment.phrase_end != 0
            } );
        }

        // Shorten the release end before adding the hold window.
        for( std::size_t first = 0; first < notes_.size(); ++first )
        {
            if( !notes_[ first ].begins )
            {
                continue;
            }

            std::size_t last = first;

            while( last + 1 < notes_.size() && !notes_[ last ].ends && !notes_[ last + 1 ].begins )
            {
                ++last;
            }

            if( !notes_[ last ].ends )
            {
                continue;
            }

            std::int64_t remaining = release_trim_samples;
            std::size_t end = last;

            while( end > first && notes_[ end ].duration <= remaining )
            {
                remaining -= notes_[ end ].duration;

                notes_[ end ].duration = 0;

                notes_[ end ].ends = false;
                --end;
                notes_[ end ].ends = true;
            }

            if( notes_[ end ].duration > remaining )
            {
                notes_[ end ].duration -= remaining;
            }

            first = last;
        }

        std::size_t first = 0;
        std::int64_t duration = 0;

        for( std::size_t index = 0; index < notes_.size(); ++index )
        {
            auto &note = notes_[ index ];

            if( note.duration == 0 )
            {
                continue;
            }

            if( note.begins )
            {
                first = index;

                duration = 0;
            }

            duration += note.duration;

            chart_end_ = note.start + note.duration;

            if( note.ends )
            {
                for( std::size_t member = first; member <= index; ++member )
                {
                    notes_[ member ].group_start = notes_[ first ].start;

                    notes_[ member ].group_duration = duration + hold_padding_;
                }

                if( note.ends )
                {
                    note.duration += hold_padding_;
                }
            }
        }

        notes_.erase( std::remove_if( notes_.begin(),
                          notes_.end(),
                          []( const Note &note )
                          {
                              return note.duration == 0;
                          } ),
            notes_.end() );

        for( const auto &guard : chart.guards )
        {
            guards_.push_back( { chart_start_ + guard.start_sample, guard.button } );
        }

        reset();
    }

    void StageInstrumentState::reset()
    {
        cursor_ = 0;

        for( auto &note : notes_ )
        {
            note.state = NoteState::Dormant;
        }

        for( auto &guard : guards_ )
        {
            guard.responded = false;
        }
    }

    std::int64_t StageInstrumentState::end_section_sample() const
    {
        return chart_end_;
    }

    bool StageInstrumentState::holding() const
    {
        return std::any_of( notes_.begin(),
            notes_.end(),
            []( const Note &note )
            {
                return note.state == NoteState::Held;
            } );
    }

    InstrumentResult StageInstrumentState::update( std::int64_t sample,
        bool aligned,
        bool pressed,
        bool held,
        bool automatic,
        bool attack,
        const std::array<bool, 4> &guard_pressed )
    {
        InstrumentResult result;

        for( std::size_t index = 0; index < guards_.size(); ++index )
        {
            auto &guard = guards_[ index ];

            if( sample < guard.sample - hit_window_ || sample > guard.sample + hit_window_ )
            {
                continue;
            }

            result.kind = guard.responded ? InstrumentResultKind::GuardRelease : InstrumentResultKind::GuardPress;

            result.note = index + 1;

            if( !guard.responded && ( ( automatic && sample >= guard.sample ) ||
                                        ( guard.button < guard_pressed.size() && guard_pressed[ guard.button ] ) ) )
            {
                guard.responded = true;

                result.quality = automatic
                                     ? 1.0f
                                     : std::clamp( 1.0f - static_cast<float>( std::abs( sample - guard.sample ) ) /
                                                              static_cast<float>( hit_window_ ),
                                           0.0f,
                                           1.0f );
            }

            return result;
        }

        while( cursor_ < notes_.size() && notes_[ cursor_ ].start + notes_[ cursor_ ].duration < sample )
        {
            notes_[ cursor_ ].state = NoteState::Hidden;
            ++cursor_;
        }

        if( notes_.empty() || sample > chart_end_ || cursor_ == notes_.size() )
        {
            for( auto &note : notes_ )
            {
                note.state = NoteState::Hidden;
            }

            if( !guards_.empty() && sample >= guards_.front().sample - hit_window_ &&
                sample <= guards_.back().sample + hit_window_ )
            {
                result.kind = InstrumentResultKind::GuardRelease;
            }

            return result;
        }

        bool pending = false;
        bool held_previous = false;
        bool missed_previous = false;
        bool accepted_press = false;

        for( std::size_t index = cursor_; index < notes_.size(); ++index )
        {
            auto &note = notes_[ index ];

            if( note.state == NoteState::Dormant && sample >= note.start - approach_samples )
            {
                note.state = NoteState::Approaching;
            }

            const bool hittable = sample >= note.start - hit_window_ && sample <= note.start + hit_window_;

            if( note.state == NoteState::Approaching && hittable )
            {
                note.state = NoteState::Early;
            }

            if( note.state == NoteState::Early && sample >= note.start )
            {
                note.state = NoteState::Late;
            }

            if( note.state == NoteState::Late && sample > note.start + hit_window_ )
            {
                note.state = NoteState::Missed;
            }

            if( !note.begins )
            {
                if( held_previous && note.state != NoteState::Released && note.state != NoteState::Hidden )
                {
                    note.state = NoteState::Held;
                }
                else if( missed_previous && note.state != NoteState::Missed && note.state != NoteState::Hidden )
                {
                    note.state = NoteState::Missed;
                }
            }

            const bool awaiting = note.state == NoteState::Early || note.state == NoteState::Late;

            if( note.begins && awaiting && result.note == 0 )
            {
                pending = true;

                result.note = index + 1;
            }

            const bool automatic_press = automatic && sample >= note.start;

            if( aligned && note.begins && awaiting && !accepted_press && ( automatic_press || ( pressed && held ) ) )
            {
                const float distance = static_cast<float>( std::abs( sample - note.start ) );
                const float inverse_window = 1.0f / static_cast<float>( hit_window_ );
                const float quality = automatic ? 1.0f
                                                : static_cast<float>( 1.0 - static_cast<double>( distance ) *
                                                                                static_cast<double>( inverse_window ) );

                if( quality != 0.0f )
                {
                    note.state = automatic ? NoteState::Automatic : NoteState::Held;

                    result.quality = std::clamp( quality, 0.0f, 1.0f );

                    result.note = index + 1;

                    accepted_press = true;
                }
            }

            if( note.state == NoteState::Automatic )
            {
                note.state = NoteState::Held;
            }

            if( note.state == NoteState::Held &&
                ( !aligned || ( !automatic && !held ) ||
                    ( automatic && note.ends && sample >= note.start + note.duration - hold_padding_ ) ) )
            {
                note.state = NoteState::Released;

                if( !accepted_press )
                {
                    const auto elapsed = sample - note.group_start;
                    result.quality = 0.0f;

                    if( automatic )
                    {
                        result.quality = 1.0f;
                    }
                    else if( note.group_duration > 0 )
                    {
                        const float hold_fraction =
                            static_cast<float>( elapsed ) / static_cast<float>( note.group_duration );

                        result.quality = std::clamp( hold_fraction, 0.0f, 1.0f );
                    }

                    result.note = index + 1;
                }
            }

            result.retained = result.retained || note.state == NoteState::Held;

            if( !accepted_press && note.state == NoteState::Held )
            {
                result.note = index + 1;
            }

            held_previous = note.state == NoteState::Held;

            missed_previous = note.state == NoteState::Missed || note.state == NoteState::Hidden;

            if( note.start > sample + hit_window_ && note.begins )
            {
                break;
            }
        }

        if( sample < notes_.front().start - hit_window_ )
        {
            result.kind = InstrumentResultKind::Idle;
        }
        else if( attack )
        {
            result.kind = pending ? InstrumentResultKind::AttackPress : InstrumentResultKind::AttackRelease;
        }
        else
        {
            result.kind = pending ? InstrumentResultKind::ChargePress : InstrumentResultKind::ChargeRelease;
        }

        return result;
    }
}
