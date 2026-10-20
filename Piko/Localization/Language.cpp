#include "Language.hpp"

#include "Utils/Json.hpp"
#include "Utils/EnumUtil.hpp"
#include "Utils/FileUtil.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <SDL3/SDL_stdinc.h>
#include <stdexcept>
#include <unordered_map>

namespace piko::localization
{
    namespace
    {
        using utils::json::Value;

        std::filesystem::path resource_root;
        std::filesystem::path game_data_root;
        std::vector<std::filesystem::path> language_roots;
        std::unordered_map<std::string, std::string> strings;
        std::vector<LanguageInfo> available_languages;
        Value fonts;
        Value styles;
        managers::InterfaceStyle interface_style;
        std::string active_id = "EN";
        bool active_right_to_left = false;
        std::uint32_t catalog_revision = 0;

        bool valid_id( std::string_view id )
        {
            return !id.empty() && id.size() <= 32 &&
                   std::all_of( id.begin(),
                       id.end(),
                       []( unsigned char ch )
                       {
                           return std::isalnum( ch ) || ch == '-' || ch == '_';
                       } );
        }

        void read_strings( const Value &catalog, std::unordered_map<std::string, std::string> &destination )
        {
            const Value *entries = catalog.find( "strings" );

            if( entries == nullptr )
            {
                return;
            }

            if( !entries->is_object() )
            {
                throw std::runtime_error( "Language strings must be an object" );
            }

            for( const auto &[ key, entry ] : entries->object() )
            {
                if( key.empty() || key.front() != '@' || !entry.is_string() )
                {
                    continue;
                }

                destination[ key ] = entry.string();
            }
        }

        rendering::Color read_color( const Value &object, const char *key, rendering::Color fallback )
        {
            const Value *entry = object.find( key );

            if( entry == nullptr || !entry->is_array() || entry->array().size() != 4 )
            {
                return fallback;
            }

            const auto &values = entry->array();
            return {
                std::clamp( static_cast<float>( values[ 0 ].number_or( fallback.r ) ), 0.0f, 1.0f ),
                std::clamp( static_cast<float>( values[ 1 ].number_or( fallback.g ) ), 0.0f, 1.0f ),
                std::clamp( static_cast<float>( values[ 2 ].number_or( fallback.b ) ), 0.0f, 1.0f ),
                std::clamp( static_cast<float>( values[ 3 ].number_or( fallback.a ) ), 0.0f, 1.0f )
            };
        }
    }

    std::string normalize_language_id( std::string_view id )
    {
        std::string result( id );
        std::transform( result.begin(),
            result.end(),
            result.begin(),
            []( unsigned char ch )
            {
                return static_cast<char>( std::toupper( ch ) );
            } );

        return result;
    }

    const std::vector<std::filesystem::path> &texture_roots()
    {
        return language_roots;
    }

