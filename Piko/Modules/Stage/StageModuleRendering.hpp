#pragma once

#include "Rendering/Renderer.hpp"
#include "Managers/TextureManager.hpp"
#include "Managers/GameSettings.hpp"
#include "Utils/BattleTexturePixels.hpp"

namespace piko::modules::stage
{
    [[nodiscard]] rendering::Vec2 gameplay_center( const rendering::Rect &area, const rendering::Vec2 &offset = {} );

    [[nodiscard]] managers::TextureHandle load_note_textures( managers::TextureManager &textures,
        const std::filesystem::path &game_data_root,
        ui::NoteTextureRegions &regions,
        const char *name = "stage/note-tube",
        std::string_view style = "default" );

    [[nodiscard]] managers::TextureHandle load_trace_texture( managers::TextureManager &textures,
        const std::filesystem::path &game_data_root,
        const char *name = "stage/trace-line",
        std::string_view style = "default" );

    [[nodiscard]] managers::TextureHandle load_aim_sprite( managers::TextureManager &textures,
        const std::filesystem::path &resource_root,
        const char *name,
        const char *filename,
        std::string_view style = "default" );

    [[nodiscard]] managers::TextureHandle load_centre_sprite( managers::TextureManager &textures,
        const std::filesystem::path &game_data_root,
        const char *name,
        const managers::GameplayVisualStyle &style,
        bool pressed );

    [[nodiscard]] std::vector<std::string> note_style_names( const std::filesystem::path &resource_root );

    [[nodiscard]] std::vector<std::string> centre_style_names( const std::filesystem::path &resource_root );

    [[nodiscard]] std::vector<std::string> aim_style_names( const std::filesystem::path &resource_root );

    struct ViewConeStartPoints
    {
        rendering::Vec2 upper;
        rendering::Vec2 lower;
    };

    [[nodiscard]] ViewConeStartPoints view_cone_start_points( const rendering::Vec2 &marker,
        const rendering::Vec2 &perpendicular,
        float inner_diameter );

    void draw_view_cone_gradient( rendering::Renderer &renderer,
        const rendering::Vec2 &marker,
        const rendering::Vec2 &upper_start,
        const rendering::Vec2 &lower_start,
        float cone_radians,
        float half_angle_radians,
        float cone_length,
        const rendering::Color &fill_color );

    void draw_view_cone_border( rendering::Renderer &renderer,
        const rendering::Vec2 &marker,
        const rendering::Vec2 &end,
        float width,
        const rendering::Color &color );

    void draw_view_cone_arrows( rendering::Renderer &renderer,
        const rendering::Vec2 &marker,
        const rendering::Vec2 &axis,
        const rendering::Vec2 &perpendicular,
        float visual_alpha,
        float slide_progress,
        float cone_half_height,
        float scale,
        const managers::GameplayVisualStyle &style,
        const rendering::Texture *arrow_texture,
        const rendering::Texture *wing_texture );

    float draw_view_cone_center_nub( rendering::Renderer &renderer,
        const rendering::Vec2 &marker,
        bool pressed,
        float scale,
        float normal_scale,
        float pressed_scale,
        const rendering::Texture *normal_texture = nullptr,
        const rendering::Texture *pressed_texture = nullptr,
        const rendering::Texture *original_texture = nullptr );

}
