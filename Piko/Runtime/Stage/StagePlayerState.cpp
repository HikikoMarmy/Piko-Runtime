#include "StagePlayerState.hpp"

#include "Utils/MathUtil.hpp"

#include <cstdint>
#include <algorithm>

namespace piko::runtime::stage
{
    float StageSideState::health_ratio() const
    {
        return piko::utils::ratio01( health, max_health );
    }

    float StageSideState::energy_ratio() const
    {
        return piko::utils::ratio01( energy, max_energy );
    }

    const char *stage_side_role_name( StageSideRole role )
    {
        switch( role )
        {
            case StageSideRole::Inactive:
                return "Inactive";
            case StageSideRole::Player:
                return "Player";
            case StageSideRole::Enemy:
                return "Enemy";
            case StageSideRole::MultiplayerPlayer:
                return "Multiplayer Player";
            default:
                return "Unknown";
        }
    }

    StageSideArray make_single_player_sides()
    {
        StageSideArray sides{};

        sides[ 0 ] = StageSideState{
            .slot = 0,
            .role = StageSideRole::Player,
            .label = "PLAYER",
            .active = true,
            .local = true,
            .health = 1.0f,
            .energy = 1.0f,
            .max_health = 1.0f,
            .max_energy = 1.0f
        };

        sides[ 1 ] = StageSideState{
            .slot = 1,
            .role = StageSideRole::Enemy,
            .label = "ENEMY",
            .active = true,
            .local = false,
            .health = 1.0f,
            .energy = 1.0f,
            .max_health = 1.0f,
            .max_energy = 1.0f
        };

        for( std::int32_t i = 2; i < 4; ++i )
        {
            sides[ static_cast<std::size_t>( i ) ] = StageSideState{
                .slot = i,
                .role = StageSideRole::Inactive,
                .label = "P" + std::to_string( i + 1 ),
                .active = false,
                .local = false,
                .health = 0.0f,
                .energy = 0.0f,
                .max_health = 1.0f,
                .max_energy = 1.0f
            };
        }

        return sides;
    }

    StageSideArray make_multiplayer_sides( std::int32_t player_count )
    {
        player_count = std::clamp( player_count, 1, 4 );

        StageSideArray sides{};

        for( std::int32_t i = 0; i < 4; ++i )
        {
            const bool active = i < player_count;

            sides[ static_cast<std::size_t>( i ) ] = StageSideState{
                .slot = i,
                .role = active ? StageSideRole::MultiplayerPlayer : StageSideRole::Inactive,
                .label = "P" + std::to_string( i + 1 ),
                .active = active,
                .local = i == 0,
                .health = active ? 1.0f : 0.0f,
                .energy = active ? 1.0f : 0.0f,
                .max_health = 1.0f,
                .max_energy = 1.0f
            };
        }

        return sides;
    }

    std::size_t active_side_count( const StageSideArray &sides )
    {
        std::size_t count = 0;

        for( const StageSideState &side : sides )
        {
            if( side.active )
            {
                ++count;
            }
        }

        return count;
    }
}
