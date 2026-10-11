#pragma once

#include "FileTypes/XG/XG.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <span>
#include <string>
#include <vector>

namespace piko::filetypes::ssq
{
    struct Vec3Key
    {
        float frame = 0.0f;
        float reciprocal_frames_to_next = 0.0f;
        xg::Vec3 value{};
        bool interpolate = false;
    };

    struct QuatKey
    {
        float frame = 0.0f;
        float reciprocal_frames_to_next = 0.0f;
        xg::Quat value{};
        xg::Quat raw_value{};
        bool interpolate = false;
    };

    struct ScaleKey
    {
        float frame = 0.0f;
        float reciprocal_frames_to_next = 0.0f;
        xg::Vec3 value{ 1.0f, 1.0f, 1.0f };
        bool interpolate = false;
    };

    struct ColorKey
    {
        float frame = 0.0f;
        float reciprocal_frames_to_next = 0.0f;
        rendering::Color value{ 1.0f, 1.0f, 1.0f, 1.0f };
        bool interpolate = false;
    };

    struct LightRotationKey
    {
        float frame = 0.0f;
        float reciprocal_frames_to_next = 0.0f;
        xg::Quat value{};
        xg::Quat raw_value{};
        bool interpolate = false;
    };

    struct LightColorKey
    {
        float frame = 0.0f;
        float reciprocal_frames_to_next = 0.0f;
        rendering::Color primary{ 1.0f, 1.0f, 1.0f, 1.0f };
        rendering::Color secondary{ 1.0f, 1.0f, 1.0f, 1.0f };
        bool interpolate = false;
    };

    struct LightSetup
    {
        std::uint32_t version = 0;
        std::uint32_t num20 = 0;
        std::uint32_t num30 = 0;
        std::vector<std::uint8_t> prelude;
        std::vector<std::uint8_t> block20;
        std::vector<std::uint8_t> block30;

        std::uint32_t light_type = 0;
        bool enabled = false;
        rendering::Color primary_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        rendering::Color secondary_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        xg::Vec3 raw_direction{ 0.0f, 0.0f, -1.0f };
        xg::Vec3 direction{ 0.0f, 0.0f, 1.0f };
        xg::Quat rotation{};
        xg::Quat raw_rotation{};
        std::vector<LightRotationKey> rotation_keys;
        std::vector<LightColorKey> color_keys;
    };

    struct ModelAnimKey
    {
        float frame = 0.0f;
        float reciprocal_frames_to_next = 0.0f;
        std::uint32_t animation_index = 0;
        bool use_current_player_time_origin = false;
        bool hidden = false;
        bool state_driven = false;
        bool loop = false;
        std::uint32_t model_property = 0;
        bool transition = false;
        bool drop_shadow = false;
        std::uint32_t unknown_28 = 0;
        std::uint32_t frame_times_160 = 0;
    };

    struct Controllable
    {
        float min_angle = 0.0f;
        float max_angle = 0.0f;
        std::uint32_t descriptor = 0;
        std::uint32_t event_flag = 0;
        std::uint32_t animation_index = 0;
        std::uint32_t hold_last_frame_count = 0;
        bool forward_playback = true;
        bool loop_playback = true;
        bool use_current_global_time_origin = false;
        bool random_connection = false;
        std::vector<std::uint32_t> connections;
        std::int32_t default_event = -1;
    };

    struct ModelEntry
    {
        std::string xg_name;
        std::uint32_t is_clone = 0;
        std::uint32_t clone_id = 0;
        std::uint32_t model_type = 0;

        float xg_animation_length = 1.0f;
        float xg_animation_speed = 1.0f;
        float xg_animation_framerate = 60.0f;

        xg::Vec3 position{};
        xg::Quat rotation{};
        xg::Quat raw_rotation{};
        xg::Vec3 scale{ 1.0f, 1.0f, 1.0f };
        bool visible = true;

        std::vector<Vec3Key> position_keys;
        std::vector<QuatKey> rotation_keys;
        std::vector<ScaleKey> scale_keys;
        std::vector<ModelAnimKey> anim_keys;
        std::vector<Controllable> controllables;

