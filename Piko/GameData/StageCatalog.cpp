#include "StageCatalog.hpp"
#include "Utils/FileUtil.hpp"
#include "Utils/StrUtil.hpp"

#include <cstdint>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <string_view>
#include <system_error>

namespace piko::gamedata::stage_catalog
{
    bool has_flag( const std::vector<std::string> &args, std::string_view flag )
    {
        return std::find( args.begin(), args.end(), flag ) != args.end();
    }

    std::string get_arg_value( const std::vector<std::string> &args, std::string_view flag, std::string fallback = {} )
    {
        for( std::size_t index = 0; index + 1u < args.size(); ++index )
        {
            if( args[ index ] == flag )
            {
                return args[ index + 1u ];
            }
        }

        return fallback;
    }

    std::int32_t parse_int_or( const std::string &text, std::int32_t fallback )
    {
        if( text.empty() )
        {
            return fallback;
        }

        std::int32_t value = fallback;
        std::from_chars( text.data(), text.data() + text.size(), value );
        return value;
    }

    float parse_float_or( const std::string &text, float fallback )
    {
        if( text.empty() )
        {
            return fallback;
        }

        try
        {
            return std::stof( text );
        }
        catch( ... )
        {
            return fallback;
        }
    }

    bool ends_with( std::string_view value, std::string_view suffix )
    {
        return value.size() >= suffix.size() && value.substr( value.size() - suffix.size() ) == suffix;
    }

    std::string stage_number_text( std::int32_t stage_number )
    {
        if( stage_number < 0 )
        {
            return "??";
        }

        std::ostringstream output;

        if( stage_number < 10 )
        {
            output << '0';
        }

        output << stage_number;
        return output.str();
    }

    std::int32_t difficulty_order( const std::filesystem::path &path )
    {
        const std::string difficulty = difficulty_from_chc_path( path );

        if( difficulty == "Easy" )
        {
            return 0;
        }

        if( difficulty == "Normal" )
        {
            return 1;
        }

        if( difficulty == "Master" )
        {
            return 2;
        }

        if( difficulty == "Multi" )
        {
            return 3;
        }

        return 9;
    }

    PlayMode mode_from_module( const std::string &module )
    {
        if( module == "MODULE_TYPE_TUTORIAL" )
        {
            return PlayMode::Tutorial;
        }

        if( module == "MODULE_TYPE_MULTI_PLYR_GAME" )
        {
            return PlayMode::Multiplayer;
        }

        return PlayMode::SinglePlayer;
    }

    std::string chart_variant_from_path( const std::filesystem::path &path )
    {
        const std::string chart_stem = piko::utils::uppercase_ascii( path.stem().string() );

        if( chart_stem.size() < 4u || chart_stem[ 0 ] != 'S' || chart_stem[ 1 ] != 'T' ||
            !std::isdigit( static_cast<unsigned char>( chart_stem[ 2 ] ) ) ||
            !std::isdigit( static_cast<unsigned char>( chart_stem[ 3 ] ) ) )
        {
            return {};
        }

        std::string suffix = chart_stem.substr( 4u );
        while( !suffix.empty() && suffix.front() == '_' )
        {
            suffix.erase( suffix.begin() );
        }

        return suffix;
    }

    std::string chart_variant_label( std::string_view variant )
    {
        if( variant == "E" )
        {
            return "Easy";
        }

        if( variant == "H" )
        {
            return "Master";
        }

        if( variant == "M" )
        {
            return "Multi";
        }

        if( variant == "N" || variant.empty() )
        {
            return "Normal";
        }

        return std::string( variant );
    }

    std::int32_t chart_variant_order( std::string_view variant )
    {
        if( variant == "E" )
        {
            return 0;
        }

        if( variant == "N" )
        {
            return 1;
        }

        if( variant.empty() )
        {
            return 2;
        }

        if( variant == "H" )
        {
            return 3;
        }

        if( variant == "M" )
        {
            return 4;
        }

        return 9;
    }

