#include "SSQJson.hpp"

#include "json.hpp"

#include <cmath>
#include <fstream>
#include <stdexcept>

namespace piko::filetypes::ssq::json
{
    using Json = nlohmann::json;

    Json vec3( const xg::Vec3 &value )
    {
        return Json::array( { value.x, value.y, value.z } );
    }

    xg::Vec3 vec3( const Json &value )
    {
        return xg::Vec3{ value.at( 0 ).get<float>(), value.at( 1 ).get<float>(), value.at( 2 ).get<float>() };
    }

    Json quat( const xg::Quat &value )
    {
        return Json::array( { value.x, value.y, value.z, value.w } );
    }

    xg::Quat quat( const Json &value )
    {
        return xg::Quat{
            value.at( 0 ).get<float>(),
            value.at( 1 ).get<float>(),
            value.at( 2 ).get<float>(),
            value.at( 3 ).get<float>()
        };
    }

    Json color( const rendering::Color &value )
    {
        return Json::array( { value.r, value.g, value.b, value.a } );
    }

    rendering::Color color( const Json &value )
    {
        return rendering::Color{
            value.at( 0 ).get<float>(),
            value.at( 1 ).get<float>(),
            value.at( 2 ).get<float>(),
            value.at( 3 ).get<float>()
        };
    }

    template <typename T> Json array( const std::vector<T> &values, Json ( *writer )( const T & ) )
    {
        Json output = Json::array();

        for( const T &value : values )
        {
            output.push_back( writer( value ) );
        }

        return output;
    }

    template <typename T> std::vector<T> vector( const Json &values, T ( *reader )( const Json & ) )
    {
        std::vector<T> output;
        output.reserve( values.size() );

        for( const Json &value : values )
        {
            output.push_back( reader( value ) );
        }

        return output;
    }

    void sanitize_numbers( Json &value )
    {
        if( value.is_number_float() )
        {
            const double number = value.get<double>();

            if( !std::isfinite( number ) )
            {
                value = 0.0;
            }

            return;
        }

        if( value.is_array() || value.is_object() )
        {
            for( auto &item : value )
            {
                sanitize_numbers( item );
            }
        }
    }

    Json vec3_key( const Vec3Key &value )
    {
        return Json{
            { "frame", value.frame },
            { "reciprocal_frames_to_next", value.reciprocal_frames_to_next },
            { "value", vec3( value.value ) },
            { "interpolate", value.interpolate }
        };
    }

    Vec3Key vec3_key( const Json &value )
    {
        Vec3Key output{};
        output.frame = value.value( "frame", 0.0f );

        output.reciprocal_frames_to_next = value.value( "reciprocal_frames_to_next", 0.0f );

        output.value = vec3( value.at( "value" ) );

        output.interpolate = value.value( "interpolate", false );
        return output;
    }

    Json quat_key( const QuatKey &value )
    {
        return Json{
            { "frame", value.frame },
            { "reciprocal_frames_to_next", value.reciprocal_frames_to_next },
            { "value", quat( value.value ) },
            { "raw_value", quat( value.raw_value ) },
            { "interpolate", value.interpolate }
        };
    }

    QuatKey quat_key( const Json &value )
    {
        QuatKey output{};
        output.frame = value.value( "frame", 0.0f );

        output.reciprocal_frames_to_next = value.value( "reciprocal_frames_to_next", 0.0f );

        output.value = quat( value.at( "value" ) );

        output.raw_value = quat( value.value( "raw_value", value.at( "value" ) ) );

        output.interpolate = value.value( "interpolate", false );
        return output;
    }

    Json scale_key( const ScaleKey &value )
    {
        return Json{
            { "frame", value.frame },
            { "reciprocal_frames_to_next", value.reciprocal_frames_to_next },
            { "value", vec3( value.value ) },
            { "interpolate", value.interpolate }
        };
    }

