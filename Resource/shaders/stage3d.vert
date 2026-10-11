#version 330 core
layout (location = 0) in vec3 a_position;
layout (location = 1) in vec2 a_uv;
layout (location = 2) in vec4 a_color;
layout (location = 3) in vec3 a_normal;
layout (location = 4) in vec4 a_bone_indices;
layout (location = 5) in vec4 a_bone_weights;

uniform mat4 u_mvp;
uniform mat4 u_model_view;
uniform mat4 u_mesh_matrix;
uniform mat3 u_normal_matrix;
uniform mat3 u_env_view_rotation;
uniform int u_texture_env;
uniform int u_gpu_skinning;
uniform mat4 u_skin_matrices[96];
uniform vec3 u_scene_ambient;
uniform vec3 u_scene_diffuse;
uniform vec3 u_light_direction;
uniform int u_gmlt_light_count;
uniform vec3 u_gmlt_light_direction[8];
uniform vec3 u_gmlt_light_color[8];
uniform vec3 u_gmlt_light_secondary[8];
uniform float u_scene_lighting_strength;
uniform float u_scene_lighting_floor;
uniform float u_gmlt_light_strength;
uniform float u_ps2_light_wrap;

out vec2 v_uv;
out vec4 v_vertex_color;
out vec3 v_normal;
out vec3 v_ps2_lighting;

float ps2_wrapped_lambert(vec3 normal, vec3 light_direction)
{
    vec3 n = normalize(normal);
    vec3 l = normalize(light_direction);
    float wrap = clamp(u_ps2_light_wrap, 0.0, 0.85);
    float d = dot(n, l);

    // The retail renderer appears to be fixed/VU vertex lighting feeding GS IIP
    // colour interpolation, not modern hard per-pixel Lambert.  A small wrapped
    // Lambert term keeps authored stage lights directional, but stops skinned
    // character normals from falling to pitch-black triangle islands.
    return clamp((d + wrap) / (1.0 + wrap), 0.0, 1.0);
}

vec3 sample_gmlt_vertex_lighting(vec3 normal)
{
    vec3 lighting = vec3(0.0);

    for(int i = 0; i < 8; ++i)
    {
        if(i >= u_gmlt_light_count)
            break;

        float front = ps2_wrapped_lambert(normal, u_gmlt_light_direction[i]);
        float back = ps2_wrapped_lambert(normal, -u_gmlt_light_direction[i]);

        // Retail submits the two authored GMLT colours independently. They
        // light the front and back hemispheres of the directional light.
        lighting += u_gmlt_light_color[i] * front;
        lighting += u_gmlt_light_secondary[i] * back;
    }

    return lighting;
}

vec3 compute_ps2_vertex_lighting(vec3 normal)
{
    float ndotl = ps2_wrapped_lambert(normal, u_light_direction);
    float fill = clamp(u_ps2_light_wrap, 0.0, 0.85) * 0.20;
    vec3 raw_lighting = u_scene_ambient + u_scene_diffuse * (ndotl + fill);
    vec3 safe_lighting = max(clamp(raw_lighting, vec3(0.0), vec3(1.35)), vec3(u_scene_lighting_floor));

    // PS2/VU-style path: calculate the light factor at the vertex, then let
    // the rasterizer interpolate it across the triangle.  This is deliberately
    // not per-fragment lighting; it should look closer to GS IIP/Gouraud output
    // and make low-poly/skinned faces less sharply patchy.
    vec3 lighting_factor = mix(vec3(1.0), safe_lighting, clamp(u_scene_lighting_strength, 0.0, 1.0));
    lighting_factor += clamp(sample_gmlt_vertex_lighting(normal), vec3(0.0), vec3(1.5)) * u_gmlt_light_strength;
    return clamp(lighting_factor, vec3(0.0), vec3(2.0));
}

void main()
{
    vec4 local_position = vec4(a_position, 1.0);
    vec3 local_normal = a_normal;

    if(u_gpu_skinning != 0)
    {
        vec4 skinned_position = vec4(0.0);
        vec3 skinned_normal = vec3(0.0);
        float total_weight = 0.0;

        for(int i = 0; i < 4; ++i)
        {
            float weight = a_bone_weights[i];
            if(abs(weight) <= 0.000001)
                continue;

            int bone_index = int(a_bone_indices[i] + 0.5);
            mat4 skin_matrix = u_skin_matrices[bone_index];
            skinned_position += (skin_matrix * vec4(a_position, 1.0)) * weight;

            // For linear-blend skinning, do not inverse-transpose the already-blended
            // per-bone skin matrix here.  The PS2/XG skin matrices are effectively
            // bone-space pose transforms; inverse-transpose per influence exaggerates
            // tiny weight changes and produces faceted, triangular lighting on faces.
            // Blend the authored normal by the same bone matrices as the position, then
            // let u_normal_matrix handle the final model/world normal transform.
            skinned_normal += mat3(skin_matrix) * a_normal * weight;
            total_weight += weight;
        }

        if(total_weight > 0.000001)
        {
            local_position = skinned_position / total_weight;
            local_position.w = 1.0;
            local_normal = normalize(skinned_normal);
        }
    }

    vec4 mesh_position = u_mesh_matrix * local_position;
    gl_Position = u_mvp * mesh_position;
    v_uv = a_uv;
    v_vertex_color = a_color;
    vec3 world_normal = u_normal_matrix * local_normal;
    if(dot(world_normal, world_normal) <= 0.000001)
        world_normal = vec3(0.0, 1.0, 0.0);
    v_normal = normalize(world_normal);
    if(u_texture_env != 0)
    {
        vec3 view_normal = normalize(u_env_view_rotation * v_normal);
        if(u_texture_env == 1)
        {
            // XG textureGen mode 3: PS2 VU1 entry 0x220 reflects the eye ray
            // about the vertex normal, then sphere-maps that reflection. The
            // retail +Z pole becomes -Z in our OpenGL camera basis. Generate
            // coordinates at vertices, as the VU does, and interpolate them.
            vec3 eye_position = (u_model_view * mesh_position).xyz;
            vec3 eye_ray = eye_position / max(length(eye_position), 0.000001);
            vec3 sphere = reflect(eye_ray, view_normal) + vec3(0.0, 0.0, 1.0);
            v_uv = vec2(0.5) + sphere.xy * (0.5 / max(length(sphere), 0.000001));
        }
        else
            v_uv = vec2(0.5) - view_normal.xy * 0.5;
    }
    v_ps2_lighting = compute_ps2_vertex_lighting(v_normal);
}
