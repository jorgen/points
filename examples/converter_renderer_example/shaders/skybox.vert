#version 330
in vec2 vertex;
uniform vec3 camera_pos;
uniform mat4 inverse_vp;
out vec3 direction;
void main() {
    vec4 ws_position = inverse_vp * vec4(vertex, 1.0, 1.0);
    ws_position /= ws_position.w;
    direction = vec3(ws_position) - camera_pos;
    gl_Position = vec4(vertex, 1.0, 1.0);
}
