#include "Utils/EnumUtil.hpp"
#include "Localization/Language.hpp"
#include "CollectionModule.hpp"
#include "Localization/LabelTexture.hpp"

#include "App/piko.hpp"
#include "Managers/InputManager.hpp"
#include "Modules/MainMenu/MainMenuModule.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/RenderUtil.hpp"

#include <cstdint>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

namespace piko::modules
{

    const char *CollectionModule::reward_name( std::int32_t index )
    {
        return index >= 0 && index < static_cast<std::int32_t>( names_.size() )
                   ? localization::text( names_[ static_cast<std::size_t>( index ) ] )
                   : "";
    }

    std::optional<CollectionModule::RewardIcon> CollectionModule::reward_icon( std::int32_t index )
    {
        if( index < 0 || index >= static_cast<std::int32_t>( board_unlocked_pieces_.size() ) )
        {
            return std::nullopt;
        }

        const SpritePiece &piece = board_unlocked_pieces_[ static_cast<std::size_t>( index ) ];

        if( utils::enum_index( piece.texture_slot ) >= original_texture_paths_.size() )
        {
            return std::nullopt;
        }

        return RewardIcon{
            original_texture_paths_[ utils::enum_index( piece.texture_slot ) ],
            rendering::Rect{ piece.source_x, piece.source_y, piece.width, piece.height }
        };
    }

    void CollectionModule::enter( App &app )
    {
        const auto position = app.menu_navigation().restore( managers::MenuLocation::Collection, 30, 29 );
        selected_node_ = position.selected_row;

        board_side_ = board_side_for_node( selected_node_ );

        selected_item_ = selected_node_ < 28 ? selected_node_ : ( board_side_ == BoardSide::A ? 15 : 4 );

        board_offset_ = board_side_ == BoardSide::A ? 0.0f : -640.0f;

        board_target_offset_ = board_offset_;

        board_transitioning_ = false;

        elapsed_seconds_ = 0.0;

        figure_enter_seconds_ = 0.0;

        leaving_ = false;

        screen_ = Screen::Board;
        load_fixed_textures( app );
        detail_background_renderer_.initialize( app );
        detail_background_renderer_.set_palette( rendering::Color{ 0.37f, 0.62f, 0.14f, 1.0f },
            rendering::Color{ 0.89f, 0.96f, 0.12f, 1.0f },
            rendering::Color{ 0.97f, 0.99f, 0.97f, 1.0f } );

        initialize_audio( app );
        play_board_bgm( app );
    }

    void CollectionModule::leave( App &app )
    {
        const std::int32_t node = board_transitioning_ ? ( board_side_ == BoardSide::A ? 29 : 28 ) : selected_node_;
        app.menu_navigation().remember( managers::MenuLocation::Collection, node );

        unload_detail_textures( app );
        app.audio().release_channel( voice_channel_ );
        app.audio().release_channel( sfx_channel_ );
        app.audio().release_channel( bgm_channel_ );
        voice_channel_ = {};

        sfx_channel_ = {};

        bgm_channel_ = {};
        detail_background_renderer_.shutdown();
        unload_fixed_textures( app );
    }

    void CollectionModule::initialize_audio( App &app )
    {
        bool loaded = false;
        const std::filesystem::path main_menu = app.game_data().main_menu_audio_path();

        if( !main_menu.empty() )
        {
            loaded = app.audio().load_audio_bank( main_menu );
        }

        const std::filesystem::path character_manifest =
            app.game_data().resolve_runtime_asset( "SOUND/AUDIO/CHARACOL/audio.json" );

        if( !character_manifest.empty() )
        {
            if( loaded )
            {
                app.audio().merge_audio_bank( character_manifest.parent_path() );
            }
            else
            {
                loaded = app.audio().load_audio_bank( character_manifest.parent_path() );
            }
        }

        const std::filesystem::path portable_manifest =
            app.game_data().resolve_runtime_asset( "UI/COLLECTION/AUDIO/audio.json" );

        if( !portable_manifest.empty() )
        {
            if( loaded )
            {
                app.audio().merge_audio_bank( portable_manifest.parent_path() );
            }
            else
            {
                loaded = app.audio().load_audio_bank( portable_manifest.parent_path() );
            }
        }

        bgm_channel_ = app.audio().claim_channel( "Collection BGM" );

        sfx_channel_ = app.audio().claim_channel( "Collection SE" );

        voice_channel_ = app.audio().claim_channel( "Collection Voice" );
    }

