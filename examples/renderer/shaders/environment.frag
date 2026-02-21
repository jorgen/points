#version 330
in vec3 direction;
out vec4 o_color;

uniform vec3 camera_pos;
uniform vec4 params; // x = ground_z, y = grid_size

void main() {
    vec3 dir = normalize(direction);

    float ground_z = params.x;
    float grid_size = params.y;

    // Sky colors
    vec3 horizon_color = vec3(0.75, 0.82, 0.90);
    vec3 zenith_color  = vec3(0.30, 0.50, 0.80);

    // Ground colors
    vec3 ground_color     = vec3(0.35, 0.38, 0.35);
    vec3 grid_line_color  = vec3(0.55, 0.58, 0.55);

    if (dir.z > 0.0) {
        // Sky: gradient from horizon to zenith
        float t = clamp(dir.z, 0.0, 1.0);
        // Ease the gradient so it's more gradual near the horizon
        t = sqrt(t);
        vec3 sky = mix(horizon_color, zenith_color, t);
        o_color = vec4(sky, 1.0);
    } else {
        // Ground plane intersection
        // Ray: P = camera_pos + t * dir
        // Solve for z = ground_z: t = (ground_z - camera_pos.z) / dir.z
        float t_hit = (ground_z - camera_pos.z) / dir.z;

        if (t_hit < 0.0) {
            // Looking up from below the ground plane — show sky
            float s = clamp(-dir.z, 0.0, 1.0);
            s = sqrt(s);
            o_color = vec4(mix(horizon_color, zenith_color, s), 1.0);
        } else {
            vec3 hit = camera_pos + dir * t_hit;
            float dist = length(hit - camera_pos);

            // Analytically box-filtered grid (Inigo Quilez)
            vec2 grid_coord = hit.xy / grid_size;
            vec2 ddx_gc = dFdx(grid_coord);
            vec2 ddy_gc = dFdy(grid_coord);
            vec2 w = max(abs(ddx_gc), abs(ddy_gc)) + 0.01;
            float N = 20.0; // line thinness: lines are 1/N of a cell wide
            vec2 a = grid_coord + 0.5 * w;
            vec2 b = grid_coord - 0.5 * w;
            vec2 i = (floor(a) + min(fract(a) * N, 1.0) -
                      floor(b) - min(fract(b) * N, 1.0)) / (N * w);
            float grid_mask = i.x + i.y - i.x * i.y;

            // Distance fade: grid dissolves toward horizon
            float fade_start = grid_size * 5.0;
            float fade_end   = grid_size * 80.0;
            float dist_fade = 1.0 - clamp((dist - fade_start) / (fade_end - fade_start), 0.0, 1.0);

            grid_mask *= dist_fade;

            vec3 color = mix(ground_color, grid_line_color, grid_mask);

            // Horizon fog: blend to horizon color at grazing angles
            float elevation = clamp(-dir.z, 0.0, 1.0);
            float fog = smoothstep(0.0, 0.08, elevation);
            color = mix(horizon_color, color, fog);

            o_color = vec4(color, 1.0);
        }
    }
}