        struct EffectControl
        {
            float reference_length = 0.0f;
            xg::Vec3 source_offset{};
            xg::Vec3 target_offset{};
            std::string source_model_name;
            std::string target_model_name;
        };

        std::array<std::uint32_t, 17> effect_control_words{};
        EffectControl effect_control{};
        bool has_effect_control = false;
    };

    struct FixedSpriteTransformKey
    {
        float frame = 0.0f;
        float reciprocal_frames_to_next = 0.0f;
        xg::Vec3 position{};
        float width = 0.0f;
        float height = 0.0f;
        bool hidden = false;
        std::uint32_t unknown_20 = 0;
        bool interpolate = false;
        bool external_visibility_control = false;
        std::uint32_t frame_times_160 = 0;
    };

    struct FixedSpriteFrameKey
    {
        float frame = 0.0f;
        float reciprocal_frames_to_next = 0.0f;
        float source_x = 0.0f;
        float source_y = 0.0f;
        float source_width = 0.0f;
        float source_height = 0.0f;
        bool interpolate = false;
        std::uint32_t frame_times_160 = 0;
    };

    struct FixedSpriteDescriptor
    {
        std::uint32_t texture_index = 0;
        float source_x = 0.0f;
        float source_y = 0.0f;
        float source_width = 0.0f;
        float source_height = 0.0f;
        xg::Vec3 position{};
        float width = 0.0f;
        float height = 0.0f;
        std::array<std::uint32_t, 10> render_words{};
        std::uint32_t owner_selector = 0;
    };

    struct FixedSprite
    {
        std::uint32_t version = 0;
        FixedSpriteDescriptor descriptor{};
        std::array<std::uint32_t, 16> base_words{};
        std::vector<FixedSpriteTransformKey> transform_keys;
        std::vector<ColorKey> color_keys;
        std::vector<FixedSpriteFrameKey> frame_keys;
    };

    struct TextureCutout
    {
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
    };

    struct TextureAnimFrame
    {
        float frame = 0.0f;
        float reciprocal_frames_to_next = 0.0f;
        std::uint32_t cutout_index = 0;
        std::uint32_t interpolate = 0;
    };

    struct TextureAnimation
    {
        std::uint32_t version = 0;
        std::uint32_t paste_x = 0;
        std::uint32_t paste_y = 0;
        std::string texture_name;
        std::array<std::uint32_t, 2> texture_words{};
        std::vector<TextureCutout> cutouts;
        std::vector<TextureAnimFrame> frames;
    };

    struct ProjectionKey
    {
        float frame = 0.0f;
        float reciprocal_frames_to_next = 0.0f;
        float fov_degrees = 60.0f;
        float aspect = 4.0f / 3.0f;
        float z_near = 1.0f;
        float z_far = 47000.0f;
        bool interpolate = false;
    };

    struct Camera
    {
        xg::Vec3 position{};
        xg::Quat rotation{};
        xg::Quat raw_rotation{};
        float fov_degrees = 60.0f;
        float aspect = 4.0f / 3.0f;
        float z_near = 1.0f;
        float z_far = 47000.0f;

        rendering::Color fog_color{ 0.0f, 0.0f, 0.0f, 1.0f };
        rendering::Color base_ambient_color{ 0.45f, 0.45f, 0.45f, 1.0f };
        rendering::Color base_diffuse_color{ 0.70f, 0.70f, 0.70f, 1.0f };
        rendering::Color ambient_color{ 0.45f, 0.45f, 0.45f, 1.0f };

        std::vector<Vec3Key> position_keys;
        std::vector<QuatKey> rotation_keys;
        std::vector<ProjectionKey> projection_keys;
        std::vector<ColorKey> ambient_keys;
        std::vector<LightSetup> light_setups;
    };

    struct Scene
    {
        std::uint32_t version = 0;
        std::vector<std::string> imx_entries;
        std::vector<ModelEntry> models;
        std::vector<FixedSprite> fixed_sprites;
        std::vector<TextureAnimation> texture_animations;
        Camera camera{};
    };

    class DecodeError final : public std::runtime_error
    {
    public:
        explicit DecodeError( const std::string &message );
    };

    [[nodiscard]] Scene decode( std::span<const std::uint8_t> bytes );
    [[nodiscard]] Scene load_file( const std::filesystem::path &path );
}