    void CollectionModule::play_menu_sound( App &app, const char *name )
    {
        if( sfx_channel_ && app.audio().has_subsong( name ) )
        {
            app.audio().play_subsong( sfx_channel_, name, false );
        }
    }

    void CollectionModule::play_character_voice( App &app )
    {
        if( !voice_channel_ || selected_item_ < 0 || selected_item_ >= static_cast<std::int32_t>( names_.size() ) )
        {
            return;
        }

        if( !app.save_data().collection_unlocked( selected_item_ ) )
        {
            return;
        }

        const char *ps2_name = ps2_voice_cues_[ static_cast<std::size_t>( selected_item_ ) ];

        if( ps2_name != nullptr && app.audio().has_subsong( ps2_name ) )
        {
            app.audio().play_subsong( voice_channel_, ps2_name, false );
            return;
        }

        char portable_name[ 16 ]{};
        std::snprintf( portable_name,
            sizeof( portable_name ),
            "CHRCL%02d",
            voice_cues_[ static_cast<std::size_t>( selected_item_ ) ] );

        if( app.audio().has_subsong( portable_name ) )
        {
            app.audio().play_subsong( voice_channel_, portable_name, false );
        }
    }

    void CollectionModule::play_board_bgm( App &app )
    {
        if( !bgm_channel_ )
        {
            return;
        }

        app.audio().stop_channel( bgm_channel_ );
        app.audio().set_channel_stem_gains( bgm_channel_, managers::AudioStemGains{ 1.0f, 1.0f, 1.0f, 1.0f } );

        if( app.audio().has_subsong( "BGM_B2" ) )
        {
            app.audio().play_subsong( bgm_channel_, "BGM_B2", true );
        }
        else if( app.audio().has_subsong( "BGM_A2" ) )
        {
            app.audio().play_subsong( bgm_channel_, "BGM_A2", true );
        }
    }

    void CollectionModule::play_detail_bgm( App &app )
    {
        if( !bgm_channel_ )
        {
            return;
        }

        app.audio().stop_channel( bgm_channel_ );
        app.audio().set_channel_stem_gains( bgm_channel_,
            managers::AudioStemGains{ 0.579852f, 0.579852f, 0.579852f, 0.579852f } );

        if( app.audio().has_subsong( "BGM_D2" ) )
        {
            app.audio().play_subsong( bgm_channel_, "BGM_D2", true );
        }
        else if( app.audio().has_subsong( "BGM_B2" ) )
        {
            app.audio().play_subsong( bgm_channel_, "BGM_B2", true );
        }
    }

    bool CollectionModule::board_node_visible( App &, std::int32_t node ) const
    {
        if( node < 0 || node >= 30 )
        {
            return false;
        }

        if( node == 28 )
        {
            return board_side_ == BoardSide::B;
        }

        if( node == 29 )
        {
            return board_side_ == BoardSide::A;
        }

        return board_side_for_node( node ) == board_side_;
    }

    CollectionModule::BoardSide CollectionModule::board_side_for_node( std::int32_t node ) const
    {
        if( node == 28 )
        {
            return BoardSide::B;
        }

        if( node == 29 )
        {
            return BoardSide::A;
        }

        if( node < 0 || node >= static_cast<std::int32_t>( board_locked_pieces_.size() ) )
        {
            return board_side_;
        }

        return board_locked_pieces_[ static_cast<std::size_t>( node ) ].destination_x >= 640.0f ? BoardSide::B
                                                                                                : BoardSide::A;
    }

    void CollectionModule::move_board_selection( App &app, Direction direction )
    {
        if( utils::enum_index( direction ) >= 4 || board_transitioning_ )
        {
            return;
        }

        std::int32_t candidate = selected_node_;

        for( std::int32_t step = 0; step < 30; ++step )
        {
            candidate = board_neighbors_[ static_cast<std::size_t>( candidate ) ][ utils::enum_index( direction ) ];

            if( candidate < 0 )
            {
                return;
            }

            if( board_node_visible( app, candidate ) )
            {
                break;
            }
        }

        if( candidate < 0 || candidate == selected_node_ || !board_node_visible( app, candidate ) )
        {
            return;
        }

        selected_node_ = candidate;

        if( selected_node_ < 28 )
        {
            selected_item_ = selected_node_;
        }

        play_menu_sound( app, "SELECT" );
    }