    std::filesystem::path canonical_chart_for( const GameDataRoot &root,
        std::int32_t stage_number,
        const std::filesystem::path &source_reference,
        PlayMode mode,
        ChartStyle chart_style )
    {
        std::vector<std::filesystem::path> charts = root.runtime_stage_charts( stage_number, chart_style );

        const std::string source_stem = piko::utils::uppercase_ascii( source_reference.stem().string() );

        const auto exact = std::find_if( charts.begin(),
            charts.end(),
            [ & ]( const std::filesystem::path &chart )
            {
                return piko::utils::uppercase_ascii( chart.stem().string() ) == source_stem;
            } );
        if( exact != charts.end() )
        {
            return *exact;
        }

        const std::string desired =
            mode == PlayMode::Multiplayer ? "Multi" : difficulty_from_chc_path( source_reference );

        std::vector<std::filesystem::path> matches;

        for( const std::filesystem::path &chart : charts )
        {
            if( difficulty_from_chc_path( chart ) == desired )
            {
                matches.push_back( chart );
            }
        }

        std::sort( matches.begin(),
            matches.end(),
            []( const std::filesystem::path &left, const std::filesystem::path &right )
            {
                const std::int32_t left_order = chart_variant_order( chart_variant_from_path( left ) );
                const std::int32_t right_order = chart_variant_order( chart_variant_from_path( right ) );

                if( left_order != right_order )
                {
                    return left_order < right_order;
                }

                return left < right;
            } );

        return matches.empty() ? std::filesystem::path{} : matches.front();
    }

    std::string short_name_for( const PlayableStageEntry &entry )
    {
        std::ostringstream output;

        if( !entry.bonus_source.empty() )
        {
            output << entry.bonus_source << " - ";
        }

        output << "Stage " << stage_number_text( entry.stage_number ) << " - ";

        if( entry.mode == PlayMode::Tutorial )
        {
            output << "Tutorial";
        }
        else if( entry.mode == PlayMode::Multiplayer )
        {
            output << "Multiplayer " << entry.multiplayer_players << 'P';
        }
        else
        {
            output << difficulty_from_chc_path( entry.chc_path );
        }

        return output.str();
    }

    std::string display_name_for( const PlayableStageEntry &entry )
    {
        std::ostringstream output;
        output << short_name_for( entry );
        output << " [" << entry.chc_path.filename().string() << ']';
        output << " page " << entry.page_id;
        return output.str();
    }

    std::string detail_line_for( const PlayableStageEntry &entry )
    {
        std::ostringstream output;

        if( !entry.bonus_source.empty() )
        {
            output << entry.bonus_source << " | ";
        }

        output << play_mode_name( entry.mode );

        if( entry.mode == PlayMode::Multiplayer )
        {
            output << ' ' << entry.multiplayer_players << 'P';
        }

        output << " | tempo " << entry.tempo;
        output << " | -l " << entry.animation_l_marker_frames;

        if( entry.play_immediately )
        {
            output << " | play-now";
        }

        if( entry.show_chart )
        {
            output << " | chart";
        }

        if( entry.stereo_phrases )
        {
            output << " | stereo";
        }

        if( entry.duet_stage )
        {
            output << " | duet";
        }

        if( entry.portable_visuals )
        {
            output << " | portable visuals";
        }

        if( !entry.chc_exists )
        {
            output << " | CHC missing";
        }
        else if( entry.using_runtime_chart )
        {
            output << " | CHC JSON";
        }

        if( !entry.runtime_audio_path.empty() )
        {
            output << " | runtime audio";
        }

        if( !entry.runtime_model_root.empty() )
        {
            output << " | glTF models";
        }

        return output.str();
    }

    void refresh_entry_display( PlayableStageEntry &entry )
    {
        entry.chc_exists = piko::utils::path_exists( entry.chc_path );

        entry.ssq_exists = piko::utils::path_exists( entry.ssq_path );

        entry.short_name = short_name_for( entry );

        entry.display_name = display_name_for( entry );

        entry.detail_line = detail_line_for( entry );
    }

