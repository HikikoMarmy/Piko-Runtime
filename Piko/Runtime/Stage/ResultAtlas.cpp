#include "ResultAtlas.hpp"

#include "Utils/Json.hpp"

#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>

namespace piko::runtime::stage::result_atlas
{
    rendering::Rect read_rect( const utils::json::Value &value, const char *name )
    {
        const auto &numbers = value.array();

        if( numbers.size() != 4u )
        {
            throw std::runtime_error( std::string( "Result atlas rectangle must contain four values: " ) + name );
        }

        return rendering::Rect{
            static_cast<float>( numbers[ 0 ].number() ),
            static_cast<float>( numbers[ 1 ].number() ),
            static_cast<float>( numbers[ 2 ].number() ),
            static_cast<float>( numbers[ 3 ].number() )
        };
    }

    std::string replace_all( std::string value, std::string_view needle, std::string_view replacement )
    {
        std::size_t offset = 0u;
        while( ( offset = value.find( needle, offset ) ) != std::string::npos )
        {
            value.replace( offset, needle.size(), replacement );
            offset += replacement.size();
        }

        return value;
    }
}

namespace piko::runtime::stage
{
    ResultAtlas load_result_atlas( const std::filesystem::path &path )
    {
        const utils::json::Value root = utils::json::load_file( path );

        if( root.require( "format" ).string() != "piko-result-atlas" || root.require( "version" ).integer_or( 0 ) != 1 )
        {
            throw std::runtime_error( "Unsupported result atlas: " + path.string() );
        }

        ResultAtlas atlas{};
        atlas.platform = root.require( "platform" ).string();

        const utils::json::Value &textures = root.require( "textures" );
        atlas.common_texture = textures.require( "common" ).string();

        atlas.stage_texture_pattern = textures.require( "stage" ).string();

        atlas.no_miss_texture = textures.require( "no_miss" ).string_or();

        const utils::json::Value &rects = root.require( "rects" );

        if( atlas.platform == "PSP_DUET" )
        {
            atlas.background = result_atlas::read_rect( rects.require( "background" ), "background" );

            atlas.win = result_atlas::read_rect( rects.require( "win" ), "win" );

            atlas.lose = result_atlas::read_rect( rects.require( "lose" ), "lose" );

            const auto &portraits = rects.require( "portraits" ).array();

            const auto &ranks = rects.require( "ranks" ).array();

            if( portraits.size() != atlas.portraits.size() || ranks.size() != atlas.ranks.size() )
            {
                throw std::runtime_error( "Invalid PSP duet result atlas" );
            }

            for( std::size_t index = 0; index < portraits.size(); ++index )
            {
                atlas.portraits[ index ] = result_atlas::read_rect( portraits[ index ], "portraits" );
            }

            for( std::size_t index = 0; index < ranks.size(); ++index )
            {
                atlas.ranks[ index ] = result_atlas::read_rect( ranks[ index ], "ranks" );
            }

            return atlas;
        }

        atlas.background = result_atlas::read_rect( rects.require( "background" ), "background" );

        atlas.common_base = result_atlas::read_rect( rects.require( "common_base" ), "common_base" );

        atlas.stage_emblem = result_atlas::read_rect( rects.require( "stage_emblem" ), "stage_emblem" );

        atlas.stage_title = result_atlas::read_rect( rects.require( "stage_title" ), "stage_title" );

        atlas.stat_panel = result_atlas::read_rect( rects.require( "stat_panel" ), "stat_panel" );

        atlas.punctuation = result_atlas::read_rect( rects.require( "punctuation" ), "punctuation" );

        atlas.percent = result_atlas::read_rect( rects.require( "percent" ), "percent" );

        atlas.score_label = result_atlas::read_rect( rects.require( "score_label" ), "score_label" );

        atlas.digit = result_atlas::read_rect( rects.require( "digit" ), "digit" );

        atlas.digit_stride = static_cast<float>( rects.require( "digit_stride" ).number() );

        atlas.no_miss = result_atlas::read_rect( rects.require( "no_miss" ), "no_miss" );

        const auto &labels = rects.require( "stat_labels" ).array();

        if( labels.size() != atlas.stat_labels.size() )
        {
            throw std::runtime_error( "Result atlas must contain four statistic labels" );
        }

        for( std::size_t index = 0u; index < atlas.stat_labels.size(); ++index )
        {
            atlas.stat_labels[ index ] = result_atlas::read_rect( labels[ index ], "stat_labels" );
        }

        const auto &ranks = rects.require( "ranks" ).array();

        if( ranks.size() != atlas.ranks.size() )
        {
            throw std::runtime_error( "Result atlas must contain six rank rectangles" );
        }

        for( std::size_t index = 0u; index < atlas.ranks.size(); ++index )
        {
            atlas.ranks[ index ] = result_atlas::read_rect( ranks[ index ], "ranks" );
        }

        if( atlas.common_texture.empty() || atlas.stage_texture_pattern.empty() || atlas.digit.w <= 0.0f ||
            atlas.digit.h <= 0.0f || atlas.digit_stride <= 0.0f )
        {
            throw std::runtime_error( "Result atlas is incomplete: " + path.string() );
        }

        return atlas;
    }

    std::string result_atlas_stage_path( const ResultAtlas &atlas, std::int32_t stage_number )
    {
        char stage[ 16 ]{};
        std::snprintf( stage, sizeof( stage ), "%d", std::max( 0, stage_number ) );
        char stage_two_digits[ 16 ]{};
        std::snprintf( stage_two_digits, sizeof( stage_two_digits ), "%02d", std::max( 0, stage_number ) );

        std::string path = atlas.stage_texture_pattern;
        path = result_atlas::replace_all( std::move( path ), "{stage2}", stage_two_digits );

        path = result_atlas::replace_all( std::move( path ), "{stage}", stage );
        return path;
    }

    const rendering::Rect &result_rank_rect( const ResultAtlas &atlas, std::int32_t rank_index )
    {
        const std::size_t index = static_cast<std::size_t>( std::clamp( rank_index, 0, 5 ) );
        return atlas.ranks[ index ];
    }
}
