#version 330 core
in vec2 v_uv;
in vec4 v_vertex_color;
in vec3 v_normal;
in vec3 v_ps2_lighting;

uniform sampler2D u_texture;
uniform int u_use_texture;
uniform int u_texture_env;
uniform int u_debug_mode;
uniform int u_shading_type;
uniform int u_blend_type;
uniform int u_alpha_active;
uniform float u_alpha_cutoff;
uniform int u_alpha_key_mode;
uniform int u_effect_alpha_as_luma;
uniform vec4 u_material_diffuse;
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
uniform int u_lighting_backend;
uniform float u_cel_shadow_threshold;
uniform float u_cel_shadow_strength;
uniform float u_cel_edge_strength;

out vec4 frag_color;

float ps2_wrapped_lambert_frag(vec3 normal, vec3 light_direction)
{
    vec3 n = normalize(normal);
    vec3 l = normalize(light_direction);
    float wrap = clamp(u_ps2_light_wrap, 0.0, 0.85);
    return clamp((dot(n, l) + wrap) / (1.0 + wrap), 0.0, 1.0);
}

vec3 sample_gmlt_lighting(vec3 normal)
{
    vec3 n = normalize(normal);
    vec3 lighting = vec3(0.0);

    for(int i = 0; i < 8; ++i)
    {
        if(i >= u_gmlt_light_count)
            break;

        float front = ps2_wrapped_lambert_frag(n, u_gmlt_light_direction[i]);
        float back = ps2_wrapped_lambert_frag(n, -u_gmlt_light_direction[i]);
        lighting += u_gmlt_light_color[i] * front;
        lighting += u_gmlt_light_secondary[i] * back;
    }

    return lighting;
}

vec3 apply_scene_lighting(vec3 base_rgb, vec3 raw_lighting)
{
    vec3 safe_lighting = max(clamp(raw_lighting, vec3(0.0), vec3(1.25)), vec3(u_scene_lighting_floor));
    vec3 lit_rgb = base_rgb * safe_lighting;
    return mix(base_rgb, lit_rgb, clamp(u_scene_lighting_strength, 0.0, 1.0));
}

vec3 apply_scene_lighting_with_gmlt(vec3 base_rgb, vec3 raw_lighting, vec3 gmlt_lighting)
{
    vec3 safe_lighting = max(clamp(raw_lighting, vec3(0.0), vec3(1.25)), vec3(u_scene_lighting_floor));
    vec3 lit_rgb = mix(base_rgb, base_rgb * safe_lighting, clamp(u_scene_lighting_strength, 0.0, 1.0));

    // Add local lights after applying the scene lighting floor.
    lit_rgb += base_rgb * clamp(gmlt_lighting, vec3(0.0), vec3(1.5)) * u_gmlt_light_strength;
    return clamp(lit_rgb, vec3(0.0), vec3(2.0));
}

float luminance(vec3 value)
{
    return dot(value, vec3(0.299, 0.587, 0.114));
}

vec3 apply_cel_lighting_with_gmlt(vec3 base_rgb, vec3 raw_lighting, vec3 gmlt_lighting)
{
    // Use two tones for cel shading.
    vec3 safe_lighting = max(clamp(raw_lighting, vec3(0.0), vec3(1.35)), vec3(u_scene_lighting_floor));
    vec3 gmlt = clamp(gmlt_lighting, vec3(0.0), vec3(1.5)) * u_gmlt_light_strength;

    float light_level = clamp(luminance(safe_lighting) + luminance(gmlt) * 0.65, 0.0, 1.5);
    float threshold = clamp(u_cel_shadow_threshold, 0.05, 0.95);
    float aa = max(fwidth(light_level) * 1.5, 0.010);
    float lit_band = smoothstep(threshold - aa, threshold + aa, light_level);
    float tone = mix(clamp(u_cel_shadow_strength, 0.05, 1.0), 1.0, lit_band);

    vec3 lit_rgb = base_rgb * tone;

    // Add coloured lights without quantising each channel.
    lit_rgb += base_rgb * gmlt * mix(0.08, 0.22, lit_band);

    return clamp(mix(base_rgb, lit_rgb, clamp(u_scene_lighting_strength, 0.0, 1.0)), vec3(0.0), vec3(2.0));
}