    void CollectionModule::switch_board_side( App &app )
    {
        if( board_transitioning_ )
        {
            return;
        }

        if( selected_node_ == 29 && board_side_ == BoardSide::A )
        {
            board_side_ = BoardSide::B;

            board_target_offset_ = -640.0f;

            board_transitioning_ = true;
            play_menu_sound( app, "OK" );
        }
        else if( selected_node_ == 28 && board_side_ == BoardSide::B )
        {
            board_side_ = BoardSide::A;

            board_target_offset_ = 0.0f;

            board_transitioning_ = true;
            play_menu_sound( app, "OK" );
        }
    }

    void CollectionModule::open_figure( App &app )
    {
        if( selected_node_ >= 28 )
        {
            switch_board_side( app );
            return;
        }

        selected_item_ = selected_node_;

        if( !app.save_data().collection_unlocked( selected_item_ ) )
        {
            play_menu_sound( app, "CANCEL" );
            return;
        }

        load_detail_textures( app );
        screen_ = Screen::Figure;

        figure_enter_seconds_ = 0.0;
        play_menu_sound( app, "OK" );
        play_detail_bgm( app );
        play_character_voice( app );
    }

    void CollectionModule::close_figure( App &app )
    {
        screen_ = Screen::Board;
        app.audio().stop_channel( voice_channel_ );
        unload_detail_textures( app );
        play_menu_sound( app, "CANCEL" );
        play_board_bgm( app );
    }

    void CollectionModule::load_fixed_textures( App &app )
    {
        unload_fixed_textures( app );

        const auto load = [ &app ]( managers::TextureHandle &handle, const std::string &key, const char *relative )
        {
            const std::filesystem::path path = app.game_data().resolve_runtime_asset( relative );

            if( path.empty() )
            {
                return;
            }

            try
            {
                handle = app.texture_manager().load_png( key, path );
            }
            catch( const std::exception & )
            {
                handle = {};
            }
        };

        constexpr std::array<const char *, 4> portable_background_files{
            "UI/COLLECTION/COLLECT/BG_BLUE.png",
            "UI/COLLECTION/COLLECT/BG_GREEN.png",
            "UI/COLLECTION/COLLECT/BG_RED.png",
            "UI/COLLECTION/COLLECT/BG_YELLOW.png"
        };

        for( std::size_t index = 0; index < portable_background_textures_.size(); ++index )
        {
            load( portable_background_textures_[ index ],
                "collection/portable-background-" + std::to_string( index ),
                portable_background_files[ index ] );
        }

        load( portable_words_texture_, "collection/portable-words", "UI/COLLECTION/COLLECT/BG_WORDS.png" );

        for( std::size_t index = 0; index < original_fixed_textures_.size(); ++index )
        {
            load( original_fixed_textures_[ index ],
                "collection/original-" + std::to_string( index ),
                original_texture_paths_[ index ] );
        }

        load_portable_figures( app );
    }

    void CollectionModule::unload_fixed_textures( App &app )
    {
        unload_portable_figures( app );

        for( managers::TextureHandle &texture : portable_background_textures_ )
        {
            app.texture_manager().unload( texture );
            texture = {};
        }

        for( managers::TextureHandle &texture : original_fixed_textures_ )
        {
            app.texture_manager().unload( texture );
            texture = {};
        }

        app.texture_manager().unload( portable_words_texture_ );
        portable_words_texture_ = {};
    }

    void CollectionModule::load_portable_figures( App &app )
    {
        unload_portable_figures( app );

        for( std::int32_t item = 0; item < static_cast<std::int32_t>( portable_figure_textures_.size() ); ++item )
        {
            char relative[ 80 ]{};
            std::snprintf( relative, sizeof( relative ), "UI/COLLECTION/COLLECT/FIGURE_%d.png", item );
            const std::filesystem::path path = app.game_data().resolve_runtime_asset( relative );

            if( path.empty() )
            {
                continue;
            }

            try
            {
                portable_figure_textures_[ static_cast<std::size_t>( item ) ] =
                    app.texture_manager().load_png( "collection/portable-figure-" + std::to_string( item ), path );
            }
            catch( const std::exception & )
            {
                portable_figure_textures_[ static_cast<std::size_t>( item ) ] = {};
            }
        }
    }

