#pragma once

#include "Managers/ControllerOrientation.hpp"

#include <SDL3/SDL.h>

#include "Utils/EnumUtil.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace piko::managers
{
    enum class InputAction : std::uint8_t
    {
        MenuUp,
        MenuDown,
        MenuLeft,
        MenuRight,
        Confirm,
        Back,

        Pause,

        StageAction,
        StageGuardLeft,
        StageGuardDown,
        StageGuardRight,
        StageGuardUp,

        StageDpadGuardLeft,
        StageDpadGuardDown,
        StageDpadGuardRight,
        StageDpadGuardUp,

        StageKeyboardGuardLeft,
        StageKeyboardGuardDown,
        StageKeyboardGuardRight,
        StageKeyboardGuardUp,
        StageMouseLeft,
        StageMouseRight,

        StageExit,

        Count
    };

    struct MouseState
    {
        float x = -1.0f;
        float y = -1.0f;

        bool moved = false;
        bool left_down = false;
        bool right_down = false;
        bool left_clicked = false;
        bool right_clicked = false;
    };

    struct MouseAimState
    {
        float x = 0.0f;
        float y = 0.0f;
        bool active = false;
    };

    enum class InputTransitionKind : std::uint8_t
    {
        Pressed,
        Released
    };

    struct InputTransition
    {
        InputAction action = InputAction::Confirm;
        InputTransitionKind kind = InputTransitionKind::Pressed;
        std::uint64_t timestamp_ns = 0;
        std::uint64_t event_serial = 0;
    };

    class InputManager
    {
    public:
        struct Device
        {
            SDL_JoystickID id = 0;
            std::string name;
        };

        bool initialize();
        void shutdown();

        void begin_frame();
        void update( double delta_seconds );
        void handle_event( const SDL_Event &event );

        [[nodiscard]] bool pressed( InputAction action ) const;
        [[nodiscard]] bool down( InputAction action ) const;
        [[nodiscard]] bool released( InputAction action ) const;
        [[nodiscard]] bool down_at_frame_start( InputAction action ) const;
        [[nodiscard]] const std::vector<InputTransition> &transitions() const;
        [[nodiscard]] std::uint64_t frame_start_timestamp_ns() const;
        [[nodiscard]] const MouseState &mouse() const;
        [[nodiscard]] MouseAimState mouse_aim() const;

        [[nodiscard]] bool gamepad_connected() const;
        [[nodiscard]] const std::string &gamepad_name() const;
        [[nodiscard]] std::vector<Device> available_devices() const;
        [[nodiscard]] SDL_JoystickID selected_device() const;
        bool select_device( SDL_JoystickID id );
        void set_controller_orientation( ControllerOrientation orientation );
        void set_battle_input( SDL_Window *window, bool enabled, bool allow_mouse_aim, bool allow_mouse_notes );

        // Type B uses the right stick
        [[nodiscard]] float left_stick_x() const;
        [[nodiscard]] float left_stick_y() const;
        [[nodiscard]] float left_stick_strength() const;
        bool rumble( float strength, std::uint32_t duration_ms );
        bool rumble_motors( float low_strength, float high_strength, std::uint32_t duration_ms );

    private:
        struct StickValues
        {
            float x = 0.0f;
            float y = 0.0f;
        };

        [[nodiscard]] static float normalize_axis( std::int16_t value );
        [[nodiscard]] static StickValues apply_radial_deadzone( std::int16_t raw_x,
            std::int16_t raw_y,
            std::int16_t inner_deadzone,
            std::int16_t outer_deadzone );

        [[nodiscard]] static bool is_joystick_south_button( std::uint8_t button );
        [[nodiscard]] static bool is_joystick_east_button( std::uint8_t button );
        [[nodiscard]] static bool is_joystick_west_button( std::uint8_t button );
        [[nodiscard]] static bool is_joystick_north_button( std::uint8_t button );
        [[nodiscard]] static bool is_joystick_back_button( std::uint8_t button );
        [[nodiscard]] static bool is_joystick_start_button( std::uint8_t button );
        [[nodiscard]] static bool is_joystick_dpad_up_button( std::uint8_t button );
        [[nodiscard]] static bool is_joystick_dpad_down_button( std::uint8_t button );
        [[nodiscard]] static bool is_joystick_dpad_left_button( std::uint8_t button );
        [[nodiscard]] static bool is_joystick_dpad_right_button( std::uint8_t button );

        void open_first_gamepad();
        void open_first_joystick();
        void open_gamepad( SDL_JoystickID joystick_id );
        void open_joystick( SDL_JoystickID joystick_id );
        void close_gamepad();
        void close_joystick();

        struct MenuRepeatState
        {
            std::int32_t direction = 0;
            double seconds = 0.0;
        };

        void update_menu_repeat( double delta_seconds );
        void repeat_menu_direction( bool negative,
            bool positive,
            InputAction negative_action,
            InputAction positive_action,
            double delta_seconds,
            MenuRepeatState &state );

        void tap( InputAction action );
        void press( InputAction action );
        void release( InputAction action );
        void rebuild_keyboard_stick();
        void set_mouse_capture( bool enabled );
        void clear_mouse_aim();
        void update_mouse_aim( float delta_x, float delta_y );
        void refresh_live_stick_state();
        [[nodiscard]] InputAction mapped_guard_action( std::int32_t physical_index ) const;
        void set_left_stick_from_raw_axes( std::int16_t raw_x, std::int16_t raw_y );

    private:
        static constexpr std::int16_t stick_inner_deadzone = 7000;
        static constexpr std::int16_t stick_outer_deadzone = 2400;
        static constexpr float stick_filter_seconds = 0.060f;
        static constexpr float stick_filter_snap_distance = 0.10f;
        static constexpr float stick_angle_deadband_radians = 0.017453292519943295f;
        static constexpr float stick_cardinal_snap = 0.055f;
        static constexpr float stick_cardinal_snap_strength = 0.85f;

        static constexpr double menu_repeat_delay = 0.5;
        static constexpr double menu_repeat_interval = 0.10;
        static constexpr float mouse_aim_radius = 200.0f;
        static constexpr float mouse_aim_deadzone = 3.0f;
        static constexpr float mouse_takeover_threshold = 0.025f;
        static constexpr float aim_active_threshold = 0.20f;
        MenuRepeatState horizontal_menu_repeat_{};
        MenuRepeatState vertical_menu_repeat_{};

        std::array<bool, utils::enum_index( InputAction::Count )> action_pressed_{};
        std::array<bool, utils::enum_index( InputAction::Count )> action_down_{};
        std::array<bool, utils::enum_index( InputAction::Count )> action_released_{};
        std::array<bool, utils::enum_index( InputAction::Count )> frame_start_down_{};
        std::vector<InputTransition> transitions_{};
        std::uint64_t frame_start_timestamp_ns_ = 0;
        std::uint64_t active_event_timestamp_ns_ = 0;
        std::uint64_t active_event_serial_ = 0;
        std::uint64_t next_event_serial_ = 0;

        SDL_Gamepad *gamepad_ = nullptr;
        SDL_JoystickID gamepad_id_ = 0;
        std::string gamepad_name_{};

        SDL_Joystick *joystick_ = nullptr;
        SDL_JoystickID joystick_id_ = 0;
        ControllerOrientation controller_orientation_ = ControllerOrientation::TypeA;
        std::string joystick_name_{};

        bool stick_up_pressed_ = false;
        bool stick_down_pressed_ = false;
        bool joystick_hat_up_pressed_ = false;
        bool joystick_hat_down_pressed_ = false;
        bool joystick_hat_left_pressed_ = false;
        bool joystick_hat_right_pressed_ = false;

        float gamepad_left_x_ = 0.0f;
        float gamepad_left_y_ = 0.0f;
        float gamepad_left_target_x_ = 0.0f;
        float gamepad_left_target_y_ = 0.0f;
        float keyboard_left_x_ = 0.0f;
        float keyboard_left_y_ = 0.0f;
        double rumble_remaining_seconds_ = 0.0;

        bool key_aim_left_ = false;
        bool key_aim_right_ = false;
        bool key_aim_up_ = false;
        bool key_aim_down_ = false;

        MouseState mouse_{};
        SDL_Window *mouse_window_ = nullptr;
        bool battle_input_enabled_ = false;
        bool mouse_relative_mode_ = false;
        bool mouse_aim_allowed_ = false;
        bool mouse_notes_allowed_ = false;
        bool mouse_aim_active_ = false;
        float mouse_aim_x_ = 0.0f;
        float mouse_aim_y_ = 0.0f;
    };
}