    ScaleKey scale_key( const Json &value )
    {
        ScaleKey output{};
        output.frame = value.value( "frame", 0.0f );

        output.reciprocal_frames_to_next = value.value( "reciprocal_frames_to_next", 0.0f );

        output.value = vec3( value.at( "value" ) );

        output.interpolate = value.value( "interpolate", false );
        return output;
    }

    Json color_key( const ColorKey &value )
    {
        return Json{
            { "frame", value.frame },
            { "reciprocal_frames_to_next", value.reciprocal_frames_to_next },
            { "value", color( value.value ) },
            { "interpolate", value.interpolate }
        };
    }

    ColorKey color_key( const Json &value )
    {
        ColorKey output{};
        output.frame = value.value( "frame", 0.0f );

        output.reciprocal_frames_to_next = value.value( "reciprocal_frames_to_next", 0.0f );

        output.value = color( value.at( "value" ) );

        output.interpolate = value.value( "interpolate", false );
        return output;
    }

    Json light_rotation_key( const LightRotationKey &value )
    {
        return Json{
            { "frame", value.frame },
            { "reciprocal_frames_to_next", value.reciprocal_frames_to_next },
            { "value", quat( value.value ) },
            { "raw_value", quat( value.raw_value ) },
            { "interpolate", value.interpolate }
        };
    }

    LightRotationKey light_rotation_key( const Json &value )
    {
        LightRotationKey output{};
        output.frame = value.value( "frame", 0.0f );

        output.reciprocal_frames_to_next = value.value( "reciprocal_frames_to_next", 0.0f );

        output.value = quat( value.at( "value" ) );

        output.raw_value = quat( value.value( "raw_value", value.at( "value" ) ) );

        output.interpolate = value.value( "interpolate", false );
        return output;
    }

    Json light_color_key( const LightColorKey &value )
    {
        return Json{
            { "frame", value.frame },
            { "reciprocal_frames_to_next", value.reciprocal_frames_to_next },
            { "primary", color( value.primary ) },
            { "secondary", color( value.secondary ) },
            { "interpolate", value.interpolate }
        };
    }

    LightColorKey light_color_key( const Json &value )
    {
        LightColorKey output{};
        output.frame = value.value( "frame", 0.0f );

        output.reciprocal_frames_to_next = value.value( "reciprocal_frames_to_next", 0.0f );

        output.primary = color( value.at( "primary" ) );

        output.secondary = color( value.at( "secondary" ) );

        output.interpolate = value.value( "interpolate", false );
        return output;
    }

    Json light_setup( const LightSetup &value )
    {
        return Json{
            { "version", value.version },
            { "num20", value.num20 },
            { "num30", value.num30 },
            { "prelude", value.prelude },
            { "block20", value.block20 },
            { "block30", value.block30 },
            { "light_type", value.light_type },
            { "enabled", value.enabled },
            { "primary_color", color( value.primary_color ) },
            { "secondary_color", color( value.secondary_color ) },
            { "raw_direction", vec3( value.raw_direction ) },
            { "direction", vec3( value.direction ) },
            { "rotation", quat( value.rotation ) },
            { "raw_rotation", quat( value.raw_rotation ) },
            { "rotation_keys", array( value.rotation_keys, light_rotation_key ) },
            { "color_keys", array( value.color_keys, light_color_key ) }
        };
    }

    LightSetup light_setup( const Json &value )
    {
        LightSetup output{};
        output.version = value.value( "version", 0u );

        output.num20 = value.value( "num20", 0u );

        output.num30 = value.value( "num30", 0u );

        output.prelude = value.value( "prelude", std::vector<std::uint8_t>{} );

        output.block20 = value.value( "block20", std::vector<std::uint8_t>{} );

        output.block30 = value.value( "block30", std::vector<std::uint8_t>{} );

        output.light_type = value.value( "light_type", 0u );

        output.enabled = value.value( "enabled", false );

        output.primary_color = color( value.at( "primary_color" ) );

        output.secondary_color = color( value.at( "secondary_color" ) );

        output.raw_direction = vec3( value.at( "raw_direction" ) );

        output.direction = vec3( value.at( "direction" ) );

        output.rotation = quat( value.at( "rotation" ) );

        output.raw_rotation = quat( value.at( "raw_rotation" ) );

        output.rotation_keys =
            vector<LightRotationKey>( value.value( "rotation_keys", Json::array() ), light_rotation_key );

        output.color_keys = vector<LightColorKey>( value.value( "color_keys", Json::array() ), light_color_key );
        return output;
    }

