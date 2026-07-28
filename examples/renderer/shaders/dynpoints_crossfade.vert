#version 330
in vec3 position;
in vec3 rgb;       // new color (mono arrives as (val,0,0))
in vec3 old_rgb;   // old color (mono arrives as (val,0,0))
out vec4 v_color;
uniform mat4 camera;
uniform float point_scale;
uniform vec4 params; // x=node fade_alpha, y=blend, z=old_is_mono, w=new_is_mono
uniform float lod_fade_alpha; // screen-door coverage of the finest LOD level (matches the steady pass)
void main() {
    vec3 new_c = (params.w > 0.5) ? vec3(rgb.x) : rgb;
    vec3 old_c = (params.z > 0.5) ? vec3(old_rgb.x) : old_rgb;
    vec3 color = mix(old_c, new_c, params.y);
    v_color = vec4(color, params.x);
    gl_Position = camera * vec4(position.xyz, 1.0);
    gl_PointSize = clamp(point_scale / gl_Position.w, 1.0, 64.0);
    // Same screen-door LOD split as the steady shader so a node keeps its density across the fade<->steady
    // transition (drawn only for [solid, draw_size); the opaque prefix is drawn with lod_fade_alpha=1).
    if (lod_fade_alpha < 1.0) {
        uint hh = uint(gl_VertexID);
        hh ^= hh >> 16; hh *= 0x7feb352du; hh ^= hh >> 15; hh *= 0x846ca68bu; hh ^= hh >> 16;
        float h = float(hh) / 4294967296.0;
        if (h > lod_fade_alpha) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            gl_PointSize = 0.0;
        }
    }
}
