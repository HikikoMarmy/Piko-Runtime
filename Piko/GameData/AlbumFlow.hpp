#pragma once

#include "GameData/GameDataRoot.hpp"
#include "GameData/StageCatalog.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace piko::gamedata
{
    enum class AlbumFlowTargetType
    {
        None,
        Stage,
        Movie,
        MainMenu
    };

    struct AlbumFlowTarget
    {
        AlbumFlowTargetType type = AlbumFlowTargetType::None;
        std::int32_t page_id = -1;
        PlayableStageEntry stage{};
        std::filesystem::path movie_path{};
    };

    [[nodiscard]] std::optional<std::int32_t> album_return_page( const GameDataRoot &root,
        std::int32_t page_id,
        std::int32_t return_code );

    [[nodiscard]] AlbumFlowTarget resolve_album_flow_target( const GameDataRoot &root,
        std::int32_t page_id,
        ChartStyle chart_style = ChartStyle::GitarooMan,
        bool allow_movies = true );

    [[nodiscard]] std::optional<std::int32_t> album_intro_movie_page( const GameDataRoot &root,
        std::int32_t stage_page_id );

    [[nodiscard]] std::optional<std::int32_t> album_opening_movie_page( const GameDataRoot &root );
}
