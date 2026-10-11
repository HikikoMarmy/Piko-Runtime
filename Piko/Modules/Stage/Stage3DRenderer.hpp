#pragma once

#include "Stage3DTypes.hpp"

#include "GameData/StageCatalog.hpp"
#include "Rendering/Shader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace piko
{
    class App;
}

namespace piko::rendering
{
    class Renderer;
}

namespace piko::modules
{
    class Stage3DRenderer
    {
    public:
        Stage3DRenderer() = default;
        ~Stage3DRenderer();

        Stage3DRenderer( const Stage3DRenderer & ) = delete;
        Stage3DRenderer &operator=( const Stage3DRenderer & ) = delete;

        using ActionAnimationInput = stage3d::ActionAnimationInput;

        bool load( App &app, const gamedata::PlayableStageEntry &stage, std::string *status = nullptr );
        void unload( App &app );
        void update( double delta_seconds );
        void set_stage_frame( float frame );
        void set_stage_timing( float global_frame, std::size_t section_index, float section_frame, float stage_tempo );
        void set_portable_song_animation_frame( float frame );
        void set_stage_result_timing( bool active, bool won, float elapsed_frame );
        void set_action_animation_input( const ActionAnimationInput &input );
        void set_actor_action_animation_input( std::uint32_t model_type, const ActionAnimationInput &input );
        bool render( App &app,
            rendering::Renderer &renderer,
            std::int32_t target_width = 0,
            std::int32_t target_height = 0 );

        void nudge_debug_camera_local( float x, float y, float z );
        void reset_debug_camera();
        void begin_debug_camera_pullback( float seconds = 3.0f );
        void nudge_camera_frame_offset( float frames );
        void reset_camera_frame_offset();
        void cycle_camera_rotation_mode();
        void nudge_camera_time_scale( std::int32_t direction );
        void cycle_animation_mode();
        void cycle_model_rotation_mode();
        void cycle_xg_rotation_mode();
        void cycle_skinning_formula_mode();
        void cycle_model_basis_mode();
        void toggle_special_model_types();
        void nudge_scene_lighting_strength( float delta );
        void nudge_scene_lighting_floor( float delta );
        void nudge_gmlt_light_strength( float delta );
        void cycle_gmlt_debug_mode();
        void cycle_gmlt_direction_mode();
        void nudge_ps2_light_wrap( float delta );
        void toggle_lighting_backend();
        void toggle_depth_mode();
        void nudge_depth_near_plane( float delta );
        void reset_scene_lighting();
        void toggle_stage_sky();

        [[nodiscard]] std::int32_t lighting_backend_index() const;
        void set_lighting_backend_index( std::int32_t index );
        [[nodiscard]] const char *lighting_backend_display_name( std::int32_t index ) const;
        [[nodiscard]] std::int32_t lighting_backend_display_count() const;

        [[nodiscard]] float scene_lighting_strength() const;
        void set_scene_lighting_strength( float value );
        [[nodiscard]] float scene_lighting_floor() const;
        void set_scene_lighting_floor( float value );
        [[nodiscard]] float gmlt_light_strength() const;
        void set_gmlt_light_strength( float value );
        [[nodiscard]] float ps2_light_wrap() const;
        void set_ps2_light_wrap( float value );
        [[nodiscard]] float minimum_projection_near() const;
        void set_minimum_projection_near( float value );
        [[nodiscard]] bool stage_sky_enabled() const;
        void set_stage_sky_enabled( bool enabled );
        [[nodiscard]] float cel_shadow_threshold() const;
        void set_cel_shadow_threshold( float value );
        [[nodiscard]] float cel_shadow_strength() const;
        void set_cel_shadow_strength( float value );
        [[nodiscard]] float cel_edge_strength() const;
        void set_cel_edge_strength( float value );

        [[nodiscard]] bool loaded() const;
        [[nodiscard]] bool debug_camera_animating() const;
        [[nodiscard]] const std::string &status_line() const;
        [[nodiscard]] const std::string &debug_camera_line() const;
        [[nodiscard]] std::vector<std::string> camera_debug_lines() const;

