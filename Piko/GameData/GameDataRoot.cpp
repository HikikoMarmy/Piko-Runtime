#include "GameDataRoot.hpp"

#include "Utils/FileUtil.hpp"
#include "Utils/StrUtil.hpp"

#include <cstdint>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <system_error>

namespace piko::gamedata
{
    bool GameDataRoot::initialize( const std::filesystem::path &application_root, GameLanguage requested_language )
    {
        clear();

        application_root_ = application_root.empty() ? std::filesystem::current_path() : application_root;

        application_root_ = application_root_.lexically_normal();

        game_data_root_ = ( application_root_ / "GAME_DATA" ).lexically_normal();

        valid_ = !installed_runtime_locales().empty();
        detect_region_and_language( requested_language );
        return valid_;
    }

    void GameDataRoot::clear()
    {
        valid_ = false;
        application_root_.clear();
        game_data_root_.clear();
        region_ = GameRegion::Unknown;

        language_ = GameLanguage::Default;
        active_locale_.clear();
        texture_roots_.clear();
    }

    void GameDataRoot::set_texture_roots( const std::vector<std::filesystem::path> &roots )
    {
        texture_roots_ = roots;
    }

    std::vector<std::filesystem::path> GameDataRoot::texture_search_roots() const
    {
        auto roots = texture_roots_;

        for( const auto &root : runtime_search_roots() )
        {
            if( std::find( roots.begin(), roots.end(), root ) == roots.end() )
            {
                roots.push_back( root );
            }
        }

        return roots;
    }

    bool GameDataRoot::valid() const
    {
        return valid_;
    }

    bool GameDataRoot::native_runtime_available() const
    {
        return !runtime_root().empty();
    }

    std::string GameDataRoot::runtime_locale_name() const
    {
        if( !active_locale_.empty() )
        {
            return active_locale_;
        }

        switch( language_ )
        {
            case GameLanguage::Japanese:
                return "JP";
            case GameLanguage::French:
                return "FR";
            case GameLanguage::German:
                return "GR";
            case GameLanguage::English:
                return region_ == GameRegion::Europe ? "EN" : "US";
            case GameLanguage::Default:
                break;
        }

        return "US";
    }

    std::filesystem::path GameDataRoot::runtime_root() const
    {
        if( game_data_root_.empty() )
        {
            return {};
        }

        const std::filesystem::path preferred = ( game_data_root_ / runtime_locale_name() ).lexically_normal();

        if( piko::utils::regular_file( preferred / "manifest.json" ) )
        {
            return preferred;
        }

        for( const std::string &locale : { "US", "EN", "JP", "FR", "GR" } )
        {
            const std::filesystem::path candidate = ( game_data_root_ / locale ).lexically_normal();

            if( piko::utils::regular_file( candidate / "manifest.json" ) )
            {
                return candidate;
            }
        }

        return {};
    }

    std::vector<std::filesystem::path> GameDataRoot::runtime_search_roots() const
    {
        std::vector<std::filesystem::path> roots;

        if( game_data_root_.empty() )
        {
            return roots;
        }

        const auto append_locale = [ & ]( std::string_view locale )
        {
            const std::filesystem::path candidate = ( game_data_root_ / std::string( locale ) ).lexically_normal();

            if( !piko::utils::regular_file( candidate / "manifest.json" ) )
            {
                return;
            }

            if( std::find( roots.begin(), roots.end(), candidate ) == roots.end() )
            {
                roots.push_back( candidate );
            }
        };

        append_locale( runtime_locale_name() );

        if( region_ == GameRegion::Europe &&
            ( language_ == GameLanguage::French || language_ == GameLanguage::German ) )
        {
            append_locale( "EN" );
        }

        if( roots.empty() )
        {
            for( const std::string &locale : { "US", "EN", "JP", "FR", "GR" } )
            {
                append_locale( locale );
            }
        }

        return roots;
    }

    std::filesystem::path GameDataRoot::native_runtime_root() const
    {
        return runtime_root();
    }

    const std::filesystem::path &GameDataRoot::application_root() const
    {
        return application_root_;
    }

    const std::filesystem::path &GameDataRoot::game_data_root() const
    {
        return game_data_root_;
    }

