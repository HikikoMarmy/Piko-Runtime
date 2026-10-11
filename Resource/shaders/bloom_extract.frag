#version 330 core
in vec2 v_uv;
uniform sampler2D u_scene;
uniform float u_threshold;
out vec4 frag_color;

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(u_scene, 0));
    vec3 color = texture(u_scene, v_uv).rgb * 0.25;
    color += texture(u_scene, v_uv + texel * vec2(-1.0, 0.0)).rgb * 0.125;
    color += texture(u_scene, v_uv + texel * vec2(1.0, 0.0)).rgb * 0.125;
    color += texture(u_scene, v_uv + texel * vec2(0.0, -1.0)).rgb * 0.125;
    color += texture(u_scene, v_uv + texel * vec2(0.0, 1.0)).rgb * 0.125;
    color += texture(u_scene, v_uv + texel * vec2(-1.0, -1.0)).rgb * 0.0625;
    color += texture(u_scene, v_uv + texel * vec2(1.0, -1.0)).rgb * 0.0625;
    color += texture(u_scene, v_uv + texel * vec2(-1.0, 1.0)).rgb * 0.0625;
    color += texture(u_scene, v_uv + texel * vec2(1.0, 1.0)).rgb * 0.0625;

    float brightness = max(max(color.r, color.g), color.b);
    float knee = max(u_threshold * 0.5, 0.0001);
    float soft = clamp(brightness - u_threshold + knee, 0.0, knee * 2.0);
    soft = soft * soft / (4.0 * knee);
    float contribution = max(brightness - u_threshold, soft) / max(brightness, 0.0001);
    vec3 highlight = max(color * contribution, vec3(0.0));
    frag_color = vec4(highlight / (vec3(1.0) + highlight), 1.0);
}