        void cycle_material_debug_mode();
        void cycle_material_debug_sample();
        void toggle_material_debug_focus();
        [[nodiscard]] bool material_debug_overlay_visible() const;
        [[nodiscard]] const std::string &material_debug_status_line() const;
        [[nodiscard]] std::vector<std::string> material_debug_lines() const;

        void toggle_texture_debug_overlay();
        void cycle_texture_debug_sample( std::int32_t direction );
        void cycle_texture_debug_view();
        [[nodiscard]] bool texture_debug_overlay_visible() const;
        [[nodiscard]] managers::TextureHandle texture_debug_handle() const;
        [[nodiscard]] std::string texture_debug_name() const;
        [[nodiscard]] std::size_t texture_debug_index() const;
        [[nodiscard]] std::size_t texture_debug_count() const;
        [[nodiscard]] const char *texture_debug_view_name() const;
        [[nodiscard]] std::int32_t texture_debug_view_index() const;

        void reset_action_animation_state();
        void set_action_event_flag_override( bool enabled, std::uint32_t flags );
        [[nodiscard]] bool action_event_flag_override_enabled() const;
        [[nodiscard]] std::uint32_t action_event_flag_override() const;
        [[nodiscard]] std::uint32_t effective_action_event_flags() const;
        [[nodiscard]] const char *action_event_flag_name( std::int32_t bit_index ) const;
        [[nodiscard]] std::vector<std::string> action_debug_lines() const;

    private:
        using GpuVertex = stage3d::GpuVertex;

        using MaterialDebugMode = stage3d::MaterialDebugMode;

        using TextureDebugView = stage3d::TextureDebugView;

        using LightingBackend = stage3d::LightingBackend;

        using GmltDebugMode = stage3d::GmltDebugMode;

        using GmltDirectionMode = stage3d::GmltDirectionMode;

        using DepthMode = stage3d::DepthMode;

        using CameraRotationMode = stage3d::CameraRotationMode;

        using AnimationMode = stage3d::AnimationMode;

        using XgRotationMode = stage3d::XgRotationMode;

        using ModelRotationMode = stage3d::ModelRotationMode;

        using SkinningFormulaMode = stage3d::SkinningFormulaMode;

        using ModelBasisMode = stage3d::ModelBasisMode;

        using DrawMesh = stage3d::DrawMesh;

        using DrawModel = stage3d::DrawModel;

        using SceneInstance = stage3d::SceneInstance;

        using ActionAnimationState = stage3d::ActionAnimationState;

        using ResolvedAnimation = stage3d::ResolvedAnimation;

        using EffectPlaybackState = stage3d::EffectPlaybackState;

        using RuntimeTextureAnimation = stage3d::RuntimeTextureAnimation;

        using RuntimeFixedSprite = stage3d::RuntimeFixedSprite;

        using SampledFixedSprite = stage3d::SampledFixedSprite;

        using CameraPresetupRange = stage3d::CameraPresetupRange;

        using SceneLight = stage3d::SceneLight;

        using SceneLighting = stage3d::SceneLighting;

        using TextureSamplerState = stage3d::TextureSamplerState;

        bool initialize_gl( App &app, std::string *status );
        void shutdown_gl();
        void upload_static_mesh_buffers( DrawMesh &mesh );
        void configure_texture_sampling( GLuint texture_id, bool wrap_texture, std::int32_t filter_mode );
        void release_model_buffers();
        DrawModel compile_model( const filetypes::xg::Model &model,
            const std::string &name,
            const std::vector<runtime::model::AnimationTiming> &animations );

        DrawModel compile_gltf_model( App &app,
            std::shared_ptr<runtime::model::GltfModelAsset> asset,
            const std::string &name,
            const std::vector<runtime::model::AnimationTiming> &animations );

        void append_dag_node( const filetypes::xg::Model &model,
            const std::string &node_name,
            const filetypes::xg::Matrix &parent_matrix,
            const std::vector<std::string> &matrix_chain,
            DrawModel &out_model );