    Json model_anim_key( const ModelAnimKey &value )
    {
        return Json{
            { "frame", value.frame },
            { "reciprocal_frames_to_next", value.reciprocal_frames_to_next },
            { "animation_index", value.animation_index },
            { "use_current_player_time_origin", value.use_current_player_time_origin },
            { "hidden", value.hidden },
            { "state_driven", value.state_driven },
            { "loop", value.loop },
            { "model_property", value.model_property },
            { "transition", value.transition },
            { "drop_shadow", value.drop_shadow },
            { "unknown_28", value.unknown_28 },
            { "frame_times_160", value.frame_times_160 }
        };
    }

    ModelAnimKey model_anim_key( const Json &value )
    {
        ModelAnimKey output{};
        output.frame = value.value( "frame", 0.0f );

        output.reciprocal_frames_to_next = value.value( "reciprocal_frames_to_next", 0.0f );

        output.animation_index = value.value( "animation_index", 0u );

        output.use_current_player_time_origin = value.value( "use_current_player_time_origin", false );

        output.hidden = value.value( "hidden", false );

        output.state_driven = value.value( "state_driven", false );

        output.loop = value.value( "loop", false );

        output.model_property = value.value( "model_property", 0u );

        output.transition = value.value( "transition", false );

        output.drop_shadow = value.value( "drop_shadow", false );

        output.unknown_28 = value.value( "unknown_28", 0u );

        output.frame_times_160 = value.value( "frame_times_160", 0u );
        return output;
    }

    Json controllable( const Controllable &value )
    {
        return Json{
            { "min_angle", value.min_angle },
            { "max_angle", value.max_angle },
            { "descriptor", value.descriptor },
            { "event_flag", value.event_flag },
            { "animation_index", value.animation_index },
            { "hold_last_frame_count", value.hold_last_frame_count },
            { "forward_playback", value.forward_playback },
            { "loop_playback", value.loop_playback },
            { "use_current_global_time_origin", value.use_current_global_time_origin },
            { "random_connection", value.random_connection },
            { "connections", value.connections },
            { "default_event", value.default_event }
        };
    }

    Controllable controllable( const Json &value )
    {
        Controllable output{};
        output.min_angle = value.value( "min_angle", 0.0f );

        output.max_angle = value.value( "max_angle", 0.0f );

        output.descriptor = value.value( "descriptor", 0u );

        output.event_flag = value.value( "event_flag", 0u );

        output.animation_index = value.value( "animation_index", 0u );

        output.hold_last_frame_count = value.value( "hold_last_frame_count", 0u );

        output.forward_playback = value.value( "forward_playback", true );

        output.loop_playback = value.value( "loop_playback", true );

        output.use_current_global_time_origin = value.value( "use_current_global_time_origin", false );

        output.random_connection = value.value( "random_connection", false );

        output.connections = value.value( "connections", std::vector<std::uint32_t>{} );

        output.default_event = value.value( "default_event", -1 );
        return output;
    }

    Json effect_control( const ModelEntry::EffectControl &value )
    {
        return Json{
            { "reference_length", value.reference_length },
            { "source_offset", vec3( value.source_offset ) },
            { "target_offset", vec3( value.target_offset ) },
            { "source_model_name", value.source_model_name },
            { "target_model_name", value.target_model_name }
        };
    }