    bool load( const std::filesystem::path &application_root, std::string_view id, std::string &error )
    {
        try
        {
            if( !valid_id( id ) || normalize_language_id( id ) == "PIKO" )
            {
                throw std::runtime_error( "Invalid UI language id" );
            }

            const std::string next_id = normalize_language_id( id );

            const auto root = application_root / "GAME_DATA" / "PIKO";

            const auto directory = application_root / "GAME_DATA";
            const Value english = utils::json::load_file( directory / "EN" / "language.json" );
            std::unordered_map<std::string, std::string> next_strings;
            read_strings( english, next_strings );

            Value selected = english;

            if( next_id != "EN" )
            {
                selected = utils::json::load_file( directory / next_id / "language.json" );
            }

            std::vector<std::filesystem::path> next_roots{
                directory / next_id
            };

            std::vector<Value> fallback_catalogs;
            Value fallback_catalog = selected;
            std::string fallback_id = next_id;

            while( fallback_id != "EN" )
            {
                const auto *fallback = fallback_catalog.find( "fallback" );
                fallback_id = normalize_language_id( fallback == nullptr ? "EN" : fallback->string() );

                if( !valid_id( fallback_id ) || fallback_id == "PIKO" )
                {
                    throw std::runtime_error( "Invalid language fallback" );
                }

                const auto fallback_root = directory / fallback_id;

                if( std::find( next_roots.begin(), next_roots.end(), fallback_root ) != next_roots.end() )
                {
                    throw std::runtime_error( "Language fallback contains a loop" );
                }

                next_roots.push_back( fallback_root );

                const auto fallback_path = fallback_root / "language.json";

                if( !std::filesystem::is_regular_file( fallback_path ) )
                {
                    fallback_id = "EN";
                    next_roots.push_back( directory / "EN" );
                    break;
                }

                fallback_catalog = utils::json::load_file( fallback_path );
                fallback_catalogs.push_back( fallback_catalog );
            }

            for( auto entry = fallback_catalogs.rbegin(); entry != fallback_catalogs.rend(); ++entry )
            {
                read_strings( *entry, next_strings );
            }

            read_strings( selected, next_strings );

            bool next_right_to_left = false;
            const auto read_direction = [ &next_right_to_left ]( const Value &catalog )
            {
                if( const Value *direction = catalog.find( "direction" ); direction != nullptr )
                {
                    if( !direction->is_string() ||
                        ( direction->string() != "ltr" && direction->string() != "rtl" ) )
                    {
                        throw std::runtime_error( "Language direction must be 'ltr' or 'rtl'" );
                    }
                    next_right_to_left = direction->string() == "rtl";
                }
            };

            read_direction( english );
            for( auto entry = fallback_catalogs.rbegin(); entry != fallback_catalogs.rend(); ++entry )
            {
                read_direction( *entry );
            }
            read_direction( selected );

            std::vector<LanguageInfo> next_languages;

            for( const auto &file : std::filesystem::directory_iterator( directory ) )
            {
                if( !file.is_directory() || file.path().filename() == "PIKO" ||
                    !valid_id( utils::path_to_utf8( file.path().filename() ) ) )
                {
                    continue;
                }

                try
                {
                    const Value catalog = utils::json::load_file( file.path() / "language.json" );

                    if( const Value *name = catalog.find( "name" ); name != nullptr && name->is_string() )
                    {
                        next_languages.push_back(
                            { normalize_language_id( utils::path_to_utf8( file.path().filename() ) ),
                                name->string() } );
                    }
                }
                catch( const std::exception & )
                {
                    // Skip broken optional packs
                }
            }

            std::sort( next_languages.begin(),
                next_languages.end(),
                []( const LanguageInfo &left, const LanguageInfo &right )
                {
                    if( left.id == "EN" || right.id == "EN" )
                    {
                        return left.id == "EN" && right.id != "EN";
                    }

                    return left.id < right.id;
                } );

            Value next_fonts = Value::make_object( {} );

            if( const Value *defaults = english.find( "fonts" ); defaults != nullptr )
            {
                if( !defaults->is_object() )
                {
                    throw std::runtime_error( "Language fonts must be an object" );
                }

                next_fonts = *defaults;
            }

            std::vector<Value> overrides_in_order( fallback_catalogs.rbegin(), fallback_catalogs.rend() );
            overrides_in_order.push_back( selected );

            for( const auto &overriding_catalog : overrides_in_order )
            {

                if( const Value *overrides = overriding_catalog.find( "fonts" ); overrides != nullptr )
                {
                    if( !overrides->is_object() )
                    {
                        throw std::runtime_error( "Language fonts must be an object" );
                    }

                    auto merged = next_fonts.object();

                    for( const auto &[ name, value ] : overrides->object() )
                    {
                        merged[ name ] = value;
                    }

                    if( overrides->find( "display" ) == nullptr )
                    {
                        const Value *display = overrides->find( "body_bold" );

                        if( display == nullptr )
                        {
                            display = overrides->find( "body" );
                        }

                        if( display != nullptr )
                        {
                            merged[ "display" ] = *display;
                        }
                    }

                    next_fonts = Value::make_object( std::move( merged ) );
                }
            }

            Value next_styles = english.require( "styles" );

            if( !next_fonts.is_object() || !next_styles.is_object() )
            {
                throw std::runtime_error( "Language fonts and styles must be objects" );
            }

            for( const auto &overriding_catalog : overrides_in_order )
            {
                if( const Value *overrides = overriding_catalog.find( "styles" );
                    overrides != nullptr && overrides->is_object() )
                {
                    auto merged = next_styles.object();

                    for( const auto &[ name, style ] : overrides->object() )
                    {
                        if( !style.is_object() )
                        {
                            continue;
                        }

                        auto properties = merged.contains( name ) ? merged.at( name ).object() : Value::Object{};

                        for( const auto &[ property, value ] : style.object() )
                        {
                            properties[ property ] = value;
                        }

                        merged[ name ] = Value::make_object( std::move( properties ) );
                    }

                    next_styles = Value::make_object( std::move( merged ) );
                }
            }

            resource_root = root;

            game_data_root = directory;

            language_roots = std::move( next_roots );

            strings = std::move( next_strings );

            available_languages = std::move( next_languages );

            fonts = std::move( next_fonts );

            styles = std::move( next_styles );

            active_id = next_id;
            active_right_to_left = next_right_to_left;

            interface_style = {};
            ++catalog_revision;
            error.clear();
            return true;
        }
        catch( const std::exception &exception )
        {
            error = exception.what();
            return false;
        }
    }

