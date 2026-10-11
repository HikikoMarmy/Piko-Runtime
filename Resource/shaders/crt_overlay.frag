#version 330 core
in vec2 v_uv;
uniform vec2 u_screen_size;
uniform float u_scanline_strength;
uniform float u_vignette_strength;
uniform float u_noise_strength;
uniform float u_time;
out vec4 frag_color;

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main()
{
    float scan = sin((v_uv.y * u_screen_size.y + u_time * 22.0) * 3.14159265);
    scan = 0.5 + 0.5 * scan;
    float scan_alpha = (1.0 - scan) * clamp(u_scanline_strength, 0.0, 0.85);

    vec2 p = v_uv * 2.0 - vec2(1.0);
    float vignette = smoothstep(0.35, 1.18, dot(p, p));
    float vignette_alpha = vignette * clamp(u_vignette_strength, 0.0, 0.85);

    float noise = (hash12(gl_FragCoord.xy + vec2(u_time * 48.0, -u_time * 21.0)) - 0.5) * 2.0;
    float noise_alpha = abs(noise) * clamp(u_noise_strength, 0.0, 0.40);

    vec3 tint = vec3(0.0, 0.018, 0.040);
    float alpha = clamp(scan_alpha + vignette_alpha + noise_alpha, 0.0, 0.75);
    frag_color = vec4(tint, alpha);
}