    ModelEntry::EffectControl effect_control( const Json &value )
    {
        ModelEntry::EffectControl output{};
        output.reference_length = value.value( "reference_length", 0.0f );

        output.source_offset = vec3( value.at( "source_offset" ) );

        output.target_offset = vec3( value.at( "target_offset" ) );

        output.source_model_name = value.value( "source_model_name", std::string{} );

        output.target_model_name = value.value( "target_model_name", std::string{} );
        return output;
    }

    Json model_entry( const ModelEntry &value )
    {
        return Json{
            { "xg_name", value.xg_name },
            { "is_clone", value.is_clone },
            { "clone_id", value.clone_id },
            { "model_type", value.model_type },
            { "xg_animation_length", value.xg_animation_length },
            { "xg_animation_speed", value.xg_animation_speed },
            { "xg_animation_framerate", value.xg_animation_framerate },
            { "position", vec3( value.position ) },
            { "rotation", quat( value.rotation ) },
            { "raw_rotation", quat( value.raw_rotation ) },
            { "scale", vec3( value.scale ) },
            { "visible", value.visible },
            { "position_keys", array( value.position_keys, vec3_key ) },
            { "rotation_keys", array( value.rotation_keys, quat_key ) },
            { "scale_keys", array( value.scale_keys, scale_key ) },
            { "anim_keys", array( value.anim_keys, model_anim_key ) },
            { "controllables", array( value.controllables, controllable ) },
            { "effect_control_words", value.effect_control_words },
            { "effect_control", effect_control( value.effect_control ) },
            { "has_effect_control", value.has_effect_control }
        };
    }

    ModelEntry model_entry( const Json &value )
    {
        ModelEntry output{};
        output.xg_name = value.value( "xg_name", std::string{} );

        output.is_clone = value.value( "is_clone", 0u );

        output.clone_id = value.value( "clone_id", 0u );

        output.model_type = value.value( "model_type", 0u );

        output.xg_animation_length = value.value( "xg_animation_length", 1.0f );

        output.xg_animation_speed = value.value( "xg_animation_speed", 1.0f );

        output.xg_animation_framerate = value.value( "xg_animation_framerate", 60.0f );

        output.position = vec3( value.at( "position" ) );

        output.rotation = quat( value.at( "rotation" ) );

        output.raw_rotation = quat( value.at( "raw_rotation" ) );

        output.scale = vec3( value.at( "scale" ) );

        output.visible = value.value( "visible", true );

        output.position_keys = vector<Vec3Key>( value.value( "position_keys", Json::array() ), vec3_key );

        output.rotation_keys = vector<QuatKey>( value.value( "rotation_keys", Json::array() ), quat_key );

        output.scale_keys = vector<ScaleKey>( value.value( "scale_keys", Json::array() ), scale_key );

        output.anim_keys = vector<ModelAnimKey>( value.value( "anim_keys", Json::array() ), model_anim_key );

        output.controllables = vector<Controllable>( value.value( "controllables", Json::array() ), controllable );

        output.effect_control_words = value.value( "effect_control_words", std::array<std::uint32_t, 17>{} );

        if( const auto it = value.find( "effect_control" ); it != value.end() )
        {
            output.effect_control = effect_control( *it );
        }

        output.has_effect_control = value.value( "has_effect_control", false );
        return output;
    }

    Json sprite_transform_key( const FixedSpriteTransformKey &value )
    {
        return Json{
            { "frame", value.frame },
            { "reciprocal_frames_to_next", value.reciprocal_frames_to_next },
            { "position", vec3( value.position ) },
            { "width", value.width },
            { "height", value.height },
            { "hidden", value.hidden },
            { "unknown_20", value.unknown_20 },
            { "interpolate", value.interpolate },
            { "external_visibility_control", value.external_visibility_control },
            { "frame_times_160", value.frame_times_160 }
        };
    }