    const char *text( const char *key )
    {
        if( key == nullptr || key[ 0 ] != '@' )
        {
            return key;
        }

        const auto found = strings.find( std::string( key ) );
        return found != strings.end() ? found->second.c_str() : key;
    }

    std::string translate( std::string_view key )
    {
        const std::string owned( key );
        return text( owned.c_str() );
    }

    std::string format( const char *key, std::initializer_list<std::pair<std::string_view, std::string>> values )
    {
        const std::string pattern = text( key );
        std::string output;

        for( std::size_t i = 0; i < pattern.size(); )
        {
            if( pattern[ i ] == '{' )
            {
                const auto end = pattern.find( '}', i + 1 );

                if( end != std::string::npos )
                {
                    const std::string_view name( pattern.data() + i + 1, end - i - 1 );

                    const auto entry = std::find_if( values.begin(),
                        values.end(),
                        [ name ]( const auto &value )
                        {
                            return value.first == name;
                        } );

                    if( entry != values.end() )
                    {
                        output += entry->second;

                        i = end + 1;
                        continue;
                    }
                }
            }

            output += pattern[ i++ ];
        }

        return output;
    }

    const std::vector<LanguageInfo> &languages()
    {
        return available_languages;
    }

    const std::string &language_id()
    {
        return active_id;
    }

    bool right_to_left()
    {
        return active_right_to_left;
    }

    std::uint32_t revision()
    {
        return catalog_revision;
    }

    std::string texture_key( std::string_view name )
    {
        return std::string( name ) + "/ui-" + std::to_string( catalog_revision );
    }

    namespace
    {
        std::filesystem::path selected_font( managers::InterfaceFont role )
        {
            const auto relative = utils::path_from_utf8( interface_style.fonts[ utils::enum_index( role ) ] );

            if( relative.empty() || relative.has_root_path() )
            {
                return {};
            }

            for( const auto &part : relative )
            {
                if( part == ".." )
                {
                    return {};
                }
            }

            auto relative_path = relative;

            if( relative_path.begin()->string() == "fonts" )
            {
                relative_path = std::filesystem::path( "PIKO" ) / relative_path;
            }

            const auto candidate = game_data_root / relative_path;
            std::error_code error;
            return std::filesystem::is_regular_file( candidate, error ) ? candidate : std::filesystem::path{};
        }
    }

    void set_interface_style( const managers::InterfaceStyle &style )
    {
        if( interface_style != style )
        {
            interface_style = style;
            ++catalog_revision;
        }
    }

