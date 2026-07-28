#version 330
in float rgb;
in vec3 position;
in float rep_level;   // u8 normalized [0,1]; *255 = the point's morton representative level
out vec4 v_color;
uniform mat4 camera;
uniform float point_scale;
uniform float lod_px_scale;      // projection[1][1] * 0.5 * viewport_height
uniform float lod_density_scale; // render_density_px / tree_config.scale
void main() {
    v_color = vec4(vec3(rgb), 1.0);
    gl_Position = camera * vec4(position.xyz, 1.0 );
    gl_PointSize = clamp(point_scale / gl_Position.w, 1.0, 64.0);
    // Per-point LOD: keep this point iff it is coarse enough for ITS OWN distance. gl_Position.w is the
    // view-space depth (~distance). cells = grid cells that project to the target px; W = log2(cells) is the
    // grid level; a point is kept when its rep_level >= W (smooth screen-door over the boundary level). This
    // is decided per point, so a node spanning near->far renders near-dense and far-coarse automatically.
    float rep = rep_level * 255.0;
    float depth = gl_Position.w;
    float cells = lod_density_scale * depth / max(lod_px_scale, 1e-6);
    float W = log2(max(cells, 1e-9));
    float alpha = clamp(rep - W + 1.0, 0.0, 1.0);
    // stable per-point hash (lowbias32 of the buffer index) -> a steady screen-door, no flicker.
    uint hh = uint(gl_VertexID);
    hh ^= hh >> 16; hh *= 0x7feb352du; hh ^= hh >> 15; hh *= 0x846ca68bu; hh ^= hh >> 16;
    float hf = float(hh) / 4294967296.0;
    if (hf > alpha) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // outside NDC -> clipped (culls the point)
        gl_PointSize = 0.0;
    }
}