    FixedSpriteTransformKey sprite_transform_key( const Json &value )
    {
        FixedSpriteTransformKey output{};
        output.frame = value.value( "frame", 0.0f );

        output.reciprocal_frames_to_next = value.value( "reciprocal_frames_to_next", 0.0f );

        output.position = vec3( value.at( "position" ) );

        output.width = value.value( "width", 0.0f );

        output.height = value.value( "height", 0.0f );

        output.hidden = value.value( "hidden", false );

        output.unknown_20 = value.value( "unknown_20", 0u );

        output.interpolate = value.value( "interpolate", false );

        output.external_visibility_control = value.value( "external_visibility_control", false );

        output.frame_times_160 = value.value( "frame_times_160", 0u );
        return output;
    }

    Json sprite_frame_key( const FixedSpriteFrameKey &value )
    {
        return Json{
            { "frame", value.frame },
            { "reciprocal_frames_to_next", value.reciprocal_frames_to_next },
            { "source_x", value.source_x },
            { "source_y", value.source_y },
            { "source_width", value.source_width },
            { "source_height", value.source_height },
            { "interpolate", value.interpolate },
            { "frame_times_160", value.frame_times_160 }
        };
    }

    FixedSpriteFrameKey sprite_frame_key( const Json &value )
    {
        FixedSpriteFrameKey output{};
        output.frame = value.value( "frame", 0.0f );

        output.reciprocal_frames_to_next = value.value( "reciprocal_frames_to_next", 0.0f );

        output.source_x = value.value( "source_x", 0.0f );

        output.source_y = value.value( "source_y", 0.0f );

        output.source_width = value.value( "source_width", 0.0f );

        output.source_height = value.value( "source_height", 0.0f );

        output.interpolate = value.value( "interpolate", false );

        output.frame_times_160 = value.value( "frame_times_160", 0u );
        return output;
    }

    Json sprite_descriptor( const FixedSpriteDescriptor &value )
    {
        return Json{
            { "texture_index", value.texture_index },
            { "source_x", value.source_x },
            { "source_y", value.source_y },
            { "source_width", value.source_width },
            { "source_height", value.source_height },
            { "position", vec3( value.position ) },
            { "width", value.width },
            { "height", value.height },
            { "render_words", value.render_words },
            { "owner_selector", value.owner_selector }
        };
    }

    FixedSpriteDescriptor sprite_descriptor( const Json &value )
    {
        FixedSpriteDescriptor output{};
        output.texture_index = value.value( "texture_index", 0u );

        output.source_x = value.value( "source_x", 0.0f );

        output.source_y = value.value( "source_y", 0.0f );

        output.source_width = value.value( "source_width", 0.0f );

        output.source_height = value.value( "source_height", 0.0f );

        output.position = vec3( value.at( "position" ) );

        output.width = value.value( "width", 0.0f );

        output.height = value.value( "height", 0.0f );

        output.render_words = value.value( "render_words", std::array<std::uint32_t, 10>{} );

        output.owner_selector = value.value( "owner_selector", 0u );
        return output;
    }

    Json fixed_sprite( const FixedSprite &value )
    {
        return Json{
            { "version", value.version },
            { "descriptor", sprite_descriptor( value.descriptor ) },
            { "base_words", value.base_words },
            { "transform_keys", array( value.transform_keys, sprite_transform_key ) },
            { "color_keys", array( value.color_keys, color_key ) },
            { "frame_keys", array( value.frame_keys, sprite_frame_key ) }
        };
    }

    FixedSprite fixed_sprite( const Json &value )
    {
        FixedSprite output{};
        output.version = value.value( "version", 0u );

        output.descriptor = sprite_descriptor( value.at( "descriptor" ) );

        output.base_words = value.value( "base_words", std::array<std::uint32_t, 16>{} );

        output.transform_keys =
            vector<FixedSpriteTransformKey>( value.value( "transform_keys", Json::array() ), sprite_transform_key );

        output.color_keys = vector<ColorKey>( value.value( "color_keys", Json::array() ), color_key );

        output.frame_keys = vector<FixedSpriteFrameKey>( value.value( "frame_keys", Json::array() ), sprite_frame_key );
        return output;
    }