    std::vector<PlayableStageEntry> build_album_entries( const filetypes::albumdef::AlbumDef &album,
        const GameDataRoot &root,
        ChartStyle chart_style )
    {
        std::vector<PlayableStageEntry> entries;

        for( const auto &[ page_id, page ] : album.pages )
        {
            if( !filetypes::albumdef::is_playable_module( page.module_type ) )
            {
                continue;
            }

            const std::string chc = get_arg_value( page.args, "-c" );

            if( chc.empty() )
            {
                continue;
            }

            PlayableStageEntry entry{};
            entry.page_id = page_id;

            entry.chart_style = chart_style;

            entry.mode = mode_from_module( page.module_type );

            entry.stage_number = parse_int_or( get_arg_value( page.args, "-n" ), -1 );

            entry.multiplayer_players = parse_int_or( get_arg_value( page.args, "-y" ), 1 );

            entry.runtime_model_root = root.runtime_stage_models( entry.stage_number );

            const std::filesystem::path source_chart_reference = chc;

            if( const std::filesystem::path runtime_chart =
                    canonical_chart_for( root, entry.stage_number, source_chart_reference, entry.mode, chart_style );
                !runtime_chart.empty() )
            {
                entry.chc_path = runtime_chart;

                entry.using_runtime_chart = true;

                entry.chart_variant = chart_variant_label( chart_variant_from_path( runtime_chart ) );
            }

            if( entry.chc_path.empty() )
            {
                continue;
            }

            const std::string chart_filename = entry.chc_path.filename().string();
            entry.runtime_audio_path = root.runtime_stage_audio( entry.stage_number, chart_style, chart_filename );

            if( entry.runtime_audio_path.empty() && chart_style == ChartStyle::GitarooManLives )
            {
                entry.runtime_audio_path =
                    root.runtime_stage_audio( entry.stage_number, ChartStyle::GitarooMan, chart_filename );
            }

            const std::string ssq = get_arg_value( page.args, "-s" );

            if( !ssq.empty() )
            {
                entry.ssq_path =
                    root.runtime_stage_sequence( entry.stage_number, std::filesystem::path( ssq ).stem().string() );
            }

            entry.tempo = parse_float_or( get_arg_value( page.args, "-t" ), 0.0f );

            entry.animation_l_marker_frames = parse_float_or( get_arg_value( page.args, "-l" ), 150.0f );

            entry.play_immediately = has_flag( page.args, "-p" );

            entry.show_chart = has_flag( page.args, "-g" );

            entry.show_sky = has_flag( page.args, "-k" );

            entry.stereo_phrases = has_flag( page.args, "-w" );

            refresh_entry_display( entry );
            entries.push_back( std::move( entry ) );
        }

        return entries;
    }

    void append_portable_only_stages( std::vector<PlayableStageEntry> &entries, const GameDataRoot &root )
    {
        for( const std::int32_t stage_number : { 11, 12 } )
        {
            for( const std::filesystem::path &chart :
                root.runtime_stage_charts( stage_number, ChartStyle::GitarooManLives ) )
            {
                const std::string variant = chart_variant_from_path( chart );
                PlayableStageEntry entry{};
                entry.page_id = 100000 + stage_number * 10 + chart_variant_order( variant );

                entry.mode = variant == "M" ? PlayMode::Multiplayer : PlayMode::SinglePlayer;

                entry.stage_number = stage_number;

                entry.multiplayer_players = variant == "M" ? 2 : 1;

                entry.chc_path = chart;

                entry.ssq_path = root.runtime_stage_sequence( stage_number, variant );

                entry.runtime_audio_path = root.runtime_stage_audio( stage_number, ChartStyle::GitarooManLives );

                entry.runtime_model_root = root.runtime_stage_models( stage_number );

                entry.using_runtime_chart = true;

                entry.chart_variant = chart_variant_label( variant );

                entry.tempo = 120.0f;

                entry.animation_l_marker_frames = 150.0f;

                entry.show_chart = true;

                entry.stereo_phrases = true;

                entry.duet_stage = true;

                entry.portable_visuals = true;

                entry.chart_style = ChartStyle::GitarooManLives;

                entry.bonus_content = true;

                entry.bonus_source = "Gitaroo Man Lives!";

                refresh_entry_display( entry );
                entries.push_back( std::move( entry ) );
            }
        }
    }