void main()
{
    vec2 uv = v_uv;

    vec4 texel = u_use_texture != 0 ? texture(u_texture, uv) : vec4(1.0);

    // Key opaque black or white pixels on masked materials.
    if(u_alpha_active != 0 && u_alpha_key_mode != 0)
    {
        float max_rgb = max(max(texel.r, texel.g), texel.b);
        float min_rgb = min(min(texel.r, texel.g), texel.b);
        float black_key = 1.0 - smoothstep(0.018, 0.070, max_rgb);
        float white_key = smoothstep(0.935, 0.990, min_rgb);
        float key = black_key;
        if(u_alpha_key_mode >= 2)
            key = max(key, white_key);

        float opaqueish = smoothstep(0.72, 0.98, texel.a);
        texel.a *= mix(1.0, 1.0 - key, opaqueish);
    }

    vec3 tex_rgb = texel.rgb;

    // Use alpha as brightness for dark effect textures.
    if(u_effect_alpha_as_luma != 0)
    {
        float luma = dot(tex_rgb, vec3(0.299, 0.587, 0.114));
        if(luma < 0.035 && texel.a > 0.01)
            tex_rgb = vec3(texel.a);
    }

    if(u_debug_mode == 1)
    {
        frag_color = vec4(tex_rgb, 1.0);
    }
    else if(u_debug_mode == 2)
    {
        frag_color = vec4(v_vertex_color.rgb, 1.0);
    }
    else if(u_debug_mode == 3)
    {
        float a = u_alpha_active != 0
            ? clamp(texel.a * v_vertex_color.a * u_material_diffuse.a, 0.0, 1.0)
            : 1.0;
        frag_color = vec4(a, a, a, 1.0);
    }
    else
    {
        vec3 rgb;
        if(u_shading_type == 3)
        {
            // Vertex colour, without lighting or material diffuse.
            rgb = tex_rgb * v_vertex_color.rgb;
        }
        else if(u_shading_type == 4)
        {
            // Lit texture colour, without material diffuse.
            rgb = tex_rgb;
            if(u_lighting_backend == 1)
            {
                rgb *= v_ps2_lighting;
            }
            else
            {
                vec3 n = normalize(v_normal);
                float ndotl = ps2_wrapped_lambert_frag(n, u_light_direction);
                float fill = clamp(u_ps2_light_wrap, 0.0, 0.85) * 0.20;
                vec3 base_lighting = u_scene_ambient + u_scene_diffuse * (ndotl + fill);
                vec3 gmlt_lighting = sample_gmlt_lighting(n);
                rgb = u_lighting_backend == 2
                    ? apply_cel_lighting_with_gmlt(rgb, base_lighting, gmlt_lighting)
                    : apply_scene_lighting_with_gmlt(rgb, base_lighting, gmlt_lighting);
            }
        }
        else
        {
            // shadingType 0: unlit diffuse.  shadingType 1/2: lit diffuse.
            vec3 material_rgb = u_material_diffuse.rgb;
            if(u_effect_alpha_as_luma != 0 && u_blend_type != 4)
            {
                float material_luma = dot(material_rgb, vec3(0.299, 0.587, 0.114));
                if(material_luma < 0.015)
                    material_rgb = vec3(1.0);
            }

            rgb = tex_rgb * material_rgb;
            if(u_shading_type == 1 || u_shading_type == 2)
            {
                if(u_lighting_backend == 1)
                {
                    // Apply interpolated vertex lighting.
                    rgb *= v_ps2_lighting;
                }
                else
                {
                    vec3 n = normalize(v_normal);
                    float ndotl = ps2_wrapped_lambert_frag(n, u_light_direction);
                    float fill = clamp(u_ps2_light_wrap, 0.0, 0.85) * 0.20;
                    vec3 base_lighting = u_scene_ambient + u_scene_diffuse * (ndotl + fill);
                    vec3 gmlt_lighting = sample_gmlt_lighting(n);
                    rgb = u_lighting_backend == 2
                        ? apply_cel_lighting_with_gmlt(rgb, base_lighting, gmlt_lighting)
                        : apply_scene_lighting_with_gmlt(rgb, base_lighting, gmlt_lighting);
                }
            }
        }

        // Darkening pass.
        if(u_blend_type == 4 && u_alpha_active == 0)
            rgb *= 0.125;

        // Clamp alpha before blending into HDR targets.
        float alpha = u_alpha_active != 0
            ? clamp(texel.a * v_vertex_color.a * u_material_diffuse.a, 0.0, 1.0)
            : 1.0;
        frag_color = vec4(rgb, alpha);
    }

    if(u_lighting_backend == 2 && u_cel_edge_strength > 0.001)
    {
        vec3 n = normalize(v_normal);
        float normal_change = length(fwidth(n));
        float edge = smoothstep(0.055, 0.22, normal_change * 6.0);
        frag_color.rgb *= 1.0 - edge * clamp(u_cel_edge_strength, 0.0, 0.95);
    }

    if(u_alpha_active != 0)
    {
        // Fade weak alpha before discarding transparent pixels.
        float hard_cutoff = 1.0 / 255.0;
        if(frag_color.a <= hard_cutoff)
            discard;

        float soft_cutoff = max(u_alpha_cutoff, hard_cutoff + 0.001);
        float alpha_ramp = smoothstep(hard_cutoff, soft_cutoff, frag_color.a);
        frag_color.a *= alpha_ramp;

        if(frag_color.a <= 0.001)
            discard;
    }
    else if(frag_color.a < 0.01)
        discard;
}

