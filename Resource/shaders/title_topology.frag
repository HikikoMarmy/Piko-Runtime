#version 330 core
in vec2 v_uv;

uniform vec2 u_resolution;
uniform float u_time;

uniform vec3 u_base_color;
uniform vec3 u_fill_color;
uniform vec3 u_line_color;

uniform float u_speed;
uniform float u_contour_scale;
uniform float u_contour_count;
uniform float u_line_width;
uniform float u_line_softness;
uniform float u_line_alpha;
uniform float u_glow;
uniform float u_vignette;
uniform float u_grain;

out vec4 frag_color;

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float smooth_height(vec2 p, float t)
{
    // Build the contour field from sine waves.
    float h = 0.0;
    h += sin(p.x * 1.72 + p.y * 0.84 + t * 1.20) * 0.34;
    h += sin(p.x * -0.93 + p.y * 1.61 - t * 0.82) * 0.27;
    h += sin(length(p + vec2(sin(t * 0.17), cos(t * 0.13)) * 1.7) * 2.08 - t * 0.66) * 0.24;
    h += sin((p.x + p.y) * 2.57 + t * 0.31) * 0.12;
    return h * 0.5 + 0.5;
}

void main()
{
    vec2 uv = v_uv;
    vec2 aspect = vec2(u_resolution.x / max(u_resolution.y, 1.0), 1.0);
    vec2 p = (uv - 0.5) * aspect * max(u_contour_scale, 0.001);

    float t = u_time * u_speed;
    p += vec2(t * 1.65, -t * 0.92);

    float h = smooth_height(p, u_time * max(abs(u_speed), 0.001));

    float contour_count = max(u_contour_count, 1.0);
    float contour_phase = abs(fract(h * contour_count) - 0.5);
    float line = 1.0 - smoothstep(u_line_width, u_line_width + max(u_line_softness, 0.0001), contour_phase);

    // Add the faint second contour layer.
    float h2 = smooth_height(p * 0.64 + vec2(2.1, -1.4), u_time * max(abs(u_speed), 0.001) * -0.72);
    float fine_phase = abs(fract(h2 * contour_count * 0.55) - 0.5);
    float fine_line = 1.0 - smoothstep(u_line_width * 0.70, u_line_width * 0.70 + max(u_line_softness, 0.0001), fine_phase);

    float vertical_gradient = smoothstep(0.0, 1.0, uv.y);
    vec3 color = mix(u_base_color, u_fill_color, vertical_gradient * 0.72 + h * 0.18);

    float line_mix = clamp(line * u_line_alpha + fine_line * u_line_alpha * 0.22, 0.0, 1.0);
    color = mix(color, u_line_color, line_mix);

    float glow = (line * 0.55 + fine_line * 0.14) * u_glow;
    color += u_line_color * glow * 0.18;

    vec2 centered = uv * 2.0 - 1.0;
    float vignette = smoothstep(0.15, 1.22, dot(centered, centered));
    color *= 1.0 - vignette * u_vignette;

    float grain = (hash12(gl_FragCoord.xy + vec2(u_time * 23.0, -u_time * 11.0)) - 0.5) * u_grain;
    color += grain;

    frag_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
