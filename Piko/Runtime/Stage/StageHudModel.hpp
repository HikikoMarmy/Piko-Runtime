#pragma once

#include "Rendering/RenderTypes.hpp"
#include "Runtime/Stage/StageRuntime.hpp"

#include <array>
#include <cstdint>

namespace piko::runtime::stage
{
    enum class StageHudJudgement
    {
        None,
        Miss,
        Ok,
        Good,
        Great
    };

    struct StageHudJudgementAnimation
    {
        bool visible = false;
        float width_scale = 1.0f;
        float height_scale = 0.5f;
        float alpha = 0.8f;
    };

    struct StageHudSideModel
    {
        bool active = false;
        bool local = false;
        bool enemy = false;
        float health_ratio = 0.0f;
        float energy_ratio = 0.0f;
        float pending_energy_delta = 0.0f;
        std::int32_t pending_gain_ticks = 0;
        std::int32_t pending_loss_ticks = 0;
    };

    struct StageHudModel
    {
        std::uint32_t score = 0;
        StageActionMode phase = StageActionMode::Unknown;
        StageHudJudgement judgement = StageHudJudgement::None;
        std::int64_t judgement_age_samples = -1;
        std::array<StageHudSideModel, 4> sides{};
    };

    [[nodiscard]] StageHudModel build_stage_hud_model( const StageRuntime &runtime );
    [[nodiscard]] rendering::Rect retail_score_digit_source( std::int32_t digit );

    struct PhaseAnimation
    {
        float width_scale = 1.0f;
        float height_scale = 1.0f;
        float offset_y = 0.0f;
        float alpha = 1.0f;
    };

    struct PhaseAnnouncementAnimation
    {
        float offset_x = 0.0f;
        float alpha = 0.0f;
        bool visible = false;
    };

    struct PhaseAnnouncementState
    {
        StageActionMode phase = StageActionMode::Intro;
        float seconds = 99.0f;
    };

    void update_phase_announcement( PhaseAnnouncementState &state, StageActionMode phase, float delta_seconds );
    [[nodiscard]] std::int32_t phase_announcement_texture_index( StageActionMode phase );
    [[nodiscard]] PhaseAnnouncementAnimation phase_announcement_animation( float seconds );
    [[nodiscard]] PhaseAnimation retail_phase_animation( float seconds );
    rendering::Rect retail_phase_source( StageActionMode phase );
    [[nodiscard]] float retail_energy_warning_alpha( std::int64_t elapsed_ticks );
    [[nodiscard]] StageHudJudgementAnimation retail_judgement_animation( std::int64_t age_samples,
        std::int64_t samples_per_tick );

    [[nodiscard]] const char *stage_hud_judgement_text( StageHudJudgement judgement );
}