    std::vector<FontFile> font_files()
    {
        std::vector<FontFile> files;
        std::vector<std::filesystem::path> directories{
            resource_root / "fonts"
        };

        for( const auto &root : language_roots )
        {
            directories.push_back( root / "fonts" );
        }

        for( const auto &directory : directories )
        {
            std::error_code error;
            auto iterator = std::filesystem::recursive_directory_iterator( directory,
                std::filesystem::directory_options::skip_permission_denied,
                error );

            for( const auto end = std::filesystem::recursive_directory_iterator{}; iterator != end;
                iterator.increment( error ) )
            {
                if( error )
                {
                    error.clear();
                    continue;
                }

                const auto &entry = *iterator;
                std::string extension = utils::path_to_utf8( entry.path().extension() );
                std::transform( extension.begin(),
                    extension.end(),
                    extension.begin(),
                    []( unsigned char ch )
                    {
                        return static_cast<char>( std::tolower( ch ) );
                    } );

                if( !entry.is_regular_file( error ) ||
                    ( extension != ".ttf" && extension != ".otf" && extension != ".ttc" ) )
                {
                    continue;
                }

                const std::string folder = directory == resource_root / "fonts"
                                               ? text( "@ui.interface.shared" )
                                               : utils::path_to_utf8( directory.parent_path().filename() );

                files.push_back(
                    { utils::path_to_utf8( entry.path().lexically_relative( game_data_root ) ),
                        utils::path_to_utf8( entry.path().filename() ) + " (" + folder + ")" } );
            }
        }

        std::sort( files.begin(),
            files.end(),
            []( const FontFile &left, const FontFile &right )
            {
                return left.name < right.name;
            } );
        return files;
    }

    std::filesystem::path label_font_path( std::string_view style )
    {
        (void)style;

        const auto role = managers::InterfaceFont::Menu;

        const auto chosen = selected_font( role );
        return chosen.empty() ? font_path() : chosen;
    }

    void add_menu_font_fallbacks( rendering::TrueTypeFont &font )
    {
        const std::array candidates{
            default_font_path(),
            default_font_path( resource_root / "fonts/noto-sans.bold.ttf" ),
            resource_root / "fonts/noto-sans.bold.ttf",
            resource_root / "fonts/noto-sans.regular.ttf"
        };

        for( const auto &path : candidates )
        {
            if( !path.empty() )
            {
                font.add_fallback( path );
            }
        }
    }

    bool load_menu_font( rendering::TrueTypeFont &font, const std::filesystem::path &original, std::string *error )
    {
        font.load( font_path( original ), error );
        add_menu_font_fallbacks( font );

        if( font.loaded() )
        {
            if( error != nullptr )
            {
                error->clear();
            }

            return true;
        }

        return false;
    }

    std::filesystem::path font_path( const std::filesystem::path &original )
    {
        if( const auto chosen = selected_font( managers::InterfaceFont::Menu ); !chosen.empty() )
        {
            return chosen;
        }

        return default_font_path( original );
    }

    std::filesystem::path default_font_path( const std::filesystem::path &original )
    {
        const std::string original_filename = utils::path_to_utf8( original.filename() );
        const bool body = original_filename.find( "noto-sans" ) != std::string::npos;
        const bool bold = original_filename.find( "bold" ) != std::string::npos;
        const char *role = "display";

        if( body )
        {
            role = bold ? "body_bold" : "body";
        }

        const Value *preferred = fonts.find( role );

        if( preferred == nullptr && body )
        {
            preferred = fonts.find( "body" );
        }

        if( preferred != nullptr && preferred->is_string() )
        {
            const auto filename = utils::path_from_utf8( preferred->string() );

            if( !filename.has_root_path() && std::none_of( filename.begin(),
                                                 filename.end(),
                                                 []( const auto &part )
                                                 {
                                                     return part == "..";
                                                 } ) )
            {
                const auto relative =
                    filename.has_parent_path() ? filename : std::filesystem::path( "fonts" ) / filename;

                std::vector<std::filesystem::path> candidates;

                for( const auto &root : language_roots )
                {
                    candidates.push_back( root / relative );
                }

                candidates.push_back( resource_root / relative );

                for( const auto &candidate : candidates )
                {
                    if( std::filesystem::is_regular_file( candidate ) )
                    {
                        return candidate;
                    }
                }
            }
        }

        if( const Value *fallbacks = fonts.find( "windows_fallbacks" ); fallbacks != nullptr && fallbacks->is_array() )
        {
            const char *windows = SDL_getenv( "WINDIR" );

            if( windows == nullptr )
            {
                windows = SDL_getenv( "windir" );
            }

            if( windows != nullptr )
            {
                for( const Value &entry : fallbacks->array() )
                {
                    const std::filesystem::path filename( entry.string_or() );

                    if( filename.empty() || filename.has_parent_path() )
                    {
                        continue;
                    }

                    const auto candidate = std::filesystem::path( windows ) / "Fonts" / filename;

                    if( std::filesystem::is_regular_file( candidate ) )
                    {
                        return candidate;
                    }
                }
            }
        }

        if( !original.empty() && std::filesystem::is_regular_file( original ) )
        {
            return original;
        }

        const char *fallback_filename = "Anak Paud.ttf";

        if( body )
        {
            fallback_filename = bold ? "noto-sans.bold.ttf" : "noto-sans.regular.ttf";
        }

        const std::array<const char *, 3> fallbacks{
            fallback_filename,
            "noto-sans.bold.ttf",
            "noto-sans.regular.ttf"
        };

        for( const char *filename : fallbacks )
        {
            const auto candidate = resource_root / "fonts" / filename;

            if( std::filesystem::is_regular_file( candidate ) )
            {
                return candidate;
            }
        }

        return {};
    }

