#include "AlbumFlow.hpp"

#include "FileTypes/AlbumDef/AlbumDef.hpp"
#include "Utils/StrUtil.hpp"

#include <cstdint>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace piko::gamedata::album_flow
{
    std::string movie_argument( const filetypes::albumdef::AlbumPage &page )
    {
        for( std::size_t index = 0; index + 1u < page.args.size(); ++index )
        {
            if( page.args[ index ] == "-m" )
            {
                return page.args[ index + 1u ];
            }
        }

        for( const std::string &argument : page.args )
        {
            const std::string upper = piko::utils::uppercase_ascii( argument );

            if( upper.ends_with( ".MP4" ) )
            {
                return argument;
            }
        }

        return {};
    }

    bool transparent_page( const filetypes::albumdef::AlbumPage &page )
    {
        return page.module_type == "MODULE_TYPE_SINGLELOADER" || page.module_type == "MODULE_TYPE_SAVELOAD" ||
               page.module_type == "MODULE_TYPE_MEMCARDCHECK";
    }

    bool transparent_route_reaches( const filetypes::albumdef::AlbumDef &album,
        std::int32_t current_page_id,
        std::int32_t target_page_id,
        std::int32_t depth,
        std::unordered_set<std::int32_t> &visited )
    {
        if( current_page_id == target_page_id )
        {
            return true;
        }

        if( depth >= 16 || !visited.insert( current_page_id ).second )
        {
            return false;
        }

        const auto page_it = album.pages.find( current_page_id );

        if( page_it == album.pages.end() || !transparent_page( page_it->second ) )
        {
            return false;
        }

        for( const auto &[ return_code, next_page_id ] : page_it->second.returns )
        {
            (void)return_code;

            if( transparent_route_reaches( album, next_page_id, target_page_id, depth + 1, visited ) )
            {
                return true;
            }
        }

        return false;
    }

    bool movie_return_reaches_stage( const filetypes::albumdef::AlbumDef &album,
        const filetypes::albumdef::AlbumPage &movie_page,
        std::int32_t stage_page_id,
        bool &preferred_return )
    {
        bool reaches = false;
        preferred_return = false;

        for( const auto &[ return_code, next_page_id ] : movie_page.returns )
        {
            std::unordered_set<std::int32_t> visited;

            if( !transparent_route_reaches( album, next_page_id, stage_page_id, 0, visited ) )
            {
                continue;
            }

            reaches = true;

            if( return_code == 1 )
            {
                preferred_return = true;
            }
        }

        return reaches;
    }

    bool is_opening_movie( const filetypes::albumdef::AlbumPage &page )
    {
        const std::string argument = movie_argument( page );

        if( argument.empty() )
        {
            return false;
        }

        const std::string stem = piko::utils::uppercase_ascii( std::filesystem::path( argument ).stem().string() );
        return stem == "OP";
    }

    bool is_premove_movie( const filetypes::albumdef::AlbumPage &page )
    {
        const std::string argument = piko::utils::uppercase_ascii( movie_argument( page ) );
        return argument.find( "PREMOV" ) != std::string::npos;
    }
}

namespace piko::gamedata
{
    std::optional<std::int32_t> album_return_page( const GameDataRoot &root,
        std::int32_t page_id,
        std::int32_t return_code )
    {
        const std::filesystem::path path = root.albumdef_path();

        if( path.empty() )
        {
            return std::nullopt;
        }

        const filetypes::albumdef::AlbumDef album = filetypes::albumdef::load_json( path );

        const auto page_it = album.pages.find( page_id );

        if( page_it == album.pages.end() )
        {
            return std::nullopt;
        }

        const auto exact = page_it->second.returns.find( return_code );

        if( exact != page_it->second.returns.end() )
        {
            return exact->second;
        }

        if( page_it->second.returns.size() == 1u )
        {
            return page_it->second.returns.begin()->second;
        }

        return std::nullopt;
    }

