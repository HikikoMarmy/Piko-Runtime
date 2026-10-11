#version 330 core
in vec2 v_uv;
uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform int u_bloom_enabled;
uniform float u_bloom_intensity;
out vec4 frag_color;

void main()
{
    vec3 color = clamp(texture(u_scene, v_uv).rgb, vec3(0.0), vec3(1.0));
    if(u_bloom_enabled != 0)
    {
        vec3 glow = clamp(texture(u_bloom, v_uv).rgb * u_bloom_intensity, vec3(0.0), vec3(1.0));
        color += glow * (vec3(1.0) - color);
    }
    frag_color = vec4(color, 1.0);
}
