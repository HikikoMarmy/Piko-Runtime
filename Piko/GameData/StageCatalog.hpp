#pragma once

#include "FileTypes/AlbumDef/AlbumDef.hpp"
#include "GameData/GameDataRoot.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace piko::gamedata
{
    enum class PlayMode
    {
        Tutorial,
        SinglePlayer,
        Multiplayer
    };

    enum class TutorialDifficulty
    {
        Easy,
        Normal
    };

    struct PlayableStageEntry
    {
        std::int32_t page_id = 0;
        PlayMode mode = PlayMode::SinglePlayer;
        TutorialDifficulty tutorial_difficulty = TutorialDifficulty::Normal;
        std::int32_t stage_number = -1;
        std::int32_t multiplayer_players = 1;
        std::filesystem::path chc_path;
        std::filesystem::path ssq_path;
        std::filesystem::path runtime_audio_path;
        std::filesystem::path runtime_model_root;

        float tempo = 0.0f;
        float animation_l_marker_frames = 150.0f;
        bool play_immediately = false;
        bool show_chart = false;
        bool show_sky = false;
        bool stereo_phrases = false;
        bool duet_stage = false;
        bool portable_visuals = false;
        ChartStyle chart_style = ChartStyle::GitarooMan;
        bool bonus_content = false;

        bool chc_exists = false;
        bool using_runtime_chart = false;
        bool ssq_exists = false;

        std::string chart_variant;
        std::string bonus_source;
        std::string display_name;
        std::string short_name;
        std::string detail_line;
    };

    [[nodiscard]] const char *play_mode_name( PlayMode mode );
    [[nodiscard]] std::string difficulty_from_chc_path( const std::filesystem::path &path );

    [[nodiscard]] std::vector<PlayableStageEntry> build_playable_stage_catalog(
        const filetypes::albumdef::AlbumDef &album,
        const GameDataRoot &root,
        ChartStyle chart_style = ChartStyle::GitarooMan );

    [[nodiscard]] std::vector<PlayableStageEntry> load_playable_stage_catalog( const GameDataRoot &root,
        ChartStyle chart_style = ChartStyle::GitarooMan );
}