    std::filesystem::path GameDataRoot::resource_root() const
    {
        if( application_root_.empty() )
        {
            return {};
        }

        return ( application_root_ / "GAME_DATA" / "PIKO" ).lexically_normal();
    }

    std::filesystem::path GameDataRoot::piko_data_root() const
    {
        if( game_data_root_.empty() )
        {
            return {};
        }

        return ( game_data_root_ / "PIKO" ).lexically_normal();
    }

    std::filesystem::path GameDataRoot::albumdef_path() const
    {
        const std::filesystem::path root = runtime_root();

        if( root.empty() )
        {
            return {};
        }

        const std::filesystem::path path = ( root / "album.json" ).lexically_normal();
        return piko::utils::regular_file( path ) ? path : std::filesystem::path{};
    }

    std::filesystem::path GameDataRoot::resolve_resource_path( std::string_view relative_path ) const
    {
        if( application_root_.empty() )
        {
            return {};
        }

        const std::filesystem::path relative = normalize_relative_asset_path( relative_path );

        if( relative.empty() || relative.is_absolute() )
        {
            return {};
        }

        return ( resource_root() / relative ).lexically_normal();
    }

    std::filesystem::path GameDataRoot::resolve_piko_path( std::string_view relative_path ) const
    {
        if( game_data_root_.empty() )
        {
            return {};
        }

        const std::filesystem::path relative = normalize_relative_asset_path( relative_path );

        if( relative.empty() || relative.is_absolute() )
        {
            return {};
        }

        return ( piko_data_root() / relative ).lexically_normal();
    }

    std::filesystem::path GameDataRoot::resolve_runtime_asset( std::string_view relative_path ) const
    {
        const std::filesystem::path relative = normalize_relative_asset_path( relative_path );

        if( relative.empty() || relative.is_absolute() )
        {
            return {};
        }

        std::error_code error{};

        const auto extension = piko::utils::uppercase_ascii( relative.extension().string() );
        const bool texture =
            extension == ".PNG" || ( relative.filename() == "atlas.json" && relative.begin()->string() == "UI" );

        const auto roots = texture ? texture_search_roots() : runtime_search_roots();

        for( const std::filesystem::path &root : roots )
        {
            const std::filesystem::path candidate = ( root / relative ).lexically_normal();

            if( std::filesystem::exists( candidate, error ) )
            {
                return candidate;
            }

            error.clear();
        }

        return {};
    }

    std::filesystem::path GameDataRoot::resolve_runtime_movie( std::string_view runtime_path ) const
    {
        const std::filesystem::path relative = normalize_relative_asset_path( runtime_path );

        if( relative.empty() || piko::utils::uppercase_ascii( relative.extension().string() ) != ".MP4" )
        {
            return {};
        }

        for( const std::filesystem::path &root : runtime_search_roots() )
        {
            const std::filesystem::path direct = ( root / relative ).lexically_normal();

            if( piko::utils::regular_file( direct ) )
            {
                return direct;
            }

            const std::filesystem::path movie = ( root / "MOVIES" / relative.filename() ).lexically_normal();

            if( piko::utils::regular_file( movie ) )
            {
                return movie;
            }
        }

        return {};
    }

    std::vector<std::filesystem::path> GameDataRoot::runtime_stage_charts( std::int32_t stage_number,
        ChartStyle chart_style ) const
    {
        std::vector<std::filesystem::path> charts;

        if( stage_number < 0 || stage_number > 99 )
        {
            return charts;
        }

        char stage_name[ 16 ]{};
        std::snprintf( stage_name, sizeof( stage_name ), "STAGE%02d", stage_number );
        const char *style_directory = chart_style == ChartStyle::GitarooManLives ? "PSP" : "PS2";

        std::vector<std::string> filenames;

        for( const std::filesystem::path &root : runtime_search_roots() )
        {
            const std::filesystem::path directory =
                ( root / stage_name / "CHARTS" / style_directory ).lexically_normal();

            std::error_code error{};

            if( !std::filesystem::is_directory( directory, error ) )
            {
                continue;
            }

            for( const std::filesystem::directory_entry &entry :
                std::filesystem::directory_iterator( directory, error ) )
            {
                if( error )
                {
                    break;
                }

                if( !entry.is_regular_file() )
                {
                    continue;
                }

                if( piko::utils::uppercase_ascii( entry.path().extension().string() ) != ".JSON" )
                {
                    continue;
                }

                const std::string filename = piko::utils::uppercase_ascii( entry.path().filename().string() );

                if( std::find( filenames.begin(), filenames.end(), filename ) != filenames.end() )
                {
                    continue;
                }

                filenames.push_back( filename );
                charts.push_back( entry.path().lexically_normal() );
            }
        }

        std::sort( charts.begin(), charts.end() );
        return charts;
    }

