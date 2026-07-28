#version 330
in float rgb;
in vec3 position;
out vec4 v_color;
uniform mat4 camera;
uniform float point_scale;
uniform float fade_alpha; // 1.0 = fully shown; <1 = stochastic dissolve of this draw range (LOD fade-in)
void main() {
    v_color = vec4(vec3(rgb), 1.0);
    gl_Position = camera * vec4(position.xyz, 1.0 );
    gl_PointSize = clamp(point_scale / gl_Position.w, 1.0, 64.0);
    // Screen-door fade: cull this point unless its stable per-point hash falls under fade_alpha. As the
    // node reveals a finer LOD level, fade_alpha eases 0->1 so the level's points dissolve in (no pop). The
    // hash is a function of the buffer index only, so the dissolve pattern is steady across frames (no
    // flicker). Integer bit-mix (lowbias32) rather than sin() -- exact and well-distributed for indices up
    // to a node's ~200k points, where fract(sin(x)) aliases into visible banding.
    if (fade_alpha < 1.0) {
        uint hh = uint(gl_VertexID);
        hh ^= hh >> 16; hh *= 0x7feb352du; hh ^= hh >> 15; hh *= 0x846ca68bu; hh ^= hh >> 16;
        float h = float(hh) / 4294967296.0;
        if (h > fade_alpha) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // outside NDC -> clipped (culls the point robustly)
            gl_PointSize = 0.0;
        }
    }
}
