#version 330 core
in vec2 v_uv;
uniform sampler2D u_texture;
uniform vec2 u_texel_direction;
out vec4 frag_color;

void main()
{
    vec3 color = texture(u_texture, v_uv).rgb * 0.227027;
    color += texture(u_texture, v_uv + u_texel_direction * 1.384615).rgb * 0.316216;
    color += texture(u_texture, v_uv - u_texel_direction * 1.384615).rgb * 0.316216;
    color += texture(u_texture, v_uv + u_texel_direction * 3.230769).rgb * 0.070270;
    color += texture(u_texture, v_uv - u_texel_direction * 3.230769).rgb * 0.070270;
    frag_color = vec4(color, 1.0);
}
