#pragma once

#include "Utils/BattleTexturePixels.hpp"

#include "Managers/MenuNavigation.hpp"

#include "Managers/TextureManager.hpp"
#include "Managers/InterfaceStyle.hpp"
#include "Localization/Language.hpp"
#include "Modules/Stage/StageGuardRenderer.hpp"
#include "Modules/PikoModule.hpp"
#include "Rendering/TrueTypeText.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
#include <vector>

namespace piko::managers
{
    struct GameSettings;
    struct GameplayVisualStyle;
}

namespace piko::rendering
{
    struct Color;
    struct Rect;
    struct Texture;
    struct VertexColor2D;
}

namespace piko::runtime::stage
{
    struct StageGameplayView;
}

namespace piko::modules
{
    class OptionsPresentation;

    class GameplayVisualModule final : public PikoModule
    {
    public:
        explicit GameplayVisualModule( std::shared_ptr<OptionsPresentation> presentation );
        void enter( App &app ) override;
        void leave( App &app ) override;
        void handle_event( App &app, const SDL_Event &event ) override;
        void update( App &app, double delta_seconds ) override;
        void render( App &app, rendering::Renderer &renderer ) override;

    private:
        enum class View
        {
            Menu,
            Cone,
            Nub,
            AimArrow,
            Line,
            Notes,
            Guard,
            AimMenu,
            FontList,
            Count
        };

        static constexpr std::array<managers::MenuLocation, utils::enum_index( View::Count )> menu_locations_{
            managers::MenuLocation::Interface,
            managers::MenuLocation::VisionCone,
            managers::MenuLocation::CentreIcon,
            managers::MenuLocation::AimArrow,
            managers::MenuLocation::TraceLine,
            managers::MenuLocation::TraceNotes,
            managers::MenuLocation::Guard,
            managers::MenuLocation::Aiming,
            managers::MenuLocation::Fonts
        };

        enum class SettingRow
        {
            AimEntry,
            MenuFont,
            FontChoice,
            VisionConeEntry,
            NubEntry,
            AimArrowEntry,
            TraceLineEntry,
            NotesEntry,
            GuardsEntry,
            SaveProfileEntry,
            LoadProfileEntry,
            TraceScale,
            NoteStyle,
            NoteScale,
            CentreStyle,
            CenterNub,
            PressedNub,
            ConeLength,
            ConeAngle,
            ConeInnerDiameter,
            ConeLineWidth,
            ArrowScale,
            ArrowWidth,
            WingStyle,
            WingSpread,
            WingScale,
            WingWidth,
            ArrowOffsetX,
            WingOffsetX,
            TraceIdle,
            TraceLocked,
            TraceLost,
            Note,
            HeldNote,
            NoteBody,
            ConeIdleLine,
            ConeIdleFill,
            ConeLockedLine,
            ConeLockedFill,
            GuardDpad,
            GuardSize,
            GuardIcons,
            GuardPop,
            GuardFade,
            GuardRails,
            GuardRailColor,
            ResetDefault,
            Back
        };

        enum class FileDialogAction
        {
            LoadProfile,
            SaveProfile
        };

        enum class ColorTarget
        {
            None,
            TraceIdle,
            TraceLocked,
            TraceLost,
            Note,
            HeldNote,
            NoteBody,
            ConeIdleLine,
            ConeIdleFill,
            ConeLockedLine,
            ConeLockedFill,
            GuardRailColor
        };

        enum class PickerDrag
        {
            None,
            SaturationValue,
            Hue
        };

        struct PreviewTraceLine;
        struct TextEntry;
        enum class TextStyle
        {
            Row,
            Heading,
            Section
        };

        static constexpr float panel_margin = 34.0f;
        static constexpr float row_top = 142.0f;
        static constexpr float row_height = 40.0f;
        static constexpr float row_gap = 2.0f;
        static constexpr float section_gap = 8.0f;
        static constexpr float label_text_size = 22.0f;
        static constexpr float value_text_size = 21.0f;
        static constexpr float preview_heading_height = 16.0f;
        static constexpr float preview_heading_margin = 8.0f;
        static constexpr float picker_width = 500.0f;
        static constexpr float picker_height = 480.0f;
        static constexpr float tau = 6.28318530717958647692f;
        static constexpr std::array<std::array<float, 4>, 8> picker_presets{
            {
                { 16.0f / 255.0f, 213.0f / 255.0f, 1.0f, 1.0f },
                { 40.0f / 255.0f, 225.0f / 255.0f, 50.0f / 255.0f, 1.0f },
                { 0.0f, 0.0f, 1.0f, 1.0f },
                { 1.0f, 176.0f / 255.0f, 0.0f, 1.0f },
                { 1.0f, 150.0f / 255.0f, 50.0f / 255.0f, 1.0f },
                { 210.0f / 255.0f, 110.0f / 255.0f, 110.0f / 255.0f, 1.0f },
                { 0.42f, 0.24f, 0.88f, 1.0f },
                { 0.92f, 0.94f, 1.0f, 1.0f }
            }
        };

