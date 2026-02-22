#version 330
in vec2 vertex;
uniform mat4 inverse_vp;
out vec3 direction;
void main() {
    vec4 ws = inverse_vp * vec4(vertex, 1.0, 1.0);
    direction = ws.xyz / ws.w;
    gl_Position = vec4(vertex, 1.0, 1.0);
}
