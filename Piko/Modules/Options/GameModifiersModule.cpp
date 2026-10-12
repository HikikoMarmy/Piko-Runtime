#include "Utils/EnumUtil.hpp"
#include "Localization/Language.hpp"
#include "GameModifiersModule.hpp"

#include "App/piko.hpp"
#include "Managers/GameSettings.hpp"
#include "Managers/InputManager.hpp"
#include "Modules/MainMenu/TitleScreenBackdrop.hpp"
#include "Modules/Options/OptionsModule.hpp"
#include "Rendering/MenuTheme.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/RenderUtil.hpp"
#include "Utils/FileUtil.hpp"

#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>

namespace piko::modules
{
    GameModifiersModule::GameModifiersModule( std::shared_ptr<OptionsPresentation> presentation )
        : presentation_( std::move( presentation ) )
    {
    }

    rendering::Rect GameModifiersModule::reference_row_rect( std::int32_t index )
    {
        return rendering::Rect{
            126.0f,
            175.0f + ( row_height + row_gap ) * static_cast<float>( index ),
            1028.0f,
            row_height
        };
    }

    const char *GameModifiersModule::row_name( Row row )
    {
        constexpr std::array<const char *, row_count> names{
            "@ui.no_death_mode",
            "@ui.auto_press_mode",
            "@ui.auto_aim_mode",
            "@ui.all_sections_mode",
            "@ui.mirror_mode",
            "@ui.player_receive_damage",
            "@ui.enemy_receive_damage",
            "@ui.note_speed",
            "@ui.snap_angles",
            "@ui.snap_angle_easy",
            "@ui.snap_angle_normal",
            "@ui.snap_angle_master",
            "@ui.snap_angle_psp_easy",
            "@ui.snap_angle_psp_normal",
            "@ui.snap_angle_psp_master",
            "@ui.reset_default",
            "@ui.back"
        };

        return utils::enum_index( row ) < names.size() ? localization::text( names[ utils::enum_index( row ) ] ) : "";
    }

    bool *GameModifiersModule::row_setting( App &app, Row row )
    {
        managers::GameSettings &settings = app.settings();

        switch( row )
        {
            case Row::NoDeath:
                return &settings.no_death_mode;
            case Row::AutoPress:
                return &settings.auto_press_mode;
            case Row::AutoAim:
                return &settings.auto_aim_mode;
            case Row::AllSections:
                return &settings.all_sections_mode;
            case Row::Mirror:
                return &settings.mirror_mode;
            default:
                return nullptr;
        }
    }

    float *GameModifiersModule::row_damage_setting( App &app, Row row )
    {
        switch( row )
        {
            case Row::PlayerDamage:
                return &app.settings().player_damage_multiplier;
            case Row::EnemyDamage:
                return &app.settings().enemy_damage_multiplier;
            default:
                return nullptr;
        }
    }

    std::span<const GameModifiersModule::Row> GameModifiersModule::active_rows() const
    {
        return snap_settings_open_ ? std::span<const Row>( snap_rows ) : std::span<const Row>( main_rows );
    }

    float *GameModifiersModule::row_snap_setting( App &app, Row row )
    {
        using managers::AimDifficulty;

        switch( row )
        {
            case Row::SnapEasy:
                return &app.settings().aim_snap_angles[ utils::enum_index( AimDifficulty::Easy ) ];
            case Row::SnapNormal:
                return &app.settings().aim_snap_angles[ utils::enum_index( AimDifficulty::Normal ) ];
            case Row::SnapMaster:
                return &app.settings().aim_snap_angles[ utils::enum_index( AimDifficulty::Master ) ];
            case Row::SnapPspEasy:
                return &app.settings().psp_aim_snap_angles[ utils::enum_index( AimDifficulty::Easy ) ];
            case Row::SnapPspNormal:
                return &app.settings().psp_aim_snap_angles[ utils::enum_index( AimDifficulty::Normal ) ];
            case Row::SnapPspMaster:
                return &app.settings().psp_aim_snap_angles[ utils::enum_index( AimDifficulty::Master ) ];
            default:
                return nullptr;
        }
    }