    void CollectionModule::unload_portable_figures( App &app )
    {
        for( managers::TextureHandle &texture : portable_figure_textures_ )
        {
            app.texture_manager().unload( texture );
            texture = {};
        }
    }

    void CollectionModule::load_detail_textures( App &app )
    {
        unload_detail_textures( app );
        detail_name_texture_ = localization::make_label( app.texture_manager(),
            names_[ static_cast<std::size_t>( selected_item_ ) ],
            "collection_name" );

        const std::int32_t file_index = selected_item_ + 1;
        char character_relative[ 96 ]{};
        char text_relative[ 96 ]{};
        std::snprintf( character_relative,
            sizeof( character_relative ),
            "CCOLLE/TEXTURES/CHR_%02d/CHARA_%02d.png",
            file_index,
            file_index );

        std::snprintf( text_relative,
            sizeof( text_relative ),
            "CCOLLE/TEXTURES/CHR_%02d/CTEXT_%02d.png",
            file_index,
            file_index );

        const auto load = [ &app ]( managers::TextureHandle &handle, const std::string &key, const char *relative )
        {
            const std::filesystem::path path = app.game_data().resolve_runtime_asset( relative );

            if( path.empty() )
            {
                return;
            }

            try
            {
                handle = app.texture_manager().load_png( key, path );
            }
            catch( const std::exception & )
            {
                handle = {};
            }
        };

        load( detail_character_texture_,
            "collection/detail-character-" + std::to_string( selected_item_ ),
            character_relative );

        load( detail_text_texture_, "collection/detail-text-" + std::to_string( selected_item_ ), text_relative );
    }

    void CollectionModule::unload_detail_textures( App &app )
    {
        app.texture_manager().unload( detail_name_texture_ );
        detail_name_texture_ = {};

        app.texture_manager().unload( detail_character_texture_ );
        app.texture_manager().unload( detail_text_texture_ );
        detail_character_texture_ = {};

        detail_text_texture_ = {};
    }

    bool CollectionModule::has_original_board( App &app ) const
    {
        for( std::size_t index = 0; index < 4; ++index )
        {
            if( app.texture_manager().get( original_fixed_textures_[ index ] ) == nullptr )
            {
                return false;
            }
        }

        return true;
    }

    bool CollectionModule::has_original_figure( App &app ) const
    {
        return app.texture_manager().get( original_fixed_textures_[ 4 ] ) != nullptr &&
               app.texture_manager().get( detail_character_texture_ ) != nullptr &&
               app.texture_manager().get( detail_text_texture_ ) != nullptr;
    }

    void CollectionModule::draw_piece( App &app,
        rendering::Renderer &renderer,
        const rendering::LogicalViewport &layout,
        const SpritePiece &piece,
        float offset_x ) const
    {
        if( utils::enum_index( piece.texture_slot ) >= original_fixed_textures_.size() )
        {
            return;
        }

        const rendering::Texture *texture =
            app.texture_manager().get( original_fixed_textures_[ utils::enum_index( piece.texture_slot ) ] );

        if( texture == nullptr )
        {
            return;
        }

        renderer.draw_texture_region( *texture,
            piece.source_x,
            piece.source_y,
            piece.width,
            piece.height,
            layout.x + ( piece.destination_x + offset_x ) * layout.scale,
            layout.y + piece.destination_y * layout.scale,
            piece.width * layout.scale,
            piece.height * layout.scale );
    }

