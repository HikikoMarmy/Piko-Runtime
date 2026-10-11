#include "Utils/EnumUtil.hpp"
#include "Localization/Language.hpp"
#include "ResultModule.hpp"
#include "Localization/LabelTexture.hpp"

#include "App/piko.hpp"
#include "FileTypes/CHC/CHCJson.hpp"
#include "GameData/AlbumFlow.hpp"
#include "Managers/InputManager.hpp"
#include "Modules/Collection/CollectionModule.hpp"
#include "Modules/MainMenu/MainMenuModule.hpp"
#include "Modules/Movie/MovieModule.hpp"
#include "Modules/Stage/StageModule.hpp"
#include "Modules/StageSelect/StageSelectModule.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/RenderUtil.hpp"
#include "Runtime/Stage/StageGameplayInterfaceAtlas.hpp"
#include "Utils/FileUtil.hpp"

#include <cstdint>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace piko::modules
{
    namespace
    {
        rendering::LogicalViewport result_viewport( rendering::Renderer &renderer )
        {
            auto viewport = rendering::make_logical_viewport( renderer, 640.0f, 484.0f );
            viewport.y += 36.0f * viewport.scale;
            return viewport;
        }
    }

    bool ResultModule::can_sample( const rendering::Texture &texture, float x, float y, float width, float height )
    {
        return texture.valid() && x >= 0.0f && y >= 0.0f && width > 0.0f && height > 0.0f &&
               x + width <= static_cast<float>( texture.width ) + 0.5f &&
               y + height <= static_cast<float>( texture.height ) + 0.5f;
    }

    void ResultModule::draw_region( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        float source_x,
        float source_y,
        float source_width,
        float source_height,
        float x,
        float y,
        float width,
        float height,
        const rendering::Color &tint )
    {
        if( !can_sample( texture, source_x, source_y, source_width, source_height ) )
        {
            return;
        }

        // Leave room for notifications
        const auto viewport = result_viewport( renderer );
        const rendering::Rect target = viewport.rect( x, y, width, height );
        renderer.draw_texture_region( texture,
            source_x,
            source_y,
            source_width,
            source_height,
            target.x,
            target.y,
            target.w,
            target.h,
            tint );
    }

    void ResultModule::draw_region_centered( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        float source_x,
        float source_y,
        float source_width,
        float source_height,
        float center_x,
        float center_y,
        float width,
        float height,
        const rendering::Color &tint )
    {
        draw_region( renderer,
            texture,
            source_x,
            source_y,
            source_width,
            source_height,
            center_x - width * 0.5f,
            center_y - height * 0.5f,
            width,
            height,
            tint );
    }

    void ResultModule::draw_result_digit_at( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const runtime::stage::ResultAtlas &atlas,
        std::int32_t digit,
        float x,
        float y,
        float alpha )
    {
        const std::int32_t safe_digit = std::clamp( digit, 0, 9 );
        draw_region( renderer,
            texture,
            atlas.digit.x + static_cast<float>( safe_digit ) * atlas.digit_stride,
            atlas.digit.y,
            atlas.digit.w,
            atlas.digit.h,
            x,
            y,
            atlas.digit.w,
            atlas.digit.h,
            rendering::Color{ 1.0f, 1.0f, 1.0f, alpha } );
    }

    void ResultModule::draw_three_digits_at( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const runtime::stage::ResultAtlas &atlas,
        std::int32_t value,
        float x,
        float y,
        float alpha )
    {
        const std::int32_t safe_value = std::clamp( value, 0, 999 );
        draw_result_digit_at( renderer, texture, atlas, safe_value / 100, x, y, alpha );
        draw_result_digit_at( renderer, texture, atlas, ( safe_value / 10 ) % 10, x + 24.0f, y, alpha );
        draw_result_digit_at( renderer, texture, atlas, safe_value % 10, x + 48.0f, y, alpha );
    }

    void ResultModule::draw_two_digits_at( rendering::Renderer &renderer,
        const rendering::Texture &texture,
        const runtime::stage::ResultAtlas &atlas,
        std::int32_t value,
        float x,
        float y,
        float alpha )
    {
        const std::int32_t safe_value = std::clamp( value, 0, 99 );
        draw_result_digit_at( renderer, texture, atlas, safe_value / 10, x, y, alpha );
        draw_result_digit_at( renderer, texture, atlas, safe_value % 10, x + 24.0f, y, alpha );
    }

    ResultModule::ResultModule( ResultLoadData load_data ) : load_data_( std::move( load_data ) )
    {
    }

    void ResultModule::enter( App &app )
    {
        reveal_state_ = RevealState::Opening;

        state_tick_ = 0;

        total_tick_ = 0;

        tick_accumulator_ = 0.0;

        no_miss_y_ = -64.0f;

        display_score_ = load_data_.result.score;

        tally_count_ = 0;

        display_energy_units_ = static_cast<std::int32_t>(
            std::lround( std::clamp( load_data_.result.final_energy, 0.0f, 1.0f ) * 100.0f ) );

        advancing_ = false;

        autosave_attempted_ = load_data_.result.won;

        autosave_succeeded_ = false;

        notice_index_ = 0;

        notice_tick_ = 0;
        rewards_.clear();
        status_.clear();
        load_textures( app );
        calculate_result();
        const std::string difficulty = gamedata::difficulty_from_chc_path( load_data_.stage.chc_path );
        const gamedata::SaveProgressMode progress_mode = difficulty == "Easy"     ? gamedata::SaveProgressMode::Easy
                                                         : difficulty == "Master" ? gamedata::SaveProgressMode::Master
                                                                                  : gamedata::SaveProgressMode::Normal;

        std::array<bool, 28> unlocked_before{};

        if( autosave_attempted_ )
        {
            for( std::int32_t index = 0; index < static_cast<std::int32_t>( unlocked_before.size() ); ++index )
            {
                unlocked_before[ static_cast<std::size_t>( index ) ] = app.save_data().collection_unlocked( index );
            }
        }

        if( autosave_attempted_ )
        {
            autosave_succeeded_ = app.save_data().record_stage_result( load_data_.stage.stage_number,
                progress_mode,
                true,
                load_data_.result.score,
                rank_index_,
                no_miss_,
                load_data_.stage.chart_style );
        }

        if( autosave_attempted_ && !autosave_succeeded_ )
        {
            std::cerr << "[Piko Save] " << app.save_data().last_error() << '\n';
        }

        if( autosave_succeeded_ )
        {
            for( std::int32_t index = 0; index < static_cast<std::int32_t>( unlocked_before.size() ); ++index )
            {
                if( !unlocked_before[ static_cast<std::size_t>( index ) ] &&
                    app.save_data().collection_unlocked( index ) )
                {
                    rewards_.push_back( RewardNotice{ .collection_index = index } );
                }
            }
        }

        load_notices( app );
        initialize_audio( app );
    }

    void ResultModule::leave( App &app )
    {
        shutdown_audio( app );
        unload_notices( app );
        unload_textures( app );
    }

    managers::TextureHandle ResultModule::notice_text( App &app,
        const std::string &key,
        const std::string &label,
        float size,
        const rendering::Color &color )
    {
        const rendering::TrueTypeFont &font = notice_font_;

        if( !font.loaded() )
        {
            return {};
        }

        const rendering::StrokedTextStyle style{
            color,
            rendering::Color{ 0.08f, 0.26f, 0.42f, 1.0f },
            rendering::Color{ 0.015f, 0.045f, 0.09f, 1.0f },
            1,
            3,
            0.6f
        };

        const rendering::RasterizedText raster = font.rasterize_stroked( label, size * notice_raster_scale, style );

        if( !raster.valid() )
        {
            return {};
        }

        return app.texture_manager().create_rgba( localization::texture_key( key ),
            raster.rgba.data(),
            raster.width,
            raster.height );
    }

    void ResultModule::load_notices( App &app )
    {
        if( !autosave_attempted_ )
        {
            return;
        }

        const std::filesystem::path font_path = utils::find_resource_file( app.game_data().application_root(),
            std::filesystem::path( "fonts" ) / "noto-sans.bold.ttf" );

        localization::load_menu_font( notice_font_, font_path );

        try
        {
            notice_headings_[ 0 ] = notice_text( app,
                "result/notice/saved",
                localization::text( "@ui.auto_saving" ),
                notice_text_size,
                rendering::Color{ 0.96f, 0.98f, 1.0f, 1.0f } );

            notice_headings_[ 1 ] = notice_text( app,
                "result/notice/save-failed",
                localization::text( "@ui.auto_save_failed" ),
                notice_text_size,
                rendering::Color{ 1.0f, 0.53f, 0.50f, 1.0f } );

            for( RewardNotice &reward : rewards_ )
            {
                reward.name = notice_text( app,
                    "result/notice/name-" + std::to_string( reward.collection_index ),
                    std::string( localization::text( "@ui.got_trophy_upper" ) ) + " " +
                        CollectionModule::reward_name( reward.collection_index ),
                    notice_text_size,
                    rendering::Color{ 0.96f, 0.98f, 1.0f, 1.0f } );

                const std::optional<CollectionModule::RewardIcon> art =
                    CollectionModule::reward_icon( reward.collection_index );

                if( !art )
                {
                    continue;
                }

                const std::filesystem::path path = app.game_data().resolve_runtime_asset( art->asset );

                if( path.empty() )
                {
                    continue;
                }

                try
                {
                    reward.icon = app.texture_manager().load_png( "result/notice/icon-" +
                                                                      std::to_string( reward.collection_index ),
                        path );

                    reward.source = art->source;
                }
                catch( const std::exception &error )
                {
                    std::cerr << "[Piko Result] Trophy icon unavailable: " << error.what() << '\n';
                }
            }
        }
        catch( const std::exception &error )
        {
            std::cerr << "[Piko Result] Trophy notice asset unavailable: " << error.what() << '\n';
        }

        const std::filesystem::path icon_path = app.game_data().resolve_runtime_asset( "MC/TEXTURES/SV/SVBASE.png" );

        if( !icon_path.empty() )
        {
            try
            {
                autosave_icon_ = app.texture_manager().load_png( "result/notice/puma", icon_path );
            }
            catch( const std::exception &error )
            {
                std::cerr << "[Piko Result] Autosave icon unavailable: " << error.what() << '\n';
            }
        }
    }

    void ResultModule::unload_notices( App &app )
    {
        for( RewardNotice &reward : rewards_ )
        {
            app.texture_manager().unload( reward.name );
            app.texture_manager().unload( reward.icon );
        }

        rewards_.clear();

        for( managers::TextureHandle &heading : notice_headings_ )
        {
            app.texture_manager().unload( heading );
            heading = {};
        }

        app.texture_manager().unload( autosave_icon_ );
        autosave_icon_ = {};
        notice_font_.clear();
    }

    void ResultModule::render_notice( App &app, rendering::Renderer &renderer ) const
    {
        if( reveal_state_ != RevealState::Ready || !autosave_attempted_ ||
            notice_index_ > static_cast<std::int32_t>( rewards_.size() ) )
        {
            return;
        }

        const rendering::LogicalViewport layout = rendering::make_logical_viewport( renderer, 640.0f, 484.0f );
        const float fade_in = std::clamp( static_cast<float>( notice_tick_ ) / notice_fade_in_ticks, 0.0f, 1.0f );
        const float remaining_ticks = static_cast<float>( notice_duration_ticks - notice_tick_ );
        const float fade_out = std::clamp( remaining_ticks / notice_fade_out_ticks, 0.0f, 1.0f );
        const float alpha = std::min( fade_in, fade_out );
        const bool trophy = notice_index_ < static_cast<std::int32_t>( rewards_.size() );
        const RewardNotice *reward = trophy ? &rewards_[ static_cast<std::size_t>( notice_index_ ) ] : nullptr;

        const auto text_handle = reward != nullptr ? reward->name : notice_headings_[ autosave_succeeded_ ? 0u : 1u ];
        const rendering::Texture *label = app.texture_manager().get( text_handle );
        const rendering::Texture *icon = app.texture_manager().get( reward != nullptr ? reward->icon : autosave_icon_ );
        const rendering::Rect source =
            reward != nullptr ? reward->source : rendering::Rect{ 436.0f, 0.0f, 64.0f, 64.0f };

        const bool show_icon = icon != nullptr && source.w > 0.0f && source.h > 0.0f;
        const float icon_space = show_icon ? notice_icon_space : 0.0f;
        const float natural_width = label != nullptr ? static_cast<float>( label->width ) / notice_raster_scale : 0.0f;
        const float screen_width = static_cast<float>( renderer.output_width() ) / layout.scale;
        const float available_width = screen_width - notice_margin * 2.0f - icon_space - notice_text_padding;
        const float text_width = std::min( std::max( natural_width, notice_minimum_text_width ), available_width );
        const float width = text_width + notice_text_padding + icon_space;
        const float scale = layout.scale;
        const float x = static_cast<float>( renderer.output_width() ) - ( width + notice_margin ) * scale;
        const float y = notice_margin * scale;

        renderer.draw_filled_rect( rendering::Rect{ x, y, width * scale, notice_height * scale },
            rendering::Color{ 0.018f, 0.035f, 0.060f, alpha * 0.86f } );

        if( show_icon )
        {
            const float fit = notice_icon_size * scale / std::max( source.w, source.h );
            const float icon_width = source.w * fit;
            const float icon_height = source.h * fit;
            const float icon_x = x + ( 5.0f + notice_icon_size * 0.5f ) * scale - icon_width * 0.5f;
            const float icon_y = y + notice_height * scale * 0.5f - icon_height * 0.5f;
            renderer.draw_texture_region( *icon,
                source.x,
                source.y,
                source.w,
                source.h,
                icon_x,
                icon_y,
                icon_width,
                icon_height,
                rendering::Color{ 1.0f, 1.0f, 1.0f, alpha } );
        }

        if( label != nullptr )
        {
            const rendering::Rect bounds{
                x + ( notice_margin + icon_space ) * scale,
                y + 5.0f * scale,
                text_width * scale,
                20.0f * scale
            };

            const float fit = std::min(
                { scale / notice_raster_scale,
                    bounds.w / static_cast<float>( std::max( 1, label->width ) ),
                    bounds.h / static_cast<float>( std::max( 1, label->height ) ) } );

            const float label_width = static_cast<float>( label->width ) * fit;
            const float label_height = static_cast<float>( label->height ) * fit;
            renderer.draw_texture( *label,
                bounds.x,
                bounds.y + ( bounds.h - label_height ) * 0.5f,
                label_width,
                label_height,
                rendering::Color{ 1.0f, 1.0f, 1.0f, alpha } );
        }
    }

    void ResultModule::load_textures( App &app )
    {
        unload_textures( app );
        const std::string atlas_relative = load_data_.stage.portable_visuals && load_data_.stage.stage_number >= 11
                                               ? "UI/RESULT/PSP_DUET/atlas.json"
                                               : "UI/RESULT/PS2/atlas.json";

        const std::filesystem::path atlas_path = app.game_data().resolve_runtime_asset( atlas_relative );

        if( atlas_path.empty() )
        {
            status_ = "Result atlas missing: " + atlas_relative;
            std::cerr << "[Piko Runtime] " << status_ << '\n';
            return;
        }

        try
        {
            atlas_ = runtime::stage::load_result_atlas( atlas_path );
        }
        catch( const std::exception &error )
        {
            status_ = std::string( "Result atlas load failed: " ) + error.what();
            std::cerr << "[Piko Runtime] " << status_ << '\n';
            return;
        }

        const auto load =
            [ &app ]( managers::TextureHandle &handle, const char *name, const std::string &runtime_relative )
        {
            if( runtime_relative.empty() )
            {
                return false;
            }

            const std::filesystem::path runtime_path = app.game_data().resolve_runtime_asset( runtime_relative );

            if( runtime_path.empty() )
            {
                return false;
            }

            try
            {
                handle = app.texture_manager().load_png( name, runtime_path );
                return static_cast<bool>( handle );
            }
            catch( const std::exception & )
            {
                handle = {};
                return false;
            }
        };

        const bool common_ok = load( common_texture_, "result/common", atlas_.common_texture );
        const bool stage_ok = load( stage_texture_,
            "result/stage",
            runtime::stage::result_atlas_stage_path( atlas_, load_data_.stage.stage_number ) );

        if( !load( no_miss_label_texture_, "result/no-miss", "LOADING/TEXTURES/STANDALONE/NOMISS.png" ) )
        {
            load( no_miss_label_texture_, "result/no-miss", "UI/RESULT/NOMISS.png" );
        }

        if( atlas_.platform == "PSP_DUET" )
        {
            const auto digit_atlas_path = app.game_data().resolve_runtime_asset( "UI/RESULT/PS2/atlas.json" );

            if( !digit_atlas_path.empty() )
            {
                try
                {
                    digit_atlas_ = runtime::stage::load_result_atlas( digit_atlas_path );
                    load( digit_texture_, "result/digits", digit_atlas_.common_texture );
                }
                catch( const std::exception &error )
                {
                    std::cerr << "[Piko Runtime] Result digits failed: " << error.what() << '\n';
                }
            }

            if( !common_ok || !stage_ok )
            {
                status_ = "PSP duet result textures are missing";
                std::cerr << "[Piko Runtime] " << status_ << '\n';
            }

            return;
        }

        bool itfc_ok = false;

        if( !itfc_ok )
        {
            std::filesystem::path converted_itfc;

            if( load_data_.stage.portable_visuals )
            {
                char filename[ 16 ]{};
                std::snprintf( filename,
                    sizeof( filename ),
                    "ST%02d.png",
                    std::max( 0, load_data_.stage.stage_number ) );

                converted_itfc =
                    app.game_data().runtime_stage_texture( load_data_.stage.stage_number, "HUD", filename );
            }
            else
            {
                char stage_group[ 16 ]{};
                std::snprintf( stage_group,
                    sizeof( stage_group ),
                    "STAGE%02d",
                    std::max( 0, load_data_.stage.stage_number ) );

                converted_itfc =
                    app.game_data().runtime_stage_texture( load_data_.stage.stage_number, stage_group, "ITFC.png" );
            }

            if( !converted_itfc.empty() )
            {
                try
                {
                    itfc_texture_ = app.texture_manager().load_png( "result/itfc", converted_itfc );

                    itfc_ok = static_cast<bool>( itfc_texture_ );
                }
                catch( const std::exception & )
                {
                    itfc_texture_ = {};
                }
            }
        }

        if( !common_ok || !stage_ok || !itfc_ok )
        {
            status_ = "Result assets:";

            if( !common_ok )
            {
                status_ += " RSLT_ALL";
            }

            if( !stage_ok )
            {
                status_ += " RSLT_S";
            }

            if( !itfc_ok )
            {
                status_ += " ITFC";
            }

            std::cerr << "[Piko Runtime] " << status_ << " (converted PNG assets only)" << '\n';
        }
    }

    void ResultModule::unload_textures( App &app )
    {
        app.texture_manager().unload( no_miss_label_texture_ );
        app.texture_manager().unload( digit_texture_ );
        digit_texture_ = {};

        no_miss_label_texture_ = {};

        app.texture_manager().unload( common_texture_ );
        app.texture_manager().unload( stage_texture_ );
        app.texture_manager().unload( itfc_texture_ );
        common_texture_ = {};

        stage_texture_ = {};

        itfc_texture_ = {};
    }

    void ResultModule::calculate_result()
    {
        const auto &counts = load_data_.result.judgement_counts;
        std::uint32_t total = 0;

        for( const std::uint32_t count : counts )
        {
            total += count;
        }

        percentages_.fill( 0 );

        if( total > 0 )
        {
            std::int32_t percentage_sum = 0;

            for( std::size_t index = 0; index < counts.size(); ++index )
            {
                const double percentage = static_cast<double>( counts[ index ] ) * 100.0 / static_cast<double>( total );
                percentages_[ index ] = static_cast<std::int32_t>( std::lround( percentage ) );

                percentage_sum += percentages_[ index ];
            }

            if( percentage_sum < 100 )
            {
                std::size_t largest_index = 0;

                for( std::size_t index = 1; index < counts.size(); ++index )
                {
                    if( counts[ index ] > counts[ largest_index ] )
                    {
                        largest_index = index;
                    }
                }

                percentages_[ largest_index ] += 100 - percentage_sum;
            }
        }

        const std::int32_t great = percentages_[ utils::enum_index( runtime::stage::StageResultGrade::Great ) ];
        const std::int32_t good = percentages_[ utils::enum_index( runtime::stage::StageResultGrade::Good ) ];
        const std::int32_t ok = percentages_[ utils::enum_index( runtime::stage::StageResultGrade::Ok ) ];
        const std::int32_t energy = static_cast<std::int32_t>(
            std::lround( std::clamp( load_data_.result.final_energy, 0.0f, 1.0f ) * 1000.0f ) );

        const std::int32_t average_score = total > 0 ? static_cast<std::int32_t>( load_data_.result.score / total ) : 0;

        rank_score_ = average_score + great * 50 + good * 30 + ok * 10 + energy;
        const std::uint32_t misses = counts[ utils::enum_index( runtime::stage::StageResultGrade::Miss ) ];
        no_miss_ = misses == 0;

        if( rank_score_ >= 4800 && no_miss_ )
        {
            rank_index_ = 5;
        }
        else if( rank_score_ >= 4000 )
        {
            rank_index_ = 1;
        }
        else if( rank_score_ >= 3000 )
        {
            rank_index_ = 2;
        }
        else if( rank_score_ >= 2000 )
        {
            rank_index_ = 3;
        }
        else
        {
            rank_index_ = 4;
        }
    }

    void ResultModule::initialize_audio( App &app )
    {
        shutdown_audio( app );

        auto &audio = app.audio();

        const auto has_result_audio = [ &audio ]()
        {
            return audio.has_subsong( "RESULT" ) || audio.has_subsong( "RESULTBP" ) || audio.has_subsong( "RESULTFX" );
        };

        if( !has_result_audio() )
        {
            for( const std::filesystem::path &candidate : load_data_.audio_bank_candidates )
            {
                if( candidate.empty() )
                {
                    continue;
                }

                if( audio.load_audio_bank( candidate ) && has_result_audio() )
                {
                    break;
                }
            }
        }

        result_music_channel_ = audio.claim_channel( "Result music" );

        result_bp_channel_ = audio.claim_channel( "Result count" );

        result_fx_channel_ = audio.claim_channel( "Result effect" );

        if( result_music_channel_ && audio.has_subsong( "RESULT" ) )
        {
            audio.play_subsong( result_music_channel_, "RESULT", false );
        }

        if( audio.has_subsong( "RESULTBP" ) )
        {
            audio.preload_subsong( "RESULTBP" );
        }

        if( audio.has_subsong( "RESULTFX" ) )
        {
            audio.preload_subsong( "RESULTFX" );
        }

        if( audio.has_subsong( "OK" ) )
        {
            audio.preload_subsong( "OK" );
        }
    }

    void ResultModule::shutdown_audio( App &app )
    {
        auto &audio = app.audio();

        if( result_fx_channel_ )
        {
            audio.release_channel( result_fx_channel_ );
        }

        if( result_bp_channel_ )
        {
            audio.release_channel( result_bp_channel_ );
        }

        if( result_music_channel_ )
        {
            audio.release_channel( result_music_channel_ );
        }

        result_fx_channel_ = {};

        result_bp_channel_ = {};

        result_music_channel_ = {};
    }

    void ResultModule::play_result_bp( App &app )
    {
        if( result_bp_channel_ && app.audio().has_subsong( "RESULTBP" ) )
        {
            app.audio().play_subsong( result_bp_channel_, "RESULTBP", false );
        }
    }

    void ResultModule::play_result_fx( App &app )
    {
        if( result_fx_channel_ && app.audio().has_subsong( "RESULTFX" ) )
        {
            app.audio().play_subsong( result_fx_channel_, "RESULTFX", false );
        }
    }

    void ResultModule::update( App &app, double delta_seconds )
    {
        if( advancing_ )
        {
            return;
        }

        tick_accumulator_ += std::max( 0.0, delta_seconds );
        while( tick_accumulator_ >= tick_seconds_ )
        {
            tick_accumulator_ -= tick_seconds_;
            ++total_tick_;
            advance_tick( app );
        }

        if( reveal_advance_pressed( app ) )
        {
            if( reveal_state_ == RevealState::Ready )
            {
                begin_exit( app );
            }
            else if( reveal_state_ != RevealState::Exiting )
            {
                skip_reveal( app );
            }
        }

        if( reveal_state_ == RevealState::Exiting && state_tick_ >= 18 )
        {
            advance( app );
        }
    }

    bool ResultModule::reveal_advance_pressed( const App &app ) const
    {
        const auto &input = app.input();
        return input.pressed( managers::InputAction::Confirm ) || input.pressed( managers::InputAction::StageAction );
    }

    std::uint32_t ResultModule::displayed_stat_count( std::size_t index ) const
    {
        const auto &counts = load_data_.result.judgement_counts;

        if( index >= counts.size() )
        {
            return 0;
        }

        const std::int32_t category_state =
            utils::to_int( RevealState::GreatTally ) + static_cast<std::int32_t>( index );

        const std::int32_t current_state = utils::to_int( reveal_state_ );

        if( current_state < category_state )
        {
            return 0;
        }

        if( current_state == category_state )
        {
            return std::min( tally_count_, counts[ index ] );
        }

        return counts[ index ];
    }

    void ResultModule::skip_reveal( App &app )
    {
        state_tick_ = 0;

        switch( reveal_state_ )
        {
            case RevealState::Opening:
                reveal_state_ = RevealState::StagePanel;

                state_tick_ = 15;
                break;
            case RevealState::StagePanel:
                reveal_state_ = RevealState::StatsIntro;

                state_tick_ = 24;
                break;
            case RevealState::StatsIntro:
                reveal_state_ = RevealState::GreatTally;

                tally_count_ = 0;
                break;
            case RevealState::GreatTally:
            {
                const std::uint32_t total =
                    load_data_.result.judgement_counts[ utils::enum_index( runtime::stage::StageResultGrade::Great ) ];

                display_score_ += ( total - std::min( tally_count_, total ) ) * 100u;

                tally_count_ = 0;

                reveal_state_ = RevealState::GoodTally;
                break;
            }
            case RevealState::GoodTally:
            {
                const std::uint32_t total =
                    load_data_.result.judgement_counts[ utils::enum_index( runtime::stage::StageResultGrade::Good ) ];

                display_score_ += ( total - std::min( tally_count_, total ) ) * 50u;

                tally_count_ = 0;

                reveal_state_ = RevealState::OkTally;
                break;
            }
            case RevealState::OkTally:
            {
                const std::uint32_t total =
                    load_data_.result.judgement_counts[ utils::enum_index( runtime::stage::StageResultGrade::Ok ) ];

                display_score_ += total - std::min( tally_count_, total );

                tally_count_ = 0;

                reveal_state_ = RevealState::MissTally;
                break;
            }
            case RevealState::MissTally:
                tally_count_ = 0;

                reveal_state_ = RevealState::EnergyTally;
                break;
            case RevealState::EnergyTally:
                display_score_ += static_cast<std::uint32_t>( std::max( 0, display_energy_units_ ) ) * 100u;

                display_energy_units_ = 0;

                tally_count_ = 0;

                reveal_state_ = RevealState::Rank;
                play_result_fx( app );
                break;
            case RevealState::Rank:
                reveal_state_ = no_miss_ ? RevealState::NoMiss : RevealState::Ready;
                break;
            case RevealState::NoMiss:
                no_miss_y_ = 192.0f;

                reveal_state_ = RevealState::Ready;
                break;
            case RevealState::Ready:
            case RevealState::Exiting:
                break;
        }
    }

    void ResultModule::advance_tick( App &app )
    {
        ++state_tick_;

        switch( reveal_state_ )
        {
            case RevealState::Opening:
                if( state_tick_ >= 60 )
                {
                    reveal_state_ = RevealState::StagePanel;

                    state_tick_ = 0;
                }
                break;

            case RevealState::StagePanel:
                if( state_tick_ >= 15 )
                {
                    reveal_state_ = RevealState::StatsIntro;

                    state_tick_ = 0;
                }
                break;

            case RevealState::StatsIntro:
                if( state_tick_ >= 24 )
                {
                    reveal_state_ = RevealState::GreatTally;

                    state_tick_ = 0;

                    tally_count_ = 0;
                }
                break;

            case RevealState::GreatTally:
            {
                const std::uint32_t total =
                    load_data_.result.judgement_counts[ utils::enum_index( runtime::stage::StageResultGrade::Great ) ];

                if( tally_count_ < total )
                {
                    ++tally_count_;
                    display_score_ += 100u;

                    if( ( tally_count_ - 1u ) % 3u == 0u )
                    {
                        play_result_bp( app );
                    }
                }
                else
                {
                    reveal_state_ = RevealState::GoodTally;

                    state_tick_ = 0;

                    tally_count_ = 0;
                }
                break;
            }

            case RevealState::GoodTally:
            {
                const std::uint32_t total =
                    load_data_.result.judgement_counts[ utils::enum_index( runtime::stage::StageResultGrade::Good ) ];

                if( tally_count_ < total )
                {
                    ++tally_count_;
                    display_score_ += 50u;

                    if( ( tally_count_ - 1u ) % 3u == 0u )
                    {
                        play_result_bp( app );
                    }
                }
                else
                {
                    reveal_state_ = RevealState::OkTally;

                    state_tick_ = 0;

                    tally_count_ = 0;
                }
                break;
            }

            case RevealState::OkTally:
            {
                const std::uint32_t total =
                    load_data_.result.judgement_counts[ utils::enum_index( runtime::stage::StageResultGrade::Ok ) ];

                if( tally_count_ < total )
                {
                    ++tally_count_;
                    display_score_ += 1u;

                    if( ( tally_count_ - 1u ) % 3u == 0u )
                    {
                        play_result_bp( app );
                    }
                }
                else
                {
                    reveal_state_ = RevealState::MissTally;

                    state_tick_ = 0;

                    tally_count_ = 0;
                }
                break;
            }

            case RevealState::MissTally:
            {
                const std::uint32_t total =
                    load_data_.result.judgement_counts[ utils::enum_index( runtime::stage::StageResultGrade::Miss ) ];

                if( tally_count_ < total )
                {
                    ++tally_count_;

                    if( ( tally_count_ - 1u ) % 3u == 0u )
                    {
                        play_result_bp( app );
                    }
                }
                else
                {
                    reveal_state_ = RevealState::EnergyTally;

                    state_tick_ = 0;

                    tally_count_ = 0;
                }
                break;
            }

            case RevealState::EnergyTally:
                if( display_energy_units_ > 0 )
                {
                    --display_energy_units_;
                    display_score_ += 100u;
                    ++tally_count_;

                    if( ( tally_count_ - 1u ) % 3u == 0u )
                    {
                        play_result_bp( app );
                    }
                }
                else
                {
                    reveal_state_ = RevealState::Rank;

                    state_tick_ = 0;
                    play_result_fx( app );
                }
                break;

            case RevealState::Rank:
                if( state_tick_ >= 28 )
                {
                    reveal_state_ = no_miss_ ? RevealState::NoMiss : RevealState::Ready;

                    state_tick_ = 0;
                }
                break;

            case RevealState::NoMiss:
                no_miss_y_ = std::min( 192.0f, no_miss_y_ + 5.0f );

                if( no_miss_y_ >= 192.0f )
                {
                    reveal_state_ = RevealState::Ready;

                    state_tick_ = 0;
                }
                break;

            case RevealState::Ready:
                if( autosave_attempted_ && notice_index_ <= static_cast<std::int32_t>( rewards_.size() ) &&
                    ++notice_tick_ >= notice_duration_ticks )
                {
                    ++notice_index_;
                    notice_tick_ = 0;
                }
                break;
            case RevealState::Exiting:
                break;
        }
    }

    void ResultModule::begin_exit( App &app )
    {
        if( reveal_state_ != RevealState::Ready )
        {
            return;
        }

        if( autosave_attempted_ && notice_index_ <= static_cast<std::int32_t>( rewards_.size() ) )
        {
            ++notice_index_;
            notice_tick_ = 0;
            return;
        }

        if( result_fx_channel_ && app.audio().has_subsong( "OK" ) )
        {
            app.audio().play_subsong( result_fx_channel_, "OK", false );
        }

        reveal_state_ = RevealState::Exiting;

        state_tick_ = 0;
    }

    void ResultModule::advance( App &app )
    {
        if( advancing_ )
        {
            return;
        }

        advancing_ = true;

        if( !load_data_.result.won )
        {
            StageLoadData retry{};
            retry.stage = load_data_.stage;

            retry.song = load_data_.song;

            retry.chc_loaded = load_data_.chc_loaded;
            app.change_module( std::make_unique<StageModule>( std::move( retry ) ) );
            return;
        }

        if( load_data_.stage.bonus_content )
        {
            const std::string difficulty = gamedata::difficulty_from_chc_path( load_data_.stage.chc_path );
            const StageSelectModule::StageCategory category =
                difficulty == "Easy"     ? StageSelectModule::StageCategory::SingleEasy
                : difficulty == "Master" ? StageSelectModule::StageCategory::SingleMaster
                                         : StageSelectModule::StageCategory::SingleNormal;

            app.change_module( std::make_unique<StageSelectModule>( category ) );
            return;
        }

        try
        {
            const std::optional<std::int32_t> next_page =
                gamedata::album_return_page( app.game_data(), load_data_.stage.page_id, 2 );

            if( !next_page.has_value() )
            {
                app.change_module( std::make_unique<StageSelectModule>() );
                return;
            }

            const gamedata::AlbumFlowTarget target = gamedata::resolve_album_flow_target( app.game_data(),
                *next_page,
                app.settings().chart_style,
                app.settings().play_movies );

            if( target.type == gamedata::AlbumFlowTargetType::Movie )
            {
                app.change_module( std::make_unique<MovieModule>( target.page_id, target.movie_path ) );
                return;
            }

            if( target.type == gamedata::AlbumFlowTargetType::MainMenu )
            {
                app.change_module( std::make_unique<MainMenuModule>() );
                return;
            }

            if( target.type == gamedata::AlbumFlowTargetType::Stage )
            {
                StageLoadData next_load{};
                next_load.stage = target.stage;

                if( next_load.stage.chc_exists )
                {
                    next_load.song = filetypes::chc::load_json_file( next_load.stage.chc_path );

                    next_load.chc_loaded = true;
                }

                app.change_module( std::make_unique<StageModule>( std::move( next_load ) ) );
                return;
            }
        }
        catch( const std::exception & )
        {
        }

        app.change_module( std::make_unique<StageSelectModule>() );
    }

    void ResultModule::render( App &app, rendering::Renderer &renderer )
    {
        const float width = static_cast<float>( renderer.output_width() );
        const float height = static_cast<float>( renderer.output_height() );
        renderer.draw_filled_rect( rendering::Rect{ 0.0f, 0.0f, width, height },
            rendering::Color{ 0.0f, 0.0f, 0.0f, 1.0f } );

        if( atlas_.platform == "PSP_DUET" )
        {
            render_portable_duet( app, renderer );
            render_notice( app, renderer );

            if( reveal_state_ == RevealState::Exiting )
            {
                const float alpha = std::clamp( static_cast<float>( state_tick_ ) / 18.0f, 0.0f, 1.0f );
                renderer.draw_filled_rect( { 0.0f, 0.0f, width, height }, { 0.0f, 0.0f, 0.0f, alpha } );
            }

            return;
        }

        render_background_layers( renderer );
        render_common_base( app, renderer );

        if( reveal_state_ != RevealState::Opening )
        {
            render_stage_panel( app, renderer );
        }

        if( reveal_state_ == RevealState::StatsIntro || reveal_state_ == RevealState::GreatTally ||
            reveal_state_ == RevealState::GoodTally || reveal_state_ == RevealState::OkTally ||
            reveal_state_ == RevealState::MissTally || reveal_state_ == RevealState::EnergyTally ||
            reveal_state_ == RevealState::Rank || reveal_state_ == RevealState::NoMiss ||
            reveal_state_ == RevealState::Ready || reveal_state_ == RevealState::Exiting )
        {
            render_energy_gauge( app, renderer );
            render_score( app, renderer );
            render_statistics( app, renderer );
        }

        if( reveal_state_ == RevealState::Rank || reveal_state_ == RevealState::NoMiss ||
            reveal_state_ == RevealState::Ready || reveal_state_ == RevealState::Exiting )
        {
            render_rank( app, renderer );
        }

        if( no_miss_ && ( reveal_state_ == RevealState::NoMiss || reveal_state_ == RevealState::Ready ||
                            reveal_state_ == RevealState::Exiting ) )
        {
            render_no_miss( app, renderer );
        }

        render_notice( app, renderer );

        if( reveal_state_ == RevealState::Exiting )
        {
            const float alpha = std::clamp( static_cast<float>( state_tick_ ) / 18.0f, 0.0f, 1.0f );
            renderer.draw_filled_rect( rendering::Rect{ 0.0f, 0.0f, width, height },
                rendering::Color{ 0.0f, 0.0f, 0.0f, alpha } );
        }

        if( !status_.empty() && !common_texture_ )
        {
            renderer.draw_text( 24.0f,
                height - 40.0f,
                status_.c_str(),
                rendering::Color{ 1.0f, 0.7f, 0.7f, 1.0f },
                1.0f );
        }

        (void)app;
    }

    void ResultModule::render_portable_duet( App &app, rendering::Renderer &renderer ) const
    {
        const auto *common = app.texture_manager().get( common_texture_ );

        const auto *portraits = app.texture_manager().get( stage_texture_ );

        if( common == nullptr || portraits == nullptr )
        {
            renderer.draw_text( 24.0f, 24.0f, status_.c_str(), { 1.0f, 0.7f, 0.7f, 1.0f } );
            return;
        }

        const auto layout = rendering::make_logical_viewport( renderer, 480.0f, 272.0f );

        const auto draw = [ & ]( const rendering::Texture &texture,
                              const rendering::Rect &source,
                              const rendering::Rect &destination,
                              float alpha = 1.0f )
        {
            const auto target = layout.rect( destination.x, destination.y, destination.w, destination.h );
            renderer.draw_texture_region( texture,
                source.x,
                source.y,
                source.w,
                source.h,
                target.x,
                target.y,
                target.w,
                target.h,
                { 1.0f, 1.0f, 1.0f, alpha } );
        };

        const float opening = reveal_state_ == RevealState::Opening
                                  ? std::clamp( static_cast<float>( state_tick_ ) / 30.0f, 0.0f, 1.0f )
                                  : 1.0f;

        draw( *common, atlas_.background, { 0.0f, 0.0f, 480.0f, 272.0f }, opening );

        if( reveal_state_ == RevealState::Opening )
        {
            return;
        }

        const float slide = reveal_state_ == RevealState::StagePanel
                                ? 1.0f - std::clamp( static_cast<float>( state_tick_ ) / 15.0f, 0.0f, 1.0f )
                                : 0.0f;

        draw( *portraits, atlas_.portraits[ 0 ], { 64.0f - slide * 166.0f, 69.0f, 92.0f, 71.0f } );
        draw( *portraits, atlas_.portraits[ 1 ], { 324.0f + slide * 162.0f, 69.0f, 92.0f, 71.0f } );

        if( reveal_state_ == RevealState::StagePanel )
        {
            return;
        }

        draw( *common, { 404.0f, 448.0f, 64.0f, 17.0f }, { 18.0f, 15.0f, 82.0f, 22.0f } );

        const auto draw_number = [ & ]( std::uint32_t value, float x, float y, float height )
        {
            const auto *digits = app.texture_manager().get( digit_texture_ );

            if( digits == nullptr || digit_atlas_.digit.h <= 0.0f )
            {
                return;
            }

            const auto number = std::to_string( value );
            const float width = height * digit_atlas_.digit.w / digit_atlas_.digit.h;

            for( std::size_t index = 0; index < number.size(); ++index )
            {
                auto source = digit_atlas_.digit;
                source.x += static_cast<float>( number[ index ] - '0' ) * digit_atlas_.digit_stride;
                draw( *digits, source, { x + static_cast<float>( index ) * width, y, width, height } );
            }
        };

        draw_number( std::min( display_score_, 9999999u ), 108.0f, 17.0f, 17.0f );

        for( std::size_t index = 0; index < stat_panels_.size(); ++index )
        {
            const float x = index < 2 ? 22.0f : 268.0f;
            const float y = 210.0f + static_cast<float>( index % 2 ) * 18.0f;
            const float source_y = 273.0f + static_cast<float>( index ) * 18.0f;
            draw( *common, { 0.0f, source_y, 56.0f, 17.0f }, { x, y, 68.0f, 16.0f } );

            draw_number( displayed_stat_count( index ), x + 76.0f, y, 14.0f );
        }

        if( reveal_state_ == RevealState::Rank || reveal_state_ == RevealState::NoMiss ||
            reveal_state_ == RevealState::Ready || reveal_state_ == RevealState::Exiting )
        {
            const auto &rank = runtime::stage::result_rank_rect( atlas_, rank_index_ );
            draw( *common, rank, { 217.0f, 151.0f, 46.0f, 38.0f } );
        }

        if( no_miss_ && ( reveal_state_ == RevealState::NoMiss || reveal_state_ == RevealState::Ready ) )
        {
            localization::draw_label( renderer,
                app.texture_manager().get( no_miss_label_texture_ ),
                layout.rect( 180.0f, 248.0f, 120.0f, 18.0f ) );
        }
    }

    void ResultModule::render_background_layers( rendering::Renderer &renderer ) const
    {
        if( renderer.output_width() <= 0 || renderer.output_height() <= 0 )
        {
            return;
        }

        const auto layout = result_viewport( renderer );

        const rendering::Vec2 center{ layout.to_x( background_center_x_ ), layout.to_y( background_center_y_ ) };
        const float screen_width = static_cast<float>( renderer.output_width() );
        const float screen_height = static_cast<float>( renderer.output_height() );
        const float corner_x = std::max( center.x, screen_width - center.x ) + background_edge_margin_;
        const float corner_y = std::max( center.y, screen_height - center.y ) + background_edge_margin_;

        const auto &outer_layer = background_layers_.front();
        const float outer_radius_x = outer_layer.width * layout.scale * 0.5f;
        const float outer_radius_y = outer_layer.height * layout.scale * 0.5f;
        const float corner_ratio_x = corner_x / outer_radius_x;
        const float corner_ratio_y = corner_y / outer_radius_y;
        const float corner_distance = std::sqrt( corner_ratio_x * corner_ratio_x + corner_ratio_y * corner_ratio_y );
        const float outer_scale = std::max( 1.0f, corner_distance );

        for( std::size_t index = 0; index < background_layers_.size(); ++index )
        {
            const auto &layer = background_layers_[ index ];
            float progress = 1.0f;

            if( reveal_state_ == RevealState::Opening )
            {
                const std::size_t reveal_index = background_layers_.size() - 1 - index;
                const float begin_tick = static_cast<float>( reveal_index * 10 );
                progress = std::clamp( ( static_cast<float>( state_tick_ ) - begin_tick ) / 10.0f, 0.0f, 1.0f );
            }

            if( progress <= 0.0f )
            {
                continue;
            }

            rendering::Color tint = layer.tint;
            tint.a *= progress;

            const float layer_scale = index == 0 ? outer_scale : 1.0f;
            const float width = layer.width * progress * layout.scale * layer_scale;
            const float height = layer.height * progress * layout.scale * layer_scale;

            renderer.draw_filled_ellipse( { center.x - width * 0.5f, center.y - height * 0.5f, width, height }, tint );
        }
    }

    void ResultModule::render_common_base( App &app, rendering::Renderer &renderer ) const
    {
        const rendering::Texture *texture = app.texture_manager().get( common_texture_ );

        if( texture == nullptr )
        {
            return;
        }

        if( reveal_state_ == RevealState::Opening )
        {
            return;
        }

        float center_y = 175.0f;

        if( reveal_state_ == RevealState::StagePanel )
        {
            const float progress = std::clamp( static_cast<float>( state_tick_ ) / 15.0f, 0.0f, 1.0f );
            center_y -= ( 1.0f - progress ) * 300.0f;
        }

        const float top = center_y - 213.5f;

        const auto &base = atlas_.common_base;
        draw_region( renderer,
            *texture,
            base.x,
            base.y,
            base.w,
            base.h,
            89.0f,
            top,
            462.0f,
            427.0f,
            rendering::Color{} );
    }

    void ResultModule::render_stage_panel( App &app, rendering::Renderer &renderer ) const
    {
        const rendering::Texture *texture = app.texture_manager().get( stage_texture_ );

        if( texture == nullptr )
        {
            return;
        }

        float vertical_offset = 0.0f;

        if( reveal_state_ == RevealState::StagePanel )
        {
            const float progress = std::clamp( static_cast<float>( state_tick_ ) / 15.0f, 0.0f, 1.0f );
            vertical_offset = ( 1.0f - progress ) * 288.0f;
        }

        const rendering::Color tint{ 1.0f, 1.0f, 1.0f, 1.0f };
        draw_region( renderer,
            *texture,
            atlas_.stage_emblem.x,
            atlas_.stage_emblem.y,
            atlas_.stage_emblem.w,
            atlas_.stage_emblem.h,
            202.0f,
            266.0f + vertical_offset,
            236.0f,
            236.0f,
            tint );

        draw_region( renderer,
            *texture,
            atlas_.stage_title.x,
            atlas_.stage_title.y,
            atlas_.stage_title.w,
            atlas_.stage_title.h,
            206.0f,
            235.0f + vertical_offset,
            230.0f,
            191.0f,
            tint );
    }

    void ResultModule::render_energy_gauge( App &app, rendering::Renderer &renderer ) const
    {
        const rendering::Texture *texture = app.texture_manager().get( itfc_texture_ );

        if( texture == nullptr )
        {
            return;
        }

        const float alpha = reveal_state_ == RevealState::StatsIntro
                                ? std::clamp( static_cast<float>( state_tick_ ) / 12.0f, 0.0f, 1.0f )
                                : 1.0f;

        const float energy = reveal_state_ == RevealState::EnergyTally || reveal_state_ == RevealState::Rank ||
                                     reveal_state_ == RevealState::NoMiss || reveal_state_ == RevealState::Ready ||
                                     reveal_state_ == RevealState::Exiting
                                 ? static_cast<float>( std::max( 0, display_energy_units_ ) ) / 100.0f
                                 : std::clamp( load_data_.result.final_energy, 0.0f, 1.0f );

        const float fill_width = 205.0f * energy;
        const rendering::Color tint{ 1.0f, 1.0f, 1.0f, alpha };

        if( fill_width > 0.0f )
        {
            draw_region( renderer,
                *texture,
                runtime::stage::gameplay_interface_atlas::energy_fill.x,
                runtime::stage::gameplay_interface_atlas::energy_fill.y,
                runtime::stage::gameplay_interface_atlas::energy_fill.w,
                runtime::stage::gameplay_interface_atlas::energy_fill.h,
                246.0f - fill_width,
                23.0f,
                fill_width,
                17.0f,
                tint );
        }

        draw_region( renderer,
            *texture,
            runtime::stage::gameplay_interface_atlas::energy_housing_left_cap.x,
            runtime::stage::gameplay_interface_atlas::energy_housing_left_cap.y,
            runtime::stage::gameplay_interface_atlas::energy_housing_left_cap.w,
            runtime::stage::gameplay_interface_atlas::energy_housing_left_cap.h,
            10.0f,
            15.0f,
            33.0f,
            40.0f,
            tint );

        draw_region( renderer,
            *texture,
            runtime::stage::gameplay_interface_atlas::energy_housing_middle.x,
            runtime::stage::gameplay_interface_atlas::energy_housing_middle.y,
            runtime::stage::gameplay_interface_atlas::energy_housing_middle.w,
            runtime::stage::gameplay_interface_atlas::energy_housing_middle.h,
            43.0f,
            15.0f,
            200.0f,
            40.0f,
            tint );

        draw_region( renderer,
            *texture,
            runtime::stage::gameplay_interface_atlas::energy_housing_right_cap.x,
            runtime::stage::gameplay_interface_atlas::energy_housing_right_cap.y,
            runtime::stage::gameplay_interface_atlas::energy_housing_right_cap.w,
            runtime::stage::gameplay_interface_atlas::energy_housing_right_cap.h,
            243.0f,
            15.0f,
            33.0f,
            40.0f,
            tint );

        draw_region( renderer,
            *texture,
            runtime::stage::gameplay_interface_atlas::energy_marker.x,
            runtime::stage::gameplay_interface_atlas::energy_marker.y,
            runtime::stage::gameplay_interface_atlas::energy_marker.w,
            runtime::stage::gameplay_interface_atlas::energy_marker.h,
            229.0f - 205.0f * energy,
            -18.0f,
            32.0f,
            56.0f,
            tint );
    }

    void ResultModule::render_score( App &app, rendering::Renderer &renderer ) const
    {
        const rendering::Texture *texture = app.texture_manager().get( common_texture_ );

        if( texture == nullptr )
        {
            return;
        }

        const float alpha = reveal_state_ == RevealState::StatsIntro
                                ? std::clamp( static_cast<float>( state_tick_ ) / 12.0f, 0.0f, 1.0f )
                                : 1.0f;

        const rendering::Color tint{ 1.0f, 1.0f, 1.0f, alpha };

        draw_region( renderer,
            *texture,
            atlas_.score_label.x,
            atlas_.score_label.y,
            atlas_.score_label.w,
            atlas_.score_label.h,
            311.0f,
            12.0f,
            318.0f,
            47.0f,
            tint );

        std::uint32_t value = std::min<std::uint32_t>( display_score_, 9999999u );
        std::array<std::int32_t, 7> digits{};

        for( std::int32_t index = 6; index >= 0; --index )
        {
            digits[ static_cast<std::size_t>( index ) ] = static_cast<std::int32_t>( value % 10u );

            value /= 10u;
        }

        for( std::size_t index = 0; index < digits.size(); ++index )
        {
            draw_result_digit_at( renderer,
                *texture,
                atlas_,
                digits[ index ],
                435.0f + static_cast<float>( index ) * 24.0f,
                24.0f,
                alpha );
        }
    }

    void ResultModule::render_statistics( App &app, rendering::Renderer &renderer ) const
    {
        const rendering::Texture *common_texture = app.texture_manager().get( common_texture_ );
        const rendering::Texture *stage_texture = app.texture_manager().get( stage_texture_ );

        if( common_texture == nullptr || stage_texture == nullptr )
        {
            return;
        }

        const float alpha = reveal_state_ == RevealState::StatsIntro
                                ? std::clamp( static_cast<float>( state_tick_ ) / 24.0f, 0.0f, 1.0f )
                                : 1.0f;

        const rendering::Color tint{ 1.0f, 1.0f, 1.0f, alpha };

        for( std::size_t index = 0; index < stat_panels_.size(); ++index )
        {
            const auto &panel = stat_panels_[ index ];

            draw_region( renderer,
                *stage_texture,
                atlas_.stat_panel.x,
                atlas_.stat_panel.y,
                atlas_.stat_panel.w,
                atlas_.stat_panel.h,
                panel.base_x,
                panel.base_y,
                262.0f,
                262.0f,
                tint );

            const auto &label = atlas_.stat_labels[ index ];
            draw_region( renderer,
                *common_texture,
                label.x,
                label.y,
                label.w,
                label.h,
                panel.base_x + 83.0f,
                panel.base_y + 83.0f,
                94.0f,
                22.0f,
                tint );

            draw_three_digits_at( renderer,
                *common_texture,
                atlas_,
                static_cast<std::int32_t>( displayed_stat_count( index ) ),
                panel.base_x + 94.0f,
                panel.base_y + 113.0f,
                alpha );

            draw_region( renderer,
                *common_texture,
                atlas_.punctuation.x,
                atlas_.punctuation.y,
                atlas_.punctuation.w,
                atlas_.punctuation.h,
                panel.base_x + 128.0f,
                panel.base_y + 134.0f,
                5.0f,
                22.0f,
                tint );

            if( percentages_[ index ] >= 100 )
            {
                draw_three_digits_at( renderer,
                    *common_texture,
                    atlas_,
                    percentages_[ index ],
                    panel.base_x + 66.0f,
                    panel.base_y + 160.0f,
                    alpha );
            }
            else
            {
                draw_two_digits_at( renderer,
                    *common_texture,
                    atlas_,
                    percentages_[ index ],
                    panel.base_x + 90.0f,
                    panel.base_y + 160.0f,
                    alpha );
            }

            draw_region( renderer,
                *common_texture,
                atlas_.percent.x,
                atlas_.percent.y,
                atlas_.percent.w,
                atlas_.percent.h,
                panel.base_x + 137.0f,
                panel.base_y + 160.0f,
                32.0f,
                17.0f,
                tint );
        }
    }

    void ResultModule::render_rank( App &app, rendering::Renderer &renderer ) const
    {
        const rendering::Texture *texture = app.texture_manager().get( stage_texture_ );

        if( texture == nullptr )
        {
            return;
        }

        float progress = 1.0f;

        if( reveal_state_ == RevealState::Rank )
        {
            progress = std::clamp( static_cast<float>( state_tick_ ) / 28.0f, 0.0f, 1.0f );
        }

        const float oscillation = std::sin( progress * 3.14159265f * 2.5f ) * ( 1.0f - progress );
        const float scale = 1.0f + ( 1.0f - progress ) * 0.75f + oscillation * 0.18f;
        const float ghost_scale = scale + 0.22f;
        const float ghost_alpha = std::max( 0.0f, ( 1.0f - progress ) * 0.45f );
        const rendering::Rect &source = runtime::stage::result_rank_rect( atlas_, rank_index_ );

        if( ghost_alpha > 0.0f )
        {
            draw_region_centered( renderer,
                *texture,
                source.x,
                source.y,
                source.w,
                source.h,
                320.0f,
                184.0f,
                source.w * ghost_scale,
                source.h * ghost_scale,
                rendering::Color{ 1.0f, 1.0f, 1.0f, ghost_alpha } );
        }

        draw_region_centered( renderer,
            *texture,
            source.x,
            source.y,
            source.w,
            source.h,
            320.0f,
            184.0f,
            source.w * scale,
            source.h * scale,
            rendering::Color{ 1.0f, 1.0f, 1.0f, std::min( 1.0f, progress * 2.0f ) } );
    }

    void ResultModule::render_no_miss( App &app, rendering::Renderer &renderer ) const
    {
        const auto layout = result_viewport( renderer );
        localization::draw_label( renderer,
            app.texture_manager().get( no_miss_label_texture_ ),
            layout.rect( 192.0f, no_miss_y_, 256.0f, 64.0f ) );
    }
}