    void GameModifiersModule::show_snap_settings( App &app, bool show )
    {
        presentation_->retain_texture( section_heading_ );

        const auto previous_menu =
            snap_settings_open_ ? managers::MenuLocation::SnapAngles : managers::MenuLocation::Modifiers;

        app.menu_navigation().remember( previous_menu, utils::to_int( selected_row_ ) );

        snap_settings_open_ = show;

        const auto next_menu = show ? managers::MenuLocation::SnapAngles : managers::MenuLocation::Modifiers;
        const Row default_row = show ? Row::SnapEasy : Row::NoDeath;

        const auto position = app.menu_navigation().restore( next_menu, row_count, utils::to_int( default_row ) );
        selected_row_ = utils::to_enum<Row>( position.selected_row );

        const auto rows = active_rows();

        if( std::find( rows.begin(), rows.end(), selected_row_ ) == rows.end() )
        {
            selected_row_ = default_row;
        }

        section_heading_ = create_text( app,
            show ? "modifiers/snap-section" : "modifiers/section",
            localization::text( show ? "@ui.snap_angles_upper" : "@ui.game_play_upper" ),
            31.0f,
            false,
            true );
    }

    void GameModifiersModule::prepare_snap_value( App &app, float value )
    {
        const std::int32_t index =
            std::clamp( static_cast<std::int32_t>( std::lround( ( value - minimum_snap_angle ) / snap_angle_step ) ),
                0,
                snap_step_count - 1 );

        TextPair &pair = snap_values_[ static_cast<std::size_t>( index ) ];

        if( pair.normal && pair.selected )
        {
            return;
        }

        std::ostringstream text;
        text << std::fixed << std::setprecision( 1 )
             << minimum_snap_angle + static_cast<float>( index ) * snap_angle_step << "°";

        const std::string prefix = "modifiers/snap-value-" + std::to_string( index );
        pair.normal = create_text( app, prefix + "-normal", text.str(), value_text_size, false );

        pair.selected = create_text( app, prefix + "-selected", text.str(), value_text_size, true );
    }

    void GameModifiersModule::prepare_note_speed_value( App &app )
    {
        const auto value = app.settings().gameplay_visual.note_speed;

        const auto step =
            static_cast<std::int32_t>( std::lround( value / managers::GameplayVisualStyle::note_speed_step ) );

        auto &pair = note_speed_values_[ step ];

        if( pair.normal && pair.selected )
        {
            return;
        }

        std::ostringstream label;
        label << std::fixed << std::setprecision( 2 ) << value << 'x';

        const auto prefix = "modifiers/note-speed-" + std::to_string( step );
        pair.normal = create_text( app, prefix + "-normal", label.str(), value_text_size, false );

        pair.selected = create_text( app, prefix + "-selected", label.str(), value_text_size, true );
    }

    managers::TextureHandle GameModifiersModule::create_text( App &app,
        const std::string &name,
        const std::string &value,
        float size,
        bool selected,
        bool section,
        bool heading )
    {
        if( managers::TextureHandle cached = app.texture_manager().find( localization::texture_key( name ) ); cached )
        {
            return cached;
        }

        if( !font_.loaded() )
        {
            return {};
        }

        const rendering::StrokedTextStyle style{
            rendering::color_from_rgb( section    ? 0x8E2943u
                                       : heading  ? 0xEFBB4Bu
                                       : selected ? 0x19C615u
                                                  : 0xEFBB4Bu ),
            rendering::color_from_rgb( section || ( selected && !heading ) ? 0xFEF67Au : 0x0D005Au ),
            rendering::color_from_rgb( section || ( selected && !heading ) ? 0xFFC2FAu : 0x4287DDu ),
            8,
            17,
            1.5f
        };

        const rendering::RasterizedText raster = font_.rasterize_stroked( value, size * 3.0f, style );
        return raster.valid() ? app.texture_manager().create_rgba( localization::texture_key( name ),
                                    raster.rgba.data(),
                                    raster.width,
                                    raster.height )
                              : managers::TextureHandle{};
    }