    void CollectionModule::update( App &app, double delta_seconds )
    {
        if( leaving_ )
        {
            return;
        }

        const double delta = std::max( 0.0, delta_seconds );
        elapsed_seconds_ += delta;

        figure_enter_seconds_ += delta;

        if( board_transitioning_ )
        {
            const float step = static_cast<float>( delta * 960.0 );

            if( board_offset_ < board_target_offset_ )
            {
                board_offset_ = std::min( board_offset_ + step, board_target_offset_ );
            }
            else
            {
                board_offset_ = std::max( board_offset_ - step, board_target_offset_ );
            }

            if( std::abs( board_offset_ - board_target_offset_ ) <= 0.01f )
            {
                board_offset_ = board_target_offset_;

                board_transitioning_ = false;

                selected_node_ = board_side_ == BoardSide::A ? 29 : 28;

                selected_item_ = board_side_ == BoardSide::A ? 15 : 4;
            }

            return;
        }

        const managers::InputManager &input = app.input();

        if( screen_ == Screen::Board )
        {
            if( input.pressed( managers::InputAction::MenuUp ) )
            {
                move_board_selection( app, Direction::Up );
            }

            if( input.pressed( managers::InputAction::MenuDown ) )
            {
                move_board_selection( app, Direction::Down );
            }

            if( input.pressed( managers::InputAction::MenuLeft ) )
            {
                move_board_selection( app, Direction::Left );
            }

            if( input.pressed( managers::InputAction::MenuRight ) )
            {
                move_board_selection( app, Direction::Right );
            }

            if( input.pressed( managers::InputAction::Confirm ) )
            {
                open_figure( app );
            }

            if( input.pressed( managers::InputAction::Back ) )
            {
                leaving_ = true;
                play_menu_sound( app, "CANCEL" );
                app.change_module( std::make_unique<MainMenuModule>() );
            }

            return;
        }

        if( input.pressed( managers::InputAction::Confirm ) )
        {
            play_character_voice( app );
        }

        if( input.pressed( managers::InputAction::Back ) )
        {
            close_figure( app );
        }
    }

    void CollectionModule::render( App &app, rendering::Renderer &renderer )
    {
        if( screen_ == Screen::Board )
        {
            render_board( app, renderer );
        }
        else
        {
            render_figure( app, renderer );
        }
    }

    void CollectionModule::render_board( App &app, rendering::Renderer &renderer )
    {
        if( has_original_board( app ) )
        {
            render_original_board( app, renderer );
        }
        else
        {
            render_portable_board( app, renderer );
        }
    }

    void CollectionModule::render_original_board( App &app, rendering::Renderer &renderer )
    {
        const auto layout = rendering::make_logical_viewport( renderer );
        render_original_figure_background( renderer );

        for( std::size_t index = 2; index < board_frame_pieces_.size(); ++index )
        {
            draw_piece( app, renderer, layout, board_frame_pieces_[ index ], board_offset_ );
        }

        for( const SpritePiece &piece : board_title_pieces_ )
        {
            draw_piece( app, renderer, layout, piece, board_offset_ );
        }

        for( std::size_t index = 0; index < board_locked_pieces_.size(); ++index )
        {
            draw_piece( app, renderer, layout, board_locked_pieces_[ index ], board_offset_ );

            if( app.save_data().collection_unlocked( static_cast<std::int32_t>( index ) ) )
            {
                draw_piece( app, renderer, layout, board_unlocked_pieces_[ index ], board_offset_ );
            }
        }

        draw_piece( app, renderer, layout, board_frame_pieces_[ 0 ], board_offset_ );
        draw_piece( app, renderer, layout, board_frame_pieces_[ 1 ], board_offset_ );

        for( const SpritePiece &piece : board_side_switch_pieces_ )
        {
            draw_piece( app, renderer, layout, piece, board_offset_ );
        }

        const SpritePiece &selected_piece =
            selected_node_ < 28 ? board_locked_pieces_[ static_cast<std::size_t>( selected_node_ ) ]
                                : board_side_switch_pieces_[ static_cast<std::size_t>( selected_node_ - 28 ) ];

        const rendering::Texture *cursor_texture = app.texture_manager().get( original_fixed_textures_[ 2 ] );

        if( cursor_texture != nullptr )
        {
            const float wobble = std::sin( static_cast<float>( elapsed_seconds_ ) * 6.0f ) * 3.0f;
            const float center_y = selected_piece.destination_y + selected_piece.height * 0.5f - 9.0f;
            const float left_x = selected_piece.destination_x - 27.0f - wobble + board_offset_;
            const float right_x = selected_piece.destination_x + selected_piece.width + 4.0f + wobble + board_offset_;
            renderer.draw_texture_region( *cursor_texture,
                0.0f,
                48.0f,
                23.0f,
                18.0f,
                layout.x + left_x * layout.scale,
                layout.y + center_y * layout.scale,
                23.0f * layout.scale,
                18.0f * layout.scale );

            renderer.draw_texture_region( *cursor_texture,
                23.0f,
                48.0f,
                23.0f,
                18.0f,
                layout.x + right_x * layout.scale,
                layout.y + center_y * layout.scale,
                23.0f * layout.scale,
                18.0f * layout.scale );
        }
    }

