#version 330
in vec4 v_color;
out vec4 color;
void main() {
    float dist = length(gl_PointCoord - vec2(0.5)) * 2.0;
    if (dist > 1.0) discard;
    float edge = 1.0 - smoothstep(0.5, 1.0, dist) * 0.4;
    color = vec4(v_color.rgb * edge, v_color.a);
}