    void GameModifiersModule::create_textures( App &app )
    {
        heading_ = create_text( app,
            "modifiers/heading",
            localization::text( "@ui.game_modifiers_upper" ),
            52.0f,
            false,
            false,
            true );

        section_heading_ =
            create_text( app, "modifiers/section", localization::text( "@ui.game_play_upper" ), 31.0f, false, true );

        for( std::int32_t index = 0; index < row_count; ++index )
        {
            const std::string prefix = "modifiers/label-" + std::to_string( index );
            labels_[ static_cast<std::size_t>( index ) ] = TextPair{
                create_text( app,
                    prefix + "-normal",
                    row_name( utils::to_enum<Row>( index ) ),
                    label_text_size,
                    false ),
                create_text( app,
                    prefix + "-selected",
                    row_name( utils::to_enum<Row>( index ) ),
                    label_text_size,
                    true )
            };
        }

        on_ = TextPair{
            create_text( app, "modifiers/on-normal", localization::text( "@ui.on_upper" ), value_text_size, false ),
            create_text( app, "modifiers/on-selected", localization::text( "@ui.on_upper" ), value_text_size, true )
        };

        off_ = TextPair{
            create_text( app, "modifiers/off-normal", localization::text( "@ui.off_upper" ), value_text_size, false ),
            create_text( app, "modifiers/off-selected", localization::text( "@ui.off_upper" ), value_text_size, true )
        };

        for( Row row : { Row::PlayerDamage, Row::EnemyDamage } )
        {
            if( float *value = row_damage_setting( app, row ) )
            {
                prepare_damage_value( app,
                    std::clamp( static_cast<std::int32_t>( std::lround( *value * 4.0f ) ), 0, damage_step_count - 1 ) );
            }
        }

        prepare_note_speed_value( app );

        for( Row row : snap_rows )
        {
            if( const float *angle = row_snap_setting( app, row ) )
            {
                prepare_snap_value( app, *angle );
                prepare_snap_value( app, *angle - snap_angle_step );
                prepare_snap_value( app, *angle + snap_angle_step );
            }
        }
    }

    void GameModifiersModule::prepare_damage_value( App &app, std::int32_t index )
    {
        TextPair &pair = damage_values_[ static_cast<std::size_t>( index ) ];

        if( pair.normal && pair.selected )
        {
            return;
        }

        std::ostringstream value;
        value << std::fixed << std::setprecision( 2 ) << static_cast<float>( index ) * 0.25f << 'x';
        const std::string prefix = "modifiers/damage-" + std::to_string( index );
        pair.normal = create_text( app, prefix + "-normal", value.str(), value_text_size, false );

        pair.selected = create_text( app, prefix + "-selected", value.str(), value_text_size, true );
    }

    void GameModifiersModule::unload_textures( App &app )
    {
        app.texture_manager().unload( heading_ );
        heading_ = {};
        app.texture_manager().unload( section_heading_ );
        section_heading_ = {};

        for( TextPair &pair : labels_ )
        {
            app.texture_manager().unload( pair.normal );
            app.texture_manager().unload( pair.selected );
            pair = {};
        }

        for( TextPair *pair : { &on_, &off_ } )
        {
            app.texture_manager().unload( pair->normal );
            app.texture_manager().unload( pair->selected );
            *pair = {};
        }

        for( TextPair &pair : snap_values_ )
        {
            app.texture_manager().unload( pair.normal );
            app.texture_manager().unload( pair.selected );
            pair = {};
        }

        for( auto &[ step, pair ] : note_speed_values_ )
        {
            app.texture_manager().unload( pair.normal );
            app.texture_manager().unload( pair.selected );
        }

        note_speed_values_.clear();

        for( TextPair &pair : damage_values_ )
        {
            app.texture_manager().unload( pair.normal );
            app.texture_manager().unload( pair.selected );
            pair = {};
        }
    }

    void GameModifiersModule::enter( App &app )
    {
        snap_settings_open_ = false;

        const auto position = app.menu_navigation().restore( managers::MenuLocation::Modifiers, row_count );
        selected_row_ = utils::to_enum<Row>( position.selected_row );

        if( std::find( main_rows.begin(), main_rows.end(), selected_row_ ) == main_rows.end() )
        {
            selected_row_ = Row::NoDeath;
        }

        elapsed_seconds_ = 0.0;
        presentation_->initialize( app );
        const std::filesystem::path font_path = utils::find_resource_file( app.game_data().application_root(),
            std::filesystem::path( "fonts" ) / "Anak Paud.ttf" );

        if( !font_path.empty() )
        {
            localization::load_menu_font( font_, font_path );
        }

        create_textures( app );
        next_damage_preload_ = 0;
    }

