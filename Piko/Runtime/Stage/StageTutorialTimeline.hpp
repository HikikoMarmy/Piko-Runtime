#pragma once

#include "Rendering/RenderTypes.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace piko::runtime::stage
{
    enum class TutorialRegion : std::int32_t
    {
        Us,
        Japanese
    };

    enum class TutorialSpriteKind
    {
        SpeechBubble,
        Instruction,
        Sprite,
        AnimatedSprite,
        SlidingSprite
    };

    struct TutorialInterval
    {
        std::int64_t start = 0;
        std::int64_t end = 0;
    };

    struct TutorialRectangleFrame
    {
        TutorialInterval interval;
        rendering::Rect source;
    };

    struct TutorialPositionFrame
    {
        TutorialInterval interval;
        rendering::Vec2 from;
        rendering::Vec2 to;
    };

    struct TutorialCentreFrame
    {
        TutorialInterval interval;
        rendering::Vec2 offset;
    };

    struct TutorialMeterEvent
    {
        std::int64_t sample = 0;
        float value = 0.0f;
    };

    struct TutorialSprite
    {
        TutorialSpriteKind kind = TutorialSpriteKind::Sprite;
        std::string texture;
        TutorialInterval interval;
        rendering::Rect source;
        rendering::Vec2 position;
        rendering::Vec2 size;
        rendering::Vec2 origin;
        rendering::Color colour;
        float scale = 1.0f;
        rendering::Vec2 slide_from;
        rendering::Vec2 slide_to;
        std::int32_t linked_sprite = -1;
        std::vector<TutorialRectangleFrame> frames;
        std::vector<TutorialPositionFrame> motion;
    };

    enum class TutorialAnimationPhase
    {
        Entering,
        Holding,
        Leaving,
        Finished
    };

    struct TutorialSpriteState
    {
        rendering::Rect source;
        rendering::Vec2 position;
        rendering::Color colour;
        bool visible = false;
        TutorialAnimationPhase animation_phase = TutorialAnimationPhase::Entering;
        float slide_progress = 1.0f;
        float slide_step = 0.01f;
    };

    class StageTutorialTimeline
    {
    public:
        void initialize( TutorialRegion region );
        void reset();
        void update( std::int64_t sample );

        [[nodiscard]] bool valid() const;
        [[nodiscard]] bool automatic_notes() const;
        [[nodiscard]] bool pointer_active() const;
        [[nodiscard]] bool aim_visible() const;
        [[nodiscard]] rendering::Vec2 centre_offset() const;
        [[nodiscard]] bool example_meter_visible() const;
        [[nodiscard]] float example_meter_value() const;
        [[nodiscard]] const std::vector<TutorialSprite> &sprites() const;
        [[nodiscard]] const std::vector<TutorialSpriteState> &states() const;
        [[nodiscard]] std::int64_t sample() const;

    private:
        static constexpr std::int64_t sample_rate_ = 48000;
        static constexpr std::int64_t maximum_sample_ = sample_rate_ * 600;
        static constexpr std::int64_t samples_per_tick_ = sample_rate_ / 60;
        static constexpr std::int64_t transition_lead_ = 4800;
        static constexpr float bubble_alpha_step_ = 0.1f;
        static constexpr float slide_acceleration_ = 0.01f;
        static constexpr float reference_width_ = 640.0f;
        static constexpr float reference_height_ = 448.0f;

        [[nodiscard]] static bool contains( const TutorialInterval &interval, std::int64_t sample );
        [[nodiscard]] bool in_intervals( const std::vector<TutorialInterval> &intervals ) const;
        void tick( std::int64_t sample );

        std::vector<TutorialSprite> sprites_;
        std::vector<TutorialSpriteState> states_;
        std::vector<TutorialCentreFrame> centre_;
        std::vector<TutorialInterval> pointer_;
        std::vector<TutorialInterval> automatic_;
        std::vector<TutorialInterval> meter_display_;
        std::vector<TutorialMeterEvent> meter_events_;
        std::size_t centre_cursor_ = 0;
        std::size_t meter_cursor_ = 0;
        std::int64_t sample_ = 0;
        std::int64_t last_tick_ = -1;
        float meter_value_ = 0.0f;
        bool initialized_ = false;
    };
}