    bool GameDataRoot::chart_style_available( ChartStyle chart_style ) const
    {
        for( std::int32_t stage_number = 0; stage_number <= 99; ++stage_number )
        {
            if( !runtime_stage_charts( stage_number, chart_style ).empty() )
            {
                return true;
            }
        }

        return false;
    }

    ChartStyle GameDataRoot::resolve_chart_style( ChartStyle preferred ) const
    {
        if( chart_style_available( preferred ) )
        {
            return preferred;
        }

        const ChartStyle alternate =
            preferred == ChartStyle::GitarooMan ? ChartStyle::GitarooManLives : ChartStyle::GitarooMan;

        return chart_style_available( alternate ) ? alternate : ChartStyle::GitarooMan;
    }

    std::filesystem::path GameDataRoot::runtime_stage_audio( std::int32_t stage_number,
        ChartStyle chart_style,
        std::string_view chart_filename ) const
    {
        if( stage_number < 0 || stage_number > 99 )
        {
            return {};
        }

        char stage_name[ 16 ]{};
        char audio_name[ 16 ]{};
        std::snprintf( stage_name, sizeof( stage_name ), "STAGE%02d", stage_number );
        std::snprintf( audio_name, sizeof( audio_name ), "ST%02d", stage_number );
        const char *style_directory = chart_style == ChartStyle::GitarooManLives ? "PSP" : "PS2";

        const std::string chart_name =
            piko::utils::uppercase_ascii( std::filesystem::path( chart_filename ).stem().string() );

        const bool tutorial_bank = stage_number == 0 && ( chart_name == "ST00A" || chart_name == "ST00B" );

        for( const std::filesystem::path &root : runtime_search_roots() )
        {
            const std::filesystem::path directory =
                ( root / stage_name / "AUDIO" / style_directory ).lexically_normal();

            if( tutorial_bank )
            {
                const std::filesystem::path requested = ( directory / chart_name ).lexically_normal();

                if( piko::utils::regular_file( requested / "audio.json" ) )
                {
                    return requested;
                }

                continue;
            }

            const std::filesystem::path expected = ( directory / audio_name ).lexically_normal();

            if( piko::utils::regular_file( expected / "audio.json" ) )
            {
                return expected;
            }

            std::vector<std::filesystem::path> banks;
            std::error_code error{};

            if( !std::filesystem::is_directory( directory, error ) )
            {
                continue;
            }

            for( const std::filesystem::directory_entry &entry :
                std::filesystem::directory_iterator( directory, error ) )
            {
                if( error )
                {
                    break;
                }

                if( entry.is_directory() && piko::utils::regular_file( entry.path() / "audio.json" ) )
                {
                    banks.push_back( entry.path().lexically_normal() );
                }
            }

            if( banks.size() == 1u )
            {
                return banks.front();
            }
        }

        return {};
    }

    std::filesystem::path GameDataRoot::runtime_stage_models( std::int32_t stage_number ) const
    {
        if( stage_number < 0 || stage_number > 99 )
        {
            return {};
        }

        char stage_name[ 16 ]{};
        std::snprintf( stage_name, sizeof( stage_name ), "STAGE%02d", stage_number );

        for( const std::filesystem::path &root : runtime_search_roots() )
        {
            const std::filesystem::path path = ( root / stage_name / "MODELS" ).lexically_normal();

            if( piko::utils::directory( path ) )
            {
                return path;
            }
        }

        return {};
    }