    void GameModifiersModule::leave( App &app )
    {
        const auto location =
            snap_settings_open_ ? managers::MenuLocation::SnapAngles : managers::MenuLocation::Modifiers;

        app.menu_navigation().remember( location, utils::to_int( selected_row_ ) );

        if( presentation_.use_count() > 1 )
        {
            const auto retain = [ this ]( managers::TextureHandle &handle )
            {
                presentation_->retain_texture( handle );
                handle = {};
            };

            retain( heading_ );
            retain( section_heading_ );

            for( TextPair &pair : labels_ )
            {
                retain( pair.normal );
                retain( pair.selected );
            }

            for( TextPair *pair : { &on_, &off_ } )
            {
                retain( pair->normal );
                retain( pair->selected );
            }

            for( TextPair &pair : snap_values_ )
            {
                retain( pair.normal );
                retain( pair.selected );
            }

            for( auto &[ step, pair ] : note_speed_values_ )
            {
                retain( pair.normal );
                retain( pair.selected );
            }

            for( TextPair &pair : damage_values_ )
            {
                retain( pair.normal );
                retain( pair.selected );
            }
        }
        else
        {
            unload_textures( app );
        }

        font_.clear();

        if( presentation_.use_count() == 1 )
        {
            presentation_->shutdown( app );
        }

        managers::save_game_settings( app.settings() );
    }

    void GameModifiersModule::handle_event( App &, const SDL_Event & )
    {
    }

    void GameModifiersModule::move_selection( App &app, std::int32_t direction )
    {
        const auto rows = active_rows();

        const auto current = std::find( rows.begin(), rows.end(), selected_row_ );
        const std::int32_t index = static_cast<std::int32_t>( std::distance( rows.begin(), current ) );
        const std::int32_t count = static_cast<std::int32_t>( rows.size() );
        selected_row_ = rows[ static_cast<std::size_t>( ( index + direction + count ) % count ) ];

        presentation_->play_sound( app, "SELECT" );
    }

    void GameModifiersModule::activate( App &app, std::int32_t direction )
    {
        if( selected_row_ == Row::NoteSpeed )
        {
            auto &speed = app.settings().gameplay_visual.note_speed;

            const auto steps = std::round( speed / managers::GameplayVisualStyle::note_speed_step ) + direction;
            speed = std::clamp( steps * managers::GameplayVisualStyle::note_speed_step,
                managers::GameplayVisualStyle::note_speed_minimum,
                managers::GameplayVisualStyle::note_speed_maximum );

            prepare_note_speed_value( app );
            presentation_->play_sound( app, "SELECT" );
            managers::save_game_settings( app.settings() );
            return;
        }

        if( selected_row_ == Row::SnapAngles )
        {
            show_snap_settings( app, true );
            presentation_->play_sound( app, "OK" );
            return;
        }

        if( float *angle = row_snap_setting( app, selected_row_ ) )
        {
            const float next = std::round( *angle / snap_angle_step ) * snap_angle_step +
                               static_cast<float>( direction ) * snap_angle_step;

            *angle = std::clamp( next, minimum_snap_angle, 80.0f );
            prepare_snap_value( app, *angle );
            prepare_snap_value( app, *angle - snap_angle_step );
            prepare_snap_value( app, *angle + snap_angle_step );
            presentation_->play_sound( app, "SELECT" );
            managers::save_game_settings( app.settings() );
            return;
        }

        if( selected_row_ == Row::ResetSnapAngles )
        {
            const managers::GameSettings defaults{};
            app.settings().aim_snap_angles = defaults.aim_snap_angles;

            app.settings().psp_aim_snap_angles = defaults.psp_aim_snap_angles;

            for( Row row : snap_rows )
            {
                if( const float *angle = row_snap_setting( app, row ) )
                {
                    prepare_snap_value( app, *angle );
                    prepare_snap_value( app, *angle - snap_angle_step );
                    prepare_snap_value( app, *angle + snap_angle_step );
                }
            }

            presentation_->play_sound( app, "OK" );
            managers::save_game_settings( app.settings() );
            return;
        }

        if( snap_settings_open_ && selected_row_ == Row::Back )
        {
            show_snap_settings( app, false );
            presentation_->play_sound( app, "OK" );
            return;
        }

        if( float *value = row_damage_setting( app, selected_row_ ) )
        {
            const std::int32_t step = std::clamp( static_cast<std::int32_t>( std::lround( *value * 4.0f ) ) + direction,
                0,
                damage_step_count - 1 );

            *value = static_cast<float>( step ) * 0.25f;
            prepare_damage_value( app, step );
            presentation_->play_sound( app, "SELECT" );
            managers::save_game_settings( app.settings() );
            return;
        }

        if( bool *setting = row_setting( app, selected_row_ ) )
        {
            *setting = !*setting;
            presentation_->play_sound( app, "SELECT" );
            managers::save_game_settings( app.settings() );
            return;
        }

        presentation_->play_sound( app, "OK" );
        app.change_module( std::make_unique<OptionsModule>( presentation_ ) );
    }

