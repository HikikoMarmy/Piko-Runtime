#pragma once

#include "Rendering/TrueTypeText.hpp"
#include "GameData/StageCatalog.hpp"
#include "Managers/InterfaceStyle.hpp"

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace piko::localization
{
    struct LanguageInfo
    {
        std::string id;
        std::string name;
    };

    struct LabelStyle
    {
        rendering::StrokedTextStyle stroke{};
        float height = 24.0f;
    };

    struct FontFile
    {
        std::string path;
        std::string name;
    };

    // Translate @keys; leave other text alone
    bool load( const std::filesystem::path &application_root, std::string_view id, std::string &error );
    [[nodiscard]] std::string normalize_language_id( std::string_view id );
    [[nodiscard]] const std::vector<std::filesystem::path> &texture_roots();
    [[nodiscard]] const char *text( const char *key );
    [[nodiscard]] std::string translate( std::string_view key );
    [[nodiscard]] std::string format( const char *key,
        std::initializer_list<std::pair<std::string_view, std::string>> values );

    [[nodiscard]] const std::vector<LanguageInfo> &languages();
    [[nodiscard]] const std::string &language_id();
    [[nodiscard]] bool right_to_left();
    [[nodiscard]] std::uint32_t revision();
    [[nodiscard]] std::string texture_key( std::string_view name );
    [[nodiscard]] std::filesystem::path font_path( const std::filesystem::path &original = {} );
    [[nodiscard]] std::filesystem::path default_font_path( const std::filesystem::path &original = {} );
    [[nodiscard]] std::filesystem::path label_font_path( std::string_view style );
    bool load_menu_font( rendering::TrueTypeFont &font,
        const std::filesystem::path &original = {},
        std::string *error = nullptr );

    void add_menu_font_fallbacks( rendering::TrueTypeFont &font );
    [[nodiscard]] std::vector<FontFile> font_files();
    void set_interface_style( const managers::InterfaceStyle &style );
    [[nodiscard]] std::filesystem::path asset_path( const std::filesystem::path &relative );
    [[nodiscard]] LabelStyle label_style( std::string_view name );
    [[nodiscard]] std::string stage_name( const gamedata::PlayableStageEntry &stage );
    [[nodiscard]] const char *difficulty_name( std::string_view difficulty );
}
