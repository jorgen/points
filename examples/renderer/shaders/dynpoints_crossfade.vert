#version 330
in vec3 position;
in vec3 rgb;       // new color (mono arrives as (val,0,0))
in vec3 old_rgb;   // old color (mono arrives as (val,0,0))
in float rep_level; // u8 normalized [0,1]; *255 = the point's morton representative level
out vec4 v_color;
uniform mat4 camera;
uniform float point_scale;
uniform vec4 params; // x=node fade_alpha, y=blend, z=old_is_mono, w=new_is_mono
uniform float lod_px_scale;      // projection[1][1] * 0.5 * viewport_height
uniform float lod_density_scale; // render_density_px / tree_config.scale
void main() {
    vec3 new_c = (params.w > 0.5) ? vec3(rgb.x) : rgb;
    vec3 old_c = (params.z > 0.5) ? vec3(old_rgb.x) : old_rgb;
    vec3 color = mix(old_c, new_c, params.y);
    v_color = vec4(color, 1.0); // opaque -- the screen-door below carries both the node fade and the LOD
    gl_Position = camera * vec4(position.xyz, 1.0);
    gl_PointSize = clamp(point_scale / gl_Position.w, 1.0, 64.0);
    // Per-point LOD (same as the steady shader) multiplied by the node crossfade alpha (params.x), as one
    // stable screen-door: the node dissolves in/out AND keeps a consistent per-point density across the
    // fade<->steady transition. No alpha blending needed.
    float rep = rep_level * 255.0;
    float depth = gl_Position.w;
    float cells = lod_density_scale * depth / max(lod_px_scale, 1e-6);
    float W = log2(max(cells, 1e-9));
    float alpha = clamp(rep - W + 1.0, 0.0, 1.0) * params.x;
    uint hh = uint(gl_VertexID);
    hh ^= hh >> 16; hh *= 0x7feb352du; hh ^= hh >> 15; hh *= 0x846ca68bu; hh ^= hh >> 16;
    float hf = float(hh) / 4294967296.0;
    if (hf > alpha) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 0.0;
    }
}
