#version 330
in vec3 direction;
out vec4 o_color;

uniform vec3 camera_pos; // xy = camera_pos mod grid_size, z = height above ground
uniform vec4 params;     // x = grid_size

void main() {
    vec3 dir = normalize(direction);

    float camera_height = camera_pos.z;
    float grid_size = params.x;

    // Sky colors
    vec3 horizon_color = vec3(0.75, 0.82, 0.90);
    vec3 zenith_color  = vec3(0.30, 0.50, 0.80);

    // Ground colors
    vec3 ground_color     = vec3(0.35, 0.38, 0.35);
    vec3 grid_line_color  = vec3(0.55, 0.58, 0.55);

    if (dir.z > 0.0) {
        // Sky: gradient from horizon to zenith
        float t = sqrt(clamp(dir.z, 0.0, 1.0));
        o_color = vec4(mix(horizon_color, zenith_color, t), 1.0);
    } else {
        // Ground plane at z = -camera_height relative to camera
        float t_hit = -camera_height / dir.z;

        if (t_hit < 0.0) {
            // Below the ground plane looking down — show sky
            float s = sqrt(clamp(-dir.z, 0.0, 1.0));
            o_color = vec4(mix(horizon_color, zenith_color, s), 1.0);
        } else {
            // Hit position relative to camera (small values, precise)
            vec3 hit_rel = dir * t_hit;
            float dist = t_hit; // |dir| = 1

            // Analytical derivatives of ground hit from stable ray direction
            vec3 ddx_dir = dFdx(dir);
            vec3 ddy_dir = dFdy(dir);
            float inv_dz = 1.0 / dir.z;
            // d(hit.xy)/dscreen = -h/dz * (d(dir.xy) - dir.xy/dz * d(dir.z))
            vec2 ddx_hit = -camera_height * inv_dz * (ddx_dir.xy - dir.xy * inv_dz * ddx_dir.z);
            vec2 ddy_hit = -camera_height * inv_dz * (ddy_dir.xy - dir.xy * inv_dz * ddy_dir.z);

            // Grid coordinates (camera_pos.xy is mod grid_size, so values stay small nearby)
            vec2 grid_coord = (camera_pos.xy + hit_rel.xy) / grid_size;
            vec2 ddx_gc = ddx_hit / grid_size;
            vec2 ddy_gc = ddy_hit / grid_size;

            // Analytically box-filtered grid (Inigo Quilez)
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
            float fog = smoothstep(0.0, 0.08, -dir.z);
            color = mix(horizon_color, color, fog);

            o_color = vec4(color, 1.0);
        }
    }
}