    void append_wan_stage( std::vector<PlayableStageEntry> &entries, const GameDataRoot &root )
    {
        const std::filesystem::path runtime = ( root.game_data_root() / "BONUS" / "WAN" ).lexically_normal();
        const std::filesystem::path chart_root = runtime / "STAGE02" / "CHARTS" / "PS2";
        std::error_code ec{};

        if( !std::filesystem::is_directory( chart_root, ec ) )
        {
            return;
        }

        for( const std::filesystem::directory_entry &item : std::filesystem::directory_iterator( chart_root, ec ) )
        {
            if( !item.is_regular_file() || piko::utils::uppercase_ascii( item.path().extension().string() ) != ".JSON" )
            {
                continue;
            }

            PlayableStageEntry entry{};
            entry.page_id = 200020;

            entry.mode = PlayMode::SinglePlayer;

            entry.stage_number = 2;

            entry.multiplayer_players = 1;

            entry.chc_path = item.path().lexically_normal();

            entry.ssq_path = ( runtime / "STAGE02" / "SEQUENCES" / "STAGE02.json" ).lexically_normal();

            entry.runtime_audio_path = ( runtime / "STAGE02" / "AUDIO" / "PS2" / "ST02" ).lexically_normal();

            entry.runtime_model_root = ( runtime / "STAGE02" / "MODELS" ).lexically_normal();

            entry.using_runtime_chart = true;

            entry.chart_variant = chart_variant_label( chart_variant_from_path( entry.chc_path ) );

            entry.tempo = 153.0f;

            entry.animation_l_marker_frames = 150.0f;

            entry.play_immediately = true;

            entry.show_chart = true;

            entry.show_sky = true;

            entry.bonus_content = true;

            entry.bonus_source = "Gitaroo Man Wan";

            refresh_entry_display( entry );
            entries.push_back( std::move( entry ) );
        }
    }
}

namespace piko::gamedata
{
    const char *play_mode_name( PlayMode mode )
    {
        switch( mode )
        {
            case PlayMode::Tutorial:
                return "Tutorial";
            case PlayMode::SinglePlayer:
                return "Single";
            case PlayMode::Multiplayer:
                return "Multi";
        }

        return "Unknown";
    }

    std::string difficulty_from_chc_path( const std::filesystem::path &path )
    {
        const std::string stem = piko::utils::uppercase_ascii( path.stem().string() );

        if( stage_catalog::ends_with( stem, "_E" ) || ( stem.size() == 5u && stem[ 4 ] == 'E' ) )
        {
            return "Easy";
        }

        if( stage_catalog::ends_with( stem, "_H" ) || ( stem.size() == 5u && stem[ 4 ] == 'H' ) )
        {
            return "Master";
        }

        if( stage_catalog::ends_with( stem, "_M" ) || ( stem.size() == 5u && stem[ 4 ] == 'M' ) )
        {
            return "Multi";
        }

        return "Normal";
    }

    std::vector<PlayableStageEntry> build_playable_stage_catalog( const filetypes::albumdef::AlbumDef &album,
        const GameDataRoot &root,
        ChartStyle chart_style )
    {
        std::vector<PlayableStageEntry> entries = stage_catalog::build_album_entries( album, root, chart_style );
        stage_catalog::append_portable_only_stages( entries, root );
        stage_catalog::append_wan_stage( entries, root );

        std::sort( entries.begin(),
            entries.end(),
            []( const PlayableStageEntry &left, const PlayableStageEntry &right )
            {
                if( left.stage_number != right.stage_number )
                {
                    return left.stage_number < right.stage_number;
                }

                const std::int32_t left_difficulty = stage_catalog::difficulty_order( left.chc_path );
                const std::int32_t right_difficulty = stage_catalog::difficulty_order( right.chc_path );

                if( left_difficulty != right_difficulty )
                {
                    return left_difficulty < right_difficulty;
                }

                if( left.mode != right.mode )
                {
                    return static_cast<std::int32_t>( left.mode ) < static_cast<std::int32_t>( right.mode );
                }

                if( left.page_id != right.page_id )
                {
                    return left.page_id < right.page_id;
                }

                return left.chc_path < right.chc_path;
            } );

        return entries;
    }

    std::vector<PlayableStageEntry> load_playable_stage_catalog( const GameDataRoot &root, ChartStyle chart_style )
    {
        const std::filesystem::path albumdef_path = root.albumdef_path();

        if( albumdef_path.empty() )
        {
            return {};
        }

        const filetypes::albumdef::AlbumDef album = filetypes::albumdef::load_json( albumdef_path );
        return build_playable_stage_catalog( album, root, chart_style );
    }
}
