#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace piko::gamedata
{
    enum class GameRegion
    {
        Unknown,
        Japan,
        Usa,
        Europe
    };

    enum class GameLanguage
    {
        Default,
        Japanese,
        English,
        French,
        German
    };

    enum class ChartStyle
    {
        GitarooMan,
        GitarooManLives
    };

    class GameDataRoot
    {
    public:
        GameDataRoot() = default;

        bool initialize( const std::filesystem::path &application_root,
            GameLanguage requested_language = GameLanguage::Default );

        void clear();
        void set_texture_roots( const std::vector<std::filesystem::path> &roots );

        [[nodiscard]] bool valid() const;
        [[nodiscard]] bool native_runtime_available() const;
        [[nodiscard]] std::filesystem::path runtime_root() const;
        [[nodiscard]] std::filesystem::path native_runtime_root() const;

        [[nodiscard]] const std::filesystem::path &application_root() const;
        [[nodiscard]] const std::filesystem::path &game_data_root() const;
        [[nodiscard]] std::filesystem::path resource_root() const;
        [[nodiscard]] std::filesystem::path piko_data_root() const;

        [[nodiscard]] std::filesystem::path albumdef_path() const;
        [[nodiscard]] std::filesystem::path resolve_resource_path( std::string_view relative_path ) const;
        [[nodiscard]] std::filesystem::path resolve_piko_path( std::string_view relative_path ) const;
        [[nodiscard]] std::filesystem::path resolve_runtime_asset( std::string_view relative_path ) const;
        [[nodiscard]] std::filesystem::path resolve_runtime_movie( std::string_view runtime_path ) const;

        [[nodiscard]] std::vector<std::filesystem::path> runtime_stage_charts( std::int32_t stage_number,
            ChartStyle chart_style = ChartStyle::GitarooMan ) const;

        [[nodiscard]] bool chart_style_available( ChartStyle chart_style ) const;
        [[nodiscard]] ChartStyle resolve_chart_style( ChartStyle preferred ) const;

        [[nodiscard]] std::filesystem::path runtime_stage_audio( std::int32_t stage_number,
            ChartStyle chart_style = ChartStyle::GitarooMan,
            std::string_view chart_filename = {} ) const;

        [[nodiscard]] std::filesystem::path runtime_stage_models( std::int32_t stage_number ) const;
        [[nodiscard]] std::filesystem::path runtime_stage_sequence( std::int32_t stage_number,
            std::string_view variant ) const;

        [[nodiscard]] std::filesystem::path runtime_ui_asset( std::string_view category,
            std::string_view filename ) const;

        [[nodiscard]] std::filesystem::path runtime_stage_texture( std::int32_t stage_number,
            std::string_view category,
            std::string_view filename ) const;

        [[nodiscard]] GameRegion region() const;
        [[nodiscard]] GameLanguage language() const;
        [[nodiscard]] std::vector<GameLanguage> available_languages() const;

        [[nodiscard]] std::string region_name() const;
        [[nodiscard]] std::string language_name() const;
        [[nodiscard]] std::filesystem::path main_menu_audio_path() const;

    private:
        void detect_region_and_language( GameLanguage requested_language );

        [[nodiscard]] bool has_runtime_locale( std::string_view locale ) const;
        [[nodiscard]] std::vector<std::string> installed_runtime_locales() const;
        static std::filesystem::path normalize_relative_asset_path( std::string_view relative_path );
        [[nodiscard]] std::string runtime_locale_name() const;
        [[nodiscard]] std::vector<std::filesystem::path> runtime_search_roots() const;
        [[nodiscard]] std::vector<std::filesystem::path> texture_search_roots() const;
        static GameRegion region_for_locale( std::string_view locale );
        static GameLanguage language_for_locale( std::string_view locale );
        static const char *region_to_string( GameRegion region );
        static const char *language_to_string( GameLanguage language );

    private:
        bool valid_ = false;

        std::filesystem::path application_root_{};
        std::filesystem::path game_data_root_{};

        GameRegion region_ = GameRegion::Unknown;
        GameLanguage language_ = GameLanguage::Default;
        std::string active_locale_{};
        std::vector<std::filesystem::path> texture_roots_{};
    };
}