    void GameModifiersModule::update( App &app, double delta_seconds )
    {
        elapsed_seconds_ += delta_seconds;
        presentation_->update( app, delta_seconds );

        if( next_damage_preload_ < damage_step_count )
        {
            prepare_damage_value( app, next_damage_preload_++ );
        }

        const managers::InputManager &input = app.input();

        if( input.pressed( managers::InputAction::MenuUp ) )
        {
            move_selection( app, -1 );
        }

        if( input.pressed( managers::InputAction::MenuDown ) )
        {
            move_selection( app, 1 );
        }

        if( input.pressed( managers::InputAction::MenuLeft ) )
        {
            activate( app, -1 );
        }

        if( input.pressed( managers::InputAction::MenuRight ) ||
            ( input.pressed( managers::InputAction::Confirm ) && !input.mouse().left_clicked ) )
        {
            activate( app, 1 );
        }

        if( input.pressed( managers::InputAction::Back ) )
        {
            presentation_->play_sound( app, "OK" );

            if( snap_settings_open_ )
            {
                show_snap_settings( app, false );
            }
            else
            {
                app.change_module( std::make_unique<OptionsModule>( presentation_ ) );
            }
        }

        const managers::MouseState &mouse = input.mouse();

        if( mouse.moved || mouse.left_clicked )
        {
            const rendering::LogicalViewport layout = rendering::make_logical_viewport( app.renderer().output_width(),
                app.renderer().output_height(),
                reference_width,
                reference_height );

            const auto rows = active_rows();

            for( std::size_t index = 0; index < rows.size(); ++index )
            {
                const Row item = rows[ index ];
                const rendering::Rect box = layout.rect( reference_row_rect( static_cast<std::int32_t>( index ) ) );

                if( rendering::point_inside( box, mouse.x, mouse.y ) )
                {
                    if( selected_row_ != item )
                    {
                        selected_row_ = item;
                        presentation_->play_sound( app, "SELECT" );
                    }

                    if( mouse.left_clicked )
                    {
                        const auto direction =
                            rendering::menu_theme::adjustment_direction( box, layout.scale, mouse.x, mouse.y );

                        activate( app, direction == 0 ? 1 : direction );
                    }
                    break;
                }
            }
        }
    }

    void GameModifiersModule::draw_text( App &app,
        rendering::Renderer &renderer,
        managers::TextureHandle handle,
        float x,
        float center_y,
        float scale,
        bool right_aligned,
        bool centered,
        float maximum_width ) const
    {
        if( const rendering::Texture *texture = app.texture_manager().get( handle ) )
        {
            const float natural_width = static_cast<float>( texture->width ) * scale / 3.0f;
            const float fit = maximum_width > 0.0f ? std::min( 1.0f, maximum_width / natural_width ) : 1.0f;
            const float width = natural_width * fit;
            const float height = static_cast<float>( texture->height ) * scale / 3.0f * fit;
            const float left = centered ? x - width * 0.5f : right_aligned ? x - width : x;
            renderer.draw_texture( *texture, left, center_y - height * 0.5f, width, height );
        }
    }