        void append_mesh( const filetypes::xg::Model &model,
            const filetypes::xg::DagMesh &mesh,
            const filetypes::xg::Matrix &matrix,
            const std::vector<std::string> &matrix_chain,
            DrawModel &out_model );

        void append_mesh_layer( const filetypes::xg::Model &model,
            const filetypes::xg::Geometry &geometry,
            const filetypes::xg::DagMesh &mesh,
            const filetypes::xg::Material *material,
            const filetypes::xg::Matrix &matrix,
            const std::vector<std::string> &matrix_chain,
            DrawModel &out_model,
            bool is_multipass,
            std::uint32_t multipass_layer_index,
            std::uint32_t multipass_layer_count );

        [[nodiscard]] filetypes::xg::Matrix make_model_transform( const filetypes::ssq::ModelEntry &entry ) const;
        [[nodiscard]] bool make_linked_effect_transform( const filetypes::ssq::ModelEntry &entry,
            float frame,
            filetypes::xg::Matrix &transform ) const;

        [[nodiscard]] filetypes::ssq::ModelEntry sample_model_entry( const filetypes::ssq::ModelEntry &entry,
            float frame ) const;

        [[nodiscard]] const filetypes::ssq::ModelAnimKey *active_model_anim_key(
            const filetypes::ssq::ModelEntry &entry,
            float frame ) const;

        [[nodiscard]] ResolvedAnimation resolve_animation_for_instance( const DrawModel &model,
            const filetypes::ssq::ModelEntry &entry,
            std::size_t model_setup_index,
            float frame );

        [[nodiscard]] float xg_animation_time( const DrawModel &model,
            const ResolvedAnimation &resolved,
            float frame ) const;

        [[nodiscard]] bool controllable_matches_action( const filetypes::ssq::Controllable &controllable,
            bool *event_match = nullptr,
            bool *descriptor_match = nullptr,
            bool *angle_match = nullptr ) const;

        [[nodiscard]] bool controllable_matches_action_with( const filetypes::ssq::Controllable &controllable,
            std::uint32_t flags,
            std::uint32_t descriptor,
            bool has_aim_angle,
            float aim_angle_radians,
            bool *event_match = nullptr,
            bool *descriptor_match = nullptr,
            bool *angle_match = nullptr ) const;

        [[nodiscard]] bool angle_in_controllable_range( float angle, float min_angle, float max_angle ) const;
        [[nodiscard]] bool controllable_is_looping_directional_aim(
            const filetypes::ssq::Controllable &controllable ) const;

        [[nodiscard]] bool controllables_share_directional_playback( const filetypes::ssq::Controllable &current,
            const filetypes::ssq::Controllable &next ) const;

        [[nodiscard]] const ActionAnimationInput &action_input_for_model_type( std::uint32_t model_type ) const;
        void update_player_pose_aim( const ActionAnimationInput &input );
        void select_xgm_animation( const DrawModel &model,
            ActionAnimationState &state,
            std::uint32_t animation_index,
            bool use_current_time_origin,
            bool loop,
            bool transition,
            bool start_guard,
            float current_time,
            float transition_duration,
            float model_animation_speed,
            bool force_restart = false );

        [[nodiscard]] float evaluate_xgm_animation( ActionAnimationState &state, float global_frame ) const;
        void update_controllable_player( const DrawModel &model,
            const filetypes::ssq::ModelEntry &entry,
            ActionAnimationState &state,
            float global_frame );

        void apply_directional_pose_blend( const DrawModel &model,
            const filetypes::ssq::ModelEntry &entry,
            const ActionAnimationState &state,
            ResolvedAnimation &resolved ) const;

        void update_effect_playback( float frame );
        [[nodiscard]] float effect_duration_stage_frames( const DrawModel &model,
            const filetypes::ssq::ModelEntry &entry,
            std::uint32_t animation_index ) const;

        void register_gltf_texture_animation( App &app,
            const std::filesystem::path &texture_path,
            managers::TextureHandle texture );

        [[nodiscard]] static bool load_png_rgba( const std::filesystem::path &path,
            std::int32_t &width,
            std::int32_t &height,
            std::vector<std::uint8_t> &pixels );