    Json texture_cutout( const TextureCutout &value )
    {
        return Json::array( { value.x0, value.y0, value.x1, value.y1 } );
    }

    TextureCutout texture_cutout( const Json &value )
    {
        return TextureCutout{
            value.at( 0 ).get<float>(),
            value.at( 1 ).get<float>(),
            value.at( 2 ).get<float>(),
            value.at( 3 ).get<float>()
        };
    }

    Json texture_anim_frame( const TextureAnimFrame &value )
    {
        return Json{
            { "frame", value.frame },
            { "reciprocal_frames_to_next", value.reciprocal_frames_to_next },
            { "cutout_index", value.cutout_index },
            { "interpolate", value.interpolate }
        };
    }

    TextureAnimFrame texture_anim_frame( const Json &value )
    {
        TextureAnimFrame output{};
        output.frame = value.value( "frame", 0.0f );

        output.reciprocal_frames_to_next = value.value( "reciprocal_frames_to_next", 0.0f );

        output.cutout_index = value.value( "cutout_index", 0u );

        output.interpolate = value.value( "interpolate", 0u );
        return output;
    }

    Json texture_animation( const TextureAnimation &value )
    {
        return Json{
            { "version", value.version },
            { "paste_x", value.paste_x },
            { "paste_y", value.paste_y },
            { "texture_name", value.texture_name },
            { "texture_words", value.texture_words },
            { "cutouts", array( value.cutouts, texture_cutout ) },
            { "frames", array( value.frames, texture_anim_frame ) }
        };
    }

    TextureAnimation texture_animation( const Json &value )
    {
        TextureAnimation output{};
        output.version = value.value( "version", 0u );

        output.paste_x = value.value( "paste_x", 0u );

        output.paste_y = value.value( "paste_y", 0u );

        output.texture_name = value.value( "texture_name", std::string{} );

        output.texture_words = value.value( "texture_words", std::array<std::uint32_t, 2>{} );

        output.cutouts = vector<TextureCutout>( value.value( "cutouts", Json::array() ), texture_cutout );

        output.frames = vector<TextureAnimFrame>( value.value( "frames", Json::array() ), texture_anim_frame );
        return output;
    }

    Json projection_key( const ProjectionKey &value )
    {
        return Json{
            { "frame", value.frame },
            { "reciprocal_frames_to_next", value.reciprocal_frames_to_next },
            { "fov_degrees", value.fov_degrees },
            { "aspect", value.aspect },
            { "z_near", value.z_near },
            { "z_far", value.z_far },
            { "interpolate", value.interpolate }
        };
    }

    ProjectionKey projection_key( const Json &value )
    {
        ProjectionKey output{};
        output.frame = value.value( "frame", 0.0f );

        output.reciprocal_frames_to_next = value.value( "reciprocal_frames_to_next", 0.0f );

        output.fov_degrees = value.value( "fov_degrees", 60.0f );

        output.aspect = value.value( "aspect", 4.0f / 3.0f );

        output.z_near = value.value( "z_near", 1.0f );

        output.z_far = value.value( "z_far", 47000.0f );

        output.interpolate = value.value( "interpolate", false );
        return output;
    }

    Json camera( const Camera &value )
    {
        return Json{
            { "position", vec3( value.position ) },
            { "rotation", quat( value.rotation ) },
            { "raw_rotation", quat( value.raw_rotation ) },
            { "fov_degrees", value.fov_degrees },
            { "aspect", value.aspect },
            { "z_near", value.z_near },
            { "z_far", value.z_far },
            { "fog_color", color( value.fog_color ) },
            { "base_ambient_color", color( value.base_ambient_color ) },
            { "base_diffuse_color", color( value.base_diffuse_color ) },
            { "ambient_color", color( value.ambient_color ) },
            { "position_keys", array( value.position_keys, vec3_key ) },
            { "rotation_keys", array( value.rotation_keys, quat_key ) },
            { "projection_keys", array( value.projection_keys, projection_key ) },
            { "ambient_keys", array( value.ambient_keys, color_key ) },
            { "light_setups", array( value.light_setups, light_setup ) }
        };
    }