    std::int32_t CollectionModule::portable_page_slot( std::int32_t item ) const
    {
        if( board_side_ == BoardSide::A )
        {
            for( std::size_t index = 0; index < side_a_items_.size(); ++index )
            {
                if( side_a_items_[ index ] == item )
                {
                    return static_cast<std::int32_t>( index );
                }
            }

            return -1;
        }

        for( std::size_t index = 0; index < side_b_items_.size(); ++index )
        {
            if( side_b_items_[ index ] == item )
            {
                return static_cast<std::int32_t>( index );
            }
        }

        return -1;
    }

    void CollectionModule::render_portable_board( App &app, rendering::Renderer &renderer )
    {
        const auto layout = rendering::make_logical_viewport( renderer );
        renderer.draw_filled_rect(
            rendering::Rect{
                0.0f,
                0.0f,
                static_cast<float>( renderer.output_width() ),
                static_cast<float>( renderer.output_height() )
            },
            rendering::Color{ 0.01f, 0.025f, 0.07f, 1.0f } );

        const rendering::Texture *background =
            app.texture_manager().get( portable_background_textures_[ utils::enum_index(
                board_side_ == BoardSide::A ? PortableBackground::Blue : PortableBackground::Red ) ] );

        if( background != nullptr )
        {
            renderer.draw_texture( *background, layout.x, layout.y, 640.0f * layout.scale, 448.0f * layout.scale );
        }

        const rendering::Texture *words = app.texture_manager().get( portable_words_texture_ );

        if( words != nullptr )
        {
            renderer.draw_texture( *words,
                layout.x,
                layout.y,
                640.0f * layout.scale,
                448.0f * layout.scale,
                rendering::Color{ 1.0f, 1.0f, 1.0f, 0.28f } );
        }

        const auto draw_item = [ & ]( std::int32_t item, std::int32_t slot )
        {
            const std::int32_t column = slot % 5;
            const std::int32_t row = slot / 5;
            const rendering::Rect card = layout.rect( 72.0f + static_cast<float>( column ) * 101.0f,
                88.0f + static_cast<float>( row ) * 95.0f,
                90.0f,
                82.0f );

            const bool unlocked = app.save_data().collection_unlocked( item );
            const bool selected = selected_node_ == item;
            renderer.draw_filled_rect( card,
                selected ? rendering::Color{ 0.16f, 0.36f, 0.58f, 0.88f }
                         : rendering::Color{ 0.01f, 0.04f, 0.09f, 0.68f } );

            const rendering::Texture *figure =
                app.texture_manager().get( portable_figure_textures_[ static_cast<std::size_t>( item ) ] );

            if( figure != nullptr )
            {
                const float fit =
                    std::min( ( card.w - 8.0f * layout.scale ) / static_cast<float>( std::max( figure->width, 1 ) ),
                        ( card.h - 8.0f * layout.scale ) / static_cast<float>( std::max( figure->height, 1 ) ) );

                const float draw_w = static_cast<float>( figure->width ) * fit;
                const float draw_h = static_cast<float>( figure->height ) * fit;
                renderer.draw_texture( *figure,
                    card.x + ( card.w - draw_w ) * 0.5f,
                    card.y + ( card.h - draw_h ) * 0.5f,
                    draw_w,
                    draw_h,
                    unlocked ? rendering::Color{ 1.0f, 1.0f, 1.0f, 1.0f }
                             : rendering::Color{ 0.12f, 0.14f, 0.18f, 0.72f } );
            }
        };

        if( board_side_ == BoardSide::A )
        {
            for( std::size_t index = 0; index < side_a_items_.size(); ++index )
            {
                draw_item( side_a_items_[ index ], static_cast<std::int32_t>( index ) );
            }
        }
        else
        {
            for( std::size_t index = 0; index < side_b_items_.size(); ++index )
            {
                draw_item( side_b_items_[ index ], static_cast<std::int32_t>( index ) );
            }
        }

        renderer.draw_text( layout.x + 24.0f * layout.scale,
            layout.y + 20.0f * layout.scale,
            board_side_ == BoardSide::A ? localization::text( "@ui.collection_side_a_upper" )
                                        : localization::text( "@ui.collection_side_b_upper" ),
            rendering::Color{ 1.0f, 0.95f, 0.74f, 1.0f },
            2.0f * layout.scale );

        if( selected_node_ >= 28 )
        {
            renderer.draw_text( layout.x + 520.0f * layout.scale,
                layout.y + 22.0f * layout.scale,
                board_side_ == BoardSide::A ? localization::text( "@ui.turn_upper" )
                                            : localization::text( "@ui.turn_upper_label" ),
                rendering::Color{ 0.8f, 0.9f, 1.0f, 1.0f },
                1.2f * layout.scale );
        }
        else
        {
            const bool unlocked = app.save_data().collection_unlocked( selected_item_ );
            renderer.draw_text( layout.x + 24.0f * layout.scale,
                layout.y + 394.0f * layout.scale,
                unlocked ? names_[ static_cast<std::size_t>( selected_item_ ) ] : "?????",
                rendering::Color{ 1.0f, 1.0f, 0.94f, 1.0f },
                1.5f * layout.scale );
        }
    }

