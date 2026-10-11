#pragma once

#include "Utils/EnumUtil.hpp"
#include "Managers/MenuNavigation.hpp"
#include "Managers/ControllerOrientation.hpp"

#include "GameData/GameDataRoot.hpp"
#include "Managers/AudioManager.hpp"
#include "Managers/TextureManager.hpp"
#include "Modules/MainMenu/TitleBackgroundRenderer.hpp"
#include "Modules/PikoModule.hpp"
#include "Rendering/RenderUtil.hpp"
#include "Rendering/TrueTypeText.hpp"

#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace piko::modules
{
    class OptionsPresentation final
    {
    public:
        void initialize( App &app );
        void shutdown( App &app );
        void update( App &app, double delta_seconds );
        void draw( App &app, rendering::Renderer &renderer );
        void play_sound( App &app, const char *name );
        void set_options_music( App &app, bool options );
        [[nodiscard]] managers::TextureHandle menu_texture() const;
        [[nodiscard]] managers::TextureHandle circle_texture() const;
        [[nodiscard]] double elapsed_seconds() const;
        void retain_texture( managers::TextureHandle handle );

    private:
        void prepare_submenu_audio( App &app );
        TitleBackgroundRenderer background_{};
        managers::TextureHandle menu_texture_{};
        managers::TextureHandle circle_texture_{};
        managers::AudioChannelHandle bgm_channel_{};
        managers::AudioChannelHandle sfx_channel_{};
        std::vector<managers::TextureHandle> retained_textures_{};
        double elapsed_seconds_ = 0.0;
        bool initialized_ = false;
        bool submenu_audio_loaded_ = false;
        bool options_music_active_ = false;
        bool options_music_requested_ = false;
    };

    class OptionsModule final : public PikoModule
    {
    public:
        enum class EntryPage
        {
            Main,
            GameData
        };

        explicit OptionsModule( std::shared_ptr<OptionsPresentation> presentation = {},
            EntryPage entry_page = EntryPage::Main );

        void enter( App &app ) override;
        void leave( App &app ) override;
        void handle_event( App &app, const SDL_Event &event ) override;
        void update( App &app, double delta_seconds ) override;
        void render( App &app, rendering::Renderer &renderer ) override;

    private:
        enum class Row
        {
            Languages,
            UiLanguage,
            GameLanguage,
            PlayMovies,
            Graphics,
            Audio,
            Controller,
            GameplayVisuals,
            GameModifiers,
            GameData,
            ImportGameData,
            ChartStyle,
            Back,
            RenderResolution,
            Msaa,
            Lighting,
            Bloom,
            TextureFiltering,
            Anisotropy,
            Stereo,
            InstrumentRouting,
            InstrumentMuting,
            BackgroundFx,
            InputDevice,
            ControllerOrientation,
            MouseControls,
            Count
        };

        enum class Page
        {
            Main,
            Graphics,
            Audio,
            Controller,
            Language,
            GameData,
            Count
        };

        static constexpr std::array<managers::MenuLocation, utils::enum_index( Page::Count )> menu_locations_{
            managers::MenuLocation::Options,
            managers::MenuLocation::Graphics,
            managers::MenuLocation::Audio,
            managers::MenuLocation::Controller,
            managers::MenuLocation::Languages,
            managers::MenuLocation::GameData
        };

        struct TextTextures
        {
            managers::TextureHandle normal{};
            managers::TextureHandle selected{};
        };

        static constexpr float reference_width = 1280.0f;
        static constexpr float reference_height = 720.0f;
        static constexpr float title_raster_scale = 3.0f;
        static constexpr float body_font_height = 24.0f;
        static constexpr float value_font_height = 23.0f;
        static constexpr float section_font_height = 31.0f;
        static constexpr rendering::Rect heading_rect{ 930.0f, 18.0f, 300.0f, 86.0f };
        static constexpr float row_x = 126.0f;
        static constexpr float row_width = 1028.0f;
        static constexpr float row_height = 43.0f;
        static constexpr std::array<float, 9>
            row_y{
            168.0f,
            213.0f,
            258.0f,
            389.0f,
            434.0f,
            479.0f,
            524.0f,
            569.0f,
            640.0f
        };

        [[nodiscard]] rendering::Rect reference_row_rect( std::int32_t index ) const;
        [[nodiscard]] rendering::Rect row_rect( std::int32_t index,
            std::int32_t output_width,
            std::int32_t output_height ) const;

        [[nodiscard]] static rendering::Rect controller_card_rect( std::int32_t orientation );
        [[nodiscard]] std::int32_t hit_test_controller_card( float x, float y ) const;
        [[nodiscard]] std::vector<Row> active_rows() const;
        [[nodiscard]] Row selected_row_kind() const;
        void set_page( Page page );
        void draw_controller_diagram( App &app,
            rendering::Renderer &renderer,
            const rendering::LogicalViewport &layout,
            managers::ControllerOrientation orientation ) const;

        [[nodiscard]] static const char *language_name( gamedata::GameLanguage language );
        [[nodiscard]] static const char *on_off( bool enabled );
        static void draw_texture_left_centered( rendering::Renderer &renderer,
            const rendering::Texture &texture,
            const rendering::LogicalViewport &layout,
            float x,
            float center_y,
            float raster_scale = 1.0f );

        static void draw_texture_fit( rendering::Renderer &renderer,
            const rendering::Texture &texture,
            const rendering::LogicalViewport &layout,
            const rendering::Rect &reference_bounds,
            float raster_scale,
            bool centered = false );

        void move_selection( App &app, std::int32_t delta );
        void adjust_selected( App &app, std::int32_t direction );
        void confirm_selection( App &app );
        void set_selection( App &app, std::int32_t index );
        [[nodiscard]] std::int32_t hit_test_row( const App &app, float x, float y ) const;
        [[nodiscard]] bool row_enabled( const App &app, Row row ) const;
        [[nodiscard]] bool row_adjustable( Row row ) const;
        [[nodiscard]] const char *row_label( Row row ) const;
        [[nodiscard]] std::string row_value( const App &app, Row row ) const;

        bool load_visual_assets( App &app );
        void unload_visual_assets( App &app );
        void retain_visual_assets();
        bool load_fonts( App &app );
        bool create_static_textures( App &app );
        bool create_value_textures( App &app );
        [[nodiscard]] const TextTextures *find_value_texture( App &app, const std::string &value );
        TextTextures create_text_pair( App &app,
            const std::string &name,
            const std::string &text,
            float pixel_height,
            const rendering::Color &normal_color,
            const rendering::Color &selected_color );

        managers::TextureHandle create_text_texture( App &app,
            const std::string &name,
            const std::string &text,
            float pixel_height,
            const rendering::StrokedTextStyle &style );

        void unload_text_pair( App &app, TextTextures &textures );

    private:
        static constexpr std::size_t row_count = utils::enum_index( Row::Count );

        std::shared_ptr<OptionsPresentation> presentation_{};
        managers::MenuNavigation *navigation_ = nullptr;
        managers::TextureHandle heading_texture_{};
        managers::TextureHandle controller_option_atlas_{};
        std::array<managers::TextureHandle, 3> controller_label_textures_{};
        managers::TextureHandle system_header_texture_{};
        managers::TextureHandle gameplay_header_texture_{};
        std::array<managers::TextureHandle, utils::enum_index( Page::Count ) - 1> submenu_header_textures_{};
        std::array<TextTextures, row_count> label_textures_{};
        std::unordered_map<std::string, TextTextures> value_texture_cache_{};
        rendering::TrueTypeFont title_font_{};

        gamedata::GameLanguage pending_language_ = gamedata::GameLanguage::Default;
        std::int32_t selected_row_ = 0;
        Page page_ = Page::Main;
        EntryPage entry_page_ = EntryPage::Main;
        std::int32_t last_output_width_ = 1280;
        std::int32_t last_output_height_ = 720;
        double elapsed_seconds_ = 0.0;
    };
}