        [[nodiscard]] static float texture_animation_coordinate_scale(
            const filetypes::ssq::TextureAnimation &animation,
            std::int32_t texture_dimension,
            bool horizontal );

        void update_texture_animations( float frame );
        [[nodiscard]] SampledFixedSprite sample_fixed_sprite( const filetypes::ssq::FixedSprite &sprite,
            float frame,
            bool external_visibility ) const;

        [[nodiscard]] bool fixed_sprite_owner_active( std::uint32_t owner_selector ) const;
        void draw_fixed_sprites( App &app,
            rendering::Renderer &renderer,
            const filetypes::xg::Matrix &projection,
            const filetypes::xg::Matrix &view,
            float frame );

        [[nodiscard]] filetypes::xg::Quat convert_rotation_for_mode( filetypes::xg::Quat rotation,
            XgRotationMode mode ) const;

        [[nodiscard]] filetypes::xg::Quat convert_model_rotation( filetypes::xg::Quat rotation ) const;
        [[nodiscard]] std::string model_rotation_mode_name( ModelRotationMode mode ) const;
        [[nodiscard]] filetypes::xg::Quat convert_xg_animation_rotation( filetypes::xg::Quat rotation ) const;
        [[nodiscard]] filetypes::xg::Matrix make_xg_animation_matrix( const filetypes::xg::Vec3 &position,
            const filetypes::xg::Quat &rotation,
            const filetypes::xg::Vec3 &scale ) const;

        [[nodiscard]] filetypes::xg::Matrix make_skin_matrix( const filetypes::xg::Matrix &pose_matrix,
            const filetypes::xg::Matrix &rest_matrix ) const;

        [[nodiscard]] filetypes::xg::Matrix evaluate_xg_matrix( const filetypes::xg::Model &model,
            const std::string &matrix_name,
            float animation_frame,
            bool include_parent ) const;

        [[nodiscard]] filetypes::xg::Matrix evaluate_xg_matrix_blended( const filetypes::xg::Model &model,
            const std::string &matrix_name,
            const ResolvedAnimation &animation,
            bool include_parent ) const;

        [[nodiscard]] filetypes::xg::Matrix evaluate_mesh_local_matrix( const DrawModel &model,
            const DrawMesh &mesh,
            const ResolvedAnimation &animation ) const;

        [[nodiscard]] std::vector<filetypes::xg::Matrix> build_gpu_skin_matrices( const DrawModel &model,
            const DrawMesh &mesh,
            const ResolvedAnimation &animation ) const;

        std::uint32_t gpu_skin_bone_index( DrawModel &model, const std::string &bone_name );
        [[nodiscard]] bool xg_rigid_animation_enabled() const;
        [[nodiscard]] bool cpu_skinning_enabled() const;
        [[nodiscard]] bool gpu_skinning_enabled() const;
        [[nodiscard]] std::string animation_mode_name() const;
        [[nodiscard]] std::string model_rotation_mode_name() const;
        [[nodiscard]] std::size_t model_rotation_mode_index() const;
        [[nodiscard]] std::string xg_rotation_mode_name() const;
        [[nodiscard]] std::string xg_rotation_mode_name( XgRotationMode mode ) const;
        [[nodiscard]] std::string skinning_formula_mode_name() const;
        [[nodiscard]] std::string skinning_formula_mode_name( SkinningFormulaMode mode ) const;
        [[nodiscard]] std::string model_basis_mode_name() const;
        [[nodiscard]] std::string model_basis_mode_name( ModelBasisMode mode ) const;
        [[nodiscard]] std::size_t model_basis_mode_index() const;
        [[nodiscard]] std::size_t xg_rotation_mode_index() const;
        [[nodiscard]] std::vector<filetypes::xg::Vertex> resolve_geometry_vertices( const filetypes::xg::Model &model,
            const filetypes::xg::Geometry &geometry,
            float animation_frame ) const;

        [[nodiscard]] std::vector<GpuVertex> build_mesh_vertices( const DrawModel &model,
            const DrawMesh &mesh,
            const ResolvedAnimation &animation ) const;