    Camera camera( const Json &value )
    {
        Camera output{};
        output.position = vec3( value.at( "position" ) );

        output.rotation = quat( value.at( "rotation" ) );

        output.raw_rotation = quat( value.at( "raw_rotation" ) );

        output.fov_degrees = value.value( "fov_degrees", 60.0f );

        output.aspect = value.value( "aspect", 4.0f / 3.0f );

        output.z_near = value.value( "z_near", 1.0f );

        output.z_far = value.value( "z_far", 47000.0f );

        output.fog_color = color( value.at( "fog_color" ) );

        output.base_ambient_color = color( value.at( "base_ambient_color" ) );

        output.base_diffuse_color = color( value.at( "base_diffuse_color" ) );

        output.ambient_color = color( value.at( "ambient_color" ) );

        output.position_keys = vector<Vec3Key>( value.value( "position_keys", Json::array() ), vec3_key );

        output.rotation_keys = vector<QuatKey>( value.value( "rotation_keys", Json::array() ), quat_key );

        output.projection_keys =
            vector<ProjectionKey>( value.value( "projection_keys", Json::array() ), projection_key );

        output.ambient_keys = vector<ColorKey>( value.value( "ambient_keys", Json::array() ), color_key );

        output.light_setups = vector<LightSetup>( value.value( "light_setups", Json::array() ), light_setup );
        return output;
    }
}

namespace piko::filetypes::ssq
{
    Scene load_json( const std::filesystem::path &path )
    {
        std::ifstream file( path, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not open sequence JSON: " + path.string() );
        }

        nlohmann::json root;
        file >> root;

        if( root.value( "format", std::string{} ) != "piko-stage-sequence" )
        {
            throw std::runtime_error( "Unsupported sequence JSON format: " + path.string() );
        }

        const nlohmann::json &scene_json = root.at( "scene" );
        Scene scene{};
        scene.version = scene_json.value( "version", 0u );

        scene.imx_entries = scene_json.value( "imx_entries", std::vector<std::string>{} );

        scene.models =
            json::vector<ModelEntry>( scene_json.value( "models", nlohmann::json::array() ), json::model_entry );

        scene.fixed_sprites = json::vector<FixedSprite>( scene_json.value( "fixed_sprites", nlohmann::json::array() ),
            json::fixed_sprite );

        scene.texture_animations =
            json::vector<TextureAnimation>( scene_json.value( "texture_animations", nlohmann::json::array() ),
                json::texture_animation );

        scene.camera = json::camera( scene_json.at( "camera" ) );
        return scene;
    }

    void save_json( const Scene &scene, const std::filesystem::path &path )
    {
        if( path.has_parent_path() )
        {
            std::filesystem::create_directories( path.parent_path() );
        }

        nlohmann::json root{
            { "format", "piko-stage-sequence" },
            { "version", 1 },
            { "scene",
                nlohmann::json{
                    { "version", scene.version },
                    { "imx_entries", scene.imx_entries },
                    { "models", json::array( scene.models, json::model_entry ) },
                    { "fixed_sprites", json::array( scene.fixed_sprites, json::fixed_sprite ) },
                    { "texture_animations", json::array( scene.texture_animations, json::texture_animation ) },
                    { "camera", json::camera( scene.camera ) }
                } }
        };

        json::sanitize_numbers( root );

        std::ofstream file( path, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not create sequence JSON: " + path.string() );
        }

        file << root.dump( 2 ) << '\n';

        if( !file )
        {
            throw std::runtime_error( "Could not write sequence JSON: " + path.string() );
        }
    }
}