    void GameModifiersModule::render( App &app, rendering::Renderer &renderer )
    {
        const rendering::LogicalViewport layout = rendering::make_logical_viewport( renderer.output_width(),
            renderer.output_height(),
            reference_width,
            reference_height );

        presentation_->draw( app, renderer );
        draw_text( app, renderer, heading_, layout.to_x( 1230.0f ), layout.to_y( 61.0f ), layout.scale, true );
        draw_text( app, renderer, section_heading_, layout.to_x( 132.0f ), layout.to_y( 142.0f ), layout.scale );
        const rendering::Texture *section = app.texture_manager().get( section_heading_ );
        const float line_start =
            section != nullptr ? 132.0f + static_cast<float>( section->width ) / 3.0f + 20.0f : 278.0f;

        renderer.draw_filled_rect( layout.rect( rendering::Rect{ line_start, 141.0f, 1136.0f - line_start, 2.0f } ),
            rendering::Color{ 0.86f, 0.94f, 1.0f, 0.34f } );

        const auto rows = active_rows();

        for( std::size_t index = 0; index < rows.size(); ++index )
        {
            const Row item = rows[ index ];
            const bool selected = selected_row_ == item;
            const rendering::Rect row = layout.rect( reference_row_rect( static_cast<std::int32_t>( index ) ) );
            renderer.draw_filled_rect( row,
                selected ? rendering::Color{ 0.04f, 0.18f, 0.34f, 0.55f }
                         : rendering::Color{ 0.0f, 0.02f, 0.06f, 0.26f } );
            const TextPair &label = labels_[ utils::enum_index( item ) ];
            draw_text( app,
                renderer,
                selected ? label.selected : label.normal,
                row.x + 22.0f * layout.scale,
                row.y + row.h * 0.5f,
                layout.scale );

            const bool adjustable = item == Row::NoteSpeed || row_snap_setting( app, item ) != nullptr ||
                                    row_damage_setting( app, item ) != nullptr;

            if( adjustable )
            {
                rendering::menu_theme::draw_adjustment_buttons( renderer, row, layout.scale, selected );
            }

            const auto value_bounds = rendering::menu_theme::adjustment_value_rect( row, layout.scale );
            const float value_x =
                adjustable ? value_bounds.x + value_bounds.w * 0.5f : row.x + row.w - 26.0f * layout.scale;

            if( item == Row::NoteSpeed )
            {
                prepare_note_speed_value( app );

                const auto step = static_cast<std::int32_t>( std::lround(
                    app.settings().gameplay_visual.note_speed / managers::GameplayVisualStyle::note_speed_step ) );

                const auto &value = note_speed_values_.at( step );
                draw_text( app,
                    renderer,
                    selected ? value.selected : value.normal,
                    value_x,
                    row.y + row.h * 0.5f,
                    layout.scale,
                    !adjustable,
                    adjustable,
                    adjustable ? value_bounds.w : 0.0f );
            }
            else if( float *angle = row_snap_setting( app, item ) )
            {
                const std::int32_t step = std::clamp(
                    static_cast<std::int32_t>( std::lround( ( *angle - minimum_snap_angle ) / snap_angle_step ) ),
                    0,
                    snap_step_count - 1 );

                const TextPair &value = snap_values_[ static_cast<std::size_t>( step ) ];
                draw_text( app,
                    renderer,
                    selected ? value.selected : value.normal,
                    value_x,
                    row.y + row.h * 0.5f,
                    layout.scale,
                    !adjustable,
                    adjustable,
                    adjustable ? value_bounds.w : 0.0f );
            }
            else if( bool *setting = row_setting( app, item ) )
            {
                const TextPair &value = *setting ? on_ : off_;
                draw_text( app,
                    renderer,
                    selected ? value.selected : value.normal,
                    value_x,
                    row.y + row.h * 0.5f,
                    layout.scale,
                    !adjustable,
                    adjustable,
                    adjustable ? value_bounds.w : 0.0f );
            }
            else if( float *value = row_damage_setting( app, item ) )
            {
                const std::int32_t step =
                    std::clamp( static_cast<std::int32_t>( std::lround( *value * 4.0f ) ), 0, damage_step_count - 1 );

                const TextPair &pair = damage_values_[ static_cast<std::size_t>( step ) ];
                draw_text( app,
                    renderer,
                    selected ? pair.selected : pair.normal,
                    value_x,
                    row.y + row.h * 0.5f,
                    layout.scale,
                    !adjustable,
                    adjustable,
                    adjustable ? value_bounds.w : 0.0f );
            }
        }
    }
}
