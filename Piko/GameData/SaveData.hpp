#pragma once

#include "GameData/GameDataRoot.hpp"
#include "Utils/EnumUtil.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>

namespace piko::utils::json
{
    class Value;
}

namespace piko::gamedata
{
    enum class SaveProgressMode
    {
        Easy,
        Normal,
        Master
    };

    struct SavedStageProgress
    {
        bool cleared = false;
        std::uint32_t best_score = 0;
        std::int32_t best_rank = 0;
        bool no_miss = false;
    };

    struct SavedChartProgress
    {
        std::array<SavedStageProgress, 100> easy{};
        std::array<SavedStageProgress, 100> normal{};
        std::array<SavedStageProgress, 100> master{};
    };

    struct SaveProfile
    {
        bool occupied = false;
        std::int32_t slot = 0;
        std::string name{};
        std::array<SavedChartProgress, utils::enum_index( ChartStyle::GitarooManLives ) + 1> charts{};

        [[nodiscard]] const SavedChartProgress &chart( ChartStyle style ) const
        {
            return charts[ utils::enum_index( style ) ];
        }
    };

    class SaveDataManager
    {
    public:
        static constexpr std::int32_t profile_count = 10;

        bool initialize( const std::filesystem::path &application_root );
        bool create_profile( std::int32_t slot );
        bool select_profile( std::int32_t slot );
        bool delete_profile( std::int32_t slot );
        bool record_stage_result( std::int32_t stage_number,
            SaveProgressMode mode,
            bool won,
            std::uint32_t score,
            std::int32_t rank,
            bool no_miss,
            ChartStyle chart_style = ChartStyle::GitarooMan );

        [[nodiscard]] const std::array<SaveProfile, profile_count> &profiles() const;
        [[nodiscard]] const SaveProfile *active_profile() const;
        [[nodiscard]] std::int32_t active_slot() const;
        [[nodiscard]] const SavedStageProgress *stage_progress( std::int32_t stage_number,
            SaveProgressMode mode,
            ChartStyle chart_style = ChartStyle::GitarooMan ) const;

        [[nodiscard]] std::int32_t clear_count( SaveProgressMode mode,
            std::int32_t maximum_stage = 99,
            ChartStyle chart_style = ChartStyle::GitarooMan ) const;

        [[nodiscard]] std::int32_t highest_reached_stage() const;
        [[nodiscard]] bool collection_unlocked( std::int32_t collection_index ) const;
        [[nodiscard]] const std::string &last_error() const;

    private:
        static constexpr std::array<const char *, 2> chart_names_{
            "ps2",
            "psp"
        };

        enum class CollectionUnlockCondition : std::int32_t
        {
            Always,
            ReachStage,
            ClearNormal,
            ClearMaster
        };

        struct CollectionRequirement
        {
            CollectionUnlockCondition condition = CollectionUnlockCondition::Always;
            std::int32_t stage = 0;
        };

        // Collection indices follow CHR_01 through CHR_28
        static constexpr std::array<CollectionRequirement, 28> collection_requirements_{
            CollectionRequirement{ CollectionUnlockCondition::ClearNormal, 10 },
            CollectionRequirement{ CollectionUnlockCondition::ClearMaster, 8 },
            CollectionRequirement{ CollectionUnlockCondition::ClearNormal, 2 },
            CollectionRequirement{ CollectionUnlockCondition::ClearNormal, 1 },
            CollectionRequirement{ CollectionUnlockCondition::ClearNormal, 6 },
            CollectionRequirement{ CollectionUnlockCondition::ClearMaster, 6 },
            CollectionRequirement{ CollectionUnlockCondition::Always, 0 },
            CollectionRequirement{ CollectionUnlockCondition::ReachStage, 5 },
            CollectionRequirement{ CollectionUnlockCondition::ClearMaster, 2 },
            CollectionRequirement{ CollectionUnlockCondition::ClearNormal, 5 },
            CollectionRequirement{ CollectionUnlockCondition::Always, 0 },
            CollectionRequirement{ CollectionUnlockCondition::ClearMaster, 4 },
            CollectionRequirement{ CollectionUnlockCondition::ClearNormal, 3 },
            CollectionRequirement{ CollectionUnlockCondition::Always, 0 },
            CollectionRequirement{ CollectionUnlockCondition::ClearNormal, 7 },
            CollectionRequirement{ CollectionUnlockCondition::ReachStage, 1 },
            CollectionRequirement{ CollectionUnlockCondition::ReachStage, 2 },
            CollectionRequirement{ CollectionUnlockCondition::ClearNormal, 4 },
            CollectionRequirement{ CollectionUnlockCondition::ReachStage, 3 },
            CollectionRequirement{ CollectionUnlockCondition::ReachStage, 9 },
            CollectionRequirement{ CollectionUnlockCondition::ClearMaster, 10 },
            CollectionRequirement{ CollectionUnlockCondition::ReachStage, 4 },
            CollectionRequirement{ CollectionUnlockCondition::ReachStage, 10 },
            CollectionRequirement{ CollectionUnlockCondition::ReachStage, 6 },
            CollectionRequirement{ CollectionUnlockCondition::ReachStage, 8 },
            CollectionRequirement{ CollectionUnlockCondition::ReachStage, 7 },
            CollectionRequirement{ CollectionUnlockCondition::ClearNormal, 9 },
            CollectionRequirement{ CollectionUnlockCondition::ClearNormal, 8 }
        };

        static void load_progress_array( const utils::json::Value *value, std::array<SavedStageProgress, 100> &output );
        static void write_progress_array( std::ostream &output, const std::array<SavedStageProgress, 100> &progress );
        [[nodiscard]] static std::int32_t rank_quality( std::int32_t rank );

        bool load();
        bool save();
        [[nodiscard]] bool valid_slot( std::int32_t slot ) const;
        [[nodiscard]] bool stage_reached( std::int32_t stage_number ) const;

        std::filesystem::path save_directory_{};
        std::filesystem::path save_path_{};
        std::array<SaveProfile, profile_count> profiles_{};
        std::int32_t active_slot_ = -1;
        std::string last_error_{};
    };
}