        [[nodiscard]] static rendering::Rect settings_panel( std::int32_t output_width,
            std::int32_t output_height,
            bool full_width = false );

        [[nodiscard]] static rendering::Rect preview_panel( std::int32_t output_width, std::int32_t output_height );
        [[nodiscard]] rendering::Rect row_rect( std::int32_t index,
            std::int32_t output_width,
            std::int32_t output_height ) const;

        [[nodiscard]] bool row_adjustable( std::int32_t index ) const;

        [[nodiscard]] static float row_height_for( std::int32_t output_width, std::int32_t output_height );
        [[nodiscard]] static float row_gap_for( std::int32_t output_width, std::int32_t output_height );
        [[nodiscard]] std::int32_t visible_row_count() const;
        void ensure_selection_visible();
        void scroll_rows( std::int32_t delta );
        [[nodiscard]] static rendering::Color array_color( const std::array<float, 4> &value );
        [[nodiscard]] static std::string format_float( float value, std::int32_t precision = 2 );
        [[nodiscard]] static std::string hex_color( const std::array<float, 4> &value );
        [[nodiscard]] static std::optional<std::array<float, 4>> parse_hex_color( std::string_view text, float alpha );
        [[nodiscard]] static std::array<float, 3> rgb_to_hsv( const std::array<float, 4> &color );
        [[nodiscard]] static rendering::Color hsv_to_color( float hue,
            float saturation,
            float value,
            float alpha = 1.0f );

        static void push_quad( std::vector<rendering::VertexColor2D> &vertices,
            const rendering::Rect &rect,
            const rendering::Color &top_left,
            const rendering::Color &top_right,
            const rendering::Color &bottom_right,
            const rendering::Color &bottom_left );

        [[nodiscard]] static rendering::Rect picker_panel( std::int32_t output_width, std::int32_t output_height );
        [[nodiscard]] static rendering::Rect picker_sv_rect( const rendering::Rect &panel );
        [[nodiscard]] static rendering::Rect picker_hue_rect( const rendering::Rect &panel );
        [[nodiscard]] static rendering::Rect picker_rect( const rendering::Rect &panel,
            float x,
            float y,
            float w,
            float h );

        [[nodiscard]] static rendering::Rect picker_code_rect( const rendering::Rect &panel );
        [[nodiscard]] static rendering::Rect picker_copy_rect( const rendering::Rect &panel );
        [[nodiscard]] static rendering::Rect picker_paste_rect( const rendering::Rect &panel );
        [[nodiscard]] static rendering::Rect picker_done_rect( const rendering::Rect &panel );
        [[nodiscard]] static rendering::Rect picker_cancel_rect( const rendering::Rect &panel );
        [[nodiscard]] static rendering::Rect picker_preset_rect( const rendering::Rect &panel, std::int32_t index );
        static void draw_checkerboard( rendering::Renderer &renderer, const rendering::Rect &rect, float cell_size );
        static void draw_rounded_panel( rendering::Renderer &renderer, const rendering::Rect &rect );
        [[nodiscard]] static std::string cached_text_name( const std::string &value,
            float pixel_height,
            TextStyle style,
            bool selected );

        void prepare_text( App &app, const std::string &text, float pixel_height, TextStyle style, TextEntry &entry );
        void prepare_row_text( App &app, float text_scale );

        float draw_stroked_text( App &app,
            rendering::Renderer &renderer,
            const std::string &text,
            float x,
            float center_y,
            float pixel_height,
            bool selected,
            TextEntry &entry,
            bool right_aligned = false,
            TextStyle style = TextStyle::Row,
            float maximum_width = 0.0f,
            bool centered = false );

        static void draw_preview_cone_geometry( rendering::Renderer &renderer,
            const managers::GameSettings &settings,
            const rendering::Vec2 &marker,
            float cone_degrees,
            float geometry_scale,
            float length_scale,
            float maximum_length,
            float off_center_mix,
            const rendering::Texture *arrow_texture,
            const rendering::Texture *wing_texture );

        [[nodiscard]] static PreviewTraceLine make_preview_trace_line( const runtime::stage::StageGameplayView &view,
            float line_length_pixels,
            float wave_amplitude_pixels = 0.0f,
            std::int64_t current_chart_sample = 0,
            std::int64_t total_chart_samples = 0 );

        static void draw_preview_trace_line( rendering::Renderer &renderer,
            const rendering::Texture *trace_texture,
            const managers::GameplayVisualStyle &style,
            const runtime::stage::StageGameplayView &view,
            const PreviewTraceLine &line,
            const rendering::Color &trace_color );

        static void draw_preview( rendering::Renderer &renderer,
            const rendering::Rect &panel,
            View view,
            const managers::GameSettings &settings,
            const rendering::Texture *trace_texture,
            const rendering::Texture *tube_texture,
            const rendering::Texture *guard_texture,
            const rendering::Texture *arrow_texture,
            const rendering::Texture *wing_texture,
            const rendering::Texture *normal_nub_texture,
            const rendering::Texture *pressed_nub_texture,
            const stage::GuardIconTextures *custom_guard_icons,
            float time_seconds,
            const ui::NoteTextureRegions &texture_regions );

