#pragma once

#include <cstdint>
#include <array>
#include <cstddef>
#include <string>

namespace piko::runtime::stage
{
    enum class StageSideRole
    {
        Inactive,
        Player,
        Enemy,
        MultiplayerPlayer
    };

    struct StageSideState
    {
        std::int32_t slot = 0;
        StageSideRole role = StageSideRole::Inactive;
        std::string label;

        bool active = false;
        bool local = false;

        float health = 1.0f;
        float energy = 1.0f;

        float logical_health = 1.0f;
        float logical_energy = 1.0f;

        float max_health = 1.0f;
        float max_energy = 1.0f;

        float pending_energy_delta = 0.0f;
        std::int32_t pending_gain_ticks = 0;
        std::int32_t pending_loss_ticks = 0;

        [[nodiscard]] float health_ratio() const;
        [[nodiscard]] float energy_ratio() const;
    };

    using StageSideArray = std::array<StageSideState, 4>;

    [[nodiscard]] const char *stage_side_role_name( StageSideRole role );

    [[nodiscard]] StageSideArray make_single_player_sides();
    [[nodiscard]] StageSideArray make_multiplayer_sides( std::int32_t player_count );
    [[nodiscard]] std::size_t active_side_count( const StageSideArray &sides );
}
