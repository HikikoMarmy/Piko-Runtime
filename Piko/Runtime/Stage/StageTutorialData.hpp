#pragma once

#include "Runtime/Stage/StageTutorialTimeline.hpp"

namespace piko::runtime::stage
{
    struct TutorialData
    {
        std::vector<TutorialSprite> sprites;
        std::vector<TutorialCentreFrame> centre;
        std::vector<TutorialInterval> pointer;
        std::vector<TutorialInterval> demonstration;
        std::vector<TutorialInterval> meter_display;
        std::vector<TutorialMeterEvent> meter_events;
    };

    [[nodiscard]] TutorialData make_tutorial_data( TutorialRegion region );
}