        void refresh_note_textures( App &app );
        void refresh_nub_textures( App &app );
        void refresh_guard_icons( App &app );
        void apply_interface_style( App &app );
        void open_font_list( const App &app, managers::InterfaceFont target );
        void close_submenu();
        void draw_interface_preview( App &app, rendering::Renderer &renderer, const rendering::Rect &panel );
        [[nodiscard]] bool full_width() const;

        void move_selection( App &app, std::int32_t delta );
        void adjust_selected( App &app, std::int32_t direction );
        void confirm_selection( App &app );
        void set_selection( App &app, std::int32_t index );
        void set_view( View view );
        void play_menu_sound( App &app, const char *name );
        [[nodiscard]] std::int32_t row_count() const;
        [[nodiscard]] SettingRow row_kind( std::int32_t index ) const;
        [[nodiscard]] static const char *view_name( View view );
        [[nodiscard]] std::int32_t hit_test_row( float x, float y ) const;
        [[nodiscard]] const char *row_label( std::int32_t index ) const;
        [[nodiscard]] std::string row_value( const App &app, std::int32_t index ) const;
        [[nodiscard]] ColorTarget row_color_target( std::int32_t index ) const;
        [[nodiscard]] std::array<float, 4> *target_color( App &app );
        [[nodiscard]] const std::array<float, 4> *target_color( const App &app ) const;
        void open_color_picker( App &app, ColorTarget target );
        void close_color_picker( App &app, bool accept );
        void update_color_picker( App &app );
        void apply_picker_color( App &app );
        void set_picker_color( App &app, const std::array<float, 4> &color );
        void focus_picker_code( App &app, bool focused );
        void edit_picker_code( App &app, std::string text );
        void paste_picker_code( App &app );
        void handle_picker_event( App &app, const SDL_Event &event );

    private:
        struct TextEntry
        {
            std::string text{};
            managers::TextureHandle normal{};
            managers::TextureHandle selected{};
            float pixel_height = 0.0f;
            TextStyle style = TextStyle::Row;
            bool reusable = false;
        };

        std::shared_ptr<OptionsPresentation> presentation_{};
        rendering::TrueTypeFont title_font_{};
        std::vector<TextEntry> label_text_{
            14
        };

        std::vector<TextEntry> value_text_{
            14
        };

        std::array<TextEntry, 5> heading_text_{};
        std::array<TextEntry, 7> picker_text_{};
        View view_ = View::Menu;
        View font_return_view_ = View::Menu;
        managers::InterfaceFont font_target_ = managers::InterfaceFont::Menu;
        std::vector<localization::FontFile> font_files_{};
        managers::TextureHandle font_preview_{};
        std::string font_preview_path_{};
        struct FileDialogState;
        std::shared_ptr<FileDialogState> file_dialog_{};
        void open_file_dialog( App &app, FileDialogAction action );
        void consume_file_dialog( App &app );
        static void file_dialog_callback( void *userdata, const char *const *files, std::int32_t filter );
        void refresh_aim_styles( App &app );
        std::vector<std::string> aim_style_names_{};
        std::vector<std::string> centre_style_names_{};
        std::vector<std::string> note_style_names_{};
        std::string profile_status_{};
        managers::MenuNavigation *navigation_ = nullptr;
        std::int32_t selected_row_ = 0;
        std::int32_t first_visible_row_ = 0;
        bool scrollbar_dragging_ = false;
        std::int32_t last_output_width_ = 1280;
        std::int32_t last_output_height_ = 720;
        double elapsed_seconds_ = 0.0;
        ColorTarget color_target_ = ColorTarget::None;
        PickerDrag picker_drag_ = PickerDrag::None;
        std::string picker_hex_text_{};
        bool picker_code_focused_ = false;
        bool picker_code_selected_ = false;
        bool picker_hex_valid_ = true;
        bool picker_key_consumed_ = false;
        bool picker_hue_focused_ = false;
        float picker_hue_ = 0.0f;
        float picker_saturation_ = 0.0f;
        float picker_value_ = 1.0f;
        float picker_alpha_ = 1.0f;
        std::array<float, 4> picker_original_color_{};
        managers::TextureHandle preview_trace_texture_{};
        ui::NoteTextureRegions preview_note_regions_{};
        managers::TextureHandle preview_tube_texture_{};
        managers::TextureHandle preview_guard_texture_{};
        managers::TextureHandle preview_arrow_texture_{};
        managers::TextureHandle preview_wing_texture_{};
        managers::TextureHandle preview_normal_nub_texture_{};
        managers::TextureHandle preview_pressed_nub_texture_{};
        stage::GuardIconHandles preview_custom_guard_handles_{};
        std::vector<std::string> guard_icon_set_names_;
    };
}
