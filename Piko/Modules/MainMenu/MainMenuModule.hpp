#pragma once

#include "Managers/TextureManager.hpp"
#include "Modules/PikoModule.hpp"
#include "Rendering/RenderUtil.hpp"

#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>

namespace piko::modules
{
    class OptionsPresentation;

    class MainMenuModule final : public PikoModule
    {
    public:
        explicit MainMenuModule( std::shared_ptr<OptionsPresentation> presentation = {} );
        void enter( App &app ) override;
        void leave( App &app ) override;

        void handle_event( App &app, const SDL_Event &event ) override;
        void update( App &app, double delta_seconds ) override;
        void render( App &app, rendering::Renderer &renderer ) override;

    private:
        enum class PendingAction
        {
            None,
            SingleMode,
            Collection,
            Profiles,
            Options,
            Exit
        };

        struct MenuTextTextures
        {
            managers::TextureHandle normal{};
            managers::TextureHandle selected{};
        };

        struct PortraitDefinition
        {
            std::size_t texture_index = 0u;
            rendering::Rect source{};
            float scale = 1.0f;
        };

        static float menu_item_center_y( std::int32_t index );
        static rendering::Rect menu_item_reference_rect( std::int32_t index );
        static rendering::Rect menu_item_rect( std::int32_t index,
            std::int32_t output_width,
            std::int32_t output_height );

        static std::filesystem::path title_font_path( const App &app );
        static std::filesystem::path footer_font_path( const App &app );
        static std::filesystem::path title_logo_path( const App &app );
        static void draw_sprite( rendering::Renderer &renderer,
            const rendering::Texture &texture,
            const rendering::Rect &source,
            const rendering::Rect &destination,
            const rendering::Color &tint = rendering::Color{} );

        static void draw_atlas_sprite_centered( rendering::Renderer &renderer,
            const rendering::Texture &texture,
            const rendering::Rect &source,
            float center_x,
            float center_y,
            float scale,
            const rendering::Color &tint = rendering::Color{} );

        static void draw_texture_fit_centered( rendering::Renderer &renderer,
            const rendering::Texture &texture,
            const rendering::Rect &bounds,
            const rendering::Color &tint = rendering::Color{} );

        static void draw_soft_panel( rendering::Renderer &renderer, const rendering::Rect &rect );

        void move_selection( App &app, std::int32_t delta );
        void set_selection( std::int32_t index );
        void confirm_selection( App &app );

        bool load_menu_textures( App &app );
        void unload_menu_textures( App &app );
        void draw_footer( App &app, rendering::Renderer &renderer, const rendering::LogicalViewport &layout );
        void load_title_art( App &app );
        void unload_title_art( App &app );
        std::int32_t hit_test_menu_item( float x, float y ) const;

    private:
        static constexpr std::size_t menu_item_count = 5u;
        static constexpr std::size_t character_texture_count = 5u;

        static constexpr const char *title_logo_texture_name_ = "mainmenu/piko_logo";
        static constexpr const char *title_font_filename_ = "Anak Paud.ttf";
        static constexpr const char *footer_font_filename_ = "noto-sans.regular.ttf";
        static constexpr float reference_width_ = 640.0f;
        static constexpr float reference_height_ = 448.0f;
        static constexpr float menu_item_center_x_ = 490.0f;
        static constexpr float menu_item_first_center_y_ = 115.0f;
        static constexpr float menu_item_gap_ = 57.0f;
        static constexpr float menu_item_width_ = 292.0f;
        static constexpr float menu_item_height_ = 40.0f;
        static constexpr float puma_center_x_ = 292.0f;
        static constexpr float portrait_center_x_ = 170.0f;
        static constexpr float portrait_center_y_ = 268.0f;
        static constexpr float title_text_height_ = 44.0f;
        static constexpr float title_text_raster_scale_ = 3.0f;
        static constexpr float footer_text_height_ = 12.0f;
        static constexpr float footer_text_raster_scale_ = 3.0f;
        static constexpr double transition_duration_ = 0.22;
        static constexpr std::array<const char *, menu_item_count>
            menu_labels_{
            "@ui.single_player",
            "@ui.collection",
            "@ui.profiles",
            "@ui.options",
            "@ui.exit"
        };

        static constexpr std::array<const char *, character_texture_count>
            character_texture_names_{
            "U-1_SP.png",
            "KAZUYA.png",
            "PIKO.png",
            "BAG.png",
            "MINMIN.png"
        };

        static constexpr std::array<const char *, character_texture_count> character_texture_keys_{
            "mainmenu/u-1_sp.png",
            "mainmenu/kazuya.png",
            "mainmenu/piko.png",
            "mainmenu/bag.png",
            "mainmenu/minmin.png"
        };

        static constexpr std::array<PortraitDefinition, menu_item_count> portrait_definitions_{
            PortraitDefinition{ 0u, rendering::Rect{ 0.0f, 0.0f, 256.0f, 256.0f }, 0.98f },
            PortraitDefinition{ 2u, rendering::Rect{ 0.0f, 0.0f, 256.0f, 256.0f }, 0.95f },
            PortraitDefinition{ 1u, rendering::Rect{ 0.0f, 0.0f, 256.0f, 256.0f }, 0.95f },
            PortraitDefinition{ 4u, rendering::Rect{ 0.0f, 0.0f, 256.0f, 256.0f }, 0.95f },
            PortraitDefinition{ 3u, rendering::Rect{ 0.0f, 0.0f, 256.0f, 256.0f }, 0.95f }
        };

        managers::TextureHandle menu_texture_{};
        managers::TextureHandle title_logo_texture_{};
        std::array<managers::TextureHandle, character_texture_count> character_textures_{};
        std::array<MenuTextTextures, menu_item_count> menu_textures_{};
        std::array<managers::TextureHandle, 3> footer_textures_{};
        std::shared_ptr<OptionsPresentation> presentation_{};

        std::int32_t selected_item_ = 0;
        std::int32_t previous_portrait_item_ = 0;
        std::int32_t last_output_width_ = 1280;
        std::int32_t last_output_height_ = 720;
        float puma_y_ = 160.0f;
        double portrait_blend_ = 1.0;

        PendingAction pending_action_ = PendingAction::None;
        double pending_transition_seconds_ = 0.0;
    };
}