        void build_camera_presetup_ranges( const gamedata::PlayableStageEntry &stage );
        [[nodiscard]] const CameraPresetupRange *active_camera_range( std::size_t *range_index = nullptr ) const;
        [[nodiscard]] float effective_stage_frame() const;
        [[nodiscard]] float animation_global_frame() const;
        [[nodiscard]] filetypes::ssq::Camera sample_camera() const;
        [[nodiscard]] SceneLighting sample_scene_lighting( float frame ) const;
        [[nodiscard]] float default_gmlt_light_strength() const;
        [[nodiscard]] bool should_draw_stage_sky() const;
        void resolve_stage_sky_texture();
        void draw_stage_sky( App &app, rendering::Renderer &renderer, std::int32_t width, std::int32_t height );
        [[nodiscard]] filetypes::xg::Matrix make_camera_view_matrix( const filetypes::ssq::Camera &camera ) const;
        [[nodiscard]] std::string camera_rotation_mode_name() const;
        [[nodiscard]] std::string camera_rotation_mode_name( CameraRotationMode mode ) const;
        [[nodiscard]] std::size_t camera_rotation_mode_index() const;
        [[nodiscard]] std::string camera_time_scale_name() const;
        void refresh_debug_camera_line();
        void refresh_material_debug_status_line();
        [[nodiscard]] std::string material_debug_mode_name() const;
        [[nodiscard]] std::string lighting_backend_name() const;
        [[nodiscard]] std::string gmlt_debug_mode_name() const;
        [[nodiscard]] std::string gmlt_direction_mode_name() const;
        [[nodiscard]] filetypes::xg::Vec3 gmlt_direction_basis() const;
        [[nodiscard]] bool gmlt_direction_uses_inverse_quat() const;
        [[nodiscard]] bool gmlt_direction_uses_raw_quat() const;
        [[nodiscard]] std::string depth_mode_name() const;
        [[nodiscard]] float effective_projection_near( const filetypes::ssq::Camera &camera ) const;
        [[nodiscard]] const DrawMesh *find_debug_mesh( std::size_t debug_id ) const;
        [[nodiscard]] std::size_t material_debug_mesh_count() const;
        [[nodiscard]] std::vector<std::pair<std::string, managers::TextureHandle>> texture_debug_entries() const;
        [[nodiscard]] filetypes::xg::Matrix model_basis_matrix() const;
        [[nodiscard]] filetypes::xg::Matrix make_projection_matrix( std::int32_t width,
            std::int32_t height,
            const filetypes::ssq::Camera &camera ) const;

        [[nodiscard]] filetypes::xg::Matrix make_standard_projection_matrix( std::int32_t width,
            std::int32_t height,
            const filetypes::ssq::Camera &camera ) const;

        [[nodiscard]] filetypes::xg::Matrix make_reverse_z_projection_matrix( std::int32_t width,
            std::int32_t height,
            const filetypes::ssq::Camera &camera ) const;

        [[nodiscard]] filetypes::xg::Matrix make_mvp( const filetypes::xg::Matrix &projection,
            const filetypes::xg::Matrix &view,
            const filetypes::xg::Matrix &model ) const;

        bool make_snake_linked_transforms( const filetypes::ssq::ModelEntry &entry,
            float frame,
            std::vector<filetypes::xg::Matrix> &chain_transforms,
            filetypes::xg::Matrix &end_transform ) const;

        static std::string normalize_texture_name( std::string text );
        static std::string normalize_model_name( std::string text );