    void CollectionModule::render_original_figure_background( rendering::Renderer &renderer )
    {
        renderer.draw_filled_rect(
            rendering::Rect{
                0.0f,
                0.0f,
                static_cast<float>( renderer.output_width() ),
                static_cast<float>( renderer.output_height() )
            },
            rendering::Color{ 0.62f, 0.84f, 0.18f, 1.0f } );

        if( !detail_background_renderer_.render( renderer, static_cast<float>( elapsed_seconds_ * 0.55 ) ) )
        {
            renderer.draw_filled_rect(
                rendering::Rect{
                    0.0f,
                    0.0f,
                    static_cast<float>( renderer.output_width() ),
                    static_cast<float>( renderer.output_height() )
                },
                rendering::Color{ 0.48f, 0.78f, 0.17f, 1.0f } );
        }
    }

    void CollectionModule::render_figure( App &app, rendering::Renderer &renderer )
    {
        if( has_original_figure( app ) )
        {
            render_original_figure( app, renderer );
        }
        else
        {
            render_portable_figure( app, renderer );
        }
    }

    rendering::Rect CollectionModule::detail_name_bounds( const DetailPlacement &placement )
    {
        const auto &heading = detail_foreground_pieces_[ 0 ];
        const float x = placement.text_bottom_x;
        const float y = placement.text_bottom_y;
        float width = std::min( detail_name_maximum_width, 640.0f - detail_name_margin - x );

        if( y < heading.destination_y + heading.height && y + detail_name_height > heading.destination_y )
        {
            width = std::min( width, heading.destination_x - detail_name_margin - x );
        }

        return rendering::Rect{ x, y, std::max( 0.0f, width ), detail_name_height };
    }

