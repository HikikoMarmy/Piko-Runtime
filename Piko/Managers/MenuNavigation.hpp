#pragma once

#include "Utils/EnumUtil.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace piko::managers
{
    enum class MenuLocation : std::int32_t
    {
        Main,
        Options,
        Languages,
        Graphics,
        Audio,
        Controller,
        Interface,
        Aiming,
        VisionCone,
        CentreIcon,
        AimArrow,
        TraceLine,
        TraceNotes,
        Guard,
        Fonts,
        Modifiers,
        SnapAngles,
        StageEasy,
        StageNormal,
        StageMaster,
        StageMultiplayer,
        StageAll,
        Profiles,
        Collection,
        GameData,
        Count
    };

    struct MenuPosition
    {
        std::int32_t selected_row = 0;
        std::int32_t first_visible_row = 0;
    };

    class MenuNavigation final
    {
    public:
        void remember( MenuLocation location, std::int32_t selected_row, std::int32_t first_visible_row = 0 )
        {
            const auto index = utils::enum_index( location );
            positions_[ index ] = {
                selected_row,
                first_visible_row
            };

            remembered_[ index ] = true;
        }

        [[nodiscard]] MenuPosition restore( MenuLocation location,
            std::int32_t row_count,
            std::int32_t default_row = 0 ) const
        {
            const auto index = utils::enum_index( location );
            MenuPosition position = remembered_[ index ] ? positions_[ index ] : MenuPosition{ default_row, 0 };
            const std::int32_t last_row = std::max( 0, row_count - 1 );
            position.selected_row = std::clamp( position.selected_row, 0, last_row );

            position.first_visible_row = std::clamp( position.first_visible_row, 0, last_row );
            return position;
        }

    private:
        std::array<MenuPosition, utils::enum_index( MenuLocation::Count )> positions_{};
        std::array<bool, utils::enum_index( MenuLocation::Count )> remembered_{};
    };
}