    std::filesystem::path asset_path( const std::filesystem::path &relative )
    {
        if( relative.empty() || relative.has_root_path() ||
            std::any_of( relative.begin(),
                relative.end(),
                []( const auto &part )
                {
                    return part == "..";
                } ) )
        {
            return {};
        }

        for( const auto &root : language_roots )
        {
            const auto candidate = root / relative;

            if( std::filesystem::is_regular_file( candidate ) )
            {
                return candidate;
            }
        }

        return {};
    }

    LabelStyle label_style( std::string_view name )
    {
        LabelStyle style{};
        style.stroke.fill = { 1.0f, 1.0f, 1.0f, 1.0f };

        style.stroke.inner_border = { 0.12f, 0.14f, 0.35f, 1.0f };

        style.stroke.outer_border = { 0.0f, 0.0f, 0.0f, 0.8f };

        if( const Value *entry = styles.find( name ); entry != nullptr && entry->is_object() )
        {
            style.stroke.fill = read_color( *entry, "fill", style.stroke.fill );

            if( const Value *palette = entry->find( "palette" ); palette != nullptr && palette->is_array() )
            {
                for( const Value &color : palette->array() )
                {
                    if( color.is_array() && color.array().size() == 4 && style.stroke.glyph_fill_colors.size() < 32 )
                    {
                        style.stroke.glyph_fill_colors.push_back(
                            read_color( Value::make_object( { { "fill", color } } ), "fill", style.stroke.fill ) );
                    }
                }
            }

            style.stroke.inner_border = read_color( *entry, "stroke", style.stroke.inner_border );

            style.stroke.outer_border = read_color( *entry, "outline", style.stroke.outer_border );

            if( const Value *height = entry->find( "height" ) )
            {
                style.height = std::clamp( static_cast<float>( height->number_or( style.height ) ), 8.0f, 100.0f );
            }

            if( const Value *width = entry->find( "stroke_width" ) )
            {
                style.stroke.inner_border_pixels =
                    static_cast<std::int32_t>( std::clamp( width->integer_or( 3 ), 0LL, 12LL ) );
            }

            if( const Value *width = entry->find( "outline_width" ) )
            {
                style.stroke.outer_border_pixels =
                    static_cast<std::int32_t>( std::clamp( width->integer_or( 7 ), 0LL, 18LL ) );
            }
        }

        return style;
    }

    std::string stage_name( const gamedata::PlayableStageEntry &stage )
    {
        const std::string mode = stage.mode == gamedata::PlayMode::Tutorial ? text( "@ui.tutorial_upper" )
                                 : stage.mode == gamedata::PlayMode::Multiplayer
                                     ? text( "@difficulty.multi" )
                                     : difficulty_name( gamedata::difficulty_from_chc_path( stage.chc_path ) );

        return format( "@ui.stage.summary", { { "stage", std::to_string( stage.stage_number ) }, { "mode", mode } } );
    }

    const char *difficulty_name( std::string_view difficulty )
    {
        if( difficulty == "Easy" )
        {
            return text( "@difficulty.easy" );
        }

        if( difficulty == "Master" )
        {
            return text( "@difficulty.master" );
        }

        if( difficulty == "Multi" )
        {
            return text( "@difficulty.multi" );
        }

        return text( "@difficulty.normal" );
    }
}
