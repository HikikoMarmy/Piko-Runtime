#version 330 core
in vec2 v_uv;

uniform float u_time;
uniform float u_speed;
uniform vec3 u_band_color_0;
uniform vec3 u_band_color_1;
uniform vec3 u_band_color_2;
uniform vec3 u_band_color_3;

out vec4 frag_color;

vec3 band_color(float index)
{
    if(index < 0.5) return u_band_color_0;
    if(index < 1.5) return u_band_color_1;
    if(index < 2.5) return u_band_color_2;
    return u_band_color_3;
}

void main()
{
    float t = u_time * u_speed;
    float wave = v_uv.y
        + sin(v_uv.x * 7.54 + sin(v_uv.y * 5.0) * 0.3 + t * 0.8) * 0.092
        + sin(v_uv.x * 14.45 - v_uv.y * 3.2 - t * 0.5) * 0.044
        + sin(v_uv.x * 3.77 + v_uv.y * 11.0 + t * 0.3) * 0.028;
    float band = wave * 8.0 + 0.65;
    float current = mod(floor(band), 4.0);
    float previous = mod(current + 3.0, 4.0);
    float edge = smoothstep(0.0, max(fwidth(band) * 1.2, 0.018), fract(band));
    vec3 background = mix(band_color(previous), band_color(current), edge);

    frag_color = vec4(background, 1.0);
}