    std::filesystem::path GameDataRoot::runtime_stage_sequence( std::int32_t stage_number,
        std::string_view variant ) const
    {
        if( stage_number < 0 || stage_number > 99 )
        {
            return {};
        }

        char stage_name[ 16 ]{};
        std::snprintf( stage_name, sizeof( stage_name ), "STAGE%02d", stage_number );

        std::string requested = piko::utils::uppercase_ascii( std::filesystem::path( variant ).stem().string() );

        char sequence_name[ 32 ]{};

        if( requested.rfind( "ST", 0u ) == 0u || requested.rfind( "M_", 0u ) == 0u )
        {
            std::snprintf( sequence_name, sizeof( sequence_name ), "%s", requested.c_str() );
        }
        else
        {
            std::snprintf( sequence_name,
                sizeof( sequence_name ),
                requested == "M" ? "ST%02dM" : "ST%02d",
                stage_number );
        }

        for( const std::filesystem::path &root : runtime_search_roots() )
        {
            const std::filesystem::path path =
                ( root / stage_name / "SEQUENCES" / ( std::string( sequence_name ) + ".json" ) ).lexically_normal();

            if( piko::utils::regular_file( path ) )
            {
                return path;
            }
        }

        return {};
    }

    std::filesystem::path GameDataRoot::runtime_ui_asset( std::string_view category, std::string_view filename ) const
    {
        const std::filesystem::path relative_category = normalize_relative_asset_path( category );
        const std::filesystem::path relative_filename = normalize_relative_asset_path( filename );

        if( relative_category.empty() || relative_filename.empty() )
        {
            return {};
        }

        for( const std::filesystem::path &root : texture_search_roots() )
        {
            const std::filesystem::path path =
                ( root / "UI" / relative_category / relative_filename ).lexically_normal();

            if( piko::utils::regular_file( path ) )
            {
                return path;
            }
        }

        return {};
    }

    std::filesystem::path GameDataRoot::runtime_stage_texture( std::int32_t stage_number,
        std::string_view category,
        std::string_view filename ) const
    {
        if( stage_number < 0 || stage_number > 99 )
        {
            return {};
        }

        char stage_name[ 16 ]{};
        std::snprintf( stage_name, sizeof( stage_name ), "STAGE%02d", stage_number );

        const std::filesystem::path relative_category = normalize_relative_asset_path( category );
        const std::filesystem::path relative_filename = normalize_relative_asset_path( filename );

        if( relative_category.empty() || relative_filename.empty() )
        {
            return {};
        }

        for( const std::filesystem::path &root : texture_search_roots() )
        {
            const std::filesystem::path path =
                ( root / stage_name / "TEXTURES" / relative_category / relative_filename ).lexically_normal();

            if( piko::utils::regular_file( path ) )
            {
                return path;
            }
        }

        return {};
    }

    GameRegion GameDataRoot::region() const
    {
        return region_;
    }

    GameLanguage GameDataRoot::language() const
    {
        return language_;
    }

    std::vector<GameLanguage> GameDataRoot::available_languages() const
    {
        std::vector<GameLanguage> languages;

        const auto append = [ & ]( GameLanguage language )
        {
            if( std::find( languages.begin(), languages.end(), language ) == languages.end() )
            {
                languages.push_back( language );
            }
        };

        if( has_runtime_locale( "US" ) || has_runtime_locale( "EN" ) )
        {
            append( GameLanguage::English );
        }

        if( has_runtime_locale( "FR" ) )
        {
            append( GameLanguage::French );
        }

        if( has_runtime_locale( "GR" ) )
        {
            append( GameLanguage::German );
        }

        if( has_runtime_locale( "JP" ) )
        {
            append( GameLanguage::Japanese );
        }

        return languages;
    }

    std::string GameDataRoot::region_name() const
    {
        return region_to_string( region_ );
    }

    std::string GameDataRoot::language_name() const
    {
        return language_to_string( language_ );
    }

    std::filesystem::path GameDataRoot::main_menu_audio_path() const
    {
        for( const std::filesystem::path &root : runtime_search_roots() )
        {
            for( const std::filesystem::path &candidate :
                { root / "SOUND" / "AUDIO" / "MAINMENU", root / "MAINMENU" / "AUDIO" / "MAINMENU" } )
            {
                if( piko::utils::regular_file( candidate / "audio.json" ) )
                {
                    return candidate.lexically_normal();
                }
            }
        }

        return {};
    }