    void CollectionModule::render_original_figure( App &app, rendering::Renderer &renderer )
    {
        const auto layout = rendering::make_logical_viewport( renderer );
        render_original_figure_background( renderer );

        float background_scroll = -1280.0f + std::fmod( static_cast<float>( figure_enter_seconds_ ) * 14.4f, 1280.0f );

        if( background_scroll >= 0.0f )
        {
            background_scroll -= 1280.0f;
        }

        for( const SpritePiece &piece : detail_background_pieces_ )
        {
            draw_piece( app, renderer, layout, piece, background_scroll );
        }

        const DetailPlacement &placement = detail_placements_[ static_cast<std::size_t>( selected_item_ ) ];
        const rendering::Texture *character = app.texture_manager().get( detail_character_texture_ );
        const rendering::Texture *text = app.texture_manager().get( detail_text_texture_ );
        const float alpha = std::clamp( static_cast<float>( figure_enter_seconds_ * 5.0 ), 0.0f, 1.0f );
        const rendering::Color tint{ 1.0f, 1.0f, 1.0f, alpha };

        if( character != nullptr )
        {
            const float circle_width = placement.layout == FigureLayout::ExpandedText ? 179.0f : 358.0f;
            const float circle_height = placement.layout == FigureLayout::ExpandedText ? 163.0f : 326.0f;
            renderer.draw_texture_region( *character,
                330.0f,
                346.0f,
                179.0f,
                163.0f,
                layout.x + placement.character_large_x * layout.scale,
                layout.y + placement.character_large_y * layout.scale,
                circle_width * layout.scale,
                circle_height * layout.scale,
                tint );
        }

        if( text != nullptr )
        {
            const float text_width = placement.layout == FigureLayout::ExpandedText ? 728.0f : 364.0f;
            const float text_height = placement.layout == FigureLayout::ExpandedText ? 740.0f : 370.0f;
            renderer.draw_texture_region( *text,
                3.0f,
                3.0f,
                364.0f,
                370.0f,
                layout.x + placement.text_large_x * layout.scale,
                layout.y + placement.text_large_y * layout.scale,
                text_width * layout.scale,
                text_height * layout.scale,
                tint );
        }

        if( character != nullptr )
        {
            const float character_width = placement.layout == FigureLayout::Portrait ? 327.0f : 506.0f;
            const float character_height = placement.layout == FigureLayout::Portrait ? 506.0f : 343.0f;
            renderer.draw_texture_region( *character,
                0.0f,
                0.0f,
                character_width,
                character_height,
                layout.x + placement.character_small_x * layout.scale,
                layout.y + placement.character_small_y * layout.scale,
                character_width * layout.scale,
                character_height * layout.scale,
                tint );
        }

        localization::draw_label( renderer,
            app.texture_manager().get( detail_name_texture_ ),
            layout.rect( detail_name_bounds( placement ) ),
            tint,
            false );

        for( const SpritePiece &piece : detail_foreground_pieces_ )
        {
            draw_piece( app, renderer, layout, piece );
        }
    }

    void CollectionModule::render_portable_figure( App &app, rendering::Renderer &renderer )
    {
        const auto layout = rendering::make_logical_viewport( renderer );
        renderer.draw_filled_rect(
            rendering::Rect{
                0.0f,
                0.0f,
                static_cast<float>( renderer.output_width() ),
                static_cast<float>( renderer.output_height() )
            },
            rendering::Color{ 0.01f, 0.02f, 0.06f, 1.0f } );

        const PortableBackground background_color = background_colors_[ static_cast<std::size_t>( selected_item_ ) ];
        const rendering::Texture *background =
            app.texture_manager().get( portable_background_textures_[ utils::enum_index( background_color ) ] );

        if( background != nullptr )
        {
            renderer.draw_texture( *background, layout.x, layout.y, 640.0f * layout.scale, 448.0f * layout.scale );
        }

        const rendering::Texture *figure =
            app.texture_manager().get( portable_figure_textures_[ static_cast<std::size_t>( selected_item_ ) ] );

        if( figure != nullptr )
        {
            const rendering::Rect bounds = layout.rect( 70.0f, 24.0f, 500.0f, 360.0f );
            const float fit = std::min( bounds.w / static_cast<float>( std::max( figure->width, 1 ) ),
                bounds.h / static_cast<float>( std::max( figure->height, 1 ) ) );

            const float draw_w = static_cast<float>( figure->width ) * fit;
            const float draw_h = static_cast<float>( figure->height ) * fit;
            renderer.draw_texture( *figure,
                bounds.x + ( bounds.w - draw_w ) * 0.5f,
                bounds.y + ( bounds.h - draw_h ) * 0.5f,
                draw_w,
                draw_h );
        }

        localization::draw_label( renderer,
            app.texture_manager().get( detail_name_texture_ ),
            layout.rect( 30.0f, 396.0f, 580.0f, detail_name_height ),
            rendering::Color{ 1.0f, 1.0f, 0.94f, 1.0f },
            false );

        renderer.draw_text( layout.x + 30.0f * layout.scale,
            layout.y + 424.0f * layout.scale,
            localization::text( "@ui.confirm_voice_back_collection_upper" ),
            rendering::Color{ 0.76f, 0.87f, 1.0f, 0.9f },
            0.9f * layout.scale );
    }
}