    AlbumFlowTarget resolve_album_flow_target( const GameDataRoot &root,
        std::int32_t page_id,
        ChartStyle chart_style,
        bool allow_movies )
    {
        AlbumFlowTarget target{};
        target.page_id = page_id;

        const std::vector<PlayableStageEntry> stages = load_playable_stage_catalog( root, chart_style );
        const std::filesystem::path album_path = root.albumdef_path();

        if( album_path.empty() )
        {
            return target;
        }

        const filetypes::albumdef::AlbumDef album = filetypes::albumdef::load_json( album_path );
        std::int32_t current_page_id = page_id;

        for( std::int32_t depth = 0; depth < 16; ++depth )
        {
            target = {};

            target.page_id = current_page_id;

            const auto stage_it = std::find_if( stages.begin(),
                stages.end(),
                [ current_page_id ]( const PlayableStageEntry &entry )
                {
                    return entry.page_id == current_page_id;
                } );
            if( stage_it != stages.end() )
            {
                target.type = AlbumFlowTargetType::Stage;

                target.stage = *stage_it;
                return target;
            }

            const auto page_it = album.pages.find( current_page_id );

            if( page_it == album.pages.end() )
            {
                return target;
            }

            const filetypes::albumdef::AlbumPage &page = page_it->second;

            if( page.module_type == "MODULE_TYPE_TITLE" || page.module_type == "MODULE_TYPE_ALBUMMENU" )
            {
                target.type = AlbumFlowTargetType::MainMenu;
                return target;
            }

            if( page.module_type == "MODULE_TYPE_MOVIE" )
            {
                if( !allow_movies )
                {
                    const auto zero_return = page.returns.find( 0 );

                    if( zero_return != page.returns.end() )
                    {
                        current_page_id = zero_return->second;
                        continue;
                    }

                    if( page.returns.size() == 1u )
                    {
                        current_page_id = page.returns.begin()->second;
                        continue;
                    }

                    return target;
                }

                const std::string argument = album_flow::movie_argument( page );

                if( argument.empty() )
                {
                    return target;
                }

                target.movie_path = root.resolve_runtime_movie( argument );

                target.type = AlbumFlowTargetType::Movie;
                return target;
            }

            if( !album_flow::transparent_page( page ) )
            {
                return target;
            }

            const auto zero_return = page.returns.find( 0 );

            if( zero_return != page.returns.end() )
            {
                current_page_id = zero_return->second;
                continue;
            }

            if( page.returns.size() == 1u )
            {
                current_page_id = page.returns.begin()->second;
                continue;
            }

            return target;
        }

        return target;
    }

    std::optional<std::int32_t> album_intro_movie_page( const GameDataRoot &root, std::int32_t stage_page_id )
    {
        const std::filesystem::path album_path = root.albumdef_path();

        if( album_path.empty() )
        {
            return std::nullopt;
        }

        const filetypes::albumdef::AlbumDef album = filetypes::albumdef::load_json( album_path );

        struct Candidate
        {
            std::int32_t page_id = -1;
            bool premove = false;
            bool preferred_return = false;
        };

        std::vector<Candidate> candidates;

        for( const auto &[ page_id, page ] : album.pages )
        {
            if( page.module_type != "MODULE_TYPE_MOVIE" )
            {
                continue;
            }

            bool preferred_return = false;

            if( !album_flow::movie_return_reaches_stage( album, page, stage_page_id, preferred_return ) )
            {
                continue;
            }

            candidates.push_back( Candidate{ page_id, album_flow::is_premove_movie( page ), preferred_return } );
        }

        if( candidates.empty() )
        {
            return std::nullopt;
        }

        std::sort( candidates.begin(),
            candidates.end(),
            [ stage_page_id ]( const Candidate &left, const Candidate &right )
            {
                if( left.premove != right.premove )
                {
                    return left.premove;
                }

                if( left.preferred_return != right.preferred_return )
                {
                    return left.preferred_return;
                }

                const bool left_before = left.page_id < stage_page_id;
                const bool right_before = right.page_id < stage_page_id;

                if( left_before != right_before )
                {
                    return left_before;
                }

                const std::int32_t left_distance = std::abs( stage_page_id - left.page_id );
                const std::int32_t right_distance = std::abs( stage_page_id - right.page_id );

                if( left_distance != right_distance )
                {
                    return left_distance < right_distance;
                }

                return left.page_id < right.page_id;
            } );
        return candidates.front().page_id;
    }

    std::optional<std::int32_t> album_opening_movie_page( const GameDataRoot &root )
    {
        const std::filesystem::path album_path = root.albumdef_path();

        if( album_path.empty() )
        {
            return std::nullopt;
        }

        const filetypes::albumdef::AlbumDef album = filetypes::albumdef::load_json( album_path );

        std::optional<std::int32_t> opening_movie;

        for( const auto &[ page_id, page ] : album.pages )
        {
            if( page.module_type != "MODULE_TYPE_MOVIE" || !album_flow::is_opening_movie( page ) )
            {
                continue;
            }

            if( !opening_movie.has_value() || page_id < *opening_movie )
            {
                opening_movie = page_id;
            }
        }

        if( opening_movie.has_value() )
        {
            return opening_movie;
        }

        const auto page_zero = album.pages.find( 0 );

        if( page_zero != album.pages.end() && page_zero->second.module_type == "MODULE_TYPE_MOVIE" )
        {
            return 0;
        }

        std::optional<std::int32_t> first_movie;

        for( const auto &[ page_id, page ] : album.pages )
        {
            if( page.module_type != "MODULE_TYPE_MOVIE" )
            {
                continue;
            }

            if( !first_movie.has_value() || page_id < *first_movie )
            {
                first_movie = page_id;
            }
        }

        return first_movie;
    }
}
