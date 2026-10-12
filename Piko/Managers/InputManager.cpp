#include "Utils/EnumUtil.hpp"
#include "InputManager.hpp"
#include "ControllerOrientation.hpp"

#include <cstdint>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace piko::managers
{
    float InputManager::normalize_axis( std::int16_t value )
    {
        constexpr float max_positive = 32767.0f;
        constexpr float max_negative = 32768.0f;

        if( value >= 0 )
        {
            return static_cast<float>( value ) / max_positive;
        }

        return static_cast<float>( value ) / max_negative;
    }

    InputManager::StickValues InputManager::apply_radial_deadzone( std::int16_t raw_x,
        std::int16_t raw_y,
        std::int16_t inner_deadzone,
        std::int16_t outer_deadzone )
    {
        const float x = normalize_axis( raw_x );
        const float y = normalize_axis( raw_y );
        const float magnitude = std::sqrt( x * x + y * y );
        const float inner_deadzone_f = static_cast<float>( inner_deadzone ) / 32768.0f;
        const float outer_deadzone_f = static_cast<float>( outer_deadzone ) / 32768.0f;
        const float outer_edge = std::max( inner_deadzone_f + 0.0001f, 1.0f - outer_deadzone_f );

        if( magnitude <= inner_deadzone_f )
        {
            return {};
        }

        const float scaled = ( magnitude - inner_deadzone_f ) / std::max( 0.0001f, outer_edge - inner_deadzone_f );

        const float normalized_scale = std::clamp( scaled, 0.0f, 1.0f ) / std::max( magnitude, 0.0001f );

        StickValues result{
            std::clamp( x * normalized_scale, -1.0f, 1.0f ),
            std::clamp( y * normalized_scale, -1.0f, 1.0f )
        };

        const float strength = std::sqrt( result.x * result.x + result.y * result.y );

        if( strength >= stick_cardinal_snap_strength )
        {
            if( std::abs( result.y ) <= stick_cardinal_snap && std::abs( result.x ) >= stick_cardinal_snap_strength )
            {
                result.x = std::copysign( strength, result.x );

                result.y = 0.0f;
            }
            else if( std::abs( result.x ) <= stick_cardinal_snap &&
                     std::abs( result.y ) >= stick_cardinal_snap_strength )
            {
                result.x = 0.0f;

                result.y = std::copysign( strength, result.y );
            }
        }

        return result;
    }

    bool InputManager::is_joystick_south_button( std::uint8_t button )
    {
        return button == 0;
    }

    bool InputManager::is_joystick_east_button( std::uint8_t button )
    {
        return button == 1;
    }

    bool InputManager::is_joystick_west_button( std::uint8_t button )
    {
        return button == 2;
    }

    bool InputManager::is_joystick_north_button( std::uint8_t button )
    {
        return button == 3;
    }

    bool InputManager::is_joystick_back_button( std::uint8_t button )
    {
        return button == 6 || button == 8;
    }

    bool InputManager::is_joystick_start_button( std::uint8_t button )
    {
        return button == 7 || button == 9;
    }

    bool InputManager::is_joystick_dpad_up_button( std::uint8_t button )
    {
        return button == 11;
    }

    bool InputManager::is_joystick_dpad_down_button( std::uint8_t button )
    {
        return button == 12;
    }

    bool InputManager::is_joystick_dpad_left_button( std::uint8_t button )
    {
        return button == 13;
    }

    bool InputManager::is_joystick_dpad_right_button( std::uint8_t button )
    {
        return button == 14;
    }

    bool InputManager::initialize()
    {
        open_first_gamepad();
        return true;
    }

    void InputManager::shutdown()
    {
        set_battle_input( mouse_window_, false, false, false );

        rumble_motors( 0.0f, 0.0f, 0 );
        close_gamepad();
        close_joystick();
    }

    void InputManager::begin_frame()
    {
        frame_start_down_ = action_down_;
        action_pressed_.fill( false );
        action_released_.fill( false );
        transitions_.clear();
        frame_start_timestamp_ns_ = SDL_GetTicksNS();

        active_event_timestamp_ns_ = frame_start_timestamp_ns_;

        active_event_serial_ = 0;

        refresh_live_stick_state();

        mouse_.moved = false;

        mouse_.left_clicked = false;

        mouse_.right_clicked = false;
    }

    void InputManager::update_menu_repeat( double delta_seconds )
    {
        const bool left =
            key_aim_left_ || down( InputAction::StageKeyboardGuardLeft ) || down( InputAction::StageDpadGuardLeft );

        const bool right =
            key_aim_right_ || down( InputAction::StageKeyboardGuardRight ) || down( InputAction::StageDpadGuardRight );

        repeat_menu_direction( left,
            right,
            InputAction::MenuLeft,
            InputAction::MenuRight,
            delta_seconds,
            horizontal_menu_repeat_ );

        const bool up_held =
            key_aim_up_ || down( InputAction::StageKeyboardGuardUp ) || down( InputAction::StageDpadGuardUp );

        const bool down_held =
            key_aim_down_ || down( InputAction::StageKeyboardGuardDown ) || down( InputAction::StageDpadGuardDown );

        repeat_menu_direction( up_held,
            down_held,
            InputAction::MenuUp,
            InputAction::MenuDown,
            delta_seconds,
            vertical_menu_repeat_ );
    }

    void InputManager::repeat_menu_direction( bool negative,
        bool positive,
        InputAction negative_action,
        InputAction positive_action,
        double delta_seconds,
        MenuRepeatState &state )
    {
        const std::int32_t direction = negative == positive ? 0 : ( negative ? -1 : 1 );

        if( direction == 0 )
        {
            state = {};
            return;
        }

        const InputAction action = direction < 0 ? negative_action : positive_action;

        if( direction != state.direction || pressed( action ) )
        {
            state.direction = direction;

            state.seconds = menu_repeat_delay;

            if( !pressed( action ) )
            {
                tap( action );
            }

            return;
        }

        state.seconds -= std::max( 0.0, delta_seconds );

        if( state.seconds <= 0.0 )
        {
            tap( action );
            state.seconds = menu_repeat_interval + std::fmod( state.seconds, menu_repeat_interval );
        }
    }

    void InputManager::update( double delta_seconds )
    {
        update_menu_repeat( delta_seconds );

        if( rumble_remaining_seconds_ > 0.0 )
        {
            rumble_remaining_seconds_ -= std::max( 0.0, delta_seconds );

            if( rumble_remaining_seconds_ <= 0.0 )
            {
                rumble_remaining_seconds_ = 0.0;

                if( gamepad_ != nullptr )
                {
                    SDL_RumbleGamepad( gamepad_, 0, 0, 0 );
                }

                if( joystick_ != nullptr )
                {
                    SDL_RumbleJoystick( joystick_, 0, 0, 0 );
                }

                SDL_UpdateJoysticks();
            }
        }

        const float target_x = gamepad_left_target_x_;
        const float target_y = gamepad_left_target_y_;
        const float delta_x = target_x - gamepad_left_x_;
        const float delta_y = target_y - gamepad_left_y_;
        const float distance = std::sqrt( delta_x * delta_x + delta_y * delta_y );
        const float current_strength =
            std::sqrt( gamepad_left_x_ * gamepad_left_x_ + gamepad_left_y_ * gamepad_left_y_ );

        const float target_strength = std::sqrt( target_x * target_x + target_y * target_y );

        if( distance <= 0.0001f )
        {
            return;
        }

        if( current_strength > 0.0001f && target_strength > 0.0001f )
        {
            const float current_angle = std::atan2( gamepad_left_y_, gamepad_left_x_ );
            const float target_angle = std::atan2( target_y, target_x );
            const float angle_delta = std::remainder( target_angle - current_angle, 6.28318530717958647692f );

            if( std::abs( angle_delta ) <= stick_angle_deadband_radians &&
                std::abs( target_strength - current_strength ) <= 0.015f )
            {
                return;
            }
        }

        if( current_strength <= 0.0001f || target_strength <= 0.0001f || distance >= stick_filter_snap_distance )
        {
            gamepad_left_x_ = target_x;

            gamepad_left_y_ = target_y;
            return;
        }

        const float step = static_cast<float>( std::clamp( delta_seconds, 0.0, 0.25 ) );
        const float factor = stick_filter_seconds > 0.0f ? 1.0f - std::exp( -step / stick_filter_seconds ) : 1.0f;

        gamepad_left_x_ += delta_x * factor;

        gamepad_left_y_ += delta_y * factor;

        if( std::abs( target_x - gamepad_left_x_ ) <= 0.0001f )
        {
            gamepad_left_x_ = target_x;
        }

        if( std::abs( target_y - gamepad_left_y_ ) <= 0.0001f )
        {
            gamepad_left_y_ = target_y;
        }
    }

    void InputManager::handle_event( const SDL_Event &event )
    {
        active_event_timestamp_ns_ = event.common.timestamp != 0 ? event.common.timestamp : SDL_GetTicksNS();

        active_event_serial_ = ++next_event_serial_;

        switch( event.type )
        {
            case SDL_EVENT_KEY_DOWN:
                if( event.key.repeat )
                {
                    break;
                }

                switch( event.key.key )
                {
                    case SDLK_UP:
                        tap( InputAction::MenuUp );
                        key_aim_up_ = true;
                        rebuild_keyboard_stick();
                        break;

                    case SDLK_W:
                        tap( InputAction::MenuUp );
                        press( InputAction::StageKeyboardGuardUp );
                        break;

                    case SDLK_DOWN:
                        tap( InputAction::MenuDown );
                        key_aim_down_ = true;
                        rebuild_keyboard_stick();
                        break;

                    case SDLK_S:
                        tap( InputAction::MenuDown );
                        press( InputAction::StageKeyboardGuardDown );
                        break;

                    case SDLK_A:
                        tap( InputAction::MenuLeft );
                        press( InputAction::StageKeyboardGuardLeft );
                        break;

                    case SDLK_LEFT:
                        tap( InputAction::MenuLeft );
                        key_aim_left_ = true;
                        rebuild_keyboard_stick();
                        break;

                    case SDLK_D:
                        tap( InputAction::MenuRight );
                        press( InputAction::StageKeyboardGuardRight );
                        break;

                    case SDLK_RIGHT:
                        tap( InputAction::MenuRight );
                        key_aim_right_ = true;
                        rebuild_keyboard_stick();
                        break;

                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        tap( InputAction::Confirm );
                        tap( InputAction::Pause );
                        break;

                    case SDLK_SPACE:
                        tap( InputAction::Confirm );
                        press( InputAction::StageAction );
                        break;

                    case SDLK_Z:
                        press( InputAction::StageAction );
                        break;

                    case SDLK_ESCAPE:
                    case SDLK_BACKSPACE:
                        tap( InputAction::Back );
                        tap( InputAction::StageExit );
                        break;

                    case SDLK_J:
                    case SDLK_4:
                        press( mapped_guard_action( 0 ) );
                        break;

                    case SDLK_K:
                    case SDLK_2:
                        press( mapped_guard_action( 1 ) );
                        break;

                    case SDLK_L:
                    case SDLK_6:
                        press( mapped_guard_action( 2 ) );
                        break;

                    case SDLK_I:
                    case SDLK_8:
                        press( mapped_guard_action( 3 ) );
                        break;

                    default:
                        break;
                }
                break;

            case SDL_EVENT_KEY_UP:
                switch( event.key.key )
                {
                    case SDLK_W:
                        release( InputAction::StageKeyboardGuardUp );
                        break;

                    case SDLK_UP:
                        key_aim_up_ = false;
                        rebuild_keyboard_stick();
                        break;

                    case SDLK_S:
                        release( InputAction::StageKeyboardGuardDown );
                        break;

                    case SDLK_DOWN:
                        key_aim_down_ = false;
                        rebuild_keyboard_stick();
                        break;

                    case SDLK_A:
                        release( InputAction::StageKeyboardGuardLeft );
                        break;

                    case SDLK_LEFT:
                        key_aim_left_ = false;
                        rebuild_keyboard_stick();
                        break;

                    case SDLK_D:
                        release( InputAction::StageKeyboardGuardRight );
                        break;

                    case SDLK_RIGHT:
                        key_aim_right_ = false;
                        rebuild_keyboard_stick();
                        break;

                    case SDLK_SPACE:
                    case SDLK_Z:
                        release( InputAction::StageAction );
                        break;

                    case SDLK_J:
                    case SDLK_4:
                        release( mapped_guard_action( 0 ) );
                        break;

                    case SDLK_K:
                    case SDLK_2:
                        release( mapped_guard_action( 1 ) );
                        break;

                    case SDLK_L:
                    case SDLK_6:
                        release( mapped_guard_action( 2 ) );
                        break;

                    case SDLK_I:
                    case SDLK_8:
                        release( mapped_guard_action( 3 ) );
                        break;

                    default:
                        break;
                }
                break;

            case SDL_EVENT_GAMEPAD_ADDED:
                if( gamepad_ == nullptr && joystick_ == nullptr )
                {
                    open_gamepad( event.gdevice.which );

                    if( gamepad_ == nullptr )
                    {
                        open_joystick( event.gdevice.which );
                    }
                }
                break;

            case SDL_EVENT_GAMEPAD_REMOVED:
                if( gamepad_id_ != 0 && event.gdevice.which == gamepad_id_ )
                {
                    close_gamepad();
                    open_first_gamepad();
                }
                break;

            case SDL_EVENT_JOYSTICK_ADDED:
                if( gamepad_ == nullptr && joystick_ == nullptr )
                {
                    open_gamepad( event.jdevice.which );

                    if( gamepad_ == nullptr )
                    {
                        open_joystick( event.jdevice.which );
                    }
                }
                break;

            case SDL_EVENT_JOYSTICK_REMOVED:
                if( joystick_id_ != 0 && event.jdevice.which == joystick_id_ )
                {
                    close_joystick();
                    open_first_gamepad();
                }
                break;

            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if( gamepad_ != nullptr && event.gbutton.which == gamepad_id_ )
                {
                    switch( event.gbutton.button )
                    {
                        case SDL_GAMEPAD_BUTTON_DPAD_UP:
                            tap( InputAction::MenuUp );
                            press( InputAction::StageDpadGuardUp );
                            break;

                        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                            tap( InputAction::MenuDown );
                            press( InputAction::StageDpadGuardDown );
                            break;

                        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                            tap( InputAction::MenuLeft );
                            press( InputAction::StageDpadGuardLeft );
                            break;

                        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                            tap( InputAction::MenuRight );
                            press( InputAction::StageDpadGuardRight );
                            break;

                        case SDL_GAMEPAD_BUTTON_SOUTH:
                            tap( InputAction::Confirm );
                            press( mapped_guard_action( 1 ) );
                            break;

                        case SDL_GAMEPAD_BUTTON_WEST:
                            press( mapped_guard_action( 0 ) );
                            break;

                        case SDL_GAMEPAD_BUTTON_EAST:
                            tap( InputAction::Back );
                            press( mapped_guard_action( 2 ) );
                            break;

                        case SDL_GAMEPAD_BUTTON_NORTH:
                            press( mapped_guard_action( 3 ) );
                            break;

                        case SDL_GAMEPAD_BUTTON_START:
                            tap( InputAction::Confirm );
                            tap( InputAction::Pause );
                            break;

                        case SDL_GAMEPAD_BUTTON_BACK:
                            tap( InputAction::Back );
                            tap( InputAction::StageExit );
                            break;

                        default:
                            break;
                    }
                }
                break;

            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                if( gamepad_ != nullptr && event.gbutton.which == gamepad_id_ )
                {
                    switch( event.gbutton.button )
                    {
                        case SDL_GAMEPAD_BUTTON_SOUTH:
                            release( mapped_guard_action( 1 ) );
                            break;

                        case SDL_GAMEPAD_BUTTON_WEST:
                            release( mapped_guard_action( 0 ) );
                            break;

                        case SDL_GAMEPAD_BUTTON_EAST:
                            release( mapped_guard_action( 2 ) );
                            break;

                        case SDL_GAMEPAD_BUTTON_NORTH:
                            release( mapped_guard_action( 3 ) );
                            break;

                        case SDL_GAMEPAD_BUTTON_DPAD_UP:
                            release( InputAction::StageDpadGuardUp );
                            break;

                        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                            release( InputAction::StageDpadGuardDown );
                            break;

                        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                            release( InputAction::StageDpadGuardLeft );
                            break;

                        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                            release( InputAction::StageDpadGuardRight );
                            break;

                        default:
                            break;
                    }
                }
                break;

            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if( gamepad_ != nullptr && event.gaxis.which == gamepad_id_ )
                {

                    if( event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY ||
                        event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTX || event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTY )
                    {
                        refresh_live_stick_state();
                    }
                }
                break;

            case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
                if( joystick_ != nullptr && event.jbutton.which == joystick_id_ )
                {
                    const std::uint8_t button = event.jbutton.button;

                    if( is_joystick_south_button( button ) )
                    {
                        tap( InputAction::Confirm );
                        press( mapped_guard_action( 1 ) );
                    }
                    else if( is_joystick_west_button( button ) )
                    {
                        press( mapped_guard_action( 0 ) );
                    }
                    else if( is_joystick_east_button( button ) )
                    {
                        tap( InputAction::Back );
                        press( mapped_guard_action( 2 ) );
                    }
                    else if( is_joystick_north_button( button ) )
                    {
                        press( mapped_guard_action( 3 ) );
                    }
                    else if( is_joystick_start_button( button ) )
                    {
                        tap( InputAction::Confirm );
                        tap( InputAction::Pause );
                    }
                    else if( is_joystick_back_button( button ) )
                    {
                        tap( InputAction::Back );
                        tap( InputAction::StageExit );
                    }
                    else if( is_joystick_dpad_up_button( button ) )
                    {
                        tap( InputAction::MenuUp );
                        press( InputAction::StageDpadGuardUp );
                    }
                    else if( is_joystick_dpad_down_button( button ) )
                    {
                        tap( InputAction::MenuDown );
                        press( InputAction::StageDpadGuardDown );
                    }
                    else if( is_joystick_dpad_left_button( button ) )
                    {
                        press( InputAction::StageDpadGuardLeft );
                    }
                    else if( is_joystick_dpad_right_button( button ) )
                    {
                        press( InputAction::StageDpadGuardRight );
                    }
                }
                break;

            case SDL_EVENT_JOYSTICK_BUTTON_UP:
                if( joystick_ != nullptr && event.jbutton.which == joystick_id_ )
                {
                    const std::uint8_t button = event.jbutton.button;

                    if( is_joystick_south_button( button ) )
                    {
                        release( mapped_guard_action( 1 ) );
                    }
                    else if( is_joystick_west_button( button ) )
                    {
                        release( mapped_guard_action( 0 ) );
                    }
                    else if( is_joystick_east_button( button ) )
                    {
                        release( mapped_guard_action( 2 ) );
                    }
                    else if( is_joystick_north_button( button ) )
                    {
                        release( mapped_guard_action( 3 ) );
                    }
                    else if( is_joystick_dpad_up_button( button ) )
                    {
                        release( InputAction::StageDpadGuardUp );
                    }
                    else if( is_joystick_dpad_down_button( button ) )
                    {
                        release( InputAction::StageDpadGuardDown );
                    }
                    else if( is_joystick_dpad_left_button( button ) )
                    {
                        release( InputAction::StageDpadGuardLeft );
                    }
                    else if( is_joystick_dpad_right_button( button ) )
                    {
                        release( InputAction::StageDpadGuardRight );
                    }
                }
                break;

            case SDL_EVENT_JOYSTICK_AXIS_MOTION:
                if( joystick_ != nullptr && event.jaxis.which == joystick_id_ )
                {

                    const std::int32_t axis_start = controller_orientation_ == ControllerOrientation::TypeB ? 2 : 0;

                    if( event.jaxis.axis == axis_start )
                    {
                        const std::int16_t raw_y =
                            joystick_ != nullptr && SDL_GetNumJoystickAxes( joystick_ ) > axis_start + 1
                                ? SDL_GetJoystickAxis( joystick_, axis_start + 1 )
                                : 0;

                        set_left_stick_from_raw_axes( event.jaxis.value, raw_y );
                    }
                    else if( event.jaxis.axis == axis_start + 1 )
                    {

                        const std::int16_t raw_x =
                            joystick_ != nullptr && SDL_GetNumJoystickAxes( joystick_ ) > axis_start
                                ? SDL_GetJoystickAxis( joystick_, axis_start )
                                : 0;

                        set_left_stick_from_raw_axes( raw_x, event.jaxis.value );
                    }
                }
                break;

            case SDL_EVENT_JOYSTICK_HAT_MOTION:
                if( joystick_ != nullptr && event.jhat.which == joystick_id_ )
                {
                    const bool up = ( event.jhat.value & SDL_HAT_UP ) != 0;
                    const bool down = ( event.jhat.value & SDL_HAT_DOWN ) != 0;
                    const bool left = ( event.jhat.value & SDL_HAT_LEFT ) != 0;
                    const bool right = ( event.jhat.value & SDL_HAT_RIGHT ) != 0;

                    if( up && !joystick_hat_up_pressed_ )
                    {
                        tap( InputAction::MenuUp );
                        press( InputAction::StageDpadGuardUp );
                    }
                    else if( !up && joystick_hat_up_pressed_ )
                    {
                        release( InputAction::StageDpadGuardUp );
                    }

                    if( down && !joystick_hat_down_pressed_ )
                    {
                        tap( InputAction::MenuDown );
                        press( InputAction::StageDpadGuardDown );
                    }
                    else if( !down && joystick_hat_down_pressed_ )
                    {
                        release( InputAction::StageDpadGuardDown );
                    }

                    if( left && !joystick_hat_left_pressed_ )
                    {
                        tap( InputAction::MenuLeft );
                        press( InputAction::StageDpadGuardLeft );
                    }
                    else if( !left && joystick_hat_left_pressed_ )
                    {
                        release( InputAction::StageDpadGuardLeft );
                    }

                    if( right && !joystick_hat_right_pressed_ )
                    {
                        tap( InputAction::MenuRight );
                        press( InputAction::StageDpadGuardRight );
                    }
                    else if( !right && joystick_hat_right_pressed_ )
                    {
                        release( InputAction::StageDpadGuardRight );
                    }

                    joystick_hat_up_pressed_ = up;

                    joystick_hat_down_pressed_ = down;

                    joystick_hat_left_pressed_ = left;

                    joystick_hat_right_pressed_ = right;
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                mouse_.x = event.motion.x;

                mouse_.y = event.motion.y;

                mouse_.moved = true;

                if( mouse_aim_allowed_ && mouse_relative_mode_ )
                {
                    update_mouse_aim( event.motion.xrel, event.motion.yrel );
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                mouse_.x = event.button.x;

                mouse_.y = event.button.y;

                if( event.button.button == SDL_BUTTON_LEFT )
                {
                    mouse_.left_down = true;

                    mouse_.left_clicked = true;

                    if( !battle_input_enabled_ )
                    {
                        tap( InputAction::Confirm );
                    }

                    if( !battle_input_enabled_ || mouse_notes_allowed_ )
                    {
                        press( InputAction::StageMouseLeft );
                    }
                }
                else if( event.button.button == SDL_BUTTON_RIGHT )
                {
                    mouse_.right_down = true;

                    mouse_.right_clicked = true;

                    if( battle_input_enabled_ )
                    {
                        if( mouse_notes_allowed_ )
                        {
                            press( InputAction::StageMouseRight );
                        }
                    }
                    else
                    {
                        tap( InputAction::Back );
                    }
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                mouse_.x = event.button.x;

                mouse_.y = event.button.y;

                if( event.button.button == SDL_BUTTON_LEFT )
                {
                    mouse_.left_down = false;
                    release( InputAction::StageMouseLeft );
                }
                else if( event.button.button == SDL_BUTTON_RIGHT )
                {
                    mouse_.right_down = false;
                    release( InputAction::StageMouseRight );
                }
                break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
                mouse_aim_allowed_ = false;
                mouse_notes_allowed_ = false;
                set_mouse_capture( false );

                for( std::size_t index = 0; index < action_down_.size(); ++index )
                {
                    release( utils::to_enum<InputAction>( index ) );
                }

                key_aim_left_ = false;

                key_aim_right_ = false;

                key_aim_up_ = false;

                key_aim_down_ = false;
                rebuild_keyboard_stick();
                mouse_.left_down = false;

                mouse_.right_down = false;

                horizontal_menu_repeat_ = {};

                vertical_menu_repeat_ = {};
                break;

            default:
                break;
        }
    }

    bool InputManager::pressed( InputAction action ) const
    {
        return action_pressed_[ utils::enum_index( action ) ];
    }

    bool InputManager::down( InputAction action ) const
    {
        return action_down_[ utils::enum_index( action ) ];
    }

    bool InputManager::released( InputAction action ) const
    {
        return action_released_[ utils::enum_index( action ) ];
    }

    bool InputManager::down_at_frame_start( InputAction action ) const
    {
        return frame_start_down_[ utils::enum_index( action ) ];
    }

    const std::vector<InputTransition> &InputManager::transitions() const
    {
        return transitions_;
    }

    std::uint64_t InputManager::frame_start_timestamp_ns() const
    {
        return frame_start_timestamp_ns_;
    }

    const MouseState &InputManager::mouse() const
    {
        return mouse_;
    }

    bool InputManager::rumble( float strength, std::uint32_t duration_ms )
    {
        const float clamped = std::clamp( strength, 0.0f, 1.0f );
        return rumble_motors( clamped * 0.55f, clamped, duration_ms );
    }

    bool InputManager::rumble_motors( float low_strength, float high_strength, std::uint32_t duration_ms )
    {
        const std::uint16_t low =
            static_cast<std::uint16_t>( std::lround( std::clamp( low_strength, 0.0f, 1.0f ) * 65535.0f ) );

        const std::uint16_t high =
            static_cast<std::uint16_t>( std::lround( std::clamp( high_strength, 0.0f, 1.0f ) * 65535.0f ) );

        const bool active = low != 0 || high != 0;
        rumble_remaining_seconds_ = active && duration_ms > 0 ? static_cast<double>( duration_ms ) / 1000.0 : 0.0;

        if( gamepad_ != nullptr )
        {
            return SDL_RumbleGamepad( gamepad_, low, high, duration_ms );
        }

        if( joystick_ != nullptr )
        {
            return SDL_RumbleJoystick( joystick_, low, high, duration_ms );
        }

        return false;
    }

    bool InputManager::gamepad_connected() const
    {
        return gamepad_ != nullptr || joystick_ != nullptr;
    }

    const std::string &InputManager::gamepad_name() const
    {
        if( gamepad_ == nullptr && joystick_ != nullptr )
        {
            return joystick_name_;
        }

        return gamepad_name_;
    }

    std::vector<InputManager::Device> InputManager::available_devices() const
    {
        std::vector<Device> devices;
        std::int32_t count = 0;
        SDL_JoystickID *ids = SDL_GetJoysticks( &count );

        for( std::int32_t index = 0; index < count && ids != nullptr; ++index )
        {
            const char *name = SDL_GetJoystickNameForID( ids[ index ] );
            devices.push_back( Device{ ids[ index ], name != nullptr ? name : "Unknown Controller" } );
        }

        SDL_free( ids );
        return devices;
    }

    SDL_JoystickID InputManager::selected_device() const
    {
        return gamepad_ != nullptr ? gamepad_id_ : joystick_id_;
    }

    bool InputManager::select_device( SDL_JoystickID id )
    {
        if( selected_device() == id )
        {
            return id != 0;
        }

        action_down_.fill( false );
        frame_start_down_.fill( false );
        close_gamepad();
        close_joystick();
        open_gamepad( id );

        if( gamepad_ == nullptr )
        {
            open_joystick( id );
        }

        if( !gamepad_connected() )
        {
            open_first_gamepad();
        }

        return selected_device() == id;
    }

    InputAction InputManager::mapped_guard_action( std::int32_t physical_index ) const
    {
        constexpr std::array<InputAction, 4> actions{
            InputAction::StageGuardLeft,
            InputAction::StageGuardDown,
            InputAction::StageGuardRight,
            InputAction::StageGuardUp
        };

        const std::int32_t rotation = controller_guard_rotation( controller_orientation_ );
        return actions[ static_cast<std::size_t>( ( physical_index + rotation ) % 4 ) ];
    }

    void InputManager::set_controller_orientation( ControllerOrientation orientation )
    {
        const ControllerOrientation next = controller_orientation_from_index( utils::to_int( orientation ) );

        if( next == controller_orientation_ )
        {
            return;
        }

        constexpr std::array<InputAction, 4> guards{
            InputAction::StageGuardLeft,
            InputAction::StageGuardDown,
            InputAction::StageGuardRight,
            InputAction::StageGuardUp
        };

        std::array<bool, 4> held{};

        for( std::int32_t physical = 0; physical < 4; ++physical )
        {
            held[ static_cast<std::size_t>( physical ) ] = down( mapped_guard_action( physical ) );
        }

        controller_orientation_ = next;

        for( InputAction action : guards )
        {
            const auto index = utils::enum_index( action );
            action_down_[ index ] = false;

            frame_start_down_[ index ] = false;

            action_pressed_[ index ] = false;

            action_released_[ index ] = false;
        }

        transitions_.erase( std::remove_if( transitions_.begin(),
                                transitions_.end(),
                                []( const InputTransition &event )
                                {
                                    return event.action >= InputAction::StageGuardLeft &&
                                           event.action <= InputAction::StageGuardUp;
                                } ),
            transitions_.end() );
        for( std::int32_t physical = 0; physical < 4; ++physical )
        {
            const auto index = utils::enum_index( mapped_guard_action( physical ) );
            action_down_[ index ] = held[ static_cast<std::size_t>( physical ) ];

            frame_start_down_[ index ] = action_down_[ index ];
        }

        refresh_live_stick_state();
        gamepad_left_x_ = gamepad_left_target_x_;

        gamepad_left_y_ = gamepad_left_target_y_;
    }

    float InputManager::left_stick_x() const
    {
        if( battle_input_enabled_ && mouse_aim_active_ )
        {
            const float length = std::hypot( mouse_aim_x_, mouse_aim_y_ );
            return length > mouse_aim_deadzone ? mouse_aim_x_ / length : 0.0f;
        }

        const float x = std::abs( gamepad_left_x_ ) > 0.001f ? gamepad_left_x_ : keyboard_left_x_;
        return std::clamp( x, -1.0f, 1.0f );
    }

    float InputManager::left_stick_y() const
    {
        if( battle_input_enabled_ && mouse_aim_active_ )
        {
            const float length = std::hypot( mouse_aim_x_, mouse_aim_y_ );
            return length > mouse_aim_deadzone ? mouse_aim_y_ / length : 0.0f;
        }

        const float y = std::abs( gamepad_left_y_ ) > 0.001f ? gamepad_left_y_ : keyboard_left_y_;
        return std::clamp( y, -1.0f, 1.0f );
    }

    float InputManager::left_stick_strength() const
    {
        const float x = left_stick_x();
        const float y = left_stick_y();
        return std::clamp( std::sqrt( x * x + y * y ), 0.0f, 1.0f );
    }

    void InputManager::open_first_gamepad()
    {
        if( gamepad_ != nullptr || joystick_ != nullptr )
        {
            return;
        }

        std::int32_t count = 0;
        SDL_JoystickID *joysticks = SDL_GetGamepads( &count );

        if( joysticks == nullptr )
        {
            open_first_joystick();
            return;
        }

        for( std::int32_t i = 0; i < count; ++i )
        {
            open_gamepad( joysticks[ i ] );

            if( gamepad_ != nullptr )
            {
                break;
            }
        }

        SDL_free( joysticks );

        if( gamepad_ == nullptr )
        {
            open_first_joystick();
        }
    }

    void InputManager::open_first_joystick()
    {
        if( gamepad_ != nullptr || joystick_ != nullptr )
        {
            return;
        }

        std::int32_t count = 0;
        SDL_JoystickID *joysticks = SDL_GetJoysticks( &count );

        if( joysticks == nullptr )
        {
            return;
        }

        for( std::int32_t i = 0; i < count; ++i )
        {
            open_joystick( joysticks[ i ] );

            if( joystick_ != nullptr )
            {
                break;
            }
        }

        SDL_free( joysticks );
    }

    void InputManager::open_gamepad( SDL_JoystickID joystick_id )
    {
        if( gamepad_ != nullptr || joystick_ != nullptr )
        {
            return;
        }

        if( joystick_id == 0 || !SDL_IsGamepad( joystick_id ) )
        {
            return;
        }

        SDL_Gamepad *gamepad = SDL_OpenGamepad( joystick_id );

        if( gamepad == nullptr )
        {
            return;
        }

        gamepad_ = gamepad;

        gamepad_id_ = SDL_GetGamepadID( gamepad_ );

        const char *name = SDL_GetGamepadName( gamepad_ );
        gamepad_name_ = name != nullptr ? name : "Unknown Gamepad";

        stick_up_pressed_ = false;

        stick_down_pressed_ = false;

        gamepad_left_x_ = 0.0f;

        gamepad_left_y_ = 0.0f;

        gamepad_left_target_x_ = 0.0f;

        gamepad_left_target_y_ = 0.0f;
    }

    void InputManager::open_joystick( SDL_JoystickID joystick_id )
    {
        if( gamepad_ != nullptr || joystick_ != nullptr )
        {
            return;
        }

        if( joystick_id == 0 )
        {
            return;
        }

        SDL_Joystick *joystick = SDL_OpenJoystick( joystick_id );

        if( joystick == nullptr )
        {
            return;
        }

        joystick_ = joystick;

        joystick_id_ = SDL_GetJoystickID( joystick_ );

        const char *name = SDL_GetJoystickName( joystick_ );
        joystick_name_ = name != nullptr ? name : "Unknown Joystick";

        stick_up_pressed_ = false;

        stick_down_pressed_ = false;

        joystick_hat_up_pressed_ = false;

        joystick_hat_down_pressed_ = false;

        gamepad_left_x_ = 0.0f;

        gamepad_left_y_ = 0.0f;

        gamepad_left_target_x_ = 0.0f;

        gamepad_left_target_y_ = 0.0f;
    }

    void InputManager::close_gamepad()
    {
        if( gamepad_ == nullptr )
        {
            return;
        }

        SDL_CloseGamepad( gamepad_ );
        gamepad_ = nullptr;

        gamepad_id_ = 0;
        gamepad_name_.clear();

        stick_up_pressed_ = false;

        stick_down_pressed_ = false;

        gamepad_left_x_ = 0.0f;

        gamepad_left_y_ = 0.0f;

        gamepad_left_target_x_ = 0.0f;

        gamepad_left_target_y_ = 0.0f;
    }

    void InputManager::close_joystick()
    {
        if( joystick_ == nullptr )
        {
            return;
        }

        SDL_CloseJoystick( joystick_ );
        joystick_ = nullptr;

        joystick_id_ = 0;
        joystick_name_.clear();

        stick_up_pressed_ = false;

        stick_down_pressed_ = false;

        joystick_hat_up_pressed_ = false;

        joystick_hat_down_pressed_ = false;

        gamepad_left_x_ = 0.0f;

        gamepad_left_y_ = 0.0f;

        gamepad_left_target_x_ = 0.0f;

        gamepad_left_target_y_ = 0.0f;
    }

    void InputManager::tap( InputAction action )
    {
        action_pressed_[ utils::enum_index( action ) ] = true;
    }

    void InputManager::press( InputAction action )
    {
        const std::size_t index = utils::enum_index( action );

        if( !action_down_[ index ] )
        {
            action_pressed_[ index ] = true;
            transitions_.push_back( InputTransition{
                action,
                InputTransitionKind::Pressed,
                active_event_timestamp_ns_,
                active_event_serial_
            } );
        }

        action_down_[ index ] = true;
    }

    void InputManager::release( InputAction action )
    {
        const std::size_t index = utils::enum_index( action );

        if( action_down_[ index ] )
        {
            action_released_[ index ] = true;
            transitions_.push_back( InputTransition{
                action,
                InputTransitionKind::Released,
                active_event_timestamp_ns_,
                active_event_serial_
            } );
        }

        action_down_[ index ] = false;
    }

    void InputManager::refresh_live_stick_state()
    {
        if( gamepad_ != nullptr )
        {
            set_left_stick_from_raw_axes( SDL_GetGamepadAxis( gamepad_,
                                              controller_orientation_ == ControllerOrientation::TypeB
                                                  ? SDL_GAMEPAD_AXIS_RIGHTX
                                                  : SDL_GAMEPAD_AXIS_LEFTX ),
                SDL_GetGamepadAxis( gamepad_,
                    controller_orientation_ == ControllerOrientation::TypeB ? SDL_GAMEPAD_AXIS_RIGHTY
                                                                            : SDL_GAMEPAD_AXIS_LEFTY ) );

            return;
        }

        const std::int32_t axis_start = controller_orientation_ == ControllerOrientation::TypeB ? 2 : 0;

        if( joystick_ != nullptr && SDL_GetNumJoystickAxes( joystick_ ) >= axis_start + 2 )
        {
            set_left_stick_from_raw_axes( SDL_GetJoystickAxis( joystick_, axis_start ),
                SDL_GetJoystickAxis( joystick_, axis_start + 1 ) );

            return;
        }

        gamepad_left_target_x_ = 0.0f;

        gamepad_left_target_y_ = 0.0f;
    }

    void InputManager::set_left_stick_from_raw_axes( std::int16_t raw_x, std::int16_t raw_y )
    {
        const StickValues stick = apply_radial_deadzone( raw_x, raw_y, stick_inner_deadzone, stick_outer_deadzone );
        // Rotate the stick into screen space
        const float target_x = controller_orientation_ == ControllerOrientation::TypeB   ? -stick.y
                               : controller_orientation_ == ControllerOrientation::TypeC ? stick.y
                                                                                         : stick.x;

        const float target_y = controller_orientation_ == ControllerOrientation::TypeB   ? stick.x
                               : controller_orientation_ == ControllerOrientation::TypeC ? -stick.x
                                                                                         : stick.y;

        const float change = std::hypot( target_x - gamepad_left_target_x_, target_y - gamepad_left_target_y_ );

        if( change > mouse_takeover_threshold && std::hypot( target_x, target_y ) > aim_active_threshold )
        {
            clear_mouse_aim();
        }

        gamepad_left_target_x_ = target_x;

        gamepad_left_target_y_ = target_y;
    }

    MouseAimState InputManager::mouse_aim() const
    {
        return { mouse_aim_x_, mouse_aim_y_, mouse_aim_active_ };
    }

    void InputManager::set_battle_input( SDL_Window *window, bool enabled, bool allow_mouse_aim, bool allow_mouse_notes )
    {
        SDL_Window *target_window = window != nullptr ? window : mouse_window_;
        const bool focused =
            target_window != nullptr && ( SDL_GetWindowFlags( target_window ) & SDL_WINDOW_INPUT_FOCUS ) != 0;

        const bool allowed = enabled && allow_mouse_aim && focused;
        const bool notes_allowed = enabled && allow_mouse_notes && focused;
        const bool changed = enabled != battle_input_enabled_ || target_window != mouse_window_;

        if( changed || !allowed )
        {
            set_mouse_capture( false );
            clear_mouse_aim();
        }

        if( changed || ( mouse_notes_allowed_ && !notes_allowed ) )
        {
            release( InputAction::StageMouseLeft );
            release( InputAction::StageMouseRight );
            mouse_.left_down = false;

            mouse_.right_down = false;
        }

        battle_input_enabled_ = enabled;

        mouse_aim_allowed_ = allowed;
        mouse_notes_allowed_ = notes_allowed;

        mouse_window_ = enabled ? target_window : nullptr;
        set_mouse_capture( allowed );
    }

    void InputManager::set_mouse_capture( bool enabled )
    {
        if( mouse_window_ == nullptr || enabled == mouse_relative_mode_ )
        {
            return;
        }

        if( !enabled )
        {
            std::int32_t width = 0;
            std::int32_t height = 0;
            const bool focused = ( SDL_GetWindowFlags( mouse_window_ ) & SDL_WINDOW_INPUT_FOCUS ) != 0;

            if( focused && SDL_GetWindowSize( mouse_window_, &width, &height ) )
            {
                const float cursor_x = static_cast<float>( width ) * 0.5f + mouse_aim_x_;
                const float cursor_y = static_cast<float>( height ) * 0.5f + mouse_aim_y_;
                SDL_WarpMouseInWindow( mouse_window_, cursor_x, cursor_y );
                mouse_.x = cursor_x;

                mouse_.y = cursor_y;
            }
        }

        if( SDL_SetWindowRelativeMouseMode( mouse_window_, enabled ) )
        {
            mouse_relative_mode_ = enabled;
        }
    }

    void InputManager::clear_mouse_aim()
    {
        mouse_aim_active_ = false;

        mouse_aim_x_ = 0.0f;

        mouse_aim_y_ = 0.0f;
    }

    void InputManager::update_mouse_aim( float delta_x, float delta_y )
    {
        if( !mouse_aim_allowed_ || !mouse_relative_mode_ || ( delta_x == 0.0f && delta_y == 0.0f ) )
        {
            return;
        }

        if( !mouse_aim_active_ && left_stick_strength() > aim_active_threshold )
        {
            mouse_aim_x_ = left_stick_x() * mouse_aim_radius;

            mouse_aim_y_ = left_stick_y() * mouse_aim_radius;
        }

        mouse_aim_active_ = true;

        mouse_aim_x_ += delta_x;

        mouse_aim_y_ += delta_y;
        const float length = std::hypot( mouse_aim_x_, mouse_aim_y_ );

        if( length > mouse_aim_radius )
        {
            const float scale = mouse_aim_radius / length;
            mouse_aim_x_ *= scale;

            mouse_aim_y_ *= scale;
        }
    }

    void InputManager::rebuild_keyboard_stick()
    {
        clear_mouse_aim();

        keyboard_left_x_ = 0.0f;

        keyboard_left_y_ = 0.0f;

        if( key_aim_left_ )
        {
            keyboard_left_x_ -= 1.0f;
        }

        if( key_aim_right_ )
        {
            keyboard_left_x_ += 1.0f;
        }

        if( key_aim_up_ )
        {
            keyboard_left_y_ -= 1.0f;
        }

        if( key_aim_down_ )
        {
            keyboard_left_y_ += 1.0f;
        }

        const float length = std::sqrt( keyboard_left_x_ * keyboard_left_x_ + keyboard_left_y_ * keyboard_left_y_ );

        if( length > 1.0f )
        {
            keyboard_left_x_ /= length;

            keyboard_left_y_ /= length;
        }
    }

}