    private:
        bool loaded_ = false;
        bool gl_initialized_ = false;
        std::string status_line_;
        std::string debug_camera_line_;
        std::string material_debug_status_line_;
        MaterialDebugMode material_debug_mode_ = MaterialDebugMode::MultipassAll;
        bool material_debug_focus_selected_ = false;
        std::size_t selected_material_debug_id_ = 0;
        std::size_t next_material_debug_id_ = 0;
        bool texture_debug_overlay_visible_ = false;
        std::size_t selected_texture_debug_index_ = 0;
        TextureDebugView texture_debug_view_ = TextureDebugView::Normal;
        filetypes::xg::Vec3 camera_debug_local_offset_{};
        double camera_debug_pullback_seconds_ = 0.0;
        float stage_frame_ = 0.0f;
        std::size_t stage_section_index_ = 0;
        float stage_section_frame_ = 0.0f;
        float stage_tempo_ = 120.0f;
        float portable_song_animation_frame_ = 0.0f;
        bool portable_visuals_ = false;
        bool stage_result_active_ = false;
        bool stage_result_won_ = true;
        float stage_result_elapsed_frame_ = 0.0f;
        CameraPresetupRange win_a_range_{};
        CameraPresetupRange win_b_range_{};
        CameraPresetupRange lose_pre_range_{};
        CameraPresetupRange lose_post_range_{};
        ActionAnimationInput action_input_{};
        std::array<ActionAnimationInput, 4> actor_action_inputs_{};
        ActionAnimationInput frozen_action_debug_input_{};
        double frozen_action_debug_seconds_ = 0.0;
        double frame_delta_seconds_ = 1.0 / 60.0;
        float player_pose_aim_angle_radians_ = 0.0f;
        bool player_pose_aim_active_ = false;
        std::vector<ActionAnimationState> action_states_{};
        std::vector<EffectPlaybackState> effect_states_{};
        std::uint64_t next_effect_activation_serial_ = 1;
        bool action_event_flag_override_enabled_ = false;
        std::uint32_t action_event_flag_override_ = 0;
        float camera_frame_offset_ = 0.0f;
        float camera_time_scale_ = 1.0f;
        CameraRotationMode camera_rotation_mode_ = CameraRotationMode::RawInverseOpenGlForward;
        AnimationMode animation_mode_ = AnimationMode::GpuSkinning;
        ModelRotationMode model_rotation_mode_ = ModelRotationMode::RawXzyNegZInverse;
        XgRotationMode xg_rotation_mode_ = XgRotationMode::ZReflectInverse;
        SkinningFormulaMode skinning_formula_mode_ = SkinningFormulaMode::PoseTimesRest;
        ModelBasisMode model_basis_mode_ = ModelBasisMode::Identity;
        bool render_special_model_types_ = true;
        std::int32_t stage_number_ = -1;
        bool stage_show_sky_flag_ = false;
        bool debug_stage_sky_enabled_ = true;
        managers::TextureHandle sky_texture_{};
        std::string sky_texture_name_;
        float scene_lighting_strength_ = 1.00f;
        float scene_lighting_floor_ = 0.00f;
        static constexpr float stage_gmlt_light_strength_ = 0.375f;
        static constexpr float tutorial_gmlt_light_strength_ = 1.0f;
        float gmlt_light_strength_ = stage_gmlt_light_strength_;
        GmltDebugMode gmlt_debug_mode_ = GmltDebugMode::All;
        GmltDirectionMode gmlt_direction_mode_ = GmltDirectionMode::InversePositiveZ;
        float ps2_light_wrap_ = 0.00f;
        float cel_shadow_threshold_ = 0.55f;
        float cel_shadow_strength_ = 0.58f;
        float cel_edge_strength_ = 0.0f;
        LightingBackend lighting_backend_ = LightingBackend::VertexPs2;
        DepthMode depth_mode_ = DepthMode::ReverseZ;
        float minimum_projection_near_ = 5.0f;
        filetypes::ssq::Scene scene_{};
        std::vector<CameraPresetupRange> camera_ranges_;

        std::vector<managers::TextureHandle> texture_handles_;
        std::unordered_map<std::string, managers::TextureHandle> texture_by_name_;
        std::unordered_map<GLuint, TextureSamplerState> texture_sampler_states_;
        std::vector<RuntimeTextureAnimation> runtime_texture_animations_;
        std::vector<RuntimeFixedSprite> runtime_fixed_sprites_;
        std::unordered_map<std::string, DrawModel> models_;
        std::vector<SceneInstance> instances_;

        rendering::Shader shader_;
        GLuint vao_ = 0;
        GLuint vbo_ = 0;
        GLuint ebo_ = 0;
    };
}
