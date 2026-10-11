#pragma once

#include "FileTypes/SSQ/SSQ.hpp"
#include "FileTypes/XG/XG.hpp"
#include "Managers/TextureManager.hpp"
#include "Rendering/RenderTypes.hpp"
#include "Runtime/Model/GltfModel.hpp"

#include <glad/gl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace piko::modules::stage3d
{
    struct ActionAnimationInput
    {
        std::uint32_t event_flags = 0;
        std::uint32_t descriptor = 0;
        float aim_angle_radians = 0.0f;
        bool has_aim_angle = false;
        bool action_down = false;
        bool guard_down = false;
        bool guard_pressed = false;
        std::uint64_t successful_strum_result_serial = 0;
        bool always_retrigger_player_strum = false;
        std::uint32_t effect_owner_model_type = 0;
        std::uint64_t effect_result_serial = 0;
        std::uint32_t effect_hold_owner_model_type = 0;
        std::uint32_t effect_hold_owner_model_mask = 0;
        std::uint32_t fixed_sprite_active_model_mask = 0;
    };

    struct GpuVertex
    {
        float px = 0.0f;
        float py = 0.0f;
        float pz = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
        float nx = 0.0f;
        float ny = 1.0f;
        float nz = 0.0f;
        float bi0 = 0.0f;
        float bi1 = 0.0f;
        float bi2 = 0.0f;
        float bi3 = 0.0f;
        float bw0 = 0.0f;
        float bw1 = 0.0f;
        float bw2 = 0.0f;
        float bw3 = 0.0f;
    };

    enum class MaterialDebugMode : std::int32_t
    {
        Normal,
        TextureOnly,
        ColorOnly,
        NoEnvMap,
        OpaqueOnly,
        AlphaOnly,
        AlphaMask,
        MultipassBase,
        MultipassAll
    };

    enum class TextureDebugView : std::int32_t
    {
        Normal,
        OpaqueRgb,
        AlphaMask
    };

    enum class LightingBackend : std::int32_t
    {
        Fragment,
        VertexPs2,
        Cel
    };

    enum class GmltDebugMode : std::int32_t
    {
        All,
        Only0,
        Only1,
        None
    };

    enum class GmltDirectionMode : std::int32_t
    {
        LocalNegativeZ,
        LocalPositiveZ,
        LocalNegativeY,
        LocalPositiveY,
        LocalNegativeX,
        LocalPositiveX,
        InverseNegativeZ,
        InversePositiveZ,
        InverseNegativeY,
        InversePositiveY,
        InverseNegativeX,
        InversePositiveX,
        RawNegativeZ,
        RawPositiveZ,
        RawNegativeY,
        RawPositiveY,
        RawNegativeX,
        RawPositiveX,
        RawInverseNegativeZ,
        RawInversePositiveZ,
        RawInverseNegativeY,
        RawInversePositiveY,
        RawInverseNegativeX,
        RawInversePositiveX
    };

    enum class DepthMode : std::int32_t
    {
        Standard,
        ReverseZ
    };

    enum class CameraRotationMode : std::int32_t
    {
        ZReflectOpenGlForward,
        ZReflectInverseOpenGlForward,
        XReflectOpenGlForward,
        XReflectInverseOpenGlForward,
        YReflectOpenGlForward,
        YReflectInverseOpenGlForward,
        RawOpenGlForward,
        RawInverseOpenGlForward
    };

    enum class AnimationMode : std::int32_t
    {
        SsqOnly,
        RigidXgMatrices,
        CpuSkinning,
        GpuSkinning
    };

    enum class XgRotationMode : std::int32_t
    {
        ZReflect,
        ZReflectInverse,
        XReflect,
        XReflectInverse,
        YReflect,
        YReflectInverse,
        Raw,
        RawInverse
    };

    enum class ModelRotationMode : std::int32_t
    {
        ZReflect,
        ZReflectInverse,
        XReflect,
        XReflectInverse,
        YReflect,
        YReflectInverse,
        Raw,
        RawInverse,
        Ignore,
        RawXzy,
        RawXzyInverse,
        RawXzyNegY,
        RawXzyNegYInverse,
        RawXzyNegZ,
        RawXzyNegZInverse,
        RawXzyNegYZ,
        RawXzyNegYZInverse
    };

    enum class SkinningFormulaMode : std::int32_t
    {
        PoseTimesRest,
        RestTimesPose,
        PoseTimesInverseRest,
        InverseRestTimesPose,
        PoseTimesRestTranspose,
        RestTransposeTimesPose
    };

    enum class ModelBasisMode : std::int32_t
    {
        Identity,
        RotateX90,
        RotateXMinus90,
        RotateX180,
        RotateY90,
        RotateYMinus90,
        RotateY180,
        RotateZ90,
        RotateZMinus90,
        RotateZ180,
        MirrorX,
        MirrorY,
        MirrorZ,
        MirrorXY,
        MirrorXZ,
        MirrorYZ
    };

    struct DrawMesh
    {
        std::vector<GpuVertex> vertices;
        std::vector<GpuVertex> gpu_skin_vertices;
        std::vector<std::uint32_t> triangle_indices;
        std::size_t vertex_count = 0;
        std::size_t index_count = 0;
        GLuint static_vao = 0;
        GLuint static_vbo = 0;
        GLuint static_ebo = 0;
        bool indexed_vertices = false;
        bool static_gpu_ready = false;
        std::vector<std::string> matrix_chain;
        filetypes::xg::Matrix static_local_matrix{};
        std::size_t debug_id = 0;
        std::string model_name;
        std::string mesh_name;
        std::string geometry_name;
        std::string material_name;
        std::string texture_name;
        managers::TextureHandle texture{};
        std::vector<std::string> input_geometry_nodes;
        bool has_material = false;
        bool texture_found = false;
        bool uses_alpha = false;
        bool wrap_texture = true;
        bool is_multipass = false;
        std::uint32_t multipass_layer_index = 0;
        std::uint32_t multipass_layer_count = 1;
        bool multipass_default_visible = true;
        std::uint32_t blend_type = 0;
        std::uint32_t shading_type = 0;
        std::uint32_t flags = 0;
        std::uint32_t texture_env = 0;
        std::uint32_t u_tile = 0;
        std::uint32_t v_tile = 0;
        std::uint32_t cull_func = 0;
        rendering::Color diffuse{ 1.0f, 1.0f, 1.0f, 1.0f };
        bool gpu_skin_ready = false;
        bool has_vertex_interpolator = false;
        bool has_normal_interpolator = false;
        bool has_texcoord_interpolator = false;
        bool has_shape_interpolator = false;
        bool has_geometry_interpolators = false;
        std::int32_t gltf_primitive_index = -1;
        std::int32_t gltf_node_index = -1;
        std::int32_t gltf_skin_index = -1;
    };

    struct DrawModel
    {
        std::string name;
        filetypes::xg::Model source_model{};
        std::shared_ptr<runtime::model::GltfModelAsset> gltf_asset;
        std::int32_t gltf_skin_index = -1;
        std::vector<runtime::model::AnimationTiming> animations;
        std::vector<std::string> gpu_skin_bones;
        std::vector<DrawMesh> meshes;
        std::size_t vertex_count = 0;
        bool has_dynamic_matrices = false;
        bool has_envelopes = false;
        bool has_geometry_interpolators = false;
    };

    struct SceneInstance
    {
        const DrawModel *model = nullptr;
        filetypes::xg::Matrix transform{};
        std::size_t model_setup_index = 0;
        std::string name;
        std::uint32_t model_type = 0;
    };

    struct ActionAnimationState
    {
        bool initialized = false;
        bool previous_state_driven = true;
        std::size_t current_controllable = 0;
        std::uint32_t selected_animation_index = 0xFFFFFFFFu;
        std::size_t selected_scheduled_key = static_cast<std::size_t>( -1 );
        float animation_origin_frame = 0.0f;
        float last_global_frame = -1.0f;
        float last_scene_frame = -1.0f;
        float evaluated_frame = 0.0f;
        float reciprocal_interval = 1.0f;
        float duration = 0.0f;
        float stage_duration = 0.0f;
        float start_keyframe = 0.0f;
        float base_speed = 0.0f;
        std::uint32_t speed_mode = 0;
        bool portable_stage_driven_model = false;
        bool portable_song_clock = false;
        bool active = false;
        bool forward = true;
        bool loop = true;
        bool start_guard = false;
        bool transition_pending = false;
        bool transition_active = false;
        float transition_duration = 0.0f;
        float transition_start_frame = 0.0f;
        std::uint32_t transition_from_animation_index = 0;
        std::uint32_t transition_to_animation_index = 0;
        float transition_from_frame = 0.0f;
        float transition_to_frame = 0.0f;
        float transition_pose_weight = 0.0f;
        float transition_source_origin_frame = 0.0f;
        float transition_source_reciprocal_interval = 1.0f;
        float transition_source_duration = 0.0f;
        float transition_source_start_keyframe = 0.0f;
        float transition_source_base_speed = 0.0f;
        std::uint32_t transition_source_speed_mode = 0;
        bool transition_source_portable_song_clock = false;
        bool transition_source_forward = true;
        bool transition_source_loop = true;
        std::uint32_t last_event_flags = 0;
        std::uint32_t last_descriptor = 0;
        std::uint64_t last_seen_strum_result_serial = 0;
        std::uint64_t last_effect_activation_serial = 0;
        std::vector<std::uint32_t> latched_event_flags;
        std::vector<std::uint32_t> hold_counters;
        bool used_default = false;
        bool used_connection = false;
        bool event_matched = false;
        bool descriptor_matched = false;
        bool angle_matched = false;
        std::size_t selected_connection_index = static_cast<std::size_t>( -1 );
    };

    struct ResolvedAnimation
    {
        std::uint32_t animation_index = 0;
        float evaluated_frame = 0.0f;
        bool pose_blend_active = false;
        std::uint32_t pose_blend_source_animation_index = 0;
        std::uint32_t pose_blend_target_animation_index = 0;
        float pose_blend_source_frame = 0.0f;
        float pose_blend_target_frame = 0.0f;
        float pose_blend_weight = 0.0f;
    };

    struct EffectPlaybackState
    {
        bool active = false;
        float start_stage_frame = 0.0f;
        float end_stage_frame = 0.0f;
        float state_driver_start_stage_frame = -1.0f;
        float last_stage_frame = -1.0f;
        std::uint64_t last_result_serial = 0;
        std::uint64_t activation_serial = 0;
    };

    struct RuntimeTextureAnimation
    {
        std::size_t scene_animation_index = 0;
        managers::TextureHandle texture{};
        std::uint32_t texture_id = 0;
        std::int32_t width = 0;
        std::int32_t height = 0;
        float coordinate_scale_x = 1.0f;
        float coordinate_scale_y = 1.0f;
        std::vector<std::uint8_t> source_rgba;
        std::uint32_t last_cutout_index = 0xFFFFFFFFu;
    };

    struct RuntimeFixedSprite
    {
        std::size_t scene_sprite_index = 0;
        managers::TextureHandle texture{};
        std::int32_t texture_width = 0;
        std::int32_t texture_height = 0;
    };

    struct SampledFixedSprite
    {
        bool visible = true;
        filetypes::xg::Vec3 position{};
        float width = 0.0f;
        float height = 0.0f;
        rendering::Color color{};
        float source_x = 0.0f;
        float source_y = 0.0f;
        float source_width = 0.0f;
        float source_height = 0.0f;
    };

    struct CameraPresetupRange
    {
        float start_frame = 0.0f;
        float end_frame = 0.0f;
        float duration_frames = 1.0f;
        bool loop = false;
        std::string name;
    };

    struct SceneLight
    {
        std::int32_t source_index = -1;
        rendering::Color primary{ 0.0f, 0.0f, 0.0f, 1.0f };
        rendering::Color secondary{ 0.0f, 0.0f, 0.0f, 1.0f };
        filetypes::xg::Vec3 direction{ 0.0f, 1.0f, 0.0f };
    };

    struct SceneLighting
    {
        rendering::Color fog{ 0.0f, 0.0f, 0.0f, 1.0f };
        rendering::Color ambient{ 0.45f, 0.45f, 0.45f, 1.0f };
        rendering::Color diffuse{ 0.70f, 0.70f, 0.70f, 1.0f };
        std::array<SceneLight, 8> gmlt_lights{};
        std::size_t gmlt_light_count = 0;
    };

    struct TextureSamplerState
    {
        bool wrap_texture = false;
        std::int32_t filter_mode = -1;
    };
}