    void GameDataRoot::detect_region_and_language( GameLanguage requested_language )
    {
        if( !valid_ )
        {
            region_ = GameRegion::Unknown;

            language_ = GameLanguage::Default;
            active_locale_.clear();
            return;
        }

        const auto select_locale = [ & ]( std::string_view locale )
        {
            if( !has_runtime_locale( locale ) )
            {
                return false;
            }

            active_locale_ = std::string( locale );

            region_ = region_for_locale( locale );

            language_ = language_for_locale( locale );
            return true;
        };

        switch( requested_language )
        {
            case GameLanguage::Japanese:
                if( select_locale( "JP" ) )
                {
                    return;
                }
                break;
            case GameLanguage::French:
                if( select_locale( "FR" ) )
                {
                    return;
                }
                break;
            case GameLanguage::German:
                if( select_locale( "GR" ) )
                {
                    return;
                }
                break;
            case GameLanguage::English:
                if( select_locale( "US" ) || select_locale( "EN" ) )
                {
                    return;
                }
                break;
            case GameLanguage::Default:
                break;
        }

        for( const std::string &locale : { "US", "EN", "JP", "FR", "GR" } )
        {
            if( select_locale( locale ) )
            {
                return;
            }
        }
    }

    bool GameDataRoot::has_runtime_locale( std::string_view locale ) const
    {
        if( game_data_root_.empty() )
        {
            return false;
        }

        const std::filesystem::path manifest =
            ( game_data_root_ / std::string( locale ) / "manifest.json" ).lexically_normal();

        return piko::utils::regular_file( manifest );
    }

    std::vector<std::string> GameDataRoot::installed_runtime_locales() const
    {
        std::vector<std::string> locales;

        for( const std::string &locale : { "US", "EN", "FR", "GR", "JP" } )
        {
            if( has_runtime_locale( locale ) )
            {
                locales.push_back( locale );
            }
        }

        return locales;
    }

    std::filesystem::path GameDataRoot::normalize_relative_asset_path( std::string_view relative_path )
    {
        std::string normalized;
        normalized.reserve( relative_path.size() );

        for( const char character : relative_path )
        {
            normalized.push_back( character == '\\' ? '/' : character );
        }

        while( !normalized.empty() && normalized.front() == '/' )
        {
            normalized.erase( normalized.begin() );
        }

        const std::filesystem::path path = std::filesystem::path( normalized ).lexically_normal();

        if( path.empty() || path.is_absolute() )
        {
            return {};
        }

        for( const std::filesystem::path &part : path )
        {
            if( part == ".." )
            {
                return {};
            }
        }

        return path;
    }

    GameRegion GameDataRoot::region_for_locale( std::string_view locale )
    {
        const std::string normalized = piko::utils::uppercase_ascii( std::string( locale ) );

        if( normalized == "JP" )
        {
            return GameRegion::Japan;
        }

        if( normalized == "EN" || normalized == "FR" || normalized == "GR" )
        {
            return GameRegion::Europe;
        }

        if( normalized == "US" )
        {
            return GameRegion::Usa;
        }

        return GameRegion::Unknown;
    }

    GameLanguage GameDataRoot::language_for_locale( std::string_view locale )
    {
        const std::string normalized = piko::utils::uppercase_ascii( std::string( locale ) );

        if( normalized == "JP" )
        {
            return GameLanguage::Japanese;
        }

        if( normalized == "FR" )
        {
            return GameLanguage::French;
        }

        if( normalized == "GR" )
        {
            return GameLanguage::German;
        }

        if( normalized == "US" || normalized == "EN" )
        {
            return GameLanguage::English;
        }

        return GameLanguage::Default;
    }

    const char *GameDataRoot::region_to_string( GameRegion region )
    {
        switch( region )
        {
            case GameRegion::Japan:
                return "Japan";
            case GameRegion::Usa:
                return "USA";
            case GameRegion::Europe:
                return "Europe";
            case GameRegion::Unknown:
                return "Unknown";
        }

        return "Unknown";
    }

    const char *GameDataRoot::language_to_string( GameLanguage language )
    {
        switch( language )
        {
            case GameLanguage::Japanese:
                return "Japanese";
            case GameLanguage::English:
                return "English";
            case GameLanguage::French:
                return "French";
            case GameLanguage::German:
                return "German";
            case GameLanguage::Default:
                return "Default";
        }

        return "Default";
    }
}
